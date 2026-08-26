# 04 — Memory Contention: The Missing Feature

**Date:** 2026-08-09
**HDL reference:** `ula.sv` lines 250-278
**Emulator reference:** `core/src/emulator/cpu/z80.cpp`, `core/src/emulator/platform.h`

---

## 1. What is ULA memory contention?

On the Sinclair ZX Spectrum, the ULA (video chip) and the Z80 CPU share access
to the lower 16K of RAM (0x4000-0x7FFF, which contains the screen and attribute
memory). The ULA has priority — it needs to read video data at a constant rate
to generate the TV signal.

When the CPU tries to access this "contended memory" during the active display
period, the ULA **stalls the CPU clock** (inserts WAIT states) until it has
finished its own memory access. This is called "memory contention" or "clock
stretching."

The result: CPU instructions that access contended memory take **longer** during
the visible screen area than during the border/blanking area.

## 2. MiSTer HDL: contention implementation

### 2.1 Contention logic

```verilog
// ula.sv lines 255-261
wire ioreq_n      = (addr[0] & ~(ulap_acc & ulap_avail)) | nIORQ;
wire clkwait_next = hc_next[2] | hc_next[3];              // Contention window
wire ulaContend   = clkwait_next & ~Border_next & CPUClk & ioreqtw3;
wire contendAddr  = ((addr[15:14] == 2'b01) |             // 0x4000-0x7FFF
                     (m128 & (addr[15:14] == 2'b11) & page_ram[0])); // 0xC000+ on 128K
wire memContend   = ioreq_n & mreqt23 & contendAddr;
wire ioContend    = ~ioreq_n;
wire next_clk     = hc_next[0] | (mZX & ulaContend & (memContend | ioContend));
```

### 2.2 How it works

- `clkwait_next`: Active during HC positions where ULA is reading video data
  (bits 2 and 3 of HC)
- `ulaContend`: True when the ULA is in its read window AND CPU is in a clock
  phase where contention can occur
- `contendAddr`: True when CPU is accessing contended memory (0x4000-0x7FFF on
  all models; also 0xC000+ on 128K when screen page is banked there)
- `next_clk`: The CPU clock enable. For Pentagon (`mZX=0`), this is simply
  `hc_next[0]` — **no contention at all**

### 2.3 Model-specific contention behavior

| Model    | Contended addresses       | Contention active? |
|----------|---------------------------|--------------------|
| Pentagon | 0x4000-0x7FFF             | **NO** (mZX=0)     |
| ZX-48K   | 0x4000-0x7FFF             | **YES**            |
| ZX-128K  | 0x4000-0x7FFF + 0xC000+   | **YES**            |

### 2.4 Contention pattern

The ULA stalls the CPU at specific t-states within each scanline. For ZX-48K
(224 t-states/line), the classic contention pattern is:

| T-state in line | Delay added |
|-----------------|-------------|
| 14337-14347     | 6 T-states  |
| 14347-14359     | 5 T-states  |
| 14359-14371     | 4 T-states  |
| 14371-14383     | 3 T-states  |
| 14383-14395     | 2 T-states  |
| 14395-14407     | 1 T-state   |

(Reference: https://worldofspectrum.org/faq/reference/48kreference.htm#Contention)

The pattern repeats every 8 t-states within the active display area (t-states
14337 through 57199 for ZX-48K, or roughly 62269 through 65535 for the last few
lines).

## 3. Emulator: contention is completely missing

### 3.1 Search results

A search across the entire `core/src/emulator` directory for contention-related
terms returned **zero results**:

```
grep -rn "contention|contend|delayMREQ|clockStretch|wait_state|waitstate" core/src/emulator/
(No matches found)
```

The Z80 CPU model in `core/src/emulator/cpu/z80.cpp` has no mechanism for
inserting delay cycles based on memory address or video timing.

### 3.2 How CPU timing works now

In `Z80::Z80FrameCycle()` (z80.cpp lines 374-403):

```cpp
uint32_t frameLimit = config.frame * state.current_z80_frequency_multiplier;
while (cpu.t < frameLimit)
{
    ProcessInterrupts(int_occurred, int_start, int_end);
    Z80Step();
    OnCPUStep();
}
```

Each `Z80Step()` advances `cpu.t` by the instruction's t-state count. There is
no adjustment for contended memory access. The CPU always runs at full speed.

### 3.3 The `even_M1` flag — also dead

The `config.even_M1` flag exists for the Scorpion model (forces M1 cycle
alignment to even t-states), but like `border_4T`, it is loaded from config
and **never used** in any code path.

## 4. Impact on accuracy

### 4.1 Pentagon — accidentally correct

The Pentagon clone has no ULA contention (the original Pentagon used a simpler
video controller that didn't share the bus the same way). The emulator's lack
of contention is correct for this model. ✅

### 4.2 ZX-48K / ZX-128K — major timing inaccuracy

Without contention emulation:

1. **CPU instructions run too fast** during screen rendering. A program that
   carefully times its effects based on contention delays will run at the wrong
   speed.

2. **Border/multicolor effects shift.** Programs that use contention to delay
   their `OUT (#FE)` or memory writes will fire at the wrong t-state relative
   to the raster beam. The effect may appear 1-6 t-states earlier than intended.

3. **Audio desync.** The beeper/AY timing depends on CPU cycle accuracy.
   Programs doing cycle-counted audio (e.g., 1-bit music routines) may produce
   wrong pitch or timing.

4. **Some copy protections break.** Several Spectrum games use contention-based
   timing checks as anti-piracy measures.

### 4.3 How this interacts with the other fixes

The contention fix should be implemented **before** or **alongside** the border
and multicolor fixes, because:
- Contention determines the exact t-state at which CPU writes occur
- Without correct contention, border latching and attribute latching will use
  wrong reference points
- Fixing contention alone will improve timing accuracy significantly, even before
  the rendering-side fixes

## 5. Required implementation

### 5.1 Add contention delay function

Create a contention model that returns delay t-states based on:
- Current t-state in the frame (only contend during active display)
- CPU address being accessed
- Current video mode (model)

```cpp
// Proposed: ContentionManager or method in Z80/Memory
uint8_t GetContentionDelay(uint16_t addr, uint32_t tstate, VideoModeEnum mode)
{
    if (mode == M_PENTAGON128K)
        return 0;  // No contention on Pentagon

    // Check if address is in contended range
    bool contended = (addr >= 0x4000 && addr < 0x8000);

    // For ZX-128K, also check high RAM when screen is banked there
    // ...

    if (!contended)
        return 0;

    // Calculate contention delay based on t-state within scanline
    uint32_t tInLine = tstate % tstatesPerLine;
    // Use the classic contention table...
    return delayTable[tInLine % 8];
}
```

### 5.2 Apply contention in CPU memory access

In the Z80 memory read/write handlers, after each memory access, add the
contention delay to `cpu.t`:

```cpp
// In Z80 memory read:
uint8_t Z80::mem_read(uint16_t addr)
{
    uint8_t val = ...;  // actual read
    cpu.t += GetContentionDelay(addr, cpu.t, currentVideoMode);
    return val;
}
```

### 5.3 Port contention

I/O port access to contended ports (like 0xFE) also triggers contention. The
MiSTer HDL handles this via `ioContend`:

```verilog
wire ioContend = ~ioreq_n;  // Any ULA port access contends
```

This should be applied in the port I/O path as well.

### 5.4 Reference resources

- Contention tables: https://worldofspectrum.org/faq/reference/48kreference.htm#Contention
- 128K contention: https://faqwiki.zxnet.co.uk/wiki/Contended_memory
- Implementation examples: ZEsarUX, FUSE, Spectacol emulators
