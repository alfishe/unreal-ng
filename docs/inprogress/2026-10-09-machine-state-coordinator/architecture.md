# Machine state coordinator: architecture

Status: draft, revised 2026-10-10 against [requirements.md](requirements.md) (requirement ids F1-F13, N1-N5 and the
owner decisions there). Facts about today's code: [current-state.md](current-state.md). No open questions; one item
postponed (§11).

## 1. Shape

```
 surfaces (Qt, WebAPI, CLI, Lua, Python, GDB, DeZog, MCP)          machine thread (Z80 traps, devices)
        │  Run / Submit / WouldRefuse                                     │  direct calls (Q2)
        ▼                                                                 ▼
 ┌──────────────────────── MachineCoordinator (one per Emulator) ─────────────────────────┐
 │ policy table · park primitive · boundary queue · mode · operation log · media identity │
 └───────┬──────────────┬───────────────┬──────────────┬──────────────┬──────────────────┘
         ▼              ▼               ▼              ▼              ▼
   TTD controller   MediaManager   SoundManager   NetworkManager   RZX session, FeatureManager, ...
   (participants: Refusal / Prepare / Commit / Abort / OnModeChanged / OnTimelineCut / OnBoundary)
```

The coordinator decides and orders; owners still implement their operations. It serializes nothing: device state
stays in `TTDSerializable`, the peripheral registry, `ITTDRegionSource` and the snapshot pipeline.

Lifetime: owned by `Emulator`, created before every participant and destroyed after all of them; participants
register in creation order and unregister in reverse (F10).

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

class MachineCoordinator {
public:
    OpResult Run(const MachineOp&);                 // any thread; blocks until done, refused or failed
    OpTicket Submit(const MachineOp&);              // any thread; lands at the next frame boundary (F5)
    OpResult WouldRefuse(const MachineOp&) const;   // no side effects; same reason as Run (F3)
    MachineMode Mode() const;                       // published copy (§5)
    const OperationLog& Log() const;                // §7

    // machine-thread direct path (Q2): no queue, no allocation
    PolicyAction Classify(MachineOpKind, OpSource) const;   // what a trap / device event becomes
    void NoteMachineEvent(MachineOpKind, OpSource, std::string_view summary);   // log only
};
```

### 2.1 Lifecycle of `Run`

1. **Policy** (§4): `(kind, mode)` → Allow, Refuse(reason), EndSession(keep | drop), RecordAsEvent. Then each
   participant's `Refusal(op)` ("the drive is busy"). A refusal is logged and returned; nothing else happens.
2. **Park**: `ParkLease` (RAII) brings the machine to a coherent point; on the machine thread it is a no-op. It is
   the only park primitive: the pause-and-sleep, 100 ms-then-proceed and per-manager variants go away (D3).
3. **Prepare**: the owner stages (parses the snapshot, opens and identifies the image, validates the config). No
   side effect. Failure → `Failed`, logged, the session untouched (F4, U5).
4. **Session effect**: the policy's EndSession / RecordAsEvent, once, through the TTD participant.
5. **Commit**: the owner applies; the others get `OnMachineChanged(op)` in registration order.
6. **Settle**: `RestartFrame` if the operation restarted the machine; publish; release the lease.

`Submit` runs steps 1 and 3 at once (so a refusal or a bad file is reported to the caller immediately) and 4-6 in
phase 1 of the next frame boundary.

## 3. Participants

```cpp
class IMachineParticipant {
public:
    virtual std::string_view Name() const = 0;
    virtual std::string Refusal(const MachineOp&) const { return {}; }
    virtual bool Prepare(const MachineOp&, OpStage&) { return true; }
    virtual void Commit(const MachineOp&, OpStage&) {}
    virtual void Abort(const MachineOp&, OpStage&) {}
    virtual void OnMachineChanged(const MachineOp&) {}
    virtual void OnModeChanged(MachineMode from, MachineMode to) {}
    virtual CutPlan PlanTimelineCut(const TimelinePoint&) { return {}; }   // §8: what the cut would need
    virtual void OnTimelineCut(const TimelinePoint&, const CutDecisions&) {}
    virtual void OnBoundary(BoundaryPhase) {}
};
```

| Participant | Owns | Reacts to |
|---|---|---|
| TTD controller | sessions, checkpoints, journals | session effects, `TimelineSeek` / `TimelineCut`, boundary phase 2 |
| MediaManager | slots, media, change layers, undo log, identity | media ops, `OnModeChanged` (host-write hold), cut |
| SoundManager | cards, host output | GS card config, `OnModeChanged` (output hold), restore re-sync |
| NetworkManager | cards, host traffic | network config, boundary phases 1 and 3, cut (queued host answers) |
| RZX session | playback, keyframes | `RzxSeek` / `RzxSnapshot`, cut (keyframes after the point) |
| FeatureManager | features, acceleration lock, debug mode | `OnModeChanged`: the single writer of debug mode (U12) |
| Screen / MainLoop | render skipping, resync | `OnModeChanged`, restore |

## 4. Policy table

One file (`machinepolicy.cpp`), one row per operation, one column per mode: Live, Recording (explicit),
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
ends; not in the TTD session file (§11).

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

## 10. Migration

| Step | Content | Behaviour |
|---|---|---|
| M0 | Defects of current-state §7: GDB edits as tool edits, `SetBlackBox` under the session lock, a load ends the session only after a successful insert, the controller unbinds the media read journal and vectors on destruction, debug mode writers listed for M4 | bug fixes |
| M1 | `MachineCoordinator`, `ParkLease`, operation log, policy table with today's cells and its test; Reset, snapshot load, media ops through `Run` | none; the log appears |
| M2 | Every refusal from the table; raw `IsRecording()` checks deleted; D43 cells (network, IDE drive kind, RZX, NeoGS SD) one at a time with the owner | the black box stops blocking (U3) |
| M3 | Boundary phases; media, GS switch, network into phase 1; media ops during a recording become events (Q4) | U2, gap 13 |
| M4 | Modes broadcast; one writer per setting | none |
| M5 | Media identity (8.2), undo log (8.1), the cut (8.3) on Qt "Rec From Here", WebAPI / CLI resume; the dialog in Qt | U1 |
| M6 | Remaining operations (state transfer, config, tool edits, RZX, TTD verbs) through `Run`; `OnLoad` / `OnConfigurationChange` removed from the TTD hooks | none |

Each step lands alone: build, `test.sh`, docker Linux, gcc:16 -O3 of changed files; M1, M3 and M5 with an A/B
frame benchmark (N1).

## 11. Postponed

**Continuing live from the past in a session loaded from a file** (owner decision 2026-10-10: not supported for now,
postponed; TTD is not to grow without a clear need). A loaded session is replayed and browsed from its journals.
"Rec From Here" in it treats every medium as unproven: Qt asks (empty slot / current medium as it is / cancel), a
non-interactive op leaves the slot empty and logs it. The way back if it is ever needed: an ancillary stream with
the guest's media writes (old and new content per written unit), sized by what the guest wrote.

Owner decisions are recorded in [requirements.md §6](requirements.md#6-owner-decisions).
