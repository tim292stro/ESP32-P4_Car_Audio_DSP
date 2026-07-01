#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "driver/gpio.h"
#include "driver/spi_slave.h"

#include "Core1.hpp"
#include "Core0.hpp"

struct __attribute__((packed, aligned(4))) SPIPacket {
    // 32-bit address + 32-bit payload transaction unit.
    uint32_t address;
    uint32_t data;
};

static_assert(sizeof(SPIPacket) == 8u, "SPIPacket layout changed; update SPI transfer assumptions.");

// =============================
// SPI Slave Register Parser
// =============================
class Core1SPISlaveParser {
public:
    // =============================
    // Register Address Constants
    // =============================
    // SPI/IRQ plumbing constants.
    static constexpr int DMA_CHAN = SPI_DMA_CH_AUTO;
    static constexpr gpio_num_t METER_READY_IRQ_GPIO = GPIO_NUM_9;
    static constexpr uint32_t METER_PRESENT_READY_BIT = (1u << 0);
    static constexpr uint32_t EQ_APPLY_KEY = 0x45515131u;

    static constexpr uint32_t IRQ_BIT_METER_INTERVAL = (1u << 0);
    static constexpr uint32_t IRQ_BIT_CLIP_LATCH = (1u << 1);
    static constexpr uint32_t IRQ_BIT_PROTECT_TIMEOUT = (1u << 2);
    static constexpr uint32_t IRQ_ENABLE_VALID_MASK = IRQ_BIT_METER_INTERVAL | IRQ_BIT_CLIP_LATCH | IRQ_BIT_PROTECT_TIMEOUT;

    static constexpr uint32_t IRQ_STATUS_ADDR = 0x0080u;
    static constexpr uint32_t IRQ_ENABLE_ADDR = 0x0084u;
    static constexpr uint32_t IRQ_CTRL_ADDR = 0x0088u;

    static constexpr uint32_t ABI_VERSION_ADDR = 0x0000u;
    static constexpr uint32_t ABI_CAPS_ADDR = 0x0004u;

    static constexpr uint32_t GAIN_PRIMARY_ADDR = 0x00A2u;
    static constexpr uint32_t GAIN_NOTIF_ADDR = 0x00A4u;
    static constexpr uint32_t PRIMARY_BYPASS_MASK_ADDR = 0x00A6u;
    static constexpr uint32_t SFX_BYPASS_MASK_ADDR = 0x00A7u;
    static constexpr uint32_t PINK_NOISE_SRC_MASK_ADDR = 0x0100u;
    static constexpr uint32_t PINK_NOISE_GAIN_TRIM_ADDR = 0x0104u;
    static constexpr uint32_t PINK_NOISE_SRC_VALID_MASK = 0x3Fu;
    static constexpr uint32_t ROOM_COMP_B0_ADDR = 0x0150u;
    static constexpr uint32_t ROOM_COMP_B1_ADDR = 0x0154u;
    static constexpr uint32_t ROOM_COMP_B2_ADDR = 0x0158u;
    static constexpr uint32_t ROOM_COMP_A1_ADDR = 0x015Cu;
    static constexpr uint32_t ROOM_COMP_A2_ADDR = 0x0160u;

    static constexpr uint32_t VOL_MASTER_ADDR = 0x0110u;
    static constexpr uint32_t VOL_MUTE_CTRL_ADDR = 0x0114u;
    static constexpr uint32_t VOL_STATE_ADDR = 0x0118u;
    static constexpr uint32_t ISO226_CTRL_ADDR = 0x011Cu;
    static constexpr uint32_t ISO226_DEPTH_ADDR = 0x0120u;
    static constexpr uint32_t ISO226_REF_DB_ADDR = 0x0124u;
    static constexpr uint32_t VOL_PATH_MAX_BASE = 0x0130u;

    static constexpr uint32_t EQ_BASE_ADDR = 0x0200u;
    static constexpr uint32_t EQ_GROUP_STRIDE = 0x0100u;
    static constexpr uint32_t EQ_GROUP_COUNT = EqControlMap::kEqChannelCount;
    static constexpr uint32_t EQ_GROUP_REG_INFO = 0x00u;
    static constexpr uint32_t EQ_GROUP_REG_APPLY = 0x04u;
    static constexpr uint32_t EQ_GROUP_REG_COMPOSITE = 0x08u;
    static constexpr uint32_t EQ_GROUP_REG_BAND_BASE = 0x10u;

    // Reused transfer buffers to avoid per-transaction heap or stack churn.
    SPIPacket rx_buffer = {0, 0};
    SPIPacket tx_buffer = {0, 0};

private:
    // =============================
    // Meter Encoding Helpers
    // =============================
    static uint32_t meterPeakToDbCode(float peakLinear) {
        // Compact host-facing dB-ish code for UI readback.
        if (peakLinear <= 0.0f) {
            return 0u; // Reserved: -infinity
        }

        const float dB = 20.0f * std::log10(peakLinear);
        const int32_t codeSigned = static_cast<int32_t>(std::lround(dB + 63.0f));

        if (codeSigned < 1) {
            return 1u;
        }
        if (codeSigned > 63) {
            return 63u;
        }
        return static_cast<uint32_t>(codeSigned);
    }

    static uint32_t meterPeakToLinearQ23(float peakLinear) {
        // Alternate host readback format with fixed-point linear precision.
        constexpr float qScale = 8388608.0f; // 2^23
        if (peakLinear <= 0.0f) {
            return 0u;
        }
        const float scaled = peakLinear * qScale;
        if (scaled >= static_cast<float>(UINT32_MAX)) {
            return UINT32_MAX;
        }
        return static_cast<uint32_t>(std::lround(scaled));
    }

    static float decodeFloatOrDefault(uint32_t raw, float defaultValue) {
        float out = defaultValue;
        std::memcpy(&out, &raw, sizeof(uint32_t));
        if (!std::isfinite(out)) {
            return defaultValue;
        }
        return out;
    }

    static float decodeClampedFloat(uint32_t raw, float defaultValue, float lo, float hi) {
        const float v = decodeFloatOrDefault(raw, defaultValue);
        return clampFloat(v, lo, hi);
    }

    static uint32_t saturatingMulU32(uint32_t a, uint32_t b) {
        const uint64_t prod = static_cast<uint64_t>(a) * static_cast<uint64_t>(b);
        if (prod > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
            return std::numeric_limits<uint32_t>::max();
        }
        return static_cast<uint32_t>(prod);
    }

    static uint32_t clampU32(uint32_t v, uint32_t lo, uint32_t hi) {
        if (v < lo) {
            return lo;
        }
        if (v > hi) {
            return hi;
        }
        return v;
    }

    // 192 kHz system sample-rate constraints and practical crossover/protection tuning bounds.
    static constexpr uint32_t XOVER_HZ_MIN = 5u;
    static constexpr uint32_t XOVER_HZ_MAX = 20000u;
    static constexpr uint32_t PROTECT_HP_HZ_MIN = 5u;
    static constexpr uint32_t PROTECT_HP_HZ_MAX = 500u;

public:

    // =============================
    // IRQ Pending/Line Helpers
    // =============================

    static uint32_t interruptPendingMask(const CompleteProcessorPayload& controls) {
        // Build pending-mask from latched runtime state.
        uint32_t pending = 0;
        if ((controls.meterPresentationStatus & METER_PRESENT_READY_BIT) != 0u) {
            pending |= IRQ_BIT_METER_INTERVAL;
        }
        if (controls.stickyClipFlags64 != 0u) {
            pending |= IRQ_BIT_CLIP_LATCH;
        }
        if ((controls.protectSelStat & (1u << 3)) != 0u) {
            pending |= IRQ_BIT_PROTECT_TIMEOUT;
        }
        return pending;
    }

    static void updateInterruptLine(const CompleteProcessorPayload& controls) {
        // IRQ line is level-driven: assert while any enabled pending source exists.
        const uint32_t pending = interruptPendingMask(controls);
        const bool irqAssert = (pending & controls.irqEnableMask & IRQ_ENABLE_VALID_MASK) != 0u;
        gpio_set_level(METER_READY_IRQ_GPIO, irqAssert ? 1 : 0);
    }

    // =============================
    // Grouped EQ Register Surface
    // =============================
    bool handleEqGroupedRegister(uint32_t address, uint32_t data, CompleteProcessorPayload& controls, ESP32P4_NVS_Manager& nvs) {
        // Grouped EQ map keeps per-channel surfaces contiguous and host-iterable.
        if (address < EQ_BASE_ADDR) {
            return false;
        }
        const uint32_t rel = address - EQ_BASE_ADDR;
        const uint32_t groupIndex = rel / EQ_GROUP_STRIDE;
        if (groupIndex >= EQ_GROUP_COUNT) {
            return false;
        }

        const uint32_t offset = rel % EQ_GROUP_STRIDE;
        const uint32_t activeBands = EqControlMap::bandCountForChannel(groupIndex);

        if (offset == EQ_GROUP_REG_INFO) {
            // Return active-band count and last apply sequence.
            tx_buffer.data = (activeBands & 0xFFu) |
                             ((groupIndex >= EqControlMap::kSfxChannelStart ? 1u : 0u) << 8u) |
                             ((controls.eqApplySequence & 0xFFFFu) << 16u);
            return true;
        }

        if (offset == EQ_GROUP_REG_APPLY) {
            // Explicit apply key avoids accidental recompute storms from noisy writes.
            if (data == EQ_APPLY_KEY) {
                recomputeEqCompositeGainForChannel(controls, groupIndex);
                ++controls.eqApplySequence;
                nvs.notifyParameterMutation();
            }
            tx_buffer.data = controls.eqApplySequence;
            return true;
        }

        if (offset == EQ_GROUP_REG_COMPOSITE) {
            std::memcpy(&tx_buffer.data, &controls.eqCompositeGain[groupIndex], sizeof(uint32_t));
            return true;
        }

        if (offset >= EQ_GROUP_REG_BAND_BASE) {
            const uint32_t bandRel = offset - EQ_GROUP_REG_BAND_BASE;
            if ((bandRel % 4u) == 0u) {
                const uint32_t band = bandRel / 4u;
                if (band < EqControlMap::kEqBandCountMax) {
                    if (band < activeBands) {
                        // Write-through update: runtime + NVS shadow.
                        float incomingGain = 1.0f;
                        std::memcpy(&incomingGain, &data, sizeof(uint32_t));
                        incomingGain = sanitizeEqBandGain(incomingGain);

                        controls.eqBandGains[groupIndex][band] = incomingGain;
                        nvs.workingPreset.eqGains[groupIndex][band] = incomingGain;
                        recomputeEqCompositeGainForChannel(controls, groupIndex);
                        nvs.notifyParameterMutation();

                        std::memcpy(&tx_buffer.data, &controls.eqBandGains[groupIndex][band], sizeof(uint32_t));
                    } else {
                        const float unity = 1.0f;
                        std::memcpy(&tx_buffer.data, &unity, sizeof(uint32_t));
                    }
                    return true;
                }
            }
        }

        tx_buffer.data = 0xDEADBEEFu;
        return true;
    }

    // =============================
    // SPI Peripheral Init
    // =============================
    esp_err_t initSlaveDriver() {
        // Shared IRQ GPIO for meter-ready / clip-latch / protect-timeout indications.
        gpio_config_t irq_cfg = {};
        irq_cfg.pin_bit_mask = (1ULL << METER_READY_IRQ_GPIO);
        irq_cfg.mode = GPIO_MODE_OUTPUT;
        irq_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        irq_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        irq_cfg.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&irq_cfg);
        gpio_set_level(METER_READY_IRQ_GPIO, 0);

        // SPI bus pins for external host controller link.
        spi_bus_config_t bus_cfg = {};
        bus_cfg.mosi_io_num = GPIO_NUM_11;
        bus_cfg.miso_io_num = GPIO_NUM_12;
        bus_cfg.sclk_io_num = GPIO_NUM_13;
        bus_cfg.quadwp_io_num = -1;
        bus_cfg.quadhd_io_num = -1;
        bus_cfg.max_transfer_sz = static_cast<int>(sizeof(SPIPacket));

        spi_slave_interface_config_t slv_cfg = {};
        slv_cfg.spics_io_num = GPIO_NUM_10;
        slv_cfg.flags = 0;
        slv_cfg.queue_size = 4;
        slv_cfg.mode = 0;
        slv_cfg.post_setup_cb = nullptr;
        slv_cfg.post_trans_cb = nullptr;

        if (spi_slave_initialize(SPI2_HOST, &bus_cfg, &slv_cfg, DMA_CHAN) != ESP_OK) {
            return ESP_FAIL;
        }
        return ESP_OK;
    }

    // =============================
    // Transaction Service Loop
    // =============================
    void handleIncomingTransaction(CompleteProcessorPayload& controls, ESP32P4_NVS_Manager& nvs) {
        // Blocking one-transaction service model.
        spi_slave_transaction_t trans = {};
        trans.flags = 0;
        trans.length = static_cast<uint32_t>(sizeof(SPIPacket) * 8u);
        trans.tx_buffer = &tx_buffer;
        trans.rx_buffer = &rx_buffer;

        // Defensive clear avoids stale fields if host delivers malformed transaction length.
        rx_buffer.address = 0u;
        rx_buffer.data = 0u;

        if (spi_slave_transmit(SPI2_HOST, &trans, portMAX_DELAY) != ESP_OK) {
            return;
        }

        const uint32_t expectedBits = static_cast<uint32_t>(sizeof(SPIPacket) * 8u);
        if (trans.trans_len < expectedBits) {
            tx_buffer.data = 0xDEADBEEFu;
            updateInterruptLine(controls);
            return;
        }

        switch (rx_buffer.address) {
            // ABI discovery surfaces.
            case ABI_VERSION_ADDR:
                tx_buffer.data = RegisterApiInfo::kAbiVersion;
                break;

            case ABI_CAPS_ADDR:
                tx_buffer.data = RegisterApiInfo::kCapabilityMask;
                break;

            // Core enable and routing masks.
            case 0x00A1:
                controls.requestedEnableMask1 = (rx_buffer.data & EnableMask1Bits::VALID_MASK);
                nvs.workingPreset.activeEnableMask1 = controls.requestedEnableMask1;
                tx_buffer.data = controls.requestedEnableMask1;
                nvs.notifyParameterMutation();
                break;

            case PRIMARY_BYPASS_MASK_ADDR:
                controls.requestedPrimaryBypassMask = (rx_buffer.data & PrimaryBypassMaskBits::VALID_MASK);
                nvs.workingPreset.activePrimaryBypassMask = controls.requestedPrimaryBypassMask;
                tx_buffer.data = controls.requestedPrimaryBypassMask;
                nvs.notifyParameterMutation();
                break;

            case SFX_BYPASS_MASK_ADDR:
                controls.requestedSfxBypassMask = (rx_buffer.data & SfxBypassMaskBits::VALID_MASK);
                nvs.workingPreset.activeSfxBypassMask = controls.requestedSfxBypassMask;
                tx_buffer.data = controls.requestedSfxBypassMask;
                nvs.notifyParameterMutation();
                break;

            case GAIN_PRIMARY_ADDR: {
                const float g = decodeClampedFloat(rx_buffer.data, controls.primaryInputGain, 0.0f, 8.0f);
                controls.primaryInputGain = g;
                nvs.workingPreset.gainSettings[0] = g;
                std::memcpy(&tx_buffer.data, &controls.primaryInputGain, sizeof(uint32_t));
                nvs.notifyParameterMutation();
                break;
            }

            case GAIN_NOTIF_ADDR: {
                const float g = decodeClampedFloat(rx_buffer.data, controls.sfxInputGain, 0.0f, 8.0f);
                controls.sfxInputGain = g;
                nvs.workingPreset.gainSettings[1] = g;
                std::memcpy(&tx_buffer.data, &controls.sfxInputGain, sizeof(uint32_t));
                nvs.notifyParameterMutation();
                break;
            }

            case PINK_NOISE_SRC_MASK_ADDR:
                controls.requestedPinkNoiseSourceMask = (rx_buffer.data & PINK_NOISE_SRC_VALID_MASK);
                tx_buffer.data = controls.requestedPinkNoiseSourceMask;
                break;

            case PINK_NOISE_GAIN_TRIM_ADDR: {
                controls.pinkNoiseGainTrim = decodeClampedFloat(rx_buffer.data, controls.pinkNoiseGainTrim, 0.0f, 2.0f);
                std::memcpy(&tx_buffer.data, &controls.pinkNoiseGainTrim, sizeof(uint32_t));
                break;
            }

            case ROOM_COMP_B0_ADDR:
                controls.roomCompB0 = decodeClampedFloat(rx_buffer.data, controls.roomCompB0, -8.0f, 8.0f);
                std::memcpy(&tx_buffer.data, &controls.roomCompB0, sizeof(uint32_t));
                break;

            case ROOM_COMP_B1_ADDR:
                controls.roomCompB1 = decodeClampedFloat(rx_buffer.data, controls.roomCompB1, -8.0f, 8.0f);
                std::memcpy(&tx_buffer.data, &controls.roomCompB1, sizeof(uint32_t));
                break;

            case ROOM_COMP_B2_ADDR:
                controls.roomCompB2 = decodeClampedFloat(rx_buffer.data, controls.roomCompB2, -8.0f, 8.0f);
                std::memcpy(&tx_buffer.data, &controls.roomCompB2, sizeof(uint32_t));
                break;

            case ROOM_COMP_A1_ADDR:
                controls.roomCompA1 = decodeClampedFloat(rx_buffer.data, controls.roomCompA1, -8.0f, 8.0f);
                std::memcpy(&tx_buffer.data, &controls.roomCompA1, sizeof(uint32_t));
                break;

            case ROOM_COMP_A2_ADDR:
                controls.roomCompA2 = decodeClampedFloat(rx_buffer.data, controls.roomCompA2, -8.0f, 8.0f);
                std::memcpy(&tx_buffer.data, &controls.roomCompA2, sizeof(uint32_t));
                break;

            // Gain and loudness controls.
            case VOL_MASTER_ADDR: {
                controls.masterVolumeLinear = decodeClampedFloat(rx_buffer.data, controls.masterVolumeLinear, 0.0f, 1.0f);
                nvs.workingPreset.gainSettings[2] = controls.masterVolumeLinear;
                recomputeEqCompositeGainAll(controls);
                std::memcpy(&tx_buffer.data, &controls.masterVolumeLinear, sizeof(uint32_t));
                nvs.notifyParameterMutation();
                break;
            }

            case VOL_MUTE_CTRL_ADDR:
                controls.volumeMuteCtrl = (rx_buffer.data & 0x3u);
                tx_buffer.data = controls.volumeMuteCtrl;
                break;

            case VOL_STATE_ADDR: {
                uint32_t state = 0;
                if ((controls.volumeMuteCtrl & 0x1u) != 0u) {
                    state |= (1u << 0);
                }
                if ((controls.volumeMuteCtrl & 0x2u) != 0u) {
                    state |= (1u << 1);
                }
                if ((controls.iso226Ctrl & 0x1u) != 0u) {
                    state |= (1u << 8);
                }
                tx_buffer.data = state;
                break;
            }

            case ISO226_CTRL_ADDR:
                controls.iso226Ctrl = (rx_buffer.data & 0x1u);
                recomputeEqCompositeGainAll(controls);
                tx_buffer.data = controls.iso226Ctrl;
                break;

            case ISO226_DEPTH_ADDR: {
                controls.iso226Depth = decodeClampedFloat(rx_buffer.data, controls.iso226Depth, 0.0f, 1.0f);
                recomputeEqCompositeGainAll(controls);
                std::memcpy(&tx_buffer.data, &controls.iso226Depth, sizeof(uint32_t));
                break;
            }

            case ISO226_REF_DB_ADDR: {
                controls.iso226ReferenceDb = decodeClampedFloat(rx_buffer.data, controls.iso226ReferenceDb, -40.0f, 0.0f);
                recomputeEqCompositeGainAll(controls);
                std::memcpy(&tx_buffer.data, &controls.iso226ReferenceDb, sizeof(uint32_t));
                break;
            }

            // Runtime status/IRQ surfaces.
            case 0x00A8:
                tx_buffer.data = controls.systemStatusMask;
                break;

            case IRQ_STATUS_ADDR: {
                const uint32_t pending = interruptPendingMask(controls);
                const uint32_t seqLsb16 = (controls.meterPresentationSeq & 0xFFFFu) << 16u;
                tx_buffer.data = seqLsb16 |
                                 ((controls.irqEnableMask & 0xFFu) << 8u) |
                                 (pending & 0xFFu);
                controls.meterPresentationStatus &= ~METER_PRESENT_READY_BIT;
                updateInterruptLine(controls);
                break;
            }

            case IRQ_ENABLE_ADDR:
                controls.irqEnableMask = (rx_buffer.data & IRQ_ENABLE_VALID_MASK);
                updateInterruptLine(controls);
                tx_buffer.data = controls.irqEnableMask;
                break;

            case IRQ_CTRL_ADDR:
                if ((rx_buffer.data & IRQ_BIT_METER_INTERVAL) != 0u) {
                    controls.meterPresentationStatus &= ~METER_PRESENT_READY_BIT;
                }
                if ((rx_buffer.data & IRQ_BIT_CLIP_LATCH) != 0u) {
                    controls.stickyClipFlags64 = 0;
                    controls.stickyClipFlags = 0;
                }
                updateInterruptLine(controls);
                tx_buffer.data = interruptPendingMask(controls);
                break;

            case 0x00AC:
                controls.bootDelaySamples = saturatingMulU32(rx_buffer.data, 192u);
                nvs.notifyParameterMutation();
                break;

            // Path max controls currently cover CH0..CH5.
            case VOL_PATH_MAX_BASE + 0x00:
            case VOL_PATH_MAX_BASE + 0x04:
            case VOL_PATH_MAX_BASE + 0x08:
            case VOL_PATH_MAX_BASE + 0x0C:
            case VOL_PATH_MAX_BASE + 0x10:
            case VOL_PATH_MAX_BASE + 0x14: {
                const uint32_t ch = (rx_buffer.address - VOL_PATH_MAX_BASE) / 4u;
                const float g = decodeClampedFloat(rx_buffer.data, controls.outputPathMaxGain[ch], 0.0f, 2.0f);
                controls.outputPathMaxGain[ch] = g;
                std::memcpy(&tx_buffer.data, &controls.outputPathMaxGain[ch], sizeof(uint32_t));
                break;
            }

            // Meter and clip-latch readouts.
            case 0x00B0:
                std::memcpy(&tx_buffer.data, &controls.channelPeakOutputs[0], sizeof(uint32_t));
                break;

            case 0x00B4:
                std::memcpy(&tx_buffer.data, &controls.channelPeakOutputs[1], sizeof(uint32_t));
                break;

            case 0x00B8:
                std::memcpy(&tx_buffer.data, &controls.channelPeakOutputs[2], sizeof(uint32_t));
                break;

            case 0x00BC:
                std::memcpy(&tx_buffer.data, &controls.channelPeakOutputs[3], sizeof(uint32_t));
                break;

            case 0x00C0:
                tx_buffer.data = static_cast<uint32_t>(controls.stickyClipFlags64 & 0xFFFFFFFFu);
                controls.stickyClipFlags64 &= 0xFFFFFFFF00000000ULL;
                controls.stickyClipFlags = static_cast<uint32_t>(controls.stickyClipFlags64 & 0xFFFFFFFFu);
                break;

            case 0x00C4:
                tx_buffer.data = static_cast<uint32_t>((controls.stickyClipFlags64 >> 32u) & 0xFFFFFFFFu);
                controls.stickyClipFlags64 &= 0x00000000FFFFFFFFULL;
                controls.stickyClipFlags = static_cast<uint32_t>(controls.stickyClipFlags64 & 0xFFFFFFFFu);
                break;

            case 0x00C8:
                if (rx_buffer.data < ClipMeterMap::kTotalMeterCount) {
                    controls.clipMeterPageSelect = rx_buffer.data;
                }
                tx_buffer.data = controls.clipMeterPageSelect;
                break;

            case 0x00CC: {
                const uint32_t meterIndex = std::min(controls.clipMeterPageSelect, ClipMeterMap::kTotalMeterCount - 1u);
                tx_buffer.data = meterPeakToDbCode(controls.clipMeterPeaks[meterIndex]);
                controls.clipMeterPeaks[meterIndex] = 0.0f;
                break;
            }

            case 0x00F4:
                if (rx_buffer.data <= 2u) {
                    controls.clipMeterModeCtrl = rx_buffer.data;
                }
                tx_buffer.data = controls.clipMeterModeCtrl;
                break;

            case 0x00F8: {
                const uint32_t meterIndex = std::min(controls.clipMeterPageSelect, ClipMeterMap::kTotalMeterCount - 1u);
                tx_buffer.data = meterPeakToLinearQ23(controls.clipMeterCurrent[meterIndex]);
                break;
            }

            case 0x00FC: {
                const uint32_t meterIndex = std::min(controls.clipMeterPageSelect, ClipMeterMap::kTotalMeterCount - 1u);
                tx_buffer.data = controls.clipMeterSampleCounter - controls.clipMeterLastUpdateSample[meterIndex];
                break;
            }

            case 0x00D8: {
                const uint32_t meterIndex = std::min(controls.clipMeterPageSelect, ClipMeterMap::kTotalMeterCount - 1u);
                const uint64_t meterBit = (1ULL << meterIndex);
                tx_buffer.data = (controls.stickyClipFlags64 & meterBit) ? 1u : 0u;
                controls.stickyClipFlags64 &= ~meterBit;
                controls.stickyClipFlags = static_cast<uint32_t>(controls.stickyClipFlags64 & 0xFFFFFFFFu);
                break;
            }

            // Persistence and protection control surfaces.
            case 0x00D0:
                if (rx_buffer.data == 0x51A151A1u) {
                    nvs.forceImmediateFlashSave();
                }
                break;

            case 0x00D4:
                tx_buffer.data = nvs.getSPIFlashTimerStatusRegister();
                break;

            case 0x00E0:
                controls.protectSelCtrl = rx_buffer.data;
                break;

            case 0x00E4:
                tx_buffer.data = controls.protectSelStat;
                break;

            case 0x00E8:
                tx_buffer.data = controls.protectSelTimerSec;
                break;

            // Crossover setpoint write surfaces.
            case 0x00EC: {
                const uint32_t xover0 = clampU32((rx_buffer.data & 0xFFFFu), XOVER_HZ_MIN, XOVER_HZ_MAX);
                const uint32_t xover1 = clampU32(((rx_buffer.data >> 16u) & 0xFFFFu), XOVER_HZ_MIN, XOVER_HZ_MAX);
                controls.crossoverHzReg0 = xover0;
                controls.crossoverHzReg1 = xover1;
                nvs.workingPreset.crossoverFreqs[0] = static_cast<float>(controls.crossoverHzReg0);
                nvs.workingPreset.crossoverFreqs[1] = static_cast<float>(controls.crossoverHzReg1);
                tx_buffer.data = (controls.crossoverHzReg0 & 0xFFFFu) |
                                 ((controls.crossoverHzReg1 & 0xFFFFu) << 16u);
                nvs.notifyParameterMutation();
                break;
            }

            case 0x00F0: {
                const uint32_t xover2 = clampU32((rx_buffer.data & 0xFFFFu), XOVER_HZ_MIN, XOVER_HZ_MAX);
                const uint32_t protectHp = clampU32(((rx_buffer.data >> 16u) & 0xFFFFu), PROTECT_HP_HZ_MIN, PROTECT_HP_HZ_MAX);
                controls.crossoverHzReg2 = xover2;
                controls.protectLowCutHzReg = protectHp;
                nvs.workingPreset.crossoverFreqs[2] = static_cast<float>(controls.crossoverHzReg2);
                nvs.workingPreset.crossoverFreqs[3] = static_cast<float>(controls.protectLowCutHzReg);
                tx_buffer.data = (controls.crossoverHzReg2 & 0xFFFFu) |
                                 ((controls.protectLowCutHzReg & 0xFFFFu) << 16u);
                nvs.notifyParameterMutation();
                break;
            }

            default:
                // Fallback to grouped-EQ map; unknown addresses return sentinel.
                if (!handleEqGroupedRegister(rx_buffer.address, rx_buffer.data, controls, nvs)) {
                    tx_buffer.data = 0xDEADBEEFu;
                }
                break;
        }

        updateInterruptLine(controls);
    }
};
