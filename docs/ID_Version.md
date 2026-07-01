# ID And Version

Back to the [root README](../README.md).

See also the [SPI Register Manual](Register_Manual.md).

## Purpose

This section defines how a host verifies register compatibility before touching runtime controls.

## Registers

| Address | Name | Access | Description |
| --- | --- | --- | --- |
| `0x0000` | `MTR_ABI_VERSION` | R | ABI version signature for host/firmware compatibility checks. |
| `0x0004` | `MTR_ABI_CAPS` | R | Capability bitmask that advertises supported register features. |

## ABI Version

`MTR_ABI_VERSION` returns the current firmware register ABI signature.

- Current value: `0x20260629`
- Host policy: verify this value at session start before writing runtime control registers.

## Capability Bits

`MTR_ABI_CAPS` bit definitions:

- Bit `0`: grouped EQ register layout supported.
- Bit `1`: split EQ band plans supported (31 primary / 15 SFX).
- Bit `2`: low-address consolidated interrupt block supported.
- Bit `3`: volume ISO226 compensation overlay supported.
- Bit `4`: DAC mute hook supported.

## Recommended Host Startup Sequence

1. Read `0x0000` and compare to the host-expected ABI version.
2. Read `0x0004` and branch host feature enablement from capability bits.
3. Continue with interrupt and runtime-register setup only if compatibility checks pass.
