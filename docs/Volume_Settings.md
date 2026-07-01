# Volume Settings

Back to the [root README](../README.md).

## Design Intent

Volume and gain are treated as separate concepts.

- Gain: sets maximum signal amplitude constraints for each signal path.
- Volume: user loudness control that operates inside those constraints.
- Mute: explicit control with software and hardware options at the last practical stage.

These controls are intentionally not stored in NVS. They are expected to be driven by the external controller at session time.

## Signal Chain Order

The runtime applies controls in this order near the final output stage:

1. DSP path generation and crossover routing.
2. DC-block and protection filtering.
3. Path max-gain constraints (`outputPathMaxGain[ch]`).
4. Master volume scalar (`masterVolumeLinear`).
5. Software mute (`volumeMuteCtrl` bit 0).
6. Final clamp and sample output to DAC path.

Hardware mute path:

- `volumeMuteCtrl` bit 1 requests PCM1795 soft-mute over I2C.
- This is intended as the closest practical control point to the analog conversion stage.

## Volume And Mute Registers

| Address | Name | Access | Description |
| --- | --- | --- | --- |
| `0x0110` | `MTR_VOL_MASTER` | RW | float32 master volume linear scalar (`0.0..1.0` clamped). |
| `0x0114` | `MTR_VOL_MUTE_CTRL` | RW | Bit 0 software mute request, Bit 1 hardware DAC mute request. |
| `0x0118` | `MTR_VOL_STATE` | R | Runtime state mirror (software mute, DAC mute request, ISO enabled). |
| `0x011C` | `MTR_ISO226_CTRL` | RW | Bit 0 enables ISO226 contour compensation overlay. |
| `0x0120` | `MTR_ISO226_DEPTH` | RW | float32 depth (`0.0..1.0`). |
| `0x0124` | `MTR_ISO226_REF_DB` | RW | float32 reference loudness dB (`-40..0`). |
| `0x0130` | `MTR_VOL_PATH_MAX_CH0` | RW | float32 max gain for output path 0 (`0.0..2.0`). |
| `0x0134` | `MTR_VOL_PATH_MAX_CH1` | RW | float32 max gain for output path 1 (`0.0..2.0`). |
| `0x0138` | `MTR_VOL_PATH_MAX_CH2` | RW | float32 max gain for output path 2 (`0.0..2.0`). |
| `0x013C` | `MTR_VOL_PATH_MAX_CH3` | RW | float32 max gain for output path 3 (`0.0..2.0`). |
| `0x0140` | `MTR_VOL_PATH_MAX_CH4` | RW | float32 max gain for output path 4 (`0.0..2.0`). |
| `0x0144` | `MTR_VOL_PATH_MAX_CH5` | RW | float32 max gain for output path 5 (`0.0..2.0`). |

## ISO226 Contour Logic (Implemented Approximation)

Current implementation uses an ISO226-inspired approximation to minimize required EQ disturbance as volume changes:

- A volume-dependent attenuation factor is derived from current master volume vs reference dB.
- Low-frequency and high-frequency boosts are shaped as smooth log-domain curves.
- Compensation is blended by `MTR_ISO226_DEPTH`.
- Compensation is applied as a non-destructive overlay during EQ composite recompute.

### Why ISO226 Matters

Human hearing is not flat across frequency. At lower playback levels, the ear is less sensitive to bass and extreme treble, so a “correct” sounding loudness curve usually needs more low-end and top-end energy when volume drops. ISO226 formalizes those equal-loudness contours.

The practical goal here is not to rewrite user EQ. Instead, the volume controller should shift the effective tonal balance as loudness changes, while preserving the user’s authored EQ intent as much as possible.

### Conceptual Response Shape

The graph below shows the expected *direction* of compensation, not standards-grade absolute SPL values.

```mermaid
xychart-beta
    title "ISO226-style contour compensation vs frequency"
    x-axis "Frequency (Hz)" [20, 40, 63, 100, 200, 500, 1000, 2000, 4000, 8000, 16000, 20000]
    y-axis "Relative compensation (dB)" -6 --> 12
    series "Low volume" [10, 9, 8, 6, 4, 2, 0, 0, 1, 3, 5, 6]
    series "Mid volume" [6, 5, 4, 3, 2, 1, 0, 0, 0.5, 1.5, 2.5, 3]
    series "High volume" [1, 1, 0.8, 0.5, 0.2, 0, 0, 0, 0, 0.2, 0.5, 0.5]
```

Interpretation:

- At low volume, bass and treble get the strongest compensation.
- Midrange is intentionally kept closer to flat.
- At high volume, compensation trends toward zero.

Important behavior:

- User EQ band gains are preserved as authored values.
- ISO226 overlay modifies effective runtime EQ response without rewriting stored band gains.
- Disabling `MTR_ISO226_CTRL` returns behavior to pure user EQ gains.

## Recommended Host Policy

1. Set path max gains (`0x0130..0x0144`) during installation/tuning.
2. Use `0x0110` for live volume control.
3. Use `0x0114` bit 0 for fast software mute, bit 1 when DAC-side mute is desired.
4. Keep `0x011C` enabled and tune `0x0120` and `0x0124` to vehicle preference.
5. Monitor `MTR_VOL_STATE` and `MTR_IRQ_STATUS` for synchronization and event handling.
6. Do not expect these values to persist across power cycles unless the external controller restores them.
