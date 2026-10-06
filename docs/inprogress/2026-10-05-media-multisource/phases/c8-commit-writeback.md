# C8 — commit into the graft base (S3), write-back into folders (S4), the Qt strategy dialog

**Status:** C8a and C8b built 2026-10-06 (as-built notes in §5, §6); C8c designed. Phase C8 of [tdd.md](../tdd.md) §14; strategies S3 and S4 of
[flatten-strategies.md](../flatten-strategies.md) with their decision trees DT-10 to DT-14, which apply as written
unless this document says otherwise. FR-50 to FR-54 in [goals-and-requirements.md](../goals-and-requirements.md).

| Part | Scope | Exit |
|---|---|---|
| **C8a** | S3: plan, journal, write, recovery of an interrupted commit on open; `media flatten` | journal crash tests; ACC: a graft session committed boots from the base alone |
| **C8b** | S4: routing (DT-10), delete policies (DT-11), the conflict gate (DT-12), staging and apply journal, whiteouts | conflict tests; round-trip tests (guest edit → host files → rebuild → the same tree) |
| **C8c** | The Qt strategy dialog (DT-9 interactive): S1 to S4 with the plan preview, `writes.save` preselected; the eject / insert prompt offers it for a dirty composite | built and checked by the owner on macOS / Windows / Linux |

## 1. `media flatten`

`media flatten <slot> --strategy flat|delta|commit|write-back [path] [--plan] [--force] [--on-conflict refuse|keep-both]`
runs the named strategy; it never goes through DT-9 (that is `save`). `--plan` reports what would be written and
writes nothing. `save --strategy commit|write-back` runs the same code (DT-9: an explicit strategy wins; an eject's
save still falls back to a delta, D-8). Every surface gets the verb through `MediaControl` (CLI, WebAPI, MCP, Lua,
Python).

## 2. C8a: S3

| Step | Rule |
|---|---|
| Preconditions (DT-14) | the composite is a graft (the slot's base device is a `GraftVolume`; a partitioned disk is not committed yet); the base file is raw / HDF / HDI / fixed VHD (not CHD) and opens read-write; no other slot holds the base (its own medium or as a layer of a composite: FR-52); `media changes` has no warnings (lost clusters, cross-links) unless `force` |
| Plan | the base sectors to write: the graft's patch sectors, the sectors of its grafted runs, the guest's changed sectors (in base coordinates: a partitioned disk maps each partition's sectors back through its graft). The content of each is what the composite reads there now. `--plan` reports the counts and bytes per kind |
| Journal | `<base>.ujournal`: magic `UNGJRNL1`, the base's size, then every planned sector's old content `(lba, 512 bytes)`, then `UNGJEND!` and an FNV-1a hash; written, flushed and synced before the base is touched |
| Write | the planned sectors in LBA order through `HddImageFormats::OpenBlock(..., ReadWrite)` (the format's own data offset: an HDI header, a VHD footer stay as they are); flushed and synced; then the journal is removed |
| Recovery on open | before an image is opened (a slot insert of the image, an image layer of a composite) a `<image>.ujournal` is looked at: complete → the old sectors go back, synced, the journal is removed, report "an interrupted commit was rolled back"; incomplete → the base was not touched yet, the journal is removed, reported |
| After | the slot holds the base image itself (session access, the change layer empty): the base now has every layer's files. The descriptor is not rewritten (its comments and layout are the user's); inserting it again grafts the upper layers again, which changes nothing that is not newer on the host. The report says so |

Deviation from flatten-strategies.md S3 step 5: the descriptor is left alone and the slot is rebased on the base
image. Rewriting a user's YAML loses its comments and order; a `committed:` marker can come later if wanted.

## 3. C8b: S4

As flatten-strategies.md S4 with DT-10 to DT-12. Specifics:

- **Input**: the C6b change set (`ListMediumChanges`) with each change's owner layer, and the after-volume read
  with `FatVolumeReader` for the new bytes.
- **Upper layer**: `writes.upper`, else the topmost `writable: true` folder layer.
- **Whiteouts**: kept in `<descriptor>.whiteout` (one guest path per line), applied after every layer merged; the
  descriptor itself is never rewritten. An inline descriptor cannot take write-back.
- **Attributes** are not carried yet (a note in the plan): the folder manifest has no attribute field.
- **Staging**: `.unreal-staging-<n>` next to each target (the same host volume), then the steps listed in
  `<descriptor>.writeback` first; an interrupted apply is completed on the next insert of the descriptor.
- **Rebuild**: the composite is built again from fresh snapshots and the change layer emptied.

## 4. Tests

| Part | Test | Checks |
|---|---|---|
| C8a | `ComposeCommit_Test.PlanCountsPatchGraftAndGuest` | the plan's sectors by kind; `--plan` writes nothing |
| C8a | `ComposeCommit_Test.CommitWritesWhatTheGuestSees` | the base afterwards: every planned sector equals the composite's, every other sector unchanged; the slot holds the base; `FatVolumeReader` reads every layer's file from the base alone |
| C8a | `ComposeCommit_Test.Refusals` | not a graft; a CHD base; the base in another slot; attribution warnings without `force` |
| C8a | `CommitJournal_Test.InterruptedCommitRolledBack` / `.UnfinishedJournalDropped` / `.DamagedJournalReported` | a crash simulated after the journal and part of the write: the next open restores the base byte for byte |
| C8a | C8a | ACC `SprinterBoot_Test.ComposeCommitBootsFromTheBase` | the ACC-C3 session (graft + guest `mkdir`) committed; the base image alone boots DSS and lists the folder |
| C8b | `ComposeWriteBack_Test.*` | each DT-10 branch, each `onDelete` policy, conflicts (refuse, keep-both), host-illegal names, the round trip, the apply journal |

## 5. As built: C8a

| Piece | Where | Notes |
|---|---|---|
| `CommitJournal` | `io/storage/commitjournal.{h,cpp}` | `Write` (the old content of the planned sectors, then the file synced with `fsync` / `_commit`), `Remove`, `Recover`, `Sync`. `Recover` sorts a journal into one of four cases. No journal: nothing to do. A complete journal (end marker, hash): the sectors are written back through the image's format, a raw image that the commit had grown is cut back to its old size, and the journal is removed. An unfinished journal (cut short, or no end marker): it is removed; the image was never touched, because the commit writes only after a complete, synced journal. A complete journal whose hash fails: kept as `*.ujournal.bad` and reported. |
| Recovery hooks | `MediaFormatRegistry::Open` (an image into a slot), `CompositeMediumFactory` (an image layer, a passthrough partition) | The report line names what happened ("an interrupted commit was rolled back (N sectors restored)"). |
| Plan | `GraftVolume::PatchLbas`, `GraftedSectorRuns` | The planned sectors are the patch sectors, every sector of the grafted runs (whole clusters) and the change layer's sectors. Each is read through the session layer, never through the read tap, so time travel does not record it. |
| Commit | `MediaManager::CommitComposite` (`save` / `flatten` with `strategy: commit`) | Checks in DT-14 order. A base of another format, or a CHD, is refused as not supported; a base used anywhere else, as a file or as a layer, as in use (FR-52); lost clusters or cross-links as dirty unless `force`. Then: journal, write in LBA order, sync, remove the journal. A cut-down raw base grows to hold its volume; other formats are refused. Last, the slot is rebased on the base (the composite is closed, the base opened read-only under the emptied session layer, the format set, the composite info cleared, and the slot told). `plan: true` returns the counts and writes nothing. |
| `media flatten` | `MediaControl::Flatten`, every surface | `strategy` is required; `flat` needs a path. The verb runs the save path with that strategy, so DT-9's defaults never apply. `write-back` answers not supported until C8b. |

Tests: `ComposeCommit_Test` (4: plan, commit equals the guest's view byte for byte and the base alone holds every
file, refusals, the registry rolling back an interrupted commit), `CommitJournal_Test` (3), and the Sprinter
acceptance test `SprinterBoot_Test.ComposeCommitBootsFromTheBase` (~3.8 s, two boots).

## 6. As built: C8b

| Piece | Where | Notes |
|---|---|---|
| Plan | `WriteBack::Plan` (`media/writeback.{h,cpp}`) | Input: `ListMediumChanges` (each change with its owner layer by name) and the descriptor read again from its file. Routing follows DT-10. A create or mkdir goes to a directory this plan makes, else the topmost writable folder layer whose host folder has the parent, else the upper layer. A modify goes to its owner when that is a writable folder, else it is copied up into the upper layer. A rename stays a host rename inside a writable owner when the target directory exists, else it becomes a create plus a delete. Attributes give a note. The upper layer is `writes.upper`, else the topmost writable folder layer. |
| Deletes (DT-11) | `Planner::Delete` | `ignore` gives a note, and the file comes back on the next build. A read-only owner, or `keep`, gives a whiteout. On a writable owner, `delete` removes the host file and `move` moves it to `<deletedFolder>/<UTC yyyymmdd-hhmmss>/<layer>/<path>`, copy then remove across volumes. `trash` is a plan error: no host trash API yet. |
| Conflicts (DT-12) | `Planner::Gate` | A host file the build read (its tree node is a host file of the pool) must still have the scanned size and mtime. A file the build did not see must not exist yet. Otherwise it is a conflict: a plan error by default, or with `onConflict: keep-both` the step writes `name (guest).ext` next to it. On Windows, names the host cannot store (device names, a trailing dot or space) are plan errors. |
| Apply | `WriteBack::Apply`, `Recover` | Each new content is staged as `.unreal-staging-<n>` next to its target, carrying the guest's FAT time. Then `<descriptor>.writeback` lists every step and ends with `end`, and is synced. `Recover` performs the list: mkdirs, renames and writes (copy and remove when a rename crosses volumes), whiteouts appended to `<descriptor>.whiteout`, removes last, deepest first. Steps already done are skipped. A list without `end` is dropped, with nothing applied. `CompositeMediumFactory::Open` runs `Recover` before reading the descriptor. Staged names are service files (`.unreal-staging-*`), so a folder scan skips them. |
| Whiteouts | `ComposeDescriptor::deleted` (from `<descriptor>.whiteout`), the factory after `UnionBuilder::Merge` | Detached from the merged tree by FAT key, with a report line; the list is part of the normalized form, so the content id follows it. A graft releases the base entries such a whiteout hides. |
| After | `MediaManager::WriteBackComposite` | The change layer is emptied, an S2 delta that the layers now hold is removed, and the slot is rebuilt (`Rescan`). `plan: true` lists the steps and the errors and writes nothing. |
| Surfaces | `save` / `flatten` `strategy: write-back`, option `onConflict` (refuse, keep-both) on `MediaControl`, CLI, WebAPI, MCP; Lua / Python through `MediaControl` | |

Tests: `ComposeWriteBack_Test` (7): modify in place, copy-up, creates into the owning, planned and upper layers; delete
and whiteout, the descriptor byte for byte unchanged; move, ignore, keep; a rename inside a layer and across;
conflicts refused and kept both; a plan that writes nothing, and `trash` as an error; an interrupted apply finished on
insert. Not done here: a partitioned composite (refused), attributes, the host trash.
