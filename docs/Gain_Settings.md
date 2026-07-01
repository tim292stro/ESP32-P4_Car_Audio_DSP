# Gain Settings

Back to the [root README](../README.md).

See also [Volume Settings](Volume_Settings.md) and the [SPI Register Manual](Register_Manual.md).

## Purpose

Gain controls define amplitude constraints and trims. They are separate from user loudness volume.

## Registers

| Address | Name | Access | Description |
| --- | --- | --- | --- |
| `0x00A2` | `MTR_GAIN_PRIMARY` | RW | Primary program-path pre-summing gain trim (float32, clamped `0.0..8.0`). |
| `0x00A4` | `MTR_GAIN_NOTIF` | RW | Notification/SFX pre-summing gain trim (float32). |
| `0x0130` | `MTR_VOL_PATH_MAX_CH0` | RW | Output-path max gain for channel 0 (`0.0..2.0`). |
| `0x0134` | `MTR_VOL_PATH_MAX_CH1` | RW | Output-path max gain for channel 1 (`0.0..2.0`). |
| `0x0138` | `MTR_VOL_PATH_MAX_CH2` | RW | Output-path max gain for channel 2 (`0.0..2.0`). |
| `0x013C` | `MTR_VOL_PATH_MAX_CH3` | RW | Output-path max gain for channel 3 (`0.0..2.0`). |
| `0x0140` | `MTR_VOL_PATH_MAX_CH4` | RW | Output-path max gain for channel 4 (`0.0..2.0`). |
| `0x0144` | `MTR_VOL_PATH_MAX_CH5` | RW | Output-path max gain for channel 5 (`0.0..2.0`). |

## Runtime Behavior

- `MTR_GAIN_PRIMARY` scales the primary program input lanes before pre-crossover ducking/summing.
- `MTR_GAIN_NOTIF` scales the notification/SFX input before summing.
- Path max gain registers clamp final output lane gain ceilings.
- Master volume is applied inside these ceilings and does not replace them.

## Recommended Host Policy

1. Set path max gains during install and system tuning.
2. Keep notification gain as a separate trim from user volume.
3. Use volume controls for live loudness, not for amplifier/channel protection limits.
