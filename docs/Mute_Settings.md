# Mute Settings

Back to the [root README](../README.md).

See also [Volume Settings](Volume_Settings.md), [Timing Policy](Timing_Policy.md), and the [SPI Register Manual](Register_Manual.md).

## Purpose

Mute control is intentionally separate from both master volume and gain ceilings.

- Software mute is the final digital stop.
- DAC mute request is the hardware soft-mute request at the converter boundary.

## Registers

| Address | Name | Access | Description |
| --- | --- | --- | --- |
| `0x0114` | `MTR_VOL_MUTE_CTRL` | RW | Bit 0 software mute, Bit 1 DAC hardware mute request. |
| `0x0118` | `MTR_VOL_STATE` | R | Runtime mirror for software mute, DAC mute request, and ISO enabled state. |

## Bitfields

### `MTR_VOL_MUTE_CTRL` (`0x0114`)

- Bit `0`: software mute request
- Bit `1`: PCM1795 DAC soft-mute request over I2C

### `MTR_VOL_STATE` (`0x0118`)

- Bit `0`: software mute requested
- Bit `1`: DAC mute requested
- Bit `8`: ISO226 compensation enabled

## Runtime Behavior

- Bit 0 drives final digital mute in the Core 0 output stage.
- Bit 1 triggers `setPCM1795SoftMute(...)` in the Core 1 hardware service path.
- These values are runtime controls and are not intended for NVS persistence.

## Recommended Host Policy

1. Use bit 0 for fast user mute transitions.
2. Use bit 1 when mute is needed close to analog output conversion.
3. Read `MTR_VOL_STATE` for host synchronization and UI state confirmation.

For canonical timing behavior (user-mute ramp versus software-mute gate behavior), see [Timing Policy](Timing_Policy.md).
