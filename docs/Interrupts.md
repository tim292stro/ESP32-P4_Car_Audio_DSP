# Interrupts

Back to the [root README](../README.md).

See also the [SPI Register Manual](Register_Manual.md).

## Purpose

This section describes the consolidated IRQ surface used for meter, clip, and protection-timeout events.

## Interrupt Line

- Signal: consolidated interrupt output
- GPIO: `GPIO9`
- Active level: high
- Assertion rule: line is asserted when `(IRQ_STATUS.pending & IRQ_ENABLE.mask) != 0`.

## Registers

| Address | Name | Access | Description |
| --- | --- | --- | --- |
| `0x0080` | `MTR_IRQ_STATUS` | R,COR(bit0) | Pending interrupt summary, enable-mask mirror, and meter sequence LSB16. |
| `0x0084` | `MTR_IRQ_ENABLE` | RW | Interrupt enable mask. |
| `0x0088` | `MTR_IRQ_CTRL` | RW | Interrupt source clear controls (`W1C` behavior for clearable sources). |

## Bitfields

### `MTR_IRQ_STATUS` (`0x0080`)

- Bits `[7:0]`: pending interrupt bits
- Bits `[15:8]`: active enable-mask mirror
- Bits `[31:16]`: meter presentation sequence LSB16
- Read side effect: clears meter interval ready source (bit 0)

### `MTR_IRQ_ENABLE` (`0x0084`)

- Bit `0`: meter-interval-ready interrupt enable
- Bit `1`: clip-latch interrupt enable
- Bit `2`: service-timeout interrupt enable
- Bits `[31:3]`: reserved, write `0`

### `MTR_IRQ_CTRL` (`0x0088`)

- Bit `0` (`W1C`): clear meter interval ready source
- Bit `1` (`W1C`): clear all clip latch flags
- Bit `2` (`W1C`): reserved (service-timeout source is state-driven)

## Recommended Host Policy

1. Enable only required sources with `MTR_IRQ_ENABLE`.
2. On IRQ, read `MTR_IRQ_STATUS` once to collect pending summary and sequence.
3. Clear optional sources with `MTR_IRQ_CTRL` when host processing is complete.
