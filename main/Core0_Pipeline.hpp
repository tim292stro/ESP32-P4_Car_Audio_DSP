#pragma once

#include "Core0_State.hpp"
#include "driver/gpio.h"

// =============================
// Core0 Real-Time Pipeline
// =============================
inline void processCompleteCore0Pipeline(float** outputChannels,
                                         int numChannels,
                                         int numSamples,
                                         float** inputChannels,
                                         int numInputs,
                                         CompleteProcessorPayload& controls,
                                         const InfrasonicCoefficients& infraCfg,
                                         InfrasonicRuntimeState& infraState) {
    // =============================
    // Block-Rate Setup / Constants
    // =============================
    // Core0 is the hard real-time DSP path. Keep this function branch-light and deterministic.
    // Processing order intentionally follows the canonical flow documented in README section 2.
    const float sampleRate = 192000.0f;
    const float twoPi = 6.283185307f;
    const float envAlpha = 1.0f - std::exp(-1.0f / (sampleRate * 0.040f));
    const float rmsAlpha = 1.0f - std::exp(-1.0f / (sampleRate * 0.060f));
    const float duckAttackAlpha = 1.0f - std::exp(-1.0f / (sampleRate * 0.005f));
    const float duckReleaseAlpha = 1.0f - std::exp(-1.0f / (sampleRate * 0.300f));
    const float rampUpDelta1s = 1.0f / sampleRate;
    const float userMuteDelta5ms = 1.0f / 960.0f;
    const float adcActivityThreshold = 0.001f;

    // Enable-mask bits consumed in this function.
    // MTR_SYS_ENABLE_1 keeps active-high mode/target semantics.
    constexpr uint32_t EN_PRIMARY_XOVER_STAGE3_MONO_BYPASS = (1u << 24);
    constexpr uint32_t EN_PRIMARY_4CH_MODE = (1u << 0);
    constexpr uint32_t EN_SFX_STEREO_MODE = (1u << 1);

    // Filter/mode state that must persist across calls (block-to-block continuity).
    static float lp175State[ClipMeterMap::kPrimaryChannelCount] = {0.0f, 0.0f, 0.0f, 0.0f};
    static float lp3kState[ClipMeterMap::kPrimaryChannelCount] = {0.0f, 0.0f, 0.0f, 0.0f};
    static float lp25StateMono = 0.0f;
    static float duckEnvelope = 0.0f;
    static float duckMusicGain = 1.0f;
    static float dcInState[10] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    static float dcOutState[10] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    // Metering runs as a side channel with decimation to reduce cost while preserving observability.
    constexpr uint32_t kMeterModeMask = 0x3u;
    constexpr uint32_t kMeterModeAWeighted = 1u;
    constexpr uint32_t kMeterModeCWeighted = 2u;
    constexpr uint32_t kMeterDecimation = 4u; // 192 kHz -> 48 kHz side-channel rate
    constexpr uint32_t kMeterPresentationReadyBit = (1u << 0);
    constexpr uint32_t kIrqBitMeterInterval = (1u << 0);
    constexpr uint32_t kIrqBitClipLatch = (1u << 1);
    constexpr gpio_num_t kMeterReadyIrqGpio = GPIO_NUM_9;
    static uint32_t meterDecimPhase = 0;
    static uint32_t meterPresentationAccumSamples = 0;
    static float meterDecimAccum[ClipMeterMap::kTotalMeterCount] = {0.0f};
    static float meterRuntimeCurrent[ClipMeterMap::kTotalMeterCount] = {0.0f};
    static float meterPresentationPeakAccum[ClipMeterMap::kTotalMeterCount] = {0.0f};

    static float aHpPrevY[ClipMeterMap::kTotalMeterCount] = {0.0f};
    static float aHpPrevX[ClipMeterMap::kTotalMeterCount] = {0.0f};
    static float aLpPrevY[ClipMeterMap::kTotalMeterCount] = {0.0f};

    static float cHpPrevY[ClipMeterMap::kTotalMeterCount] = {0.0f};
    static float cHpPrevX[ClipMeterMap::kTotalMeterCount] = {0.0f};
    static float cLpPrevY[ClipMeterMap::kTotalMeterCount] = {0.0f};

    // Crossover/protection setpoints are transferred as 16-bit Hz values in 32-bit registers.
    const float xoverHz0 = static_cast<float>(controls.crossoverHzReg0 & 0xFFFFu);
    const float xoverHz1 = static_cast<float>(controls.crossoverHzReg1 & 0xFFFFu);
    const float xoverHz2 = static_cast<float>(controls.crossoverHzReg2 & 0xFFFFu);
    const float protectLowCutHz = static_cast<float>(controls.protectLowCutHzReg & 0xFFFFu);

    const float alpha175 = 1.0f - std::exp(-(twoPi * xoverHz0) / sampleRate);
    const float alpha3000 = 1.0f - std::exp(-(twoPi * xoverHz1) / sampleRate);
    const float alpha25 = 1.0f - std::exp(-(twoPi * xoverHz2) / sampleRate);
    const float dcR = (protectLowCutHz <= 0.0f) ? 1.0f : std::exp(-(twoPi * protectLowCutHz) / sampleRate);

    const float sidechainRate = sampleRate / static_cast<float>(kMeterDecimation);
    const float aHpR = std::exp(-(twoPi * 150.0f) / sidechainRate);
    const float aLpAlpha = 1.0f - std::exp(-(twoPi * 12000.0f) / sidechainRate);
    const float cHpR = std::exp(-(twoPi * 20.0f) / sidechainRate);
    const float cLpAlpha = 1.0f - std::exp(-(twoPi * 20000.0f) / sidechainRate);

    float localPeaks[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float sampleClipMeters[ClipMeterMap::kTotalMeterCount] = {0.0f};
    uint64_t localClipFlags = 0;

    // Snapshot control masks once per block to avoid repeated volatile-looking reads.
    const uint32_t enMask1 = controls.enableMask1;
    const uint32_t primaryBypassMask = controls.primaryBypassMask;
    const uint32_t sfxBypassMask = controls.sfxBypassMask;
    const bool serviceBypassActive = (controls.protectSelStat & (1u << 2)) != 0u;

    // Meter helper used at major stage boundaries to support clip localization during tuning.
    auto accumulateClipMeter = [&](uint32_t meterIndex, float value) {
        const float absVal = std::fabs(value);
        if (absVal > sampleClipMeters[meterIndex]) {
            sampleClipMeters[meterIndex] = absVal;
        }
        if (absVal > 1.0f) {
            localClipFlags |= (1ULL << meterIndex);
        }
    };

    // Power-sequence gate: wait for valid ingress activity before opening audio path.
    if (controls.currentState == PowerSeqState::WAITING_FOR_ADC) {
        bool signalFound = false;
        for (int ch = 0; ch < numInputs && !signalFound; ++ch) {
            for (int s = 0; s < numSamples; ++s) {
                if (std::fabs(inputChannels[ch][s]) > adcActivityThreshold) {
                    signalFound = true;
                    break;
                }
            }
        }
        if (signalFound) {
            controls.currentState = PowerSeqState::BOOT_HOLD_TIME;
            controls.sampleCounter = 0;
        }
        controls.currentSystemGain = 0.0f;
    } else if (controls.currentState == PowerSeqState::BOOT_HOLD_TIME) {
        controls.sampleCounter += static_cast<uint32_t>(numSamples);
        controls.currentSystemGain = 0.0f;
        if (controls.sampleCounter >= controls.bootDelaySamples) {
            controls.currentState = PowerSeqState::BOOT_RAMP_UP;
        }
    }

    // Per-sample loop: canonical order is ingest -> program shaping -> duck/sum -> crossovers -> output map.
    for (int s = 0; s < numSamples; ++s) {
        std::fill_n(sampleClipMeters, ClipMeterMap::kTotalMeterCount, 0.0f);

        if (controls.currentState == PowerSeqState::BOOT_RAMP_UP) {
            controls.currentSystemGain += rampUpDelta1s;
            if (controls.currentSystemGain >= 1.0f) {
                controls.currentSystemGain = 1.0f;
                controls.currentState = PowerSeqState::RUNNING;
            }
        }

        // Smooth user mute transitions to reduce clicks on abrupt host mute toggles.
        const float targetUserMuteGain = ((enMask1 & (1u << 2)) != 0u) ? 1.0f : 0.0f;
        if (controls.userMuteRampGain < targetUserMuteGain) {
            controls.userMuteRampGain = std::min(targetUserMuteGain, controls.userMuteRampGain + userMuteDelta5ms);
        } else if (controls.userMuteRampGain > targetUserMuteGain) {
            controls.userMuteRampGain = std::max(targetUserMuteGain, controls.userMuteRampGain - userMuteDelta5ms);
        }

        const float softwareMute = ((controls.volumeMuteCtrl & 0x1u) != 0u) ? 0.0f : 1.0f;
        const float finalGlobalGainMultiplier = controls.currentSystemGain *
                            controls.userMuteRampGain *
                            softwareMute;

        // Primary ingress selection: 2ch mode mirrors ch0/ch1 into ch2/ch3, 4ch mode uses discrete lanes.
        const float rawPrimary0 = (numInputs > 0) ? inputChannels[0][s] : 0.0f;
        const float rawPrimary1 = (numInputs > 1) ? inputChannels[1][s] : rawPrimary0;
        const bool primary4ChModeEnabled = (enMask1 & EN_PRIMARY_4CH_MODE) != 0u;
        const float rawPrimary2 = (primary4ChModeEnabled && numInputs > 2) ? inputChannels[2][s] : rawPrimary0;
        const float rawPrimary3 = (primary4ChModeEnabled && numInputs > 3) ? inputChannels[3][s] : rawPrimary1;

        float primaryIn[ClipMeterMap::kPrimaryChannelCount] = {0.0f, 0.0f, 0.0f, 0.0f};
        const bool primaryEqBypassCh0 = (primaryBypassMask & (1u << 0)) != 0u;
        const bool primaryEqBypassCh1 = (primaryBypassMask & (1u << 1)) != 0u;
        const bool primaryEqBypassCh2 = (primaryBypassMask & (1u << 2)) != 0u;
        const bool primaryEqBypassCh3 = (primaryBypassMask & (1u << 3)) != 0u;
        const float primaryEqGain0 = primaryEqBypassCh0 ? 1.0f : controls.eqCompositeGain[0];
        const float primaryEqGain1 = primaryEqBypassCh1 ? 1.0f : controls.eqCompositeGain[1];
        const float primaryEqGain2 = primaryEqBypassCh2 ? 1.0f : controls.eqCompositeGain[2];
        const float primaryEqGain3 = primaryEqBypassCh3 ? 1.0f : controls.eqCompositeGain[3];
        primaryIn[0] = rawPrimary0 * primaryEqGain0;
        primaryIn[1] = rawPrimary1 * primaryEqGain1;
        primaryIn[2] = rawPrimary2 * primaryEqGain2;
        primaryIn[3] = rawPrimary3 * primaryEqGain3;

        // SFX ingress selection: dedicated SFX lanes preferred, fallback to primary pair when unavailable.
        const float rawSfxInputL = (numInputs > 4) ? inputChannels[4][s] : rawPrimary0;
        const float rawSfxInputR = (numInputs > 5) ? inputChannels[5][s] : rawPrimary1;
        const bool sfxStereoModeEnabled = (enMask1 & EN_SFX_STEREO_MODE) != 0u;

        float sfxPathInL = rawSfxInputL;
        float sfxPathInR = rawSfxInputR;
        // SFX mono mode duplicates (L+R) into both channels; stereo mode preserves independent L/R.
        if (!sfxStereoModeEnabled) {
            const float sfxMono = rawSfxInputL + rawSfxInputR;
            sfxPathInL = sfxMono;
            sfxPathInR = sfxMono;
        }

        const bool sfxEqBypassL = (sfxBypassMask & (1u << 0)) != 0u;
        const bool sfxEqBypassR = (sfxBypassMask & (1u << 1)) != 0u;
        const float sfxEqGainL = sfxEqBypassL ? 1.0f : controls.eqCompositeGain[4];
        const float sfxEqGainR = sfxEqBypassR ? 1.0f : controls.eqCompositeGain[5];
        const float sfxModeL = sfxPathInL * sfxEqGainL;
        const float sfxModeR = sfxPathInR * sfxEqGainR;

        float primaryGain[ClipMeterMap::kPrimaryChannelCount] = {0.0f, 0.0f, 0.0f, 0.0f};

        // Per-primary-lane normalize + gain stage before any cross-lane mixing.
        for (uint32_t ch = 0; ch < ClipMeterMap::kPrimaryChannelCount; ++ch) {
            accumulateClipMeter(ClipMeterMap::primaryIndex(ch, ClipMeterMap::PRIMARY_NORMALIZE), primaryIn[ch]);

            primaryGain[ch] = primaryIn[ch] * controls.primaryInputGain;
            accumulateClipMeter(ClipMeterMap::primaryIndex(ch, ClipMeterMap::PRIMARY_GAIN), primaryGain[ch]);
        }

        accumulateClipMeter(ClipMeterMap::SFX_LEFT_NORMALIZE, sfxModeL);
        accumulateClipMeter(ClipMeterMap::SFX_RIGHT_NORMALIZE, sfxModeR);

        const float sfxGainL = sfxModeL * controls.sfxInputGain;
        const float sfxGainR = sfxModeR * controls.sfxInputGain;
        accumulateClipMeter(ClipMeterMap::SFX_LEFT_GAIN, sfxGainL);
        accumulateClipMeter(ClipMeterMap::SFX_RIGHT_GAIN, sfxGainR);

        // Keep four independent primary lanes active through duck/sum and crossover stages.
        float primaryProgram[ClipMeterMap::kPrimaryChannelCount] = {
            primaryGain[0], primaryGain[1], primaryGain[2], primaryGain[3]
        };
        float shapedPrimary[ClipMeterMap::kPrimaryChannelCount] = {
            primaryProgram[0], primaryProgram[1], primaryProgram[2], primaryProgram[3]
        };

        // Four independent restoration/room-comp branches, one per primary lane.
        static ESP32Biquad roomCompLane[ClipMeterMap::kPrimaryChannelCount][4];
        static float laneMonoEnv[ClipMeterMap::kPrimaryChannelCount] = {0.0f, 0.0f, 0.0f, 0.0f};
        static float laneInputRmsEnergy[ClipMeterMap::kPrimaryChannelCount] = {0.0f, 0.0f, 0.0f, 0.0f};
        static float laneOutputGainScale[ClipMeterMap::kPrimaryChannelCount] = {1.0f, 1.0f, 1.0f, 1.0f};
        static float lanePhaseBass[ClipMeterMap::kPrimaryChannelCount] = {0.0f, 0.0f, 0.0f, 0.0f};
        static float lanePhaseInfrasonic[ClipMeterMap::kPrimaryChannelCount] = {0.0f, 0.0f, 0.0f, 0.0f};
        static float laneCurrentPhaseStepBass[ClipMeterMap::kPrimaryChannelCount] = {0.0f, 0.0f, 0.0f, 0.0f};
        static float laneCurrentPhaseStepInfrasonic[ClipMeterMap::kPrimaryChannelCount] = {0.0f, 0.0f, 0.0f, 0.0f};

        for (uint32_t ch = 0; ch < ClipMeterMap::kPrimaryChannelCount; ++ch) {
            const bool bassBypassLane = (primaryBypassMask & (1u << (4 + ch))) != 0u;
            if (bassBypassLane) {
                laneCurrentPhaseStepBass[ch] += 0.05f * (0.0f - laneCurrentPhaseStepBass[ch]);
                laneCurrentPhaseStepInfrasonic[ch] += 0.02f * (0.0f - laneCurrentPhaseStepInfrasonic[ch]);
                continue;
            }

            for (int i = 0; i < 4; ++i) {
                roomCompLane[ch][i].b0 = controls.roomCompB0;
                roomCompLane[ch][i].b1 = controls.roomCompB1;
                roomCompLane[ch][i].b2 = controls.roomCompB2;
                roomCompLane[ch][i].a1 = controls.roomCompA1;
                roomCompLane[ch][i].a2 = controls.roomCompA2;
            }

            const float filteredTrackerInput = primaryProgram[ch];

            laneMonoEnv[ch] = (envAlpha * std::fabs(filteredTrackerInput)) + ((1.0f - envAlpha) * laneMonoEnv[ch]);
            const bool isVoiceDetected = laneMonoEnv[ch] > (infraCfg.voiceSensitivity * 0.008f);

            const float currentSq = filteredTrackerInput * filteredTrackerInput;
            laneInputRmsEnergy[ch] = (rmsAlpha * currentSq) + ((1.0f - rmsAlpha) * laneInputRmsEnergy[ch]);
            const float currentRms = std::sqrt(laneInputRmsEnergy[ch]);

            if (currentRms > infraCfg.targetRmsThresholdLinear && currentRms > 0.001f) {
                const float excessGain = infraCfg.targetRmsThresholdLinear / currentRms;
                laneOutputGainScale[ch] += 0.1f * (excessGain - laneOutputGainScale[ch]);
            } else {
                laneOutputGainScale[ch] += 0.01f * (1.0f - laneOutputGainScale[ch]);
            }

            const float localTargetStepBass = infraState.targetStepBass[ch].load(std::memory_order_relaxed);
            laneCurrentPhaseStepBass[ch] += 0.05f * (localTargetStepBass - laneCurrentPhaseStepBass[ch]);
            lanePhaseBass[ch] += laneCurrentPhaseStepBass[ch];
            if (lanePhaseBass[ch] >= 6.283185307f) {
                lanePhaseBass[ch] -= 6.283185307f;
            }

            float synthSubharmonic = 0.0f;
            if (localTargetStepBass > 0.000523f && !isVoiceDetected) {
                synthSubharmonic = std::sin(lanePhaseBass[ch]) * currentRms * laneOutputGainScale[ch] * 1.414f;
            }

            float synthInfrasonic = 0.0f;
            const bool infrasonicBypassLane = (primaryBypassMask & (1u << (8 + ch))) != 0u;
            if (!infrasonicBypassLane) {
                const float localTargetStepInfrasonic = infraState.targetStepInfrasonic[ch].load(std::memory_order_relaxed);
                laneCurrentPhaseStepInfrasonic[ch] += 0.02f * (localTargetStepInfrasonic - laneCurrentPhaseStepInfrasonic[ch]);
                lanePhaseInfrasonic[ch] += laneCurrentPhaseStepInfrasonic[ch];
                if (lanePhaseInfrasonic[ch] >= 6.283185307f) {
                    lanePhaseInfrasonic[ch] -= 6.283185307f;
                }

                if (localTargetStepInfrasonic > 0.000261f && !isVoiceDetected) {
                    synthInfrasonic = std::sin(lanePhaseInfrasonic[ch]) * currentRms * laneOutputGainScale[ch] * 1.414f;
                }
            } else {
                laneCurrentPhaseStepInfrasonic[ch] += 0.02f * (0.0f - laneCurrentPhaseStepInfrasonic[ch]);
            }

            const float totalSynthesisLayer = (synthSubharmonic * infraCfg.subMix) + (synthInfrasonic * infraCfg.infraMix);
            float outputLayer = totalSynthesisLayer;
            const bool roomCompBypassLane = (primaryBypassMask & (1u << (12 + ch))) != 0u;
            if (!roomCompBypassLane) {
                float comp = totalSynthesisLayer;
                for (int i = 0; i < 4; ++i) {
                    roomCompLane[ch][i].process(comp, comp);
                }
                outputLayer = totalSynthesisLayer - comp;
            }
            shapedPrimary[ch] += outputLayer;
        }

        // Duck detector uses post-SFX-gain energy to attenuate primary program during notifications.
        const float sfxDuckDrive = std::max(std::fabs(sfxGainL), std::fabs(sfxGainR));
        if (sfxDuckDrive > duckEnvelope) {
            duckEnvelope += duckAttackAlpha * (sfxDuckDrive - duckEnvelope);
        } else {
            duckEnvelope += duckReleaseAlpha * (sfxDuckDrive - duckEnvelope);
        }

        if (duckEnvelope > 0.01f) {
            float targetDuckGain = 1.0f - (duckEnvelope * 2.5f);
            targetDuckGain = std::max(0.10f, targetDuckGain);
            duckMusicGain += 0.01f * (targetDuckGain - duckMusicGain);
        } else {
            duckMusicGain += 0.001f * (1.0f - duckMusicGain);
        }

        // Canonical order requirement: duck each primary lane first, then perform per-side SFX duplication at duck/sum.
        float preXoverPrimary[ClipMeterMap::kPrimaryChannelCount] = {0.0f, 0.0f, 0.0f, 0.0f};
        for (uint32_t ch = 0; ch < ClipMeterMap::kPrimaryChannelCount; ++ch) {
            const float duckedPrimary = shapedPrimary[ch] * duckMusicGain;
            const float sfxInject = ((ch & 0x1u) == 0u) ? sfxGainL : sfxGainR;
            preXoverPrimary[ch] = duckedPrimary + sfxInject;

            accumulateClipMeter(ClipMeterMap::primaryIndex(ch, ClipMeterMap::PRIMARY_SUM), preXoverPrimary[ch]);
            accumulateClipMeter(ClipMeterMap::primaryIndex(ch, ClipMeterMap::PRIMARY_PRE_XOVER), preXoverPrimary[ch]);
        }

        float x1High[ClipMeterMap::kPrimaryChannelCount] = {0.0f, 0.0f, 0.0f, 0.0f};
        float x1Low[ClipMeterMap::kPrimaryChannelCount] = {0.0f, 0.0f, 0.0f, 0.0f};
        float x2High[ClipMeterMap::kPrimaryChannelCount] = {0.0f, 0.0f, 0.0f, 0.0f};
        float x2Low[ClipMeterMap::kPrimaryChannelCount] = {0.0f, 0.0f, 0.0f, 0.0f};

        float stage3InputMono = 0.0f;
        float stage3WooferMono = 0.0f;
        float stage3InfraMono = 0.0f;

        // Stage-1: independent crossover per primary lane unless bypass is enabled.
        for (uint32_t ch = 0; ch < ClipMeterMap::kPrimaryChannelCount; ++ch) {
            const bool stage1BypassLane = (primaryBypassMask & (1u << (16 + ch))) != 0u;
            if (!stage1BypassLane) {
                lp175State[ch] += alpha175 * (preXoverPrimary[ch] - lp175State[ch]);
                x1Low[ch] = lp175State[ch];
                x1High[ch] = preXoverPrimary[ch] - x1Low[ch];
            } else {
                // Stage-1 bypass fan-out: feed the same signal to both Stage-2 and Stage-3 downstream paths.
                x1Low[ch] = preXoverPrimary[ch];
                x1High[ch] = preXoverPrimary[ch];
            }
        }

        for (uint32_t ch = 0; ch < ClipMeterMap::kPrimaryChannelCount; ++ch) {
            accumulateClipMeter(ClipMeterMap::primaryIndex(ch, ClipMeterMap::PRIMARY_X1_HP), x1High[ch]);
            accumulateClipMeter(ClipMeterMap::primaryIndex(ch, ClipMeterMap::PRIMARY_X1_LP), x1Low[ch]);
        }

        // Stage-2: independent crossover per primary lane on Stage-1 high branch unless bypass is enabled.
        for (uint32_t ch = 0; ch < ClipMeterMap::kPrimaryChannelCount; ++ch) {
            const bool stage2BypassLane = (primaryBypassMask & (1u << (20 + ch))) != 0u;
            if (!stage2BypassLane) {
                lp3kState[ch] += alpha3000 * (x1High[ch] - lp3kState[ch]);
                x2Low[ch] = lp3kState[ch];
                x2High[ch] = x1High[ch] - x2Low[ch];
            } else {
                x2High[ch] = x1High[ch];
                x2Low[ch] = 0.0f;
            }
        }

        for (uint32_t ch = 0; ch < ClipMeterMap::kPrimaryChannelCount; ++ch) {
            accumulateClipMeter(ClipMeterMap::primaryIndex(ch, ClipMeterMap::PRIMARY_X2_HP), x2High[ch]);
            accumulateClipMeter(ClipMeterMap::primaryIndex(ch, ClipMeterMap::PRIMARY_X2_LP), x2Low[ch]);
        }

        // Stage-3: single shared crossover after summing all Stage-1 LP outputs to mono unless bypass is enabled.
        stage3InputMono = 0.25f * (x1Low[0] + x1Low[1] + x1Low[2] + x1Low[3]);
        const bool stage3BypassMono = (primaryBypassMask & EN_PRIMARY_XOVER_STAGE3_MONO_BYPASS) != 0u;
        if (!stage3BypassMono) {
            lp25StateMono += alpha25 * (stage3InputMono - lp25StateMono);
            stage3WooferMono = stage3InputMono - lp25StateMono;
            stage3InfraMono = lp25StateMono;
        } else {
            stage3WooferMono = stage3InputMono;
            stage3InfraMono = 0.0f;
        }

        accumulateClipMeter(ClipMeterMap::MONO_X3_HP, std::fabs(stage3WooferMono));
        accumulateClipMeter(ClipMeterMap::MONO_X3_LP, std::fabs(stage3InfraMono));

        // Final hard limiter to float full-scale before PCM24 packing.
        auto inlineClamp = [](float val) { return std::max(-1.0f, std::min(1.0f, val)); };

        // Explicit 10-lane map: Stage-2 HP ch0..ch3, Stage-2 LP ch0..ch3, Stage-3 mono woofer, Stage-3 mono infrasonic.
        float outAll[10] = {
            x2High[0],      // lane 0: primary ch0 Stage-2 high-pass branch
            x2High[1],      // lane 1: primary ch1 Stage-2 high-pass branch
            x2High[2],      // lane 2: primary ch2 Stage-2 high-pass branch
            x2High[3],      // lane 3: primary ch3 Stage-2 high-pass branch
            x2Low[0],       // lane 4: primary ch0 Stage-2 low-pass branch
            x2Low[1],       // lane 5: primary ch1 Stage-2 low-pass branch
            x2Low[2],       // lane 6: primary ch2 Stage-2 low-pass branch
            x2Low[3],       // lane 7: primary ch3 Stage-2 low-pass branch
            stage3WooferMono, // lane 8: shared Stage-3 high-pass (woofer/sub mono)
            stage3InfraMono,  // lane 9: shared Stage-3 low-pass (infrasonic/shaker mono)
        };
        // Only first 10 lanes are intentionally populated today; remaining TDM lanes are zero-filled below.
        const int routeCount = std::min(numChannels, 10);
        for (int ch = 0; ch < routeCount; ++ch) {
            // Keep path-limit and global gain upstream of the final protection stage.
            // This preserves DC-block/service-bypass as the last DSP operation before packing.
            int gainIndex = ch;
            if (gainIndex > 5) {
                gainIndex = (ch <= 7) ? (ch - 2) : 4;
            }
            const float pathLimitedGain = controls.outputPathMaxGain[gainIndex];
            const float gainedPreProtect = outAll[ch] * finalGlobalGainMultiplier * pathLimitedGain;

            const float dcBlocked = (gainedPreProtect - dcInState[ch]) + (dcR * dcOutState[ch]);
            dcInState[ch] = gainedPreProtect;
            dcOutState[ch] = dcBlocked;
            const float speakerProtected = serviceBypassActive ? gainedPreProtect : dcBlocked;
            const float clamped = inlineClamp(speakerProtected);
            outputChannels[ch][s] = clamped;

            if (ch < 4) {
                localPeaks[ch] = std::max(localPeaks[ch], std::fabs(clamped));
            }
        }

        // Preserve deterministic frame content on unused lanes.
        for (int ch = routeCount; ch < numChannels; ++ch) {
            outputChannels[ch][s] = 0.0f;
        }

        for (uint32_t i = 0; i < ClipMeterMap::kTotalMeterCount; ++i) {
            meterDecimAccum[i] += sampleClipMeters[i];
        }

        // Meter decimation/presentation logic is intentionally independent of audio routing choices.
        ++meterDecimPhase;
        if (meterDecimPhase >= kMeterDecimation) {
            meterDecimPhase = 0;
            const uint32_t meterMode = (controls.clipMeterModeCtrl & kMeterModeMask);
            const uint32_t sampleStamp = controls.clipMeterSampleCounter + static_cast<uint32_t>(s + 1);

            for (uint32_t i = 0; i < ClipMeterMap::kTotalMeterCount; ++i) {
                const float raw48k = meterDecimAccum[i] / static_cast<float>(kMeterDecimation);
                meterDecimAccum[i] = 0.0f;

                float weighted = raw48k;
                if (meterMode == kMeterModeAWeighted) {
                    const float hp = aHpR * (aHpPrevY[i] + raw48k - aHpPrevX[i]);
                    aHpPrevX[i] = raw48k;
                    aHpPrevY[i] = hp;
                    aLpPrevY[i] += aLpAlpha * (hp - aLpPrevY[i]);
                    weighted = std::fabs(aLpPrevY[i]);
                } else if (meterMode == kMeterModeCWeighted) {
                    const float hp = cHpR * (cHpPrevY[i] + raw48k - cHpPrevX[i]);
                    cHpPrevX[i] = raw48k;
                    cHpPrevY[i] = hp;
                    cLpPrevY[i] += cLpAlpha * (hp - cLpPrevY[i]);
                    weighted = std::fabs(cLpPrevY[i]);
                }

                meterRuntimeCurrent[i] = weighted;
                if (weighted > meterPresentationPeakAccum[i]) {
                    meterPresentationPeakAccum[i] = weighted;
                }
                if (weighted > 0.0f) {
                    controls.clipMeterLastUpdateSample[i] = sampleStamp;
                }
            }
        }
    }

    for (int ch = 0; ch < 4; ++ch) {
        if (localPeaks[ch] > controls.channelPeakOutputs[ch]) {
            controls.channelPeakOutputs[ch] = localPeaks[ch];
        }
    }

    controls.stickyClipFlags64 |= localClipFlags;
    controls.stickyClipFlags = static_cast<uint32_t>(controls.stickyClipFlags64 & 0xFFFFFFFFu);

    const uint32_t blockEndSample = controls.clipMeterSampleCounter + static_cast<uint32_t>(numSamples);
    controls.clipMeterSampleCounter = blockEndSample;

    const uint32_t presentationIntervalSamples =
        (controls.meterPresentationIntervalSamples == 0u) ? 1600u : controls.meterPresentationIntervalSamples;
    meterPresentationAccumSamples += static_cast<uint32_t>(numSamples);
    while (meterPresentationAccumSamples >= presentationIntervalSamples) {
        meterPresentationAccumSamples -= presentationIntervalSamples;

        for (uint32_t i = 0; i < ClipMeterMap::kTotalMeterCount; ++i) {
            controls.clipMeterCurrent[i] = meterRuntimeCurrent[i];
            controls.clipMeterPeaks[i] = std::max(controls.clipMeterPeaks[i], meterPresentationPeakAccum[i]);
            meterPresentationPeakAccum[i] = 0.0f;
        }

        ++controls.meterPresentationSeq;
        controls.meterPresentationStatus |= kMeterPresentationReadyBit;

        if ((controls.irqEnableMask & kIrqBitMeterInterval) != 0u) {
            gpio_set_level(kMeterReadyIrqGpio, 1);
        }
    }

    if ((localClipFlags != 0u) && ((controls.irqEnableMask & kIrqBitClipLatch) != 0u)) {
        gpio_set_level(kMeterReadyIrqGpio, 1);
    }

    uint32_t statusUpdate = 0;
    if (controls.currentSystemGain * controls.userMuteRampGain == 0.0f) {
        statusUpdate |= (1u << 0);
    }
    if (controls.currentState == PowerSeqState::BOOT_HOLD_TIME || controls.currentState == PowerSeqState::BOOT_RAMP_UP) {
        statusUpdate |= (1u << 1);
    }
    if (controls.currentState != PowerSeqState::WAITING_FOR_ADC) {
        statusUpdate |= (1u << 2);
    }
    if (controls.stickyClipFlags64 != 0u) {
        statusUpdate |= (1u << 3);
    }
    if ((controls.meterPresentationStatus & kMeterPresentationReadyBit) != 0u) {
        statusUpdate |= (1u << 4);
    }
    controls.systemStatusMask = statusUpdate;
}
