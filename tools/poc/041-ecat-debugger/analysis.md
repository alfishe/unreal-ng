# Analysis

Numbers: [measurements-2026-10-29-port.md](measurements-2026-10-29-port.md),
[measurements-2026-10-29-cost.md](measurements-2026-10-29-cost.md),
[measurements-2026-10-29-followups.md](measurements-2026-10-29-followups.md).

## The views are independent of the machine

ecat's debugger reaches its machine only through a narrow API: `CPU` (PC, registers, flags, memory through the
processor, breakpoints, history, a disassembler), `AddressableDevice` / `MemoryMapper` (dumps, page owners),
`ComputerDevice` (devices' fields) and `Emulator` (run control, the stop reason, a change counter). A shim of that
API in about 280 lines and an adapter of about 800 lines over unreal-ng's core were enough: the views compiled and
worked without a single edit. This is better than the 5-10 % expected. The adapter maps the API onto the core's
existing calls: `Pause` / `Resume` with a GUI requester, `RunSingleCPUCycle`, `StepOver`, `StepOut`, breakpoints
through `BreakpointManager` (run to cursor as a breakpoint with its own owner), tool edits (`EditMemoryFromTool`,
`SetRegisterFromTool`), coherent moments for the GS card, `MessageCenter` events for the change counter.

What did change is not porting work, it is a rendering defect of upstream: on every 200 ms tick the window
repainted its listing and registers whole and, through a layout request, both tab widgets and the window
background. 22 Mpx/s of painting for a menu where almost nothing moves. The POC's first fix (7.5 %) became the
upstream pull request [Ptr314/ecat3#84](https://github.com/Ptr314/ecat3/pull/84); the views are now that branch
(7.0 % against upstream `dev`) plus one fix of its `PaintedRows`: the jump arrows' column left every listing row
half painted and so repainted the whole listing on the next tick. With it the debugger paints 0.25 Mpx/s while
running (90x less than upstream; the stack view, centred on a moving SP, is most of it), and nothing while paused.

## Cost against the criterion

The criterion is the window's cost while following a running machine, under 2 % of a core, 50 frames/s kept.
Against "debug mode, no window" the window adds 0.1 % (GS on) and 0.5 % (GS off) of process CPU, 0.4-0.5 % on the
GUI thread; the emulation keeps the Pentagon's 48.8 frames/s in every window. Debug mode itself costs 0.2-0.7 %
over plain running. Paused, the window costs nothing measurable (0.2 % is the process at rest).

The earlier, higher numbers were partly the measurement: the window phase contained the window's creation (missing
Consolas font alias population alone is 80 ms). After moving the window out of the measured second, and after the
partial repaint, the whole cost is in the noise of a 10 s window.

## Second processor

The GS Z80 appears exactly as ecat's extra processors do: its own tab with listing, registers, stack, its address
space dump, and the card's RAM as a memory tab.

The dump tabs are what a processor runs or a screen shows: each CPU's space (the status line names the RAM or ROM
page of the byte under the cursor), RAM and ROM by page (the ROM pages the machine loaded: 16 KB on the 48K, 512 KB
on the TS-Conf), and the device memories named `*.ram`, `*.vram`, `*.fastram` (`neogs.ram`, `sprinter.vram`). Firmware
flash, EEPROMs, the CMOS and the TS-Conf palette and sprite table are in the "+" menu. The page map is not a tab: it
showed the CPU's 64 KB a second time. `--list-memory` prints the set for a model. Breakpoints in it stop the machine and bring its
tab to the front (`debug_stop_cpu` answers from the core's last stop). Stepping is a step of the machine, as in ecat,
where a step is a step in time across processors; there is no "step the GS Z80 one instruction" in the core.

## Gaps of the core (goal 4)

1. **No "run this on the emulation thread"**. ecat's `Emulator::invoke(fn)` queues work onto the emulation thread;
   every state change of its debugger goes through it. In the shim `invoke` runs in place and each operation keeps
   itself safe (tool edits, coherent moments, the thread-safe breakpoint manager, direct steps). It works, but every
   new operation needs its own care. A core-level `RunOnEmulationThread(fn)` (async, also while paused) would give
   the debugger one rule.
2. **No single-instruction step of a second processor**. Stepping the GS card's Z80 alone needs a core-level step
   for card CPUs (the debugger-model feature already has the CPU ids: `DebugCpuId::GS`).
3. **Display reads while running are not coherent**. Registers and memory are read from the GUI thread while the
   CPU runs, as ecat does: a 16-bit register or a stack word can be torn between two instructions. Harmless for a
   200 ms follow view; a coherent snapshot per tick (one `RunAtCoherentMoment` that copies the visible state) is
   the clean form, and the debugger-model protocol's "snapshot of what is shown" is the place for it.
4. **Labels for immediates** (resolved). The disassembler replaced an immediate equal to a labelled address by the
   label: `LD hl,START (#0000)` for `LD hl,0`. `Z80Disassembler::ListingStyleScope` now turns labels off for
   operands that are not addresses (jumps, calls, `(nn)` keep them).
5. **Mnemonic case** (resolved). unreal-ng printed operands in lower case; the same scope gives the whole
   instruction in upper case, and the adapter's post-processing is gone.

None of them blocks the window; 1 and 3 are about robustness, 2 is a missing feature.

## Against the expectations

| Expected before measuring | Measured |
|---------------------------|----------|
| go for the main Z80 | go: 25 of 25 checks on Pentagon, ATM3, Scorpion, 20-23 on 48K / 128K / TS-Conf / Sprinter |
| GS tab partial (no per-instruction step of it) | as expected: everything but its own single step |
| diff 5-10 % | 0 % for the port; 7.0 % with the partial repaint (upstream PR #84 plus one fix) |
| timer cost well under 1 % | 0.1-0.5 % of a core after the partial repaint; upstream's full repaints were the main cost |
