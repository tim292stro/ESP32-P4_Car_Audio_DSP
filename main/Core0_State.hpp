#pragma once

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>

#define PITCH_BUF_SIZE 4096
#define PITCH_BUF_MASK (PITCH_BUF_SIZE - 1)

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

enum class PowerSeqState : uint32_t {
    // Waiting for valid ingress before enabling output path.
    WAITING_FOR_ADC = 0,
    // Hold muted for configurable startup delay.
    BOOT_HOLD_TIME = 1,
    // Smooth gain ramp to avoid startup transients.
    BOOT_RAMP_UP = 2,
    // Fully active runtime state.
    RUNNING = 3
};

// =============================
// Meter Index Map
// =============================
namespace ClipMeterMap {
    // Primary path meters are grouped by channel and stage for deterministic host indexing.
    constexpr uint32_t kPrimaryChannelCount = 4;
    constexpr uint32_t kPrimaryMetersPerChannel = 8;
    constexpr uint32_t kSfxMeterCount = 4;
    constexpr uint32_t kMonoMeterCount = 2;
    constexpr uint32_t kTotalMeterCount =
        (kPrimaryChannelCount * kPrimaryMetersPerChannel) + kSfxMeterCount + kMonoMeterCount;

    enum PrimaryStage : uint32_t {
        PRIMARY_NORMALIZE = 0,
        PRIMARY_GAIN = 1,
        PRIMARY_SUM = 2,
        PRIMARY_PRE_XOVER = 3,
        PRIMARY_X1_HP = 4,
        PRIMARY_X1_LP = 5,
        PRIMARY_X2_HP = 6,
        PRIMARY_X2_LP = 7,
    };

    enum SfxStage : uint32_t {
        SFX_LEFT_NORMALIZE = 32,
        SFX_LEFT_GAIN = 33,
        SFX_RIGHT_NORMALIZE = 34,
        SFX_RIGHT_GAIN = 35,
    };

    enum MonoStage : uint32_t {
        MONO_X3_HP = 36,
        MONO_X3_LP = 37,
    };

    constexpr uint32_t primaryIndex(uint32_t channelIndex, PrimaryStage stage) {
        return (channelIndex * kPrimaryMetersPerChannel) + static_cast<uint32_t>(stage);
    }
}

// =============================
// EQ Control Map
// =============================
namespace EqControlMap {
    // 4 primary channels use 31-band plan; 2 SFX channels use reduced 15-band plan.
    constexpr uint32_t kEqChannelCount = 6;
    constexpr uint32_t kPrimaryEqBandCount = 31;
    constexpr uint32_t kSfxEqBandCount = 15;
    constexpr uint32_t kEqBandCountMax = kPrimaryEqBandCount;

    constexpr uint32_t kPrimaryChannelCount = 4;
    constexpr uint32_t kSfxChannelStart = 4;

    constexpr std::array<float, kPrimaryEqBandCount> kPrimaryIsoBandHz = {
        20.0f, 25.0f, 31.5f, 40.0f, 50.0f, 63.0f, 80.0f, 100.0f,
        125.0f, 160.0f, 200.0f, 250.0f, 315.0f, 400.0f, 500.0f, 630.0f,
        800.0f, 1000.0f, 1250.0f, 1600.0f, 2000.0f, 2500.0f, 3150.0f, 4000.0f,
        5000.0f, 6300.0f, 8000.0f, 10000.0f, 12500.0f, 16000.0f, 20000.0f,
    };

    constexpr std::array<float, kSfxEqBandCount> kSfxBandHz = {
        25.0f, 40.0f, 63.0f, 100.0f, 160.0f, 250.0f, 400.0f, 630.0f,
        1000.0f, 1600.0f, 2500.0f, 4000.0f, 6300.0f, 10000.0f, 16000.0f,
    };

    constexpr uint32_t bandCountForChannel(uint32_t channelIndex) {
        return (channelIndex < kPrimaryChannelCount) ? kPrimaryEqBandCount : kSfxEqBandCount;
    }
}

// =============================
// ABI And Capability Flags
// =============================
namespace RegisterApiInfo {
    // ABI/versioning and capability flags exported to host over SPI.
    constexpr uint32_t kAbiVersion = 0x20260629u;
    constexpr uint32_t kCapabilityGroupedEq = (1u << 0);
    constexpr uint32_t kCapabilitySplitEqPlans = (1u << 1);
    constexpr uint32_t kCapabilityLowIrqBlock = (1u << 2);
    constexpr uint32_t kCapabilityVolumeIso226 = (1u << 3);
    constexpr uint32_t kCapabilityDacMuteHook = (1u << 4);
    constexpr uint32_t kCapabilityMask =
        kCapabilityGroupedEq |
        kCapabilitySplitEqPlans |
        kCapabilityLowIrqBlock |
        kCapabilityVolumeIso226 |
        kCapabilityDacMuteHook;
}

namespace EnableMask1Bits {
    // Named bit masks for MTR_SYS_ENABLE_1 to avoid magic literals in runtime code.
    // Ordered to follow canonical input/mode flow then output mute target.
    constexpr uint32_t PRIMARY_4CH_MODE_ENABLE = (1u << 0);
    constexpr uint32_t SFX_STEREO_MODE_ENABLE = (1u << 1);
    constexpr uint32_t USER_MUTE_OVERRIDE = (1u << 2);
    constexpr uint32_t VALID_MASK = PRIMARY_4CH_MODE_ENABLE |
                                    SFX_STEREO_MODE_ENABLE |
                                    USER_MUTE_OVERRIDE;
}

namespace PrimaryBypassMaskBits {
    // Per-primary-lane bypass controls packed into one register.
    // Bits 0..3: primary EQ bypass per lane.
    constexpr uint32_t PRIMARY_EQ_CH0_BYPASS = (1u << 0);
    constexpr uint32_t PRIMARY_EQ_CH1_BYPASS = (1u << 1);
    constexpr uint32_t PRIMARY_EQ_CH2_BYPASS = (1u << 2);
    constexpr uint32_t PRIMARY_EQ_CH3_BYPASS = (1u << 3);

    // Bits 4..7: bass restoration bypass per lane.
    constexpr uint32_t BASS_REST_CH0_BYPASS = (1u << 4);
    constexpr uint32_t BASS_REST_CH1_BYPASS = (1u << 5);
    constexpr uint32_t BASS_REST_CH2_BYPASS = (1u << 6);
    constexpr uint32_t BASS_REST_CH3_BYPASS = (1u << 7);

    // Bits 8..11: infrasonic restoration bypass per lane.
    constexpr uint32_t INFRA_REST_CH0_BYPASS = (1u << 8);
    constexpr uint32_t INFRA_REST_CH1_BYPASS = (1u << 9);
    constexpr uint32_t INFRA_REST_CH2_BYPASS = (1u << 10);
    constexpr uint32_t INFRA_REST_CH3_BYPASS = (1u << 11);

    // Bits 12..15: room compensation bypass per lane.
    constexpr uint32_t ROOM_COMP_CH0_BYPASS = (1u << 12);
    constexpr uint32_t ROOM_COMP_CH1_BYPASS = (1u << 13);
    constexpr uint32_t ROOM_COMP_CH2_BYPASS = (1u << 14);
    constexpr uint32_t ROOM_COMP_CH3_BYPASS = (1u << 15);

    // Bits 16..19: crossover stage-1 bypass per lane.
    constexpr uint32_t XOVER_STAGE1_CH0_BYPASS = (1u << 16);
    constexpr uint32_t XOVER_STAGE1_CH1_BYPASS = (1u << 17);
    constexpr uint32_t XOVER_STAGE1_CH2_BYPASS = (1u << 18);
    constexpr uint32_t XOVER_STAGE1_CH3_BYPASS = (1u << 19);

    // Bits 20..23: crossover stage-2 bypass per lane.
    constexpr uint32_t XOVER_STAGE2_CH0_BYPASS = (1u << 20);
    constexpr uint32_t XOVER_STAGE2_CH1_BYPASS = (1u << 21);
    constexpr uint32_t XOVER_STAGE2_CH2_BYPASS = (1u << 22);
    constexpr uint32_t XOVER_STAGE2_CH3_BYPASS = (1u << 23);

    // Bit 24: shared stage-3 crossover bypass after mono summing.
    constexpr uint32_t XOVER_STAGE3_MONO_BYPASS = (1u << 24);

    constexpr uint32_t VALID_MASK = 0x01FFFFFFu;
}

namespace SfxBypassMaskBits {
    // Per-SFX-lane bypass controls packed into one register.
    // Bit 0: SFX left EQ bypass, Bit 1: SFX right EQ bypass.
    constexpr uint32_t SFX_EQ_CH4_BYPASS = (1u << 0);
    constexpr uint32_t SFX_EQ_CH5_BYPASS = (1u << 1);
    constexpr uint32_t VALID_MASK = SFX_EQ_CH4_BYPASS | SFX_EQ_CH5_BYPASS;
}

// =============================
// Scalar Utility Helpers
// =============================
inline float sanitizeEqBandGain(float gain) {
    // Defensive clamp keeps host-provided NaN/Inf or extreme values from destabilizing DSP.
    if (!std::isfinite(gain)) {
        return 1.0f;
    }
    return std::max(0.03125f, std::min(32.0f, gain));
}

inline float clampFloat(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

inline float linearToDb(float linear) {
    const float safe = std::max(linear, 1.0e-6f);
    return 20.0f * std::log10(safe);
}

inline float dbToLinear(float db) {
    return std::exp((std::log(10.0f) / 20.0f) * db);
}

inline float iso226ApproxCompensationDb(float frequencyHz,
                                        float volumeDb,
                                        float referenceDb,
                                        float depth) {
    // Lightweight loudness contour approximation used at control-rate gain recompute time.
    // Intentionally avoids expensive table lookups in the real-time path.
    const float atten = clampFloat((referenceDb - volumeDb) / 40.0f, 0.0f, 1.0f);
    if (atten <= 0.0f || depth <= 0.0f) {
        return 0.0f;
    }

    float lowBoostDb = 0.0f;
// =============================
// DSP Primitive Types
// =============================
    if (frequencyHz < 1000.0f) {
        const float x = clampFloat(std::log(1000.0f / std::max(frequencyHz, 20.0f)) / std::log(1000.0f / 20.0f), 0.0f, 1.0f);
        lowBoostDb = 12.0f * x;
    }

    float highBoostDb = 0.0f;
    if (frequencyHz > 3000.0f) {
        const float x = clampFloat(std::log(std::max(frequencyHz, 3000.0f) / 3000.0f) / std::log(20000.0f / 3000.0f), 0.0f, 1.0f);
        highBoostDb = 6.0f * x;
    }

    return (lowBoostDb + highBoostDb) * atten * depth;
}

inline void recomputeEqCompositeGainForChannel(struct CompleteProcessorPayload& controls, uint32_t channelIndex);
inline void recomputeEqCompositeGainAll(struct CompleteProcessorPayload& controls);

struct ESP32Biquad {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;

    float x1 = 0.0f;
    float x2 = 0.0f;
    float y1 = 0.0f;
    float y2 = 0.0f;

    inline void process(float input, float& output) {
        // Direct form I biquad with persistent state.
        output = (b0 * input) + (b1 * x1) + (b2 * x2) - (a1 * y1) - (a2 * y2);
        x2 = x1;
        x1 = input;
        y2 = y1;
        y1 = output;
    }
};

// =============================
// Infrasonic Config/Runtime
// =============================
struct InfrasonicCoefficients {
    // User/host-tunable synthesis shaping values.
    float subMix = 0.4f;
    float infraMix = 0.3f;
    float voiceSensitivity = 3.2f;
    float targetRmsThresholdLinear = 0.5f;
};

struct InfrasonicRuntimeState {
    // Per-primary-lane restoration pitch targets produced by Core1 analysis.
    std::atomic<float> targetStepBass[ClipMeterMap::kPrimaryChannelCount] = {};
    std::atomic<float> targetStepInfrasonic[ClipMeterMap::kPrimaryChannelCount] = {};
};

// =============================
// Shared Runtime Payload
// =============================
struct CompleteProcessorPayload {
    // Central runtime control/state block shared between control and audio threads.
    // Keep field additions deliberate to preserve host assumptions and NVS mapping logic.
    uint32_t enableMask1 = 0;
    uint32_t systemStatusMask = 0;
    uint32_t bootDelaySamples = 384000;
    PowerSeqState currentState = PowerSeqState::WAITING_FOR_ADC;
    uint32_t sampleCounter = 0;

    float currentSystemGain = 0.0f;
    float userMuteRampGain = 0.0f;
    float primaryInputGain = 1.0f;
    float sfxInputGain = 1.0f;
    float pinkNoiseGainTrim = 1.0f;
    uint32_t pinkNoiseSourceMask = 0u; // Bits [5:0] map input channels CH0..CH5 replacement enable.
    uint32_t primaryBypassMask = 0u;   // Per-primary-lane bypass mask (eq/restoration/room-comp).
    uint32_t sfxBypassMask = 0u;       // Per-SFX-lane bypass mask (eq).
    uint32_t requestedEnableMask1 = 0u;
    uint32_t requestedPinkNoiseSourceMask = 0u;
    uint32_t requestedPrimaryBypassMask = 0u;
    uint32_t requestedSfxBypassMask = 0u;
    float roomCompB0 = 1.0f;
    float roomCompB1 = 0.0f;
    float roomCompB2 = 0.0f;
    float roomCompA1 = 0.0f;
    float roomCompA2 = 0.0f;
    float masterVolumeLinear = 1.0f;
    // Current host surface exposes CH0..CH5 path gains; lanes above this currently reuse mapped indices.
    float outputPathMaxGain[6] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    float iso226Depth = 1.0f;
    float iso226ReferenceDb = -10.0f;
    uint32_t volumeMuteCtrl = 0;
    uint32_t iso226Ctrl = 1;
    uint32_t eqChannelPageSelect = 0;
    uint32_t eqBandPageSelect = 0;
    uint32_t eqApplySequence = 0;
    float eqBandGains[EqControlMap::kEqChannelCount][EqControlMap::kEqBandCountMax] = {{1.0f}};
    float eqCompositeGain[EqControlMap::kEqChannelCount] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    uint32_t irqEnableMask = 0x00000001u;
    float channelPeakOutputs[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    uint64_t stickyClipFlags64 = 0;
    uint32_t stickyClipFlags = 0;
    uint32_t clipMeterPageSelect = 0;
    uint32_t clipMeterModeCtrl = 0;
    uint32_t clipMeterSampleCounter = 0;
    uint32_t meterPresentationStatus = 0;
    uint32_t meterPresentationSeq = 0;
    uint32_t meterPresentationIrqCtrl = 1;
    uint32_t meterPresentationIntervalSamples = 1600;
    float clipMeterPeaks[ClipMeterMap::kTotalMeterCount] = {0.0f};
    float clipMeterCurrent[ClipMeterMap::kTotalMeterCount] = {0.0f};
    uint32_t clipMeterLastUpdateSample[ClipMeterMap::kTotalMeterCount] = {0};

    // SPI crossover setpoints (16-bit Hz values carried in 32-bit registers).
    uint32_t crossoverHzReg0 = 175;
    uint32_t crossoverHzReg1 = 3000;
    uint32_t crossoverHzReg2 = 25;
    uint32_t protectLowCutHzReg = 10;

    // SPI-exposed service selector registers and runtime status.
    uint32_t protectSelCtrl = 0x00000000;
    uint32_t protectSelStat = 0x00000000;
    uint32_t protectSelTimerSec = 0x00000000;
};

// =============================
// EQ Composite Recompute
// =============================
inline void recomputeEqCompositeGainForChannel(CompleteProcessorPayload& controls, uint32_t channelIndex) {
    // Recompute one composite gain factor per channel from per-band settings.
    // This pushes expensive math out of the per-sample path.
    if (channelIndex >= EqControlMap::kEqChannelCount) {
        return;
    }

    float logSum = 0.0f;
    const uint32_t activeBands = EqControlMap::bandCountForChannel(channelIndex);
    const float volumeDb = linearToDb(controls.masterVolumeLinear);
    const bool isoEnabled = (controls.iso226Ctrl & 0x1u) != 0u;

    for (uint32_t band = 0; band < EqControlMap::kEqBandCountMax; ++band) {
        if (band >= activeBands) {
            controls.eqBandGains[channelIndex][band] = 1.0f;
            continue;
        }
        const float safe = sanitizeEqBandGain(controls.eqBandGains[channelIndex][band]);
        controls.eqBandGains[channelIndex][band] = safe;

        float effective = safe;
        if (isoEnabled) {
            const float freqHz = (channelIndex < EqControlMap::kPrimaryChannelCount)
                                     ? EqControlMap::kPrimaryIsoBandHz[band]
                                     : EqControlMap::kSfxBandHz[band];
            const float compDb = iso226ApproxCompensationDb(freqHz,
                                                            volumeDb,
                                                            controls.iso226ReferenceDb,
                                                            clampFloat(controls.iso226Depth, 0.0f, 1.0f));
            effective *= dbToLinear(compDb);
        }

        logSum += std::log(std::max(effective, 1.0e-6f));
    }

    const float meanLog = logSum / static_cast<float>(activeBands);
    controls.eqCompositeGain[channelIndex] = std::exp(meanLog);
}

inline void recomputeEqCompositeGainAll(CompleteProcessorPayload& controls) {
    // Helper used after global controls that impact all channels (master volume, ISO toggles, etc.).
    for (uint32_t ch = 0; ch < EqControlMap::kEqChannelCount; ++ch) {
        recomputeEqCompositeGainForChannel(controls, ch);
    }
}
