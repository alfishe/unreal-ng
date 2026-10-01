# Evo control ports and the AVR (clock, NVRAM, keyboard, versions) — technical design

| | |
|---|---|
| **Date** | 2026-09-27 |
| **Status** | Design, ready for review. Nothing implemented |
| **Closes** | gaps **P-1…P-7, P-9, P-10**, **C-1…C-9**, **A-1…A-7**, **R-1**, **R-2** of [gap-analysis.md](gap-analysis.md); user item "BaseConf and AVR-boot indication in the ERS" |
| **Hardware source** | [baseconf-hardware-reference.md](baseconf-hardware-reference.md) §A (FPGA), §B (AVR), §C 2 (ERS) |
| **Shared with** | TSConf (PLAN #41): M1 hook (TSConf technical-design §3.6), Gluk extension window (TSConf hardware-spec §9), Z-Controller glue ([tdd-storage-sd-ide-cd.md](tdd-storage-sd-ide-cd.md) §3); NeoGS (PLAN #45): `Flash29F040B` |

## 0. Summary

Three pieces of work:

1. **Decoder corrections** in `PortDecoder_ATM3`: full `#FE/#F6/#FC` decode, shadow gating for the FDC,
   Kempston mouse and joystick, Covox, `#xBF7` write protect, the missing `#EFF7` rules. Each is a
   few lines with a truth-table test.
2. **Evo control block**: the `#xxBD` readback/config port (current FPGA) or `#xxBE` (legacy FPGA),
   the `#xxBE` exit strobe, and every `#xxBF` bit: NMI, breakpoint, flash writes, font RAM, 4:4:4
   palette. Plus ULA+.
3. **`EvoAvr`**: one device that replaces the plain `CMOS` array on ATM3. It emulates what the AVR
   firmware answers on the Gluk ports: the MC146818 clock, battery-backed NVRAM saved to a file,
   registers A-D with the Evo meanings, and the `F0-FF` extension window (firmware and bootloader
   versions, PS/2 scancode buffer, modes register, EEPROM window). TSConf uses the same AVR.

## 1. Glossary

See [gap-analysis.md](gap-analysis.md) §1 for BaseConf, ERS, AVR, Gluk, shadow, trdemu.

| Term | Meaning |
|---|---|
| **Programmed window type** | What the pager register says a window holds (ROM or RAM). NMI and trdemu temporarily override what is *mapped* without changing this. |
| **Exit strobe** | A write to `#xxBE`. The value is ignored; the act of writing ends an NMI or a trdemu swap. |
| **Set-2 scancode** | The byte sequence a PS/2 keyboard sends: one make code per press (`1C` for A), `F0 1C` on release, an `E0` prefix for extended keys. |

## 2. Decisions

| # | Decision | Reason |
|---|---|---|
| D1 | `[EVO] Fpga=trdemu` (default) or `legacy`. `trdemu` = current `base_trdemu` tree: readback on `#xxBD`, breakpoint at `#10BD/#11BD`, `#13BD`, `#BE` write-only, `#BF`.5, 8-bit joystick. `legacy` = frozen `baseconf` tree: readback on `#xxBE`, breakpoint writes `#00BD/#01BD`, RAM-disk latches `#2F-#8F`, 5-bit joystick | The ROM image decides what software expects; both images exist in the wild. One switch, one table (§3.2), no scattered `if`s |
| D2 | Default ROM becomes the official `zxevo_fe.rom` (current ERS, NEO-DOS in page 29) under `data/rom/zxevo-fe.rom`; `ATM3` config points at it. The current `data/rom/zxevo.rom` stays for `legacy` and for TSConf until TSConf gets its own TS-BIOS image (R-1) | `zxevo_fe.rom` pages 0-3 are an empty custom slot, so TSConf cannot share it. Kebab-case file name per repo rules |
| D3 | `EvoAvr` replaces `CMOS` for ATM3 and is reused by TSConf; the `CMOS` class stays for whoever else uses it | TSConf hardware-spec §9 describes the same Gluk extension; one implementation |
| D4 | NVRAM (`0E-EF`) and the 4 KiB EEPROM persist to `[EVO] NvramFile`: read once at power-on, written when the machine is destroyed. Empty (the core default) = session only; the GUI default `<AppDataLocation>/zxevo-nvram.bin` is an E10 item, because the core has no user data directory. A missing or truncated file keeps the power-on contents: NVRAM all `#00` (the CMOS base class zero-fills for determinism; with a blank NVRAM the ERS falls back to its defaults), EEPROM all `#FF` (erased: no user keymap). Tests always use a unique scratch path | Real board keeps these on a battery; the ERS stores every setting there (reset target, boot device, virtual drive, automount) |
| D5 | Clock registers keep the existing live/fixed-time switch (`SetFixedTime`); TTD and tests use fixed time | Determinism |
| D6 | Version records are the exact 16-byte tags of the released images: firmware from `cfgs/standalone_base_trdemu/trunk/zxevo_fw.bin` offset `0xC670` ("ZXEvo 4M", 07.01.2026, release), bootloader from `avrboot/trunk/avr/zxevo_bl.hex` address `0x1FFF0` ("ZXEvoAVRBoot", 25.05.2019, beta). Configurable strings (`[EVO] FirmwareVersion=` ...) are deferred until someone needs to mimic older firmware | Shows exactly what a stock board shows, CRC bytes included |
| D7 | NMI entry is modelled as "the CPU executed a `NOP` at `#0066` from the forced bus value, and the next fetch at `#0067` comes from RAM `#FF`": after the Z80's NMI acceptance, the ATM3 M1 hook sees `PC = #0066` with `nmiEntry` set, charges one `NOP` (4 T, R+1), sets `PC = #0067`, `inNmi = 1`, remaps | Observably identical to the RTL (`drive_00` + page switch on that M1's refresh) without an opcode-override path in the Z80 core |
| D8 | Flash writes use NeoGS's `Flash29F040B` (AMD/JEDEC state machine) over the 512 KB ROM buffer; `[EVO] FlashWrite=session` (default: changes live until the machine is destroyed), `persist` (write back to the ROM file), `off` (writes ignored) | Same model and the same three modes as NeoGS flash and SD writes; `persist` is opt-in because it rewrites a shipped file |
| D9 | PS/2 input is carried by the key event itself: one host key event holds both the ZX matrix code and the physical PC key, and is journaled once by the TTD live-input gateway (§6.1). No second event stream, no reserved code ranges | The scancode buffer is guest-visible input; replay must see the same bytes at the same T-state, and the physical key must survive the ZX decomposition (Up must stay Up, not become Caps Shift + 7) |
| D10 | Raster selection (A-7) is its own later phase; until then ATM3 keeps the 48K raster and `modes_register` reports raster `10` (48K) so the ERS shows what we emulate | Correct reporting now, full feature later |

## 3. Port map after this work

### 3.1 Always / by shadow state

`shadow = CF_TRDOS || #BF.0`. "noshad" = only when shadow is off.

| Port | Dir | When | Behavior | Gap |
|---|---|---|---|---|
| `#xxFE` (low byte exact) | R/W | always | keys/tape in; border 0-7, tape out, beeper | P-1 |
| `#xxF6` | R/W | always | read = `#FE`; write = border 8-15 only (no beeper/tape) | P-1 |
| `#xxFC` | W | always | border 0-7; if A15 = 0 also a `#7FFD` write | P-1 |
| `#7FFD` (A15=0, low `#FD`) | W | always | 128K mode (`#EFF7`.2 = 1): pages 0-2, screen 3, map 4, lock 5. 1 MB mode: page = `{D7..D5, D2..D0}`, no lock | P-6 |
| `#EFF7` (A12=0, low `#F7`, A8=1) | W | **noshad** | stored; bit 3 RAM 0 at `#0000`; bit 7 Gluk enable | P-6 |
| `#DFF7`/`#BFF7` | W / R/W | noshad **and** `#EFF7`.7 | Gluk address / data → `EvoAvr` | A-* |
| `#DEF7`/`#BEF7` | W / R/W | shadow (always enabled) | same | A-* |
| `#xFF7`, `#x7F7`, `#xBF7` | W | shadow | pager, 8-bit RAM page, **write protect** | P-5 |
| `#xx77` | W | shadow | ATM system port (unchanged) | — |
| `#xx77` | R/W | noshad | Z-Controller CS (storage design) | ST-1 |
| `#xx57` | R/W | always | SD data; in shadow with A15 = 1 it is the CS port | ST-1 |
| `#xx1F` | R | noshad | Kempston joystick, 8 bits (`trdemu`) / 5 bits (`legacy`) | P-2 |
| `#xxDF` | R | always | Kempston mouse: A8 = 0 buttons + wheel, A8 = 1 & A10 = 0 X, A10 = 1 Y | P-3 |
| `#xxFB` | W | always | Covox DAC (dispatch to the existing self-decoding `Covox`) | P-4 |
| `#1F/#3F/#5F/#7F/#FF` | R/W | shadow | WD1793 + system register; trdemu rules | ST-5 |
| `#xxFF` palette | W | shadow + palette-write mode | unchanged, plus 4:4:4 (§5.4) | C-7 |
| `#xxBF` | R/W | always | §3.3 | C-3…C-7 |
| `#xxBE` | W | always | exit strobe (§5) | C-2 |
| `#xxBD` / `#xxBE` read | R(/W) | always | §3.2 | C-1 |
| `#BF3B` / `#FF3B` | W / R/W | always | ULA+ register / data | C-8 |
| NemoIDE `#10…#F0/#11/#C8/#x8` | R/W | always | storage design | ST-2 |
| `#F8EF…#FFEF` | R/W | always | RS-232: `#FF` until A-6 is done | A-6 |

### 3.2 Readback / config table (one implementation, two port numbers)

`ReadEvoRegister(index)` / `WriteEvoRegister(index, value)`, index = A12..A8. `trdemu`: port `#xxBD`
read + write. `legacy`: port `#xxBE` read; writes only via `#00BD/#01BD` (breakpoint).

| Index | Read | Write |
|---|---|---|
| `00-03` | map 0 window 0-3 page, **inverted** (as written to `#x7F7`) | — |
| `04-07` | map 1 window 0-3 page, inverted | — |
| `08` | RAM/ROM bits (bit i = window i map 0, bits 4-7 map 1) | — |
| `09` | dos7ffd bits, same order | — |
| `0A` / `0B` | last `#7FFD` / last `#EFF7` (raw) | — |
| `0C` | `{~pen2, cpm_n, ~pen, DOS, turbo, mode[2:0]}` | — |
| `0D` | palette entry under the beam, `~{g,r,b,G,1,1,R,B}` | — |
| `0E` | font byte under the beam (§5.3) | — |
| `0F` | border (4 bits) | — |
| `10` / `11` | breakpoint address low / high | same (trdemu); legacy writes at `#00BD/#01BD` |
| `12` | write-protect bits (`#xBF7`), order as `08` | — |
| `13` | FDD emulation mask (trdemu only) | same |
| other | `#FF` | ignored |

### 3.3 `#xxBF` bits

| Bit | Effect | Reset |
|---|---|---|
| 0 | shadow ports on | 0 |
| 1 | flash write enable (§5.2) | 0 |
| 2 | font RAM write: every memory write also writes `fontRam[A & 0x7FF]` (§5.3) | 0 |
| 3 | NMI request on the 1 → 0 edge, delivered at the next INT start (§5.1) | 0 |
| 4 | breakpoint enable (§5.1) | 0 |
| 5 | 4:4:4 palette (trdemu only) | 0 |

Read returns `{00, b5..b0}` (trdemu) / `{000, b4..b0}` (legacy).

## 4. Reset and clock

Power-on/reset (hardware reference §A.4, §A.11): pager off (all windows ROM 31), DOS forced, mode
`011`, `#7FFD` = `#EFF7` = `#BF` = 0, **7 MHz** (P-7), NMI/trdemu/breakpoint state cleared, FDD mask 0,
Z-Controller CS deselected. Pager registers are **not** reset by hardware; we keep today's
`ApplyBootROMDefaults` values because they are overwritten by the ERS before first use.

## 5. Behaviors

### 5.1 NMI and breakpoint (C-3, C-4, P-8)

State: `nmiPending` (INT-synchronized request), `nmiEntry`, `inNmi`, `nmiExitCount`, `brkAddr`.

1. Sources: `#BF`.3 falling edge and the Magic key (`Emulator::RequestMNI()` extended to ATM3) set
   `nmiPending`. At the next frame-INT start the decoder raises the Z80 NMI line and sets `nmiEntry`.
   The breakpoint (M1 at `brkAddr` with `#BF`.4 = 1) raises the NMI immediately and sets `nmiEntry`;
   it stays armed.
2. Entry (D7): when the M1 hook sees `nmiEntry && PC == #0066`, it charges a `NOP`, moves PC to
   `#0067`, sets `inNmi = 1`, clears `nmiEntry`, remaps (window 0 = RAM `#FF`).
3. Exit: `OUT (#xxBE)` arms the exit; `inNmi` clears after the **next two M1 cycles**, counting
   prefix fetches (RTL `clr_count`, decremented per refresh, `znmi.v:152-163`). With the usual
   `OUT (#BE),A : RETN` the two M1s are `ED` and `45`, so `RETN` runs from page `#FF` and the next
   instruction from the restored map. The M1 hook must therefore see prefix M1s, not only
   instruction starts (NMI-2 tests both `RETN` and `NOP : RET`).
4. The existing dead path (`EmulatorState::nmi_in_progress`) is removed; the page rule reads
   `inNmi` from the decoder.

**As built (E3, 2026-09-28).** Generic hooks, empty for every other model:

| Hook | Where | Used for |
|---|---|---|
| `Z80::machineM1Hook` (`IMachineM1Hook::OnMachineM1(address)`) | called on **every** M1, prefix fetches included, right after the opcode read (the refresh edge the RTL acts on); out of line, one pointer test per M1 when unset | NMI exit countdown (`pBE`), breakpoint compare. The decoder attaches itself only while it has work there (`RefreshM1Hook`) and re-attaches after any state restore. This is the M1 hook TSConf phase 0 asked for (TSConf technical-design §3.6): reuse it rather than adding `CF_MACHINEM1` (all eight `CF_*` bits are taken) |
| `EmulatorState::nmiAtIntStartPending` + `PortDecoder::OnFrameIntStartNmi()` | checked in `Z80::ProcessInterrupts` at every boundary, also while halted; promoted to the /NMI pin inside the frame INT pulse; the board may veto it | `#BF` bit 3 edge and the Magic button |
| `PortDecoder::OnNmiAccepted()` | right after the Z80 NMI acknowledge | a board NMI: the Z80 charges the forced NOP (4 T, R + 1) and continues at `#0067`; the decoder maps RAM `#FF` |
| `PortDecoder::RequestBoardNmi()` | `Emulator::RequestMNI` | the Magic button goes to the board instead of a raw /NMI pulse |
| `PortDecoder::IsDosLeavingBank(bank)` | `CF_LEAVEDOSRAM` in the instruction-start hooks | ZX-Evo: the programmed window type decides, so the NMI page (and later the trdemu page) keep DOS on |

State: `EmulatorState::evoInNmi / evoNmiEntry / nmiAtIntStartPending` (one byte), `pBE` = M1s left
until the NMI page leaves; all three plus `pBE` are in the TTD `AtmPaging` blob.

### 5.2 Flash writes (C-5, P-5)

Memory writes to a window that is **programmed ROM**, while `#BF`.1 = 1 and the window's write-protect
bit is 0, go to `Flash29F040B::Write(romOffset, value)`. Reads from ROM windows go through
`Flash29F040B::Read` while a command is in progress (status polling: toggle bit, DQ7). Chip ID
`01/A4` (AMD Am29F040B); the ERS uses sector erase only for IDs `#E220`/`#A401`, so this ID
exercises the fast path. `FlashWrite` modes per D8. Write-protected windows (`#xBF7`) drop writes to
RAM as well as to flash.

Needs a memory-write intercept for ROM banks: a write-only `HostBusOverlay` (built by PLAN #60(a), 2026-09-29; TSConf technical-design §3.5 item 2), installed while `#BF`.1 = 1; write-protected windows drop the RAM store through the trash page.

### 5.3 Font RAM (C-6)

`fontRam[2048]`, initialized from the built-in `ATM_FONT` (the FPGA's power-on content). The
text-mode renderer reads `fontRam` instead of the constant table. `#BF`.2 = 1: every memory write
also writes `fontRam[addr & 0x7FF]` (the normal write still happens). `#0EBD` returns the font byte
the renderer is fetching at the current beam position, `#FF` outside text modes. Folds PLAN #53
item 2 into this design.

### 5.4 4:4:4 palette (C-7) and ULA+ (C-8)

With `#BF`.5 = 1 a `#FF` palette write takes the low bit of each channel from A15..A8 in the same bit
layout (`atm_paldatalow`, RTL `zports.v:917`), giving 4 bits per channel. `atmPalette` stores the
resulting RGB; `atmPaletteRegs` keeps both bytes for `#0DBD`. ULA+ is the standard 64-entry G3R3B2
palette with mode register bit 0 = on; it applies to the ZX modes only.

### 5.5 Small decoder fixes

| Gap | Rule |
|---|---|
| P-1 | exact low-byte decode for `#FE/#F6/#FC` (table §3.1) |
| P-2 | FDC ports in shadow only; joystick outside |
| P-3 | route `#xxDF` to `Default_Port_KempstonMouse_In` with the Evo sub-decode |
| P-4 | call `DispatchSelfDecodingOut/In` from the ATM decoders (fixes ATM710 too) |
| P-5 | `#xBF7` D0 → per-window write protect in the active map |
| P-6 | `#EFF7` not writable in shadow; bit 3 RAM 0; 1 MB `#7FFD` page bits; `#EFF7`.7 gates `#DFF7/#BFF7` |
| P-7 | reset at 7 MHz |
| P-9 | TTD blob: palette, palette regs, border-bright, the right CMOS latch (now `EvoAvr` state) |
| P-10 | ROM page count from the loaded image; port-trace model name `ZX-Evo BaseConf`; ATM rows in the port map (PLAN #8) |

## 6. `EvoAvr`

`core/src/emulator/memory/atm/evoavr.{h,cpp}`, derived from the `CMOS` class next to it (the clock
registers, the fixed-time test mode and the address latch are inherited; `ReadCMOS`/`WriteCMOS`
are virtual). Implemented in E2a except the PS/2 log (§6.1).

```cpp
class EvoAvr
{
public:
    void    SetAddress(uint8_t index);          // #DFF7 / #DEF7
    uint8_t Read();                             // #BFF7 / #BEF7
    void    Write(uint8_t value);

    // Host side
    void    OnPcKey(PcKey key, bool pressed);   // E2b: from the journaled key event (§6.1)
    void    SetModifiers(uint8_t dMask);        // register D bits 6..0
    void    SetSdStatus(bool present, bool wp); // register C bits 3/2, from the SD slot
    bool    LoadNvram(const std::string& path); // D4
    bool    SaveNvram(const std::string& path) const;

    void    SaveState(EvoAvrState& out) const;  // TTD (POD, static_assert on size)
    void    LoadState(const EvoAvrState& in);
};
```

Register behavior (hardware reference §B 1.3-1.6):

| Cells | Read | Write |
|---|---|---|
| `00-09` | time/date, BCD unless B.DM | stored (live clock keeps counting) |
| `0A` (A) | EEPROM page | EEPROM page |
| `0B` (B) | `#02 \| (DM << 2)` | keeps bit 2 only |
| `0C` (C) | `{eepromMode, 0, 0, UF, sdPresent, sdWp, caps, tapeOut}`; read clears UF | bit 7 → EEPROM mode; bit 1 → Caps LED; bit 0 = 1 → clear PS/2 buffer |
| `0D` (D) | `#80 \| modifiers` | ignored |
| `0E-EF` | NVRAM | NVRAM (persisted) |
| `F0-FF`, EEPROM mode | `eeprom[(A << 4) + (index & 15)]` | same (persisted) |
| `F0-FF`, extension mode | by `extType`: 0 firmware record byte `index-#F0`; 1 bootloader record; 2 pop PS/2 byte (0 empty, `#FF` overflow, reading `#FF` resets); 3 index `F0` = `modes_register`, others `#FF`; other types `#FF` | any value → `extType` (**never stored as data**, so a read never echoes it — this is what the ERS checks) |

Version record (16 bytes): name (12, zero-padded), date word little-endian
(`day | month << 5 | (year-2000) << 9 | release << 15`), CRC big-endian (a fixed value; nothing on the
Z80 side checks it). UF comes from the CMOS base class: host time in normal runs, never set while
the clock is frozen (tests, TTD-driven runs); a T-state-driven UF is a later refinement.

### 6.1 PS/2 keyboard (phase E2b — done 2026-09-30)

> **Implemented** on branch `zxevo-ps2` (2026-09-30), with two changes to the design below:
>
> - **Its own journal event, not a field on the matrix event.** A host key is journaled once as
>   `TTDInputKind::PcKey` (key = `PcKey`), next to the matrix events it also produces. A field on
>   the matrix event cannot carry keys that have no matrix event (F1, Home) and is tangled with the
>   decomposition and press counters (Up = Caps Shift + 7: two matrix events, one physical key).
>   Still journaled once, still applied at the one input apply point, never derived from the matrix.
>   Only journaled when the machine has a PS/2 sink, so other models pay nothing.
> - **Its own blob, `PeripheralId::EvoPs2` = 19** (40 bytes: log, pointers, parser flags, modifier
>   mask, held keys) instead of growing `AtmPagingState`, as §8 already suggested.
>
> The AVR side follows the firmware byte for byte: `ps2keyboard_parse` / `to_log` / `from_log`
> (`ps2.c`) and the modifier part of `to_zx` (`zx.c`), incl. the 15-byte ring, the overflow `#FF`,
> "after a reset the first byte logged must start a key", Pause never logged, protocol bytes dropped.
> Code: `core/src/emulator/io/keyboard/pckey.{h,cpp}` (keys, set-2 encoder, macOS / Windows / Linux
> host tables, ZX key and US character mappings), `EvoAvr` (`IPs2KeySink`), `Keyboard::SetPs2Sink`,
> `TTDEvoPs2`, `KeyboardManager::createKeyboardEvent` (Qt). Automation: typed text sends US-layout
> PC keys; key names accept PC keys (`f1`, `home`, `pc.up`) on WebAPI, CLI, Python and MCP.
> Tests: `pckey_test.cpp` (PS2-1), `evoavr_test.cpp` (PS2-2, PS2-3, AVR-2), `keyboard_test.cpp`
> (PS2-5, PS2-6), `ttdevops2_test.cpp` (PS2-4), `zxevo_ers_test.cpp` `NedoOsShellRunsATypedCommand`
> (NOS-KBD-1). Not done: ERS-KBD-1 (the ERS keyboard test screen); TSConf's `EvoAvr` has no sink
> yet (its TTD blob would need the PS/2 state too).

**Why it matters.** The NedoOS ZX-Evo kernel is built with `PS2KBD=1` (`kernel/build_kernel_evo.bat`):
its ZX-matrix scanner `syskey2.asm` is left out (`KEYSCAN` becomes a stub, `syskrnl.asm:526-538`) and
`ps2drv.asm` reads only the AVR scancode buffer (`bdospg2.asm:163-166`, `GETKEY` at `syskrnl.asm:814`).
Without the buffer NedoOS on ZX-Evo has **no keyboard at all**. The ERS "Test PC keyboard" uses it
too. Plain Spectrum software reads the `#FE` matrix, which is unaffected.

**The trap to avoid.** The host path (`Keyboard::OnKeyPressed`) decomposes extended keys into ZX
matrix keys *before* the TTD journal (host Up → Caps Shift + 7). Encoding PS/2 from those matrix
events gives `12 3D` (Shift + 7 = `&` in NedoOS) instead of `E0 75` (Up). The physical key must
travel with the event. Rejected shortcuts: deriving PS/2 from matrix events; a second, unjournaled
host-side push into the buffer (breaks TTD replay, and races the emulation thread); smuggling PC keys
through unused `ZXKeysEnum` codes such as `0xC0+` (overloads one field with two meanings).

**Design.**

```
Qt QKeyEvent ──► KeyboardEvent { zxKeyCode, pcKey }         (one message, both codes)
                    │  Keyboard::OnKeyPressed / OnKeyReleased (MessageCenter thread)
                    ▼
                 TTDInputEvent { kind = Key, key = zx, pcKey, pressed }   (journaled ONCE)
                    │  applied on the emulation thread (live or replay), same T-state
                    ├──► ZX matrix: existing decomposition + press counters (unchanged)
                    └──► IPs2KeySink::OnPcKey(pcKey, pressed)   (only when a sink is attached)
                              └─ EvoAvr: Ps2Set2Encoder → 16-byte log; register D modifiers
```

| Piece | Rule |
|---|---|
| `PcKey` enum (`core/src/emulator/io/keyboard/pckey.h`) | physical PC keys, modeled on USB HID usages: letters, digits, F1-F12, Esc, Tab, Caps Lock, both Shifts/Ctrls/Alts, Enter, Backspace, Space, arrows, Ins/Del/Home/End/PgUp/PgDn, punctuation, keypad. `PcKey::None` for events without a physical key (automation typing) |
| `KeyboardEvent` | gains `pcKey` next to `zxKeyCode`. Qt fills both from the same `QKeyEvent` (`nativeScanCode` / `key()`); keys with no ZX equivalent (F1-F12, Home...) arrive with `zxKeyCode = ZXKEY_NONE` and a valid `pcKey` instead of being dropped |
| `TTDInputEvent` | gains `pcKey`; **TTD format change**: bump the input-journal version, update `ttd.ksy` and `tools/verification/ttd-analyzer`, re-record the `testdata/ttd/` corpus. Old journals load with `pcKey = None` |
| Apply point | the single place that applies a journaled key (live and replay) updates the matrix from `key` and calls the sink with `pcKey`. Matrix press counters stay exactly as today |
| `IPs2KeySink` | `Keyboard::SetPs2Sink(IPs2KeySink*)`; `PortDecoder_ATM3` attaches its `EvoAvr` after construction and detaches in its destructor. Other models attach nothing, so they pay nothing |
| `Ps2Set2Encoder` | `PcKey` → set-2 make bytes; break = `F0` + make (extended: `E0 F0 xx`); Print Screen and Pause use their multi-byte sequences (the AVR does not log Pause). The table is checked against `avr/baseconf/trunk/src/kbmap.c` (e.g. LShift `12` = Caps Shift, LCtrl `14` = Symbol Shift) |
| `EvoAvr` log | 16 bytes (`ps2.c:72-178`): a read of a `F0-FF` cell in extension type 2 pops one byte, `0` when empty; a byte that does not fit sets overflow, the next read returns `#FF` and clears the log; writing register C bit 0 = 1 clears it. Register D bits 6..0 track the modifier keys. Protocol bytes (`FA/FE/EE/AA`) are never logged |
| TTD state | the log (16 bytes, count, overflow) and the modifier mask join the ATM3 blob |
| Automation | `keyboard/type` and friends send `PcKey` too when the target is ZX-Evo, so scripted typing reaches NedoOS |

**Tests (E2b).**

| ID | Asserts |
|---|---|
| PS2-1 | encoder table: `A` → `1C` / `F0 1C`; Up → `E0 75` / `E0 F0 75`; LShift `12`, LCtrl `14`, F1 `05`, Enter `5A`, Backspace `66`, Esc `76` |
| PS2-2 | log: empty reads `0`; 16 bytes fit; the 17th sets overflow → next read `#FF` → then `0`; register C bit 0 clears |
| PS2-3 | register D follows modifier make/break |
| PS2-4 | one host key event → one journal record carrying both codes; replay produces the same log bytes at the same T-state (TTD round trip) |
| PS2-5 | Up on the host reaches NedoOS as `E0 75`, not Shift + 7 (unit level on the host → sink path) |
| PS2-6 | models without a sink ignore `pcKey`; the ZX matrix result is identical with and without `pcKey` |
| NOS-KBD-1 | NedoOS Evo build: typing a shell command works (needs E5 to boot NedoOS) |

**Prerequisite.** Other work is in flight in `ttdinputjournal.*`, `timetravelmanager.*` and
`ttd.ksy` (2026-09-28); change the journal format only after that lands, so there is one format bump.

## 7. Configuration

```ini
[EVO]
Fpga=trdemu                 ; trdemu | legacy (D1)
NvramFile=                  ; empty = session only; default set by the GUI to the user data dir (D4)
FlashWrite=session          ; session | persist | off (D8)
FirmwareVersion=ZXEvo 4M    ; D6
FirmwareDate=2026-01-07
FirmwareRelease=1
BootloaderVersion=ZXEvoAVRBoot
BootloaderDate=2026-01-07
Raster=48k                  ; 48k now; pentagon | 128k | 60hz with phase E9 (D10)

[ROM]
ATM3=rom/zxevo-fe.rom       ; D2
```

`[ZC]` and `[HDD]` belong to the storage design. `[MISC] CMOS=` becomes a no-op for ATM3 (documented).

## 8. TTD

`AtmPagingState` grows (new blob version, size `static_assert`): `#BD` registers (breakpoint, mask,
write-protect bits), `#BF` bits, NMI state (`nmiPending/Entry/inNmi/exitCount`), trdemu state
(virtual TR-DOS design §3), palette and palette regs, border-bright, font RAM (2 KB; TTD v2 region
once PLAN #40 Phase 1 lands, a blob field until then), flash command state (the flash contents are the ROM
buffer: a TTD v2 region; until V1 the first flash write invalidates the recording, same rule as
floppy writes). `EvoAvr` gets its own `PeripheralId` (next free id, appended) with clock, NVRAM,
EEPROM, extension type, PS/2 buffer. `ttdmodelstatecontract_test` lists both for ATM3.

## 9. Automation

`GET /api/v1/emulator/{id}/state/evo` (and CLI `evo`, Lua/Python, MCP `inspect_state` aspect `evo`):
FPGA variant, `#BD` table decoded, `#BF` bits, NMI and trdemu state, AVR extension type, PS/2
buffer fill, NVRAM file. `POST …/evo/nvram` load/save/reset; `POST …/evo/nmi` (Magic button).
Port-trace decode rules and port-map rows for every port in §3.1.

## 10. Tests

Unit tests in `core/tests/emulator/ports/models/portdecoder_atm3_test.cpp` (extend) and
`core/tests/emulator/memory/atm/evoavr_test.cpp`; real-ROM tests in
`core/tests/emulator/machines/zxevo/` (skip when the ROM is absent; pin the image md5).

| ID | Asserts |
|---|---|
| DEC-1 | exhaustive sweep: all 65 536 ports × {shadow on/off} × {trdemu, legacy}: each port claimed by at most one arm; `#10…#F0` never reach `#FE` (P-1) |
| DEC-2 | `#F6` write changes border to 8-15 and leaves beeper/tape unchanged; `#FC` with A15 = 0 writes `#7FFD` |
| DEC-3 | FDC answers only in shadow; `#1F` outside shadow = joystick (8 bits trdemu, 5 bits legacy) |
| DEC-4 | mouse `#FADF/#FBDF/#FFDF` incl. wheel nibble; `#FF` with no mouse |
| DEC-5 | `OUT (#FB),#80` reaches the Covox device on ATM3 and ATM710 |
| DEC-6 | `#xBF7` protects RAM and ROM windows of the active map only |
| DEC-7 | `#EFF7`: ignored in shadow; bit 3 maps RAM 0 over the pager; 1 MB mode uses `#7FFD` bits 7-5; bit 7 opens `#DFF7/#BFF7` |
| DEC-8 | reset → 7 MHz, pager off, `#BD` state cleared |
| BD-1 | every index of §3.2 in both variants (port number, inverted pages, `12`, `13`) |
| BD-2 | NedoOS probe: `LD A,4 : IN A,(#BD)` returns the map-1 window-0 ROM value `AND #BF` = the DOS ROM page |
| NMI-1 | `#BF`.3 1 → 0 edge: NMI taken at the next frame INT, not before; `PC` at handler = `#0067`, R advanced by one extra M1, window 0 = RAM `#FF` |
| NMI-2 | exit: `OUT (#BE)` then `RETN` runs from page `#FF`, next instruction from the restored map; with `OUT (#BE) : NOP : RET` both `NOP` and `RET` run from `#FF` (two M1s) |
| NMI-3 | breakpoint: `#10BD/#11BD` = X, `#BF`.4 = 1 → NMI on the M1 at X immediately; again on the next pass |
| NMI-4 | Magic button API on ATM3 = `#BF`.3 path |
| FL-1 | `#BF`.1 = 0: ROM writes ignored; = 1: JEDEC program sequence changes the byte; write-protected window: ignored |
| FL-2 | sector erase + ID read (`01/A4`) + status polling; `FlashWrite=off` ignores; `persist` writes the file (scratch copy) |
| FNT-1 | `#BF`.2 = 1: a write to `#4000+n` also lands in `fontRam[n & 0x7FF]`; text mode renders the new glyph; `#0EBD` returns the byte under the beam |
| PAL-1 | `#BF`.5 = 1: 4:4:4 write via A15..A8; `#0DBD` readback |
| ULA-1 | ULA+ register/data round trip, ZX-mode rendering uses the ULA+ palette when enabled |
| AVR-1 | write 0 to `F0`, read `F0-FF` = firmware record; write 1 → bootloader record; a read never equals the written type |
| AVR-2 | PS/2: press+release A → `1C`, `F0 1C`; empty → 0; 17 bytes → `#FF` then reset; reg C bit 0 write clears |
| AVR-3 | reg C: SD present/WP bits follow `SetSdStatus`; UF cleared by read; reg D modifiers |
| AVR-4 | EEPROM mode window + page register A; persisted with NVRAM |
| AVR-5 | NVRAM persists across machine destroy/create via `NvramFile`; missing file → `#FF` fill; empty key → session only |
| AVR-6 | type 3 `F0` = `modes_register` with raster bits `10` (D10) |
| TTD-E1 | round trip of every new blob field; hash sensitivity; contract test lists `AtmPaging` + `EvoAvr` for ATM3 |
| **ERS-VER-1** | real ROM: ERS header shows `Baseconf: ZXEvo 4M 07.01.2026` and `AVR Boot: ZXEvoAVRBoot 07.01.2026` (screen text via the OCR/text helper); with `FirmwareRelease=0` the line ends in `beta` |
| **ERS-VER-2** | real ROM: no "Incorrect FPGA zxevo_fw.bin" with `Fpga=trdemu` |
| **ERS-CMOS-1** | real ROM: change the reset target in ERS setup, reset the machine → the choice survives (NVRAM); destroy/create with the same `NvramFile` → survives |
| **ERS-KBD-1** | real ROM: ERS "Test PC keyboard" shows the key pressed through the PS/2 path |
| **ERS-FLASH-1** | real ROM: ERS "Update custom ROM" with a 64K file from the SD fixture programs pages 0-3; GLUK/ProfROM pages unchanged |
| **NOS-KBD-1** | NedoOS Evo build: typing in the shell works through the PS/2 buffer (needs the SD phase for booting) |
