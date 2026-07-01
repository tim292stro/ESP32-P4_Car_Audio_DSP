# EQ Settings

Back to the [root README](../README.md).

## Purpose

This document captures the two active EQ band plans and the grouped SPI register layout for all 6 unique EQ channels.

## Band Plans

## Primary Input EQ Plan (31 bands, ISO-like 1/3 octave)

Used by primary channels 0-3.

| Band | Center Frequency (Hz) |
| --- | --- |
| 0 | 20 |
| 1 | 25 |
| 2 | 31.5 |
| 3 | 40 |
| 4 | 50 |
| 5 | 63 |
| 6 | 80 |
| 7 | 100 |
| 8 | 125 |
| 9 | 160 |
| 10 | 200 |
| 11 | 250 |
| 12 | 315 |
| 13 | 400 |
| 14 | 500 |
| 15 | 630 |
| 16 | 800 |
| 17 | 1000 |
| 18 | 1250 |
| 19 | 1600 |
| 20 | 2000 |
| 21 | 2500 |
| 22 | 3150 |
| 23 | 4000 |
| 24 | 5000 |
| 25 | 6300 |
| 26 | 8000 |
| 27 | 10000 |
| 28 | 12500 |
| 29 | 16000 |
| 30 | 20000 |

## SFX Input EQ Plan (15 bands)

Used by SFX channels 4..5.

| Band | Center Frequency (Hz) |
| --- | --- |
| 0 | 25 |
| 1 | 40 |
| 2 | 63 |
| 3 | 100 |
| 4 | 160 |
| 5 | 250 |
| 6 | 400 |
| 7 | 630 |
| 8 | 1000 |
| 9 | 1600 |
| 10 | 2500 |
| 11 | 4000 |
| 12 | 6300 |
| 13 | 10000 |
| 14 | 16000 |

## Grouped Register Layout (6 Unique EQs)

Every EQ group base starts at an address with `0x..00` LSB and has a dedicated `0x100` window.

Shared offsets inside each group:

- `+0x00` `EQ_INFO` (R)
- `+0x04` `EQ_APPLY` (RW), write key `0x45515131`
- `+0x08` `EQ_COMPOSITE_GAIN` (R, float32)
- `+0x10 + 2*n` `EQ_BAND_GAIN_n` (RW, **int16 Q14**), `n=0..30`

## EQ Group Table

| Group | Channel Type | Base Address | Active Bands | Band Gain Address Range |
| --- | --- | --- | --- | --- |
| EQ0 | Primary CH0 | `0x0200` | 31 | `0x0210` to `0x0278` |
| EQ1 | Primary CH1 | `0x0300` | 31 | `0x0310` to `0x0378` |
| EQ2 | Primary CH2 | `0x0400` | 31 | `0x0410` to `0x0478` |
| EQ3 | Primary CH3 | `0x0500` | 31 | `0x0510` to `0x0578` |
| EQ4 | SFX CH4 | `0x0600` | 15 | `0x0610` to `0x0638` (active), `0x063A` to `0x0678` ignored/unity |
| EQ5 | SFX CH5 | `0x0700` | 15 | `0x0710` to `0x0738` (active), `0x073A` to `0x0778` ignored/unity |

## Q14 Fixed-Point Control Format

**Band Gain Representation:** Signed 16-bit Q14 format (14 fractional bits)

| Aspect | Specification | Notes |
| --- | --- | --- |
| **Range** | ±2.0 (−1.0 to +1.0 with sign) | Exactly covers ±6 dB (0.5x to 2.0x linear) |
| **Resolution** | 2^−14 ≈ 0.000061 per LSB | ≈ 0.52 mdB per step; imperceptible for tuning |
| **Register encoding** | int16 in lower 16 bits | High 16 bits ignored on write, read as zero |
| **Firmware conversion** | `float_gain = (int16_q14 / 16384.0f)` | 2–3 cycles overhead per band; negligible |
| **SNR impact** | **None** | Control granularity does not affect DSP noise floor; conversion is register I/O only |
| **Rationale** | 50% bandwidth savings vs. float32 | See [Audio Topology & SNR Justifications](Audio-Topology_Justifications.md) for detailed justification |
| **Practical tuning** | Unchanged | 0.52 mdB steps far exceed measurement uncertainty (±3–4 dB automotive environment) |

**Encoding Examples (Gain/Cut Multiplier):**

| Hex Value | Decimal | Linear Gain | dB | Effect |
| --- | --- | --- | --- | --- |
| `0x4000` | 16384 | 1.0 | 0 dB | Unity (no change) |
| `0x8000` | −32768 | ~2.0 | +6 dB | Boost: amplify band 2× (max allowed) |
| `0x2000` | 8192 | 0.5 | −6 dB | Cut: attenuate band to 1/2 (max allowed) |
| `0x1000` | 4096 | 0.25 | −12 dB | Firmware clamps; not used (exceeds ±6 dB limit) |
| `0x0001` | 1 | ~0.000061 | −120 dB | Firmware clamps; nearly silent (not used) |

## Runtime Notes

- Band gain writes are sanitized to finite safe values before use.
- Composite gain is recomputed on each band write and on `EQ_APPLY`.
- Core0 currently consumes composite gains per channel for low-cost runtime integration.
- This register map is grouped to allow bulk updates per EQ without touching unrelated control regions.

## SNR and Noise Floor Optimization

### Per-Band Gain Limits

To minimize cumulative round-off noise from the cascade of 186 biquad filters, observe the following best practices:

- **Per-band gain range**: Limit individual EQ band gains to **±6 dB** (0.5x to 2.0x linear scaling in Q14 format: `0x2000` to `0x4000`).
  - Conservative tuning (<±3 dB per band) yields the best SNR: ~96 dB effective output SNR.
  - Moderate tuning (±6 dB per band) maintains good SNR: ~92 dB effective output SNR.
  - Aggressive tuning (>±12 dB per band) significantly degrades SNR and should be avoided unless necessary for specific frequency response correction.

- **Total EQ energy**: Monitor total composite gain across all active bands.
  - Target composition sum: **±3 dB of unity** (i.e., composite gain between 0.7x and 1.4x).
  - Excessive total boost (>+6 dB across all bands) concentrates signal energy early in the filter cascade, amplifying subsequent quantization noise.
  - If tuning requires >+6 dB total boost, re-evaluate using master volume control instead of EQ gain.

### Filter Ordering for Minimal Signal Swing

Within each EQ group, cascaded biquad filters accumulate noise fastest when intermediate signals peak. To minimize this, apply filters in order of increasing signal modification:

1. **High-pass and Shelving Filters** (Input stage, first): Removes DC/subsonic content and manages extreme LF boost (if needed).
2. **Peaking EQ Filters** (Mid-cascade): Boosts/cuts narrow frequency bands; minimal signal disruption.
3. **Room Compensation & Presence Filters** (Late stage): Corrects steady-state response without amplifying transients.

**Rationale:** Placing large-gain filters early concentrates round-off noise amplification through subsequent stages. By placing them late, noise is less multiply-amplified by remaining filter sections.

### Interaction with Composite Gain

- The firmware computes `EQ_COMPOSITE_GAIN` by multiplying all per-band linear gains: $\prod_{i=0}^{N-1} g_i$.
- Core0 applies this composite gain multiplicatively after the full EQ cascade to normalize overall channel level.
- **Important**: Do NOT use per-band gains to achieve large overall level changes (e.g., channel attenuation). Use `MTR_VOL_PATH_MAX_CH*` or `MTR_VOL_MASTER` instead to minimize intermediate signal swing.

The interrupt block has been moved low in address space to minimize host reads:

- `0x0080` `MTR_IRQ_STATUS`
- `0x0084` `MTR_IRQ_ENABLE`
- `0x0088` `MTR_IRQ_CTRL`

A single read of `MTR_IRQ_STATUS` returns pending sources, enabled mask mirror, and meter sequence LSB16.

## Host Update Example

Recommended host sequence for one full EQ-group update:

1. Read `0x0000` (`ABI_VERSION`) and `0x0004` (`ABI_CAPS`) to confirm grouped-EQ map support.
2. Pick one EQ group base (for example `0x0400` for `PRIMARY_CH2`).
3. Read `base + 0x00` (`EQ_INFO`) to get active band count.
4. Write all active `EQ_BAND_GAIN_n` registers at `base + 0x10 + 4*n`.
5. Write key `0x45515131` to `base + 0x04` (`EQ_APPLY`).
6. Read back `base + 0x08` (`EQ_COMPOSITE_GAIN`) for runtime confirmation.
7. Optionally read `0x0080` (`MTR_IRQ_STATUS`) once to clear meter-ready source and observe pending summary.
