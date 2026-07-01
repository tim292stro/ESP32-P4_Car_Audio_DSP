#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "driver/i2c_master.h"
#include "esp_err.h"

// =============================
// TI Converter Configuration
// =============================
class TIHardwareConfig {
public:
    // Static I2C address map for current hardware reference design.
    static constexpr std::array<uint8_t, 2> ADC_ADDRS = {0x48, 0x49};
    static constexpr std::array<uint8_t, 5> DAC_ADDRS = {0x4C, 0x4D, 0x4E, 0x4F, 0x50};

    // =============================
    // Low-Level I2C Write
    // =============================
    static esp_err_t writeReg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val) {
        // Minimal register write helper used by all converter init/mute paths.
        uint8_t pkt[2] = {reg, val};
        return i2c_master_transmit(dev, pkt, sizeof(pkt), -1);
    }

    // =============================
    // ADC Bring-Up
    // =============================
    static esp_err_t configurePCM1822(i2c_master_dev_handle_t adcDev) {
        // Register sequence corresponds to known-good bring-up settings for this project.
        if (writeReg(adcDev, 0x01, 0x01) != ESP_OK) {
            return ESP_FAIL;
        }
        if (writeReg(adcDev, 0x14, 0x10) != ESP_OK) {
            return ESP_FAIL;
        }
        if (writeReg(adcDev, 0x15, 0x01) != ESP_OK) {
            return ESP_FAIL;
        }
        return ESP_OK;
    }

    // =============================
    // DAC Bring-Up
    // =============================
    static esp_err_t configurePCM1795(i2c_master_dev_handle_t dacDev) {
        // Register sequence corresponds to known-good bring-up settings for this project.
        if (writeReg(dacDev, 0x12, 0x80) != ESP_OK) {
            return ESP_FAIL;
        }
        if (writeReg(dacDev, 0x13, 0x03) != ESP_OK) {
            return ESP_FAIL;
        }
        if (writeReg(dacDev, 0x14, 0x00) != ESP_OK) {
            return ESP_FAIL;
        }
        return ESP_OK;
    }

    // =============================
    // Fleet Configuration Pass
    // =============================
    static esp_err_t configureHardwareConverters(i2c_master_bus_handle_t bus) {
        // Boot-time probe/config pass across all expected converter addresses.
        i2c_device_config_t devCfg = {};
        devCfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        devCfg.scl_speed_hz = 100000;

        uint32_t configuredAdcCount = 0;
        uint32_t configuredDacCount = 0;

        for (uint8_t address : ADC_ADDRS) {
            i2c_master_dev_handle_t adcDev = nullptr;
            devCfg.device_address = address;
            if (i2c_master_bus_add_device(bus, &devCfg, &adcDev) != ESP_OK) {
                continue;
            }

            if (configurePCM1822(adcDev) == ESP_OK) {
                ++configuredAdcCount;
            }
        }

        for (uint8_t address : DAC_ADDRS) {
            i2c_master_dev_handle_t dacDev = nullptr;
            devCfg.device_address = address;
            if (i2c_master_bus_add_device(bus, &devCfg, &dacDev) != ESP_OK) {
                continue;
            }

            if (configurePCM1795(dacDev) == ESP_OK) {
                ++configuredDacCount;
            }
        }

        // Require at least one ADC and one DAC configured to proceed.
        if (configuredAdcCount == 0 || configuredDacCount == 0) {
            return ESP_FAIL;
        }

        return ESP_OK;
    }

    // =============================
    // Runtime DAC Soft-Mute
    // =============================
    static esp_err_t setPCM1795SoftMute(i2c_master_bus_handle_t bus, bool mute) {
        // Runtime soft-mute fan-out across all DAC devices.
        i2c_device_config_t devCfg = {};
        devCfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        devCfg.scl_speed_hz = 100000;

        esp_err_t aggregate = ESP_OK;
        for (uint8_t address : DAC_ADDRS) {
            i2c_master_dev_handle_t dacDev = nullptr;
            devCfg.device_address = address;
            if (i2c_master_bus_add_device(bus, &devCfg, &dacDev) != ESP_OK) {
                aggregate = ESP_FAIL;
                continue;
            }

            // PCM1795 CTRL register retains baseline 0x80 from init; bit0 is used here as soft-mute control.
            const uint8_t ctrlVal = mute ? 0x81u : 0x80u;
            if (writeReg(dacDev, 0x12, ctrlVal) != ESP_OK) {
                aggregate = ESP_FAIL;
            }
        }
        return aggregate;
    }

    // =============================
    // Runtime DAC Digital Volume
    // =============================
    static esp_err_t setPCM1795DigitalVolume(i2c_master_bus_handle_t bus, float gainLinear01) {
        // PCM1795 uses per-channel digital attenuation registers; this helper fans out to all DACs.
        const float g = std::max(0.0f, std::min(1.0f, gainLinear01));
        const uint8_t volCode = static_cast<uint8_t>(g * 255.0f + 0.5f); // 0xFF ~ unity, 0x00 ~ mute floor.

        i2c_device_config_t devCfg = {};
        devCfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        devCfg.scl_speed_hz = 100000;

        esp_err_t aggregate = ESP_OK;
        for (uint8_t address : DAC_ADDRS) {
            i2c_master_dev_handle_t dacDev = nullptr;
            devCfg.device_address = address;
            if (i2c_master_bus_add_device(bus, &devCfg, &dacDev) != ESP_OK) {
                aggregate = ESP_FAIL;
                continue;
            }

            // 0x10 = left digital attenuation, 0x11 = right digital attenuation.
            if (writeReg(dacDev, 0x10, volCode) != ESP_OK) {
                aggregate = ESP_FAIL;
            }
            if (writeReg(dacDev, 0x11, volCode) != ESP_OK) {
                aggregate = ESP_FAIL;
            }
        }
        return aggregate;
    }
};
