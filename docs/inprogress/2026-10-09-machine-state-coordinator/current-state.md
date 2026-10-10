# How the state-owning managers coordinate today

Inventory taken 2026-10-09 on master `b4c2cf5cd`, as the input to a single coordinator for operations on machine
state and time ([architecture.md](architecture.md)). Paths are under `core/src/` unless noted. Line numbers are
from that commit.

## 1. Summary

- **There is no coordinator.** Every operation that changes the machine as a whole (reset, snapshot load, media
  insert, GS card switch, network change, model switch, debugger edit, speed change) calls the TTD hooks itself and
  parks the machine in its own way.
- **The TTD rules are centralised in the controller but invoked by the callers.** D42 ("an operation that replaces
  or restarts the machine ends the session") lives in `TimeTravelController::EndSessionImpl`; whether it applies
  depends on each caller remembering to call `EndSession`, `OnLoad`, `OnConfigurationChange` or `OnModelTransfer`.
- **Refusals while recording are checked two ways.** Half the callers ask `RecordingGuard` (which knows explicit vs
  background sessions, D43), half test `IsRecording()` directly.
- **Parking is ad hoc.** `Pause()` + `WaitForPauseConfirmation`, `Pause(); sleep_ms(20)`, a 100 ms wait that
  proceeds on time-out, `SessionOperation`, `ParkedEmulator`, `RunWhileParked`, frame-boundary queues in three
  managers.
- **The frame-boundary order is the code layout of `MainLoop::CompleteFrame`.** There is no registry of who runs
  at the boundary and in which order.
- **Media and TTD know each other directly**: TTD holds host writes and binds the media read journal; media record
  TTD markers and refuse loads.

## 2. Who calls whom

### 2.1 Into TTD (`ITimeTravelHooks`, `debugger/ttd/timetravelhooks.h:113-209`)

| Caller | Call | Where |
|---|---|---|
| MainLoop | `OnFrameBoundary`, `OnMachineParking` | `mainloop.cpp:521-531`, `:153` |
| Emulator::Reset / autostart | `EndSession("reset" / "autostart")`, `OnMachineReset`, ROM reload `OnConfigurationChange` | `emulator.cpp:1025`, `:960`, `:1053`, `:1072` |
| Snapshot load | `OnLoad(Snapshot)` through `snapshot::Options::beforeCommit` | `emulator.cpp:1881-1891`, `snapshotpipeline.cpp:146-153` |
| LoadTape / LoadDisk / CreateDisk | `RecordingAllows`, then `OnLoad(Tape/Disk)` **before** the insert can fail; `MediaManager::Insert` then calls `OnLoad(Media)` again | `emulator.cpp:2209-2238`, `:2333`, `:2404` |
| MediaManager | `RecordExternalEvent(DiskWrite)` once per frame per slot (`NoteWrite`); `CheckRecording` → `RecordingGuard` + `OnLoad(Media)` on insert / eject / discard | `mediamanager.cpp:688-710`, `:1373-1391` |
| MachineStateTransfer | `RecordingGuard(SwitchModel)`, `OnModelTransfer` | `machinestatetransfer.cpp:1219`, `:1253` |
| ModelSwitch, SlotChange | none: they replace the instance; the old controller dies with it | `modelswitch.cpp:74-174`, `slotchange.cpp:92-120` |
| SoundManager (GS card) | `RecordingGuard(SwitchGsCard)` twice, `OnConfigurationChange(GsCard)`, `UpdatePeripheral` | `soundmanager.cpp:2052-2061`, `:2120`, `:2206` |
| NetworkManager | raw `IsRecording()` refusal | `networkmanager.cpp:833-839` |
| RZX session | raw `IsRecording()` refusals, `OnLoad(RzxSnapshot / RzxSeek)`, `NoteReplaySource` | `rzxsession.cpp:365-372`, `:457-486`, `:409`, `:611` |
| Core / Emulator speed | `OnAccelerationChanging`, `OnConfigurationChange(SpeedMultiplier)` | `core.cpp:1019`, `:1065`, `emulator.cpp:689-697` |
| Tool edits | `BeginToolEdit` / `EndToolEdit` via `Emulator::EditMemoryFromTool` (WebAPI, CLI, Lua, Python, DeZog, device tools) | `emulator.cpp:726-750` |
| Z80 traps | `BeginToolEdit` around fast tape / disk / autostart hooks | `z80.cpp:344-388` |
| Devices | `RecordExternalEvent`, `SubmitLiveInput`, `SubmitMachineTask` (tape, floppy, NeoGS, keyboard, network, MIDI, Evo flash) | many |
| FeatureManager | `StopForFeatureChange` (outside its mutex), acceleration lock callbacks | `featuremanager.cpp:131-138`, `:105-117` |
| Qt | black box: `TTDControl` start / stop and `SetBlackBox` directly | `unreal-qt/src/mainwindow.cpp:4877-4902` |
| GDB | `TTDControl` verbs; refuses register / memory writes only while detached | `core/automation/gdb/src/gdbserver.cpp` |

### 2.2 Out of TTD

| To | Call | Where (`timetravelcontroller.cpp`) |
|---|---|---|
| Emulator | park / resume around Start, Stop, journal switch, `SessionOperation`; `RunTStates` for replay | `:196-203`, `:399-405`, `:941-947` |
| FeatureManager | engage `timetravel` / `debugmode`, restore on stop, acceleration lock callbacks | `:547-575`, `:433-447`, `:700-721` |
| Core / Z80 | speed to 1x while recording; `isDebugMode` and the memory interface set directly during a replay | `:686-697`, `:1856-1861` |
| MediaManager | `HoldHostWrites`, `SetReadJournal`, `CurrentVersions` / `SetHead` | `:1838`, `:3916-3930`, `:3959-3985` |
| SoundManager | host output hold and replay telemetry, `onStateRestored` | `:1845-1849`, `:1695` |
| Devices | `NotifyRecording` | `:681`, `:705` |
| Context flags | `ttdReplayActive`, `ttdPortReads/Writes`, `ttdVectors`, coverage, step work | several |

## 3. Cross-cutting rules and where they live

| Rule | Implemented in | Applied by |
|---|---|---|
| D42: replace / restart the machine ends the session | `EndSessionImpl` (`:1445-1467`) | each caller (reset, autostart, snapshot, ROM, transfer, GS switch) |
| Refused while an explicit recording runs (D43) | `RecordingGuard` (`:1155-1189`) | Emulator loads, MediaManager, SoundManager, SlotManager, CD, surfaces |
| Refused while anything records (no D43 distinction) | raw `IsRecording()` | Network, IDE drive kind, RZX, NeoGS SD eject |
| Acceleration lock (1x while recording, turbo masked) | `EngageRecordingLock`, `FeatureManager::isMaskedByTtd` | controller, FeatureManager |
| Host writes held during a replay | `MediaManager::HoldHostWrites`, `MediaWriteGate` | controller |
| Replay mode | `ttdReplayActive` (plain bool) | read by MainLoop, MediaManager, FeatureManager, Z80, Emulator |
| Debug mode | four writers: FeatureManager, TTD capture, TTD replay (directly), step-over / DeZog / profiler | |

## 4. Threading

- **Emulation thread**: the frame loop and the boundary work, instruction-level input (`ServiceInput`), Z80 traps,
  RZX playback, media `NoteWrite`, network refits.
- **Control threads** (automation, UI, debuggers): TTD verbs (seek replays run on the caller's thread while the
  machine is parked), reset, loads, speed, transfers, model switch, feature changes, RZX seek.
- **Marshalling**: frame-boundary queues (MediaManager `ApplyPending`, SoundManager `_pendingGSSwitch`,
  NetworkManager `_pendingChange`, `MainLoop::RunAtFrameBoundary`, `Emulator::RunAtCoherentMoment`), the
  instruction-boundary queue of the TTD controller, pause-and-wait in about ten places with different time-outs,
  `SessionOperation` locks, published snapshots.

## 5. Frame-boundary order (`mainloop.cpp:477-568`)

1. `OnFrameEnd`: render / latch (skipped in replay), T-state count, tape, turbo tape (may change acceleration),
   BetaDisk, sound, memory / screen, recording, shared memory, frame refresh, analyzers.
2. `OnFrameStart`: tape, **sound (applies a pending GS card switch, which may end the TTD session)**, memory,
   screen, turbo decimation, analyzers.
3. `Z80::BeginFrame`.
4. `MediaManager::ApplyPending` (before the checkpoint).
5. Network devices (before the checkpoint).
6. `OnFrameBoundary` (TTD capture, restart, auto-pause).
7. Automation input (after the checkpoint), network host answers, `PortDecoder::OnFrameEnd`.

Snapshot loads, reset, transfer and RZX restores are **not** frame-queued: they run on the caller's thread after a
park and finish with `RestartFrame` (no frame end).

## 6. Participant-style interfaces that exist

| Interface | Scope | Implementers |
|---|---|---|
| `TTDSerializable` + `TTDPeripheralRegistry` (`RegisterMachinePeripherals`) | device state capture / restore | ~40 devices; reused by `MachineStateTransfer` |
| `ITTDRegionSource` | large device memories | Sprinter VRAM / fast RAM, VDAC2, Evo flash / AVR, SMUC NVRAM, MoonSound, GS, NeoGS |
| `ITTDDisplayParticipant` | picture composition after a restore | VDAC2 card |
| `IMediaHistory`, `IMediaReadJournal`, `IMediaSlot` | media versions, read journal, slots | MediaManager, TTD adapter, peripherals |
| `ISnapshotCommitPolicy` / `ISnapshotCapturePolicy`, `IStateTransferHost` | snapshot and transfer variants | TS-Conf, Sprinter |
| `RzxKeyframeStore` | a second whole-machine checkpoint mechanism (SZX images) | RZX session |
| `handleFrameStart` / `handleFrameEnd` | per-frame work | hard-coded list in MainLoop |

## 7. Problems found

Defects (each worth fixing on its own, before any redesign):
1. **GDB memory and register writes are not tool edits.** `GDBSession::handleWriteMemory` writes through
   `DirectWriteToZ80Memory` and refuses only while detached (`gdbserver.cpp:1276-1315`); register writes likewise.
   An edit made while a recording is paused is not in the history, so a replay diverges from what the user saw.
2. **`SetBlackBox` races the emulation thread.** It writes `_blackBox`, `_blackBoxMinutes`, `_restartPending`
   from the Qt thread (`mainwindow.cpp:4900`) without `SessionOperation`; `OnFrameBoundary` and `EndSessionImpl`
   read them on the emulation thread (`timetravelcontroller.cpp:1374-1380`, `:1220-1226`).
3. **A tape or disk load ends the session before the insert can fail**, and the insert then signals the TTD a
   second time (`emulator.cpp:2209-2238` vs `mediamanager.cpp:1373-1391`).
4. **Inconsistent refusals**: network, IDE drive kind, RZX and NeoGS SD eject refuse during a background session
   (black box) where every other operation does not (D43).
5. **The controller's destructor leaves `MediaManager::_readJournal` and `ttdVectors` pointing at it**
   (`timetravelcontroller.cpp:117-129`; the media manager is deleted later, `emulator.cpp:528-583`).
6. **Debug mode has four writers**; the replay sets `isDebugMode` behind FeatureManager, and a feature change during
   a replay overwrites it.

Structural:
- every new machine-level operation adds another pairwise rule (the media rollback on "resume from the past" would
  have made TTD reach into the media manager again);
- the frame-boundary order matters (media before the checkpoint, input after it, the GS switch in `OnFrameStart`)
  but nothing states or checks it;
- two whole-machine checkpoint mechanisms (TTD, RZX keyframes);
- a media swap queued before a recording can land inside it without a marker (TTD gap 13): an ordering problem
  between `ApplyPending` and the recording start.

Related documents: [snapshot pipeline TODO P7 / P11](../2026-10-02-snapshot-pipeline/TODO.md),
[engine decisions D42 / D43](../2026-09-25-ttd-v2-migration/engine-decisions.md),
[storage manager](../2026-09-28-storage-manager/TODO.md), [BUGS.md](../BUGS.md) open #1 (2x-4x speed while
recording).
