# Crossover Points

Back to the [root README](../README.md).

See also [Routing Bypass And Channel Modes](Bypass_Routing_and_Control.md) and the [SPI Register Manual](Register_Manual.md).

## Purpose

Crossover points are kept in dedicated packed registers so host logic can treat crossover as hard routing boundaries.

## Registers

| Address | Name | Access | Description |
| --- | --- | --- | --- |
| `0x00EC` | `MTR_XOVER_SET_A` | RW | Packed crossover set A: bits `[15:0]` XOVER0 Hz, bits `[31:16]` XOVER1 Hz. |
| `0x00F0` | `MTR_XOVER_SET_B` | RW | Packed crossover set B: bits `[15:0]` XOVER2 Hz, bits `[31:16]` PROTECT_HP Hz. |

## Packing

- `MTR_XOVER_SET_A`
  - Bits `[15:0]`: XOVER0 Hz
  - Bits `[31:16]`: XOVER1 Hz
- `MTR_XOVER_SET_B`
  - Bits `[15:0]`: XOVER2 Hz
  - Bits `[31:16]`: PROTECT_HP Hz

## Runtime Notes

- Crossover setpoints are consumed in Core 0 as 16-bit Hz values carried in 32-bit register words.
- Effective stage activation depends on `MTR_PRIMARY_BYPASS_MASK`: Stage-1 on bits `[19:16]`, Stage-2 on bits `[23:20]`, and shared Stage-3 on bit `24` (bypass enabled when bit = `1`).
- SPI write-path bounds checking is applied before commit:
  - `XOVER0`, `XOVER1`, `XOVER2` clamp to `5..20000 Hz`.
  - `PROTECT_HP` clamps to `5..500 Hz`.
