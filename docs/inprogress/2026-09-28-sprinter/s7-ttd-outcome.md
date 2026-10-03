# S7 TTD outcome: time travel on the Sprinter (2026-10-02)

Branch `sprinter-ttd`. The TTD part of phase S7 ([roadmap-and-plan.md](roadmap-and-plan.md) S7 row):
TTD records, seeks and replays the Sprinter exactly. Until now it refused the machine
(`PeripheralId::SprinterPld` was declared without a serializer). Design as built:
[tdd-integration.md](tdd-integration.md) §2. Automation recipe: [.recipe/analysis/sprinter-ttd.md](../../../.recipe/analysis/sprinter-ttd.md).

## What a checkpoint holds

Rule: every byte of machine state that affects future execution is in the checkpoint; host input
reaches the machine only through the TTD input journal.

| Part | Where | Id / size |
|---|---|---|
| CPU registers (the Z84C15 engine executes on the `Z80State` register file), the library's instruction-boundary state | `TTDCpuState` (`boundary`) | - |
| 4 MB RAM: 256 pages, the port table `#40` and the graphics pages included | page store | - |
| PLD (`SprinterPldState`), the decoder's own fields, the INT source, the frame height in force, the configuration module (by name, with its state), the block accelerator | `SprinterPld` | 25, 177 + module + accelerator |
| Video RAM, 256 KB | `SprinterVideoRam` | 28, 1 + 262 144 |
| Z84C15 beside the registers: WCR / MWBR / CSBR / MCR, the wait generator (power-on M1 counter, the RETI rule's after-ED flag), watchdog, CTC, SIO (receive FIFOs), PIO, daisy chain IP / IUS | `Z84C15` (`Z84C15::SaveState`) | 29, 1 + 227 (v2 since 2026-10-02: the CTC counter mode - anchors, triggered timers, the CPU clock period - and the watchdog's folded clocks; v1 was 1 + 171) |
| Fast RAM, 64 KB | `SprinterFastRam` | 30, 1 + 65 536 |
| AT keyboard stream (bytes on the wire, typematic, held keys), serial mouse (packet in flight, last sample) | `SprinterInput` | 31, 88 (v2: + the board mouse counters) |
| WD1793 command in flight beyond the BetaDisk blob: queued steps (as tags), transfer pointers (drive, track, offset), byte cell, rotational delay, rate-retry search, read-track noise seed | `Wd1793Context` | 35, 1 + 112 |
| CMOS, IDE (two channels, adapter latch), WD1793 + drives, AY / TSFM, Kempston mouse and joystick | shared serializers | 18, 17, 1, 0 / 4, 7, 23 |

Every new blob starts with a version byte; a blob of another version is not loaded. Layouts:
`ttdsprinter.cpp`, `wd1793.cpp` (`SaveTransferContext`), `z84c15.h`, and `ttd.ksy`.

Video RAM and fast RAM are whole-array blobs: TTD v2 memory regions (migration-trajectory Phase 1)
are not on master. They move to regions when those land.

## Keyboard and mouse

- **Keyboard:** a host key is journaled as a ZX key (`Key`: the matrix of code `#40`) and a PC key
  (`PcKey`: the PS/2 stream to SIO A). Replay applies the journal at the recorded instruction
  boundary; the bytes already on the wire, the typematic timer and the SIO A FIFO are in the
  checkpoint.
- **Mouse:** the journal holds the mouse input at the MouseManager (`MouseMove` / `MouseButtons` /
  `MouseCounters`). The board mouse keeps its own counters (blob 31 v2, bytes 85-87); the Microsoft
  serial mouse on SIO B is a packet generator that samples them when SIO B is accessed; its packet
  in flight and the SIO B FIFO are in the checkpoint.
  A restore in the middle of a packet resumes on the same byte (tested).

## Findings that changed shared code

- **WD1793 restore inside a command was broken for every machine**: the BetaDisk blob nulls the
  transfer pointers and empties the queued steps (closures), so a restore during an ID search or a
  sector ended the command with Not Ready. The queue is now built from tags (`FifoKind`,
  `MakeFifoEvent`; behavior unchanged) and the new `Wd1793Context` blob restores both. Only the
  Sprinter declares it: adding it to the other Beta machines changes their device set and needs their
  fixtures re-recorded (open item below).
- **Restore order**: `TTDPeripheralRegistry::RestoreAll` restores in ascending id order (was the
  unordered map's), so a blob that completes another device's state loads after it. The corpus
  (other machines) still passes unchanged.
- The rate-retry search of the WD1793 (transient, not in its blob) is in `Wd1793Context`.
- `Z84C15::SaveState` / `LoadState` and `Z84Ctc::SetVector` in the library (README change table).

## Tests

`core/tests/debugger/ttd/sprinter/ttdsprinter_test.cpp`:

| Test | What |
|---|---|
| `TTDSprinter_Test.*` (9) | each serializer round-trips (save, scramble, load, save = same bytes, fields back); another version is not loaded; the module travels by name with its state; registry capture / restore complete |
| `ExactRestore_MidPldLoad` | full start: replays from the load's start and from a checkpoint in the middle of the bitstream (count 0 < n < 473 720) reach every later checkpoint: CPU, chipset, every blob, RAM, picture |
| `DssFloppyBoot_RecordAndReplayWithKeysAndMouse` | DSS 1.62 from the floppy, "ver" + Enter typed inside frames and mouse moves between the keys; the whole recording replayed; exact restores from a boundary inside a floppy sector and one with a PS/2 byte on the wire |
| `ExactRestore_MidIdeSector` | DSS from a hard disk; restore from a boundary with the ATA buffer half moved |
| `ExactRestore_MidPs2ByteAndMidMousePacket` | a program polls SIO A and B with interrupts off and logs every byte; restores from a PS/2 byte on the wire and from a mouse packet half sent: the log (RAM), FIFOs, streams equal |
| `SeekAnywhere_InsideFramesBackAndForth` | seeks to positions inside frames, back and forth: CPU and video RAM as recorded |
| `ExactRestore_AcceleratorArmedAndInIntSuspendWindow` | a program repeats `LD D,D : LD E,16` and `LD C,C : LD (HL),A` with interrupts on; every other handler runs past the frame end: restores from a boundary with the fill armed and from one inside the INT suspend window (`blocked`) |

Mutation checks: the mouse state not loaded fails `ExactRestore_MidPs2ByteAndMidMousePacket`; the
accelerator state not loaded fails both accelerator restores; before the WD1793 context the
mid-sector restore failed at the first frame. Run time of the machine tests: ~15 s together under `test-parallel`, the DSS boot ~9 s of it (boot-bound,
justified in each test's comment).

Corpus: `testdata/machines/sprinter/ttd/boot.ttd` (`record_fixtures.py --only sprinter_boot`): the cold
full start, 300 frames - the PLD load, then BIOS POST at 21 MHz; `TTD_Corpus_Test` restores
checkpoints 0 / 50 / 51 / 300 and replays 37 + 25 (inside the PLD load). The other fixtures and the
CI gate are unchanged.

## Accelerator (S5)

S5 was not on master when this work finished; branch `sprinter-s5` (0800d2a17, 7d1e19552) is merged
into `sprinter-ttd`. The standard accelerator's `SprinterAccelState` (280 bytes: mode, direction,
function, length, prefix / ED / RETI tracking, `blocked`, the alternate addressing, the 256-byte
buffer, the counters) is the PLD blob's accelerator section; on load the engine's data watch follows
the mode and the active accelerator is the CPU's bus agent again (`RefreshAccelerator`). The write in
progress between `BeforeWrite` and `AfterWrite` is inside one bus cycle, never at a boundary. A
configuration module that brings its own accelerator carries it in its module state.

## Reserved ids and sections (for S6 and later)

| Id | Reserved for |
|---|---|
| 32 `SprinterCovoxBlaster` | **taken in S6** (2026-10-02): v1, 545 bytes (`CovoxBlasterState`); the PLD blob keeps the control byte; the Sprinter fixture re-recorded ([s6-sound-outcome.md](s6-sound-outcome.md) §5) |
| 33 `SprinterIsa` | S6b: ISA I/O window latches, the ZX-bus adapter |
| 34 `SprinterPads` | input extras: the two extended pads and their select counters |

A device that lands declares its id in `GetTTDModelStateIds` and adds a serializer; the existing
Sprinter blobs keep their layout, so a checkpoint only gains a blob and only the Sprinter fixture is
re-recorded (once). The Covox-Blaster control byte (code `#89`) is in the PLD blob today; S6 may keep
it there or move it to id 32 with a PLD blob version bump.

## Open items

- `Wd1793Context` for the other Beta machines (Pentagon, Scorpion, ATM, Profi ...): declare it when
  their fixtures are re-recorded next; until then a restore inside a floppy command on them still
  ends the command.
- Video RAM and fast RAM to TTD v2 memory regions (Phase 1).
- Port journals stay off on the Sprinter (its interrupt source supplies the IM2 vector; NeoGS when
  fitted): replay runs against the live media.
