#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

// =============================
// Pink Noise Generator
// =============================
class Core1PinkNoiseGenerator {
public:
    // Voss-McCartney pink noise implementation with three octave taps
    struct OctaveTap {
        float value = 0.0f;
        uint32_t updatePeriod = 0;  // How many samples until next update
        uint32_t sampleCounter = 0; // Current position in update period
    };

    OctaveTap tap1;  // Updates every sample (white noise component)
    OctaveTap tap2;  // Updates every ~16 samples
    OctaveTap tap3;  // Updates every ~256 samples

    uint32_t xorstate = 0x12345678u;  // Simple PRNG state for random taps
    float currentGain = 1.0f;
    bool enabled = false;

    static constexpr float OUTPUT_SCALE = 0.5f;  // Normalize to prevent clipping

    // =============================
    // Initialization
    // =============================
    void initialize() {
        // Octave spacing chosen as low-cost approximation for colored-noise test injection.
        // Set initial update periods for octave taps (powers of 2 relative spacing)
        tap1.updatePeriod = 1;
        tap2.updatePeriod = 16;
        tap3.updatePeriod = 256;

        tap1.sampleCounter = 0;
        tap2.sampleCounter = 0;
        tap3.sampleCounter = 0;

        // Initialize tap values
        tap1.value = randomFloat();
        tap2.value = randomFloat();
        tap3.value = randomFloat();

        xorstate = 0x12345678u;
        currentGain = 1.0f;
        enabled = false;
    }

    // =============================
    // Runtime Controls
    // =============================
    inline void setGain(float gain01) {
        // gain01: 0.0 = -infinity (silence), 1.0 = unity (0 dB)
        currentGain = std::max(0.0f, std::min(1.0f, gain01));
    }

    inline void setEnabled(bool enable) {
        enabled = enable;
    }

    // =============================
    // Per-Sample Generation
    // =============================
    inline float generateSample() {
        // Runtime gate allows generator to exist with negligible cost when disabled.
        if (!enabled || currentGain <= 0.0f) {
            return 0.0f;
        }

        // Update tap 1 every sample (white noise contribution)
        tap1.sampleCounter = 0;
        tap1.value = randomFloat();

        // Update tap 2 every 16 samples
        tap2.sampleCounter++;
        if (tap2.sampleCounter >= tap2.updatePeriod) {
            tap2.sampleCounter = 0;
            tap2.value = randomFloat();
        }

        // Update tap 3 every 256 samples
        tap3.sampleCounter++;
        if (tap3.sampleCounter >= tap3.updatePeriod) {
            tap3.sampleCounter = 0;
            tap3.value = randomFloat();
        }

        // Combine taps: Voss-McCartney color filter (sum of octave-spaced noise)
        // Each tap contributes equally; sum is normalized by OUTPUT_SCALE
        float colored = (tap1.value + tap2.value + tap3.value) * OUTPUT_SCALE;

        // Apply gain: 1.0 = full scale, 0.0 = silence
        return colored * currentGain;
    }

private:
    // Xorshift32 PRNG: returns [-1.0, +1.0] uniformly distributed
    inline float randomFloat() {
        xorstate ^= xorstate << 13;
        xorstate ^= xorstate >> 17;
        xorstate ^= xorstate << 5;

        // Convert uint32 to float in [-1.0, +1.0]
        uint32_t temp = xorstate & 0x7FFFFFFFu;  // Clear sign bit to make positive
        float normalized = (static_cast<float>(temp) / 1073741824.0f) - 1.0f;  // Divide by 2^30, center at -1..+1
        return normalized;
    }
};
