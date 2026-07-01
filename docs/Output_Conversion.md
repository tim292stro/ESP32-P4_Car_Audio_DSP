# Output Conversion & Dithering Control

Back to the [root README](../README.md).

See also [Metering](Metering.md), [Audio Topology & SNR Justifications](Audio-Topology_Justifications.md), and the [SPI Register Manual](Register_Manual.md).

## Purpose

The output conversion stage transforms 32-bit float audio samples to 24-bit PCM for the automotive DAC interface. This document covers the dithering control that can be toggled at runtime without recompilation.

## Registers

| Address | Name | Access | Description |
| --- | --- | --- | --- |
| `0x0148` | `MTR_OUT_DITHER_CTRL` | RW | Bit0 enables TPDF dithering on output conversion; Bit1-31 reserved. |

## Register Details

### `MTR_OUT_DITHER_CTRL` (0x0148)

**Bit Field:**

- Bit 0: **DITHER_ENABLE** (default: `1` — dithering ON)
  - `1`: TPDF dithering active (recommended for production/car audio)
  - `0`: Dithering disabled; uses direct quantization (lab/FFT measurement only)
- Bits 1–31: Reserved for future use; read as zero.

**Reset Value:** `0x00000001` (dithering enabled by default)

**Access:** Read/Write, effective immediately (no latency; changes apply to next output block).

## Dithering Modes

### With Dithering (Bit 0 = 1) — DEFAULT & RECOMMENDED

**When:** Production deployment, car audio playback, user-facing system.

**What:** TPDF (Triangular Probability Distribution Function) dithering (±0.5 LSB) is applied before 24-bit PCM quantization.

**Benefits:**

- Eliminates quantization distortion on low-level signals (<−80 dBFS)
- Fade-ins, quiet passages, and orchestral content sound clean and transparent
- Low-level distortion replaced by inaudible white noise
- Effective SNR: 119–124 dB at output

**Performance:**

- Cost: ~3 cycles per sample for PRNG (xorshift32)
- Per-block overhead: ~0.5 µs at 192 kHz / 192 samples
- Negligible impact on core scheduling (0.08% of 5.2 µs window)

**Implementation:**

- TPDF generator: Xorshift32 PRNG (32-bit state, 100 bytes code)
- Dither magnitude: ±5.96e-8 in float domain (±0.5 LSB)

### Without Dithering (Bit 0 = 0) — LAB TESTING ONLY

**When:** FFT measurements, raw SNR benchmarking, performance profiling, comparison testing.

**What:** Direct truncation to 24-bit PCM without PRNG overhead.

**Benefits:**

- Theoretical SNR: 125–130 dB (pure quantization floor)
- ~0.5 µs faster per block (but negligible in practice)
- Raw quantization characteristics visible in FFT analysis

**Drawbacks:**

- 2–3% THD on low-level test signals (<−80 dBFS) due to quantization steps
- Not recommended for production audio playback

**Implementation:**

- No PRNG; standard C++ float→int conversion via `lrintf()`

## Runtime Switching Example

```python
def enable_dithering():
    """Enable TPDF dithering (production mode)."""
    write_u32(0x0148, 0x00000001)

def disable_dithering():
    """Disable dithering (lab/test mode)."""
    write_u32(0x0148, 0x00000000)

def read_dither_status():
    """Check current dithering status."""
    status = read_u32(0x0148)
    return (status & 0x1) != 0

# Example: A/B testing
print("Testing WITHOUT dither (raw SNR measurement)...")
disable_dithering()
time.sleep(2)  # Run lab measurement

print("Testing WITH dither (perceptual quality)...")
enable_dithering()
time.sleep(2)  # Run user listening test

print(f"Dither is currently: {'ON' if read_dither_status() else 'OFF'}")
```

## Recommended Host Policy

1. **On Boot:** Verify `MTR_OUT_DITHER_CTRL` Bit0 is set to `1` (dithering enabled).
   - If a system starts with dithering accidentally disabled, user will perceive audible distortion on quiet content.
   - Firmware default is `1`, but host should confirm during initialization.

2. **During Normal Operation:** Keep dithering enabled; do not change this setting live.
   - Runtime switching is supported (changes apply immediately), but unnecessary in production.
   - Switching during playback will cause brief discontinuity in noise floor (imperceptible but not recommended).

3. **For Lab Measurements:**
   - Disable dithering before FFT measurements: `write_u32(0x0148, 0x00000000)`
   - This reveals raw quantization floor for instrument calibration.
   - Re-enable after testing: `write_u32(0x0148, 0x00000001)`

4. **For A/B Testing:**
   - Can compare dithered vs. non-dithered output by toggling this bit.
   - **Result:** Non-dithered sounds slightly "grainier" on low-level signals; dithered sounds smoother.
   - Distortion is typically inaudible in car environment (70–80 dB ambient noise).

## SNR Impact

| Mode | Effective SNR | Use Case |
| --- | --- | --- |
| **Dithering ON (default)** | 119–124 dB | Production car audio, user playback |
| **Dithering OFF** | 125–130 dB (theory) | FFT analysis, raw quantization measurement |

**In Car Environment:**

- Ambient noise floor: 70–80 dB SPL (highway speeds)
- Either mode achieves inaudible noise floor
- Perceptual difference: Dithering eliminates audible distortion (preferred)

## Implementation Notes

The firmware uses `interleaveFloatToS24In32_Runtime()` which dispatches to:

- `interleaveFloatToS24In32WithDither()` when Bit0 = 1 (default)
- `interleaveFloatToS24In32()` when Bit0 = 0 (lab mode)

Both functions are compiled in; no compile-time decision required. The register bit controls the runtime choice.

## Related Documentation

- [Audio Topology & SNR Justifications](Audio-Topology_Justifications.md) — Detailed SNR modeling, Q14 fixed-point control justification, and dithering rationale
- [Metering](Metering.md) — Output peak and clip tracking (independent of dithering mode)
- [Calibration Guide](Calibration_Guide.md) — System commissioning and verification
