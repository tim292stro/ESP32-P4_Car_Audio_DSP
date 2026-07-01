# Room Compensation

Back to the [root README](../README.md).

See also [Crossover Points](Crossover_Points.md), [Calibration Guide](Calibration_Guide.md), and the [SPI Register Manual](Register_Manual.md).

## Purpose

This document defines how room-compensation processing is represented in the current firmware, how each runtime control is used by the host, and how the stage is applied in the signal path.

## Design Background

Room-compensation behavior in this project is informed by classical room-response correction concepts from legacy system design literature and related historical reference material.

Room compensation is needed because low-frequency playback in a cabin/installation is dominated by standing-wave behavior (room modes) rather than loudspeaker free-field response. The result is location-dependent response error: some frequencies are exaggerated (boomy peaks), others are attenuated (nulls), and nearby listening positions can differ significantly.

Without compensation, these modal effects reduce translation of tuning decisions, force unnecessary master-gain reduction to avoid over-energized bands, and increase the risk of inconsistent bass perception across seats and program material.

This stage specifically addresses:

1. Narrow or moderate-width low-frequency modal peaks that create boom and mask detail.
2. Low-end spectral tilt caused by enclosure/cabin gain interacting with crossover handoff.
3. Seat-to-seat low-frequency variance by targeting persistent response errors seen across nearby measurement positions.
4. Headroom inefficiency caused by uncontrolled low-frequency hotspots.

In practical terms for this firmware, room compensation is implemented as a host-controlled second-order IIR stage in the infrasonic synthesis branch. It is intended for corrective shaping of low-frequency response, not broad tonal voicing of the full-band program.

## Current Implementation

Runtime coefficients are stored in `InfrasonicCoefficients`:

- `compB0`, `compB1`, `compB2`
- `compA1`, `compA2`

At Core0 pipeline runtime, these coefficients are copied into room-comp biquad instances and applied in the infrasonic branch processing path.

Current behavior summary:

- Room-comp coefficients are present and active in DSP runtime.
- Coefficients are host-tunable via dedicated SPI register set.
- Application occurs in the Core0 real-time path as part of infrasonic synthesis shaping.

## Control Registers

| Address | Name | Access | Description |
| --- | --- | --- | --- |
| `0x0150` | `MTR_ROOM_COMP_B0` | RW | float32 room-comp coefficient `b0` (`-8.0..8.0` clamp). |
| `0x0154` | `MTR_ROOM_COMP_B1` | RW | float32 room-comp coefficient `b1` (`-8.0..8.0` clamp). |
| `0x0158` | `MTR_ROOM_COMP_B2` | RW | float32 room-comp coefficient `b2` (`-8.0..8.0` clamp). |
| `0x015C` | `MTR_ROOM_COMP_A1` | RW | float32 room-comp coefficient `a1` (`-8.0..8.0` clamp). |
| `0x0160` | `MTR_ROOM_COMP_A2` | RW | float32 room-comp coefficient `a2` (`-8.0..8.0` clamp). |

## Room Compensation Controls: Use And Function

The room-comp stage is a single second-order IIR filter section driven by five host-managed coefficients.

Transfer-function form used by the runtime stage:

$$
H(z)=\frac{b_0+b_1 z^{-1}+b_2 z^{-2}}{1+a_1 z^{-1}+a_2 z^{-2}}
$$

Control behavior:

1. `MTR_ROOM_COMP_B0` (`0x0150`): numerator feed-forward term `b0`; sets base pass gain contribution.
2. `MTR_ROOM_COMP_B1` (`0x0154`): numerator first-delay term `b1`; shapes center/shoulder behavior with `b0/b2`.
3. `MTR_ROOM_COMP_B2` (`0x0158`): numerator second-delay term `b2`; completes zero placement for boost/cut contour.
4. `MTR_ROOM_COMP_A1` (`0x015C`): denominator first-delay term `a1`; controls pole placement and resonance behavior.
5. `MTR_ROOM_COMP_A2` (`0x0160`): denominator second-delay term `a2`; controls damping/stability with `a1`.

Host contract and runtime semantics:

1. All five registers are writable at runtime over SPI and are echoed back on read.
2. Values are float32 and parser-clamped to `-8.0..8.0` per term.
3. Writes update live DSP control state used by Core0 processing.
4. There is currently no dedicated room-comp enable bit; active behavior depends on the broader infrasonic path being active.
5. For consistent updates, host should write all five terms as one coefficient-set transaction window.

Operational intent:

1. Use coefficients to attenuate low-frequency modal peaks and shape low-end uniformity.
2. Prefer corrective cuts over aggressive boosts to preserve headroom.
3. Validate final coefficient sets under real program material and peak monitoring.

## Signal Placement

Room compensation is applied within the infrasonic synthesis path before final blending into the program path.

High-level order in this branch:

1. Infrasonic/subharmonic synthesis generation
2. Room-comp biquad processing
3. Branch output blend back into main program path

## Calibration Intent

Room compensation is intended to reduce cabin/installation low-frequency modal issues and improve perceived low-end uniformity.

Practical calibration flow (current implementation):

1. Establish crossover and gain structure first.
2. Validate infrasonic branch behavior with service-level test material.
3. Tune room-comp coefficients offline and deploy as config values.
4. Re-verify clip/headroom margins after compensation is enabled.

Detailed coefficient-calibration procedure is intentionally maintained in [Calibration Guide](Calibration_Guide.md) to keep this document focused on control definition and runtime behavior.

## Control-Surface Status

Current status:

- Dedicated SPI register block for runtime room-comp coefficient control is implemented.
- No explicit room-comp enable bit distinct from broader infrasonic path control.

Planned completion direction:

- Add optional explicit room-comp enable bit and/or profile selector if required by host UX.
- Add clear host policy for safe update timing and coefficient commit behavior.
- Document bounds/validation constraints for coefficient writes.

## Notes For Completion

To close the remaining punch-list item, decide one of these as canonical:

1. Keep current runtime-coefficient control ABI as authoritative and document host-safe tuning workflow.
2. Add an explicit room-comp on/off mode bit if runtime bypass of this stage is required.

Either direction is valid, but the project should document a single authoritative control-surface contract.
