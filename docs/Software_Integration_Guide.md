# Software Integration Guide

Back to the [root README](../README.md).

See also the [SPI Register Manual](Register_Manual.md).

## Purpose

This guide defines a practical host software API layered over SPI register transactions, with examples for each integration function.

## Transport Contract

- SPI transaction is `32-bit address + 32-bit data`.
- Undefined reads return `0xDEADBEEF`.
- Float registers use IEEE-754 single-precision bit layout.

## Core Host Functions

```python
import struct

class DspHost:
    def __init__(self, bus):
        self.bus = bus

    def read_u32(self, addr: int) -> int:
        return self.bus.transfer(addr, 0)

    def write_u32(self, addr: int, value: int) -> int:
        return self.bus.transfer(addr, value & 0xFFFFFFFF)

    def read_f32(self, addr: int) -> float:
        raw = self.read_u32(addr)
        return struct.unpack('<f', struct.pack('<I', raw))[0]

    def write_f32(self, addr: int, value: float) -> float:
        raw = struct.unpack('<I', struct.pack('<f', value))[0]
        echoed = self.write_u32(addr, raw)
        return struct.unpack('<f', struct.pack('<I', echoed))[0]
```

## Integration Functions And Calls

### 1) Compatibility And Feature Discovery

```python
def read_abi(host: DspHost):
    version = host.read_u32(0x0000)
    caps = host.read_u32(0x0004)
    return version, caps

version, caps = read_abi(host)
assert version == 0x20260629
```

Information to set and why:

- Expected ABI version in host constants: prevents map mismatch.
- Capability bit handling: allows feature gating by firmware support.

### 2) Interrupt Configuration

```python
def configure_irqs(host: DspHost, meter=True, clip=True, timeout=False):
    mask = (1 if meter else 0) | ((1 if clip else 0) << 1) | ((1 if timeout else 0) << 2)
    host.write_u32(0x0084, mask)

configure_irqs(host, meter=True, clip=True, timeout=True)
```

Information to set and why:

- Source mask policy: balances responsiveness with host interrupt load.

### 2b) Clip-Latch Clear Strategy (Recommended)

Sticky clip latches are split across two clear-on-read registers:

- `0x00C0` (`MTR_CLIP_WARN_FLAGS_LO`) for bits `[31:0]`
- `0x00C4` (`MTR_CLIP_WARN_FLAGS_HI`) for bits `[63:32]`

Use one of these host patterns depending on integration model.

Polling model (aggregate read + full clear of both halves):

```python
def read_and_clear_clip_latches_polling(host: DspHost) -> int:
    lo = host.read_u32(0x00C0)  # clear-on-read low half
    hi = host.read_u32(0x00C4)  # clear-on-read high half
    return (hi << 32) | lo
```

Interrupt-driven model (clear clip source via IRQ control):

```python
def clear_clip_irq_source(host: DspHost):
    host.write_u32(0x0088, 0x00000002)  # MTR_IRQ_CTRL bit1 W1C
```

Operational guidance:

- If the host polls aggregate sticky clip history, always read both halves (`0x00C0` then `0x00C4`) in the same service cycle.
- If the host uses clip IRQ handling, clear the IRQ clip source with `MTR_IRQ_CTRL` bit `1` (`W1C`) after latching host-side diagnostics.
- Do not assume reading only one half provides a complete clip picture.

### 3) Mute And Volume Control

```python
def set_mute(host: DspHost, software: bool, dac: bool):
    value = (1 if software else 0) | ((1 if dac else 0) << 1)
    host.write_u32(0x0114, value)

def set_master_volume(host: DspHost, linear: float):
    host.write_f32(0x0110, linear)

set_mute(host, software=True, dac=True)
set_master_volume(host, 0.30)
set_mute(host, software=False, dac=False)
```

Information to set and why:

- Software mute flag: fastest digital silence path.
- DAC mute flag: analog-boundary silence control.
- Master volume scalar: user loudness control inside gain ceilings.

### 4) ISO226 Loudness Contour

```python
def configure_iso226(host: DspHost, enabled: bool, depth: float, ref_db: float):
    host.write_u32(0x011C, 1 if enabled else 0)
    host.write_f32(0x0120, depth)
    host.write_f32(0x0124, ref_db)

configure_iso226(host, enabled=True, depth=0.70, ref_db=-12.0)
```

Information to set and why:

- Depth (`0.0..1.0`): compensation intensity.
- Reference dB (`-40..0`): anchors when compensation ramps in.

### 5) Routing And Protection Selector Controls

```python
def set_routing_masks(host: DspHost, enable1: int, primary_bypass: int, sfx_bypass: int):
    host.write_u32(0x00A1, enable1)
    host.write_u32(0x00A6, primary_bypass)
    host.write_u32(0x00A7, sfx_bypass)

def request_service_bypass(host: DspHost):
    host.write_u32(0x00E0, 0x1)

def force_protected(host: DspHost):
    host.write_u32(0x00E0, 0x2)

set_routing_masks(host, enable1=0x04, primary_bypass=0x00000000, sfx_bypass=0x00000000)
```

Information to set and why:

- `enable1` mode bits: bit `0` selects primary 2ch/4ch mode, bit `1` selects SFX mono/stereo mode, bit `2` controls user mute target.
- Bypass masks: define per-lane stage bypass states (`0` active processing, `1` bypass enabled).
- Service selector commands: control protection/bypass operating mode.

### 6) Gains And Crossover

```python
def set_path_gain_limit(host: DspHost, ch: int, gain: float):
    host.write_f32(0x0130 + ch * 4, gain)

def set_notification_gain(host: DspHost, gain: float):
    host.write_f32(0x00A4, gain)

def set_crossover(host: DspHost, x0: int, x1: int, x2: int, protect_hp: int):
    host.write_u32(0x00EC, ((x1 & 0xFFFF) << 16) | (x0 & 0xFFFF))
    host.write_u32(0x00F0, ((protect_hp & 0xFFFF) << 16) | (x2 & 0xFFFF))
```

Information to set and why:

- Path limits: enforce per-output hardware-safe ceilings.
- Notification gain: controls blending into primary path.
- Crossover points: define frequency split boundaries.

### 7) EQ Group Operations

```python
EQ_BASES = [0x0200, 0x0300, 0x0400, 0x0500, 0x0600, 0x0700]


def set_eq_band(host: DspHost, group: int, band: int, gain: float):
    base = EQ_BASES[group]
    host.write_f32(base + 0x10 + band * 4, gain)


def apply_eq(host: DspHost, group: int):
    base = EQ_BASES[group]
    host.write_u32(base + 0x04, 0x45515131)

set_eq_band(host, group=0, band=0, gain=1.08)
apply_eq(host, group=0)
```

Information to set and why:

- Group index: picks target channel EQ block.
- Band index and gain: tune tonal balance for active plan bands.
- Apply key: commits composite recompute and sequence update.

### 8) Meter Readout Pattern

```python
def read_meter_page(host: DspHost, page: int):
    host.write_u32(0x00C8, page)
    value_dbfs_code = host.read_u32(0x00CC)
    latch = host.read_u32(0x00D8)
    raw_q23 = host.read_u32(0x00F8)
    age_samples = host.read_u32(0x00FC)
    return value_dbfs_code, latch, raw_q23, age_samples
```

Information to set and why:

- Page index (`0..37`): selects monitored internal stage.
- Meter mode (`0x00F4`): chooses unweighted or weighted representation.

## Recommended Integration Order

1. `read_abi`
2. `configure_irqs`
3. `set_mute` safe default
4. `set_path_gain_limit` and `set_notification_gain`
5. `set_crossover` and routing/bypass masks
6. EQ writes and apply
7. volume/ISO setup
8. controlled unmute and meter verification

## Quick Start Script (Power-On To First Verified Audio)

Use this as a reference integration flow for initial bring-up on a new host implementation.

```python
def quick_start(host: DspHost):
    # 1) ABI and feature negotiation
    version, caps = read_abi(host)
    if version != 0x20260629:
        raise RuntimeError(f"Unsupported ABI version: 0x{version:08X}")

    # 2) Safe startup state: hard mute + conservative gain envelope
    set_mute(host, software=True, dac=True)
    for ch in range(6):
        set_path_gain_limit(host, ch, 0.50)
    set_master_volume(host, 0.20)

    # 3) Base processing topology
    set_routing_masks(host, enable1=0x04, primary_bypass=0x00000000, sfx_bypass=0x00000000)  # primary 2ch + SFX mono baseline; all processing active
    set_crossover(host, x0=175, x1=3000, x2=25, protect_hp=10)

    # 4) Blend and contour defaults
    set_notification_gain(host, 0.70)
    configure_iso226(host, enabled=True, depth=0.70, ref_db=-12.0)

    # 5) Optional IRQ setup
    configure_irqs(host, meter=True, clip=True, timeout=True)

    # 6) Controlled unmute sequence
    set_mute(host, software=False, dac=True)   # release digital mute first
    set_mute(host, software=False, dac=False)  # then release DAC mute

    # 7) Basic output verification through meter pages
    checks = {}
    for page in [0, 4, 8, 16, 24, 32]:
        checks[page] = read_meter_page(host, page)
    return checks
```

### Why This Sequence Works

- ABI check first prevents accidental writes to mismatched firmware.
- Mute and low gain first reduces startup transients and protects downstream hardware.
- Routing and crossover are established before tonal tuning so signal path behavior is deterministic.
- Notification gain and ISO contour are set before unmute to avoid a sudden spectral jump.
- Unmute in two stages gives a cleaner handoff from digital to DAC boundary.

### Quick Triage If Audio Is Not Correct

1. Read `0x0118` (`MTR_VOL_STATE`) and confirm both mute bits are clear.
2. Read `0x00A8` (`MTR_SYS_STATUS`) and verify ADC-valid and no persistent clip condition.
3. Read a few meter pages and confirm non-zero values where expected.
4. If clipping is present, lower `MTR_VOL_MASTER` and per-path limits before changing EQ.
