#pragma once

#include "driver/gpio.h"
#include "driver/i2s_tdm.h"
#include "esp_err.h"

// =============================
// TDM Driver Wrapper
// =============================
class ESP32P4TDMDriver {
public:
    // TX: DSP->DAC frame stream, RX: ADC->DSP frame stream.
    i2s_chan_handle_t tx_handle = nullptr;
    i2s_chan_handle_t rx_handle = nullptr;

    // =============================
    // Transport Initialization
    // =============================
    esp_err_t initTDM16_192kHz() {
        // Single I2S peripheral hosts both directions for synchronous full-duplex TDM.
        i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        chanCfg.dma_desc_num = 8;
        chanCfg.dma_frame_num = 128;

        if (i2s_new_channel(&chanCfg, &tx_handle, &rx_handle) != ESP_OK) {
            return ESP_FAIL;
        }

        // Canonical transport: 192 kHz, 32-bit slots, 16 active lanes.
        i2s_tdm_config_t tdmCfg = {
            .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(192000),
            .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
                I2S_DATA_BIT_WIDTH_32BIT,
                I2S_SLOT_MODE_STEREO,
                static_cast<i2s_tdm_slot_mask_t>(I2S_TDM_SLOT0 |
                                                 I2S_TDM_SLOT1 |
                                                 I2S_TDM_SLOT2 |
                                                 I2S_TDM_SLOT3 |
                                                 I2S_TDM_SLOT4 |
                                                 I2S_TDM_SLOT5 |
                                                 I2S_TDM_SLOT6 |
                                                 I2S_TDM_SLOT7 |
                                                 I2S_TDM_SLOT8 |
                                                 I2S_TDM_SLOT9 |
                                                 I2S_TDM_SLOT10 |
                                                 I2S_TDM_SLOT11 |
                                                 I2S_TDM_SLOT12 |
                                                 I2S_TDM_SLOT13 |
                                                 I2S_TDM_SLOT14 |
                                                 I2S_TDM_SLOT15)),
            .gpio_cfg = {
                .mclk = GPIO_NUM_4,
                .bclk = GPIO_NUM_5,
                .ws = GPIO_NUM_6,
                .dout = GPIO_NUM_7,
                .din = GPIO_NUM_8,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false,
                },
            },
        };

        // Configure and enable both directions before entering runtime tasks.
        if (i2s_channel_init_tdm_mode(tx_handle, &tdmCfg) != ESP_OK) {
            return ESP_FAIL;
        }
        if (i2s_channel_init_tdm_mode(rx_handle, &tdmCfg) != ESP_OK) {
            return ESP_FAIL;
        }

        if (i2s_channel_enable(tx_handle) != ESP_OK) {
            return ESP_FAIL;
        }
        if (i2s_channel_enable(rx_handle) != ESP_OK) {
            return ESP_FAIL;
        }

        return ESP_OK;
    }
};
