#pragma once

// =============================
// 48k->192k Linear ASRC
// =============================
class Core1ASRC {
public:
    // Linear-interpolation 4x upsampler state for 48 kHz -> 192 kHz conversion.
    float phaseAccumulator = 0.0f;
    float lastSampleL = 0.0f;
    float currentSampleL = 0.0f;
    float lastSampleR = 0.0f;
    float currentSampleR = 0.0f;

    // =============================
    // Source Frame Update
    // =============================
    inline void pushNew48kHzFrame(float newL, float newR) {
        // Shift current frame into "last" and store fresh source frame.
        lastSampleL = currentSampleL;
        currentSampleL = newL;

        lastSampleR = currentSampleR;
        currentSampleR = newR;
    }

    // =============================
    // Interpolated Output
    // =============================
    inline void generateNext192kHzFrame(float& outL, float& outR) {
        // Interpolate between last and current source points.
        outL = lastSampleL + (currentSampleL - lastSampleL) * phaseAccumulator;
        outR = lastSampleR + (currentSampleR - lastSampleR) * phaseAccumulator;

        // Advance quarter-step each call (4 output samples per input sample).
        phaseAccumulator += 0.25f;
        if (phaseAccumulator >= 1.0f) {
            phaseAccumulator -= 1.0f;
        }
    }
};
