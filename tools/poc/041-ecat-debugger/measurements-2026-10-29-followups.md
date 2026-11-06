# Measurements 2026-10-29: the follow-ups

Setup as in [measurements-2026-10-29-port.md](measurements-2026-10-29-port.md). Raw data:
`scratch/2026-10-29-poc-041/` (`selfcheck-round3/`, `measure-round3.txt`, `tabs/`).

## The views are upstream's partial repaint

`cpp/ecat/dialogs/debugger/` is now the branch of the upstream pull request
([Ptr314/ecat3#84](https://github.com/Ptr314/ecat3/pull/84), `ecat3-repaint` at `f61057ef`) instead of the POC's own
repaint. The POC's earlier diff table in the port measurements is superseded:

| Against | Files | Added | Removed | Of upstream lines |
|---------|------:|------:|--------:|------------------:|
| `ecat3-dev` `27a98108` (upstream `dev`) | 9 | 219 | 20 | 3 402 (7.0 %) |
| the pull request's branch | 1 | 5 | 2 | `debugutil.h`: `PaintedRows` below |

`PaintedRows` fix: a row counted as painted only when one paint covered it whole. The jump arrows are a column over
all the listing's rows; whenever they changed, their update left every row half painted, and the next tick repainted
the whole listing although no row's content had changed. A row now also stays painted when a partial paint draws it
with the key it was already fully painted with: the part left out still shows that key. The pull request has the
same defect.

Pentagon with NeoGS, booted to the 128 menu, `--measure 5`, the window following the run:

| Views | debugger paints/s | debugger px/s | of which the listing |
|-------|------------------:|--------------:|---------------------:|
| the pull request as it is | 26.0 | 1 056 208 | 611 502 |
| with the `PaintedRows` fix | 20.0 | 254 154 | 22 657 |

Process CPU 2.6 % and GUI thread 0.6 % in both, 48.79 frames/s; paused, nothing is painted. Most of what is left
(146 376 px/s) is the stack view: it is centred on SP, and SP sampled at a 200 ms tick in a running program is a
different value each time, so every row shows another word. That is a change of content, not an over-repaint.

## Self-check

`--self-check` now also drives the window itself, not only the API under it (native dialogs off, the file dialogs
answered by typing into their name field):

- the listing and the dump are found in the window;
- an instruction's bytes typed over in the listing (`LD A,#42` at the cursor) land in memory;
- the dump's find (`#C123`), save (64 KB as the CPU reads it, compared with `peek_mem`) and load (16 bytes at the
  cursor, `#C200`);
- the Devices tab has fields.

Breakpoint and step-out checks follow the machine: the breakpoint goes on `#0038` under IM 1 with interrupts on,
otherwise on an address the PC history shows; step out runs only for the IM 1 handler; step over is skipped on a
`HALT`. Run to cursor targets the most frequent address of the history.

3 rounds, each model with `--card neogs` (and Pentagon also with `--card multisound`), all passed, 24 of 24 runs:

| Model | checks | not run |
|-------|-------:|---------|
| Pentagon, NeoGS / MultiSound | 25 | |
| ATM3, Scorpion | 25 | |
| TS-Conf | 23 | step out (not in IM 1 with interrupts on), step over (the CPU in HALT at the stop) |
| 48K, 128K | 21 | the GS checks (no `zxbus.1` slot: the edge connector) |
| Sprinter | 20 | the GS checks (no `zxbus.1` slot: ISA), step out (not in IM 1 with interrupts on) |

## Other changes

- **Font**: Consolas, the font the views ask for, is registered from the resources at startup (copied from ecat3's
  own resources), so the views get it on every platform instead of Qt's fallback search.
- **Disassembler style** (gaps 4 and 5 of [analysis.md](analysis.md#gaps-of-the-core-goal-4)): the core's
  `Z80Disassembler::ListingStyleScope` sets, for the calling thread, upper case for the whole instruction and labels
  only for address operands (jumps, calls, `(nn)`). The adapter's post-processing is gone. Test
  `Disassembler_Test.listingStyleScope`.
- **MultiSound**: `--card multisound` puts the MultiSound card with its GS in `zxbus.1`; its Z80 gets its own
  processor tab (`DebugCpuId::MultiSoundGS`), as NeoGS's does.
- **Run to cursor over a user breakpoint**: the core's breakpoint manager keeps one plain breakpoint per address
  whatever its owner, so a user breakpoint set where a run-to was pending was absorbed into it and vanished at the
  stop. The adapter now removes the run-to breakpoint at that address first.
- Noted, not changed: writing PC while the CPU is halted leaves it halted (the core's semantics, as MAME's
  `state_import`); the 48K's RAM tab shows 8 pages, the core's RAM of every model.

## The window over a running unreal-qt (2026-10-30)

`ecat-debugger-poc --connect 127.0.0.1:<port> [--emulator <id>]` opens the same window over an emulator that runs
in another process, through its WebAPI ([`adapter/remotemachine.cpp`](cpp/adapter/remotemachine.cpp), the views
unchanged). The window over unreal-qt running Dizzy X: [window-remote-unreal-qt.png](window-remote-unreal-qt.png).

- Reads: one `GET /debug/snapshot?memory=cpu:0:65536` per tick (registers, the CPU's 64 KB, its page map, the
  stop) serves every read of the listing, registers, stack and CPU dump; RAM / ROM dumps read 16 KB pages
  (`/memory/page/{kind}/{n}?format=binary`), device memories 4 KB chunks (`/memory/region/{name}`), each once per
  snapshot; breakpoints are listed once per snapshot.
- Events: a long poll (`GET /debug/wait`, its own thread and connection) wakes the window at a stop, a run start
  or another client's edit, and removes the run-to breakpoint (its own group, `ecat-run-to`) at a stop.
- Changes: `POST /step | /stepover | /stepout | /pause | /resume`, `PUT /registers/{name}`, `PUT /memory/{addr}`,
  `POST /breakpoints`, under a run-control session (`client: ecat-debugger`), released on exit; debug mode goes
  back to what it was.

Self-check over unreal-qt (Pentagon with NeoGS, Dizzy X: IM 2, interrupts off): 20 of 20 pass, the same checks as
in process minus step out (not IM 1) and the GS ones (below). After it the emulator has no breakpoint left and no
session held.

Cost, `--measure 10`, 3 runs (unreal-qt's CPU and frames sampled from outside at the same moments,
`scratch/2026-10-30-poc-041-remote/measure_remote.py`):

| Window | unreal-qt CPU | unreal-qt frames/s | POC process CPU | requests/s | received |
|--------|--------------:|-------------------:|----------------:|-----------:|---------:|
| no debugger | 6.4-6.6 % | 48.8-48.9 | 0.2 % | 2.0 (the long poll) | 9-19 KB/s |
| window following the run | 7.3-7.6 % | 48.5-48.9 | 1.2-2.5 % | 11.9 | 433 KB/s |
| window, paused | 1.2-1.3 % | 0 | 0.2 % | 2.0 | 0.1 KB/s |

The window adds about 1 % of a core to unreal-qt and keeps its 48.8 frames/s (the one 48.5 is the sampling: the
two other runs give 48.9). Nothing is read while paused but the long poll.

Not over the WebAPI yet:
- the sound card's Z80 tab: the WebAPI gives only its PC, SP and AF (`/state/audio/gs`), no register write, no read
  or write of its address space; its breakpoints exist (`?cpu=gs`);
- labels in the listing (the decoder runs in the POC without the emulator's label table; `/labels` exists);
- the ROM tab has the core's ROM capacity (128 pages), not the pages the model loaded.
