# Machine state coordinator: architecture

Status: draft, revised 2026-10-10 against [requirements.md](requirements.md) (requirement ids F1-F13, N1-N8 and the
owner decisions there): ports and one wiring function (§1, §3), testing (§10). Facts about today's code:
[current-state.md](current-state.md). No open questions; one item postponed (§12).

## 1. Shape and wiring

The coordinator is an autonomous agent (requirements §0.3, N6-N8): it holds its own mode, queue and log, and talks
to the rest of the emulator only through small interfaces it defines itself (ports, §3). Arrows are compile-time
dependencies.

```
 surfaces (Qt, WebAPI, CLI, Lua, Python, GDB, DeZog, MCP)
        │ IMachineOps: Run / Submit / WouldRefuse / Mode / Log
        ▼
 ┌──────────────── MachineCoordinator (core/src/emulator/coordinator/, one per Emulator) ────────────────┐
 │ policy table · lifecycle · park through IMachineThread · boundary queue · mode · operation log        │
 │ includes: its own headers and the standard library only                                              │
 └──────▲───────────────▲────────────────▲───────────────▲───────────────▲───────────────────────────────┘
        │ implements    │ implements     │ implements    │ implements    │ implements
   IOpOwner        ISessionSink     IModeListener   IBoundaryStep   ICutParticipant
   (media, emulator, (TTD controller) (features,     (media, sound,  (TTD, media,
    sound, network,                    media, sound,  network, TTD)   network, RZX)
    RZX, transfer)                     main loop)

 machine thread (Z80 traps, devices) ──► IMachineEvents (Classify / Note), implemented by the coordinator (Q2)
 TTD controller ──► IModeReporter (its own transitions: black box restart, acceleration stop), implemented by the coordinator
```

Rules:
- the coordinator includes no manager, device or emulator header; it is built as its own library
  (`core-coordinator`) whose include path is its own directory only, so a forbidden include does not compile;
  a test (`CoordinatorWiring_Test`) catches the relative way around it (`"../emulator.h"`);
- a participant includes only the port headers it implements or calls; it never includes `machinecoordinator.h`
  and never calls another participant for a machine-level operation;
- each `MachineOpKind` has exactly one owner (a second registration asserts); capabilities are opt-in, a participant
  registers only the ports it has;
- all links are made in one function, `Emulator::WireCoordinator()` (§3.3), run once after the participants exist;
- the links that go away are listed in §3.4.

The coordinator serializes nothing: device state stays in `TTDSerializable`, the peripheral registry,
`ITTDRegionSource` and the snapshot pipeline.

Lifetime: owned by `Emulator`, created before every participant and destroyed after all of them; participants
register in creation order and unregister in reverse (F10). A participant's port is unregistered before the
participant is destroyed; the coordinator never holds a dangling port (a test, §10).

## 2. Operations

```cpp
enum class MachineOpKind : uint8_t {
    Reset, Autostart, SnapshotLoad, StateTransferIn,
    MediaInsert, MediaEject, MediaDiscard,
    ConfigChange,                      // ROM, GS card, network card, IDE drive kind, speed, turbo
    ToolEdit,                          // memory, registers, ports, sectors - any surface, GDB included
    TtdStart, TtdStop, TimelineSeek, TimelineCut,
    RzxSeek, RzxSnapshot,
};

enum class OpTiming : uint8_t { Now, FrameBoundary };   // Now = machine parked; FrameBoundary = §6 phase 1

struct MachineOp {
    MachineOpKind kind;
    OpSource source;                   // qt, webapi, cli, lua, python, gdb, dezog, mcp, trap, device, test
    bool interactive = false;          // a person can be asked (Qt); everything else is non-interactive
    OpTiming timing;
    OpPayload payload;                 // kind-specific: slot + medium, config key + value, plan, time point, ...
    MediaChoice mediaChoice = MediaChoice::Ask;   // §8.3: Ask, EmptySlot, AsIs, Cancel
};

struct OpResult {
    OpStatus status;                   // Done, Refused, Failed, NeedsDecision
    std::string reason;                // the one reason text every surface shows (F2, F3)
    std::vector<MediaDecision> decisions;   // NeedsDecision: which slots, why (§8.3)
    uint64_t logSeq;                   // entry in the operation log (§7)
};

class MachineCoordinator : public IMachineOps, public IMachineEvents, public IModeReporter {
public:
    // IMachineOps - surfaces
    OpResult Run(const MachineOp&);                 // any thread; blocks until done, refused or failed
    OpTicket Submit(const MachineOp&);              // any thread; lands at the next frame boundary (F5)
    OpResult WouldRefuse(const MachineOp&) const;   // no side effects; same reason as Run (F3)
    OpResult Ask(const MachineOp&);                 // WouldRefuse for a surface about to prepare; logs a refusal
    MachineMode Mode() const;                       // published copy (§5)
    const OperationLog& Log() const;                // §7

    // IMachineEvents - machine-thread direct path (Q2): no queue, no allocation
    PolicyAction Classify(MachineOpKind, OpSource) const;   // what a trap / device event becomes
    void NoteMachineEvent(MachineOpKind, OpSource, std::string_view summary);   // log only
};
```

### 2.1 Lifecycle of `Run`

1. **Policy** (§4): `(kind, mode)` → Allow, Refuse(reason), EndSession(keep | drop), RecordAsEvent. Then the
   owner's `Refusal(op)` ("the drive is busy"). A refusal is logged and returned; nothing else happens, not even a
   park.
2. **Park**: `ParkLease` (RAII) brings the machine to a coherent point; on the machine thread it is a no-op. It is
   the only park primitive: the pause-and-sleep, 100 ms-then-proceed and per-manager variants go away (D3). The
   park is taken **before** the coordinator's lock (an operation that held the coordinator while waiting for the
   machine would wait forever on a machine-thread operation waiting for the coordinator); the rules are asked
   again under the lock and a missing park is taken with the lock released.
3. **Prepare**: the owner stages (parses the snapshot, opens and identifies the image, validates the config,
   settles the outgoing medium's writes). Nothing changes in the machine or the session. Failure → `Abort`,
   `Failed`, logged, the session untouched (F4, U5).
4. **Commit** with the **commit point**: the owner calls `stage.CommitPoint()` right before its first change; the
   coordinator applies the policy's session effect there, once (D42: the session ends only when the operation
   really goes ahead - a snapshot whose plan refuses it keeps the recording). Commit fails before the commit
   point → as a failed Prepare; after it → `Failed` with the effect named in the result and the log. An owner that
   never marks it still gets the effect, after its change, and a note in the log (its contract test fails).
   Nobody else is called: a participant that must react does so through its own port (mode change, boundary
   phase, cut), never by watching other participants.
5. **Settle**: the session hears `MachineReset` when the row says so (reset, autostart); the owner restarts the
   frame itself; release the lease; log.

`Submit` runs steps 1 and 3 at once (so a refusal or a bad file is reported to the caller immediately) and 4-6 in
phase 1 of the next frame boundary.

## 3. Ports

### 3.1 What the coordinator needs from the outside (`coordinator/ports.h`)

As built in M1 (later steps add the listeners below):

```cpp
class IMachineThread {                 // the main loop and the execution arbiter's CPU hold; a fake in tests
public:
    virtual bool OnMachineThread() const = 0;
    virtual bool Park(const char* tag) = 0;   // true: held, Unpark follows; nested holds on one thread are free
    virtual void Unpark() = 0;
};

class IMachineClock {                  // frame, T-state in frame, wall time; a fake in tests
public:
    virtual MachineTime Now() const = 0;
};

class OpStage {                        // what an owner carries from Prepare to Commit
public:
    std::any data; std::string error; std::any output;
    void CommitPoint();                // the owner's mark right before its first change (§2.1 step 4)
};

class IOpOwner {                       // exactly one per OpKind
public:
    virtual std::string Refusal(const MachineOp&) const { return {}; }   // owner-specific: "the drive is busy"
    virtual bool Prepare(const MachineOp&, OpStage&) { return true; }    // nothing changes (F4)
    virtual bool Commit(const MachineOp&, OpStage&) = 0;                 // marks the commit point
    virtual void Abort(const MachineOp&, OpStage&) {}                    // drop what Prepare staged
};

class ISessionSink {                   // the TTD controller
public:
    virtual MachineMode Mode() const = 0;   // read by the rules at each decision; M4 publishes it to listeners
    virtual void Apply(SessionEffect, const std::string& reason) = 0;   // at the commit point
    virtual void MachineReset() = 0;        // after a reset: the machine leaves the recorded timeline
};

class IMachineOps {                    // the coordinator, as surfaces and composite owners see it
public:
    virtual OpResult Run(MachineOp) = 0;
    virtual OpResult WouldRefuse(const MachineOp&) const = 0;
    virtual OpResult Ask(MachineOp) = 0;   // a surface's question before its own preparation; a refusal is logged
    virtual MachineMode Mode() const = 0;
};
```

Later steps:

```cpp
class IModeListener   { public: virtual void OnModeChanged(MachineMode from, MachineMode to) = 0; };
class IBoundaryStep   { public: virtual void OnBoundary(BoundaryPhase) = 0; };   // registered with a phase and an order
class ICutParticipant {
public:
    virtual CutPlan Plan(const TimelinePoint&) = 0;                       // no side effect (§8.3)
    virtual void Apply(const TimelinePoint&, const CutDecisions&) = 0;
};
```

### 3.2 What the coordinator offers

| Port | Used by | Calls |
|---|---|---|
| `IMachineOps` | every surface, Qt | `Run`, `Submit`, `WouldRefuse`, `Mode`, `Log` (§2) |
| `IMachineEvents` | Z80 traps, devices on the machine thread (Q2) | `Classify`, `Note` (no queue, no allocation) |
| `IModeReporter` | TTD controller | its own transitions (black box restart, acceleration stop, replay start / end) |

### 3.3 The wiring function

```cpp
void Emulator::WireCoordinator()       // the whole graph in one place
{
    _coordinator->SetMachine(_mainLoopPort, _clockPort);
    _coordinator->Own({Reset, Autostart, SnapshotLoad, StateTransferIn, ToolEdit}, _emulatorOps);
    _coordinator->Own({MediaInsert, MediaEject, MediaDiscard}, *_mediaManager);
    _coordinator->Own({ConfigChange}, _configOps);            // dispatches by key to sound / network / core
    _coordinator->Own({RzxSeek, RzxSnapshot}, *_rzxSession);
    _coordinator->Own({TtdStart, TtdStop, TimelineSeek, TimelineCut}, *_ttdController);
    _coordinator->SetSession(*_ttdController);
    _coordinator->ListenModes({_featureManager, _mediaManager, _soundManager, _mainLoopPort});
    _coordinator->AtBoundary(BoundaryPhase::Apply, {_mediaManager, _soundManager, _networkManager});
    _coordinator->AtBoundary(BoundaryPhase::Checkpoint, {_ttdController});
    _coordinator->AtBoundary(BoundaryPhase::Inject, {_automationInput, _networkManager, _portDecoder});
    _coordinator->CutWith({_ttdController, _mediaManager, _networkManager, _rzxSession});
    _ttdController->SetModeReporter(*_coordinator);
    _context->pMachineEvents = _coordinator.get();
}
```

The names are illustrative; the shape is the rule: one function, ports only, the order of each list is the order of
calls.

### 3.4 Links that go away

| Today (current-state §2) | After |
|---|---|
| `Emulator::LoadTape / LoadDisk / CreateDisk`, snapshot load, reset, autostart call `OnLoad` / `EndSession` | the policy's session effect, once, through `ISessionSink` |
| `MediaManager::CheckRecording` / `EndSessionForMediaChange` → `RecordingGuard`, `OnLoad(Media)` | gone; the media manager is an `IOpOwner` and knows nothing of TTD for operations |
| `SoundManager`, `SlotManager`, CD, `MachineStateTransfer` → `RecordingGuard`, `OnConfigurationChange`, `OnModelTransfer` | gone; policy cells |
| `NetworkManager`, IDE drive kind, RZX, NeoGS SD → raw `IsRecording()` | gone; policy cells (D43 in M2) |
| TTD → `FeatureManager` (debug mode, acceleration lock), `MediaManager::HoldHostWrites`, sound output hold | `IModeListener` reactions to the published mode |
| `ttdReplayActive` read by MainLoop, media, features, Z80, Emulator | a read of the published mode |
| ten park variants (`Pause` + sleep, 100 ms time-out, `ParkedEmulator`, `RunWhileParked`, ...) | `IMachineThread::Park` through `ParkLease` |
| hard-coded boundary list in `MainLoop::CompleteFrame` | `IBoundaryStep` registrations by phase |

Links that stay, on purpose (hot path, recording data rather than operations): the TTD read journal on media reads
(`IMediaReadJournal`), port journals, `NoteWrite` markers, `RecordExternalEvent` / `SubmitLiveInput` from devices.
They carry data into the recording; they decide nothing.

| Participant | Ports |
|---|---|
| TTD controller | `IOpOwner` (TTD verbs, seek, cut), `ISessionSink`, `IBoundaryStep` (Checkpoint), `ICutParticipant`; calls `IModeReporter` |
| MediaManager | `IOpOwner` (media ops), `IModeListener` (host-write hold), `IBoundaryStep` (Apply), `ICutParticipant` (undo log, identity) |
| SoundManager | `IModeListener` (output hold), `IBoundaryStep` (Apply: GS switch); config keys through the config owner |
| NetworkManager | `IBoundaryStep` (Apply: refit; Inject: host answers), `ICutParticipant` (queued answers) |
| RZX session | `IOpOwner` (RZX seek / snapshot), `ICutParticipant` (keyframes after the point) |
| FeatureManager | `IModeListener`: the single writer of debug mode and the acceleration lock |
| Main loop | `IMachineThread`, `IMachineClock`, `IModeListener` (render skipping) |

## 4. Policy table

One file (`machinepolicy.cpp`), a `constexpr` table indexed by `MachineOpKind` (a `static_assert` ties its size to
the enum, so a new kind without a row does not compile), one column per mode: Live, Recording (explicit),
Recording (black box), Replaying, Detached. It replaces `RecordingGuard`, the raw `IsRecording()` refusals,
`OnLoad`'s per-kind branches, `OnConfigurationChange` and `EndSessionForMachineChange`. M1 fills it with today's
behaviour, read from the code, and pins it with a test walking every cell; later changes are one cell plus that test.

| Operation | Recording (explicit) | Black box | Replaying / detached |
|---|---|---|---|
| Reset, autostart | EndSession(keep) | EndSession(keep) | EndSession(keep) |
| Snapshot load | EndSession(keep) | EndSession(drop) | EndSession(keep) |
| Media insert / eject / discard | an event of the recording at the next frame boundary (M3, Q4) | EndSession(drop) | EndSession(drop) |
| ROM, GS card | EndSession(drop) | EndSession(drop) | EndSession(drop) |
| Speed, turbo | Refuse (lock) | black box steps aside, restarts | Invalidate |
| Network settings, IDE drive kind | Refuse | EndSession(drop) (D43, M2) | Refuse |
| Tool edit (any surface) | RecordAsEvent | RecordAsEvent | Refuse |
| RZX seek back / snapshot block | EndSession(keep) | EndSession(keep) (D43, M2) | EndSession(keep) |
| Timeline seek | pause recording (D8), seek | same | seek |
| Timeline cut | — | — | cut (§8), resume recording |

Machine-thread sources (Q2) are rows too: a fast-loader trap or the autostart name hook → RecordAsEvent (tool
edit); a media write note → event (barrier); tape control → input event. The call sites ask `Classify` once per
event instead of carrying their own rule.

## 5. Modes

The coordinator owns `MachineMode` {Live, Recording, RecordingBlackBox, Replaying, Detached}; TTD reports its
transitions to the coordinator, which broadcasts `OnModeChanged`. Owners react: media hold host writes while
Replaying, sound holds host output, FeatureManager sets debug mode and the acceleration lock, MainLoop skips render.
`ttdReplayActive` and `FeatureManager::isTtdRecordingActive` become reads of the published mode.
**As built (M4, §11.6):** the broadcast and `FeatureManager`'s debug mode; the media and sound holds stay direct
(their order reverses on the way out), `ttdReplayActive` keeps TTD as its one writer.

## 6. Frame boundary

`MainLoop::CompleteFrame` calls `coordinator.Boundary()` where it now calls `ApplyPending`, the network devices and
`OnFrameBoundary`:

1. **Apply**: submitted operations in submission order (media swaps, GS card switch, network refit, config).
   Today: `MediaManager::ApplyPending`, `SoundManager::handleFrameStart`, `OnNetworkFrameDevices`.
2. **Checkpoint**: TTD captures; operations from phase 1 are events of this boundary (closes gap 13; makes Q4
   possible).
3. **Inject**: automation input, network host answers, decoder frame end.

A test fixes the order (T3). Phase 1 runs on the machine thread, so operations applied there need no lease.

## 7. Operation log (F8, owner decision: always on)

- A ring of the last 1024 entries per instance (~64 KB), always on, with or without TTD.
- Entry: `seq`, frame, T-state in frame, wall time, kind, source, interactive, summary (slot + file, config key and
  value, edit address + length), result (Done / Refused + reason / Failed + error / decision taken).
- Written by `Run` / `Submit` (every outcome, refusals included) and by `NoteMachineEvent` from the direct path.
  Machine-thread events are rare (traps, tape control, at most one media write note per slot per frame); a mutex is
  enough, measured in the N1 benchmark.
- Read: status on every surface (`GET /api/v1/emulator/operations`, CLI `ops`, Lua / Python, MCP), included in bug
  reports.
- TTD: operations whose frame falls inside a session are saved in the session file as an **ancillary** stream
  (`TTDStreamKind::Ancillary`: older readers skip it), so a loaded session shows what was done and when (T2).

## 8. Timeline cut and media

### 8.1 Undo log

While a session records, the first write to a unit after each checkpoint saves the unit's previous content, keyed
by checkpoint index:
- block media: the 512-byte sector, taken where every write passes (`SessionWriteMap`, `HostWriteHold`);
- floppies: the whole track (~6.25 KB) before its first write in that checkpoint (WD1793 / uPD765 write into the
  track buffer; the hook is the drive slot's `NoteSlotWrite`, which already runs before the write).

Cost = what the guest writes, not the image size. Kept in memory with the media manager, dropped when the session
ends; not in the TTD session file (§12).

### 8.2 Identity (F12, owner decision: lazy)

`MediaIdentity { path, size, contentHash, state: Pending | Ready | Failed }` per attached medium:
- floppies, tapes, images up to a threshold (start: 64 MB, to be measured): hashed at attach;
- larger images: hashed in a background thread when a TTD session starts (≈1-2 s per GB, measured in M5);
- host folders: the existing folder snapshot hash (names, sizes, mtimes);
- replaces the path + size `ContentId` of raw images and the 0 of floppies and tapes in TTD's media check.

Within one run the medium is proven when it is the same attached `Medium` (never re-inserted) and its host file's
size and mtime are what our last write left; an outside edit of a write-through file during the run is the accepted
low risk of requirements §3a.

### 8.3 The cut

`MachineOp{TimelineCut, at}`:
1. **Plan**: each participant's `PlanTimelineCut(at)`. The media manager reports, per slot: rollback possible
   (proven, undo log covers `at`), or not proven (with the reason: missing file, different hash, folder changed,
   different medium attached).
2. **Decide** (owner decision): all proven → proceed. Otherwise an interactive op returns `NeedsDecision` with the
   slots; Qt asks (empty slot / current medium as it is / cancel) and calls `Run` again with the choice. A
   non-interactive op takes `EmptySlot`. The choice is logged. A rollback onto an unproven medium never happens (F11).
3. **Apply**: TTD truncates after `at` (today's `ResumeRecordingFrom`); media apply the undo log backwards down to
   `at` and drop the rest; network drops queued host answers after `at`; RZX drops keyframes after `at`.
4. **Write-through media** (F13, owner decision): at the cut the medium switches to session access; the rollback
   and every later write go into its change layer; the host file keeps its content until the user saves. A
   write-through floppy likewise stops being rewritten at the boundary.

A plain seek is not a cut: nothing outside the CPU machine changes while browsing.

## 9. Threading (N2, D3)

- Any thread may call `Run`, `Submit`, `WouldRefuse`, `Mode`, `Log`.
- Machine state changes only on the machine thread (phase 1) or under a `ParkLease`.
- One coordinator mutex orders operations; `Run` from the machine thread itself (traps, RZX playback end) runs
  inline without parking.
- Mode and log are published copies for readers on other threads.

## 10. Testing (N8)

The coordinator is tested as a unit first, the participants against their ports second, the wiring end to end last.
Each level catches what the one below cannot.

### 10.1 The coordinator alone

Tests in `core/tests/coordinator/` use the library and fakes only, no emulator; each well under 1 ms, no sleeps, no
waits on real time.

| Fake | Records / controls |
|---|---|
| `FakeMachineThread` | `Park` / `Unpark` calls, "on the machine thread" switch, a manual `Boundary()` |
| `FakeClock` | the time each log entry gets |
| `FakeOwner` | `Refusal`, `Prepare` (succeeds / fails / throws), `Commit`, `Abort` calls with the op, in one shared call log |
| `FakeSession` | `EndSession` (keep / drop, reason), `BeginEvent` / `EndEvent` |
| `FakeListener`, `FakeStep`, `FakeCut` | mode changes, boundary phases, plans and applies, in the shared call log |

The shared call log makes each test one comparison: the expected sequence of calls across all fakes.

| Area | Tests |
|---|---|
| Policy | a walk over every (kind, mode) cell and every machine-thread source against an expected table written in the test, not copied from the code; M1's table equals today's behaviour (read from current-state §2-3) |
| Lifecycle | refused → no owner call, no park, logged; `Prepare` fails or throws → no `Commit`, no session effect, `Abort` called, park released, logged `Failed`; success → the policy effect once at the owner's commit point, `MachineReset` after a reset; a commit that fails before or after its commit point; an owner that never marks it; `NeedsDecision` → nothing applied, a second `Run` with the choice applies |
| `WouldRefuse` | for every cell: the same status and reason as `Run`, and the call log is empty |
| Parking | `Park` and `Unpark` balanced on every path, failures and exceptions included; no park on the machine thread; nested `Run` from the machine thread runs inline |
| Ordering | submitted ops apply in submission order in phase 1; phase order Apply → Checkpoint → Inject; an op applied in phase 1 is an event of that boundary's checkpoint |
| Modes | each transition broadcast once, in registration order; derived flags (replay active, acceleration lock) follow |
| Log | every outcome logged with seq, time, source; the ring wraps at 1024 with seq still increasing; readers on other threads see whole entries |
| Wiring | a second owner for a kind asserts; a port unregistered before its participant dies is never called again; an op kind without an owner is refused with a reason |
| Threads | `Run` / `Submit` / `WouldRefuse` from several control threads against a fake machine thread running boundaries; same results as serial; run under TSan in the Linux docker job (the library is small enough) |

### 10.2 Each real participant against its port

Typed contract tests, one suite per port, instantiated for each real implementation on a small booted machine:
- `IOpOwner`: `Refusal` and `Prepare` change nothing (machine state hash, media state and files before and after);
  `Prepare` + `Abort` leaves no trace; `Prepare` + `Commit` gives the same machine as today's direct call;
- `ICutParticipant`: `Plan` changes nothing; `Apply` at point P leaves its state equal to what it was at P;
- `IModeListener`: every mode reached and left restores the participant's settings;
- `IBoundaryStep`: work done in its phase only.

A new participant gets these tests by adding one line to the instantiation list.

### 10.3 End to end

A few tests on real machines check what the levels below cannot:
- every surface (WebAPI, CLI, Lua, Python, GDB, DeZog, MCP) gives the same status and reason text for each refused
  cell (table-driven over the surfaces);
- the boundary order on a running machine;
- a disk swapped during a recording, seek back across it, replay equal to live (M3);
- "Rec From Here" after the guest wrote the disk: the disk returns to the point; a deleted or edited medium gives
  `NeedsDecision` (interactive) or an empty slot (non-interactive) (M5);
- the operation log of a TTD session saved and loaded back.

### 10.4 Each landing step

- mutation checks: a flipped policy cell, a skipped `Unpark`, a swapped boundary phase, a missing log entry must each
  fail a test;
- the include check of §1 in the build;
- A/B frame benchmark for M1, M3, M5 (N1).

## 11. Migration

| Step | Content | Behaviour |
|---|---|---|
| M0 | Defects of current-state §7: GDB edits as tool edits, `SetBlackBox` under the session lock, a load ends the session only after a successful insert, the controller unbinds the media read journal and vectors on destruction, debug mode writers listed for M4 | bug fixes; **done 2026-10-10** (11e394ee1, af19cac62, 4c90c418d, ad38d73b2) |
| M1 | The `core-coordinator` library with its ports, fakes and §10.1 tests; `ParkLease`, operation log, policy table with today's cells; `Emulator::WireCoordinator`; reset, snapshot load, media ops through `Run` with their §10.2 contract tests; the links of §3.4 for these operations removed | none; the log appears. **Built 2026-10-10** (§11.1) |
| M2 | Every refusal from the table; raw `IsRecording()` checks deleted; D43 cells (network, IDE drive kind, RZX, NeoGS SD) one at a time with the owner | the black box stops blocking (U3). **Built 2026-10-10** (§11.2-§11.4) |
| M3 | Boundary phases; media, GS switch, network into phase 1; media ops during a recording become events (Q4) | U2, gap 13. **Built 2026-10-10** (§11.5); after it the media undo log, gaps 11-12 (§11.5, last items) |
| M4 | Modes broadcast; one writer per setting | none. **Built 2026-10-10** (§11.6) |
| M5 | Media identity (8.2), undo log (8.1), the cut (8.3) on Qt "Rec From Here", WebAPI / CLI resume; the dialog in Qt | U1. **Built 2026-10-11** (§11.7; the undo log 2026-10-10, §11.5) |
| M6 | Remaining operations (state transfer, config, tool edits, RZX, TTD verbs) through `Run`; `OnLoad` / `OnConfigurationChange` removed from the TTD hooks | none. **Built 2026-10-11** (§11.8) |

Each step lands alone: build, `test.sh`, docker Linux, gcc:16 -O3 of changed files; M3 and M5 with an A/B frame
benchmark (N1). M1 needs none: it touches no per-frame or per-instruction code, only reset, snapshot load and media
operations.

### 11.1 M1 as built (2026-10-10)

- `core/src/emulator/coordinator/`: `machineop.h` (kinds, sources, modes, effects), `ports.h` (`IMachineThread`,
  `IMachineClock`, `OpStage`, `IOpOwner`, `ISessionSink`, `IMachineOps`), `machinepolicy.cpp` (the table, M1 rows:
  reset, autostart, snapshot load, media insert / eject / discard, disk create), `operationlog.cpp` (the ring of
  1024), `machinecoordinator.cpp` (`Run`, `WouldRefuse`, `Mode`, wiring). Its own library `core-coordinator`, include
  path its own directory only.
- `Emulator`: creates the coordinator first; `WireCoordinator` (called in the constructor and again when the media
  manager exists) links everything; `CoordinatorPorts` is the machine thread (the execution arbiter's CPU hold, the
  loop thread), the clock and the owner of reset, autostart and snapshot load (`ResetNow`, `AutostartNow`,
  `LoadSnapshotNow`); `MachineOps()` and `GetOperationLog()` for callers.
- `MediaManager`: owner of the media operations (`MediaOpOwner`); its public `Insert` / `Eject` / `Discard` run
  through `IMachineOps`; `CheckRecording` and `EndSessionForMediaChange` are gone. Without an emulator
  (`MediaManager(nullptr)` in tests) the owner runs alone, no rules.
- `ttd::TTDSessionPort`: the TTD side; the mode from today's questions (the recording guard, replay, detached,
  recording), each effect onto the hook call its callers made (`EndSession`, `OnLoad(Media)`, `StopRecording`,
  `OnMachineReset`), for whichever backend `pTimeTravelHooks` points at.
- `Emulator::LoadTape` / `LoadDisk` / `CreateBlankDisk` no longer ask the recording guard: the rules refuse in
  `MediaManager::Insert`, before the file is opened, logged, with the same sentence as before.

Differences from the target design, on purpose for M1:
- the mode is read from `ISessionSink::Mode()`; M4 added the broadcast for listeners, the rules keep reading it
  (§11.6);
- RZX playback is still stopped by the callers of reset and snapshot load (RZX joins in M6);
- the ROM reload inside a reset still calls `OnConfigurationChange` itself (configuration changes join in M6);
- the log is read in core only (`Emulator::GetOperationLog`); the surfaces follow.

One behaviour change: a media change outside live mode (black box, replay, detached) now parks the machine first,
so it never waits for the machine while holding the coordinator (§2.1 step 2). Such a change from a surface now
applies at once, with the machine held, instead of at the next frame boundary.

Tests: `MachinePolicy_Test` (every cell), `MachineCoordinator_Test` (the lifecycle with fakes, threads),
`OperationLog_Test`, `CoordinatorWiring_Test` (includes), `MediaOwnerContract_Test` (the media owner against
`IOpOwner`), `CoordinatorEmulator_Test` (a real machine: reset, snapshot, tape, discard, eject).

### 11.2 M2, first part as built (2026-10-10)

- **Every refusal is logged.** The surfaces no longer ask TTD (`RecordingGuard`) before a tape load, a disk load or a
  blank disk: they ask the coordinator, `Emulator::AskMediaChange` -> `IMachineOps::Ask` (the rules and the owner, as
  `WouldRefuse`; a refusal is logged like `Run`'s, with the same sentence; a yes is not logged, `Run` logs the
  operation). WebAPI tape / disk load and create (409), CLI `tape load`, Lua `tape_load`, Python `tape_load` /
  `disk_create`, the GDB monitor load and Qt (drop, open, audio import) ask before their own preparation; a surface
  without one calls the operation and `Run` refuses it.
- **The log names the source.** `coordinator::SetThreadSource` / `SourceScope` (thread-local); `Run` and `Ask` take
  it for an operation that names none. Marked: the Qt main thread, the Lua and Python interpreter threads, each CLI
  client thread, each GDB session thread, the DeZog snapshot restore, every WebAPI request (a pre-handling advice:
  `webapi`, or `mcp` when the request carries `X-Unreal-Source: mcp`, which the MCP server's client sends). The
  emulator's own threads stay `unknown`.
- Model switches and state transfers: see §11.4.

### 11.3 M2, the D43 cells as built (2026-10-10)

Owner decisions: [requirements.md §6](requirements.md#6-owner-decisions). The four raw `IsRecording()` refusals are
gone; each is a row of the policy table or the media rows:

| Operation | Kind | Live | Recording | Black box | Replaying / detached |
|---|---|---|---|---|---|
| Network settings (`NetworkManager::RequestChange`) | `NetworkChange` | allowed | refused | EndSession(drop) | refused |
| IDE unit's drive (`IdeController::SetUnitKind`) | `IdeDriveKind` | allowed (held) | refused | EndSession(drop) | refused |
| RZX snapshot block, seek back (`RzxSession`) | `RzxRestore` | EndSession(keep) | EndSession(keep) | EndSession(keep) | EndSession(keep) |
| NeoGS SD insert / eject | `MediaInsert` / `MediaEject` | media rows | | | |

- `coordinator::RunCommit` (ports.h): a subsystem hands its change to the coordinator as an `OpCommit`; the emulator
  owns these kinds and calls it as the Commit; it marks the commit point before its first change. Without a
  coordinator (a bare context in a unit test) the change runs alone. The subsystems reach the coordinator through
  `EmulatorContext::pMachineOps`, set in `WireCoordinator`.
- `RemoteAccess` alone is no operation (it moves the host listeners only). A forward RZX seek plays on: no
  operation. NeoGS keeps its replay input guard (`ttdReplayActive`: a TTD re-execution owns the machine).
- The slot control's network reply says `recording` when the coordinator's mode is a recording the user started or
  the recorded past.

Tests: `MachinePolicy_Test` (every cell, the sentences), `CoordinatorEmulator_Test` (network refused / ends the black
box, `RemoteAccess` free; an IDE unit's drive; an RZX seek back during a recording ends it and keeps the history),
`NeoGSMedia_Test` (the black box ends at an SD change).

### 11.4 M2, model switches asked of the coordinator (2026-10-10)

A model switch (a snapshot of another model, `SnapshotLauncher`) and a machine state transfer (WebAPI
`.../snapshot/transfer`, `MachineStateTransfer::Transfer`) no longer ask TTD: they ask the coordinator,
`Emulator::AskModelSwitch` -> `IMachineOps::Ask` with kind `ModelSwitch`. The rules allow it in every mode (a change
of the machine itself ends the session, owner rule 2026-10-05); TTD v1 still refuses it while recording (D26), as the
emulator's owner refusal - it goes with v1. A refusal is logged with its source; a yes is not: the switch replaces
the instance and its coordinator (Q1), it does not run through `Run`. Test: `CoordinatorEmulator_Test` (the engine
says yes, v1 refuses and the log has it). No surface asks TTD's `RecordingGuard` any more.

Tests: `MachineCoordinator_Test` (a source from the thread, a scope, another thread; `Ask` logs a refusal with its
source and nothing else), `CoordinatorEmulator_Test` (a surface's question on a real machine: same sentence, logged),
`test_api_operations.py` (sources over WebAPI / Lua / Python, a refused `tape/load` logged).

### 11.5 M3 as built (2026-10-10)

Owner decision (2026-10-10): **the history keeps that a medium changed and what came in, then ports and sectors
only** - no images in the history, no medium versions to go back to.

- **Policy**: while a recording the user started runs (also paused for browsing), insert, eject, discard and a blank
  disk are allowed: `EventUnlessEnd` cells, no session effect at the commit, no park; `PolicyDecision::atFrameBoundary`
  -> `OpStage::AtFrameBoundary()`: the media owner never applies the change inside a frame, also on a parked
  machine - it lands in `MediaManager::ApplyPending`, at the next frame boundary the machine runs. A caller that asks
  to end the recording ends it (`StopThenMediaChanged`), as before. The black box: unchanged (its session ends).
- **Event**: each landing (eject, attach, discard) in `ApplySlot` calls `ITimeTravelHooks::NoteMediaChange(slot,
  what)`; the controller keeps it as a `MediaChange` cut (0x0201) at `{frame, 0}`, payload "slot what". The landing
  precedes the boundary's checkpoint (`CompleteFrame`: `ApplyPending`, network devices, then `OnFrameBoundary`), so
  the checkpoint holds the devices after the change, and a replay or a run on the history that reaches the boundary
  restores it (the existing cut path). A change queued before the recording that lands during it is recorded too
  (gap 13). A swap delay gives two events (the eject, later the attach), each its own (Q4).
- **The history before a medium**: each slot keeps the frame its medium came in (`landedFrame`); on the recorded
  history (a replay, a seek, the machine detached in the past) the slot is empty for its device before that frame
  (`MediaManager::FollowReplay`, from the replay restore and every `ApplyPending`): the CPU gets the recorded
  exchange, and the guest's writes of that time never reach a medium that came later. Back live the medium is
  there again; a recording resumed from the past takes it from there.
- **TTD v1** keeps no such events: `ITimeTravelHooks::RecordsMediaChanges()` is false, and the media owner's refusal
  refuses as before (goes with v1).
- Boundary phases: the order in `CompleteFrame` already is phase 1 (media, GS switch in `OnFrameStart`, network
  devices), phase 2 (checkpoint), phase 3 (input, host answers, decoder); no `Boundary()` indirection was added.
- After M3 (owner 2026-10-10: an undo log, not the change layer H1 / H5), gaps 11-12: while a TTD history exists
  the media manager keeps every change of a medium in the slot's `MediaUndoLog` - a block sector (the read tap tells
  it before the write), a floppy track (the controller, before it writes), the write-protect switch. A version is the
  count of the medium's changes. The restore sets every medium to its checkpoint's version before the devices load
  (`TimeTravelController::SetMediaToCheckpoint`); a replay's writes move the version along the history; a write after
  a seek back drops the versions above. A write-protect toggle while recording lands at the next frame boundary as an
  event and a version of its own, which a replay meets at its frame. `MediaVersionDiffers` is left for another medium
  and for versions past the log's budget (64 MB per slot). TTD v1 reports as before and never sets a medium back.

Tests: `MachinePolicy_Test` (cells, `atFrameBoundary`), `MachineCoordinator_Test`
(`AMediaChangeWhileRecordingLandsAtTheNextFrameBoundary`), `MediaOwnerContract_Test`, `MediaManager_Test`,
`CoordinatorEmulator_Test` (`ADiskChangedWhileRecordingIsAnEventAtTheFrameBoundaryWhereItLands`,
`BeforeTheFrameADiskCameInTheHistoryHasNoDiskThere`; v1 refusals), `TapeSlot_Test`, `NeoGSMedia_Test`,
`TTD_SessionLifecycle_Test`, `TimeTravelController_Test`, `TTDAtaChannel_Test`, `ZXEvoSdCardTtd_Test`.

## 12. Postponed

**Continuing live from the past in a session loaded from a file** (owner decision 2026-10-10: not supported for now,
postponed; TTD is not to grow without a clear need). A loaded session is replayed and browsed from its journals.
"Rec From Here" in it treats every medium as unproven: Qt asks (empty slot / current medium as it is / cancel), a
non-interactive op leaves the slot empty and logs it. The way back if it is ever needed: an ancillary stream with
the guest's media writes (old and new content per written unit), sized by what the guest wrote.

Owner decisions are recorded in [requirements.md §6](requirements.md#6-owner-decisions).

### 11.6 M4 as built (2026-10-10)

- **Mode broadcast.** `IModeListener::OnModeChanged(from, to)` and `ModeState {mode, replaying}` in `ports.h`;
  `MachineCoordinator::Listen` / `Unlisten`, `SessionChanged`, `Published`. TTD reports (`ttd::NoteSessionChange`,
  `ttdsessionport.h`): every state transition (`SetState`, v2 and v1), a replay entered or left, a recording paused
  or resumed, the background flag. The coordinator reads the mode again, publishes it and tells the listeners once
  per transition, in registration order, on the reporting thread; nothing when nothing changed. It does not take
  the ports' lock (TTD reports from inside an operation's session effect too). `replaying` is
  `ISessionSink::Replaying()`: the machine executes recorded history now, also inside a paused recording the user
  started (whose mode stays Recording).
- **The rules still read the mode when they decide** (`ISessionSink::Mode()` under the operation's lock): the
  published mode is for the listeners and readers; a decision needs the mode at its own moment, which the
  §10.1 tests pin.
- **One writer per setting: debug mode** (current-state §7 item 6). `FeatureManager` listens; it holds debug mode
  on while the machine replays (`debugModeInEffect`: the feature, or the hold) and is the one writer of
  `Z80::isDebugMode`. The v2 and v1 replays no longer save and restore it; `Emulator::DebugOn` / `DebugOff` and the
  CLI `debugmode` go through the feature. A feature change during a replay keeps the hold. A context without a
  coordinator (a unit test's bare context) tells the feature manager directly.
- **Not moved:** the media hold of host writes and the sound host hold stay direct calls in `EnterReplayMode` /
  `ExitReplayMode`: their order is reversed on the way out (the hold goes first, then the flag), which a broadcast in
  registration order cannot give. `ttdReplayActive` keeps its one writer (TTD). The TTD recording lock in
  `FeatureManager` (`onTtdRecordingStarted` / `Stopped`) stays as it is.

Tests: `MachineCoordinator_Test.AModeTransitionIsBroadcastOnceInRegistrationOrder` (fakes: `FakeListener`),
`CoordinatorEmulator_Test.AReplayHoldsDebugModeAndTheFeatureHasItsSayAfter`.

### 11.7 M5 as built (2026-10-11)

- **The undo log does the rollback** (§11.5): a seek, and so "Rec From Here", sets every medium to its version at
  that point; the first write after it drops the medium's old future. §8.3's "a plain seek changes nothing outside
  the CPU machine" no longer holds for media: owner decision 2026-10-10 (gaps 11-12), a replay must read the disk
  it read when recording.
- **Identity (F12).** `Medium::Identity`: an FNV-1a hash of the source file's bytes when the manager opens it, for
  files up to 64 MB; larger images and folders keep `VolumeId` (path and size; the folder snapshot). The background
  hash of large images is not built. A version is `lineage << 32 | count`: the lineage is one insert of a medium
  (random start, unique in the run and across runs), the count its changes since. Two lineages never meet, except
  at count 0 with the same identity: the untouched medium is the same content (F11). Another run's or another
  insert's writes are never taken for this medium's.
- **The cut (F11).** `TimeTravelController::PlanCut(at)` lists, without side effects, the media that cannot go
  back (`IMediaHistory::Reaches`): another medium in the slot, or a version past the undo log. `TTDControl`'s
  `resume` takes `media`: `ask` (Qt) answers `needs_decision` with the slots and changes nothing; `empty` (the
  default where nobody can be asked: WebAPI, CLI, scripts) ejects it, an event of the recording that goes on;
  `keep` leaves it. A medium with unsaved writes is not ejected: the reply and the log say "kept". Each decision is
  logged as `OpKind::TimelineCut` through `IMachineOps::Note` (the cut itself runs on TTD's path, not `Run`: TTD's
  session lock and a park do not nest). Qt's Rec From Here asks with a dialog (empty the slots / keep them / cancel).
- **Write-through (F13).** A write-through medium that the history sets back turns into a session one in
  `SetHead`, before the rollback: a block medium gets a `SessionWriteMap` over its `HostWriteHold` (the held
  sectors move into it, none reaches the file), a floppy stops being written through. The file keeps its content
  until the user saves.

Tests: `TTDMediaUndo_Test.AWriteThroughDiskSetBackKeepsItsFileUntilSaved`,
`TheSameDiskInsertedAgainIsTheSameOnlyWhereItWasUntouched`, `TTDMediaCut_Test` (ask / keep / empty, the log).

### 11.8 M6 as built (2026-10-11)

Four phases, each with an A/B frame benchmark against the base (§11.8, last item).

- **Changes of the machine itself (phase 1).** New kinds `RomLoad` (`Emulator::LoadROM`; a reset's pending ROM reload
  is a `RomLoad` nested in the reset), `GsCardSwitch` (`SoundManager::switchGeneralSoundCard`, on the machine thread
  at a frame boundary), `SpeedChange` (`Emulator::SetSpeedMultiplier`), `StateTransfer`
  (`MachineStateTransfer::Transfer`, the target's coordinator, inside the two CPU holds). New session effects
  `MachineChanged` (the session ends, its history goes; a black box starts again) and `TimingChanged` (a history goes
  except the black box's). `SlotChange` and `CdFrontPanel` are asked only (`WouldRefuse` from the slot plan and the
  General Sound requests, `Ask` from the CD control): the CD front panel is refused while a recording the user started
  runs, the text the guard had. TTD v1's refusals of these changes are the emulator's owner refusal, as for model
  switches (goes with v1).
- **The TTD hooks.** `OnLoad`, `OnConfigurationChange` and `OnModelTransfer` are gone, with `TTDLoadKind` and
  `TTDConfigChangeKind`; one `EndSessionFor(TTDChange{Media, Machine, Timing}, reason)` replaces them, and its only
  caller is the coordinator's session port. Nothing to end (no history, no recording): nothing happens - before, a
  ROM load or a state transfer with no session wrote `machine-change` into `last_stop_reason`.
- **Tool edits (phase 2).** `Emulator::EditMemoryFromTool` is `OpKind::ToolEdit` (logged with its source: "lua
  register", a WebAPI path, ...). No session effect; parked while a recording runs (a new per-cell `park` in the
  table): the edit is its event (`BeginToolEdit` / `EndToolEdit`); live, the edit runs as it comes. **Refused in the
  recorded past** (replaying, detached; owner 2026-10-11, the §4 cell): nothing would keep the edit - the next seek
  sets the point back, a replay goes off its recording - so "Rec From Here" first. `EditMemoryFromTool` returns the
  reason (empty: done) and every surface reports it: WebAPI 409 (memory, page, register writes), the CLI's error line,
  a Lua / Python error (`EditMemoryFromToolOrThrow`), GDB `E0D`, the port, Next register, sector, device memory, RTC,
  ISA, network frame and assembler inject helpers their `error`. DeZog leaves the history before its edits. Register writes from the CLI, WebAPI, Lua and Python were bare `Z80::SetRegisterValue` calls,
  not recorded: they go through `Emulator::SetRegisterFromTool` now (a fix, the GDB and DeZog register writes were
  tool edits already). Not moved: the fast tape, fast disk and autostart traps in `Z80` keep calling
  `BeginToolEdit` / `EndToolEdit` themselves - they are recording data on the per-instruction path (§3.4, links that
  stay), not operations.
- **RZX (phase 3).** `IPlaybackSink` (ports.h): the policy row says an operation ends an input playback
  (`endsPlayback`: reset, autostart, snapshot load); `Run` calls `EndPlayback` before the park and outside the lock
  (the RZX stop holds the machine itself, and its own lock may be held by a thread waiting for the machine), for the
  outermost operation on the thread only. `Emulator::Reset` and `LoadSnapshotStaged` no longer stop RZX themselves.
  The autostart joined: before, its reset left an RZX playback running on a reset machine.
- **TTD verbs (phase 4).** `TTDControl`'s start, stop, invalidate, seek, step-back / step-forward,
  step-instruction, reverse-step, reverse-continue, load and resume run as `TtdStart`, `TtdStop`, `TtdDiscard`,
  `TimelineSeek`, `TtdLoad` and `TimelineCut` through `IMachineOps::RunOwnLocked`: the rules and the log, the verb on
  the caller's thread without the coordinator's lock or park. TTD keeps its own order (its session lock, then the
  CPU); `Run`'s order (the CPU, then the coordinator) around it would invert that against every TTD read that holds
  the session lock and waits for the CPU. A kind with a park or a session effect is refused there (a contract error).
  The cut's media decisions are the `detail` of its one `timeline-cut` entry (M5 noted one entry per slot). Not moved:
  the debugger's and Qt's direct controller calls (DeZog's background recording, Qt's black-box switch), and the
  verbs that read or tune the session (status, markers, bookmarks, journal, memory-at, ...).
- **Performance (N1).** A/B against the base (`ad899e9b0`), Release, interleaved rounds, CPU time per frame:
  `BM_HostFrame_*` (48K, Pentagon, TS-Conf, fast and debug) and `BM_Frame_*` (no TTD, TTD gaming and development).
  Phase 1: every frame benchmark within ±1.4 % on the minima, paired medians within ±0.7 % (the A/A noise of the same
  run: ±1.5 %). Phases 2-4 (one run, they add nothing to the per-frame or per-instruction paths), against phase 1:
  `BM_HostFrame_*` within ±0.5 %; `BM_Frame_*` 20 interleaved rounds within ±0.5 % on the minima and the paired
  medians (a first 8-round run against the base read +1-2 % under a changing load; it did not repeat).
  A tool edit outside a session (`BM_ToolEdit_LivePoke`, a script's poke): 15 ns -> 215 ns (the rules and the log);
  64 K pokes cost 13 ms more.
- **The log keeps the other operations** (`OperationLog::Append`): the same tool edit again (source, mode, status,
  summary) counts in the newest entry (`repeats`) under the next seq instead of taking a slot - a script's pokes
  would push the resets and loads out of the ring of 1024 otherwise. `seq` may skip numbers now.

Tests: `MachinePolicy_Test` (every cell, playback ends), `MachineCoordinator_Test`
(`APlaybackEndsBeforeTheParkOfTheOutermostOperationOnly`, `AnOperationOnItsOwnLockIsLoggedWithoutAParkOrTheSession`),
`CoordinatorEmulator_Test` (`AChangeOfTheMachineItselfEndsTheSessionAndDropsItsHistory`,
`ASpeedChangeIsLoggedAndDropsAHistory`, `TtdV1RefusesARomLoadWhileRecordingAndTheLogHasIt`), `TimeTravelHooks_Test`
(`MediaMachineAndSpeedChangesDropAStoppedSessionWithTheirReason`), `OperationLog_Test`
(`TheSameToolEditAgainCountsInTheNewestEntry`); `CoordinatorEmulator_Test` also `AResetEndsAnRzxPlaybackFirst`,
`AToolsEditIsLoggedAndARegisterWriteIsOneToo`, `AToolsEditInTheRecordedPastIsRefused`, `TtdVerbsAreLogged`. Benchmark: `BM_ToolEdit_LivePoke`
(`core/benchmarks/emulator/coordinator/tooledit_benchmark.cpp`).

