# Metering

Back to the [root README](../README.md).

See also the [SPI Register Manual](Register_Manual.md).

## Purpose

Metering is split into live output peaks, sticky clip history, and a page-based meter side-channel.

## Registers

| Address | Name | Access | Description |
| --- | --- | --- | --- |
| `0x00A8` | `MTR_SYS_STATUS` | R | Coarse runtime state (mute, boot, ADC valid, clip present). |
| `0x00B0` | `MTR_PEAK_OUT_0` | R | Output channel 0 peak as float bits. |
| `0x00B4` | `MTR_PEAK_OUT_1` | R | Output channel 1 peak as float bits. |
| `0x00B8` | `MTR_PEAK_OUT_2` | R | Output channel 2 peak as float bits. |
| `0x00BC` | `MTR_PEAK_OUT_3` | R | Output channel 3 peak as float bits. |
| `0x00C0` | `MTR_CLIP_WARN_FLAGS_LO` | R,COR | Sticky clip bits `[31:0]`; read clears low half. |
| `0x00C4` | `MTR_CLIP_WARN_FLAGS_HI` | R,COR | Sticky clip bits `[63:32]`; read clears high half. |
| `0x00C8` | `MTR_CLIP_METER_PAGE` | RW | Page selector for page-based meter view (`0..37`). |
| `0x00CC` | `MTR_CLIP_METER_VALUE` | R,COR | Selected page dBFS code; clears selected page latched peak. |
| `0x00D8` | `MTR_CLIP_METER_STATUS` | R,COR | Selected page clip-latch state; clears selected page latch on read. |
| `0x00F4` | `MTR_CLIP_METER_MODE` | RW | Meter weighting mode (`0` unweighted, `1` A, `2` C). |
| `0x00F8` | `MTR_CLIP_METER_RAW_Q23` | R,ND | Selected page current linear magnitude in unsigned Q23. |
| `0x00FC` | `MTR_CLIP_METER_AGE_SAMPLES` | R,ND | Samples since selected page last non-zero weighted energy. |

## System Status Bits

`MTR_SYS_STATUS` bit definitions:

- Bit `0`: global audio mute fully applied
- Bit `1`: boot ramp active
- Bit `2`: valid incoming ADC stream confirmed
- Bit `3`: active inter-stage clip condition present

## Runtime Cadence

- Meter side-channel runtime rate: 48 kHz
- Presentation snapshot cadence: 120 Hz (`1600` samples at 192 kHz)

## Host Pattern

1. Select a page with `MTR_CLIP_METER_PAGE`.
2. Read `MTR_CLIP_METER_VALUE` for dBFS-coded peak.
3. Optionally read `MTR_CLIP_METER_RAW_Q23` and `MTR_CLIP_METER_AGE_SAMPLES` for additional detail.
4. Use `MTR_CLIP_METER_STATUS` and `MTR_CLIP_WARN_FLAGS_*` for sticky clip history handling.
