# Machine state coordinator: who gets what, and requirements

Status: draft for the owner's review, 2026-10-09. Comes before [architecture.md](architecture.md), which is revised
against this document once it is accepted. Facts about today's code: [current-state.md](current-state.md).

## 1. The problem in one paragraph

Some operations change the machine as a whole: reset, snapshot load, disk / tape / SD change, ROM / card / speed
change, a debugger edit, a TTD seek or "Rec From Here", an RZX seek. Each of them has to respect the same rules
(what TTD does with it, whether it is refused while recording, when it lands relative to the frame, how the machine
is paused for it). Today every operation, on every surface, applies those rules itself. Since 2026-09-01 the rules
have been retrofitted caller by caller in a stream of commits: "WebAPI and CLI memory writes are replayed edits",
"fast loader traps are recorded edits", "the disk autostart's name hook is a recorded edit", "a refused snapshot load
no longer ends the TTD recording session", "slot-set guard on the engine's load", "black box arguments on every
automation surface". GDB was missed; a few others are inconsistent (current-state §7).

## 2. What each person gets

### 2.1 The user (Qt, playing and debugging)

| # | Situation today | With the coordinator |
|---|---|---|
| U1 | A game saves to disk. You rewind before the save and press "Rec From Here". The disk still holds the save from the future you threw away; the game shows progress you did not make. With a write-through IDE / SD image those writes are already in **your file on the host disk**, and a DOS that keeps its FAT or catalog in RAM now writes with the old table over the newer disk: the image can get corrupted. | The disk returns to its content at the point you resumed from, host file included. **Only while the same medium is still attached and unchanged; see §3a.** |
| U2 | A two-disk game during a recording: "insert disk 2" is refused. You must stop recording and lose the continuity. | The swap is recorded as an event; the recording goes on; seeking back across it works (replay reads the journals, no medium needed). Continuing live from before the swap needs the earlier disk: §3a. |
| U3 | Always Record (black box) is on. Inserting a tape works, but changing network settings, the IDE drive type, RZX seek or ejecting the NeoGS SD card is refused "while recording". | Nothing ordinary is refused because of the black box. Every rule is one row in one table. |
| U4 | You try something and get an error dialog. | The menu item is disabled with the reason, the same reason every other surface gives. |
| U5 | A tape / disk load fails (bad file). Your TTD recording has already ended. | A failed load changes nothing. |

### 2.2 The developer (adding a device, a manager, an operation)

| # | Today | With the coordinator |
|---|---|---|
| D1 | A new machine-level operation must remember: the recording guard (and which of the two kinds), pause + wait (one of ten variants), the right TTD hook (`EndSession`, `OnLoad(kind)`, `OnConfigurationChange`, `OnModelTransfer`, `BeginToolEdit`), `RestartFrame`, and do it again on every surface. Forgetting one is a silent bug found weeks later (GDB edits). | Write the owner's code; add one row to the policy table; every surface gets it. |
| D2 | A bug that only happens after "swap disk, then seek, then edit memory" is found by bisecting by hand. | The operation log says what happened, at which frame, from which surface. |
| D3 | Cross-thread writes and teardown order are per manager (the `SetBlackBox` race, the TTD controller leaving the media read journal bound after it is destroyed, the 2026-10-02 use-after-free). | One park primitive; participants unregister in reverse order. |

### 2.3 The tester and automation (CI, scripts, MCP / AI agents)

| # | Today | With the coordinator |
|---|---|---|
| T1 | A script inserts a disk or changes config; where it lands depends on timing, so two runs can give different sessions. | An operation lands at a stated frame boundary; the same script gives the same session (comparable by hash). |
| T2 | A bug report is a description and maybe a snapshot. What the tester did (swaps, edits, config) is lost. | The TTD session carries the operation log: open it, see the steps, replay them (journals, no media needed). Continuing live from that session on another machine needs the tester's media: §3a. |
| T3 | Tests that need "this change lands before the checkpoint of frame N" drive boundaries by hand and have gone wrong (TTD TODO 4b-1). | Submit the operation, run frames. |
| T4 | WebAPI, CLI, Lua, Python, GDB, DeZog answer the same request differently in some modes. | One answer; `can I do X now?` is a query with the same reason text. |

## 3. Honest comparison: point fixes vs the coordinator

| Benefit | Possible with a point fix? | What the coordinator adds |
|---|---|---|
| U1 disk follows "Rec From Here" | Yes: an undo log in the media layer, called from `ResumeRecordingFrom` | Nothing for U1 alone; it is one more TTD → media call |
| U2 disk swap as an event | Yes: in `MediaManager::CheckRecording` + the frame boundary | The ordering (swap before the checkpoint) stated and tested once |
| U3 black box never blocks | Yes: replace the four raw `IsRecording()` checks | Keeps it from coming back with the next operation |
| U4 disabled with reason | Partly: per menu item | One query for all |
| U5, GDB edits, races | Yes: M0 fixes | — |
| D1 one place for the rules | No | This is the coordinator |
| D2, T2 operation log | Hard: every caller would log itself | Free once every operation passes one point |
| T1, T3 stated landing frame | Partly | One queue, one boundary order |

**Conclusion.** Everything the user sees (U1-U5) can be fixed point by point, and cheaper. The coordinator pays off
for the developer and the tester (D1, D2, T1, T2): it stops the stream of per-caller retrofits and gives the
operation log. It is worth building if more cross-cutting features are coming (they are: D10b media events, the
media cut, RZX on the engine, more machines with their own operations). If they are not, M0 plus the point fixes is
the right amount of work.

## 3a. Risk: is the medium of the past still there?

Every "go back and continue" promise depends on the medium. Replay does not: sector and CD reads are in the TTD
read journal, port traffic in the port journals, so seeking and watching work with the medium gone. Continuing
**live** from a past point does: from there on the guest reads and writes the real medium.

| Case | What can be wrong | What we can detect today | Risk |
|---|---|---|---|
| Same run, medium still attached (UC "Rec From Here" right after a rewind) | another program changes the image file while we run (write-through) | nothing | low |
| Same run, host folder | the host folder changes on disk | not needed: the volume is a snapshot taken at attach; host changes are ignored until a rescan (`foldersnapshot.h:7`) | low |
| Session saved, loaded later (other day) | image deleted or moved | path missing | medium |
| — | image edited in place with the same size | **nothing**: a raw / HDF / VHD image's `ContentId` is path + size (`rawimage.cpp:80`), not content | **high** |
| — | host folder reshuffled (files added, removed, touched) | yes: the folder hash mixes names, sizes and mtimes; a rescan builds a different FAT, so sector N is another file's data | high, detectable |
| — | floppy / tape | `ContentId` is 0 for both; nothing | high |
| Bug report opened on another machine | the tester's media are not there at all | path missing | certain |
| Large media | a 1-8 GB IDE / SD image cannot be copied into the session | — | — |

**Sizes.** The undo log for a cut costs what the guest wrote, not the image size: 512 bytes per written sector,
one track (~6.25 KB) per written floppy track, kept only while a session records. The TTD session holds no media; carrying them
is UNS's job (owner decision).

**Rules that follow** (requirements F11-F13 below):
- never roll back or write onto a medium that is not proven to be the one recorded;
- prove it by content, not by path + size: a content hash at attach (cost: one read of the image, ~1-2 s per GB on
  an SSD; sampled or lazy for large images, to be measured), the folder hash as it is, a hash for floppy and tape;
- when it cannot be proven, continuing live from the past is offered without the medium (slot left empty, an event
  in the log) or refused with the reason; replay and seeking are unaffected;
- a write-through rollback writes into the user's host file: prefer switching the medium to session mode at the cut
  (the host file stays as it is, the rollback lives in the change layer), and let the user save explicitly;
- the TTD session never carries media; bundling a snapshot, the disks and a TTD session into one artifact (a bug
  report that can be continued live elsewhere) is the job of UNS, not of the coordinator or TTD.

## 4. Requirements (each traced to a benefit)

| # | Requirement | For |
|---|---|---|
| F1 | Every machine-level operation goes through the coordinator, whatever the surface. | D1, T4, U3 |
| F2 | What an operation does in each mode (live, explicit recording, black box, replay, detached) is one row of a policy table: allow, refuse with a reason, end the session (keep / drop history), record as an event. | U3, D1, T4 |
| F3 | `WouldRefuse(op)`: the same answer and reason as the operation, no side effects. | U4, T4 |
| F4 | An operation prepares before it changes anything; a failed preparation leaves no trace. | U5 |
| F5 | Operations have a stated landing point: now (machine parked) or the next frame boundary, before the TTD checkpoint. | U2, T1, T3 |
| F6 | "Rec From Here" is a timeline cut: media (and any other state outside the CPU machine) return to the cut point. A plain seek changes nothing outside the CPU machine. | U1 |
| F7 | Media insert / eject / discard during a recording are events, each on its own, no pairing (owner decision Q4). | U2 |
| F8 | Every operation is logged, always (a ring of the last N, owner decision): frame, T-state, kind, surface, summary, result; shown in status and bug reports; the operations inside a TTD session are saved in its file. | D2, T2 |
| F9 | Machine-thread sources (Z80 traps, device markers) call TTD directly; the table decides what they become (owner decision Q2). | D1, hot path |
| F10 | One park primitive; participants register and unregister in order. | D3 |
| F11 | A cut or a live continuation from the past touches a medium only if its identity is proven by content (§3a); otherwise an interactive session asks the user, a non-interactive one logs it and leaves the slot empty (owner decision). | U1, U2, T2 |
| F12 | Media identity by content: floppies, tapes and small images hashed at attach; large images hashed lazily in the background when a TTD session starts, checked when continuing from the past (owner decision); the folder snapshot hash as is. | U1, T2 |
| F13 | A rollback on a write-through medium switches it to session mode at the cut: the rollback goes into the change layer, the host file stays as it is until the user saves (owner decision). | U1 |

| # | Non-functional |
|---|---|
| N1 | No cost on instruction / memory paths; a few calls per frame at most; A/B benchmark. |
| N2 | Each step lands alone and keeps behaviour unless the step's purpose is an owner-approved change. |
| N3 | Surface commands keep their syntax and results; refusal texts may become uniform. |
| N4 | Every policy row, the boundary order, F4 and F6 are core tests. |
| N5 | One coordinator per instance, no global state (owner decision Q1). |

## 5. Not in scope

Cross-instance operations (model switch, slot change stay instance replacement); new state formats; TTD engine
internals; full media history (H1-H5).

## 6. Owner decisions

- 2026-10-09 (Q1): **per instance.** The coordinator lives in `Emulator`; a model switch or slot change is an "end
  everything" operation on the old instance; the new instance starts with its own coordinator.
- 2026-10-09 (Q2): **machine-thread sources call TTD directly** (traps, device markers, media write notes): no
  queue, no allocation; the policy table decides what they become.
- 2026-10-09 (Q3): **RZX is a participant**; moving RZX seeking onto the TTD engine stays a separate task after v1
  is deleted.
- 2026-10-09 (Q4): **a media change during a recording is an event** at the boundary where it lands, each insert,
  eject, discard on its own, no pairing; logged so a later failure can be correlated. Refused until M3.
- 2026-10-09: **build the coordinator, in stages.** M0 defects first, each on its own; then the coordinator with the
  policy table and no behaviour change; operations move onto it one by one. The media cut ("Rec From Here") and media
  swaps as events are built on it, not as point fixes.
- 2026-10-09: **the operation log is always on**: a ring of the last N operations (hundreds of entries, tens of KB)
  with or without TTD, shown in status and included in bug reports; while a TTD session runs, the operations inside
  it are also saved in the session file (F8).
- 2026-10-09: **a medium that cannot be proven the same** (deleted, edited, folder reshuffled, a bug report from
  another machine) when continuing from the past: an interactive session (Qt) asks the user (empty slot, the current
  medium as it is, or cancel); a non-interactive one (automation, headless) logs it and continues with the slot
  empty. A rollback onto an unproven medium never happens (F11).
- 2026-10-09: **a rollback on a write-through medium goes into the change layer**: at the cut the medium switches
  to session mode; the host file stays as it is; the rollback and later writes live in the change layer until the
  user saves (F13).
- 2026-10-09: **media content hash is lazy**: nothing is hashed at attach for large images; the hash is computed in
  the background when a TTD session starts and checked when continuing from the past. Floppies, tapes and small
  images are hashed at once (cheap). No delay in normal use (F12).
- 2026-10-10: **the TTD session does not embed media.** A bundle of snapshot + disks + TTD session is UNS's job;
  the coordinator only needs identity (path + content hash) to decide whether a medium is the one recorded.
- 2026-10-10: **postponed: continuing live from the past of a session loaded from a file.** A loaded session is
  browsed and replayed; "Rec From Here" in it treats every medium as unproven (F11). TTD does not store guest media
  writes.
