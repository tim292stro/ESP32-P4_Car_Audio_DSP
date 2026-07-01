#pragma once

#include <atomic>
#include <cstdint>

// =============================
// Pitch Tracker
// =============================
class Core1PitchTracker {
public:
    static constexpr int DOWN_BUF_SIZE = 256;

    float trackBuffer[DOWN_BUF_SIZE] = {0.0f};
    uint32_t writeIdx = 0;

    // =============================
    // Analysis Sample Ingest
    // =============================
    void process3kHzSample(float decimatedSample,
                           std::atomic<float>& targetStepBass,
                           std::atomic<float>& targetStepInfrasonic) {
        // Ring-buffer latest analysis samples from decimated monitor stream.
        trackBuffer[writeIdx] = decimatedSample;
        writeIdx = (writeIdx + 1u) % DOWN_BUF_SIZE;

        // Run pitch update at lower cadence to bound CPU cost.
        ++sampleCounter;
        if (sampleCounter >= 32u) {
            sampleCounter = 0;
            runAutocorrelation(targetStepBass, targetStepInfrasonic);
        }
    }

private:
    // =============================
    // Internal Correlation Pass
    // =============================
    uint32_t sampleCounter = 0;

    void runAutocorrelation(std::atomic<float>& bassStep, std::atomic<float>& infrasonicStep) {
        // Search lag window matching expected source fundamentals for this application.
        float maxCorr1 = -1.0f;
        int bestLag1 = -1;

        for (int lag = 23; lag <= 79; ++lag) {
            float corr = 0.0f;
            for (int i = 0; i < 128; ++i) {
                const int idx1 = static_cast<int>((writeIdx + DOWN_BUF_SIZE - i - 1) % DOWN_BUF_SIZE);
                const int idx2 = static_cast<int>((writeIdx + DOWN_BUF_SIZE - i - 1 - lag + DOWN_BUF_SIZE) % DOWN_BUF_SIZE);
                corr += trackBuffer[idx1] * trackBuffer[idx2];
            }
            if (corr > maxCorr1) {
                maxCorr1 = corr;
                bestLag1 = lag;
            }
        }

        // Convert best lag in 3 kHz domain to synthesis phase steps in 192 kHz domain.
        if (bestLag1 > 0 && maxCorr1 > 0.01f) {
            const float freq3kHz = 3000.0f / static_cast<float>(bestLag1);
            const float targetBassFreq = freq3kHz * 0.5f;
            bassStep.store((2.0f * 3.14159265f * targetBassFreq) / 192000.0f, std::memory_order_relaxed);

            const float targetInfrasonicFreq = targetBassFreq * 0.5f;
            if (targetInfrasonicFreq >= 8.0f) {
                infrasonicStep.store((2.0f * 3.14159265f * targetInfrasonicFreq) / 192000.0f, std::memory_order_relaxed);
            } else {
                infrasonicStep.store(0.0f, std::memory_order_relaxed);
            }
        } else {
            bassStep.store(0.0f, std::memory_order_relaxed);
            infrasonicStep.store(0.0f, std::memory_order_relaxed);
        }
    }
};
