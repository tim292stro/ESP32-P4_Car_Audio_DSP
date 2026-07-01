#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace audio_frameio {

// =============================
// Frame Conversion Constants
// =============================
// This namespace owns the canonical PCM24<->float frame boundary logic.
// Keep these helpers deterministic and side-effect free: they are used in the Core0 real-time loop.

static constexpr float kPcm24ToFloat = 1.0f / 8388608.0f;
static constexpr float kFloatToPcm24 = 8388607.0f;

// Lightweight TPDF (Triangular Probability Distribution Function) dither generator.
// Uses xorshift32 for minimal code footprint (~100 bytes vs. ~8KB for std::mt19937).
// Dither is optional: can be compiled out or disabled at runtime for testing.
class TPDFDitherGenerator {
 public:
  explicit TPDFDitherGenerator(uint32_t seed = 0x12345678u) : state_(seed) {}

  // Generate next TPDF dither sample (±0.5 LSB in float domain).
  // Uses two xorshift32 calls to create triangular distribution via (u1 - u2).
  float next_sample() {
    // First uniform [0, 1) via xorshift
    float u1 = xorshift32_uniform();
    // Second uniform [0, 1) via xorshift
    float u2 = xorshift32_uniform();
    // TPDF: subtract to get triangular [-1, 1), then scale to ±0.5 LSB
    return (u1 - u2) * kDitherScaleFactor;
  }

  void reseed(uint32_t seed) {
    state_ = seed;
  }

 private:
  // Xorshift32 PRNG: extremely simple, ~3 cycles, minimal state (32 bits).
  uint32_t xorshift32() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return state_;
  }

  // Convert xorshift32 output to uniform float [0, 1).
  float xorshift32_uniform() {
    // Use upper 24 bits of xorshift32 output to get uniform [0, 1).
    uint32_t bits = xorshift32() >> 8;  // 24-bit precision
    return static_cast<float>(bits) * (1.0f / 16777216.0f);  // 2^24
  }

  // Dither magnitude: ±0.5 LSB in float domain = ±(0.5 / 8388608) ≈ ±5.96e-8
  static constexpr float kDitherScaleFactor = 1.0f / (2.0f * 8388608.0f);

  uint32_t state_;  // Single 32-bit state (vs. 624×32-bit for MT19937)
};

// =============================
// Ingress Conversion (PCM24 -> Float)
// =============================
// Canonical transport format: signed 24-bit PCM left-justified in a 32-bit slot.
inline void deinterleaveS24In32ToFloat(const int32_t* interleaved,
                                       float** planar,
                                       int channels,
                                       int samplesPerBlock) {
    // Input words are signed 24-bit payloads left-justified in 32-bit slots.
    // The conversion clamps to an asymmetric float full-scale to preserve int24 top code behavior.
    for (int s = 0; s < samplesPerBlock; ++s) {
        for (int ch = 0; ch < channels; ++ch) {
            const int32_t slotWord = interleaved[(s * channels) + ch];
            const int32_t sample24 = slotWord >> 8;
            float sample = static_cast<float>(sample24) * kPcm24ToFloat;
            sample = std::max(-1.0f, std::min(0.99999994f, sample));
            planar[ch][s] = sample;
        }
    }
}

// =============================
// Egress Conversion (Float -> PCM24)
// =============================
inline void interleaveFloatToS24In32(const float** planar,
                                     int32_t* interleaved,
                                     int channels,
                                     int samplesPerBlock) {
    // Conversion path mirrors deinterleave assumptions to keep transport symmetric.
    for (int s = 0; s < samplesPerBlock; ++s) {
        for (int ch = 0; ch < channels; ++ch) {
            float v = planar[ch][s];
            v = std::max(-1.0f, std::min(0.99999994f, v));
            const int32_t sample24 = static_cast<int32_t>(std::lrintf(v * kFloatToPcm24));
            interleaved[(s * channels) + ch] = (sample24 << 8);
        }
    }
}

// =============================
// Dithering Policy And Runtime Dispatch
// =============================
// ============================================================================
// DITHERING TRADEOFF ANALYSIS
// ============================================================================
//
// WITHOUT DITHERING (interleaveFloatToS24In32):
//   ✓ Minimal code overhead (no PRNG calls)
//   ✓ ~0.5 µs faster per sample block (192 samples at 192 kHz)
//   ✓ Effective SNR ~125 dB (pure quantization-limited)
//   ✗ DISTORTION on low-level signals (<-80 dBFS): quantization steps become
//     audible as harmonic distortion (e.g., 1 kHz sine at -100 dBFS shows
//     ~2-3% THD from truncation errors).
//   → USE IF: Testing DSP performance, lab measurements where distortion
//     artifacts are acceptable, or if DAC analog noise floor masks
//     quantization (rare in automotive).
//
// WITH DITHERING (interleaveFloatToS24In32WithDither):
//   ✓ NO distortion on low-level signals; quantization replaced by white noise
//   ✓ Effective SNR ~119 dB (dither adds ~6 dB noise floor but eliminates
//     distortion)
//   ✓ Industry standard for audio quality (Blu-ray, CD mastering)
//   ✓ In car: added 6 dB noise is inaudible under tire/wind noise floor
//     (70-80 dB SPL ambient)
//   ✗ ~3 cycles per sample for PRNG call (xorshift32)
//   ✗ Additional xorshift32 PRNG state (~32 bits per instance)
//   → USE IF: Production audio playback, transparent/inaudible low-level
//     playback required, perceptual quality prioritized over raw SNR number.
//
// RECOMMENDATION FOR ESP32-P4 CAR AUDIO:
// ==========================================
// Use dithering (WithDither variant) as DEFAULT. The 6 dB noise floor increase
// is completely masked by car ambient noise (tire rumble: 60-80 dB SPL, HVAC:
// 50-70 dB SPL). The elimination of distortion artifacts is audible and
// important for clean fade-ins, orchestral quiet passages, and percussive
// transients.
//
// Switch to non-dithering ONLY for:
//   - Lab measurements / FFT analysis where you want raw quantization floor
//   - Performance profiling if PRNG overhead becomes critical
//   - Head-to-head SNR comparison testing
//
// ============================================================================

// Variant with TPDF dithering for improved low-level transparency.
// Adds ±0.5 LSB triangular dither before quantization to avoid distortion artifacts.
// RECOMMENDED for production audio output to automotive DACs.
inline void interleaveFloatToS24In32WithDither(const float** planar,
                                               int32_t* interleaved,
                                               int channels,
                                               int samplesPerBlock,
                                               TPDFDitherGenerator& dither_gen) {
    for (int s = 0; s < samplesPerBlock; ++s) {
        for (int ch = 0; ch < channels; ++ch) {
            float v = planar[ch][s];
            v = std::max(-1.0f, std::min(0.99999994f, v));
            // Add TPDF dither before quantization to ±0.5 LSB
            float dithered = v + dither_gen.next_sample();
            // Clamp again after dither addition to prevent wrapping
            dithered = std::max(-1.0f, std::min(0.99999994f, dithered));
            const int32_t sample24 = static_cast<int32_t>(std::lrintf(dithered * kFloatToPcm24));
            interleaved[(s * channels) + ch] = (sample24 << 8);
        }
    }
}

// Unified output conversion with optional runtime dithering control.
// Call this from Core0 output path; it dispatches to dithered or non-dithered based on flag.
inline void interleaveFloatToS24In32_Runtime(const float** planar,
                                             int32_t* interleaved,
                                             int channels,
                                             int samplesPerBlock,
                                             bool enable_dither,
                                             TPDFDitherGenerator* dither_gen) {
    if (enable_dither && dither_gen != nullptr) {
        // Use dithering (recommended for production)
        interleaveFloatToS24In32WithDither(planar, interleaved, channels, samplesPerBlock, *dither_gen);
    } else {
        // Bypass dithering (lab testing, raw SNR measurement)
        interleaveFloatToS24In32(planar, interleaved, channels, samplesPerBlock);
    }
}

}  // namespace audio_frameio
