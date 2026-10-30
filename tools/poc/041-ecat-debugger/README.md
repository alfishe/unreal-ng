# 041 — ecat-debugger

**Result (2026-10-29): go.** [conclusions.md](conclusions.md), [proposal.md](proposal.md), [analysis.md](analysis.md),
measurements: [port](measurements-2026-10-29-port.md), [cost](measurements-2026-10-29-cost.md),
[follow-ups](measurements-2026-10-29-followups.md). Window:
[main Z80](window-pentagon.png), [GS Z80](window-pentagon-gs.png).

Build: configure with `-DBUILD_ECAT_DEBUGGER_POC=ON`, then `tools/build/build.sh ecat-debugger-poc`; run
`ecat-debugger-poc [--model 48K] [--card neogs|multisound] [--no-gs] [file]`, `--self-check`, `--list-memory`,
`--measure 10`, `--screenshot out.png`, or `--connect 127.0.0.1:8090` (a running unreal-qt through its WebAPI) from the
build's `bin/`.

Can the debugger of eCat3 (Mikhail Revzin's emulator of Soviet machines, <https://github.com/Ptr314/ecat3>, GPL-3.0)
be the Qt debugger of unreal-ng? The owner showed its window on 2026-10-29 (УК-НЦ, three processors) and asked whether
unreal-ng can have one like it. Feature: [`docs/inprogress/2026-09-28-debugger-model`](../../../docs/inprogress/2026-09-28-debugger-model/)
(the "modern GUI skin", `gui-main-debugger.md`); POC 035 picks the GUI technology, this POC tries one finished,
working design on our core.

Charter written 2026-10-29, before any code. Source: ecat3 branch `dev` at `27a98108` (2026-10-27), the newest of the
repository; master `69a5817` has the same debugger minus the last 49 lines.

## The window being ported

`src/dialogs/debugger/` of ecat3 (about 3 400 lines) plus the Qt-free `src/emulator/disasm_buffer.*` (backward
disassembly that never rewrites a line already shown):

- one window for the machine, a toolbar (run, pause, run without breakpoints, step, step over, step out, run to
  cursor, go to address, breakpoint add / remove / clear), a state label;
- a tab per processor: `DisasmView` (jump arrows in lanes by length, PC and breakpoint marks, address, bytes,
  instruction coloured by flow: jump / call / return; edit an instruction's bytes in place) | `RegistersView`
  (value and binary, edit in place, changed values red) + `FlagsView`;
- below: `MemoryTabs` (one dump per address space: each processor's view, then memories; bytes or words, hex or
  octal, search, load / save, edit in place, changes since the last stop red) and side tabs: Stack (SP in the
  middle), Breakpoints, History (where PC has been), Devices (every device's fields).

## Goals

1. Does the ecat window run over unreal-ng's core with its views kept as they are, everything machine-specific behind
   an adapter (a shim of the parts of ecat's `Emulator`, `CPU`, `DisAsm`, `AddressableDevice` the views call)?
   How much of the ported code had to change (lines of diff against upstream), and where?
2. Which features work on the main Z80 of a Pentagon (and 48K / 128K): listing following PC and live memory, jump
   arrows, the stop / step / step over / step out / run to cursor / breakpoints, register and flag editing, memory
   editing, stack, history, dumps of the CPU view and of the RAM / ROM pages?
3. Does the second processor of the machine (the General Sound card's Z80) appear as a second processor tab, as the
   УК-НЦ's three do in ecat, with its own listing, registers and dumps?
4. What does unreal-ng's core lack for it (missing calls, thread-safety gaps, side effects of reads), listed as
   concrete items the debugger-model feature can take over?
5. What it costs while the machine runs: the window's 200 ms follow timer on the GUI thread and its effect on the
   emulation (frames per second with and without the window).

## Requirements

- Qt 6 Widgets on macOS (the owner's machine); the code must stay portable (no platform calls).
- In-process: the POC app links `unrealng::core`, creates one emulator and opens the window over it, the way unreal-qt
  owns its emulator. No WebAPI round trips in the hot path.
- Models: Pentagon (the main target, with General Sound enabled), 48K and 128K as checks.
- Reads for display are side-effect free (no contention, no port strobes, no memory-access hooks or breakpoints fired
  by the debugger's own reads); writes go through the core's debug write path.
- Every change of machine state the window makes (registers, memory, breakpoints, steps) runs where the core allows
  it: on the emulation thread or while it is paused, never racing the running CPU.

## Constraints

- POC code lives in `tools/poc/041-ecat-debugger/`; it is not linked into `core/` or the apps. No changes to `core/`
  or `unreal-qt/` inside this POC: what the core lacks is recorded (goal 4), worked around in the adapter where
  possible, and left for the feature otherwise.
- The ported ecat files keep their GPL-3.0 headers and authorship, and each records the upstream commit it came from.
  Edits to them are minimal and marked, so the diff against upstream answers goal 1.
- Things ecat has that unreal-ng's model does not are dropped, not emulated: octal notation stays available but the
  default base is hex; the PDP-11 / 8086 special cases go; ecat's script recording (`record_command`) is a no-op.
- Budget: one working session for the port and the checks; the cost measurement (goal 5) is a short A/B, not a
  benchmark campaign.

## Acceptance criteria / expected results

- **Go** (the design can be adopted as a Qt skin): the window runs over Pentagon with General Sound; every item of
  goal 2 works on the main Z80; the GS Z80 has its own tab with listing, registers and dumps (stepping it may be
  limited to "step the machine", as in ecat, where a step is a step in time); the views' diff against upstream is
  under 15 % of their lines; the follow timer costs under 2 % of a core of the GUI thread and the emulation keeps its
  50 frames/s.
- **Partial**: the window runs on the main Z80, but the GS processor or a stepping mode needs core work; the list of
  that work is the result.
- **No-go**: the views cannot be kept (more than 40 % rewritten) or the core cannot give side-effect-free reads and
  safe stepping without changes; then only the design (layout, arrows, colours) is taken over.
- Expected before measuring: go for the main Z80; the GS tab partial (its CPU is driven from the card's own loop, so
  per-instruction stepping of it alone is unlikely); diff 5-10 %; timer cost well under 1 %.
