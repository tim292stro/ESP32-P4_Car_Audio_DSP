#pragma once

#include <cstdint>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char* NVS_TAG_LOG = "CORE1_NVS_SYS";
static const char* NVS_NAMESPACE_KEY = "dsp_nv_storage";

// =============================
// Persistent Payload
// =============================
// Persistent payload committed as one blob.
// Keeping all related fields together simplifies versioning and atomicity for this firmware stage.
struct DevicePresetPayload {
    float eqGains[6][31] = {{1.0f}};
    float crossoverFreqs[4] = {175.0f, 3000.0f, 25.0f, 10.0f};
    float gainSettings[16] = {1.0f};
    uint32_t activeEnableMask1 = 0;
    uint32_t activePrimaryBypassMask = 0;
    uint32_t activeSfxBypassMask = 0;
};

class ESP32P4_NVS_Manager {
public:
    // =============================
    // Runtime Commit State
    // =============================
    // Dirty indicates there are unapplied runtime changes pending commit.
    bool isDirty = false;
    // Deferred-write countdown to reduce flash wear.
    uint32_t secondsRemainingUntilCommit = 0;
    // 240s stability window chosen to absorb active tuning sessions.
    static constexpr uint32_t STABILITY_TIMEOUT_SECONDS = 240;
    DevicePresetPayload workingPreset;

    // =============================
    // Public Lifecycle APIs
    // =============================
    void initialize() {
        // Handle erased/outdated NVS partitions with erase+reinit fallback.
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            err = nvs_flash_init();
        }
        ESP_ERROR_CHECK(err);
        loadConfigurationsFromFlash();
    }

    void notifyParameterMutation() {
        // Restart deferred commit timer after each mutation.
        isDirty = true;
        secondsRemainingUntilCommit = STABILITY_TIMEOUT_SECONDS;
    }

    void forceImmediateFlashSave() {
        // Manual commit path used by explicit host command.
        if (isDirty) {
            ESP_LOGI(NVS_TAG_LOG, "Forced manual flash commit executed.");
            commitConfigurationsToFlash();
        }
    }

    void processBackgroundStorageTick() {
        // Called once per second by Core1 loop.
        if (!isDirty) {
            return;
        }
        if (secondsRemainingUntilCommit > 0) {
            --secondsRemainingUntilCommit;
        } else {
            commitConfigurationsToFlash();
        }
    }

    uint32_t getSPIFlashTimerStatusRegister() const {
        // 0xFFFFFFFF means clean/no pending commit; otherwise return remaining seconds.
        if (!isDirty) {
            return 0xFFFFFFFFu;
        }
        return secondsRemainingUntilCommit;
    }

private:
    // =============================
    // Internal Flash Helpers
    // =============================
    void commitConfigurationsToFlash() {
        // Best-effort commit; failures leave dirty state asserted so host can inspect timer status.
        nvs_handle_t flashMemoryHandle;
        esp_err_t err = nvs_open(NVS_NAMESPACE_KEY, NVS_READWRITE, &flashMemoryHandle);
        if (err != ESP_OK) {
            return;
        }

        err = nvs_set_blob(flashMemoryHandle, "storage_blob", &workingPreset, sizeof(DevicePresetPayload));
        if (err == ESP_OK) {
            err = nvs_commit(flashMemoryHandle);
            if (err == ESP_OK) {
                isDirty = false;
                ESP_LOGI(NVS_TAG_LOG, "Data committed.");
            }
        }
        nvs_close(flashMemoryHandle);
    }

    void loadConfigurationsFromFlash() {
        // Load persisted blob if present and size matches expected struct layout.
        nvs_handle_t flashMemoryHandle;
        esp_err_t err = nvs_open(NVS_NAMESPACE_KEY, NVS_READONLY, &flashMemoryHandle);
        if (err != ESP_OK) {
            loadFactoryHardwareDefaults();
            return;
        }

        size_t expectedSize = sizeof(DevicePresetPayload);
        err = nvs_get_blob(flashMemoryHandle, "storage_blob", &workingPreset, &expectedSize);
        if (err != ESP_OK || expectedSize != sizeof(DevicePresetPayload)) {
            loadFactoryHardwareDefaults();
        }
        nvs_close(flashMemoryHandle);
    }

    void loadFactoryHardwareDefaults() {
        // Conservative startup defaults to avoid accidental overdrive.
        for (int ch = 0; ch < 6; ++ch) {
            for (int b = 0; b < 31; ++b) {
                workingPreset.eqGains[ch][b] = 1.0f;
            }
        }

        workingPreset.crossoverFreqs[0] = 175.0f;
        workingPreset.crossoverFreqs[1] = 3000.0f;
        workingPreset.crossoverFreqs[2] = 25.0f;
        workingPreset.crossoverFreqs[3] = 10.0f;

        for (float& g : workingPreset.gainSettings) {
            g = 1.0f;
        }

        workingPreset.gainSettings[2] = 1.0f;   // masterVolumeLinear
        workingPreset.gainSettings[3] = 1.0f;   // outputPathMaxGain[0]
        workingPreset.gainSettings[4] = 1.0f;   // outputPathMaxGain[1]
        workingPreset.gainSettings[5] = 1.0f;   // outputPathMaxGain[2]
        workingPreset.gainSettings[6] = 1.0f;   // outputPathMaxGain[3]
        workingPreset.gainSettings[7] = 1.0f;   // outputPathMaxGain[4]
        workingPreset.gainSettings[8] = 1.0f;   // outputPathMaxGain[5]
        workingPreset.gainSettings[9] = 1.0f;   // iso226Depth
        workingPreset.gainSettings[10] = 1.0f;  // iso226Ctrl (enabled)
        workingPreset.gainSettings[11] = -10.0f; // iso226ReferenceDb

        workingPreset.activeEnableMask1 = 0x00000000;
        workingPreset.activePrimaryBypassMask = 0x00000000;
        workingPreset.activeSfxBypassMask = 0x00000000;

        isDirty = false;
        secondsRemainingUntilCommit = 0;
    }
};
