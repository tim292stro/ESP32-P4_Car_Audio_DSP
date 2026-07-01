#pragma once

#include <cmath>
#include <cstdint>

// =============================
// Analysis Decimator
// =============================
class Core1Decimator {
public:
    // Two cascaded low-pass biquads used for staged rate reduction (192 kHz -> 24 kHz -> 3 kHz valid strobe).
    float stage1_coeffs[5] = {0.0f};
    float stage2_coeffs[5] = {0.0f};

    float s1_x[2] = {0.0f, 0.0f};
    float s1_y[2] = {0.0f, 0.0f};
    float s2_x[2] = {0.0f, 0.0f};
    float s2_y[2] = {0.0f, 0.0f};

    // =============================
    // Initialization
    // =============================
    void init() {
        // Stage 1 suppresses ultrasonic content before first decimation tap.
        computeBiquadLPF(192000.0f, 12000.0f, stage1_coeffs);
        // Stage 2 tightens analysis bandwidth for tracker-side processing.
        computeBiquadLPF(24000.0f, 1500.0f, stage2_coeffs);
    }

    // =============================
    // Per-Sample Processing
    // =============================
    inline void processSample(float input, float& output, bool& valid3kHz) {
        // output is valid only when valid3kHz is true.
        valid3kHz = false;
        float out1 = 0.0f;
        processStage(input, out1, stage1_coeffs, s1_x, s1_y);
        ++s1_counter;

        // Decimate by 8 after stage 1.
        if ((s1_counter & 0x7u) == 0u) {
            float out2 = 0.0f;
            processStage(out1, out2, stage2_coeffs, s2_x, s2_y);
            ++s2_counter;

            // Decimate by another 8 after stage 2.
            if ((s2_counter & 0x7u) == 0u) {
                output = out2;
                valid3kHz = true;
            }
        }
    }

private:
    // =============================
    // Internal State
    // =============================
    uint32_t s1_counter = 0;
    uint32_t s2_counter = 0;

    static void processStage(float in, float& out, float* c, float* x, float* y) {
        // Biquad direct-form update.
        out = c[0] * in + c[1] * x[0] + c[2] * x[1] - c[3] * y[0] - c[4] * y[1];
        x[1] = x[0];
        x[0] = in;
        y[1] = y[0];
        y[0] = out;
    }

    static void computeBiquadLPF(float sampleRate, float cutoff, float* c) {
        // RBJ-style low-pass coefficient generation.
        const float omega = 2.0f * 3.14159265f * cutoff / sampleRate;
        const float alpha = std::sin(omega) / 1.41421356f;
        const float cosw = std::cos(omega);
        const float a0 = 1.0f + alpha;

        c[0] = ((1.0f - cosw) * 0.5f) / a0;
        c[1] = (1.0f - cosw) / a0;
        c[2] = ((1.0f - cosw) * 0.5f) / a0;
        c[3] = (-2.0f * cosw) / a0;
        c[4] = (1.0f - alpha) / a0;
    }
};
