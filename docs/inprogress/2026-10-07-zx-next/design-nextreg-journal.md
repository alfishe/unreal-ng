# ZX Spectrum Next: the NextREG write journal

**Date:** 2026-10-09 · part of [README.md](README.md) · surfaces: [design-automation.md](design-automation.md) · pattern: the Sprinter PLD journal (`DeviceState::SprinterJournal`)

A bounded, switchable log of every write to the Next's register file - who wrote which register, with which value, when (frame,
T-state, PC) and through which door (the `NEXTREG` instruction, port `#253B`, the copper) - on every automation plane.

## 1. What NEXTREG is

The Next has a **register file of 256 byte registers** ("NextREG", `NR #00-#FF`) that configures everything the classic Spectrum
did with a handful of ports: the machine type and timing (`#03`), CPU speed (`#07`), the 8K MMU slots (`#50-#57`), the video layers,
palettes, clip windows, sprites, the copper (`#60-#64`), the tilemap, interrupts (`#C0-#CE`), the DivMMC entry points (`#B8-#BB`),
the reset register (`#02`), audio routing, DMA interrupts... [research-nextreg-and-ports.md](research-nextreg-and-ports.md) lists them.

Software reaches the file in four ways, and they are **different bus events**:

| Door | What the program does | Bus cycles | Select latch |
|:--|:--|:--|:--|
| select + data ports | `OUT (#243B),reg` then `OUT (#253B),value` (or `IN` to read) | two port cycles | set by `#243B` |
| `NEXTREG n,v` / `NEXTREG n,A` (`ED 91` / `ED 92`) | one instruction | **none** - the CPU hands the pair to the board | untouched |
| the copper | `MOVE reg,value` in the copper list, 28 MHz | none (the copper is a bus master of its own) | untouched |
| I/O traps, NMI machinery | the board writes by itself | none | - |

NextZXOS, the firmware and nearly every Next program use `NEXTREG` for writes - it is two bytes shorter and does not disturb the
select latch.

## 2. Why capture it

Every tool of the emulator that answers "who did this?" is built on **port cycles**: the port trace, port breakpoints and the TTD I/O
journal. A `NEXTREG` write or a copper `MOVE` produces none, so the question "who set NR #07 to 28 MHz?" or "what reset the machine?"
cannot be answered at all - the register changes and nothing in the tools says why.

The case that made this a requirement (2026-10-09, NextZXOS Browser starting `!Copper.snx`): the screen stays black, the CPU runs
a tight loop of ~480 iterations per frame, the port trace shows `OUT #243B,2` and `OUT #E3,1` and nothing else. The loop is a chain of
**soft resets requested by `NEXTREG 2,1`** - invisible. Three separate emulation gaps were behind it (RETN not leaving the DivMMC
automap, NR `#02` reading "power on" always, NR `#02` bit 2 not generating the DRIVE NMI) and each was found by guesswork and
disassembly of the DivMMC ROM. With the journal the first screen of output would have shown `reg 02 <- 01 (nextreg) pc 2038`
repeating, and the register that decides the loop (`02`, read back by the same code).

Other uses: copper effects (which register at which line - the order of `MOVE`s is the picture), bring-up of new peripherals (what the
firmware programs before it works), regression evidence in bug reports, test oracles ("the program must write NR `#15` exactly
once"), TTD-style "find the last write to NR `#50`".

## 3. Requirements

| # | Requirement |
|:--|:--|
| R1 | Every write is an event: `seq, frame, t (T in frame), pc, source, reg, name, value, previous`. Reads are out of scope (the board already counts reads per register) |
| R2 | Off by default; when off the cost on the hot path is one predictable branch; on, no allocation per event (ring buffer) |
| R3 | Bounded: ring of 65 536 events, oldest overwritten, `evicted` counted |
| R4 | Query: `since` (seq), `from` / `to` frame, `regs` list (`07,02`), `sources` list, `limit` (newest N) - the same shape as the Sprinter journal |
| R5 | Control: on / off / clear, replies with the journal's state (enabled, size, capacity, evicted, seq) |
| R6 | **Every automation plane**: WebAPI + OpenAPI (MCP's `invoke_api` / `search_api` reach it from the schema), CLI, MCP `inspect_state`, Lua, Python. Same function in `DeviceState`, thin adapters |
| R7 | Docs: control-interface docs of each plane, `.recipe/machines/next.md`, tools README |
| R8 | The write path stays the single `NextBoard::Write` - no register can be written without being journaled |

## 4. Design

### 4.1 Core

`core/src/emulator/io/z80n/nextregjournal.{h,cpp}` - `NextRegJournal`: a fixed ring of `NextRegWriteEvent` (24 bytes: `uint64 seq`,
`uint32 frame`, `uint32 t`, `uint16 pc`, `uint8 source`, `uint8 reg`, `uint8 value`, `uint8 previous`), `Record(...)`, `Query(...)`,
`SetEnabled / Clear / Stats`. Owned by `NextBoard`.

`NextBoard::Write(reg, value)` is the one choke point (select + data port, `NEXTREG`, copper and the board's own writes all end
there). The **source** is known by the caller: the `INextRegHost::WriteNextReg` entry is the instruction, `WriteSelected` is
the data port, the copper's writer sets `_copperWrite`, anything else is `internal` (the NEX loader's register set-up, reset). Frame, T and PC
come from a clock callback the port decoder installs (`frame_counter`, `Z80::t`, the instruction's PC) - the board does not know the CPU.

Enabled state is a plain `bool`; the check sits at the top of `Write`. Also journaled: the *previous* value, so a reader sees what
changed without a second query.

### 4.2 Report

`DeviceState::NextRegJournal(context, NextRegJournalQuery)` and `NextRegJournalControl(context, enable, clear)` (declared in
`devicestate.h`, defined next to `NextRegs` in `portdecoder_next_state.cpp`). `StateNode` rendering gives JSON and text for free. Each
event carries the register's name from the table (`CPU Speed`, `Reset`, ...) and the value decoded for the few registers whose bits
matter most (`#02` reset bits, `#07` speed, `#03` machine type) so a reader needs no table.

### 4.3 Surfaces (R6)

| Plane | Read | Control |
|:--|:--|:--|
| WebAPI | `GET /api/v1/emulator/{id}/state/next/reg-journal?regs=&sources=&since=&from=&to=&limit=` | `POST /api/v1/emulator/{id}/next/reg-journal {"enabled":true,"clear":false,"capacity":N}` |
| OpenAPI | both paths in `openapi_state.inc` with parameters and the response schema | |
| MCP | `inspect_state` aspect `next_reg_journal` (+ params `nr_journal_regs`, `nr_journal_sources`, `nr_journal_since`, `nr_journal_limit`); control via `invoke_api` (the POST above) | |
| CLI | `next regjournal [regs=07,02] [sources=copper] [since=N] [limit=N] [--json]`; `next regjournal on|off|clear` | |
| Lua | `next_reg_journal({regs="07,02", sources="nextreg", since=N, limit=N})` | `next_reg_journal_control({enabled=true, clear=true})` |
| Python | `emu.next_reg_journal(regs=..., sources=..., since=..., limit=...)` | `emu.next_reg_journal_control(enabled=..., clear=...)` |
| Qt | later (N12): a panel beside the NextREG table | |

### 4.4 TTD

The TTD I/O write journal records port cycles only, so a replay of a recording knows nothing of `NEXTREG` writes either. Two ways, to
decide when TTD v2 is in the Next (see [design-ttd.md](design-ttd.md)): (a) the board posts every NextREG write to the TTD sink as a
pseudo I/O write on a reserved port pair so the existing queries (`find_last`, port events) answer for it; (b) the NextREG journal
is part of the TTD checkpoint state. This design ships the **live** journal only and keeps `source=ttd` reserved in the query so the
surface does not change later.

### 4.5 Cost

Enabled, one 24-byte store per write. NextREG writes are rare (hundreds per frame at most; the copper may write every few lines).
Disabled: one branch. No change to the instruction hot path: the engine already calls `WriteNextReg` out of line.

## 5. Tests (written first)

| Test | What |
|:--|:--|
| `NextRegJournal_Test` ring | capacity, overwrite, `evicted`, `seq` monotonic, `previous` |
| source tags | `NEXTREG` instruction vs `OUT #253B` vs copper `MOVE` each recorded with its own source (a real program in a core test) |
| disabled costs nothing | no events while off, enabling mid-run starts at the next write |
| report | `DeviceState::NextRegJournal` filters by regs / sources / since / frame range / limit; text and JSON |
| surfaces | WebAPI handler test (the Sprinter journal test is the model), Lua and Python smoke through the existing embedded interpreters, CLI command through the processor |
| regression use | the reset-loop case: `NEXTREG 2,1` repeated by a small program shows up as repeated `Reset` events with `source=nextreg` |

## 6. Documents to update

- `docs/emulator/design/control-interfaces/{webapi,lua,python,cli,mcp}-interface.md` - one section each
- `.recipe/machines/next.md` (new): the recipe "find who wrote a register" with real output; the Browser-driving and real-board
  recipes
- `tools/machines/next/*/README.md` for the scripts that use it
- `design-automation.md` report table (this report added), `TODO.md`, `phases.md`
- `docs/emulator/environment-variables.md` only if an env switch is added (none planned)

## 7. Open questions

1. Reads: a separate `next_reg_reads` counter exists; a read journal (who polls NR `#02`) is the other half of the reset-loop story. Cheap to add as a `kind` once the write journal is in; not in this change.
2. TTD (section 4.4): option (a) or (b).
3. Should the journal be on by default in the app (not in tests)? Proposal: off; the recipe says how to switch it on first.
