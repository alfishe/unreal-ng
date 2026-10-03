# Phase 3 — Everything a replay needs: technical design

Status: **design, not implemented (2026-10-02).** Roadmap and checks: [README.md, Phase 3](README.md#phase-3--everything-a-replay-needs). Requirements: FR-10, FR-14, FR-20, FR-21 ([requirements.md](requirements.md)). Decisions: D14, D17, D20, D21, D24, D25 ([engine-decisions.md](engine-decisions.md)); D5, D9, D10, D16, D19, D26 and D33 where they touch this phase.

The engine is `ttd::TimeTravelEngine`, built next to v1 (`ttd::TimeTravelManager`); engine-only parts live in `core/src/debugger/ttd/engine/` ([engine-approach-and-naming.md](engine-approach-and-naming.md)). Phase 1 owns machine time, the frame table and positions with a branch ([phase-1-memory-regions-tdd.md](phase-1-memory-regions-tdd.md)); Phase 2 owns the device registry and device state ([phase-2-device-state-tdd.md](phase-2-device-state-tdd.md)); Phase 4 owns the file ([phase-4-session-file-tdd.md](phase-4-session-file-tdd.md)). This phase defines the in-memory model of everything a replay consumes, and what Phase 4 must write.

Code references are to master at `8ddaf708e`. `TTM` = `core/src/debugger/ttd/timetravelmanager.cpp`, `DF` = `core/src/debugger/ttd/ttddumpformat.h`.

## 1. Glossary

| Term | Meaning |
|---|---|
| Replay | Re-running the emulator from a checkpoint to reach a point between two checkpoints |
| Sealed replay | A replay whose result depends on nothing outside the session: no host clock, no host file, no host network |
| Event | One recorded thing that happened at a known machine time: a key press, a reset, a byte the CPU read from a port |
| Point of application | Where in the emulation an event takes effect: at a frame boundary, between two instructions, or inside the access that consumes it |
| Payload | Bytes an event carries that do not fit its record, such as the data a network socket received |
| Bus data | A byte that enters a CPU or memory from a device: an `IN` result, an interrupt vector, a byte moved by DMA |
| DMA | Direct memory access: a device writes memory without the CPU executing an instruction for it |
| Machine time | The engine's single time line: main-CPU cycles in top-clock units since the session start, 64-bit (D20) |
| Top-clock unit | The shortest T-state the model can run at. On a 1×/6× Sprinter one 3.5 MHz T-state is 6 units |
| Frame table | Each frame's start in machine time. Frames do not all have the same length (D21) |
| Secondary CPU | A CPU other than the main Z80: the General Sound card Z80, the ATM2 keyboard controller, the FT812 on VDAC2 |
| Clock map | The table that converts a secondary CPU's cycle count to machine time and back, built from clock-change events |
| Configuration fingerprint | The emulator settings exact replay depends on: frame length, clocks, audio rate, decimator, ROMs, devices |
| Media version | A sealed state of a disk, SD card or hard disk image, kept by the storage manager's change layer |
| Write journal | The record of every memory write with its time and the writing instruction; answers "who last wrote this byte" |
| Coverage index | Per frame, the set of addresses executed, written and read; tells a reverse search which frames to replay |
| Derived index | Data the engine can rebuild from the session by replay. Keeping it is a speed choice, not a correctness one |
| Retention policy | The rule that decides which part of a derived index is kept in memory and in the file |

## 2. What changes, in one example

A Sprinter (top clock ×6, so one 3.5 MHz T-state is 6 units; 224 T per line) records a DSS program. Frames 0–100 are 320-line frames: 320 × 224 × 6 = 430,080 units, 20,480 µs each ([Sprinter goals FR-7](../2026-09-28-sprinter/goals-and-requirements.md); [crash analysis](../2026-09-28-sprinter/crash-fb-overflow.md)). During frame 100 the program writes the code `#2D`, so frame 101 has 312 lines: 419,328 units, 19,968 µs. At frame 101, T 6,000, the user presses a key; at frame 103 the program writes a sector to the hard disk image and reads the real-time clock.

| What | v1 today | Engine after Phase 3 |
|---|---|---|
| Time of frame 101's start | `GlobalT = frame × FrameSpan()` (timetravelmanager.h:895, TTM:2211-2216) uses the current frame length: 101 × 419,328 = 42,352,128, earlier than frame 100's start (100 × 430,080 = 43,008,000). Write-journal records go backwards | frame table: 43,008,000 + 430,080 = **43,438,080**; the key press is at 43,444,080 |
| Clock reading at frame 101's start | `PortDecoder::EmulatedMicroseconds` (portdecoder.cpp:237-255) = 101 × 19,968 = 2,016,768 µs, 31 ms before frame 100's start (2,048,000 µs) | from the frame table: 2,048,000 + 20,480 = **2,068,480 µs** past the session's time base |
| The `IM2` vector of each interrupt | taken from the live device at replay (`IInterruptSource::AcknowledgeInterrupt`, z80.h:341); the port journal is refused for the machine (TTM:2077-2078) | recorded as bus data at the acknowledge; a replay reads it from the session |
| The key press | input event, applied between instructions (TTM:1928-1945) | the same, as an event of kind `Key` in the one event stream |
| The disk write at frame 103 | a replay barrier (`DiskWrite`, mediamanager.cpp:651-667): a seek stops before it | no barrier: a replay of frame 103 runs, the CPU reads from the controller what it read when recording (bus journals), the write goes into the held overlay (FR-20), the image file is untouched; the checkpoint of frame 104 names the disk's new version so the controller's own state matches too |
| The session opened in an emulator with another audio rate | loads; a replay inside a frame silently differs | loads; status lists `audioCoreRate: recorded 44100, live 48000`, replay operations report **not bit-exact** |

## 3. How it works today

### 3.1 Three journals, three formats

| Journal | Records | Applied | In the file |
|---|---|---|---|
| Input (`ttdinputjournal.h`) | 16 kinds (ttdinputjournal.h:72-105): keyboard, PC keys for PS/2, Kempston mouse and joystick, front-panel switches, General Sound host stimuli, network events. `TTDInputEvent` holds time and the union of all fields (:110-126) | between instructions: `ServiceInput` walks a cursor through the events due (TTM:1928-1945), driven by the per-step work gate; live input is journaled before it is applied (TTM:1901-1926) | yes: header bit 6, fixed 22-byte records (DF:94-107, :172) |
| Network part of input | `TTDNetInput` per `NetEvent` (ttdinputjournal.h:131-145), bytes in one payload vector (:244-250). A device names its buffered bytes by journal index (virtualnetwork.cpp:593), so a checkpoint stores references, not bytes. `ModemLines` is a `NetEvent` type (nettypes.h:28) | as input | yes: bit 10 (DF:154-163) |
| External events (`ttdexternalevents.h`) | markers: tape control, disk write, debugger edit, hardware reset, other (:65-72), with a 64-byte reason, **no payload** | never applied: a seek stops before the first marker it would cross (`FirstMarkerInInterval`, :171-172) | yes: bit 7 (DF:109-120) |
| Port journals (`ttdportjournal.h`) | every `IN` result and every `OUT` of the main CPU: time, port, PC, value (:47-60); blocks of 32,768 records in five zstd columns | `Play` mode hands the CPU the recorded value; the device still sees the read; a differing answer is counted (:115-134) | yes: bit 8, with one cursor per checkpoint (DF:122-143) |

So since the v1 amendments the input and the external events **are** saved (the roadmap's "a saved session holds neither the input" predates them). What is missing is a common model: three time keys (`TTDTimePoint`, `globalT`), three orderings, no payloads for markers.

### 3.2 What a v1 replay depends on

- **Bus data beyond `IN`.** The port journal is refused when the machine supplies its own `IM2` vector, steps a DMA engine with the CPU, or carries NeoGS or ZX Next DMA (`PortJournalUnsupportedReason`, TTM:2050-2086). Sprinter, TSConf, ZX Next and every NeoGS configuration replay against live devices.
- **Markers stop seeks.** Every tape command (tape.cpp) and every media write (floppydriveslot.cpp:53, mediamanager.cpp:651-667) is a barrier. A debugger edit is a barrier because its data is not recorded (emulator.cpp:680-697). `HardwareReset` exists as a kind but is never emitted: a reset stops the recording (emulator.cpp:940-944).
- **Time assumes fixed frames.** `GlobalT` and the RTC's emulated clock multiply the frame number by the current frame length (§2).
- **Real-time clocks.** Every machine with an MC146818 / DS12887 (ATM3 / ZX-Evo, Profi, Scorpion SMUC, TSConf, Sprinter) runs the chip on emulated time while a recording runs: anchored at the host time when recording starts, advanced by `EmulatedMicroseconds` (ds12887.cpp:131-150, :210-229; ttdds12887.cpp:40-48; the anchor is in the blob, ds12887.cpp:597-598). Outside a recording the chip reads the host clock. D25's "Profi CMOS and SMUC RTC read the host clock today" is true only outside a recording.
- **Configuration.** The header records the model, the RAM page count, one ROM signature and the device mask (`ttd.ksy` header). Frame length, clock settings, audio rate and decimator quality (platform.h:519, :526, :662) are not recorded; a speed change ends the session (emulator.cpp:650).
- **Writes outside the session.** Only the NeoGS SD insert / eject refuse during replay (neogsmedia.cpp:89, :117). A replay that writes a disk changes the session write map or, in write-through mode, the image file (mediatypes.h:27; sdcardspi.cpp:39).
- **Several CPUs.** A position is `{frame, tInFrame}` of the main CPU. A General Sound card catches up in bursts ([GS debugger requirements §2](../2026-09-27-gs-debugger/requirements.md)); its cycle count is only inside its blob.

### 3.3 Write journal and coverage index

- The write journal (`ttdwritejournal.h`) keeps 12-byte records - 40-bit `globalT`, address, port flag, PC, value, physical page (:54-67) - in a ring of 64 MB (:90): 8,388,608 records. Port `OUT`s go into it a second time (TTM:4776-), next to the port journal.
- Find-last answers from it only while it is gapless for the whole session; header bit 4 carries that (DF:76-83; TTM:3611, :4599). Otherwise it replays.
- The coverage index (`ttdcoverageindex.h`) keeps per frame sorted sets of executed, written and read physical addresses, about 307 B per frame (:17-26). Without it a reverse query replays 111-215 frames at 1.3 ms each (:7-9).
- Measured on real use ([E6](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e6-v1-v2-model/README.md)): 870-3,500 memory writes per frame on the demos and the game, 2.5-3.4 bytes per write compressed, 7-31 MB per minute; v1's ring is full after 0.8-3.2 minutes. In the engine, kept whole, the journal is the largest stream of every active session.

## 4. Design

### 4.1 Overview

```
                         machine time (u64, top-clock units)
 frame table   |  f100 (430,080)  |  f101 (419,328)  |  f102  | ...         Phase 1
               |                  |                  |        |
 event log     .   Key            .   Edit           .        .             Step 1 (sparse)
 bus journals  ||||||||||||||||||||||||||||||||||||||||||||||||  IN, OUT,   Step 1 (dense)
                                                               vector, DMA
 payload store [net bytes][edit bytes][DMA bytes] <- refcounts from events and checkpoints
 CPU table     main | gs.z80 (12 MHz) | ngs.z80 (10-24 MHz) | ... + clock map  Step 3
 checkpoint N  { regions, devices (Ph 1-2), bus cursors, CPU counters,
                 fingerprint id, media versions }                       Steps 1, 3, 4
 derived       write index (policy, E7) | coverage index (whole)        Step 7
```

### 4.2 Step 1 — One event stream

**One ordering, two storage classes.** D24 asks for one stream with a kind per event. Logically it is one sequence ordered by `(machineTime, seq)`; `seq` keeps the recording order of events at the same instant. Physically there are two encodings, because their rates differ by four orders of magnitude:
- the **event log**: sparse events (input, markers, facts), one record each;
- the **bus journals**: dense bus data, in v1's columnar block format (`TTDPortJournal`, shared with v1 as [engine-approach-and-naming.md](engine-approach-and-naming.md) lists it). A polling loop compresses to almost nothing there; a per-record header would not.

A merged cursor walks both in time order for queries and the timeline.

**Data.**

```cpp
// core/src/debugger/ttd/engine/ttdeventlog.h
enum class TTDEventKind : uint16_t   // stable: stored in files, appended, never reused
{
    // 0x0000-0x00FF input: the value of TTDInputKind (Key = 0 ... FrontPanelSwitch = 15)
    // 0x0100-0x01FF markers: 0x0100 + TTDExternalEventKind
    TapeControl = 0x0100, MediaWrite = 0x0101, DebuggerEdit = 0x0102, HardwareReset = 0x0103,   // HardwareReset: v1 import only (D39)
    // 0x0200-0x02FF frame-boundary cuts
    SnapshotLoad = 0x0200, MediaChange = 0x0201, ConfigChange = 0x0202,   // 0x0203 reserved (was DeviceSetChange, D38)
    // 0x0300-0x03FF facts the machine produces itself
    ClockChange = 0x0300, FrameLengthChange = 0x0301, ReplaySourceChange = 0x0302, InterruptFrame = 0x0303,
    OtherMarker = 0x01FF,
};

enum class TTDApplyPoint : uint8_t { FrameBoundary, InstructionBoundary, OnAccess };
enum class TTDEventRole  : uint8_t { Input, Cut, Fact, Barrier };

struct TTDPayloadRef { uint32_t id = 0; };          // 0 = none

struct TTDEvent
{
    uint64_t machineTime = 0;
    uint32_t seq = 0;            // order among events at the same machineTime
    TTDEventKind kind;
    TTDCpuId cpu = TTDCpuId::Main;   // the CPU whose boundary applies it (Step 3)
    uint8_t args[16] = {};       // kind-specific: a TTDInputEvent's fields, a clock rate
    TTDPayloadRef payload;       // network bytes, edit bytes, a reason string
};

class TTDEventLog
{
public:
    void Append(TTDEvent ev);                                  // emulation thread, while recording
    TTDEventCursor CursorAt(uint64_t machineTime) const;       // first event at or after
    const TTDEvent* FirstBarrierIn(uint64_t from, uint64_t to) const;   // (from, to]
    static TTDApplyPoint PointOf(TTDEventKind kind);
    static TTDEventRole RoleOf(TTDEventKind kind);
};
```

The kind ranges make the v1 mapping an identity: a v1 input kind keeps its number, a v1 marker adds `0x0100`. The engine reuses v1's `TTDInputEvent` fields and `ApplyInputEvent` (`ttdinputapply.h`); the `TTDInputJournal` container itself is not reused.

**Kinds, their point of application, and how v1 maps.**

| Kind | Role | Point | Payload | From v1 |
|---|---|---|---|---|
| `Key`, `PcKey`, mouse, `Joystick`, `FrontPanelSwitch`, `KeyboardReset` | Input | instruction boundary | — | input journal, same kind number |
| `GSCommand`, `GSData`, `GSNmi`, `GSResetCard`, `GSReset` | Input | instruction boundary of the main CPU | — | input journal |
| `NetEvent` (incl. `ModemLines`), `NetLinkReset` | Input | instruction boundary | received bytes | input journal + network section; `netIndex` / `journalIndex` become payload ids |
| `TapeControl` | Input | instruction boundary | — | external event (the deck is part of the machine; what the CPU reads from the tape is EAR bits, bus data) |
| `DebuggerEdit` | Input | instruction boundary | target + bytes (memory region and offset, register id, device register) | external event; a v1 edit has no data, so it imports as `Barrier` |
| `MediaWrite` | Fact | — | slot id | external `DiskWrite`; the replay writes again into the held overlay (FR-20) |
| `HardwareReset` | — | — | — | not emitted: a reset ends the session (D39); the kind number stays for v1 files |
| `SnapshotLoad`, `MediaChange`, `ConfigChange` | Cut | frame boundary | what changed | none: v1 ends or invalidates the session (D10, D26). The device set is fixed for a session (D38) |
| `ClockChange`, `FrameLengthChange`, `InterruptFrame` | Fact | — | rate, length, RZX frame | none |
| `ReplaySourceChange` | Fact | instruction boundary | source (Step 2) | none |
| `IN` result | bus data | on access | — | port read journal, unchanged blocks |
| `OUT` value | Fact (bus) | on access | — | port write journal, unchanged blocks |
| Interrupt vector (`IM2` low byte) | bus data | on access (INT acknowledge) | — | none: refused config in v1 |
| DMA into memory | bus data | on access (per transfer) | the bytes | none: refused config in v1 |

What each role means for a replay:
- **Input** drives the machine. A replay applies it at its point, exactly as live input was applied (journal before apply, TTM:1901-1926).
- **Bus data** is consumed by the access that asks for it, in order, and checked against the access's time, port and PC (as `PlayNext` does). D24 names two points; consumption "on access" is the third, needed because an `IN` happens inside an instruction.
- **Fact** is produced by the emulation itself. A replay regenerates it and compares; facts feed indexes (clock map, frame table, RZX frame lookup) and queries.
- **Cut** ends the frame early and is followed by a checkpoint. A replay never executes through a cut: crossing it means restoring the checkpoint after it. Reset and snapshot load therefore stop being the end of a session.
- **Barrier** exists only for records read from v1 files without their data: an edit without its bytes, a v1 reset, `OtherMarker`. A seek stops before it and says why. A v2 session has none.

**A replay is sealed** (owner principle, 2026-09-29, restated 2026-10-03): during a replay the machine is cut off from the outside world and fed only what the session recorded - input events, network answers, and at the CPU's boundary every value it reads from outside (the bus journals: `IN` results, interrupt vectors, DMA into memory). Disks, tapes and network adapters keep working inside the replay exactly as when recording, until the replay ends; nothing they do reaches the host (Step 6). Media versions (Step 4) are not what makes the replay exact - the CPU already reads the recorded bytes - they keep the controllers' own state (an FDC's buffer, a hard disk's cache) equal to the recording where the replay stops.

**Which events are bus data, and where they are recorded.**

| Bus data | Tap | Machines |
|---|---|---|
| `IN` result | `Z80::in`, after the RZX substitution ([RZX design §4](../2026-09-29-rzx-replay/design.md)) - the existing hook | all |
| Interrupt vector | `IInterruptSource::AcknowledgeInterrupt` return value (z80.h:330-346), one pointer test per acknowledge | Sprinter (Z84C15 daisy chain), TSConf |
| DMA into memory | the DMA engine, once per transfer, only when its source is outside the sealed machine: SPI / SD, IDE, a network card. Memory-to-memory DMA needs nothing: it is a function of state already captured | TSConf (SPI source), ZX Next, NeoGS ZX-DMA |
| Secondary-CPU `IN` | the card CPU's port read, same block format, `cpu` column | General Sound, NeoGS (when a card reads host-supplied data) |

With these, `PortJournalUnsupportedReason` (TTM:2050-2086) has no reason left on any creatable configuration. A configuration still refuses until its tap is in and tested, and reports why (`portJournalOffReason`, FR-21).

**Payload store and retention.**
- `TTDPayloadStore` (engine): an arena of byte ranges with a reference count each. `Store()` returns a `TTDPayloadRef`.
- References come from events and from checkpoints. A device that buffers received bytes (the virtual network's socket queues) names them by payload id in its state, as v1 does with `journalIndex`. Phase 2's device contract gains `CollectPayloadRefs(std::vector<TTDPayloadRef>&)`, so the engine knows each checkpoint's references (D5: every stored item records what it depends on).
- A payload is kept while any kept event or kept checkpoint refers to it. When the start of history moves forward (Phase 4 eviction), events before the new start are dropped, but their payloads stay as long as a kept checkpoint still holds them.

**Debugger edits as input — as built (2026-10-03).** `Emulator::EditMemoryFromTool` (every tool edit: Lua, Python, WebAPI memory and page writes, device memory, the RTC) brackets the edit with `TimeTravelManager::BeginToolEdit` / `EndToolEdit`. The tool's edit is an opaque function, so its bytes are taken from what it left behind: every machine RAM page and device-memory piece marked dirty since the last checkpoint (the edit's among them), whole, and every device state the edit changed. A replay reaches the edit with the same contents as the recording, so writing these is exactly the state after the edit; no new check on the memory write path. v1 keeps its marker (a barrier); the engine gets the bytes (`kEditCarriesData`) and its replay applies them and goes on. `TimeTravelManager_EngineSeek_Test.AToolEditReplaysWithItsBytes`: v1 stops at the edit, the engine lands on the recorded machine 10,000 T after it. Register edits from the debugger do not go through this path yet.

**Debugger edits as input.** D9: an edit at the present is journaled. With its bytes in the payload, a replay re-applies it, so an edited session replays through the edit instead of stopping. An edit in the past starts a branch (Phase 5, [phase-5-switchover-tdd.md](phase-5-switchover-tdd.md)). `Emulator::EditMemoryFromTool` (emulator.cpp:680-697) passes the edit's target and bytes, not only its source name.

**Built in now, used later.** Events carry the branch through their position (Phase 1); per-branch event data is a separate stream in the file ([what-if §8](../2026-09-29-model-what-if/design.md)). `MediaChange` becomes exact with media versions (Step 4).

**File consequences (Phase 4).** An event-log stream (records grouped per frame, delta-coded times); a payload stream; the four bus journals in v1's block layout with one cursor per checkpoint each; a kind table in the header so a reader names kinds it does not know and treats unknown `Input` kinds as barriers.

**As built (2026-10-03), the first full-scenario A/B.** The engine holds a session's events (`TTDEventLog`, `TTDPayloadStore`) and bus data (`IN` / `OUT` journals in v1's block format, a cursor per checkpoint), fed from v1 live in shadow mode and from v1 files. `TimeTravelManager::SetReplaySource(engine)` makes a seek restore from the engine's checkpoint (CPU, chipset, every memory region, every device), apply input from its event log and hand the CPU its recorded `IN` values; the replay loop is still v1's (Phase 5 moves it). A session fed from a file is bound to the live machine first (`TimeTravelEngine::BindLive`: regions by id and size, devices by v1 id; Phase 4's file reader needs the same). `TimeTravelManager_EngineSeek_Test` seeks to points inside frames twice, from v1's data and from the engine's, and compares the machine - CPU, all RAM, every device's state - on a live recording with key presses mid-frame and on the Dizzy X and Green Beret fixtures. Mutations: not restoring devices, not applying input, not playing the bus journals each fail it. Found on the way: a fed session's device table had no live devices, so the engine's restore skipped them (the cause of the first A/B failure); and on Green Beret's tape load the live tape answers differently from the recording during a replay (up to 3,444 value mismatches, v1 and engine alike; the CPU reads the recorded values), an open item for Q5.

**As built (2026-10-03), media reads (owner decision: record at the sector read).** A code survey found three paths where data from outside reaches the machine without an `IN`: TS-Conf's DMA from the SD card and from IDE, and the NeoGS card's own SD reads (which the main CPU then sees through ZX-DMA). Interrupt vectors are all functions of captured device state. Instead of a tap per DMA engine, every block medium's stack now ends in `MediaReadTap` (emulator/io/storage/mediareadtap.h), bound by the media manager to its slot; `IMediaReadJournal` (emulator/media/mediareadjournal.h) is the media layer's interface. While recording, each sector read from an image goes into the engine's `TTDMediaJournal` (frame, T, slot, LBA, bytes; `mediaReadCursor` per checkpoint); a seek replaying from the engine hands the reads back in order, and a read the recording lacks (another slot or LBA: the execution left the recording) goes to the image and is counted. One tap covers the CPU's `IN`, both DMA paths and the card CPU, and a replay needs no image file. `TimeTravelManager_EngineSeek_Test.SectorReadsComeFromTheSessionNotTheImage`: a ZX-Evo program reads a sector from its SD card while recording, the image file is then changed; seeks from the engine's data land on the machine the original image gave (card buffer, RAM, CPU), seeks from v1's data do not. The ATAPI drive reads CD data past the block stack (`CdImage::ReadUser`, `ReadFrame`): those reads are journaled in `CdImage` itself, bound by the media manager like the tap (the block stack's own sector reads go by an unrecorded path, so nothing is recorded twice). CD audio samples (`ReadAudio`) are not recorded: they are the drive's output to the host (the renderer's cache is not machine state), and a replay is muted. D33 prints the journal apart (v1 has none).

Found while closing it: the Green Beret value mismatches (Q5) were two bugs in the tape's restore - a restore into a deck whose blocks were not installed yet (the deck installs them lazily when it starts) left it empty, and a block whose edges had to be generated again (freed when the deck moved on, or freshly installed) restarted from its first pulse. With the session's tape in the deck the replay's devices now answer as recorded (`RecordedFixtures_EngineSeeksLandOnV1sMachine` checks zero mismatches on both engines; reverting either fix fails it). A session still does not carry its tape: that is media in the session (Step 4).

**As built (2026-10-03), vectors and the bus data on every machine.** The engine's bus data came from v1's port journals, which v1 recorded only where its gate let its own replay play them: not on TS-Conf, the Sprinter, Scorpion, Profi or with NeoGS fitted. The journals are now recorded on every machine (`_portJournalRecorded`); the gate (`_portJournalValid`) only decides whether v1's replay plays them, so v1 replays as before. Every interrupt vector the CPU takes on a machine with its own INT logic (`IInterruptSource`: TS-Conf, Sprinter) goes into the engine's `BusVectors` journal (one record per acknowledge, port #FFFF; the hook sits in that branch of the CPU only, so other machines pay nothing); a replay from the engine hands the recorded vector back. Vectors are functions of captured state, so this is a check and a search key, and it keeps the replay sealed if a device ever answers otherwise. Tests: `TimeTravelManager_EngineSeekModels_Test` (Pentagon with NeoGS, TS-Conf, Sprinter, Scorpion, Profi, ZX-Evo booting their ROMs: engine seeks land on v1's machine; the engine holds every IN), `InterruptVectorsAreRecordedAndPlayedBack` (TS-Conf in IM 2). Removing the vector hook, or recording the journals only where v1 plays them, fails them. Cost: v1's memory grows on the formerly gated machines by its port journals; the engine stays below v1 on all 46 configurations.

### 4.3 Step 2 — Two replay modes

D14: a replay is driven either by recorded input events or by recorded `IN` values.

| | `Events` (default) | `InValues` (RZX) |
|---|---|---|
| What drives the devices | input events applied at their points; devices answer live | nothing: there are no input events |
| Bus data on the recorded path | handed to the CPU and compared with the live answer; a differing answer is counted (`ValueMismatches`) | handed to the CPU; authoritative |
| When the execution leaves the recording (a branch, an edit, a divergence) | devices keep answering live; recorded bus data stops being used from the first divergence; events still apply | the replay stops and reports a desync, as an RZX player does |
| Used for | recordings with live input; branches and forks, which are fed input events, never `IN` values ([what-if WI-9](../2026-09-29-model-what-if/design.md)) | sessions recorded while an RZX played; RZX import (RZ-F19, RZ-F20, [RZX requirements](../2026-09-29-rzx-replay/requirements.md)) |

- In `Events` mode the recorded values still go to the CPU, as v1 does (TTM:2030-2048). This keeps every restore identical to v1 (D33). The mismatch counter is a determinism check: on the fixture corpus it must be zero once Phase 2 and Step 4 make devices and media exact.
- **The mode belongs to a stretch of history,** not to the session. A `ReplaySourceChange` event marks where an RZX playback starts and ends; a replay takes the mode of the stretch it runs through.
- **RZX frames.** While an RZX plays, each RZX frame end (the forced interrupt) is recorded as an `InterruptFrame` fact with its RZX frame number. The RZX player's position is a device state (RZ-F19). A seek to "RZX frame N" becomes an engine seek to that fact's machine time.
- **RZX's own key-frame store retires** (`RzxKeyframeStore`, [RZX design §18](../2026-09-29-rzx-replay/design.md)): once an RZX playback always runs with the engine recording and `rzx/seek` goes through the engine on every RZX-supported model, the store and its 32 MB budget are removed. The removal itself lands after the switch-over (Phase 5); this step builds the mapping and the parity test.

**Interface.**

```cpp
enum class TTDReplaySource : uint8_t { LiveInput, RzxPlayback };
struct TTDReplayStats { uint64_t valueMismatches; uint64_t divergences; std::optional<TTDPosition> firstDivergence; };
TTDReplayStats TimeTravelEngine::LastReplayStats() const;   // every surface shows it next to the seek result
```

### 4.4 Step 3 — Several CPUs

**Machine time** (D20, Phase 1) stays the main CPU's cycles in top-clock units, so one value names one instant through every main-CPU turbo switch (v1's B4 fix, platform.h:1165-1171). Each secondary CPU keeps its own cycle counter.

```cpp
enum class TTDCpuId : uint16_t   // stable, appended
{
    Main = 0, GeneralSoundZ80 = 1, NeoGSZ80 = 2, Atm2KeyboardMcs51 = 3, Vdac2Ft812 = 4,
};

struct TTDCpuDesc
{
    TTDCpuId id;
    TTDDeviceRef owner;            // Phase 2 device (type id + instance name)
    const char* name;              // "gs.z80", "ngs.z80", "atm2.kbc", "vdac2.ft812"
    uint64_t cyclesPerSecond;      // at registration; changes come as ClockChange events
};

class ISecondaryCpuClock           // implemented by the owning device
{
public:
    virtual uint64_t CycleCounter() const = 0;              // cycles since power-on, 64-bit
    virtual uint64_t InstructionCounter() const = 0;        // for reverse steps in card instructions
    virtual void RunUntilCycle(uint64_t cycle) = 0;          // advance alone, at instruction granularity
};
```

| CPU | Clock | Source |
|---|---|---|
| General Sound Z80 | 12 MHz | [GS debugger requirements §2](../2026-09-27-gs-debugger/requirements.md) |
| NeoGS Z80 | 24, 20, 12 or 10 MHz, switched by `GSCFG0` bits 5:4 from the next instruction; counted in base ticks of 1/120 MHz | soundchip_neogs.h:14-18, :70-75 |
| ATM2 keyboard controller (MCS-51) | 11.0592 MHz | atm2kbc.h:43-50 |
| VDAC2 FT812 | PLL multiple of the 8 MHz crystal: 48-80 MHz in VDAC2 software (D20 says 40-80) | [ft812-behavior-spec.md](../2026-10-01-tsconf-vdac2/ft812-behavior-spec.md) |

**Clock map.** A `ClockChange` fact holds `{cpu, machineTime, cycleAtChange, cyclesPerSecond}`. Between two changes the relation is linear:

```
cycle(m) = cycleAtChange + (m - machineTimeAtChange) × cyclesPerSecond / machineUnitsPerSecond
```

computed with exact integer arithmetic (128-bit intermediate), in both directions. `machineUnitsPerSecond` is the frame's length in units divided by its duration, so it follows the frame table too. The map is the **nominal** relation: a card that catches up in bursts runs cycle 120,000 of a Pentagon frame while the main CPU is at T 34,996, but cycle 120,000 still belongs to main T 35,000 ([GS debugger requirements §2](../2026-09-27-gs-debugger/requirements.md)). Main-CPU turbo switches are `ClockChange` facts for `Main` as well; they need no conversion of machine time but let tools convert to seconds.

**Per checkpoint.** At a frame boundary every secondary CPU has caught up (FR-19). The checkpoint records each CPU's cycle counter as the **difference from the clock map's prediction**: usually zero or a few cycles of instruction overshoot. Unchanged frames therefore add nothing measurable (PR-10).

**Positions naming any CPU** (D15). A position is `{branch, frame, T}` plus an optional `{cpu, cycle}`.
- *Seek to a card cycle `c`:* the clock map gives the machine time `m(c)` and so the frame; restore its checkpoint; replay the main CPU with a read-only analyzer hook (D16) on the card's instruction boundaries. When the card reaches the first instruction boundary at or after `c`, the hook takes a copy of the card's registers and of the pieces of its memory written since the checkpoint.
- *Where the main CPU then rests:* the card may reach `c` inside a burst, that is inside a main-CPU instruction. The machine stops at the next main-CPU instruction boundary; the position reports the card state from the copy, and the main CPU's own position separately. Resuming execution is offered from the main-CPU boundary only (open question Q2).
- *Reverse step in card instructions* (GS debugger D1): the replay window is counted in card instructions. The engine replays the frame that contains `c` once with the hook, collects the cycle of every card instruction boundary in it, and steps along that list; before the frame's first boundary it moves to the previous frame. One instruction of a turbo main CPU can span several card instructions; the list does not care.
- *Card breakpoints in reverse search* (GS debugger D4): the same hook evaluates card breakpoints during replay, which D16 already requires for the main CPU.

**Variable frames.** Phase 1's frame table holds each frame's start. This step adds what ends a frame and what changes its length:
- `FrameLengthChange` fact: a Sprinter `#2C` / `#2D` write takes effect at the next frame boundary;
- a cut ends the frame early (a reset ends the session, D39);
- RZX frames are not video frames ([RZX design §6](../2026-09-29-rzx-replay/design.md)): they are `InterruptFrame` facts, not frame-table entries.

Everything that turned a frame number into time - `GlobalT`, `EmulatedMicroseconds`, the write journal's `globalT` - uses the frame table instead.

**As built (2026-10-03), the engine's frame table.** In shadow mode each frame's start is the last captured frame's start plus that frame's measured length: the base T-states the machine ran since (`emulatorState.t_states` grows by each closed frame) times `ttd_clock_units`, not the frame number times the current length. When a frame's length differs from the one before, a `FrameLengthChange` fact goes into the event log at the boundary (args: the closed frame's length in units, u32). `TimeTravelManager_FrameTable_Test` switches a Sprinter to 312 lines and back mid-recording: every frame is one of the two measured lengths and machine time never goes back; v1's frame x length fails it. v1's own `GlobalT` (its write journal, find-last) and `EmulatedMicroseconds` (the RTC) are unchanged; the RTC moves to the frame table with the session time base (Step 5).

**File consequences.** A CPU table in the header; clock changes in the event log; per checkpoint, the CPU counter residuals (zero in nearly every frame).

### 4.5 Step 4 — Configuration fingerprint and media versions

**Fingerprint.**

```cpp
struct TTDConfigFingerprint
{
    uint16_t model;  uint32_t ramPages;
    uint32_t frameTStates;      // config.frame (platform.h:519)
    uint32_t lineTStates, intStart, intLength;
    uint32_t frameDurationUs;   // platform.h:526
    uint8_t  clockUnits;        // ttd_clock_units
    uint8_t  hardwareTurboConfig;   // e.g. Sprinter turbo allowed, Profi switch at power-on
    uint32_t audioCoreRate;
    bool     decimatorHighFidelity; // platform.h:662
    bool     soundHq, screenHq;
    uint64_t romSignature;
    uint64_t deviceTableHash;   // Phase 2 device table: type ids, instances, layout versions, firmware fingerprints
};

struct TTDFingerprintDiff { std::string field; std::string recorded; std::string live; bool affectsRestore; };
std::vector<TTDFingerprintDiff> Compare(const TTDConfigFingerprint& recorded, const TTDConfigFingerprint& live);
```

- The session keeps a **fingerprint table**; a `ConfigChange` cut (D26) points at a new entry. A model switch starts a new session linked to its parent.
- **On load** and whenever the live configuration changes while a loaded session is open, the engine compares the fingerprint of the current position with the live machine.
- **What a mismatch does** (FR-14, FR-7):
  - restoring a checkpoint stays exact for CPU, memory and devices, unless the device table differs - that is Phase 2's degraded result;
  - every operation that replays (seek inside a frame, reverse step, reverse search, resume, branch switch) runs and returns `notBitExact` with the differing fields;
  - the session is open for inspection either way: state, memory, disassembly.

**Media versions** ([media history §3.2, §7](../2026-09-28-storage-manager/media-history-design.md); [storage manager §9](../2026-09-28-storage-manager/technical-design.md)).
- A media slot table in the session: slot id (`fdd.a`, `sd.zc`, `ide0.master`), kind, the source's `ContentId`, format.
- Each checkpoint lists the version of every medium **that changed since the previous checkpoint**; the change layer cuts a version at the frame boundary after a write ("at most one per medium per frame"; the media manager already marks the first write per frame, mediamanager.cpp:655).
- A checkpoint depends on its versions (D5): the change layer keeps them while a checkpoint refers to them.
- A seek sets each medium's head to the checkpoint's version, so a controller that reads the medium during the replay sees what it saw when recording, and its state where the replay stops matches the recording.
- Inserts and ejects are `MediaChange` cuts (WI-6, D10).
- **Until the change layer exists** (storage manager phases H1 and H5), a version is the source's `ContentId` plus a write count; a medium without versions is reported in the session status (the CPU still reads the recorded bytes, so the replay stays exact; only the controller's state at the stop can differ). No barrier.

```cpp
class IMediaHistory    // implemented by the media manager's change layer
{
public:
    virtual uint64_t CurrentVersion(uint16_t slot) const = 0;
    virtual bool SetHead(uint16_t slot, uint64_t version) = 0;   // seek
    virtual bool HasVersions(uint16_t slot) const = 0;           // false: reported in status, no barrier
};
```

### 4.6 Step 5 — Real-time clocks on an emulated time base

v1 already runs every DS12887 user on emulated time during a recording (§3.2). What is left:

1. **One time base per session.** `TTDTimeBase { int64_t wallMicrosAtStart; uint64_t machineTimeAtStart; }` in the session, taken from the host clock once when the session starts. Every clock device reads `wallMicrosAtStart + MicrosOf(machineTime - machineTimeAtStart)`. Today each chip keeps its own anchor (ds12887.cpp:210-218), so two clock chips in one machine could disagree, and resuming a branch would re-anchor at the host time.
2. **Time from the frame table.** `MicrosOf` sums frame durations from the frame table, not `frame_counter × frame_duration_us` (portdecoder.cpp:237-255). The Sprinter's clock no longer jumps back at a 320 → 312 line switch (§2).
3. **The base survives save and load.** A loaded session and its branches keep the recorded base, so the program reads the same clock after a reload as during the recording.
4. **Covered devices:** the DS12887 class serves ATM3 / ZX-Evo (through the AVR), Profi, Scorpion SMUC, TSConf and Sprinter (portdecoder_atm3.cpp:29, portdecoder_profi.cpp:43, portdecoder_scorpion256.cpp:66, portdecoder_tsconf.cpp:33, portdecoder_sprinter.cpp:46). NVRAM cells are in the chip's state (ds12887.h `kStateSize`), the EEPROMs are Phase 1 regions, so the initial state contains them (D25). Network time (the ESP module's SNTP) arrives as `NetEvent` input already.
5. **Outside a recording** the chip reads the host clock, as today (migration-trajectory risk for Phase 3). With always-on recording (D29) "outside a recording" becomes rare; see Q4.

The clock's `Fixed` mode (tests and the benchmark's frozen RTC) is unchanged.

### 4.7 Step 6 — No writes outside the session during replay

FR-20 asks for one gate in the media layer, keyed by the replay state, not a flag per controller.

```cpp
// core/src/emulator/media/mediawritegate.h  (media layer, shared by both engines)
class MediaWriteGate
{
public:
    static bool HostWritesAllowed(const EmulatorContext& ctx) { return !ctx.ttdReplayActive; }
};
```

- **Guest writes during a replay** still reach the emulated device and the medium's in-memory state, so the replayed program sees what it wrote:
  - with media versions they go into the head set by the seek, a new head that never changes a stored version;
  - without versions they go into a **replay overlay** above the session write map or the floppy image, dropped when the replay ends.
- **Host persistence is refused while the gate is closed:**

| Path | Code |
|---|---|
| write-through to an image file | mediatypes.h:27; `SdCardSpi` opened `ReadWrite` (sdcardspi.cpp:39) |
| session write map save | sessionwritemap.cpp:65 |
| NeoGS flash persistence | soundchip_neogs.cpp:273, :615-623 |
| NVRAM stores | `Ds12887::SaveNvram` (ds12887.cpp:564), `EvoAvr::SaveNvram` (evoavr.cpp:395) |
| device memory save | devicememory.cpp:95-107 |

- A refused persist is retried when the gate opens, with the state at that moment, not the replayed one.
- `ttdReplayActive` is the flag v1 already sets for every replay (TTM:1613-1671), so the gate also protects v1 from the day it lands.
- **Test:** replay history that writes a floppy, an SD card and a hard disk; the image files, the session write maps and the flash file are byte-identical before and after (FR-20).

**As built (2026-10-03).** The paths that wrote a host file during a replay, found by a code survey: the write-through IDE / SD images (`RawImage::WriteSector`, the default for IDE), floppy write-through (written at the frame boundary by `MediaManager::ApplyPending`, which a replay crosses), the VDAC2 bus capture file and the audio of a video recording (its frames were already skipped). Already safe: NeoGS flash and SD changes (refused during a replay), the virtual network, the session write maps and NVRAM (saved only on an explicit save or at shutdown). What changed:
- every write-through block stack ends in `HostWriteHold` (emulator/io/storage/hostwritehold.h). `MediaManager::HoldHostWrites(true)` holds the guest's sector writes in memory, where the replayed program reads them back; `HoldHostWrites(false)` writes them to the file. That is the retry of a refused persist "with the state of that moment" above; the overlay is not dropped, because the machine stands at the replay's target afterwards and its disk must match it. A medium inserted while held is held;
- floppy write-through is skipped while held, the disk stays dirty, and the next live frame writes it;
- `Vdac2Card::CaptureLive()` and `RecordingManager::CaptureAudio` skip replayed traffic;
- `TimeTravelManager::EnterReplayMode` / `ExitReplayMode` hold and release, so v1 is covered now; `MediaWriteGate::HostWritesAllowed` (emulator/media/mediawritegate.h) is the shared rule.

Tests: `HostWriteHold_Test`, `MediaManager_Test.WriteThroughImagesAreHeldWhileReplaying`, `MediaManager_Test.FloppyWriteThroughWaitsWhileReplaying`, `TimeTravelManager_HostWrites_Test` (a guest writing a probe port sees host writes held during a v1 replay and only then). Each of the three mechanisms, removed, fails its test. Not covered: an SD card opened directly by `SdCardSpi::open` in persist mode, which only machines without a media manager do (bare test contexts).

### 4.8 Step 7 — The write journal as a derived index

**Why it can be derived.** With a sealed replay (Steps 1-6), re-running a frame regenerates its writes exactly. Keeping the journal is a speed choice (D17).

**Record.** Memory writes only: port `OUT`s are already in the `OUT` bus journal, so the engine does not store them twice (v1 does, TTM:4776-). Columns, compressed as v1's file blocks:

| Column | Width raw | Note |
|---|---|---|
| machine-time delta | u32 | from the previous record |
| region | u16 | region id (Phase 1): machine RAM, General Sound RAM, ... |
| piece offset | u32 | offset in the region |
| cpu | u16 | `TTDCpuId`: answers "which card instruction wrote this" (GS debugger D2) |
| pc | u16 | of the writing instruction |
| value | u8 | |

A card's writes to its own memory go in with their CPU id. That is built in now and used when the card write hooks of Phase 1, Step 6 feed it.

**Retention policies.**

```cpp
class ITTDWriteIndexPolicy
{
public:
    virtual void OnBlockSealed(TTDWriteBlock&& block) = 0;         // capture
    virtual void OnPositionChanged(uint64_t machineTime) = 0;      // seek / run
    virtual TTDCoverageIntervals Covered() const = 0;              // what can be answered without replay
};
```

| Policy | Keeps | Answers outside what it keeps by |
|---|---|---|
| `Ring` | the newest N bytes of blocks (v1's behavior, compressed) | replay |
| `WholeHistory` | every block (E6's model) | — |
| `Window` | blocks within ±W frames of the current position; older blocks dropped, or never written when W = 0 | regenerating the needed frames by replay, guided by the coverage index |

- **Coverage intervals replace the single "complete" bit** (DF:76-83). The index records the machine-time intervals it holds: from journal switch-on to switch-off, minus what a ring dropped or a window released. Find-last answers inside a covered interval from the index, a "no match" there is final, and only the uncovered parts are replayed. This is the "coverage window" item that used to be Phase 5, Step 4 ([README §5](README.md#5-former-step-names)).
- **Regeneration** replays frame by frame backwards from the query position, skipping every frame whose `Written` coverage set does not contain the address. Regenerated blocks may be kept by the policy, so repeated queries in the same area are fast.
- **The coverage index's place.** It stays whole history under every policy:
  - it is small (0.02-1.15 MB per minute in E6's model);
  - it is what makes `Window` affordable, because it turns "replay every frame" into "replay the candidate frames";
  - its keys gain the region id, so card memory is covered too (GS debugger D4).

  It is a derived index as well and stays optional (switchable, one check per frame).
- **Not sealed means no window.** A configuration whose replay is not sealed yet (a tap of Step 1 missing) cannot regenerate. It may only use `Ring` or `WholeHistory`, and says so in status.

**Experiment E7 — choosing the default.** Run before Phase 4 (D17), as a model on recordings like E6, in `tools/poc/011-ttd-v2-capture-analysis/experiments/e7-write-journal-retention/`.

- *Input:*
  - the six real-use sessions of E6 (1 and 5 minutes), plus the matrix cases with the most writes (48K BASIC at 1,800 writes per frame, ATM450, ZX-Evo with GS512 + MoonSound + TSFM);
  - recorded with a ring large enough not to wrap, so the full write history is known.
- *Policies modeled:* `Ring` at v1's 8,388,608 records; `WholeHistory`; `Window` with W = 0, 50, 250 and 1,500 frames.
- *Query workload:*
  - find-last-write at 1,000 random positions per session;
  - addresses drawn from three classes: written within the last frame, last written 1-60 seconds earlier, last written more than 60 seconds earlier or never;
  - also reverse continue to a write watchpoint.
- *What is computed per policy:*
  - memory and file bytes per minute of history;
  - frames replayed per query, using the coverage sets from the same files.
- *Replay cost per frame:* measured, not assumed. BM-5's replay part on the same configurations (`core-benchmarks`), on an idle host (load < 12), run twice. The 1.3 ms per frame of ttdcoverageindex.h:7-9 is only the order of magnitude.
- *Exactness check:* for 1,000 sampled frames per session, replaying the frame regenerates a write list byte-identical to the recorded one. Any difference is a determinism bug and blocks `Window` on that configuration.
- *Success metric:* the policy with the fewest bytes per minute of history kept, subject to:
  1. find-last p99 at or under the latency bound **L** (Q1);
  2. "no match" answers as final as v1's;
  3. frame overhead within PR-1 (W = 0 also removes the per-write capture cost; E7 reports by how much, from BM-1).

  The result table goes into the experiment's README with the chosen default and the per-configuration exceptions.

## 5. Performance

What runs, and how often:

| Work | When | Cost rule |
|---|---|---|
| `IN` / `OUT` journal | per port access | unchanged from v1: one pointer test in `Z80::in` / `Z80::out`, null when off |
| Interrupt vector | per INT acknowledge, only on machines with an `IInterruptSource` | one pointer test, null when off |
| DMA into memory | per transfer from an outside source (not per byte) | one pointer test in the DMA engine |
| Input, markers, facts | per event (keys: about 10 per second) | an append; nothing per instruction beyond v1's step-work gate |
| Clock map | per clock change | rebuilt only then; conversions run on queries, not while emulating |
| CPU counters | per frame, per registered secondary CPU (0-4) | one residual each; one check per frame when none is registered |
| Media versions | per frame with a media write | the existing per-frame write mark (mediamanager.cpp:655); nothing in frames without writes |
| Write gate | per medium write (sector or track), not per CPU byte | one flag test |
| Write index | per memory write, when on | an append to the open block; when off the hook pointer is null, not a test inside the hook (v1 tests inside, TTM:4758) |
| Coverage index | per access, when on | as v1 |

- **Zero cost when off** (D19): every optional part (write index, coverage, the bus taps a machine does not have) costs one check per frame, never one per event.
- **Measured with:**
  - BM-1 frame overhead per mode, with `Window` W = 0 as an extra mode;
  - BM-3 bytes per frame split into event log, payloads, `IN`, `OUT`, vectors, DMA, CPU residuals, write index, coverage;
  - BM-5 seek latency, split into restore and replay, plus a card-cycle target;
  - counted work: `bm2_work_events`, `bm2_work_bus_records`, `bm2_work_write_records` (deterministic, CI gate).
- **Expected:**
  - on configurations v1 already isolates, the bus journals are v1's blocks, so their bytes equal v1's;
  - the event log replaces 22-byte input records and 14-byte-plus-reason markers with delta-coded records, to be measured with BM-3;
  - an unchanged frame adds no event, CPU residual or media bytes (PR-10).

## 6. Tests

Every test is checked by mutation: it must fail when the mechanism it guards is removed.

| Step | Test | Proves |
|---|---|---|
| 1 | Every v1 fixture's input, external and network sections import into the event log; kinds and times equal, payload bytes equal | the v1 mapping is an identity |
| 1 | Replay a recording with key presses, mouse, GS host commands and network data from every checkpoint; machine hash equals the recording at every frame end. Mutation: skip one kind → fails | every input kind is applied at its point |
| 1 | A payload referenced only by a checkpoint's network device state survives dropping the events before it; restore delivers the same bytes. Mutation: free on event drop → fails | payload retention by dependency |
| 1 | Sprinter and TSConf: record, then seek into frames with interrupts and SD DMA; port journal on; zero divergences. Mutation: skip the vector tap → divergence | bus data beyond `IN` (FR-21) |
| 1 | A debugger edit while recording, then a seek across it: the seek does not stop, memory after equals the recording | edits replay |
| 1 | A reset while recording ends the session (D39); with the setting on, a new session starts, linked to the one it ended | reset ends the session |
| 2 | A session recorded while an RZX plays: seek to RZX frame N via the engine equals `RzxKeyframeStore` seek to N (registers, all RAM, `#7FFD`) on the 16 archive recordings | `InValues` mode; parity before the RZX store retires |
| 2 | `Events` mode on the fixture corpus: `ValueMismatches` = 0 | devices and media are exact; the CPU feed hides nothing |
| 2 | Branch from frame F with the parent's events: the branch continues past the end of recorded `IN` values without a desync | `Events` mode serves branches |
| 3 | GS card: reverse-step five card instructions; registers equal a forward run to the same point (GS debugger D1) | positions naming a card CPU |
| 3 | NeoGS switched from 12 to 24 MHz mid-frame: the clock map converts both ways exactly around the switch | clock changes as events |
| 3 | Sprinter 320 → 312 → 320 lines: machine time and the RTC's microseconds never go backwards; write-journal records are in time order | frame table, not frame × length |
| 3 | An unchanged frame stores zero CPU residual bytes | PR-10 for CPUs |
| 4 | Load a session with another audio rate, decimator and ROM: status lists exactly those fields; a frame-aligned restore is exact; a seek inside a frame reports `notBitExact` on every surface | fingerprint (FR-14) |
| 4 | With media versions: write a sector in frame 103, seek to frame 102 and replay into 103: the read returns the old sector, then the new one | media versions per checkpoint |
| 4 | Without media versions: a seek across a write runs (no barrier), the CPU reads the recorded bytes; status names the medium without versions | sealed replay without versions |
| 5 | Record, save, reload a day later, replay a program that prints the RTC: the same output | one session time base |
| 5 | Two DS12887-style chips in one session read the same instant | one base for all clocks |
| 6 | Replay history that writes a floppy, an SD card and a hard disk: image files, session write maps and the NeoGS flash file are byte-identical. Mutation: remove the gate → fails | FR-20 |
| 7 | `Window` policy: find-last for an address last written outside the window returns the same answer as `WholeHistory` | regeneration is exact |
| 7 | Journal off, on, off, on: find-last inside a covered interval answers without replay; in an uncovered one it replays | coverage intervals |
| 7 | A wrapped ring: a query older than the ring edge replays only the uncovered part | no full replay after a wrap |

**D33 comparison with v1.**
- The oracle restores and replays every frame and points inside frames of the fixture corpus and of the real-use sessions on both engines. Results are byte-identical, including `ValueMismatches` and `Divergences`.
- File bytes per stream are compared on recordings where v1's write-journal ring did not wrap, or per frame of history kept (D33, "same history kept"). The event log, payloads and bus journals are not larger than v1's sections in any case.
- Counted capture work (`bm2_work_*`) is not larger than v1's.

## 7. Order of work

Each item lands as its own commits and passes the full gate:

1. **E7** (model only, no engine code), in parallel with the start of the phase.
2. **Step 6** (media write gate): small, in the media layer, protects v1 at once.
3. **Step 1, part 1:**
   - event log, payload store, the v1 reader for input, external and network sections;
   - the engine's replay applies input and matches v1;
   - the `IN` / `OUT` bus journals with cursors, reusing `TTDPortJournal`.
4. **Step 3:** CPU table, clock map, CPU residuals, `FrameLengthChange`; machine time from the frame table everywhere (write index, RTC).
5. **Step 5:** the session time base, on the frame table.
6. **Step 1, part 2:** vector and DMA taps, one machine per commit (Sprinter, TSConf, NeoGS, ZX Next); reset and edit events; cuts.
7. **Step 2:** replay sources, RZX frame facts, the parity test against `RzxKeyframeStore`.
8. **Step 4:** fingerprint, then media versions behind `IMediaHistory` (no barrier fallback: a medium without versions is reported until the storage manager's H1 / H5 land).
9. **Step 7:** write index with the policy interface, coverage intervals, the default from E7.
10. Phase check: D33 on the matrix; store the Phase 3 baseline; write the results document.

## 8. Risks and open questions

| # | Risk / question | Plan |
|---|---|---|
| Q1 | **User decision:** the find-last latency bound L that E7 must meet (it decides how small the window can be) | E7 reports bytes per minute against p99 latency for every W; the user picks L, or the knee of that curve is taken |
| Q2 | **User decision:** a card-CPU position inside a main-CPU instruction: inspect only (proposed), or also resume from there, which needs a resumable burst in every card emulation | Inspect only; resume from the main-CPU boundary after it |
| Q3 | Media versions depend on the storage manager's change layer (H1, H5) | No barrier either way (sealed replay); until a kind of medium has versions, a controller's internal state where a replay stops may differ, and status says so |
| Q4 | **User decision:** with always-on recording (D29) the RTC runs on emulated time almost always. After a long pause it is behind the host clock. Re-anchor to the host on resume of the live end (a cut, recorded), or stay on emulated time? | Proposed: re-anchor when the live end resumes after a pause, recorded as a `ClockChange` of the time base; replay reads the recorded base |
| Q5 | The CPU still gets recorded `IN` values in `Events` mode, so a device restore bug can hide | The mismatch counter must be zero on the corpus (test above); a non-zero count fails the phase check |
| Q6 | A configuration whose replay is not yet sealed cannot use `Window` | Policy refused with a reason; `Ring` or `WholeHistory` until its taps land |
| Q7 | Event kind numbers collide with another branch | One fixed table, appended, first to master takes the number (the `PeripheralId` rule) |
| Q8 | [Media history §3.2](../2026-09-28-storage-manager/media-history-design.md) still says the media layer truncates the future after a seek back and a write, against D7 | Aligned in that design (its owner): media branches follow TTD branches |
| Q9 | DMA from a medium could be served from media versions instead of recorded bytes | Record the bytes (sealed in both modes); revisit if BM-3 shows DMA payloads large |

## 9. Sources

- Code on master `8ddaf708e`: `core/src/debugger/ttd/` (`timetravelmanager.{h,cpp}`, `ttdinputjournal.h`, `ttdexternalevents.h`, `ttdportjournal.h`, `ttdwritejournal.h`, `ttdcoverageindex.h`, `ttddumpformat.h`, `ttd.ksy`, `ttdds12887.cpp`), `core/src/emulator/io/rtc/ds12887.{h,cpp}`, `core/src/emulator/ports/portdecoder.cpp`, `core/src/emulator/platform.h`, `core/src/emulator/cpu/z80.h`, `core/src/emulator/media/`, `core/src/emulator/sound/chips/neogs/`, `core/src/emulator/emulator.cpp`.
- Decisions and requirements: [engine-decisions.md](engine-decisions.md), [requirements.md](requirements.md), [target-architecture.md §5](target-architecture.md), [migration-trajectory.md](migration-trajectory.md), [current-state.md](current-state.md).
- Measurements: [E5](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e5-heap-split/README.md), [E6](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e6-v1-v2-model/README.md), [experiments overview](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/README.md), [benchmark harness](../../../tools/verification/ttd-bench/README.md).
- Users of the engine: [RZX design](../2026-09-29-rzx-replay/design.md) and [requirements](../2026-09-29-rzx-replay/requirements.md); [GS debugger requirements](../2026-09-27-gs-debugger/requirements.md) (D1-D4); [model what-if design](../2026-09-29-model-what-if/design.md) (WI-6, WI-9, §8).
- Media: [storage manager technical design](../2026-09-28-storage-manager/technical-design.md), [media history](../2026-09-28-storage-manager/media-history-design.md), [TTD and snapshots integration](../2026-09-28-storage-manager/integration-ttd-snapshots.md).
- Machines: [Sprinter goals](../2026-09-28-sprinter/goals-and-requirements.md), [Sprinter frame-length crash](../2026-09-28-sprinter/crash-fb-overflow.md), [FT812 behavior](../2026-10-01-tsconf-vdac2/ft812-behavior-spec.md).
- Port-read journal design: [ttd-port-read-journal.md](../../emulator/design/debugger/time-travel-debug/ttd-port-read-journal.md); reverse-search index: [2026-08-20-ttd-reverse-search-index](../2026-08-20-ttd-reverse-search-index/README.md).
