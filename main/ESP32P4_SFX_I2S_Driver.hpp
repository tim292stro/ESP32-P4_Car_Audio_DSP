#pragma once

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_err.h"

// Dedicated SFX ingress driver: stereo I2S at 48 kHz.
class ESP32P4SfxI2SDriver {
public:
    i2s_chan_handle_t rx_handle = nullptr;

    // Configure separate I2S port for notification/SFX input capture.
    esp_err_t initSfxRx48kHzStereo() {
        i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
        chanCfg.dma_desc_num = 4;
        chanCfg.dma_frame_num = 64;

        if (i2s_new_channel(&chanCfg, nullptr, &rx_handle) != ESP_OK) {
            return ESP_FAIL;
        }

        i2s_std_config_t stdCfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(48000),
            .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
            .gpio_cfg = {
                .mclk = GPIO_NUM_16,
                .bclk = GPIO_NUM_17,
                .ws = GPIO_NUM_18,
                .dout = I2S_GPIO_UNUSED,
                .din = GPIO_NUM_19,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false,
                },
            },
        };

        if (i2s_channel_init_std_mode(rx_handle, &stdCfg) != ESP_OK) {
            return ESP_FAIL;
        }

        if (i2s_channel_enable(rx_handle) != ESP_OK) {
            return ESP_FAIL;
        }

        return ESP_OK;
    }
};
