# TTD branches: storage

Part of [TTD branches](README.md). Status: **design** (2026-11-08). Format reference: `core/src/debugger/ttd/ttd.ksy`
(schema 2), container `engine/ttdcontainer.*`, session `engine/ttdsessionfile.*`, folder `ttdrecordingfolders.*`,
writer `engine/ttdrecordingwriter.*`.

## 1. Requirements

| Id | Requirement | From |
|---|---|---|
| S-1 | A session without branches is written byte-identical to today's | zero overhead |
| S-2 | A build without branch support opens a branched file as its trunk, unchanged | FR-23 |
| S-3 | Every branch is on disk as it records; a crash keeps every complete part of every branch | Q-3, F-1 |
| S-4 | Opening one branch (or seeking into an evicted segment) reads only that branch's data: no scan of the folder or of the whole file | owner: fast demux |
| S-5 | Append-only: no file is rewritten in place; metadata edits and deletions are appended records | owner: immutable stream |
| S-6 | Branch content is relocatable: extracting a lineage or dropping branches copies parts verbatim, no re-encoding of records | tools.md |
| S-7 | The whole session on disk, no limit; a full disk stops recording with a warning before | Q-3a |
| S-8 | A clean close deletes an unsaved folder; a crash folder keeps the 7-day cleanup | Q-3c |
| S-9 | Save keeps every branch by default; "this lineage only" is an option and gives a linear file | Q-9, Q-7 |
| S-10 | Media writes are in the branch streams | Q-10 |

## 2. What the format gives already

- Streams declared **ancillary** are skipped by a reader that does not know them (`ttdcontainer.cpp:319`); required
  unknown streams refuse the file.
- Ids 0x0200-0x02FF are reserved for branches (`ttd.ksy:105-109`, `ttdsessionfile.h:81-82`).
- `part_end.branch` and `index_part.branch` (u16) exist, always 0 (`ttd.ksy:262, 313`).
- The index lists per part `part_end_offset`, `first_frame`, `frame_count`, `branch`; parts list the parts they
  depend on.
- A part-end flag marks a file start: the loader restarts the numbering of versions and journal positions there.
- The loader does **not** look at `part.branch` today: records of a branch must therefore not sit in the trunk's
  stream ids, or an old reader would load them as trunk records (§3.2).

## 3. Encoding

### 3.1 Branch identity

- Stream **0x0200 `branch-table`** (ancillary): one record per branch event, appended: `create {local id, GUID,
  parent local id, fork {frame, T}, fork checkpoint, meta}`, `update {local id, changed meta fields}`, `delete
  {local id}`. The table of a file is the replay of its records. Written only when the session has a second branch
  (S-1).
- `part_end.branch` / `index_part.branch` = the local id of the branch whose records the part holds. **A part holds
  one branch's records.**
- Local ids are per file: `save` / `join` / `extract` renumber them densely; GUIDs never change.

### 3.2 Branch content streams

A branch other than the trunk writes its records into the branch range, ancillary, so an old reader skips them
(S-2):

| Trunk stream | Branch stream |
|---|---|
| 0x0001-0x003F (pieces 1, checkpoints 3, events 5, configuration 6, write journal 7, coverage 8-10, bookmarks 12, bus / media journals 13-16, holder 17-0x3F) | 0x0200 + id (0x0201-0x023F); 0x0200 itself is the branch table |
| 0x0100-0x013F frame streams | 0x0240-0x027F |
| (new) media writes | 4 for the trunk (2 and 4 are the engine ids not taken; 17-0xFF belong to the holder), 0x0204 for branches (S-10) |
| 0x0280-0x02FF | reserved |

Holder streams above 0x3F have no branch twin; the holders in use (8-12, 17 on) fit.

### 3.3 Numbering and references (relocatable, S-6)

- Each branch numbers its own piece versions and journal positions in its own streams, from 0 at its first part
  (and again at each file start, as the trunk does).
- A reference to a piece of an ancestor is `(branch local id, version)`; inside its own branch, `version` alone
  (today's encoding, so the trunk's records do not change).
- Removing branches or the trunk's parts after the latest fork therefore does not renumber anything that stays;
  only the branch table and the `branch` fields of part ends and the index are rewritten.

### 3.4 Media writes

Record `{frame, T, slot, unit kind, index, image}` per changed unit and frame (after-image), plus `{base, ...}` the
first time a unit is written in the session (its image before). A protect-switch change is a one-byte image. The
per-unit index of architecture.md §7 is rebuilt from these records on load.

## 4. Recording folder

```text
<user data>/ttd/<date-time>-<name>/
  owner.pid
  branches.tdx                 # folder index, append-only (from the second branch on)
  segment-0000.ttd             # trunk, as today
  segment-0001.ttd
  segment-0002-b3.ttd          # branch local id 3, its first segment
  segment-0003-b3.ttd
  segment-0004.ttd             # trunk again (capture order numbers the files)
```

### 4.1 Demultiplexing: options and the proposal

| Option | How a branch is found | Write | Read one branch / one segment | Crash | Many short branches |
|---|---|---|---|---|---|
| (a) parts of several branches mixed in one segment file | the file's index | one open file | read the index, seek to the parts | as today | few files |
| (b) a file per branch and segment | the file name | a file open per active branch only (only the active one records) | open the file | as today | one small file per experiment |
| (c) (a) or (b) plus a folder index | `branches.tdx` | one more append per segment and per branch event | one index read, then the file | index rebuilt from the files' own indexes | - |

**Proposal: (b) + (c).**

- Only the active branch records, so (b) never has two files open for writing; a switch closes the current
  segment (it already has to: a segment is one branch).
- The file name answers "which files hold branch 3"; `branches.tdx` answers "which file holds branch 3, frame
  f" and "what is the tree" without opening any segment file.
- `branches.tdx` is an append-only list of records: the branch-table records (§3.1), and per closed segment
  `{file name, branch, first frame, frame count, first checkpoint, bytes, CRC of the file's index}`. It is a
  container file like a segment (same record headers and CRCs), so the same reader and the same fuzz harness serve
  it. When it is missing or damaged, it is rebuilt by reading each segment file's trailer index (no record scan).
- Measured before freezing (B-5): open one branch out of 1000 in a folder; seek into an evicted segment; the
  number of files for the UC-5 workload (1000 experiments of 200 frames); against (a). If (b) gives too many small
  files, branches shorter than one segment share a file per fork position (still one branch per part).

### 4.2 Lifecycle

| Event | Folder |
|---|---|
| Recording starts | folder created lazily at the first capture, as today |
| Resume / edit in the past | a new branch: the current segment file closes, the branch's first file opens; nothing deleted (F-1 fixed) |
| Switch | the current segment closes; recording on the target opens its next file |
| Delete / prune | `delete` records appended to `branches.tdx`, then the branch's files deleted |
| Memory eviction | nothing on disk (the files are complete before a segment may leave memory) |
| Disk full | warning at a free-space threshold (default 1 GB or 10 minutes at the measured rate), recording pauses at the limit (S-7) |
| Clean close | unsaved folder deleted (S-8) |
| Crash | folder kept; the next start offers recovery (as today), 7-day cleanup |
| Loaded session forks (Q-11) | a folder is created for the new branches; `branches.tdx` records the source file (UUID, path, size, index CRC); save joins both |

## 5. Save, load, join

- **Save (all branches, default):** `JoinSessionFiles` (`ttdrecordingwriter.cpp:97-207`) copies parts verbatim
  (it already copies `part.branch`); it adds the branch table stream and the index. Branch local ids are
  renumbered densely; parts of deleted branches are left out.
- **Save lineage (option):** a linear file: the lineage's parts with branch records moved to trunk stream ids.
  This needs re-encoding of references (3.3) and is done by the C++ engine (verb `ttd save lineage=`), not by the
  tools.
- **Load:** the index gives the parts per branch; the branch table gives the tree. With two tiers the load reads
  the active lineage's last window into memory (PR-8 style) and leaves the rest on disk for read back.
- **Old reader:** sees the trunk streams only, its parts with `branch == 0` contain all its records; the branch
  parts contain only skipped streams. Tested (test-strategy.md T-F5).

### 5.1 As built (P2 step 1: one file from memory)

The branches are saved from memory; the folder of §4 is §5.2.

- **Save all:** `TTDSessionWriter::WriteAllLines` writes every live engine line, the trunk first, numbered densely
  in id order (a parent always before its children). A branch's first part starts with the create record
  `{U8 1, local, parent local, fork frame, fork T, holder bytes}` and depends on the parent's parts from the
  fork's segment start to the fork; its later parts depend on the line's previous part. The fork's copy (the
  branch's first checkpoint) is written without change records: the loader rebuilds its tables from the parent's
  checkpoint of the fork frame (`TimeTravelEngine::ImportForkLine`). In branch records a version's base is
  `Varint(base line local + 1, or 0)` and then `Varint(its number there)`.
- **Holder data:** the trunk's streams hold the facts, the trunk's coverage and the trunk's bookmarks only (all an
  old reader sees); stream 18 (`holderstream::kBranchState`) holds `{U8 1, active branch local, the trunk's record}`.
  Each branch's coverage and bookmarks go into its twins (0x0208, 0x020C). The holder bytes of the create record are
  the controller's branch record: version, GUID, name, creation time, creator, run, reason, tags, note, pinned, end.
- **Save lineage:** `TimeTravelController::SerializeSession(out, err, /*lineageOnly=*/true)` sets
  `TTDSessionSaveParams::allLines = false` and `lineageOf` = the active branch; `TTDSessionWriter::WriteLineage`
  writes the trunk up to the first fork, then each branch of the lineage up to the next fork, all on trunk streams
  with one numbering of versions. The fork's copy keeps its change records (they are the parent's fork checkpoint's),
  so it follows the parent's checkpoint before the fork as the parent's own would. The journal positions run on
  across each joint, the settings in force at a fork go with the parent's parts. The lineage's bookmarks move to the
  trunk; the coverage index and the write journal are left out when the lineage has a branch (each branch's start at
  its fork; both are derived). The verb on the automation planes is P5.
- **Load:** a branch whose create record is damaged, out of order, with a parent not loaded, or whose fork the parent
  does not hold is left out with a note, with its descendants; the trunk and the other branches load. The tree keeps
  the engine's line ids: an id left out is taken by a deleted entry (`TTDBranchTable::SkipId`). A GUID taken twice
  gives the second branch a new one. The create record must name the fork its first checkpoint was taken at (F-1
  found records that did not, with a valid CRC): the copy's frame is the fork's; a copy written without changes
  depends on the parent's part that holds the fork; a whole copy (a folder file) has every piece's CRC equal to the
  parent's checkpoint at the fork (`TimeTravelEngine::ForkCopyMatchesParent`). A branch whose first part fails any
  of these is left out whole, its line released. Only the fork's T inside the frame cannot be checked.
- **Old reader:** `TTDSessionFile::SetBranchReadingForTesting(false)` reads as a reader without branch support (the
  branch table and the twins unknown, every part the trunk's): it loads the trunk and stops at the first branch part,
  whose pieces and checkpoints it does not find.

### 5.2 As built (P2 step 2: the recording folder)

Option (b) + (c) of §4.1, with these choices:

- **A file holds one line.** The controller keeps one writer, on the active branch (`TimeTravelController::
  StartLineFiles` / `FinishLineFiles`). A fork finishes the parent's open file, the new branch's file opens at its
  next capture; a switch finishes the open file; recording on a line that has files again starts a segment
  (`TimeTravelEngine::RequestBaseline`), so its next file starts with a baseline and never repeats a part.
- **A branch's files are self-contained.** `TTDSessionSaveParams::fileLine` makes `TTDSessionWriter` write one
  line's file: a branch's first file starts with its create record and writes the fork's copy whole (every piece of
  every region from the copy's full tables; a difference version decoded and written as Full), flagged as a
  baseline. No part depends on another file, so cutting, deleting or joining files never breaks a reference; the
  loader imports such a copy with `ImportBranchLine` + `ImportCheckpoint` (the parent's checkpoint at the fork must be
  loaded before, which capture order gives).
- **Ids in folder files are the engine's line ids**, gaps allowed (a deleted branch's id stays unused). Join copies
  parts verbatim and does not renumber (S-6); this replaces "renumber densely" of §5 for joins. The loader pads its
  lines; the controller takes a missing id with `TTDBranchTable::SkipId`.
- **Names:** one number sequence in capture order, `segment-NNNN.ttd` for the trunk, `segment-NNNN-bK.ttd` for line
  K (`TTDRecordingFolder::LinePaths`). A folder without branches is named as before.
- **`branches.tdx`:** a container without trailer, one synced part per record, stream 0x0200: kind 1 create (the
  create record's bytes), 3 delete `{line}`, 4 segment `{file name, line, first frame, frame span, parts, bytes}`.
  Created at the first fork from the files there (`RebuildBranchIndex`: delete records first, then per file the
  branch's create before its first segment), then appended: the create before the branch's first file opens, a
  segment record when a file closes. A cut keeps its complete records. Meta updates (rename, tags) come with P5.
- **Delete:** the delete records are appended, then the lines' files removed. `Segments()` skips the files of
  deleted lines; `TTDRecordingFolder::Open` deletes them (a crash between the record and the removal).
- **Join:** the header declares the union of the files' streams (a branch file declares the branch table and the
  twins).
- **Holder data:** the session's holder streams go with the last file, a branch's too (trunk ids in a branch part,
  read as the session's). The branch records of the folder are the creates' (the end as at the fork): the loaded
  end is the line's own when it holds more. Branch state, per-branch coverage and bookmarks are not in the folder
  (a save from memory has them).
- **Disk full (S-7):** the free space is checked at each new file, every 25 captures and before a recording goes on
  (`TimeTravelController::ShadowDiskSpace`): under max(1 GB, 10 minutes at the folder's measured rate) a warning,
  under 128 MB the open file is finished whole and the recording stops at the frame's end (`last_stop_reason`
  `disk-full`); `ResumeRecordingFrom` and a start are refused while the disk is full, and the files go on in a new
  file (a baseline) when there is space again.
- **Clean close (S-8):** unchanged: the controller's destruction deletes the folder, branches included; a saved file
  stays.
- **A loaded session's folder (Q-11):** the folder of a loaded session holds only what is recorded after the load;
  `branches.tdx` starts with a source record (kind 2: `{Str path, Varint size, 16 bytes header UUID}` of the
  loaded file, or the path of the loaded recording folder) and `TTDRecordingFolder::SaveAs` joins that file
  first (a folder: its own join, sources followed up to 16 deep). A source gone or changed (size or UUID) is named
  in the join's error; the folder alone does not load then (its branches' parents are in the source).
- **Default names:** the per-kind counters ("resume 3") go on from the names in the table, so a loaded session
  names its next branch "resume 4", not "resume 1 (2)".
- **Not covered (edges):** a black box drops the oldest files by count over all lines (P3 makes the window per
  lineage); the branch state, per-branch coverage and bookmarks are not in the folder (a save from memory has
  them).

## 6. Sizes and timings (measured, B-5 / B-9)

Measured 2026-11-10 on the M-series dev machine at load 9-14, Pentagon 128K at its ROM menu, three repetitions
(`core-benchmarks --benchmark_filter=TTD_Branch`, `core/benchmarks/debugger/ttd/ttd_branches_benchmark.cpp`).

- Branch table record: about 64 bytes plus strings.
- **A branch's first file** (its fork's copy whole plus 20 frames): about 52 KB; a folder of 1000 branches is 1003
  files, 52 MB (joined: 50 MB). Tens of kilobytes per experiment as expected, so the fallback of §4.1 (short
  branches sharing a file) is not needed.
- **B-5, find and read one branch out of 1000** (87 KB of records, the file cache warm):

  | Layout | p50 | p99 |
  |---|---|---|
  | (a) one file, the parts mixed: open, read its index, read the branch's parts | 5.73 ms | 7.44 ms |
  | (b) + (c) `branches.tdx`, then the branch's own file | 4.94 ms | 6.45 ms |

  (b) + (c) is 14 % faster than (a): kept.
- **B-9, a 30-minute session (90 000 frames) with 100 branches** (90 forked from the trunk, a chain of 10):

  | Operation | Time (p50) | File |
  |---|---|---|
  | save all branches | 240 ms | 16.7 MB |
  | save the active lineage (11 lines) | 221 ms | 14.8 MB |
  | load the save-all file | 233 ms | - |

  The load of the whole session is under PR-8's 1 s for the active lineage's window; the window load of P3 can
  only be smaller.
