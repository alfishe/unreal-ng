# SpecEmu Conditional Breakpoint System — Research Report

**Emulator:** SpecEmu v3.4 (latest build 25/04/2026), Windows ZX Spectrum emulator by Mark Woodmass ("Woody" / "woodywoodster").
**Primary sources used:** the actual SpecEmu 3.4 b250426 distribution (downloaded from the official site), including `Docs/Conditional Breakpoints.txt`, `Docs/Debugger command parser commands.txt`, the full `ChangeLog.txt`, plus dialog/menu resources extracted directly from `SpecEmu.exe` — the UI facts below are verified against the shipping binary, not just docs.

---

## 1. Capabilities matrix

| Capability | Supported | How |
|---|---|---|
| Execute (PC) breakpoints | Yes | "Simple breakpoints" list; double-click in disassembly; `Toggle Breakpoint`; right-click "Run to Here" |
| Conditional expression breakpoints | Yes (since v3.2 b24/03/2022, still labelled "preliminary") | "Conditional breakpoints" list in Breakpoints dialog; `stop <cond>` command; `/bpc` CLI arg |
| Memory read/write | Yes | Via condition pseudo-registers `MRA/MWA/MRV/MWV`; also a dedicated "Memory Breakpoint" panel (Address + Access R/W) in the debugger window; Cheats Finder can set R/W/RW memory breakpoints on found addresses |
| Port IN/OUT | Yes | Via `PRA/PWA/PRV/PWV` (aliases `IN`/`OUT`); also "Run until → Port Read from / Port Write to" menu triggers |
| T-state / cycle | Yes | `TS` condition variable (16-bit only); "Run until → Cycle Event → Specify Cycle"; `set tstates` command |
| Interrupt | Yes | `FRAMEINTS` variable; "Run until → Interrupt / Retriggered Interrupt" |
| Opcode triggers | Yes | "Run until → Opcode" (DI, EI, HALT, IM 1, IM 2, LD A,R, LD R,A…); `(pc)=…` conditions; `optime` (execution time of last opcode) |
| 128K paging awareness | Yes | `P0–P3`, `PAGING`, `SCREEN` condition variables |
| ROM enter/leave | Yes | Menu options "Break on entering/leaving ROM space" |
| Display/attribute (screen byte) breakpoints | Yes (b25/04/2026) | Mouse-set on the emulated display; per-byte, not rectangular regions |
| Tape event triggers | Yes | "Run until → Tape Event → Tape Starts / Tape Stops" (no per-block trigger) |
| Disk triggers | Yes | "Run until → Disk Event → Motor On/Off"; TR-DOS System/Command/Track/**Sector**/Data register port triggers; +3 FDC dialog "Command Breakpoint" (break on a specific µPD765 FDC command); `FDC0–FDC8` condition variables |
| Skip/hit counts | **No** | Not present anywhere in binary or docs |
| Rectangular screen-region breakpoints | **No** | Only single display/attribute byte breakpoints |
| OR / grouping in conditions | **No** | Implicit AND only |

## 2. Conditional expression syntax (documented grammar)

From `Docs/Conditional Breakpoints.txt` (verbatim-derived):

**Operands (condition variables):**
- CPU registers: `PC SP IX IY IXH IXL IYH IYL AF BC DE HL A F B C D E H L AF' BC' DE' HL' A' F' B' C' D' E' H' L' IR I R`
- `MPTR` or `WZ` (MEMPTR), `IM`, `IFF1`, `IFF2`
- Memory bus: `MRA MWA` (read/write address), `MRV MWV` (read/write value)
- Port bus: `PRA PWA PRV PWV`; `IN`/`OUT` accepted in place of `PRA`/`PWA`
- Paging: `P0 P1 P2 P3` (page at #0000/#4000/#8000/#C000; 0–7 or `#FF` for ROM), `PAGING` (all four nibbles, e.g. `PAGING = #F527` = ROM/5/2/7)
- Machine state: `TS` (T-state counter, "up to 65535 only"), `SNOW` (0/1), `SCREEN` (active 128K display, 0/1 — i.e. shadow screen), `BORDER` (0–7), `FRAMEINTS`, `AYSEL` (selected AY register), `FDC0`–`FDC8` (+3 FDC command parameters), `optime` (T-states of last opcode, added v3.3 b07/04/23)

**Memory dereference:** `(expr)` = byte at address, `(expr.w)` = little-endian word. The address can be a literal or a register: `(23560)=13`, `(pc)=#c7`, `(pc.w)=#b0ed`, `(32768.w) = ix`.

**Literals:** decimal by default; hexadecimal with `#` prefix (`#c000`, `#0d`). Note: *not* `FFh` or `0x` style, and there are no `Z/NZ/C/NC/P/M` flag mnemonics — you test the `F` register with masks instead (e.g. `f&1=1` for carry).

**Comparison operators:** `=`, `!=`, `<`, `>`, `<=`, `>=`. (Documented as `=`, not `==`; no `<>` documented.)

**Bitwise:** `&` (AND) usable on the left side of a comparison, e.g. `out&1=0`, `pwv&#10>0`. No `|` documented.

**Combination:** multiple space-separated conditions in one breakpoint are **implicitly ANDed** — there is no `AND`/`OR` operator and no parenthesized boolean grouping: *"Currently all breakpoint conditions can only be specified as conditions that must all be True (as if all results were ANDed together, but without an AND operator)."*

**Official examples:**
```
pc>=32768
hl=de b>128 b <= 255
(23560)=13
(32768.w) = ix iy!=23610
stop snow=1
stop pc=0
stop (pc)=#c7                       ; RST 0 about to execute
stop (pc.w)=#b0ed                   ; LDIR about to execute (little-endian!)
stop (pc.w)=#b0ed de>=49152         ; LDIR with DE range check
stop mwa>=23296 mwa<23552           ; catch writes into a memory range
stop P3=7 mwa>=#c000                ; what's writing to bank 7 in error
stop out&1=0 pwv&#10>0              ; ULA port write with speaker bit high
```

## 3. UI entry points

- **Breakpoints dialog** (debugger → View → Breakpoints; since v2.8 b03/08/2009): two lists — "Simple breakpoints" (Break at: address, Add/Remove/Remove All, Enable checkbox) and "Conditional breakpoints" (free-text expression, Add/Remove/Remove All, Enable). Double-clicking a breakpoint address jumps the disassembly there.
- **Command Parser** (console in the debugger): `stop <conditions>` creates a **one-shot** conditional breakpoint that *overrides all other conditional breakpoints until the debugger next opens*. Other commands: `set tstates`, `set intlen`, `ops` (opcode/max-timing stats), `slowmo`, `bw`, PC trace start/stop.
- **Command line:** `/bpc "hl=5 a=#0d"` adds a conditional breakpoint at launch (b31/08/25); also `/trace`, `/stoptrace`.
- **Disassembly context menu:** Run to Here, Toggle Breakpoint, Set As Next Instruction. Breakpoints highlighted in green.
- **Run until menu** (one-shot event triggers): Cycle Event (Start/End of Frame, Interrupt, Retriggered Interrupt, Specify Cycle), Opcode, Port Read from (keyboard half-rows, TR-DOS System/Status/Track/Sector/Data registers, Floating Bus, arbitrary port), Port Write to (ULA #FE, 0x7FFD, 0x1FFD, TR-DOS registers, arbitrary port), Tape Event (Starts/Stops), Disk Event (Motor On/Off).
- **+3 FDC state window:** "Break on:" a specific FDC command (Command Breakpoint), respected/overridden correctly by Run-To since b25/06/22.
- **On-screen display/attribute breakpoints** (b25/04/26): point the mouse at the emulated display and (a) pause + hold right button, click left; or (b) hold context-menu key + right button, click left — sets a breakpoint on that display or attribute byte.
- **Cheats Finder:** "Set memory breakpoint on" a result with Read / Write / Read-Write access.

## 4. Paging/bank awareness

Handled through the expression language rather than per-breakpoint bank tags: `P0–P3` (per-slot page, `#FF` = ROM), `PAGING` (whole config in one 16-bit compare), `SCREEN` (normal vs shadow screen on 128K), plus ROM-space enter/leave options. Example from the docs: `stop P3=7 mwa>=#c000`. There is no UI field to bind a simple PC breakpoint to a bank (contrast: Spectaculator has page-specific breakpoints; ZX Spin has an "Ignore page in breakpoints" toggle).

## 5. The "Screen Region Breakpoints" / Tape-Block / Disk-Sector screenshots — not matched to SpecEmu

The dialog described in the user's screenshots (rectangular screen regions with C,R,W,H; Tape Block / Disk Read / Disk Sector triggers with skip counts) could **not** be matched to SpecEmu: it appears nowhere in the current binary's dialog/menu resources, docs, or the 25-year changelog. Also checked and ruled out via binaries/docs/manuals:

- **ZX Spin 0.7** (binary inspected): simple + conditional breakpoints, "Ignore page in breakpoints" — no screen regions or tape/disk triggers.
- **Spectaculator 9/9.1**: the *closest* match ("screen breakpoints + hit counts") but no rectangular C,R,W,H regions and no tape-block/disk-sector triggers documented.
- **Es.pectrum**: X/R/W/I/O breakpoints with hex digit-wildcard patterns and ranges (`3DXX`, `0-3FFF`, `%8 X0`) — no screen/tape/disk triggers.
- **EightyOne, Zero, Spectrum Analyser, Icemark Spectrum Window Debugger, zxspectrum4.net**: none match.

Closest functional equivalents in SpecEmu itself: mouse-set display/attribute byte breakpoints + `stop mwa>=<a> mwa<<b>` range conditions (screen regions), Run-until Tape Starts/Stops (tape), and TR-DOS Sector-register / +3 FDC command breakpoints (disk sector).

## 6. Strengths and weaknesses (user-reported + observed)

**Strengths:** widely regarded as the most hardware-accurate Windows Spectrum emulator (won a WoS accuracy poll, ahead of Fuse); the bus-level condition variables (`MRA/MWA/MRV/MWV`, `PRA/PWA/PRV/PWV`, `PAGING`, `SNOW`, `FDC0-8`) let you break on things almost no other Spectrum debugger can express; PC logging/trace ("go back and see every single thing executed"); rich one-shot Run-until event system; `optime`/`ops` opcode-timing tools; `slowmo`+`bw` screen-drawing analysis; free.

**Weaknesses:** conditional breakpoints are self-describedly "preliminary" — the author's own doc opens with *"Not really useful yet..."*; no OR operator or grouping (AND-only); `TS` limited to 16 bits (can't express a full-frame T-state on frames > 65535); `=` vs `==` and no flag mnemonics trip up newcomers; no hit/skip counts, labels, or per-bank simple breakpoints (users wanting those cite Spectaculator 9); UI is dense/old-school and documentation sparse — community threads show developers mixing SpecEmu with Spin/Spectaculator/Fuse because "one emulator might be better at something but lack something else".

## Sources

- [The Unofficially Official Home of SpecEmu](https://specemu.zxe.io/) and the shipped `specemu-3.4.b250426.zip` (Docs/Conditional Breakpoints.txt, Docs/Debugger command parser commands.txt, ChangeLog.txt, SpecEmu.exe resources)
- [SpecEmu 3.4 b250426 ChangeLog](https://specemu.zxe.io/download/specemu-3.4.b250426-ChangeLog.txt)
- [Spectaculator 9 debugger Part 1: Breakpoints](https://www.spectaculator.com/2013/04/new-debugger-features-in-spectaculator-9-part-1-breakpoints/)
- [Es.pectrum manual (EN)](https://www.habisoft.com/espectrum/Manual/EN.htm)
- [ZX Spin 0.7 (Zophar's Domain)](https://www.zophar.net/sinclair/zx-spin.html) (binary inspected)
- [WoS forums: The best ZX Spectrum emulators](https://worldofspectrum.org/forums/discussion/49944/the-best-zx-spectrum-emulators) · [Spectrum Computing: SpecEmu or another emulator with a good Debugger?](https://spectrumcomputing.co.uk/forums/viewtopic.php?t=10152)
