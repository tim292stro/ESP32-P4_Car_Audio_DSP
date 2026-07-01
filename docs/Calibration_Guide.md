# Calibration Guide

Back to the [root README](../README.md).

See also [Volume Settings](Volume_Settings.md), [EQ Settings](EQ_Settings.md), and [Metering](Metering.md).

## Purpose

This guide explains how to calibrate levels, crossover points, and tonal balance using the host control surface.

## Inputs You Must Decide Up Front

| Input | Why It Is Needed |
| --- | --- |
| Target output voltage or SPL per lane | Defines path max gain ceilings. |
| Crossover design frequencies | Determines sub, midbass, and high-pass split behavior. |
| Preferred loudness contour | Sets ISO226 depth/reference tradeoff. |
| EQ baseline and tuning target | Controls tonal balance without violating gain headroom. |
| Clip margin target | Prevents repeated clipping under real program material. |

## Calibration Functions (Host API Style)

```python
def set_path_gain_limit(ch: int, gain: float):
    base = 0x0130
    write_f32(base + (4 * ch), gain)

def set_master_volume(gain_linear: float):
    write_f32(0x0110, gain_linear)

def set_iso226(enabled: bool, depth: float, ref_db: float):
    write_u32(0x011C, 1 if enabled else 0)
    write_f32(0x0120, depth)
    write_f32(0x0124, ref_db)

def set_crossover(x0_hz: int, x1_hz: int, x2_hz: int, protect_hp_hz: int):
    write_u32(0x00EC, ((x1_hz & 0xFFFF) << 16) | (x0_hz & 0xFFFF))
    write_u32(0x00F0, ((protect_hp_hz & 0xFFFF) << 16) | (x2_hz & 0xFFFF))

def write_eq_band(group_base: int, band: int, gain: float):
    write_f32(group_base + 0x10 + 4 * band, gain)

def apply_eq_group(group_base: int):
    write_u32(group_base + 0x04, 0x45515131)

def read_clip_status_page(page: int) -> int:
    write_u32(0x00C8, page)
    return read_u32(0x00D8)  # selected page latch
```

## Step-By-Step Calibration Flow

### Bypass Isolation Examples (Block-By-Block Validation)

Use these examples to isolate specific functional blocks while holding other processing constant.

1. Total continuity path test (input to output 1:1 style check)
     - Goal: confirm transport, routing, and output path continuity.
     - Setup:
         - Minimize all optional processing influence (restoration pass gates off, conservative gains, no pink-noise source replacement).
         - Enable required routing/crossover path controls for continuous signal flow.
     - Verify:
         - Expected signal present on all intended output lanes.
         - No unexpected lane cross-coupling.

2. Stage-1 bypass fan-out validation
     - Goal: verify Stage-1 bypass fans the same signal to both Stage-2 input path and Stage-3 input path.
     - Setup:
         - Stage-1 bypass active.
         - Stage-2 and Stage-3 enabled with different setpoints.
     - Verify:
         - Stage-2 HP branch and Stage-3 HP branch both respond from the same bypassed upstream signal.

3. Stage-2 bypass continuity validation
     - Goal: verify Stage-2 bypass behavior for downstream output mapping.
     - Setup:
         - Stage-2 bypass active, Stage-1 active.
     - Verify:
         - Stage-2 HP output carries bypassed Stage-1 HP signal.
         - Stage-2 LP output follows bypass policy (muted per current bypass documentation intent).

4. Stage-3 mono bypass validation
     - Goal: verify Stage-3 bypass behavior on mono low-frequency branch.
     - Setup:
         - Stage-3 bypass active with valid Stage-1 LP mono feed.
     - Verify:
         - Stage-3 HP output carries bypassed mono input.
         - Stage-3 LP output follows bypass policy (muted per current bypass documentation intent).

5. Service bypass mode validation
     - Goal: validate service bypass control-path gating.
     - Setup:
         - Assert service bypass request over register control.
         - Confirm physical selector validity/stability requirements are met.
     - Verify:
         - Service bypass state transitions only when both register and physical conditions permit.

6. Pink-noise source-mask transition validation
     - Goal: confirm source replacement and transition-protection behavior.
     - Setup:
         - Toggle `MTR_PINK_NOISE_SRC_MASK` channel selections during controlled playback.
     - Verify:
         - Expected channels are replaced.
         - Transition-protection envelope prevents pop transients during switching.

7. EQ flatten-only reference-input calibration
     - Goal: flatten a known reference input with EQ only.
     - Setup:
         - Keep restoration and crossover shaping influences minimized.
         - Use a known reference stimulus and tune EQ bands only.
     - Verify:
         - Measured response tracks flattening target without clip-margin violations.

8. Restoration-only calibration
     - Goal: tune bass/infrasonic restoration behavior in isolation.
     - Setup:
         - Enable restoration passes.
         - Keep EQ and crossover shaping influences minimized to avoid confounding.
     - Verify:
         - Desired low-frequency enhancement behavior without instability or clipping.

9. Crossover-only calibration
     - Goal: tune crossover points without restoration/EQ confounds.
     - Setup:
         - Keep restoration influence minimal.
         - Set and sweep crossover setpoints only.
     - Verify:
         - Expected split behavior at the configured transition frequencies.

10. Manufacturing bypass profile check (all bypasses active)
        - Goal: fast production sanity check of raw path continuity and control-state transitions.
        - Setup:
            - Activate documented bypass states for a minimal-processing profile.
        - Verify:
            - Expected lane continuity and stable output behavior in the bypass profile.

### Independent Block Calibration Strategy

By combining bypass selection with pink-noise channel insertion, you can bench-test downstream blocks independently of upstream content.

- Select pink-noise source channels with `MTR_PINK_NOISE_SRC_MASK` (`0x0100`) to replace any combination of CH0..CH5.
- Set pink-noise excitation level with `MTR_PINK_NOISE_GAIN_TRIM` (`0x0104`).
- Toggle bypass/routing bits in `MTR_PRIMARY_BYPASS_MASK`/`MTR_SFX_BYPASS_MASK`/`MTR_SYS_ENABLE_1` to isolate the next block under test.
- Measure only at the target stage outputs while upstream variation is held constant by source replacement.

Transition protection policy for route/source changes:

- Route/source changes use the canonical transition-protection envelope and are committed at mute floor.
- See [Timing Policy](Timing_Policy.md) for authoritative timing values and host-side timing expectations.

### Room Compensation Calibration

Use this procedure to derive and validate the five room-comp coefficients documented in [Room Compensation](Room_Compensation.md) and exposed in [SPI Register Manual](Register_Manual.md).

1. Complete preconditions first:
    - final polarity, crossover, and limiter setup
    - representative playback level
    - calibrated measurement microphone at the primary listening position

2. Capture baseline response with room-comp neutral coefficients:
    - `b0 = 1.0`, `b1 = 0.0`, `b2 = 0.0`, `a1 = 0.0`, `a2 = 0.0`
    - measure multiple nearby positions in the listening area

3. Define correction objective:
    - prioritize narrow/high modal peak cuts before dip boosts
    - keep boosts conservative to protect headroom
    - prefer minimal, high-impact corrections over dense filter shaping

4. Derive candidate coefficients:
    - fit second-order IIR terms from frequency/Q/gain targets
    - normalize with `a0 = 1`
    - reject non-finite or unstable solutions

5. Push runtime coefficient set over SPI as one update window:
    - `0x0150` (`B0`)
    - `0x0154` (`B1`)
    - `0x0158` (`B2`)
    - `0x015C` (`A1`)
    - `0x0160` (`A2`)
    - read back each term to confirm payload acceptance

6. Validate compensated behavior:
    - re-measure same mic positions and compare against baseline/target
    - check clips, headroom, and thermal margin under sustained low-frequency content
    - verify no ringing, pumping, or instability artifacts

7. Iterate with control discipline:
    - change one correction objective at a time
    - keep a revision log of coefficient sets and outcomes
    - stop when improvements become negligible across the listening area

Safety note:

- Keep host-side guardrails aligned with parser clamps (`-8.0..8.0` per coefficient term).

### Phase 1: Gain Structure (Protect Hardware)

1. Set conservative path max gain ceilings (`0x0130..0x0144`) and low master volume.
2. Configure crossover setpoints (`0x00EC`, `0x00F0`) and verify stage bypass masks (`0x00A6`, `0x00A7`) are set as intended.
3. Tune notification/SFX gain (`0x00A4`) so summing does not dominate program content.

### Phase 2: EQ Tuning (Minimize SNR Degradation)

1. **Apply baseline EQ per group with SNR-aware gain limits** (see [EQ Settings](EQ_Settings.md) for details):
   - Limit individual band gains to **±6 dB** (0.5x to 2.0x linear).
   - Keep total composite EQ gain **within ±3 dB of unity** (do not use EQ for overall level changes).
   - Order filters to place high-gain stages late in the cascade (after high-pass, before room correction).
   - Validate each group via `read_u32(group_base + 0x08)` to inspect `EQ_COMPOSITE_GAIN`.

2. Issue `EQ_APPLY` per group after all band writes complete.

**SNR Impact**: Conservative EQ (<±3 dB) maintains ~96 dB effective SNR. Moderate EQ (±6 dB) preserves ~92 dB effective SNR. Aggressive EQ (>±12 dB) risks audible noise floor and should trigger re-evaluation.

### Phase 3: Response Shaping & Monitoring

1. Adjust ISO226 depth/reference while comparing low-level and high-level listening.

2. Monitor page-based clip latches and output peaks; lower gains where headroom is insufficient.

### Output Dithering (Production Deployment)

**For transparent, low-distortion playback to automotive DACs**, the firmware implements **TPDF (Triangular Probability Distribution Function) dithering** at the float→24-bit PCM output stage. This is enabled by default in the Core0 output buffer encoding.

- **Dither magnitude**: ±0.5 LSB (∼±5.96e-8 in normalized float domain)
- **Effect**: Replaces quantization distortion with noise floor. Effective output SNR with dither: ~119–124 dB (trades ~6 dB noise floor for elimination of distortion artifacts on low-level signals).
- **Perceptual benefit**: Low-level signals remain transparent without harmonic distortion bumps at quantization edges.
- **Configuration**: No host-level control required; dithering is applied unconditionally at output conversion.

## Example Calibration Pass

```python
# Baseline ceilings
for ch in range(6):
    set_path_gain_limit(ch, 0.85)
set_master_volume(0.35)

# Crossover baseline
set_crossover(x0_hz=175, x1_hz=3000, x2_hz=25, protect_hp_hz=10)

# Notification blend trim
write_f32(0x00A4, 0.70)

# EQ example on PRIMARY_CH0 group base 0x0200
# Respecting ±6 dB per-band limit for SNR optimization
write_eq_band(0x0200, 0, 1.26)    # +2.0 dB low bass lift (within ±6 dB)
write_eq_band(0x0200, 23, 0.89)   # -1.0 dB upper-mid trim (within ±6 dB)
apply_eq_group(0x0200)

# Verify composite gain is within reasonable range (±3 dB recommended)
composite = read_u32(0x0200 + 0x08)  # Should be close to unity after EQ apply
print(f"Composite gain (float bits): {composite}")

# Loudness contour tuning
set_iso226(True, depth=0.60, ref_db=-14.0)

# Clip sanity check on a few pages
for p in [0, 4, 8, 16, 24, 32]:
    if read_clip_status_page(p) != 0:
        print(f"clip on page {p}; reduce gain/volume")
```

## Why These Choices

- Path ceilings first: protects downstream hardware during all later tuning.
- Crossover before EQ: EQ should tune each active band after routing is stable.
- **EQ gain limits (±6 dB per band, ±3 dB total composite)**: Minimizes cascaded biquad round-off noise; preserves ~92 dB effective SNR.
- Notification trim before final volume: avoids hidden summing overload.
- ISO226 last: it modifies perceived tonal balance and should be tuned after core EQ.
- **Dithering enabled at output**: Eliminates quantization distortion on low-level playback; increases effective output SNR to 119–124 dB (at cost of +6 dB noise floor compared to perfect quantization).
