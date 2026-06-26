#include <stdio.h>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

// Include all modules generated in the previous project chunks
#include "ESP32P4_AudioProcessor_Core0.hpp"
#include "Core1_Decimator.hpp"
#include "Core1_PitchTracker.hpp"
#include "Core1_ASRC.hpp"
#include "Core1_NotificationDucker.hpp"
#include "TI_Hardware_Config.hpp"
#include "ESP32P4_TDM_Driver.hpp"
#include "InterCore_BufferExchange.hpp"
#include "Core1_SPISlave_Parser.hpp"

static const char* TAG = "MAIN_AUDIO_SYS";

// Statically allocate system structures inside global memory namespaces
CompleteProcessorPayload globalControls;
InfrasonicCoefficients   globalInfraCfg;
InfrasonicRuntimeState   globalInfraState;

// Static allocation arrays for physical TDM channel processing tracks
ESP32Biquad roomCompPair1[4];
ESP32Biquad roomCompPair2[4];
ESP32Biquad p1PreFilter;

// Core 1 Management Class Instances
Core1Decimator         decimator;
Core1PitchTracker      pitchTracker;
Core1ASRC              asrc;
Core1NotificationDucker ducker;
ESP32P4_NVS_Manager    nvsManager;
Core1SPISlaveParser    spiParser;

// Pin assignment handles for the physical I2C initialization
i2c_master_bus_handle_t i2c_bus_handle = nullptr;

// Task pinned directly to Core 1 handling NVS storage and SPI updates
void core1_control_task(void* pvParameters) {
    ESP_LOGI(TAG, "Initializing Core 1 Peripheral Control Tasks...");
    
    // Initialize storage and communication hardware modules
    nvsManager.initialize();
    if (spiParser.initSlaveDriver() != ESP_OK) {
        ESP_LOGE(TAG, "Fatal Error: SPI Slave Initialization Failed!");
        vTaskDelete(NULL);
        return;
    }

    // Load saved settings directly into active operational state on boot
    globalControls.enableMask0 = nvsManager.workingPreset.activeEnableMask0;
    globalControls.enableMask1 = nvsManager.workingPreset.activeEnableMask1;
    
    TickType_t lastWakeTime = xTaskGetTickCount();
    uint32_t msCounter = 0;

    while (1) {
        // Execute block call waiting for host SPI register updates
        spiParser.handleIncomingTransaction(globalControls, nvsManager);

        // Track a local timer to tick the NVS manager exactly once per second
        msCounter += 10; // Approx loop cycle tracking
        if (msCounter >= 1000) {
            nvsManager.processBackgroundStorageTick();
            msCounter = 0;
        }

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(10));
    }
}

// Task pinned to Core 0 handling real-time audio block execution
void core0_audio_task(void* pvParameters) {
    ESP_LOGI(TAG, "Starting Hard Real-Time Audio Engine on Core 0...");
    
    ESP32P4TDMDriver tdmDriver;
    if (tdmDriver.initTDM16_192kHz() != ESP_OK) {
        ESP_LOGE(TAG, "Fatal Error: TDM Transmit Peripheral Configuration Failed!");
        vTaskDelete(NULL);
        return;
    }

    const int blockSize = 128; // Hard real-time audio block granularity boundary
    size_t bytesTransferred = 0;

    // Local static alignment buffers for intermediate block signal tracks
    float txBuffer[blockSize] = {0.0f};
    float rxBuffer[blockSize] = {0.0f};

    // Statically layout multidimensional pointer tracking maps for pipeline integration
    float* outChannels = { txBuffer, txBuffer + 32, txBuffer + 64, txBuffer + 96 };
    float* inChannels  = { rxBuffer, rxBuffer + 32, rxBuffer + 64, rxBuffer + 96 };

    while (1) {
        // Core 0 blocking read call waiting on high-speed hardware TDM RX DMA slots
        i2s_channel_read(tdmDriver.rx_handle, rxBuffer, sizeof(rxBuffer), &bytesTransferred, portMAX_DELAY);

        // Core 0 executes real-time 192kHz biquad processing and mixing pipeline inline
        processCompleteCore0Pipeline(outChannels, 4, 32, inChannels, 4,
                                     globalControls, globalInfraCfg, globalInfraState,
                                     roomCompPair1, roomCompPair2, p1PreFilter);

        // Feed decimated real-time monitoring tracks directly to Core 1 analytics queue
        for (int s = 0; s < 32; ++s) {
            float decimatedMono = 0.0f;
            bool valid3kHz = false;
            float rawMono = (inChannels[0][s] + inChannels[1][s]) * 0.5f;
            
            decimator.processSample(rawMono, decimatedMono, valid3kHz);
            if (valid3kHz) {
                pitchTracker.process3kHzSample(decimatedMono, globalInfraState.targetStepP1, globalInfraState.targetStepP2);
            }
        }

        // Core 0 structural write phase out to physical stacked PCM1795 converters
        i2s_channel_write(tdmDriver.tx_handle, txBuffer, sizeof(txBuffer), &bytesTransferred, portMAX_DELAY);
    }
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Initializing System Boot Sequence...");

    // 1. Initialize System I2C Master Bus configuration (ESP-IDF v5.3+)
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = GPIO_NUM_1; // Dedicated physical I2C SDA pin
    bus_config.scl_io_num = GPIO_NUM_2; // Dedicated physical I2C SCL pin
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.intr_priority = 0;
    
    if (i2c_new_master_bus(&bus_config, &i2c_bus_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Hardware Boot Failure: I2C Master Bus Init Failed.");
        return;
    }

    // 2. Transmit static register boot-strings to TI Converters over I2C
    if (TIHardwareConfig::configureHardwareConverters(i2c_bus_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Hardware Boot Failure: TI Burr-Brown Chips rejected configuration.");
        return;
    }
    ESP_LOGI(TAG, "TI PCM1822 ADCs and PCM1795 DACs validated online.");

    // 3. Initialize background decimation and upsampling classes
    decimator.init();

    // 4. Instantiate background tasks pinned uniquely to asymmetric cores
    // Core 1 Control Task (SPI communication, NVS logging, parameters)
    xTaskCreatePinnedToCore(
        core1_control_task,    // Task function
        "control_task",        // Task descriptive string identifier
        4096,                  // Stack size depth words
        NULL,                  // Parameter inputs
        10,                    // Medium execution task priority hierarchy
        NULL,                  // Task handle assignment identifier
        1                      // Pinned directly to Core 1 (Brain Core)
    );

    // Core 0 Hard Real-Time Audio Engine (Strict timing loop)
    xTaskCreatePinnedToCore(
        core0_audio_task,      // Task function
        "audio_task",          // Task descriptive string identifier
        8192,                  // Extended stack depth for real-time math
        NULL,                  // Parameter inputs
        24,                    // Max critical priority ceiling underneath system interrupts
        NULL,                  // Task handle assignment identifier
        0                      // Pinned strictly to Core 0 (Audio Muscle Core)
    );

    ESP_LOGI(TAG, "Asymmetric Dual-Core Architecture initialized successfully.");
}
