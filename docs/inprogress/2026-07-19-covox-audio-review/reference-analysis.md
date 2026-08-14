# COVOX Implementation Analysis: Reference Emulators vs unreal-ng

**Date:** 2026-07-19

---

## Overview

Analysis of COVOX/SOUNDRIVE implementations in ZXMAK2 and original Unreal Speccy to understand:
1. Why 4-channel SOUNDRIVE doesn't work in unreal-ng
2. Root causes of audio hiccups

---

## ZXMAK2 Implementation

### Architecture

ZXMAK2 uses an event-driven architecture where port handlers are registered separately:

```csharp
// CovoxStereo.cs - BusInit()
bmgr.Events.SubscribeWrIo(MaskR, PortR & MaskR, WritePortR);
bmgr.Events.SubscribeWrIo(MaskL, PortL & MaskL, WritePortL);
```

**Key insight:** Each port gets its own handler registered with the bus manager. When a port write occurs, the system dispatches directly to the correct handler with the original port address preserved.

### Sound Rendering (SoundDeviceBase.cs)

ZXMAK2 uses a sophisticated resampling approach based on original Unreal Speccy:

```csharp
// Queue-based sample storage
private readonly Queue<SndOut> m_sndQueue = new Queue<SndOut>();

// Port write triggers immediate sample queuing
protected void UpdateDac(ushort left, ushort right)
{
    var timestamp = (int)(GetFrameTime() * m_frameTactCount + 0.5D);
    UpdateDacInt(timestamp, left, right);
}
```

**Critical detail:** `GetFrameTime()` returns fractional frame time (0.0-1.0), which is then converted to an absolute t-state timestamp. This avoids issues with scaled timing in turbo modes.

### Frame-Wrap Handling

```csharp
if (timestamp < frameLength)
    sndout.Timestamp = timestamp;  // inframe
else
    sndout.Timestamp = timestamp - frameLength;  // overframe → next frame queue
```

ZXMAK2 uses two queues (`m_sndQueue` for current frame, `m_sndQueueNext` for samples that landed after frame end). This naturally handles wraparound without losing sub-frame offsets.

---

## Original Unreal Speccy Implementation

### Architecture

Unreal Speccy uses a simpler global-variable approach:

```cpp
// sound.h
extern int covFB_vol;  // FB port volume (mono)
extern int covDD_vol;  // DD port volume (mono)
extern int sd_l, sd_r; // Soundrive stereo

// io.cpp - port FB handler (line 631-637)
if (conf.sound.covoxFB && !(port & 4))
{
    flush_dig_snd();
    covFB_vol = val * conf.sound.covoxFB_vol / 0x100;
    return;
}
```

### Sound Rendering

```cpp
// sound.cpp - flush_dig_snd()
void flush_dig_snd()
{
    int mono = spkr_dig + mic_dig + covFB_vol + covDD_vol;
    sound.update(cpu.t - temp.cpu_t_at_frame_start, 
                 unsigned(mono + sd_l), 
                 unsigned(mono + sd_r));
}
```

**Key insight:** Unreal Speccy calculates `cpu.t - temp.cpu_t_at_frame_start` to get the relative in-frame t-state. This automatically handles turbo modes correctly because both values are in the same time domain.

### Soundrive Support (4-channel)

```cpp
// io.cpp line 619-630
if (conf.sound.sd && (port & 0xAF) == 0x0F)
{
    if ((unsigned char)port == 0x0F) comp.p0F = val;
    if ((unsigned char)port == 0x1F) comp.p1F = val;
    if ((unsigned char)port == 0x4F) comp.p4F = val;
    if ((unsigned char)port == 0x5F) comp.p5F = val;
    flush_dig_snd();
    sd_l = (conf.sound.sd_vol * (comp.p0F + comp.p1F)) >> 8;
    sd_r = (conf.sound.sd_vol * (comp.p4F + comp.p5F)) >> 8;
    return;
}
```

**Critical insight:** Original Unreal Speccy uses **different ports for Soundrive** (0x0F, 0x1F, 0x4F, 0x5F), not the SOUNDRIVE 1.05 ports (0xF1, 0xF3, 0xF9, 0xFB). The port matching (`port & 0xAF == 0x0F`) decodes these specific 4 ports.

---

## unreal-ng Problems Identified

### Problem 1: Port Canonicalization

In unreal-ng, the port decoder resolves all 4 SOUNDRIVE ports to a single canonical value:

```cpp
// portdecoder_pentagon128.cpp
{ 0b0000'0000'1111'0101, 0b0000'0000'1111'0001, 0x00FB },  // COVOX/SOUNDRIVE

// This mask (0xF5) and match (0xF1) catches:
// 0xF1 & 0xF5 = 0xF1 → match
// 0xF3 & 0xF5 = 0xF1 → match  
// 0xF9 & 0xF5 = 0xF1 → match
// 0xFB & 0xF5 = 0xF1 → match
// All resolve to device port 0x00FB
```

The `portToChannel()` function then tries to distinguish channels based on the **original** port, but by this point the original port is lost:

```cpp
Covox::Channel Covox::portToChannel(uint16_t port)
{
    uint8_t lowByte = port & 0xFF;  // This is 0xFB for all 4 ports!
    switch (lowByte)
    {
        case 0xF1: return Channel::LeftA;   // Never reached
        case 0xF3: return Channel::LeftB;   // Never reached
        case 0xF9: return Channel::RightA;  // Never reached
        case 0xFB: return Channel::RightB;  // Always taken
    }
}
```

**Solution options:**
1. Pass original port through `PeripheralPortOut` (requires interface change)
2. Register 4 separate port handlers like ZXMAK2
3. Keep mono-only (current intentional design)

### Problem 2: Turbo Mode Timing (FIXED)

The original bug:
```cpp
void Covox::handleFrameEnd()
{
    renderToBuffer(config.frame);  // Unscaled!
}
```

Port writes used `Z80::t` which is scaled, but frame-end used unscaled `config.frame`. This caused the buffer to only fill partially in turbo mode → hiccups.

**Fix applied:**
```cpp
uint32_t scaledFrame = config.frame * _context->emulatorState.current_z80_frequency_multiplier;
renderToBuffer(scaledFrame);
```

### Problem 3: Frame Wraparound (FIXED)

The original code:
```cpp
if (_prevFrameTState > frameTState)
    _prevFrameTState = 0;  // Loses sub-frame offset
```

This matches ZXMAK2's queue-based approach more closely after the fix:
```cpp
if (_prevFrameTState > frameTState && _prevFrameTState >= scaledFrame)
    _prevFrameTState -= scaledFrame;  // Preserves offset
```

---

## Why ZXMAK2 4-Channel Works

1. **Separate port registration**: Each of the 4 ports (PortL, PortR for stereo or 4 ports for SOUNDRIVE) has its own handler registered with the bus.

2. **Original port preserved**: The handler receives the actual port address, not a canonicalized value.

3. **Immediate DAC update**: Each port write immediately calls `UpdateDac(left, right)` with the appropriate channel value:

```csharp
// CovoxStereo WritePortL
m_left = (ushort)(value * m_mult);
UpdateDac(m_left, m_right);  // Immediately update with new left value
```

---

## Why Original Unreal Speccy 4-Channel Works

1. **Different ports**: Original uses 0x0F/0x1F/0x4F/0x5F (older Soundrive standard), not 0xF1/0xF3/0xF9/0xFB.

2. **Explicit port matching**: Each port is checked individually in the handler, then stored to separate variables:
```cpp
if ((unsigned char)port == 0x0F) comp.p0F = val;
if ((unsigned char)port == 0x1F) comp.p1F = val;
// etc.
```

3. **Sum-based stereo**: Left/Right are computed as sums of two channels each:
```cpp
sd_l = (conf.sound.sd_vol * (comp.p0F + comp.p1F)) >> 8;
sd_r = (conf.sound.sd_vol * (comp.p4F + comp.p5F)) >> 8;
```

---

## Recommendations for unreal-ng

### Keep Current Mono-Only Design (Recommended)

The current implementation is correct for mono COVOX demos that use port #FB only. The mono detection logic properly routes RightB to both channels:

```cpp
bool leftHasAudio = (_dacValue[0] != 0x80) || (_dacValue[1] != 0x80);
bool rightAHasAudio = (_dacValue[2] != 0x80);
if (!leftHasAudio && !rightAHasAudio)
{
    left = sampleRB;
    right = sampleRB;  // Mono mode
}
```

**Rationale:**
- No SOUNDRIVE 1.05 test content in our corpus
- Most Pentagon COVOX demos use mono #FB only
- Port canonicalization is a deliberate architectural choice

### Future: Full SOUNDRIVE Support

If needed, the cleanest approach would be:

**Option A: Extend PortDevice interface**
```cpp
void portDeviceOutMethod(uint16_t canonicalPort, uint16_t originalPort, uint8_t value);
```

**Option B: Register multiple handlers**
Register 4 separate port decoder entries that each resolve to a unique canonical port:
```cpp
{ 0xFF, 0xF1, 0x01F1 },  // SOUNDRIVE LeftA
{ 0xFF, 0xF3, 0x01F3 },  // SOUNDRIVE LeftB
{ 0xFF, 0xF9, 0x01F9 },  // SOUNDRIVE RightA
{ 0xFF, 0xFB, 0x01FB },  // SOUNDRIVE RightB
```

---

## Summary

| Feature | ZXMAK2 | Original Unreal | unreal-ng |
|---------|--------|-----------------|-----------|
| Port architecture | Per-port handlers | Global switch | Canonicalized port |
| Original port preserved | ✅ Yes | ✅ Yes | ❌ No |
| 4-channel support | ✅ Yes | ✅ Yes (different ports) | ❌ Mono only |
| Timing calculation | Frame-relative | T-state delta | Absolute T-state |
| Frame wraparound | Queue-based | N/A (delta) | Subtraction |
| Turbo mode | Automatic (delta) | Automatic (delta) | Fixed (scaled frame) |

The hiccups were caused by C1 (turbo mode truncation), now fixed. 4-channel support requires architectural changes to preserve original port information through the port decoder pipeline.
