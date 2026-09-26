# General Sound (GS) Card Implementation

## 1. Overview

The General Sound (GS) card is a sound expansion board for ZX Spectrum clones featuring a secondary Z80 CPU running at 12 MHz, 128–512 KB RAM, 32 KB ROM, and four 8-bit DAC channels. It was designed by Stinger/CPU in 1997 and became one of the most popular sound expansions for Pentagon-class machines.

**Priority:** P0  
**Scope:** Config-based enable/disable only (no runtime hot-swap).

### 1.1 Hardware Summary

| Component | Specification |
|:----------|:--------------|
| CPU | Z80 @ 12 MHz |
| ROM | 32 KB (2 × 16 KB pages) |
| RAM | 128–512 KB (8–32 × 16 KB pages) |
| DAC | 4 × 8-bit channels, 6-bit volume each |
| Interrupt | 37.5 kHz (every 320 CPU cycles) |
| Ports (host) | #B3 (data), #BB (command/status), #33 (control) |

### 1.2 Design Goals

1. **Config-driven activation** — GS enabled via `[SOUND] GSType=Z80` in machine config (legacy key compatibility)
2. **LLE Z80 emulation only** — Full Z80 coprocessor; HLE/BASS mode is out of scope for this design
3. **Lazy sync model** — Flush GS CPU on every host port access + frame-boundary catch-up (Xpeccy model)
4. **Separate audio channel** — Like MoonSound/TSFM, integrated with existing HUD volume meters
5. **Universal port injection** — Works across Pentagon, ATM, Scorpion, ZX Evo via `RegisterPortHandler`

### 1.3 Related Designs

Cross-reference these sibling designs for established patterns:
- `2026-09-13-moonsound/` — Config-key reuse, turbo/sound-off, wide-mix prerequisite
- TSFM design — TTD serialization, SoundManager integration

---

## 2. Hardware Architecture

### 2.1 Memory Map (GS Internal)

```
0x0000–0x3FFF  ROM page 0 (or RAM0 when NOROM bit set)
0x4000–0x7FFF  RAM page 3 (fixed, DAC sample buffers)
0x8000–0xBFFF  ROM/RAM page N (switchable via port 0x00)
0xC000–0xFFFF  ROM/RAM page M (switchable via port 0x00 or 0x10)
```

**DAC sample fetch:** Any memory read with `(addr & 0xE000) == 0x6000` triggers DAC output.
- Channel = `(addr >> 8) & 0x03`
- Aliases span `0x6000–0x7FFF` (firmware uses narrow `0x6x00–0x6xFF` windows)

| Address Range | Channel | Stereo |
|:--------------|:--------|:-------|
| `0x6000–0x60FF` | 1 | Left |
| `0x6100–0x61FF` | 2 | Right |
| `0x6200–0x62FF` | 3 | Right |
| `0x6300–0x63FF` | 4 | Left |

### 2.2 Port Map

#### Host-side ports (ZX Spectrum → GS)

| Port | R/W | Function |
|:-----|:----|:---------|
| #B3 | W | Write data byte to GS (sets bit 7 of status) |
| #B3 | R | Read data byte from GS (clears bit 7 of status) |
| #BB | W | Write command byte to GS (sets bit 0 of status) |
| #BB | R | Read status register: `gsstat \| 0x7E` (bits 1–6 always high) |
| #33 | W | Control: bit 7 = reset (discards pending GS execution), bit 6 = NMI |

**Port decoding:** `(port & 0xFF)` — low byte only; high byte ignored.  
**Bit 3 semantics:** `(port & 0xF7) == 0xB3` — bit 3 selects data (0) vs command (1).

> **High-byte aliasing:** Real GS software often uses `IN A,(n)/OUT (n),A` which puts A in
> the high byte (e.g., `LD A,#BB / IN A,(#BB)` decodes at 0xBBBB). Both Unreal and Xpeccy
> decode only the low byte (zxbus.v:154-156 confirms 8-bit compare). RegisterPortHandler
> uses 16-bit keys; registering 0x00B3/0x00BB/0x0033 misses high-byte aliases.
>
> **Design decision:** Add low-byte-match mode to PortDecoder for these three ports, OR
> register all 256 aliases (0x00B3, 0x01B3, ..., 0xFFB3). Unmatched IN returns 0xFF on
> this port bus. Verify against real GS software (MHM, Dizzy MOD players) before accepting.

#### GS-side ports (internal Z80)

| Port | R/W | Function |
|:-----|:----|:---------|
| 0x00 | W | Memory page select (MPAG) — see §2.3 |
| 0x01 | R | Read command from host |
| 0x02 | R/W | Clear bit 7 of status (read returns 0xFF) |
| 0x03 | R/W | Write data to host / set bit 7 (read sets bit 7, returns 0xFF) |
| 0x04 | R | Read status register |
| 0x05 | R/W | Clear bit 0 of status (read returns 0xFF) |
| 0x06–0x09 | W | Volume registers (channels 1–4, 6-bit) |
| 0x0A | W | Copy page bit 0 to status bit 7 |
| 0x0B | W | Copy volume1 bit 5 to status bit 0 |

> **NGS-only ports (P2, out of scope):** 0x0F (GSCFG0), 0x10 (MPAGEX), 0x16–0x19 (ch 5–8), 0x1B–0x1F (DMA).

### 2.3 MPAG Page Encoding (Original GS — P0)

Memory page select (port 0x00) follows **firmware/Xpeccy semantics**:

```cpp
// Original GS hardware rule (verified via firmware INIT_L.a80:90-120)
// V==0 → ROM pair; V≥1 → RAM pair (V-1)
if (val == 0) {
    // Windows 2,3 = ROM pages 0,1
    _window2 = _rom;
    _window3 = _rom + 0x4000;
} else {
    // Windows 2,3 = RAM pair (val-1), masked to installed RAM
    uint8_t pair = (val - 1) & _ramPairMask;  // _ramPairMask = (ram_kb/32)-1
    _window2 = _ram + pair * 0x8000;
    _window3 = _window2 + 0x4000;
}
_mpagValue = val;  // Preserve for port 0x0A copyback
```

| Write Value | Window 2 (0x8000) | Window 3 (0xC000) |
|:------------|:------------------|:------------------|
| 0x00 | ROM page 0 | ROM page 1 |
| 0x01 | RAM pair 0 | RAM pair 0+1 |
| 0x02 | RAM pair 1 | RAM pair 1+1 |
| ... | ... | ... |

**_ramPairMask** = `(ram_kb / 32) - 1` — limits pair index to physical RAM.

> **NeoGS (P2):** Uses rotated encoding `gspage = rol8(val,1) & mask` with NOROM bit
> controlling ROM/RAM selection wholesale. See neogs-tdd.md §2.2.

### 2.4 Timing

- **GS CPU clock:** 12 MHz
- **Interrupt frequency:** 37,500 Hz (12,000,000 / 320)
- **Cycles per interrupt:** 320 T-states
- **Interrupt delivery:** Level-triggered simplification (see note below)

> **Hardware note:** NeoGS FPGA generates a ~4.2µs pulse (interrupts.v:40-73); original GS
> uses discrete logic. We use
> level-hold (Xpeccy model: `intrq |= Z80_INT`) as a safe simplification — firmware 1.04/1.05
> never misses interrupts with IFF enabled, and z80ex-style cores expect level semantics.
> NMI from #33 is also pulsed (~4 Z80 cycles) — we latch it rather than sample.

**Synchronization:** Derive ratio from machine's actual clock (platform.h):
```cpp
// Base ZX frequency: config.frame / (config.frame_duration_us * 1e-6)
// Account for turbo: multiply by config.current_z80_frequency_multiplier
double zx_hz = config.frame / (config.frame_duration_us * 1e-6);
double effective_zx_hz = zx_hz * config.current_z80_frequency_multiplier;
// GS stays at 12 MHz regardless of ZX turbo
const double gs_ratio = 12000000.0 / effective_zx_hz;
```

**Sync model:** Lazy flush on port access + frame-boundary catch-up (see §4.5).

---

## 3. Cross-Emulator Analysis

### 3.1 Unreal Speccy (`gsz80.cpp`)

**Architecture:** Full Z80 coprocessor emulation with separate CPU instance.

Key implementation details:
- Namespace `z80gs` contains GS-specific Z80 loop
- `flush_gs_z80()` — runs GS CPU until synchronized with ZX CPU
- `gsbankr[4]` / `gsbankw[4]` — read/write bank pointers (16 KB each)
- Sound output via `flush_gs_sound()` feeding per-sample data
- Supports NGS extensions (8 channels, extended paging, SD card, VS1001 MP3)

**Port handling:**
```cpp
void out_gs(unsigned port, u8 val)  // Host → GS
u8 in_gs(unsigned port)             // Host ← GS
void out(unsigned port, u8 val)     // GS internal OUT
u8 in(unsigned port)                // GS internal IN
```

**Volume calculation:**
```cpp
gs_v[chan] = ((signed char)(gsbyte[chan]-0x80) * (signed)gs_vfx[gsvol[chan]]) / 256 + gs_vfx[33];
```

### 3.2 Xpeccy (`gs.c`)

**Architecture:** Simplified Z80 emulation using shared CPU abstraction.

Key implementation details:
- `GSound` structure holds all state
- `gsSync(gs, ns)` — advances GS CPU by nanoseconds
- Uses generic `CPU*` from libxpeccy's CPU abstraction
- Memory: 2 MB RAM, 32 KB ROM via `memSetBank()`
- Interrupt every 320 cycles via `gs->cnt` counter

**Port check:**
```c
if ((adr & 0xf7) != 0xb3) return 0;  // Bit 3 selects register
```

**Volume output:**
```c
sndPair gsVolume(GSound* gs) {
    res.left = ((gs->ch1 * gs->vol1 + gs->ch2 * gs->vol2) >> 1);
    res.right = ((gs->ch3 * gs->vol3 + gs->ch4 * gs->vol4) >> 1);
}
```

### 3.3 Implementation Comparison

| Feature | Unreal Speccy | Xpeccy |
|:--------|:--------------|:-------|
| CPU emulation | Dedicated TGsZ80 instance | Shared CPU abstraction |
| Memory model | Direct bank pointers | Memory subsystem |
| NGS support | Full (8ch, SD, MP3) | Basic 4-channel |
| Synchronization | Flush on every port access | Flush on port access |
| Volume curve | gs_vfx[] lookup + cross-feed | Simple multiply |
| Complexity | ~670 LOC | ~240 LOC |

**Key insight:** Both emulators flush GS on every host port access (gsz80.cpp:217,233; gs.c:196,209). This is required — ZX software polls status after writes; bulk frame-end execution would cause one-frame latency per byte.

### 3.4 Volume and Stereo Model

**Volume curve (Unreal gsz80.cpp:249,357):**
```cpp
// gs_vfx[65] table built from conf.sound.gs_vol (gs.cpp:13-20, make_gs_volume)
gs_v[chan] = ((signed char)(gsbyte[chan]-0x80) * (signed)gs_vfx[gsvol[chan]]) / 256 + gs_vfx[33];
```

**Stereo mapping:** Ch 1,2 → L; Ch 3,4 → R (all emulators + NeoGS FPGA sound_main2.v).

> **Note:** gs-programming-guide.md §4.1 shows ch1=L, ch2=R, ch3=R, ch4=L — this describes
> physical jack wiring, not emulator channel mixing. Every emulator uses 1,2→L, 3,4→R.

**Stereo mixing with cross-feed (Unreal gsz80.cpp:179-182):**
```cpp
l = gs_v[0] + gs_v[1];  // Channels 1,2 -> left
r = gs_v[2] + gs_v[3];  // Channels 3,4 -> right
lv = (l + r/2) / 2;     // Cross-feed
rv = (r + l/2) / 2;
```

**Decision:** Use Unreal's gs_vfx curve (rebuilt from gs_vol config) and cross-feed stereo.

---

## 4. Implementation Plan

### 4.1 Files to Create

| File | Purpose |
|:-----|:--------|
| `core/src/emulator/sound/chips/gs/soundchip_gs.h` | GS device class declaration |
| `core/src/emulator/sound/chips/gs/soundchip_gs.cpp` | GS device implementation |
| `core/tests/emulator/sound/soundchip_gs_test.cpp` | Unit tests |

### 4.2 Files to Modify

| File | Change |
|:-----|:-------|
| `core/src/emulator/platform.h` | Use existing `CONFIG.sound.gsreset`, `gs_vol`; add `gstype` enum |
| `core/src/emulator/sound/soundmanager.h` | Add `hasGeneralSound()`, `getGeneralSound()` getters (M8 pattern) |
| `core/src/emulator/sound/soundmanager.cpp` | Create/destroy GS based on `[SOUND] GSType=Z80` |
| `core/src/emulator/ports/portdecoder.cpp` | Register GS ports with `PortTag::SoundGs` |

**Config integration:** Reuse legacy keys from `pentagon128k/unreal.ini` (lines 361-362, 392-394):
```ini
[SOUND]
GSType=Z80       ; Z80 | BASS | NONE (BASS = HLE, out of scope)
GSReset=1        ; GS persists across ZX reset (separate subsystem)
gs_vol=100       ; 0-100

[NGS]            ; P2, out of scope
RamSize=512
```

> **Implementation note (2026-09-20, BUG-6):** `[NGS] RamSize` stays a NeoGS-only
> key. The classic card is created with the stock geometry constant
> `SoundChip_GeneralSound::RAM_SIZE_STANDARD_KB` (128 KB): feeding the shipped 2048 KB
> NeoGS default into the classic card clamped it to 512 KB, quadrupling the firmware
> POST so fastdisk-booted trainers probed the card mid-POST and bailed on the missing
> 0x7E idle signature (scorpion-family ZONE128.SCL boots). The chip constructor still
> accepts 128-512 KB for expansion-card emulation. See
> [`verification-findings-and-bugs.md`](verification-findings-and-bugs.md) BUG-6.

### 4.3 CPU Isolation Strategy

**Critical:** GS requires a dedicated Z80 core isolated from the main emulator.

The main Z80 (`core/src/emulator/cpu/z80.cpp`) cannot be reused:
- Hardwired to EmulatorContext: memory via `_context->pMemory`, breakpoints via `pDebugManager`
- Tape/disk fast-load traps, opcode profiler (lines 174–394)
- Constructor requires EmulatorContext (z80.h:362-363)

Neither Unreal Speccy nor Xpeccy reuses their main Z80 — Unreal has a dedicated `TGsZ80` variant.

**Recommended: Option A — embed a minimal dedicated Z80 core (z80ex or similar):**

```cpp
#include <z80ex/z80ex.h>  // Lightweight Z80 core

class SoundChip_GeneralSound : public PortDevice
{
private:
    z80ex_context* _cpu;  // Dedicated lightweight Z80
    
    // Memory/IO callbacks registered with z80ex
    static Z80EX_BYTE gsMemRead(Z80EX_CONTEXT*, Z80EX_WORD addr, int, void* self);
    static void gsMemWrite(Z80EX_CONTEXT*, Z80EX_WORD addr, Z80EX_BYTE val, void* self);
    static Z80EX_BYTE gsIORead(Z80EX_CONTEXT*, Z80EX_WORD port, void* self);
    static void gsIOWrite(Z80EX_CONTEXT*, Z80EX_WORD port, Z80EX_BYTE val, void* self);
};
```

Benefits:
- Clean separation, no EmulatorContext dependencies
- Simpler TTD serialization (standard z80ex state dump)
- No risk of breaking main CPU optimizations
- z80ex from [github.com/alfishe/z80ex](https://github.com/alfishe/z80ex)

### 4.4 Class Design

```cpp
// SoundDevice(clockRate, sampleRate) provides the ring buffer + update() API
class SoundChip_GeneralSound : public PortDevice, 
                                public SoundDevice,
                                public ttd::TTDSerializable
{
public:
    // SoundDevice ctor: SoundDevice(12000000, coreRate) for 12MHz GS clock
    SoundChip_GeneralSound(EmulatorContext* context);
    virtual ~SoundChip_GeneralSound();

    // PortDevice interface (ZX-side ports)
    uint8_t portDeviceInMethod(uint16_t port) override;
    void portDeviceOutMethod(uint16_t port, uint8_t value) override;

    // TTDSerializable interface (ttdserializable.h)
    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "GeneralSound"; }
    ttd::PeripheralId TTDPeripheralId() const override { return ttd::PeripheralId::GeneralSound; }
    uint64_t TTDHashState() const override;

    // GS-specific
    void reset();
    void loadROM(const std::string& romPath);

private:
    EmulatorContext* _context;

    // Dedicated GS Z80 (z80ex, NOT main emulator Z80)
    z80ex_context* _cpu = nullptr;
    std::vector<uint8_t> _rom;      // 32 KB (warn + zero-fill if missing)
    std::vector<uint8_t> _ram;      // CONFIG.sound.ngs_ramsize KB

    // Memory banking (§2.3 MPAG encoding)
    uint8_t _mpag = 0;
    uint8_t _gsRamMask;             // (ram_kb / 16) - 1
    uint8_t* _bankR[4];             // Read bank pointers
    uint8_t* _bankW[4];             // Write bank pointers

    // Communication (ZX ↔ GS)
    uint8_t _dataIn = 0;
    uint8_t _dataOut = 0;
    uint8_t _command = 0;
    uint8_t _status = 0x7E;         // Bits 1-6 always high

    // DAC channels with gs_vfx volume curve
    uint8_t _channelData[4] = {0x80, 0x80, 0x80, 0x80};
    uint8_t _channelVol[4] = {0};
    int32_t _channelOut[4] = {0};
    static const uint16_t gs_vfx[65];  // Volume curve table

    // Timing
    int64_t _gsCycles = 0;
    int64_t _gsFrameStart = 0;
    int _intCounter = 0;
    double _gsRatio;                // 12MHz / machine_clock

    // Sync helpers
    void flushToZxCycle(size_t zxTacts);
    void triggerInterrupt();
};
```

### 4.5 Port Registration

```cpp
// In SoundManager, following TSFM/MoonSound pattern
PortDecoder* pd = _context->pPortDecoder;
pd->RegisterPortHandler(0x00B3, this, {PortTag::SoundGs});  // Data
pd->RegisterPortHandler(0x00BB, this, {PortTag::SoundGs});  // Command/status  
pd->RegisterPortHandler(0x0033, this, {PortTag::SoundGs});  // Control
// High-byte aliases: see §2.2 design decision (low-byte-match or register all)
```

### 4.6 Synchronization Strategy

**Model:** Lazy flush on port access + frame-boundary catch-up (Xpeccy model).

Both references flush GS on every host port access (gsz80.cpp:217,233; gs.c:196,209). This is required — ZX software polls status after writes; bulk frame-end execution causes one-frame latency per byte.

```cpp
// Called from portDeviceInMethod/portDeviceOutMethod BEFORE accessing status
void SoundChip_GeneralSound::flushToZxCycle(size_t zxTacts)
{
    // Derive ratio from actual machine clock (handles turbo modes)
    int64_t targetGsCycles = static_cast<int64_t>(zxTacts * _gsRatio);
    
    while (_gsCycles < targetGsCycles)
    {
        int tStates = z80ex_step(_cpu);  // Execute one instruction
        _gsCycles += tStates;
        _intCounter += tStates;          // Count T-states, not instructions
        
        if (_intCounter >= 320)
        {
            _intCounter -= 320;
            triggerInterrupt();
        }
    }
}
```

---

## 5. Config Integration

### 5.1 Legacy Config Keys (platform.h)

Use existing keys from `pentagon128k/unreal.ini` (lines 361-362, 392-394):

```cpp
// In CONFIG.sound struct (platform.h:532-537)
struct {
    uint8_t gsreset;     // GS persists across ZX reset
    uint8_t gs_vol;      // 0-100 volume
    // Add:
    GSType gstype;       // enum { NONE, Z80, BASS }
} sound;
```

### 5.2 Machine Config (unreal.ini)

```ini
[SOUND]
GSType=Z80            ; Z80 | BASS | NONE (BASS = HLE, P2)
GSReset=1             ; GS separate from ZX reset
gs_vol=100
gs_rom=gs105a.rom     ; NEW KEY: ROM file (no legacy key exists)

[NGS]                 ; P2, out of scope for this design
RamSize=512
```

### 5.3 Initialization Flow

```
Emulator::reset()
  └─> SoundManager::reset()
        └─> if (config.sound.gstype == GSType::Z80)
              └─> createGeneralSound()
                    ├─> _gs = new SoundChip_GeneralSound(context)
                    ├─> _gs->loadROM(config.sound.gs_rom)  // warn + zero-fill if missing
                    └─> registerPorts()

TimeTravelManager::RegisterModelPeripherals()  // timetravelmanager.cpp:1041+
  └─> if (auto* gs = _context->pSoundManager->getGeneralSound())
        └─> _peripherals.Register(PeripheralId::GeneralSound, gs)
```

### 5.4 GSReset Semantics

GS persists across ZX reset (it's a separate subsystem):
- `#33` reset discards pending GS execution, does NOT flush first
- ZX reset does not reset GS unless `GSReset=0`

### 5.5 Threading Model

**GS runs in the emulator thread, NOT a separate thread.**

Both reference implementations (Unreal, Xpeccy) run GS synchronously:
- GS CPU is flushed on every ZX port access (required for polling)
- Sound samples generated during frame execution
- Audio buffer handed to audio thread at frame end

```
┌─────────────────────────────────────────────────────────────┐
│  Emulator Thread                                            │
│  ┌─────────────────────────────────────────────────────────┐│
│  │ ZX CPU executes                                         ││
│  │   └─> OUT #BB → flushToZxCycle() → GS CPU catches up   ││
│  │   └─> IN #BB  → flushToZxCycle() → return status       ││
│  │ frameEnd() → GS CPU final catch-up → mix audio buffer  ││
│  └─────────────────────────────────────────────────────────┘│
│                          │                                   │
│                          ▼                                   │
│                  Audio buffer ready                          │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│  Audio Thread (existing SoundManager audio callback)        │
│  └─> Consumes mixed buffer, sends to hardware               │
└─────────────────────────────────────────────────────────────┘
```

**No additional synchronization primitives needed** — GS shares the emulator's existing audio buffer handoff mechanism.

---

## 6. Audio Integration

### 6.1 Sound Manager Pattern

Follow TSFM/MoonSound pattern — SoundManager owns GS, provides getters:

```cpp
// soundmanager.h
class SoundManager {
    SoundChip_GeneralSound* _gs = nullptr;
public:
    bool hasGeneralSound() const { return _gs != nullptr; }
    SoundChip_GeneralSound* getGeneralSound() { return _gs; }
};
```

### 6.2 Audio Mixing (Per-Write Emission)

Inherit `SoundDevice` and emit samples on each DAC memory write (not frame-splat):

```cpp
// DAC memory-write hook (§3.3 memory read trigger)
void SoundChip_GeneralSound::dacWrite(uint16_t addr, uint8_t val, uint32_t tact)
{
    uint8_t ch = (addr >> 8) & 3;
    _channelData[ch] = val;
    
    // Per-channel volume with centering (Unreal gsz80.cpp:249)
    // gs_v[ch] = ((signed char)(data - 0x80) * gs_vfx[vol]) / 256 + gs_vfx[33]
    auto gsv = [this](int ch) {
        int centered = (int8_t)(_channelData[ch] - 0x80);
        return (centered * _gs_vfx[_channelVol[ch]]) / 256 + _gs_vfx[33];
    };
    
    int32_t l = gsv(0) + gsv(1);  // Ch 1,2 → L
    int32_t r = gsv(2) + gsv(3);  // Ch 3,4 → R
    
    // Cross-feed (Unreal gsz80.cpp:179-182)
    float lv = (l + r/2) / 2.0f / 32768.0f;  // Normalize to [-1,1]
    float rv = (r + l/2) / 2.0f / 32768.0f;
    
    update(tact, lv, rv);  // SoundDevice::update(tact, float, float)
}
```

Stereo mapping: ch 1,2 → L; ch 3,4 → R (Unreal/Xpeccy/NeoGS FPGA, not gs_prog.pdf §4.1 table).

### 6.3 Turbo Mode Handling

Follow MoonSound D2 precedent: when turbo mode skips render frames, GS still executes but audio may be discarded. Check `SoundManager::isSynthesisSuppressed()` (soundmanager.h:243).

### 6.4 HUD Integration

Use Unreal's LED computation (gsz80.cpp:167-172):
```cpp
gsleds[ch].level = abs(int(gsbyte[ch] - 0x80) * gsvol[ch]) / ((128 * 63) / 15);
```

---

## 7. ROM Files

### 7.1 Available ROMs

| File | Version | Notes |
|:-----|:--------|:------|
| `data/rom/bootGS.rom` | NeoGS flash | 512 KB (NeoGS flash image, not minimal) |
| `data/rom/gs104.rom` | v1.04 | Classic firmware |
| `data/rom/gs105a.rom` | v1.05a | Recommended, most compatible |

### 7.2 ROM Loading

Follow MoonSound D10 precedent — warn + zero-fill if missing, don't throw:

```cpp
void SoundChip_GeneralSound::loadROM(const std::string& filename)
{
    // filename from config.sound.gs_rom (pattern: rom.cpp:410 uses config.romSetSOSPath)
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    
    _rom.resize(32768, 0x00);  // Zero-fill first
    
    if (!file)
    {
        MLOGWARNING("GS ROM not found: %s — running with zeroed ROM", filename.c_str());
        return;
    }
    
    size_t size = file.tellg();
    file.seekg(0);
    
    if (size == 32768)
    {
        file.read(reinterpret_cast<char*>(_rom.data()), 32768);
    }
    else if (size == 524288)  // bootGS.rom is 512KB — §7.1 label is wrong
    {
        MLOGWARNING("GS ROM %s is 512KB (NeoGS flash), using first 32KB", filename.c_str());
        file.read(reinterpret_cast<char*>(_rom.data()), 32768);
    }
    else
    {
        MLOGWARNING("GS ROM %s unexpected size %zu, loading what fits", filename.c_str(), size);
        file.read(reinterpret_cast<char*>(_rom.data()), std::min(size, size_t(32768)));
    }
}
```

> **Note:** `bootGS.rom` is actually 512 KB (NeoGS flash image), not "minimal init ROM".

---

## 8. TTD (Time-Travel Debugging) Integration

### 8.1 TTD Registry Expansion

**Step 1:** PeripheralId enum already includes GeneralSound (ttdserializable.h:49):
```cpp
enum class PeripheralId : uint8_t {
    // ... existing entries ...
    GeneralSound = 5,  // Already defined
    // ...
};
```

**Step 2:** Implement TTDSerializable interface:
```cpp
// SoundChip_GeneralSound implements these (ttdserializable.h)
size_t TTDStateSize() const override;      // Fixed blob size
void TTDSaveState(uint8_t* dst) const override;  // Write state
void TTDLoadState(const uint8_t* src) override;  // Restore state
std::string TTDDeviceName() const override { return "GeneralSound"; }
PeripheralId TTDPeripheralId() const override { return PeripheralId::GeneralSound; }
uint64_t TTDHashState() const override;    // For divergence detection
```

**Step 3:** Registration in TimeTravelManager::RegisterModelPeripherals (timetravelmanager.cpp:1060+):
```cpp
if (auto* gs = _context->pSoundManager->getGeneralSound())
    _peripherals.Register(PeripheralId::GeneralSound, gs);
```

### 8.2 State Structure

```cpp
#pragma pack(push, 1)
struct GSState {
    uint8_t status;           // Communication status
    uint8_t dataIn;           // ZX → GS data
    uint8_t dataOut;          // GS → ZX data
    uint8_t command;          // Last command
    uint8_t mpag;             // Memory page register
    uint8_t channelVol[4];    // Volume registers
    uint8_t channelData[4];   // Current sample values
    int32_t gsCycles;         // Cycle counter (low 32 bits)
    int16_t intCounter;       // Interrupt counter
};  // 21 bytes (packed)
#pragma pack(pop)
```

### 8.3 Full State (includes Z80 + RAM)

TTDStateSize() returns:
- Z80 registers: ~26 bytes (from z80ex state)
- Communication state: 21 bytes (GSState above)
- RAM: 128–512 KB (original GS); TTDPeripheralRegistry compresses the blob

### 8.4 Registration in TimeTravelManager

GS registration happens alongside other peripherals in `RegisterModelPeripherals()`:

```cpp
// timetravelmanager.cpp:1060+ pattern
if (auto* gs = _context->pSoundManager->getGeneralSound())
    _peripherals.Register(PeripheralId::GeneralSound, gs);
```

---

## 9. Debugger Integration

### 9.1 GS CPU Debug Window

When GS is enabled, expose second CPU state in debugger:

```cpp
struct GSDebugState {
    // Z80 registers
    uint16_t pc, sp;
    uint16_t af, bc, de, hl;
    uint16_t af_, bc_, de_, hl_;
    uint16_t ix, iy;
    uint8_t i, r;
    bool iff1, iff2;
    uint8_t im;
    
    // GS-specific
    uint8_t currentBank[4];
    uint8_t status;
};
```

### 9.2 Memory View

GS memory accessible in debugger as separate address space:
- Address range: `GS:0000`–`GS:FFFF`
- Bank indicators in memory map view
- ROM/RAM distinction visible

### 9.3 Breakpoints

Support breakpoints on GS CPU:
- PC breakpoints
- Memory read/write breakpoints
- Port I/O breakpoints (internal ports 0x00–0x0F)

---

## 10. Automation Modules

### 10.1 WebAPI

The existing stub at `GET /api/v1/emulator/{id}/state/audio/gs` (state_audio_api.cpp:473)
needs implementation. Follow the drogon pattern from `getStateAudioAY`:

```cpp
// state_audio_api.cpp — implement existing stub (drogon-based WebAPI)
void EmulatorAPI::getStateAudioGS(const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id)
{
    auto* gs = _context->pSoundManager->getGeneralSound();
    if (!gs) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        callback(resp);
        return;
    }
    // Return GS state JSON
}
```

Response format:
```json
{
    "enabled": true,
    "status": 126,
    "command_pending": false,
    "data_pending": true,
    "page": 5,
    "channels": [
        {"sample": 128, "volume": 32},
        {"sample": 128, "volume": 32},
        {"sample": 128, "volume": 32},
        {"sample": 128, "volume": 32}
    ]
}
```

### 10.2 MCP Tools

Follow existing aspect naming in mcp-tools.cpp:587-588 (`audio_ay`, `audio_fm`):

```cpp
// Add to inspect_state aspect vocabulary (mcp-tools.cpp:587+)
{"audio_gs", "/api/v1/emulator/{id}/state/audio/gs"}

// Route at mcp-tools.cpp:914-917 pattern
```

### 10.3 CLI Commands

New file: `core/automation/cli/src/commands/cli-processor-gs.cpp`

```
state audio gs      → GS state (follows existing `state audio ay` pattern)
```

### 10.4 Lua Bindings

Follow existing pattern in `lua_emulator.h` — access via SoundManager:

```cpp
luaState["gs_enabled"] = [this]() -> bool {
    return _context->pSoundManager->getGeneralSound() != nullptr;
};

luaState["gs_state"] = [this]() -> sol::table {
    auto* gs = _context->pSoundManager->getGeneralSound();
    if (!gs) return sol::nil;
    sol::table t = luaState.create_table();
    t["status"] = gs->getStatus();
    // ... populate table ...
    return t;
};
```

### 10.5 Python Bindings

```python
# emulator module extensions

def gs_enabled() -> bool:
    """Check if General Sound card is enabled."""
    
def gs_state() -> dict:
    """Get GS state as dictionary."""
    
def gs_reset() -> None:
    """Reset General Sound card."""
    
def gs_send_command(byte: int) -> None:
    """Send command byte to GS."""
    
def gs_send_data(byte: int) -> None:
    """Send data byte to GS."""
    
def gs_read_data() -> int:
    """Read data byte from GS."""
    
def gs_read_status() -> int:
    """Read GS status register."""

def gs_channel_info(channel: int) -> dict:
    """Get channel info: {sample, volume, level_db}."""
```

Implementation in `python_emulator.h` — access via SoundManager:
```cpp
m.def("gs_enabled", [this]() {
    return _context->pSoundManager->getGeneralSound() != nullptr;
});

m.def("gs_state", [this]() {
    auto* gs = _context->pSoundManager->getGeneralSound();
    if (!gs) return py::none();
    py::dict d;
    d["status"] = gs->getStatus();
    // ... populate dict ...
    return d;
});
```

---

## 11. Documentation Updates

### 11.1 Files to Create/Update

| File | Action | Description |
|:-----|:-------|:------------|
| `docs/command-interface.md` | Update | Add GS state/control commands to command reference |
| `core/automation/webapi/openapi.json` | Update | Fix GS stub description, add response schema |
| `core/automation/cli/README.md` | Update | Document `state audio gs` command |
| `core/automation/mcp/README.md` | Update | Document `audio_gs` aspect |

### 11.2 WebAPI Implementation

| Endpoint | Method | Handler | Description |
|:---------|:-------|:--------|:------------|
| `/api/v1/emulator/{id}/state/audio/gs` | GET | `EmulatorAPI::getStateAudioGS` | Return GS state JSON |
| `/api/v1/emulator/{id}/control/audio/gs` | POST | `EmulatorAPI::postControlAudioGS` | Reset/send cmd/data |

### 11.3 MCP Tool Expansion

```cpp
// mcp-tools.cpp — add to aspect vocabulary
{"audio_gs", "/api/v1/emulator/{id}/state/audio/gs"}

// mcp-tools.cpp — add to emulator_manage actions
case "gs_reset": // Reset GS card
case "gs_send_command": // Send command byte
case "gs_send_data": // Send data byte
```

### 11.4 CLI Command Reference

```
state audio gs           # Show GS state (status, page, channels)
state audio gs --verbose # Include Z80 registers
```

### 11.5 Lua API

| Function | Returns | Description |
|:---------|:--------|:------------|
| `gs_enabled()` | bool | Check if GS is enabled |
| `gs_state()` | table | Get full GS state |
| `gs_reset()` | nil | Reset GS card |
| `gs_send_command(byte)` | nil | Send command to GS |
| `gs_send_data(byte)` | nil | Send data to GS |
| `gs_read_data()` | int | Read data from GS |
| `gs_read_status()` | int | Read status register |

### 11.6 Python API

Same functions as Lua, exposed via pybind11 in `python_emulator.h`.

### 11.7 GS Stub Fix

The existing GS stub at `/api/v1/emulator/{id}/state/audio/gs` has a factually incorrect description claiming GS "was never released commercially" — fix this during implementation.

---

## 12. Test Plan

### 12.1 Unit Tests

```cpp
// soundchip_gs_test.cpp
TEST(GeneralSound, PortDecode_B3_IsData)
TEST(GeneralSound, PortDecode_BB_IsCommand)
TEST(GeneralSound, StatusBit0_SetOnCommand)
TEST(GeneralSound, StatusBit7_SetOnDataWrite)
TEST(GeneralSound, StatusBit7_ClearOnDataRead)
TEST(GeneralSound, VolumeRange_0to63)
TEST(GeneralSound, PageSwitch_ROMtoRAM)
TEST(GeneralSound, InterruptTiming_Every320Cycles)
TEST(GeneralSound, TTDSerialize_RoundTrip)
TEST(GeneralSound, TTDStateChanged_DetectsVolume)
```

### 12.2 Integration Tests

- Load GS-enabled machine config
- Verify ports #B3/#BB respond
- Reset via #33 port
- Basic command/data exchange
- TTD frame capture/restore
- WebAPI state endpoint
- CLI command output

### 12.3 CPU Isolation Tests

```cpp
// Verify GS CPU is fully isolated from main CPU
TEST(GeneralSound, CPUIsolation_SeparateRegisters)
TEST(GeneralSound, CPUIsolation_SeparateMemory)
TEST(GeneralSound, CPUIsolation_SeparateCycleCounter)
TEST(GeneralSound, CPUIsolation_MainCPUUnaffected)
```

---

## 13. Implementation Checklist

- [ ] Create `SoundChip_GeneralSound` class with isolated Z80 instance
- [ ] Implement host port handlers (#B3, #BB, #33)
- [ ] Implement internal port handlers (0x00–0x0B)
- [ ] Memory banking (ROM/RAM page switching)
- [ ] DAC sample fetch on memory read
- [ ] Volume calculation
- [ ] Interrupt generation (37.5 kHz)
- [ ] Config fields and loading
- [ ] ROM loading from `data/rom/`
- [ ] Port registration via `RegisterPortHandler`
- [ ] Sound output integration
- [ ] HUD level display
- [ ] TTD serialization/deserialization
- [ ] Unit tests (port, volume, timing, isolation)
- [ ] Integration tests
- [ ] WebAPI state/control endpoints
- [ ] MCP tool extension
- [ ] CLI commands
- [ ] Lua bindings
- [ ] Python bindings
- [ ] Documentation updates

---

## 14. References

### Local Materials (`materials/gs/`)

| File | Description |
|:-----|:------------|
| [`gs_prog.pdf`](materials/gs/gs_prog.pdf) | Official GS Programming Manual (31 pages, Russian) |
| [`gs-programming-guide.md`](materials/gs/gs-programming-guide.md) | Compiled English guide with command reference |
| [`gs-firmware/`](materials/gs/gs-firmware/) | Original firmware source (Z80 assembly) |
| [`gs-firmware/sch/GS_schematic.pdf`](materials/gs/gs-firmware/sch/GS_schematic.pdf) | Circuit schematic |
| [`gs-firmware/docs/mod_form.txt`](materials/gs/gs-firmware/docs/mod_form.txt) | MOD format specification |

### Web Sources

| Source | URL |
|:-------|:----|
| GS Firmware Repository | https://github.com/psbhlw/gs-firmware |
| Spectrum Computing DB | https://spectrumcomputing.co.uk/entry/1000171/Hardware/General_Sound |
| Wikipedia (Russian) | https://ru.wikipedia.org/wiki/General_Sound |
| ZX-News #26 Article | https://zxpress.ru/ru/ezines/zx-news/26/general-sound-muzykalnaya-karta-dlya-zx-spectrum-s-podderzhkoy-8-bitnogo-zvuka-4-kanalov-i-mod |
| NedoPC NeoGS Page | http://nedopc.com/gs/ngs.php |
| ZX-PK Schematic Thread | https://zx-pk.ru/threads/6007-general-sound-(skhema).html |

### Reference Emulator Sources

| Emulator | Files | Notes |
|:---------|:------|:------|
| Unreal Speccy | `gsz80.cpp`, `gshle.cpp`, `gs.h` | Full Z80 + NGS support |
| Xpeccy | `src/libxpeccy/sound/gs.c`, `gs.h` | Simplified model |

### Related Designs

| Design | Path |
|:-------|:-----|
| MoonSound | `docs/inprogress/2026-09-13-moonsound/` |
| TSFM | (search for TSFM design doc) |
