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
    virtual MachineMode Mode() const = 0;   // read by the coordinator until M4 broadcasts modes
    virtual void Apply(SessionEffect, const std::string& reason) = 0;   // at the commit point
    virtual void MachineReset() = 0;        // after a reset: the machine leaves the recorded timeline
};

class IMachineOps {                    // the coordinator, as surfaces and composite owners see it
public:
    virtual OpResult Run(MachineOp) = 0;
    virtual OpResult WouldRefuse(const MachineOp&) const = 0;
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
| Media insert / eject / discard | Refuse; from M3 RecordAsEvent, each op its own event (Q4) | EndSession(drop) | EndSession(drop) |
| ROM, GS card | EndSession(drop) | EndSession(drop) | EndSession(drop) |
| Speed, turbo | Refuse (lock) | black box steps aside, restarts | Invalidate |
| Network card, IDE drive kind | Refuse | Refuse today; Allow per D43 (M2) | Refuse |
| Tool edit (any surface) | RecordAsEvent | RecordAsEvent | Refuse |
| RZX seek / snapshot | Refuse | Refuse today; per D43 (M2) | EndSession(drop) |
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
| M2 | Every refusal from the table; raw `IsRecording()` checks deleted; D43 cells (network, IDE drive kind, RZX, NeoGS SD) one at a time with the owner | the black box stops blocking (U3) |
| M3 | Boundary phases; media, GS switch, network into phase 1; media ops during a recording become events (Q4) | U2, gap 13 |
| M4 | Modes broadcast; one writer per setting | none |
| M5 | Media identity (8.2), undo log (8.1), the cut (8.3) on Qt "Rec From Here", WebAPI / CLI resume; the dialog in Qt | U1 |
| M6 | Remaining operations (state transfer, config, tool edits, RZX, TTD verbs) through `Run`; `OnLoad` / `OnConfigurationChange` removed from the TTD hooks | none |

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
- the mode is read from `ISessionSink::Mode()`; M4 replaces it with the broadcast;
- RZX playback is still stopped by the callers of reset and snapshot load (RZX joins in M6);
- the ROM reload inside a reset still calls `OnConfigurationChange` itself (configuration changes join in M6);
- the log is read in core only (`Emulator::GetOperationLog`); the surfaces follow.

One behaviour change: a media change outside live mode (black box, replay, detached) now parks the machine first,
so it never waits for the machine while holding the coordinator (§2.1 step 2). Such a change from a surface now
applies at once, with the machine held, instead of at the next frame boundary.

Tests: `MachinePolicy_Test` (every cell), `MachineCoordinator_Test` (the lifecycle with fakes, threads),
`OperationLog_Test`, `CoordinatorWiring_Test` (includes), `MediaOwnerContract_Test` (the media owner against
`IOpOwner`), `CoordinatorEmulator_Test` (a real machine: reset, snapshot, tape, discard, eject).

## 12. Postponed

**Continuing live from the past in a session loaded from a file** (owner decision 2026-10-10: not supported for now,
postponed; TTD is not to grow without a clear need). A loaded session is replayed and browsed from its journals.
"Rec From Here" in it treats every medium as unproven: Qt asks (empty slot / current medium as it is / cancel), a
non-interactive op leaves the slot empty and logs it. The way back if it is ever needed: an ancillary stream with
the guest's media writes (old and new content per written unit), sized by what the guest wrote.

Owner decisions are recorded in [requirements.md §6](requirements.md#6-owner-decisions).
