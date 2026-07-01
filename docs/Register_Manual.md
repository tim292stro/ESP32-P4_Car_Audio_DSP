# SPI Register Manual

Back to the [root README](../README.md).

## Scope

This document is the formal SPI register reference for the ESP32-P4 car audio processor firmware.

- Bus role: ESP32-P4 is SPI slave.
- Packet format: 32-bit address + 32-bit data.
- Undefined address reads return `0xDEADBEEF`.
- Register widths are 32-bit unless noted.

## Companion Section Docs

- [Setup and Commissioning Guide](Setup_Commissioning_Guide.md)
- [Calibration Guide](Calibration_Guide.md)
- [Software Integration Guide](Software_Integration_Guide.md)
- [ID and Version](ID_Version.md)
- [Interrupts](Interrupts.md)
- [Mute Settings](Mute_Settings.md)
- [Volume Settings](Volume_Settings.md)
- [Routing, Bypass, and Channel Modes](Bypass_Routing_and_Control.md)
- [Gain Settings](Gain_Settings.md)
- [Crossover Points](Crossover_Points.md)
- [Bass and Infrasonic Restoration](Bass_Infrasonic_Restoration.md)
- [Equalizer Settings](EQ_Settings.md)
- [Metering](Metering.md)

## Conventions

- Access types:
  - `R`: Read-only
  - `W`: Write-only
  - `RW`: Read/write
  - `COR`: Clear on read
  - `ND`: Non-destructive read

SPI input hardening behavior:

- Transactions shorter than one full packet (`64` bits) are rejected.
- Float register writes are decoded defensively; non-finite payloads are ignored via safe defaults and clamped to register bounds.
- Boot-delay conversion (`ms -> samples`) uses saturating multiply to avoid overflow.

## Register Map

| Address | Name | Access | Reset | Description |
| --- | --- | --- | --- | --- |
| `0x0000` | `MTR_ABI_VERSION` | R | build-time | Register ABI version signature. |
| `0x0004` | `MTR_ABI_CAPS` | R | build-time | Register capability bitmask for host feature detection. |
| `0x0080` | `MTR_IRQ_STATUS` | R,COR(bit0) | `0` | Consolidated interrupt pending summary. |
| `0x0084` | `MTR_IRQ_ENABLE` | RW | `0x00000001` | Interrupt enable mask. |
| `0x0088` | `MTR_IRQ_CTRL` | RW | `0` | Interrupt clear controls (`W1C` semantics for selected sources). |
| `0x0114` | `MTR_VOL_MUTE_CTRL` | RW | `0x00000000` | Bit0 software mute, Bit1 DAC hardware mute request. |
| `0x0118` | `MTR_VOL_STATE` | R | runtime | Volume/mute/ISO runtime state mirror. |
| `0x0110` | `MTR_VOL_MASTER` | RW | `0x3F800000` | float32 master volume scalar (`0.0..1.0`). |
| `0x011C` | `MTR_ISO226_CTRL` | RW | `0x00000001` | Bit0 enables ISO226 compensation overlay. |
| `0x0120` | `MTR_ISO226_DEPTH` | RW | `0x3F800000` | float32 compensation depth (`0.0..1.0`). |
| `0x0124` | `MTR_ISO226_REF_DB` | RW | `-10.0f` | float32 reference loudness dB (`-40..0`). |
| `0x00A1` | `MTR_SYS_ENABLE_1` | RW | `0x00000000` | Routing, channel-mode, and mute-target control mask group 1. |
| `0x00E0` | `MTR_PROTECT_SEL_CTRL` | RW | `0` | Service/protection bypass request and force-protected control. |
| `0x00E4` | `MTR_PROTECT_SEL_STAT` | R | runtime | Service/protection bypass status bits. |
| `0x00E8` | `MTR_PROTECT_SEL_TMR` | R | runtime | Remaining service bypass timer seconds. |
| `0x00A2` | `MTR_GAIN_PRIMARY` | RW | `0x3F800000` | Primary program-path pre-summing gain trim (float32, clamped `0.0..8.0`). |
| `0x00A4` | `MTR_GAIN_NOTIF` | RW | `0x3F800000` | Notification/SFX pre-summing gain trim (float32). |
| `0x00A6` | `MTR_PRIMARY_BYPASS_MASK` | RW | `0x00000000` | Per-primary-lane bypass mask (EQ/restoration/room-comp/xover stage-1/xover stage-2) plus shared stage-3 bypass. |
| `0x00A7` | `MTR_SFX_BYPASS_MASK` | RW | `0x00000000` | Per-SFX-lane bypass mask (SFX EQ lanes). |
| `0x0100` | `MTR_PINK_NOISE_SRC_MASK` | RW | `0x00000000` | Bits `[5:0]` replace input channels CH0..CH5 with pink-noise source (`1`=replace). |
| `0x0104` | `MTR_PINK_NOISE_GAIN_TRIM` | RW | `0x3F800000` | float32 pink-noise gain/trim scalar (clamped `0.0..2.0`). |
| `0x0150` | `MTR_ROOM_COMP_B0` | RW | `0x3F800000` | float32 room-comp biquad coefficient `b0` (clamped `-8.0..8.0`). |
| `0x0154` | `MTR_ROOM_COMP_B1` | RW | `0x00000000` | float32 room-comp biquad coefficient `b1` (clamped `-8.0..8.0`). |
| `0x0158` | `MTR_ROOM_COMP_B2` | RW | `0x00000000` | float32 room-comp biquad coefficient `b2` (clamped `-8.0..8.0`). |
| `0x015C` | `MTR_ROOM_COMP_A1` | RW | `0x00000000` | float32 room-comp biquad coefficient `a1` (clamped `-8.0..8.0`). |
| `0x0160` | `MTR_ROOM_COMP_A2` | RW | `0x00000000` | float32 room-comp biquad coefficient `a2` (clamped `-8.0..8.0`). |
| `0x0130` | `MTR_VOL_PATH_MAX_CH0` | RW | `0x3F800000` | float32 output-path max gain channel 0 (`0.0..2.0`). |
| `0x0134` | `MTR_VOL_PATH_MAX_CH1` | RW | `0x3F800000` | float32 output-path max gain channel 1 (`0.0..2.0`). |
| `0x0138` | `MTR_VOL_PATH_MAX_CH2` | RW | `0x3F800000` | float32 output-path max gain channel 2 (`0.0..2.0`). |
| `0x013C` | `MTR_VOL_PATH_MAX_CH3` | RW | `0x3F800000` | float32 output-path max gain channel 3 (`0.0..2.0`). |
| `0x0140` | `MTR_VOL_PATH_MAX_CH4` | RW | `0x3F800000` | float32 output-path max gain channel 4 (`0.0..2.0`). |
| `0x0144` | `MTR_VOL_PATH_MAX_CH5` | RW | `0x3F800000` | float32 output-path max gain channel 5 (`0.0..2.0`). |
| `0x0148` | `MTR_OUT_DITHER_CTRL` | RW | `0x00000001` | Bit0 enables TPDF dithering on output conversion; Bit1-31 reserved. |
| `0x00A8` | `MTR_SYS_STATUS` | R | runtime | System status bitfield from core0 runtime. |
| `0x00AC` | `MTR_BOOT_DELAY_MS` | RW | preset | Boot hold parameter in ms units; firmware multiplies by 192 samples. |
| `0x00B0` | `MTR_PEAK_OUT_0` | R | `0` | Channel 0 output peak (float bits). |
| `0x00B4` | `MTR_PEAK_OUT_1` | R | `0` | Channel 1 output peak (float bits). |
| `0x00B8` | `MTR_PEAK_OUT_2` | R | `0` | Channel 2 output peak (float bits). |
| `0x00BC` | `MTR_PEAK_OUT_3` | R | `0` | Channel 3 output peak (float bits). |
| `0x00C0` | `MTR_CLIP_WARN_FLAGS_LO` | R,COR | `0` | Sticky clip bits `[31:0]`; low half is cleared on read. |
| `0x00C4` | `MTR_CLIP_WARN_FLAGS_HI` | R,COR | `0` | Sticky clip bits `[63:32]`; high half is cleared on read. |
| `0x00C8` | `MTR_CLIP_METER_PAGE` | RW | `0` | Meter page selector (`0..37`). |
| `0x00CC` | `MTR_CLIP_METER_VALUE` | R,COR | `0` | Selected page dBFS code from snapshot peak; selected peak clears on read. |
| `0x00D0` | `MTR_FLASH_CMD` | W | `0` | Write `0x51A151A1` to force immediate NVS commit. |
| `0x00D4` | `MTR_FLASH_TIMER_STAT` | R | `0xFFFFFFFF` clean | NVS timer status (`0xFFFFFFFF` when clean; else seconds remaining). |
| `0x00D8` | `MTR_CLIP_METER_STATUS` | R,COR | `0` | Bit 0 reports selected page clip latch; selected clip bit clears on read. |
| `0x00EC` | `MTR_XOVER_SET_A` | RW | preset | Packed crossover set A: bits `[15:0]` XOVER0 Hz, `[31:16]` XOVER1 Hz. |
| `0x00F0` | `MTR_XOVER_SET_B` | RW | preset | Packed crossover set B: bits `[15:0]` XOVER2 Hz, `[31:16]` PROTECT_HP Hz. |
| `0x00F4` | `MTR_CLIP_METER_MODE` | RW | `0` | Meter mode: `0` unweighted, `1` A, `2` C. |
| `0x00F8` | `MTR_CLIP_METER_RAW_Q23` | R,ND | `0` | Selected page current linear magnitude in unsigned Q23. |
| `0x00FC` | `MTR_CLIP_METER_AGE_SAMPLES` | R,ND | `0` | Samples since selected page last non-zero weighted energy. |

## ID / Version

### `MTR_ABI_VERSION` (`0x0000`)

- Returns register ABI signature currently `0x20260629`.

### `MTR_ABI_CAPS` (`0x0004`)

- Bit `0`: grouped EQ register layout supported.
- Bit `1`: split EQ band plans supported (31 primary / 15 SFX).
- Bit `2`: low-address consolidated interrupt block supported.
- Bit `3`: volume ISO226 compensation overlay supported.
- Bit `4`: DAC mute hook supported.

## Interrupts

The consolidated interrupt output is on `GPIO9` and is active high.

- The line is asserted when `(IRQ_STATUS.pending & IRQ_ENABLE.mask) != 0`.
- `MTR_IRQ_STATUS` exposes the pending bits, enabled mirror, and meter sequence LSB16.
- `MTR_IRQ_CTRL` uses write-one-to-clear behavior for the clearable sources.

### `MTR_IRQ_STATUS` (`0x0080`)

- Bits `[7:0]`: pending interrupt bits.
- Bits `[15:8]`: active interrupt enable bits (mirror of `MTR_IRQ_ENABLE`).
- Bits `[31:16]`: meter presentation sequence LSB16.
- Read side effect: clears meter interval pending source (bit 0) by clearing internal ready-sticky.

### `MTR_IRQ_ENABLE` (`0x0084`)

- Bit `0`: meter-interval-ready interrupt enable.
- Bit `1`: clip-latch interrupt enable.
- Bit `2`: service-timeout interrupt enable.
- Bits `[31:3]`: reserved, write `0`.

### `MTR_IRQ_CTRL` (`0x0088`)

- Bit `0` (`W1C`): clear meter interval ready source.
- Bit `1` (`W1C`): clear all clip latch flags.
- Bit `2` (`W1C`): reserved (service-timeout source is state-driven and not manually clearable).

## Mutes

Mute control is distinct from both gain limits and master volume.

- Software mute (`0x0114` bit0) is the final digital stop.
- DAC hardware mute request (`0x0114` bit1) sends the PCM1795 soft-mute request over I2C.

### `MTR_VOL_MUTE_CTRL` (`0x0114`) bitfield

- Bit `0`: software mute request (final digital stage scalar forced to 0).
- Bit `1`: PCM1795 DAC soft-mute request over I2C.

### `MTR_VOL_STATE` (`0x0118`) bitfield

- Bit `0`: software mute requested.
- Bit `1`: DAC mute requested.
- Bit `8`: ISO226 compensation enabled.

## Volumes

Volume is separate from gain constraints and is applied inside the path ceilings.

- `MTR_VOL_MASTER` is the user-facing loudness scalar.
- `MTR_ISO226_CTRL`, `MTR_ISO226_DEPTH`, and `MTR_ISO226_REF_DB` control the loudness contour overlay.

### `MTR_VOL_MASTER` (`0x0110`)

- float32 master volume scalar (`0.0..1.0`).

### ISO226 Overlay Controls

- `MTR_ISO226_CTRL` (`0x011C`) bit0 enables/disables compensation overlay.
- `MTR_ISO226_DEPTH` (`0x0120`) sets compensation blend depth.
- `MTR_ISO226_REF_DB` (`0x0124`) sets reference loudness dB anchor.

## Routing, Bypass, And Channel Modes

`MTR_SYS_ENABLE_1` and the protection selector form the host-facing routing and mode control surface.

- Power-on defaults are all zeros.
- The fixed 6x10 output routing matrix is implemented in Core 0; these registers only gate the stages and operating modes around it.
- There is no separate channel-mode register. The current implementation uses the enable masks and protection selector to express routing state.

### `MTR_SYS_ENABLE_1` (`0x00A1`) bitfield

- Bit `0`: `PRIMARY_4CH_MODE_ENABLE` (`0` = primary 2-channel mode, `1` = primary 4-channel mode)
- Bit `1`: `SFX_STEREO_MODE_ENABLE` (`0` = SFX mono mode, `1` = SFX stereo mode)
- Bit `2`: `USER_MUTE_OVERRIDE` (`0` muted target, `1` unmuted target).
- Bits `[31:3]`: reserved, write `0`.

### `MTR_PRIMARY_BYPASS_MASK` (`0x00A6`) bitfield

All bits are bypass-enable semantics (`0` bypass disabled, `1` bypass enabled).

- Bits `[3:0]`: Primary EQ bypass per lane (`CH0..CH3`).
- Bits `[7:4]`: Bass restoration bypass per lane (`CH0..CH3`).
- Bits `[11:8]`: Infrasonic restoration bypass per lane (`CH0..CH3`).
- Bits `[15:12]`: Room-compensation bypass per lane (`CH0..CH3`).
- Bits `[19:16]`: Crossover stage-1 bypass per lane (`CH0..CH3`).
- Bits `[23:20]`: Crossover stage-2 bypass per lane (`CH0..CH3`).
- Bit `24`: Shared stage-3 crossover bypass after mono summing.
- Bits `[31:25]`: reserved, write `0`.

### `MTR_SFX_BYPASS_MASK` (`0x00A7`) bitfield

All bits are bypass-enable semantics (`0` bypass disabled, `1` bypass enabled).

- Bit `0`: SFX EQ bypass for lane `CH4`.
- Bit `1`: SFX EQ bypass for lane `CH5`.
- Bits `[31:2]`: reserved, write `0`.

### `MTR_PROTECT_SEL_CTRL` (`0x00E0`) bitfield

- Bit `0`: request service bypass when the hardware selector is in the bypass position.
- Bit `1`: force protected mode and clear the bypass request.

### `MTR_PROTECT_SEL_STAT` (`0x00E4`) bitfield

- Bit `0`: selector pair is valid.
- Bit `1`: selector is still debouncing / not stable.
- Bit `2`: service bypass is active.
- Bit `3`: service timeout expired.

### `MTR_PROTECT_SEL_TMR` (`0x00E8`)

- Remaining service bypass time in seconds.

## Gains

Gain control is split between pre-summing trim and per-path output ceilings.

### `MTR_GAIN_PRIMARY` (`0x00A2`)

- float32 primary program-path pre-summing gain trim.
- Runtime clamp range: `0.0..8.0`.

### `MTR_GAIN_NOTIF` (`0x00A4`)

- float32 notification/SFX pre-summing gain trim.
- Runtime clamp range: `0.0..8.0`.

### Path Max Gain Registers

- `0x0130..0x0144` define the per-output amplitude ceilings applied at the final output stage.

## Crossoverpoints

Crossover points are separate from EQ so the host can treat them as hard routing boundaries.

### `MTR_XOVER_SET_A` (`0x00EC`)

- Packed crossover set A: bits `[15:0]` XOVER0 Hz, `[31:16]` XOVER1 Hz.

### `MTR_XOVER_SET_B` (`0x00F0`)

- Packed crossover set B: bits `[15:0]` XOVER2 Hz, `[31:16]` PROTECT_HP Hz.

## Equalizer Settings

EQ control is organized as 6 independent channel groups. Each group base starts on an address with zero LSB (`0x..00`) and occupies a dedicated `0x100` address window.

- Group 0 (`PRIMARY_CH0`) base: `0x0200` (31 bands)
- Group 1 (`PRIMARY_CH1`) base: `0x0300` (31 bands)
- Group 2 (`PRIMARY_CH2`) base: `0x0400` (31 bands)
- Group 3 (`PRIMARY_CH3`) base: `0x0500` (31 bands)
- Group 4 (`SFX_CH4`) base: `0x0600` (15 bands)
- Group 5 (`SFX_CH5`) base: `0x0700` (15 bands)

For each group base `B`:

- `B + 0x00` `EQ_INFO` (R)
  - Bits `[7:0]`: active band count (`31` or `15`)
  - Bit `8`: SFX group marker (`1` for groups 4/5)
  - Bits `[31:16]`: `eqApplySequence` LSB16
- `B + 0x04` `EQ_APPLY` (RW)
  - Write key `0x45515131` to recompute this channel composite and increment global `eqApplySequence`
  - Read returns current `eqApplySequence`
- `B + 0x08` `EQ_COMPOSITE_GAIN` (R)
  - float32 effective runtime composite gain for this group
- `B + 0x10 + 2*n` `EQ_BAND_GAIN_n` (RW), `n = 0..30`
  - **int16 Q14 fixed-point band gain** (signed 16-bit, Q14 format; range ±2.0 exactly)
  - Firmware converts to float32: `gain = (int16_q14 / 16384.0f)`
  - Range: ±6 dB (0.5x to 2.0x linear); resolution: 0.52 mdB per LSB
  - See [Audio Topology & SNR Justifications](../docs/Audio-Topology_Justifications.md) for Q14 rationale
  - For inactive SFX-only bands (`n >= 15`), reads return unity (0x4000) and writes are ignored

## EQ Runtime Behavior

- Primary lanes use 31-band plans.
- SFX lanes use 15-band plans.
- Band writes are sanitized to finite safe gain values.
- Per-channel composite gain is recomputed on band write and can also be explicitly recomputed using `EQ_APPLY`.
- Core0 applies `eqCompositeGain[0..5]` directly to six incoming DSP lanes.

## Metering

Metering is split between live peaks, sticky clip flags, and the page-based clip meter side-channel.

- `MTR_SYS_STATUS` exposes coarse runtime state, including mute, boot, and clip status.
- `MTR_PEAK_OUT_0..3` expose live output peak values for the four monitored output channels.
- `MTR_CLIP_WARN_FLAGS_*` expose sticky clip history.
- `MTR_CLIP_METER_*` exposes the page-selected meter view and its derived values.

### `MTR_SYS_STATUS` (`0x00A8`)

- Bit `0`: global audio mute fully applied.
- Bit `1`: boot ramp active.
- Bit `2`: valid incoming ADC clock/data stream confirmed.
- Bit `3`: active inter-function clip condition present.

### `MTR_PEAK_OUT_0..3` (`0x00B0..0x00BC`)

- Live channel peak values read back as float bits.

### `MTR_CLIP_WARN_FLAGS_LO` / `MTR_CLIP_WARN_FLAGS_HI` (`0x00C0` / `0x00C4`)

- Sticky clip bits `[31:0]` and `[63:32]`.
- The half that is read is cleared in place.

### `MTR_CLIP_METER_PAGE` (`0x00C8`)

- Selects the page shown by the clip meter readout (`0..37`).

### `MTR_CLIP_METER_VALUE` (`0x00CC`)

- Returns the selected page dBFS code and clears that page's latched peak.

### `MTR_CLIP_METER_STATUS` (`0x00D8`)

- Bit `0` reports whether the selected page is currently latched.
- The selected page's latch clears on read.

### `MTR_CLIP_METER_MODE` (`0x00F4`)

- Meter mode: `0` unweighted, `1` A-weighted, `2` C-weighted.

### `MTR_CLIP_METER_RAW_Q23` (`0x00F8`)

- Selected page current linear magnitude in unsigned Q23.

### `MTR_CLIP_METER_AGE_SAMPLES` (`0x00FC`)

- Samples since the selected page last recorded non-zero weighted energy.

- Meter side-channel runs at 48 kHz internal cadence.
- Presentation snapshots are generated at 120 Hz (`1600` samples at 192 kHz).
- `MTR_CLIP_METER_VALUE`, `MTR_CLIP_METER_RAW_Q23`, and `MTR_CLIP_METER_AGE_SAMPLES` remain page-based at `0x00C8..0x00FC`.

## Support Registers

### `MTR_BOOT_DELAY_MS` (`0x00AC`)

- Boot hold parameter in ms units; firmware multiplies by 192 samples.

### `MTR_FLASH_CMD` (`0x00D0`)

- Write `0x51A151A1` to force immediate NVS commit.

### `MTR_FLASH_TIMER_STAT` (`0x00D4`)

- NVS timer status (`0xFFFFFFFF` when clean; else seconds remaining).
