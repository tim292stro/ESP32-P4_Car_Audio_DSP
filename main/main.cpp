#include <stdio.h>
#include <cmath>
#include <cstring>
#include <algorithm>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_cpu.h"
#include "esp_log.h"

// Module includes split by responsibility:
// - Core0 real-time DSP pipeline
// - Core1 control/analysis helpers
// - Transport and hardware glue
#include "Core0.hpp"
#include "Core1_Decimator.hpp"
#include "Core1_PitchTracker.hpp"
#include "Core1_ASRC.hpp"
#include "Core1_NotificationDucker.hpp"
#include "Core1_PinkNoiseGenerator.hpp"
#include "Core1_PinkNoiseGainTrim.hpp"
#include "Audio_FrameIO.hpp"
#include "TI_Hardware_Config.hpp"
#include "ESP32P4_TDM_Driver.hpp"
#include "ESP32P4_SFX_I2S_Driver.hpp"
#include "InterCore_BufferExchange.hpp"
#include "Core1_SPISlave_Parser.hpp"

static const char* TAG = "MAIN_AUDIO_SYS";

// =============================
// Global Shared State
// =============================
// Shared runtime state.
// Global/static storage is deliberate to avoid dynamic allocation in real-time paths.
CompleteProcessorPayload globalControls;
InfrasonicCoefficients   globalInfraCfg;
InfrasonicRuntimeState   globalInfraState;

// Core1 helpers and managers.
Core1Decimator         primaryDecimator[4];
Core1PitchTracker      primaryPitchTracker[4];
Core1ASRC              asrc;
Core1NotificationDucker ducker;
Core1PinkNoiseGenerator pinkNoiseGen;
Core1PinkNoiseGainTrim  pinkNoiseTrim;
ESP32P4_NVS_Manager    nvsManager;
Core1SPISlaveParser    spiParser;

// I2C bus handle shared by boot-time hardware config and runtime DAC soft-mute writes.
i2c_master_bus_handle_t i2c_bus_handle = nullptr;

// Service selector differential inputs (DPDT with opposite pull scheme).
// The protection logic requires A/B disagreement as a valid state.
static constexpr gpio_num_t PROTECT_SEL_A_GPIO = GPIO_NUM_14;
static constexpr gpio_num_t PROTECT_SEL_B_GPIO = GPIO_NUM_15;
static constexpr uint32_t SERVICE_TIMEOUT_SECONDS = 120;

static void init_service_selector_gpio() {
    // Input-only configuration with both pulls to bias floating conditions toward deterministic reads.
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << PROTECT_SEL_A_GPIO) | (1ULL << PROTECT_SEL_B_GPIO);
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

// =============================
// Core1 Control Task
// =============================
// Core1 task: control-plane, persistence, service selector supervision, and background generators.
void core1_control_task(void* pvParameters) {
    ESP_LOGI(TAG, "Initializing Core 1 Peripheral Control Tasks...");

    init_service_selector_gpio();
    ProtectionSelectorConfig selectorCfg = {
        .samplePeriodMs = 10,
        .softwareDebounceMs = 500,
    };
    ProtectionSelectorState selectorState;
    uint32_t serviceSecondsRemaining = 0;
    bool serviceModeActive = false;
    bool lastDacMuteState = false;
    float lastDacVolumeGain = -1.0f;
    enum class PathSwitchTransitionState {
        IDLE,
        RAMP_DOWN,
        HOLD_FLOOR,
        RAMP_UP,
    };
    PathSwitchTransitionState pathSwitchState = PathSwitchTransitionState::IDLE;
    uint32_t pathSwitchStep = 0;
    static constexpr uint32_t kPathSwitchRampDownMs = 200;
    static constexpr uint32_t kPathSwitchHoldFloorMs = 200;
    static constexpr uint32_t kPathSwitchRampUpMs = 300;
    static constexpr uint32_t kControlTickMs = 10;
    static constexpr uint32_t kPathSwitchRampDownSteps = kPathSwitchRampDownMs / kControlTickMs;
    static constexpr uint32_t kPathSwitchHoldFloorSteps = kPathSwitchHoldFloorMs / kControlTickMs;
    static constexpr uint32_t kPathSwitchRampUpSteps = kPathSwitchRampUpMs / kControlTickMs;
    
    // Initialize persistence and SPI command interface before entering loop.
    nvsManager.initialize();
    if (spiParser.initSlaveDriver() != ESP_OK) {
        ESP_LOGE(TAG, "Fatal Error: SPI Slave Initialization Failed!");
        vTaskDelete(NULL);
        return;
    }

    // Initialize pink noise generator
    pinkNoiseGen.initialize();

    // Load persisted settings and synthesize runtime derivatives (composite EQ gains).
    globalControls.enableMask1 = (nvsManager.workingPreset.activeEnableMask1 & EnableMask1Bits::VALID_MASK);
    globalControls.primaryBypassMask = (nvsManager.workingPreset.activePrimaryBypassMask & PrimaryBypassMaskBits::VALID_MASK);
    globalControls.sfxBypassMask = (nvsManager.workingPreset.activeSfxBypassMask & SfxBypassMaskBits::VALID_MASK);
    globalControls.requestedEnableMask1 = globalControls.enableMask1;
    globalControls.requestedPrimaryBypassMask = globalControls.primaryBypassMask;
    globalControls.requestedSfxBypassMask = globalControls.sfxBypassMask;
    globalControls.requestedPinkNoiseSourceMask = globalControls.pinkNoiseSourceMask;
    globalControls.primaryInputGain = nvsManager.workingPreset.gainSettings[0];
    globalControls.sfxInputGain = nvsManager.workingPreset.gainSettings[1];
    globalControls.masterVolumeLinear = nvsManager.workingPreset.gainSettings[2];
    for (uint32_t ch = 0; ch < EqControlMap::kEqChannelCount; ++ch) {
        for (uint32_t band = 0; band < EqControlMap::kEqBandCountMax; ++band) {
            globalControls.eqBandGains[ch][band] = nvsManager.workingPreset.eqGains[ch][band];
        }
    }
    recomputeEqCompositeGainAll(globalControls);
    globalControls.crossoverHzReg0 = static_cast<uint32_t>(nvsManager.workingPreset.crossoverFreqs[0]) & 0xFFFFu;
    globalControls.crossoverHzReg1 = static_cast<uint32_t>(nvsManager.workingPreset.crossoverFreqs[1]) & 0xFFFFu;
    globalControls.crossoverHzReg2 = static_cast<uint32_t>(nvsManager.workingPreset.crossoverFreqs[2]) & 0xFFFFu;
    globalControls.protectLowCutHzReg = static_cast<uint32_t>(nvsManager.workingPreset.crossoverFreqs[3]) & 0xFFFFu;
    
    TickType_t lastWakeTime = xTaskGetTickCount();
    uint32_t msCounter = 0;

    while (1) {
        // Service one host SPI transaction.
        spiParser.handleIncomingTransaction(globalControls, nvsManager);

        const bool routeChangeRequested =
            (globalControls.requestedEnableMask1 != globalControls.enableMask1) ||
            (globalControls.requestedPrimaryBypassMask != globalControls.primaryBypassMask) ||
            (globalControls.requestedSfxBypassMask != globalControls.sfxBypassMask) ||
            (globalControls.requestedPinkNoiseSourceMask != globalControls.pinkNoiseSourceMask);

        if (pathSwitchState == PathSwitchTransitionState::IDLE && routeChangeRequested) {
            pathSwitchState = PathSwitchTransitionState::RAMP_DOWN;
            pathSwitchStep = 0;
        }

        const bool requestedDacMute = (globalControls.volumeMuteCtrl & 0x2u) != 0u;
        const float masterVolumeToDac = std::max(0.0f, std::min(1.0f, globalControls.masterVolumeLinear));
        auto computeDacGain = [&](float transitionGain01) {
            const float t = std::max(0.0f, std::min(1.0f, transitionGain01));
            return requestedDacMute ? 0.0f : (t * masterVolumeToDac);
        };

        if (pathSwitchState == PathSwitchTransitionState::RAMP_DOWN) {
            pathSwitchStep = std::min(pathSwitchStep + 1u, kPathSwitchRampDownSteps);
            const float t = static_cast<float>(pathSwitchStep) / static_cast<float>(kPathSwitchRampDownSteps);
            const float transitionMuteGain = std::max(0.0f, 1.0f - t);

            if (pathSwitchStep >= kPathSwitchRampDownSteps) {
                // Apply the route/source switch exactly at mute floor.
                globalControls.enableMask1 = globalControls.requestedEnableMask1;
                globalControls.primaryBypassMask = globalControls.requestedPrimaryBypassMask;
                globalControls.sfxBypassMask = globalControls.requestedSfxBypassMask;
                globalControls.pinkNoiseSourceMask = globalControls.requestedPinkNoiseSourceMask;

                pathSwitchState = PathSwitchTransitionState::HOLD_FLOOR;
                pathSwitchStep = 0;
            }

            const float dacTransitionGain = computeDacGain(transitionMuteGain);
            if (std::fabs(dacTransitionGain - lastDacVolumeGain) > 0.001f) {
                TIHardwareConfig::setPCM1795DigitalVolume(i2c_bus_handle, dacTransitionGain);
                lastDacVolumeGain = dacTransitionGain;
            }
        } else if (pathSwitchState == PathSwitchTransitionState::HOLD_FLOOR) {
            pathSwitchStep = std::min(pathSwitchStep + 1u, kPathSwitchHoldFloorSteps);

            const float dacTransitionGain = computeDacGain(0.0f);
            if (std::fabs(dacTransitionGain - lastDacVolumeGain) > 0.001f) {
                TIHardwareConfig::setPCM1795DigitalVolume(i2c_bus_handle, dacTransitionGain);
                lastDacVolumeGain = dacTransitionGain;
            }

            if (pathSwitchStep >= kPathSwitchHoldFloorSteps) {
                pathSwitchState = PathSwitchTransitionState::RAMP_UP;
                pathSwitchStep = 0;
            }
        } else if (pathSwitchState == PathSwitchTransitionState::RAMP_UP) {
            pathSwitchStep = std::min(pathSwitchStep + 1u, kPathSwitchRampUpSteps);
            const float t = static_cast<float>(pathSwitchStep) / static_cast<float>(kPathSwitchRampUpSteps);
            const float transitionMuteGain = std::min(1.0f, t);

            const float dacTransitionGain = computeDacGain(transitionMuteGain);
            if (std::fabs(dacTransitionGain - lastDacVolumeGain) > 0.001f) {
                TIHardwareConfig::setPCM1795DigitalVolume(i2c_bus_handle, dacTransitionGain);
                lastDacVolumeGain = dacTransitionGain;
            }

            if (pathSwitchStep >= kPathSwitchRampUpSteps) {
                pathSwitchState = PathSwitchTransitionState::IDLE;
                pathSwitchStep = 0;
            }
        } else {
            const float steadyGain = computeDacGain(1.0f);
            if (std::fabs(steadyGain - lastDacVolumeGain) > 0.001f) {
                TIHardwareConfig::setPCM1795DigitalVolume(i2c_bus_handle, steadyGain);
                lastDacVolumeGain = steadyGain;
            }
        }

        // Mirror host DAC mute bit to hardware soft-mute writes only on state change.
        if (requestedDacMute != lastDacMuteState) {
            TIHardwareConfig::setPCM1795SoftMute(i2c_bus_handle, requestedDacMute);
            lastDacMuteState = requestedDacMute;
        }

        // Debounced service selector handling and policy enforcement.
        const bool lineA = gpio_get_level(PROTECT_SEL_A_GPIO) != 0;
        const bool lineB = gpio_get_level(PROTECT_SEL_B_GPIO) != 0;
        updateProtectionSelector(selectorState, selectorCfg, lineA, lineB);

        const bool forceProtected = (globalControls.protectSelCtrl & (1u << 1)) != 0;
        if (forceProtected) {
            serviceModeActive = false;
            serviceSecondsRemaining = 0;
            globalControls.protectSelCtrl &= ~(1u << 0);
            globalControls.protectSelCtrl &= ~(1u << 1);
        }

        const bool serviceRequested = (globalControls.protectSelCtrl & (1u << 0)) != 0;
        const bool timeoutExpired = serviceModeActive && (serviceSecondsRemaining == 0);
        const bool bypassAllowed = serviceRequested &&
                                   protectionBypassPermitted(selectorState, true, timeoutExpired);

        if (bypassAllowed && !serviceModeActive) {
            serviceModeActive = true;
            serviceSecondsRemaining = SERVICE_TIMEOUT_SECONDS;
        }

        if (serviceModeActive && timeoutExpired) {
            serviceModeActive = false;
            globalControls.protectSelCtrl &= ~(1u << 0);
        }

        uint32_t status = 0;
        if (selectorState.validPair) {
            status |= (1u << 0);
        }
        if (!selectorState.isStable) {
            status |= (1u << 1);
        }
        if (serviceModeActive) {
            status |= (1u << 2);
        }
        if (timeoutExpired) {
            status |= (1u << 3);
        }
        globalControls.protectSelStat = status;
        globalControls.protectSelTimerSec = serviceSecondsRemaining;

        // 10 ms loop cadence also drives 1-second maintenance timers.
        msCounter += kControlTickMs; // Approx loop cycle tracking
        if (msCounter >= 1000) {
            nvsManager.processBackgroundStorageTick();
            if (serviceModeActive && serviceSecondsRemaining > 0) {
                --serviceSecondsRemaining;
            }
            msCounter = 0;
        }

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(kControlTickMs));
    }
}

// =============================
// Core0 Audio Task
// =============================
// Core0 task: hard real-time audio ingress -> DSP -> egress pipeline.
void core0_audio_task(void* pvParameters) {
    ESP_LOGI(TAG, "Starting Hard Real-Time Audio Engine on Core 0...");
    
    ESP32P4TDMDriver tdmDriver;
    ESP32P4SfxI2SDriver sfxI2SDriver;
    if (tdmDriver.initTDM16_192kHz() != ESP_OK) {
        ESP_LOGE(TAG, "Fatal Error: TDM Transmit Peripheral Configuration Failed!");
        vTaskDelete(NULL);
        return;
    }
    if (sfxI2SDriver.initSfxRx48kHzStereo() != ESP_OK) {
        ESP_LOGE(TAG, "Fatal Error: SFX I2S RX 48 kHz configuration failed!");
        vTaskDelete(NULL);
        return;
    }

    // Transport width tracks active 16-slot TDM frame.
    const int ioChannels = 16;
    const int dspChannels = 16;
    const int samplesPerBlock = 32;
    static_assert((32 % 4) == 0, "Core0 block size must remain divisible by 4 for 48k->192k ASRC.");
    const int sfxFrames48kPerBlock = samplesPerBlock / 4;
    const int sfxI2SChannels = 2;
    const int ioWordsPerBlock = ioChannels * samplesPerBlock;
    const int sfxWordsPerBlock = sfxI2SChannels * sfxFrames48kPerBlock;
    size_t bytesTransferred = 0;
    size_t sfxBytesTransferred = 0;

    uint64_t ingressCyclesAcc = 0;
    uint64_t dspCyclesAcc = 0;
    uint64_t egressCyclesAcc = 0;
    uint32_t blockCounter = 0;

    // Frame format: signed 24-bit PCM left-justified in 32-bit slots.
    int32_t rawTxWords[ioWordsPerBlock] = {0};
    int32_t rawRxWords[ioWordsPerBlock] = {0};
    int32_t rawSfxWords[sfxWordsPerBlock] = {0};

    float inPlanar[dspChannels][samplesPerBlock] = {{0.0f}};
    float outPlanar[dspChannels][samplesPerBlock] = {{0.0f}};
    float sfx48kPlanar[sfxI2SChannels][sfxFrames48kPerBlock] = {{0.0f}};
    float sfxAsrcL[samplesPerBlock] = {0.0f};
    float sfxAsrcR[samplesPerBlock] = {0.0f};

    constexpr int kSfxLeftLane = 4;
    constexpr int kSfxRightLane = 5;
    constexpr int kSfxUpsampleFactor = 4;
    constexpr uint32_t kPinkNoiseSrcValidMask = 0x3Fu;

    float* inChannels[dspChannels] = {
        inPlanar[0], inPlanar[1], inPlanar[2], inPlanar[3],
        inPlanar[4], inPlanar[5], inPlanar[6], inPlanar[7],
        inPlanar[8], inPlanar[9], inPlanar[10], inPlanar[11],
        inPlanar[12], inPlanar[13], inPlanar[14], inPlanar[15],
    };
    float* outChannels[dspChannels] = {
        outPlanar[0], outPlanar[1], outPlanar[2], outPlanar[3],
        outPlanar[4], outPlanar[5], outPlanar[6], outPlanar[7],
        outPlanar[8], outPlanar[9], outPlanar[10], outPlanar[11],
        outPlanar[12], outPlanar[13], outPlanar[14], outPlanar[15],
    };
    float* ioInChannels[ioChannels] = {
        inPlanar[0], inPlanar[1], inPlanar[2], inPlanar[3],
        inPlanar[4], inPlanar[5], inPlanar[6], inPlanar[7],
        inPlanar[8], inPlanar[9], inPlanar[10], inPlanar[11],
        inPlanar[12], inPlanar[13], inPlanar[14], inPlanar[15],
    };
    const float* ioOutChannels[ioChannels] = {
        outPlanar[0], outPlanar[1], outPlanar[2], outPlanar[3],
        outPlanar[4], outPlanar[5], outPlanar[6], outPlanar[7],
        outPlanar[8], outPlanar[9], outPlanar[10], outPlanar[11],
        outPlanar[12], outPlanar[13], outPlanar[14], outPlanar[15],
    };
    float* sfx48kChannels[sfxI2SChannels] = {
        sfx48kPlanar[0], sfx48kPlanar[1],
    };

    while (1) {
        // Block on RX DMA for next frame.
        i2s_channel_read(tdmDriver.rx_handle, rawRxWords, sizeof(rawRxWords), &bytesTransferred, portMAX_DELAY);

        uint32_t cycleStart = esp_cpu_get_cycle_count();
        std::memset(inPlanar, 0, sizeof(inPlanar));
        audio_frameio::deinterleaveS24In32ToFloat(rawRxWords, ioInChannels, ioChannels, samplesPerBlock);

        if (i2s_channel_read(sfxI2SDriver.rx_handle,
                             rawSfxWords,
                             sizeof(rawSfxWords),
                             &sfxBytesTransferred,
                             portMAX_DELAY) != ESP_OK ||
            sfxBytesTransferred != sizeof(rawSfxWords)) {
            std::memset(sfx48kPlanar, 0, sizeof(sfx48kPlanar));
        } else {
            audio_frameio::deinterleaveS24In32ToFloat(rawSfxWords, sfx48kChannels, sfxI2SChannels, sfxFrames48kPerBlock);
        }

        // SFX arrives from dedicated I2S at 48 kHz; upsample to 192 kHz before entering DSP routing.
        for (int s = 0; s < samplesPerBlock; ++s) {
            if ((s % kSfxUpsampleFactor) == 0) {
                const int sfx48kIndex = s / kSfxUpsampleFactor;
                asrc.pushNew48kHzFrame(sfx48kPlanar[0][sfx48kIndex], sfx48kPlanar[1][sfx48kIndex]);
            }
            asrc.generateNext192kHzFrame(sfxAsrcL[s], sfxAsrcR[s]);
        }

        if (ioChannels > kSfxRightLane) {
            for (int s = 0; s < samplesPerBlock; ++s) {
                inPlanar[kSfxLeftLane][s] = sfxAsrcL[s];
                inPlanar[kSfxRightLane][s] = sfxAsrcR[s];
            }
        }

        // Pink-noise replacement source select runs after ingress preparation and before DSP metering/clip paths.
        const uint32_t pinkSrcMask = (globalControls.pinkNoiseSourceMask & kPinkNoiseSrcValidMask);
        pinkNoiseGen.setEnabled(true); // Free-running by design; source mask gates replacement, not generator state.
        pinkNoiseGen.setGain(1.0f);
        pinkNoiseTrim.setGain(globalControls.pinkNoiseGainTrim);
        const int replaceChannels = std::min(ioChannels, 6);
        for (int s = 0; s < samplesPerBlock; ++s) {
            const float pink = pinkNoiseTrim.process(pinkNoiseGen.generateSample());
            if (pinkSrcMask != 0u) {
                for (int ch = 0; ch < replaceChannels; ++ch) {
                    if ((pinkSrcMask & (1u << ch)) != 0u) {
                        inPlanar[ch][s] = pink;
                    }
                }
            }
        }

        ingressCyclesAcc += static_cast<uint32_t>(esp_cpu_get_cycle_count() - cycleStart);

        // Execute full DSP pipeline for this frame.
        cycleStart = esp_cpu_get_cycle_count();
        processCompleteCore0Pipeline(outChannels, dspChannels, samplesPerBlock, inChannels, ioChannels,
                                     globalControls, globalInfraCfg, globalInfraState);
        dspCyclesAcc += static_cast<uint32_t>(esp_cpu_get_cycle_count() - cycleStart);

        // Feed analysis side path (decimated) for per-lane pitch tracking updates.
        for (int s = 0; s < samplesPerBlock; ++s) {
            const int analysisChannels = std::min(ioChannels, 4);
            for (int ch = 0; ch < analysisChannels; ++ch) {
                float decimated = 0.0f;
                bool valid3kHz = false;
                const float rawLane = inChannels[ch][s];

                primaryDecimator[ch].processSample(rawLane, decimated, valid3kHz);
                if (valid3kHz) {
                    primaryPitchTracker[ch].process3kHzSample(decimated,
                                                              globalInfraState.targetStepBass[ch],
                                                              globalInfraState.targetStepInfrasonic[ch]);
                }
            }
        }

        cycleStart = esp_cpu_get_cycle_count();
        audio_frameio::interleaveFloatToS24In32(ioOutChannels, rawTxWords, ioChannels, samplesPerBlock);
        egressCyclesAcc += static_cast<uint32_t>(esp_cpu_get_cycle_count() - cycleStart);

        ++blockCounter;
        if (blockCounter == 1000) {
            const uint32_t avgIngress = static_cast<uint32_t>(ingressCyclesAcc / blockCounter);
            const uint32_t avgDsp = static_cast<uint32_t>(dspCyclesAcc / blockCounter);
            const uint32_t avgEgress = static_cast<uint32_t>(egressCyclesAcc / blockCounter);
            const uint32_t avgTotal = avgIngress + avgDsp + avgEgress;
            const uint32_t avgPerSample = avgTotal / samplesPerBlock;
            ESP_LOGI(TAG,
                     "Core0 cycles avg/block ingress=%u dsp=%u egress=%u total=%u (~%u/sample)",
                     avgIngress,
                     avgDsp,
                     avgEgress,
                     avgTotal,
                     avgPerSample);
            ingressCyclesAcc = 0;
            dspCyclesAcc = 0;
            egressCyclesAcc = 0;
            blockCounter = 0;
        }

        // Publish processed frame to TX DMA.
        i2s_channel_write(tdmDriver.tx_handle, rawTxWords, sizeof(rawTxWords), &bytesTransferred, portMAX_DELAY);
    }
}

// =============================
// System Boot / Task Launch
// =============================
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Initializing System Boot Sequence...");

    // 1) Bring up I2C master used for converter configuration.
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

    // 2) Configure ADC/DAC devices over I2C.
    if (TIHardwareConfig::configureHardwareConverters(i2c_bus_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Hardware Boot Failure: TI Burr-Brown Chips rejected configuration.");
        return;
    }
    ESP_LOGI(TAG, "TI PCM1822 ADCs and PCM1795 DACs validated online.");

    // 3) Initialize analysis-path modules.
    for (int ch = 0; ch < 4; ++ch) {
        primaryDecimator[ch].init();
    }

    // 4) Launch asymmetric tasks (Core1 control, Core0 audio).
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
