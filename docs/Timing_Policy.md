# Timing Policy

Back to the [root README](../README.md).

See also [Register Manual](Register_Manual.md), [Mute Settings](Mute_Settings.md), [Setup and Commissioning Guide](Setup_Commissioning_Guide.md), and [Bypass Routing and Control](Bypass_Routing_and_Control.md).

## Purpose

This document is the canonical timing-policy reference for startup and mute-related behavior.

Use this table as the source of truth for host integration and test expectations.

## Canonical Timing Table

| Policy Area | Trigger | Timing Envelope | Control Surface | Runtime Notes |
| --- | --- | --- | --- | --- |
| Power-on hold | Valid ADC activity detected and power sequence enters `BOOT_HOLD_TIME` | `MTR_BOOT_DELAY_MS` milliseconds (host-programmable) | `MTR_BOOT_DELAY_MS` (`0x00AC`) | Firmware converts ms to samples using `ms * 192` at `192 kHz` with saturating multiply. |
| Power-on ramp-up | Boot hold completes and power sequence enters `BOOT_RAMP_UP` | `1000 ms` linear ramp (`currentSystemGain` from `0.0` to `1.0`) | Internal runtime policy | Status bit `MTR_SYS_STATUS` bit `1` reports boot-ramp activity. |
| User mute target ramp | Host toggles `USER_MUTE_OVERRIDE` target (`MTR_SYS_ENABLE_1` bit `2`) | `5 ms` linear ramp (`userMuteRampGain` toward target) | `MTR_SYS_ENABLE_1` bit `2` | This is a click-reduction ramp for user mute target transitions. |
| Software mute gate | Host toggles software mute request | Immediate final digital gate | `MTR_VOL_MUTE_CTRL` bit `0` | Not ramped; this is the fastest digital mute path. |
| DAC soft mute request | Host toggles DAC mute request | Applied on control-loop state change | `MTR_VOL_MUTE_CTRL` bit `1` | Mirrors to PCM1795 soft-mute control over I2C. |
| Route/source transition protection | Any route/source change request (`mode/bypass masks` or `pink-noise source mask`) | `200 ms` ramp down -> `200 ms` hold floor -> `300 ms` ramp up | `MTR_SYS_ENABLE_1`, `MTR_PRIMARY_BYPASS_MASK`, `MTR_SFX_BYPASS_MASK`, `MTR_PINK_NOISE_SRC_MASK` | Route/source switch is committed at mute floor during hold phase. |

## Host Integration Guidance

1. When changing route/source controls, budget at least `700 ms` for the full protection envelope.
2. For startup timing, treat `MTR_BOOT_DELAY_MS` and boot-ramp duration as separate phases.
3. Use `MTR_SYS_STATUS` bit `1` to observe boot-ramp activity during bring-up.
4. Do not assume software mute (`0x0114` bit `0`) and user mute target (`0x00A1` bit `7`) have identical timing behavior.

## Validation Checklist

1. Boot hold duration tracks `MTR_BOOT_DELAY_MS` changes.
2. Boot ramp reaches unity in approximately `1 s` after hold completes.
3. User mute target transitions complete in approximately `5 ms`.
4. Route/source switch transitions follow `200/200/300 ms` envelope without pop transients.
