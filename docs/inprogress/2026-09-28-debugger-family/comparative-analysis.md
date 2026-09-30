# Comparative analysis: what is best, most convenient and most striking, by category

- **Date:** 2026-09-28
- **Status:** draft for review. Second of four documents
  ([use-cases](use-cases.md) · this · [proposition](proposition.md) ·
  [roadmap](roadmap.md)).
- **Evidence:** the survey folder
  [2026-09-28-emulator-debugger-survey](../2026-09-28-emulator-debugger-survey/):
  Xpeccy+, Unreal Speccy, TS-Labs Unreal, ZXMAK2, Kozynax, zxsp, Spectral,
  ZX-M8XXX, Zero, 8BitAnalysers, jnext, MAME, Mesen2, FCEUX, BizHawk, WinUAE,
  vAmiga, DeZog. Spectaculator and ZXSpin surveys are added when their
  documents land ([§20](#20-pending-inputs)). Every claim below is backed by
  the named survey document, which cites source lines.

> **In one line.** Nobody is best everywhere. The Spectrum emulators lead on
> Spectrum-specific triggers (beam, ports, pages), the NES and Amiga tools lead
> on visualization (event viewers, DMA overlays) and data tooling (CDL, RAM
> search), and MAME leads on one language for everything. Our TTD and
> automation are already ahead of all of them.

## Contents

- [0. Method](#0-method)
- [1. Execution control and stepping](#1-execution-control-and-stepping)
- [2. Breakpoints: kinds and keying](#2-breakpoints-kinds-and-keying)
- [3. Conditions, expressions and actions](#3-conditions-expressions-and-actions)
- [4. Watches and data triggers](#4-watches-and-data-triggers)
- [5. Disassembly and the code view](#5-disassembly-and-the-code-view)
- [6. Navigation](#6-navigation)
- [7. Symbols, comments and source](#7-symbols-comments-and-source)
- [8. Memory views, search and patching](#8-memory-views-search-and-patching)
- [9. Memory intelligence](#9-memory-intelligence)
- [10. Beam, raster, contention and bus views](#10-beam-raster-contention-and-bus-views)
- [11. Device boards and peripheral monitoring](#11-device-boards-and-peripheral-monitoring)
- [12. Tracing and logs](#12-tracing-and-logs)
- [13. Time: rewind, step back, history, comparison](#13-time-rewind-step-back-history-comparison)
- [14. Several CPUs](#14-several-cpus)
- [15. Scripting, automation and remote protocols](#15-scripting-automation-and-remote-protocols)
- [16. Front-end architecture](#16-front-end-architecture)
- [17. Hot-path cost: keeping the debugger free when idle](#17-hot-path-cost-keeping-the-debugger-free-when-idle)
- [18. Scoreboard](#18-scoreboard)
- [19. Conclusions for unreal-ng](#19-conclusions-for-unreal-ng)
- [20. Pending inputs](#20-pending-inputs)

---

## 0. Method

Each category is judged on six axes:

| Axis | Question |
|---|---|
| **Power** | What can a skilled user do with it that they cannot do elsewhere? |
| **Convenience** | How few steps, how little knowledge, how little tuning? |
| **Wow** | Does it make someone who watches say "I want that"? |
| **Build cost (for us)** | Given what unreal-ng already has, how much work? S / M / L / XL |
| **Runtime cost** | What does it cost the emulation when on, and when off? |
| **Fit** | How well it rides on existing unreal-ng pieces (TTD, automation, DeviceState, BreakpointManager, memory access tracker). |

Each category ends with a **verdict**: the most powerful, the most convenient,
the biggest wow, and the **pick for unreal-ng** (what we build, and from whom
we take the idea). "Today" describes unreal-ng on master (`35e9d050`), from the
[inventory in use-cases.md](use-cases.md#6-symbols-and-source-what-is-specified-what-works)
and the code.

---

## 1. Execution control and stepping

| Approach | Who | Notes |
|---|---|---|
| Step over / out by **stack-pointer target**, not PC+3 | zxsp, jnext (SP depth + return-opcode set incl. RETN aliases) | robust against code that pops its return address or returns elsewhere |
| Step over via **temporary breakpoints, logic in the client** | DeZog | the backend only implements "run until one of two addresses" |
| **Step off** a breakpoint only on the paused→running edge | jnext | fixes "F5 on a breakpoint re-hits it" in one place |
| **Run to T-state / to pixel / to end of line** | zxsp (T-state), jnext (end of scanline), vAmiga (`eol`/`eof` beam traps), WinUAE (`fs vpos hpos`) | raster stepping |
| **HALT → ISR target** in step-over; branch preview with resolved `RET`/`JP (HL)` | Unreal Speccy | the next PC is shown before stepping |
| **Semantic steps**: step to the next screen write | 8BitAnalysers (F7 "Step Screen Write"; enum also has next IO write, bank switch, border change) | one key replaces a breakpoint setup |
| **Cycle delta on every stop** | WinUAE ("cycles since resume, (V,H)→(V,H)"), ZX-M8XXX ΔT stopwatch | timing without a calculator |

**Today:** step, over, out, run frames / T-states / scanlines / to pixel / to
interrupt on automation; Qt has the basic steps.

**Verdict.** Power: jnext/vAmiga raster stepping. Convenience: 8BitAnalysers
semantic steps. Wow: stepping a line and seeing the picture grow (TS-Labs,
jnext). **Pick:** SP-based over/out (zxsp, jnext), step-off edge (jnext),
semantic steps (8BitAnalysers), cycle delta on every stop (WinUAE). Build: S.

## 2. Breakpoints: kinds and keying

| Approach | Who | Notes |
|---|---|---|
| **Physical cell** (page + offset) as the default, CPU address as the variant | Xpeccy+ (Space vs Shift+Space), zxsp (flag bits in each cell), Mesen2 (memory type + offset), FCEUX (ROM offset), BizHawk (per-bank domains) | follows code into whatever window it is paged |
| CPU address only | ZXMAK2, Kozynax, jnext, MAME (PC only), Zero | wrong hits after paging |
| **Port breakpoints with ZX partial decode** | jnext (low-byte match), Zero ("any even port", masks), Xpeccy+ (port + mask) | how Spectrum ports really decode |
| **Screen-region write** (cell or pixel rectangle, normal or shadow screen) | ZX-M8XXX | "who draws here" in one trigger |
| **Media triggers**: tape block, disk sector, disk read | ZX-M8XXX | loaders and protections |
| **Register-value** breakpoints | Zero | "when HL reaches the screen" |
| **Beam position** as a breakpoint (scheduled, free per cycle) | vAmiga beam traps, WinUAE `fs` | raster breakpoints with zero per-instruction cost |
| **Mark-only** (plot on the event viewer, no stop), **forbid ranges** (mute breaks inside an ISR) | Mesen2, FCEUX | non-stopping probes; noise control |
| **Predictive** (break before the access, dummy CPU) | Mesen2 | registers still show the pre-access state |
| **Magic breakpoint / magic port** (`ED FF`, `DD 01`, printf port) | jnext (ZEsarUX/CSpect conventions), WinUAE (printf + cycle stopwatch port) | guest code can stop or print |
| **Source-comment** breakpoints (`ASSERTION`, `WPMEM`, `LOGPOINT`) | DeZog | debug intent lives in the `.asm` |

**Today:** memory R/W/X, I/O, keyboard; groups; bank-exact match mode;
page-specific silent breakpoints used by analyzers. No hit counts, no
conditions (#6), no screen-region, media or beam-position kinds.

**Verdict.** Power: Mesen2 (keying + variants). Convenience: Xpeccy+ physical
default with one key. Wow: ZX-M8XXX screen-region trigger. **Pick:** physical
keying by default everywhere (we already store banks); partial-decode port
match; screen-region, media and beam-position kinds; mark-only and forbid
ranges; source-comment triggers through SLD. Build: M on top of #6.

## 3. Conditions, expressions and actions

| Approach | Who | Notes |
|---|---|---|
| **One language everywhere** (conditions, actions, view addresses, printf, Lua); side-effect-free `@` vs side-effecting `!` memory reads; address-space prefixes; `temp0..9` | MAME | the most general |
| **Access variables**: `RD`/`WR`/`MDT`/`IN`/`OUT`/`VAL`, `DOS`, `FRAME`, `RAY(x,y)`, `HITS` | Xpeccy+ (and its ancestors Unreal Speccy / TS-Labs: `IN/OUT/VAL/FD/DOS`, `M()`, `PG0..3`) | Spectrum-shaped: ports, paging, beam |
| **Predicted write / read value**, bank of data and of PC | FCEUX (`W`, `R`, `T`, `K`) | filter by the value about to be written |
| Compile once: RPN with live pointers (Unreal), postfix cache (Mesen2), **JIT to .NET IL** (ZXMAK2 Adlers) | | per-instruction cost stays low |
| **Fail-open** on a broken condition, with live validation | Xpeccy+ | never silently ignore a breakpoint |
| **Edge-triggered** global conditions ("on change") | Xpeccy+ | fire once when the predicate becomes true |
| **Registerpoints**: break on an expression with no address | MAME, Xpeccy+ global conditions | "stop when the state is X" |
| **Actions**: log and continue, count, screen dump, run a command | MAME (`do`, `printf`, `g`), Xpeccy+ (log with full state, `.scr` dump) | breakpoints become probes |
| Chaining and hit count without a language | WinUAE (`H`, `N`) | minimal AND and "n-th hit" |

**Today:** no core expression engine (#6 designed); a ZEsarUX-protocol
condition evaluator exists for that protocol only; DeZog evaluates its own
conditions client-side after every stop (costly round trips).

**Verdict.** Power: MAME. Convenience and Spectrum fit: Xpeccy+. Speed:
Adlers JIT, Unreal RPN. **Pick:** the #6 language with MAME's generality and
Xpeccy's operand set (access variables, `RAY`, `HITS`, pages, ports, beam,
frame), FCEUX's predicted value, fail-open with live validation, edge-trigger,
actions (log / count / capture / script / mark). Evaluate in the core so
DeZog and MCP get conditions without round trips. Build: L (already #6).

## 4. Watches and data triggers

| Approach | Who | Notes |
|---|---|---|
| Watchpoints as **memory-system taps**, re-installed on map change | MAME | zero cost outside watched ranges |
| **Write-triggered** evaluation of memory-value conditions | ZXMAK2 Adlers | `(addr)==v` checked only after a write |
| **Channel-filtered** watch (by bus master), value match, must-change, log-only, freeze | WinUAE | CPU vs ULA vs DMA on our machines |
| **Last writer per hardware register** | WinUAE `ex` | last write to `#FE`, `#7FFD`, AY, with PC and T-state |
| Watch window with expressions | Xpeccy+ watcher (always CPU-mapped, not saved), Mesen2 watch | |
| **RAM search** with previous-value modes, change counts, undo | BizHawk; DeZog diff memory view and `-mdelta` string search | the cheat-finder workflow |

**Today:** memory breakpoints; automation `find`; no watch window in Qt; no
RAM search workflow; access counters exist.

**Verdict.** Power: MAME taps. Convenience: BizHawk RAM search. **Pick:**
page-level taps (we key by physical page anyway), write-triggered value
conditions, last-writer per port/register (cheap, high value), RAM search with
undo and diff views. Build: M.

## 5. Disassembly and the code view

| Approach | Who | Notes |
|---|---|---|
| **Code/data decided by execution** ("runtime mapping", CDL) | Xpeccy+, 8BitAnalysers, Mesen2, FCEUX | data tables stop disassembling as garbage |
| **Taken-branch arrows**, operand values on the PC row, F4 on `RET` follows `(SP)` | Xpeccy+ | reads like an IDE |
| **Timing column** (T-states per line), T-sum over a selection with taken / not-taken totals | ZXMAK2, ZX-M8XXX | demo coders count cycles all day |
| **Bank digits** on every line | Spectral | paging always visible |
| **In-place editing** with re-decode; inline assembler with live validity color | Xpeccy+, Zero, zxsp | patching without a dialog |
| ROM system-variable names as operands by default | Zero | `LD HL,(CH_ADD)` out of the box |
| Lazily formatted rows (hex/dec switch without re-decode) | Zero | |

**Today:** disassembler with labels and control-flow decoder; three Qt views
(text, column, listing); read-only in Qt.

**Verdict.** **Pick:** everything in this table; the two that matter most are
code/data from execution and in-place editing (the Qt debugger cannot patch
today). Build: M.

## 6. Navigation

| Approach | Who | Notes |
|---|---|---|
| History back (unlimited), pushed by typed address, follow operand, register click, labels list | Xpeccy+ | back only, no forward |
| Page-tagged PC history (`page:addr`, 32 entries), always recorded | TS-Labs Unreal | "how did I get here" for free |
| 5 mark slots (Ctrl/Alt+1..5), not persisted | Xpeccy+ | top row stored, not the cursor |
| Screen pixel → writer PC, double-click to jump; data address → cell outline on screen | 8BitAnalysers | navigation from the picture |

**Today:** Ctrl+G only (debugger-enhancements Phase 1 designed, open questions
recorded there).

**Verdict.** **Pick:** back *and* forward, page-aware entries, persisted marks,
page-tagged PC history always on, picture-to-code navigation. Build: S-M.

## 7. Symbols, comments and source

| Approach | Who | Notes |
|---|---|---|
| **Comments keyed by address + CRC32 of the instruction bytes** | MAME | follow the code across paging without knowing the banking |
| Labels keyed by physical memory; "out of scope" when the bank is unmapped | Mesen2, Unreal Speccy (host pointer), 8BitAnalysers | bank-correct |
| Many import formats | Mesen2 (ca65, WLA, RGBDS, bass, SDCC, ELF, …), Xpeccy+ (only `BB:AAAA NAME`), Unreal (user.l, XAS/ALASM from memory, live reload) | |
| **Project container** (flags, label sets, comments) | Xpeccy+ `.xmap`, 8BitAnalysers `Analysis.json` + state | analysis survives restarts |
| Export to sjasmplus / SkoolKit / Ghidra | ZX-M8XXX, 8BitAnalysers | |
| SLD source-level | DeZog (client side) | |
| Command-line label and breakpoint files | TS-Labs (`-b`, `-l`) | build-and-debug loop |

**Today:** see [use-cases §6](use-cases.md#6-symbols-and-source-what-is-specified-what-works):
sjasmplus `.sym` silently dropped, SLD absent, lookup ignores banks, `.lst`
single file.

**Verdict.** Power: Mesen2 (formats + physical keying). Cleverest: MAME's CRC
binding. **Pick:** physical keying with scope; MAME CRC binding for comments on
unknown code; SLD first, then the long tail of formats with a test corpus; a
project container; exports. Build: M-L.

## 8. Memory views, search and patching

| Approach | Who | Notes |
|---|---|---|
| **Memory domains / physical-page sources** shared by all views | BizHawk, zxsp, Mesen2 | inspect unmapped pages |
| **Save-state items as memory sources** (every device's state browsable) | MAME | device RAM for free |
| Disk editor inside the memory pane (track or sectors, CRC fix-up) | Unreal Speccy; Xpeccy+ colored track dump | |
| Encodings in the text column (WIN1251 / CP866 / KOI8-R) | Xpeccy+ | Russian software |
| **Edit mode sandbox**: snapshot on enter, restore on exit | 8BitAnalysers | patches are experiments by default |
| Masked byte search, fill with Mask/Put/Or/And/Xor | Xpeccy+ (with bugs) | |

**Today:** automation reads/writes pages, banks and devices; Qt memory views
are read-only.

**Verdict.** **Pick:** physical-page and device-memory sources in every view;
edit sandbox on top of TTD (our checkpoint is the sandbox); encodings; disk
editor from the media manager (#58). Build: M.

## 9. Memory intelligence

| Approach | Who | Notes |
|---|---|---|
| **Per-byte provenance**: up to 32 reader and 32 writer PCs, last writer, per-instruction read/write sets, persisted | 8BitAnalysers | the "who" answer |
| Provenance by range and call site, port-read provenance by (pc, port), indirect-jump targets, runtime call graph, SMC ranges | ZX-M8XXX | exportable to Ghidra / SkoolKit |
| **Access stamps** (time of last access), not only counts | Mesen2 | fading heat, "written last frame", uninitialized reads |
| **CDL** as a primitive driving coverage, disassembly, hex colors, trace filter, "break on first execution of new code" | FCEUX, Mesen2, BizHawk (per bank, OR-merge) | one byte of flags per byte |
| **Behavior profiler** that auto-names routines from what they do; hotspot classes | ZX-M8XXX | from coverage to names |
| **Code-path set diffs** (baseline vs event) | ZX-M8XXX | isolate an event handler without reading code |
| Heat maps: categorical R/W/X per physical byte (Xpeccy+), decay modes (zxsp), per bus master (WinUAE), frame-decay everywhere (8BitAnalysers) | | |
| **Graphics finders**: 1-bit bitmap viewers with manual width/height/interleave (Xpeccy+ sprite scanner, zxsp, Adlers ripper); screen-cell search in all banks (8BitAnalysers) | | all manual today |
| Function parameter / return capture per call | 8BitAnalysers | light dynamic profiling |
| Validators: SMC with writer PC, writes to read-only registers | WinUAE | |

Nobody infers sprite layout automatically, nobody recognizes engines,
players or compressors by signature, and nobody keeps a shared knowledge base.
**This is open ground** ([use-cases §4.5](use-cases.md#45-memory-intelligence)).

**Today:** memory access counters (counts per address), call trace, coverage,
ROM signature catalog; no provenance PCs, no CDL feeding the disassembly.

**Verdict.** Power: 8BitAnalysers + ZX-M8XXX. Wow: live decaying heat maps
(zxsp, WinUAE). **Pick:** provenance + access stamps + CDL as one per-byte
record on physical pages; then automatic graphics layout from recorded
read/write strides, and the signature and knowledge bases — the part nobody
has. Build: L (record), XL (inference and bases).

## 10. Beam, raster, contention and bus views

| Approach | Who | Notes |
|---|---|---|
| **Screen drawn up to the beam** on every step, beam pixel marked | TS-Labs Unreal, Unreal Speccy `Alt+F9`, ZX-M8XXX (previous frame greyed, crosshair, border-only variant), jnext (unreached rows darkened) | the frame being built |
| **Event viewer**: accesses plotted at (line, cycle) over the frame, previous-frame ghosting, click-through | Mesen2 (NES border rebuilt from color-change events) | ports and border on the picture |
| **Scanline-tagged event strip** beside the screen | 8BitAnalysers | border, beeper, AY, paging, keyboard |
| **Per-line bus usage bars** by client (CPU, video, tiles, sprites, DMA) | TS-Labs Unreal (`Border=5`) | TSConf arbitration |
| **Per-cycle bus-owner array that is also the arbitration table**, painted at HSYNC with blend modes and data-dependent shading | vAmiga | the view cannot drift from the emulation |
| **Per-slot recorder** + pixel overlay + side cycle diagram + 8-row text decode of a line with the CPU mnemonic at instruction starts | WinUAE | the deepest |
| **Logic-analyzer timing diagram** of one line (owner, address, data, probes) | vAmiga Bus tab | a scope for the bus |
| Viewer snapshot at a chosen (line, cycle) | Mesen2, FCEUX | mid-frame state |
| Contention parameters shown (waitmap, offset) | zxsp | numbers only |
| Beam in conditions (`RAY`, `beamy`) | Xpeccy+, MAME | raster breakpoints without a view |
| Beam timestamp on every log line | WinUAE, vAmiga | logs correlate with the raster |

No Spectrum emulator has a **contention view**. The Amiga tools show how:
record the owner of every T-state as a side effect of the contention logic
itself (vAmiga), then paint it.

**Today:** Qt ULA beam widget, border timing widget, `DescribeBeam` on
automation, run to pixel/scanline, port trace with T-states, video debug
translation designed (#42).

**Verdict.** Power: WinUAE. Cleanest design: vAmiga. Spectrum-specific wow:
TS-Labs bus bars and screen-up-to-beam. **Pick:** a per-T-state owner/event
record filled by the ULA contention code (vAmiga principle), drawn as (a) the
screen up to the beam, (b) an event viewer over the frame (Mesen2), (c) a
contention and bus strip per line (TS-Labs, WinUAE), (d) a one-line logic
analyzer (vAmiga); beam traps as scheduled events; beam stamps on every log.
Build: L. **This is the demo coder's flagship.**

## 11. Device boards and peripheral monitoring

| Approach | Who | Notes |
|---|---|---|
| **Decoded chipset register panel** (bit ranges, symbolic values, raw byte; programmed vs in-flight DMA) | TS-Labs Unreal | the template for a device board |
| **One inspector per hardware item**, switchable in any tool window | zxsp | |
| **Attribute-reflected hardware properties** (device fields declared once, UI generated) | Kozynax | no per-device UI code |
| Save-state items as generic memory sources | MAME | |
| Sound: all AY registers editable, envelope plot, per-channel mute, FM operators; **detachable, live at frame rate** | Xpeccy+ | |
| Composed-value register editor with real side effects | jnext (NextREG) | |
| Capability-based services, unsupported features greyed | BizHawk | one UI for many machines |

Nobody shows **every device live at once**. Most refresh only while paused.

**Today:** `DeviceState` reports for AY/TSFM/FDC/uPD765/GS/MoonSound/NeoGS/
screen on every automation surface; Qt has FDC status and floppy widgets; the
debugger model defines device boards from published descriptions.

**Verdict.** Power: TS-Labs panel. Design: Kozynax attributes, debugger-model
board descriptions. **Pick:** boards generated from each device's published
description (we already have DeviceState), live at frame rate through pushed
events, arranged as the peripheral wall. Build: M. **This is the spectator
and first-impression flagship.**

## 12. Tracing and logs

| Approach | Who | Notes |
|---|---|---|
| **Binary records, lazy formatting, writer thread** | FCEUX, Mesen2 | millions of lines in real time |
| Trace with **memory writes old/new** → scrub the screen back | ZX-M8XXX | trace doubles as a short rewind |
| Condition re-checked on each memory op of the instruction; multi-CPU interleaving by global row id | Mesen2 | |
| **Loop condensation** (`LDIR`, `DJNZ`, key-scan loops) | MAME, ZX-M8XXX (repeated block instructions collapsed) | readable traces |
| **Trace format compatible with another emulator** for differential debugging | jnext (CSpect plugin) | |
| **Logging channels** with frame/line/cycle/PC prefix | vAmiga (~90 channels), WinUAE | |
| Breakpoint log lines with full machine state | Xpeccy+ | |
| RZX-aware trace (expected vs actual fetch / IN counts) | Zero | replay desync visible |

**Today:** call trace, opcode profiler, port trace, TTD journal; no
general instruction trace view.

**Pick:** binary trace ring with lazy formatting, writes old/new, loop
condensation, beam stamps, export compatible with at least one other emulator
(differential runs). Build: M.

## 13. Time: rewind, step back, history, comparison

| Approach | Who | Notes |
|---|---|---|
| **Full TTD**: record, seek, reverse step/continue, find-last r/w/x, coverage, bookmarks, frame-accurate render, input journal | **unreal-ng** | ahead of every surveyed tool |
| Rewind keyframes + dense per-instruction snapshots near the target; step back by line and frame | Mesen2 | |
| Frame snapshots + deterministic replay to a cycle; step back from trace cycle stamps | jnext | |
| Tiered greenzone + **branches with screenshots** | BizHawk TAStudio | memory-bounded long sessions; branches |
| **Differential run with first-divergence report** (self-checked) | ZX-M8XXX | the answer to "why does this build break" |
| Per-frame CRC of the opcode stream + RAM checksum as a desync fingerprint | FCEUX netplay | determinism tests |
| Frame Trace ring with screenshots and restore | 8BitAnalysers | |
| Step back from the trace ring (registers + old value) | FCEUX | cheap single-step back |

**Verdict.** We lead. **Pick** what we lack: branches (BizHawk), first-divergence
diff (ZX-M8XXX), a desync fingerprint for determinism tests (FCEUX), keeping
history across code reloads ([use-cases §5](use-cases.md#5-the-code-change-loop-in-detail)).
Build: M-L.

## 14. Several CPUs

| Approach | Who | Notes |
|---|---|---|
| Multi-CPU trace interleaving; one debugger for all CPUs | Mesen2, MAME | |
| Copper as its own disassembly source | MAME (Next), vAmiga | a coprocessor is a CPU to the debugger |
| **Two Z80s, one clock**, card mailbox | designed for unreal-ng (debugger model, #45) | nobody has it for GS/NeoGS |

**Pick:** the debugger-model rules as designed. Build: L (#45).

## 15. Scripting, automation and remote protocols

| Approach | Who | Notes |
|---|---|---|
| **CLI + WebAPI + MCP + Lua + Python + GDB + DeZog + ZEsarUX**, TTD on all | **unreal-ng** | the widest surface of any surveyed tool |
| Lua with memory callbacks that **rewrite values**, `getState`, overlays, `--testrunner` exit codes | Mesen2; BizHawk (`comm.*` socket/HTTP/WebSocket/MMF) | |
| **Self-describing API** (`brief`, `capabilities`, `require`) for LLM drivers, test-enforced manifest | ZX-M8XXX | |
| Everything is an option, every command is RPC (JSON-RPC), option metadata generates commands | vAmiga | parity by construction |
| Headless batch via command queue | Zero | |
| Custom driver commands | MAME | machines add their own debugger commands |
| DZRP protocol limits: conditions client-side, machine types 16K/48K/128K/Next only, rewind registers only | DeZog | our DZRP history extensions are not used by upstream DeZog |

**Verdict.** We lead on breadth. **Pick:** capability self-description
(ZX-M8XXX), generated commands from metadata (vAmiga) to keep parity cheap,
value-rewriting callbacks (Mesen2, BizHawk), core-side conditions for DeZog.

## 16. Front-end architecture

| Approach | Who | Notes |
|---|---|---|
| **One model, several renderers** (WinForms, SDL cell UI in the ZX font, terminal) | Kozynax | GUI and TUI in lockstep |
| Debuggers as **hot-pluggable machine devices** (several at once, incl. a network stub) | ZXMAK2 | |
| One core debugger, four front ends (Qt, Win, Cocoa, ImGui) | MAME | |
| Capability services, one UI for many cores | BizHawk | |
| Text monitor in the Spectrum's own look | Unreal Speccy, TS-Labs (own window, 157 columns) | beloved by the community |
| Browser | ZX-M8XXX (the whole emulator), DeZog (VS Code webviews) | |

**Today:** a Qt debugger wired directly to the core (to be replaced); the
debugger model (one protocol, many skins) is drafted; TUI POC exists (#49).

**Verdict:** the debugger model's direction matches the best practice
(Kozynax, MAME). Options are weighed in [proposition.md](proposition.md).

## 17. Hot-path cost: keeping the debugger free when idle

| Approach | Who |
|---|---|
| **Two cores** (fast / debug) switched by "any breakpoint armed?" | Unreal Speccy `isbrk()`; unreal-ng already selects fast / debug memory interfaces |
| One cached `armed` flag, opt-in persistent breakpoints | jnext |
| **Flag bits in each memory cell** or per-byte flag maps | zxsp, Xpeccy+ |
| Taps installed only on watched ranges | MAME, ZXMAK2 (64K delegate tables) |
| Scheduled beam traps | vAmiga |
| Reference-counted expensive recorders (only while a view is open) | vAmiga |
| Tiered region cache for script hooks | FCEUX |

**Pick:** keep our fast/debug interface split; add per-page flag bytes for
breakpoints, CDL and heat in one place; scheduled beam traps; recorders
reference-counted by open views. Every new recorder must have a "zero cost when
off" benchmark (project rule: naive first, then measure).

---

## 18. Scoreboard

Strength per category: **3** best in class · **2** good · **1** basic ·
**–** absent. Based on the survey documents; unreal-ng column is master today.

| Category | unreal-ng | Xpeccy+ | Unreal | TS-Labs | ZXMAK2 | zxsp | ZX-M8XXX | 8BitAn. | jnext | MAME | Mesen2 | FCEUX | BizHawk | WinUAE | vAmiga | DeZog |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 Stepping | 2 | 2 | 2 | 2 | 1 | 3 | 2 | 3 | 3 | 2 | 3 | 2 | 1 | 2 | 3 | 2 |
| 2 Breakpoint kinds / keying | 2 | 3 | 1 | 2 | 1 | 2 | 3 | 1 | 2 | 1 | 3 | 2 | 1 | 2 | 2 | 2 |
| 3 Conditions / actions | 1 | 3 | 2 | 3 | 2 | – | 1 | 1 | – | 3 | 2 | 2 | 1 | 2 | 1 | 2 |
| 4 Watches / data triggers | 1 | 1 | 1 | 2 | 2 | 1 | 2 | 1 | 1 | 3 | 2 | 1 | 3 | 3 | 1 | 2 |
| 5 Code view | 2 | 3 | 3 | 2 | 2 | 2 | 3 | 3 | 1 | 2 | 3 | 2 | 1 | 1 | 1 | 2 |
| 6 Navigation | 1 | 2 | 1 | 2 | 1 | 1 | 1 | 2 | 1 | 1 | 2 | 1 | 1 | 1 | 1 | 1 |
| 7 Symbols / source | 1 | 2 | 2 | 2 | 2 | – | 2 | 2 | – | 1 | 3 | 2 | 1 | 1 | 1 | 3 |
| 8 Memory / patching | 2 | 2 | 3 | 2 | 1 | 2 | 2 | 2 | 1 | 3 | 2 | 2 | 3 | 1 | 1 | 2 |
| 9 Memory intelligence | 1 | 2 | 2 | 1 | 1 | 2 | 3 | 3 | 1 | 2 | 3 | 3 | 2 | 2 | 1 | 1 |
| 10 Beam / raster / bus | 2 | 2 | 2 | 3 | 1 | 1 | 2 | 2 | 3 | 1 | 3 | 2 | – | 3 | 3 | – |
| 11 Device boards | 2* | 2 | 2 | 3 | 1 | 2 | 1 | 2 | 2 | 2 | 2 | 2 | 1 | 1 | 2 | 1 |
| 12 Tracing / logs | 1 | 2 | 1 | 1 | 2 | – | 3 | 1 | 2 | 3 | 3 | 3 | 2 | 3 | 3 | 1 |
| 13 Time | **3** | 1 | – | – | – | – | 2 | 1 | 2 | 1 | 2 | 1 | 3 | – | – | 1 |
| 14 Several CPUs | 1 | – | – | – | – | – | – | – | 1 | 3 | 3 | – | 2 | 1 | 2 | – |
| 15 Automation / remote | **3** | 1 | – | 1 | 1 | – | 2 | 2 | 1 | 2 | 2 | 2 | 2 | 1 | 2 | 2 |
| 16 Front-end architecture | 1 | 1 | 1 | 1 | 2 | 2 | 2 | 2 | 1 | 3 | 2 | 1 | 2 | 1 | 2 | 2 |

\* device state is rich on automation surfaces, thin in the GUI.

## 19. Conclusions for unreal-ng

1. **We already lead on time and automation.** TTD and the multi-surface API
   are unique. Every new feature should be born on the protocol and reachable
   from TTD (e.g. provenance answered from history, not only live).
2. **We trail on the interactive, visual layer.** Code view, navigation,
   memory editing, device boards and beam views exist only on automation or
   not at all. The GUI is where people judge a debugger.
3. **The three flagships** that would put us ahead of everyone:
   - the **peripheral wall** (every device board live at once) — first
     impression, spectators, hardware people;
   - the **beam / contention / bus view** built on a per-T-state owner record
     (vAmiga principle, TS-Labs and WinUAE looks) — demo coders; nobody has a
     Spectrum contention view;
   - **memory intelligence** with automatic graphics layout and signature
     knowledge bases — reverse engineers and game developers; nobody has it.
4. **The two-CPU card debugger** is a unique capability (designed); valuable
   for musicians and firmware authors, and a showcase.
5. **Foundations first:** the #6 condition language, physical-page keying for
   everything, a per-byte record (flags, stamps, provenance) and pushed events.
   All three flagships ride on them.

## 20. Pending inputs

- Spectaculator and ZXSpin surveys (from existing reverse-engineering
  material; open internals go to an IDA Pro question list).
The unreal-ng self-survey has landed:
[unreal-ng-debugger.md](../2026-09-28-emulator-debugger-survey/unreal-ng-debugger.md)
(about 110 capabilities, each with status and surfaces). It confirms the
unreal-ng column of the scoreboard and adds facts used by the roadmap: the
WebSocket server publishes no events today (so pushed events need #23), the
Qt debugger is read-only and run-to-cursor is disabled, and several stubs
exist in code (keyboard breakpoints cannot be added, `DebugManager`
breakpoint methods are empty, `BorderTimingWidget` measures nothing,
`DocumentDisasm` is empty, `ROMPrintDetector` is not registered).

Spectaculator and ZXSpin are merged into §1-§18 when they land; the
scoreboard gains their columns.
