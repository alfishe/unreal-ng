# Phase 4 — The session file, written as it records: technical design

Status: **design, not implemented** (2026-10-02). Roadmap and checks: [README.md, Phase 4](README.md#phase-4--the-session-file). Requirements: FR-11, FR-12, FR-13, FR-15, FR-16, FR-23, PR-7 (disk part), PR-8, PR-11, PR-12, QR-2, QR-3, QR-4, QR-5 ([requirements.md](requirements.md)). Decisions: D5, D6, D11, D12, D19, D22, D28, D30, D31, D33 ([engine-decisions.md](engine-decisions.md)). Open investigation this phase closes: [integrity-and-versioning.md](integrity-and-versioning.md).

Code references are to master at `8ddaf708e`. `TTM` = `core/src/debugger/ttd/timetravelmanager.cpp`, `TTM.h` = `timetravelmanager.h` in the same folder. The engine's in-memory model (pieces, regions, reference blocks, device state versions, the event stream) comes from [phase-1-memory-regions-tdd.md](phase-1-memory-regions-tdd.md), `phase-2-device-state-tdd.md` and `phase-3-replay-inputs-tdd.md`; this phase decides how that model lives in a file and how memory relates to it.

## 1. Glossary

| Term | Meaning |
|---|---|
| Session file | The `.ttd` file the engine writes while it records. Memory holds only a part of it (D28). The decisions glossary calls it the *spill file* |
| Append-only | The file only grows at its end; bytes already written are never changed. A crash can therefore damage only the unfinished tail |
| Stream | One kind of data in the file, with a numeric id: memory pieces, checkpoints, events, write journal, screenshots… |
| Record | One block of one stream in the file: a 32-byte header (stream id, sizes, checksum) and a payload |
| Part | A group of records covering a run of consecutive frames (about one second), closed by a *part-end* record. A part is the unit of crash recovery: complete parts are kept, an incomplete one is dropped |
| Dependency | Data an item needs from earlier: the base of an XOR chain, a shared reference block, an unchanged device state, an event payload (D5). A part lists the earlier parts it depends on (D6) |
| Required / ancillary stream | Required: needed to restore exactly; a reader that does not know it must refuse the file. Ancillary: useful but not needed (write journal, coverage, screenshots); an unknown one is skipped. The idea comes from PNG's critical and ancillary chunks |
| Index / footer | At the end of a finished file: a table of parts and frames so a reader finds data without scanning; a fixed trailer points to it |
| Finalize | Write the index and footer when recording stops. A file without them is still readable, by scanning (crash recovery) |
| Writer thread | A background thread that appends the already-compressed items to the file, so the emulator never waits for the disk |
| Memory-mapped view | The operating system shows the file as memory; reading a byte reads it from disk on first use and caches it. Used to read evicted data back |
| Eviction | Removing data from memory because it is safely in the file. Nothing is lost; a seek reads it back |
| Release | Removing history for good (from memory and, for rolling files, from disk), because a retention policy says so. The session start moves forward (D12) |
| Rebasing | Before history is released, every still-needed item whose chain reaches into the released range is rewritten as a full item (D5) |
| Retention policy | What history is kept: a memory budget (default), or the last N minutes (black box) (D11) |
| Frame-boundary stream | An optional stream that stores something per frame, for example a screenshot; off by default, switched at run time (D19) |
| Arena block | A large memory block (64 MB, the user's direction in [target-architecture §6.1](target-architecture.md#61-memory-as-linked-blocks-direction-2026-09-29)) the engine allocates items in, in capture order |

## 2. What changes, in one example

A one-hour recording of a demo as busy as *Eye Ache*, on Pentagon 128 with its default cards. Rates are the engine's model in [E6](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e6-v1-v2-model/README.md): 36.9 MB per minute in the file, 41.4 MB per minute in memory, write journal kept whole. The memory budget is 512 MB (proposed default, §5.3).

| | v1 today | Engine after Phase 4 |
|---|---|---|
| Where the hour lives | all of it in process memory; v1 has no budget and drops nothing but its journal ring | about 2.2 GB in `scratch/ttd/2026-10-02-153012-pentagon.ttd`, written as it records (36.9 × 60, arithmetic) |
| Memory | grows for the whole session | at most 512 MB: about the last 12 minutes stay in memory (512 / 41.4, arithmetic); older parts are evicted and read back from the file on a seek |
| Emulator thread | — | hands over pointers to items it already compressed; one atomic store per frame. The writer thread appends about 0.6 MB per second |
| Emulator crashes at minute 37 | everything is lost | the file opens up to the last complete part, about one second before the crash |
| "Save" | serializes the whole session in one call | renames the finished file to the chosen name; nothing is copied when the target is on the same disk |
| Unwanted recording | dropped from memory | the file is deleted |
| Seek to minute 3 | decode from memory | read the needed parts through the memory-mapped view; target p99 ≤ 20 ms on an SSD (PR-8) |
| Screenshot of frame 90,000 | seek, restore, render | read from the screenshot stream if it was on (§5.4), no restore |

An idle machine writes far less: ZX-Evo at the BASIC prompt 5.9 MB per minute, Pentagon 128 3.2 MB per minute (E6), so an hour is 200–350 MB.

## 3. How it works today

### 3.1 The whole session is in memory

- Checkpoints live in one vector, `_timeline` (TTM.h:1856), and grow until the session stops. Nothing is evicted. The only place that removes checkpoints is `TruncateTimelineAfter` (TTM:3088-3115), which drops the future on resume, a behavior the engine replaces by branches (D7).
- Memory accounting: `GetHeapBreakdown` (TTM:781) fills `TTDHeapBreakdown` (TTM.h:161-185), the split that [E5](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e5-heap-split/README.md) introduced and the benchmark reports as `bm4_heap_<part>_bpf` ([ttd-bench README](../../../tools/verification/ttd-bench/README.md)). It is reported, not enforced: no budget exists.

### 3.2 Saving is one explicit step

- `SerializeSession(std::ostream&)` (TTM:3512) writes everything at once: header, the live page-store slots, every checkpoint, then flag-gated trailing sections (write journal, coverage, bookmarks, input journal, external events, port journals, network inputs; flags at `ttddumpformat.h:50-163`).
- Callers open the stream themselves: the CLI (`core/automation/cli/src/commands/cli-processor-ttd.cpp:822`), Lua (`lua_emulator.h:3596`), Python (`python_emulator.h:3524`), the benchmark (`core/src/debugger/ttd/bench/ttdbench.cpp:384`). A crash before that call loses the session.
- CPU and chipset are written as raw structs (`WritePod`, TTM:3155-3159, used at TTM:3768-3769). The layout is the host compiler's; a static assertion only checks little-endian (TTM:57-66).
- The reader (`DeserializeSessionImpl`, TTM:3971) needs the whole file and refuses any schema other than 1 (TTM:3985-3990).

### 3.3 Integrity and versioning

- Only 4 KB pieces carry a CRC32C, checked at restore; a mismatch zero-fills the piece and the seek still succeeds (TTM:1584-1600). A bit-flip experiment found 57–100% of flips per section silent ([current-state.md §9](current-state.md#9-integrity)).
- The format is "schema 1, amended in place": every change re-records the fixtures (`ttddumpformat.h:37-48`).
- `ttd.ksy` describes the header, page store and checkpoints; the trailing sections are "not yet modeled" (`ttd.ksy:174-180`). The Python analyzer has a hand-written reader (`tools/verification/ttd-analyzer/src/ttd_format.py`) and no conformance test against the C++ writer (its own docstring, lines 23-25).

### 3.4 Frames as pictures

`ExportClip` (`ttdclipexport.cpp`) walks a stopped session frame by frame, seeking and composing each picture, and writes RGBA in zstd chunks. On *Across the Edge* it took 105 s for 16,571 frames and wrote 93 MB, about 5.6 KB per frame for RGBA and plane B at zstd level 3 in 500-frame chunks ([p0a-plane-b.md §8](../2026-09-27-zxdlss-gigascreen/p0a-plane-b.md)). It is the only per-frame picture TTD can produce today, and it needs a full seek per frame.

## 4. What Phase 4 builds on

- **Phase 1:** pieces with explicit dependencies (D5), a piece store not owned by a session (D22), exact-size allocation in an arena, copy-on-write reference blocks, regions, the frame table (D21) and positions with a branch.
- **Phase 2:** device state versions with a stable type id, instance name and layout version (D23).
- **Phase 3:** one event stream with payloads (D24), the configuration fingerprint and media versions (D25), the write journal on demand, as segments, built by replay elsewhere (D40).

Phase 4 does not change what is recorded. It decides where it is stored, how it gets there and how long it stays.

## 5. Design

### 5.1 Step 1 — Integrity and versioning decided

**Decided (owner, 2026-10-04): the recommendation below, "open with holes".** A CRC32C on every record, the header and the index; header, index and part ends checked at open, records on first access; a damaged record makes only the frames that depend on it unreachable (a seek there fails with the reason), damage in an ancillary stream disables that stream. The options were those of [integrity-and-versioning.md](integrity-and-versioning.md).

**Integrity**

| Question | Options | Recommendation |
|---|---|---|
| I-1 What a checksum covers | stored bytes; reconstructed content; both | **Both.** A CRC32C of every record's stored bytes (detects storage damage without decompressing) and the existing per-piece CRC of the content (detects codec and chain bugs). Phase 1, Step 2 measures the content CRC's share of capture and seek (I-6); it is dropped only if that share is large |
| I-2 Granularity | piece, checkpoint, part, stream, file | **Per record** (one stream within one part, usually a few KB to a few MB), plus the header and the index each with their own CRC. Device states, reference blocks and CPU/chipset are covered by their record's CRC, which they lack today |
| I-3 When checked | eager at load; lazy at access; background | **Header, index and part-end records eagerly at open** (they are small: PR-12), **records lazily on first access**, and an optional **background verify** that walks all parts after open and feeds the status |
| I-4 On failure | refuse the file; open with holes | **Open with holes.** The damaged range is computed from the explicit dependencies (D5, D6): every frame whose dependency closure contains the damaged record is unreachable, and seeks into it fail with the reason. Damage in an ancillary stream (journal, coverage, screenshots) disables that stream, with the replay fallback, and does not fail the load |
| I-5 Streaming and crash safety | header rewrite vs footer; fsync policy; rename vs in-place finalize | **Header written once, never rewritten; everything known only at the end goes into the footer.** A record header carries its own CRC, so a torn record is recognized. The writer thread calls `fsync` once per closed part (off the emulator thread; cost measured by BM-1, §6). Finalize appends the index and footer, then renames (§5.2.7) |
| I-6 In-memory CRC | keep; drop | Decided by the Phase 1 measurement, as above |
| I-7 Algorithm | CRC32C; other | **CRC32C**, hardware path on SSE4.2 and ARMv8 with the existing table fallback (`ttdcompression.h:192-245`). zstd's own frame checksum stays off: it duplicates the record CRC |
| I-8 Tooling | — | `ttd.ksy`, the analyzer's `validate` and the fuzz test check exactly what the C++ reader checks (Step 6) |

**Versioning**

| Question | Recommendation |
|---|---|
| V-1 What carries a version | The container framing (`schemaVersion` in the header, 2 for this format); each stream's layout (u16 in the stream table); each device state layout (Phase 2 registry); the emulator's behavior version inside the configuration fingerprint (Phase 3, Step 4) |
| V-2 Directions | **New reader, old file:** every version since the promise starts is read; a device layout change gets an upgrade function when it is mechanical, otherwise that device restores as *degraded* (FR-7). **Old reader, new file:** opens it when every required stream is known, skipping unknown ancillary ones; refuses with the stream's name otherwise |
| V-3 Skippable content | A **required / ancillary flag per stream** in the stream table (PNG model). Device states carry the same flag per device type |
| V-4 Behavior vs layout | The behavior version is part of the fingerprint: a mismatch reports "restore exact, replay may differ" (FR-14) |
| V-5 Fixed sizes | The size check becomes "known version → known size" |
| V-6 When the promise starts | **At Phase 5** (users get the engine). Until then the engine's format is amended in place like v1's, with fixtures re-recorded; from Phase 5 on every change is a version bump, a converter or a documented refusal, and fixtures of each supported version stay in `testdata/ttd/` |
| V-7 Identifiers | Stream ids, region ids and device type ids are append-only tables: never reused, never renumbered. The file stores names next to ids (stream table, region table, device table), so a tool can name what it does not know |

v1 files keep `schemaVersion = 1` and stay readable only by the schema-1 reader kept for verification (D31, D32).

### 5.2 Step 2 — The file, written as it records

#### 5.2.1 Layout

```
┌─────────────────────────────────────────────────────────────┐
│ header: magic "TTDD", schemaVersion 2, session UUID,        │  written once
│ created, stream table, region table, device table,          │
│ configuration fingerprint, parent link, header CRC          │
├─────────────────────────────────────────────────────────────┤
│ part 0: [record s=pieces][record s=refblocks][s=checkpoints]│
│         [s=devicestates][s=events] ... [part-end]           │
│ part 1: ...                                    [part-end]   │  appended while
│ ...                                                         │  recording
│ part N: ...                                    [part-end]   │
├─────────────────────────────────────────────────────────────┤
│ index: part table, frame table, stream summary, index CRC   │  written at
│ trailer (24 B): index offset, size, CRC, magic "TTDX"       │  finalize
└─────────────────────────────────────────────────────────────┘
```

- **Magic and version.** The magic stays `TTDD`; `schemaVersion` becomes 2. A v1 build refuses the file with its existing message, "unsupported schema v2" (TTM:3985-3990), so an old emulator never misreads a new file.
- **Header** (fixed 64 bytes, then length-prefixed tables): magic, `schemaVersion` u16, flags u16, header size u32, header CRC32C u32, session UUID (16 B), creation time (u64, microseconds since 1970, UTC), zstd version (u32, `10507` for the embedded 1.5.7), then the tables. The header holds nothing that is known only at the end.
- **Record header** (32 bytes, little-endian):

  | Field | Type | Meaning |
  |---|---|---|
  | `sync` | u32 | `TREC`, to find records when scanning a damaged file |
  | `streamId` | u16 | from the stream table |
  | `flags` | u16 | bit 0 payload zstd-compressed, bit 1 part-end |
  | `partIndex` | u32 | part the record belongs to |
  | `sequence` | u32 | record number in the file, strictly increasing |
  | `storedSize` | u32 | payload bytes in the file |
  | `rawSize` | u32 | payload bytes after decompression |
  | `payloadCrc` | u32 | CRC32C of the stored payload |
  | `headerCrc` | u32 | CRC32C of the first 28 bytes |

- **Part-end record** (stream 0): first frame (u64), frame count (u32), branch (u16), the first item number of each item stream in this part, the offset of every record of the part, bytes per stream, and the sorted list of earlier parts this part depends on (varint deltas). A part is complete when its part-end record is present and both CRCs match.
- **Index** (finalized files): the part table (offset, size, first frame, frame count, branch, dependency list offset), the frame table (each frame's start in machine time, D21, and its part), per-stream totals. 12 bytes per frame in the frame table: 2.2 MB per hour at 50 frames per second (arithmetic).
- **Trailer** (24 bytes, last in the file): index offset u64, index size u32, index CRC32C u32, magic `TTDX`, a u32 trailer CRC. A reader looks at the last 24 bytes first.

#### 5.2.2 Streams

| Id | Stream | Kind | Phase that defines the content |
|---|---|---|---|
| 0 | Container: part-end records, index | required | 4 |
| 1 | Pieces (all regions): encoding, base item, content CRC, compressed bytes | required | 1 |
| 2 | Reference blocks (copy-on-write table blocks) | required | 1 |
| 3 | Checkpoints: per frame, CPU, chipset, region block-table changes, device-state references | required | 1, 2 |
| 4 | Device state versions | required | 2 |
| 5 | Events with payloads (input, external events, markers, bus data, network), and `IN` values in RZX mode | required | 3 |
| 6 | Configuration and media-version changes (the device set is fixed for a session, D38) | required | 3 |
| 7 | Write journal: segments and their records | ancillary (derived, D40) | 3 |
| 8–10 | Coverage: executed, written, read | ancillary (derived) | v1 format, carried over |
| 11 | Port journal index | ancillary (derived) | 3 |
| 12 | Bookmarks | ancillary | v1 format, carried over |
| 0x0100–0x01FF | Frame-boundary streams (§5.4); 0x0100 = screenshot | ancillary | 4 |
| 0x0200–0x02FF | **Reserved: branches** — branch table, non-trunk checkpoints and events, the parent link of a forked session (FR-23) | ancillary to a trunk reader | PLAN #76 |
| 0x0300–0x03FF | **Reserved: groups** — group table, member sessions (D22) | ancillary | later |

Every stream has its own layout version in the stream table. A reader without branch support reads the trunk and skips 0x02xx, so a branched file opens as its trunk (FR-23).

**Item references.** Every item (piece, reference block, device state version, event payload) has a u32 number, assigned in capture order per item stream. The part-end record gives the first number of each stream in the part; a reader finds an item by a binary search over the part table and an offset in the part's item directory. References in the file are 4 bytes, as in v1's checkpoints (`ttd.ksy`, `ram_sub_slots`), not 8-byte file offsets, which would double the reference-block bytes against D33. The width is checked: 4.29 billion items is 24 hours at an extreme 1,000 new items per frame (arithmetic); the writer refuses to go past it and the session continues as a new linked file.

#### 5.2.3 Parts cut by dependencies

A part closes at the first frame boundary at which it holds **50 frames or 4 MB**, whichever comes first. Both limits are parameters (`TTDFileParams::partFrames`, `partBytes`). They bound what a crash loses (about one second) and what one record costs: about 10 record headers and one part-end record per part, roughly 8 bytes per frame on an idle session (arithmetic), inside the PR-10 limit of 64 bytes per unchanged frame together with the 12-byte frame table entry and the checkpoint record.

A part is not required to be independent of earlier parts: a piece that last changed an hour ago keeps its base in a part from an hour ago. Instead, **every part lists the earlier parts it depends on** (D6). This replaces target-architecture §7's "cut at chain-cap boundaries", which per-piece limits do not have. The list is computed from the dependencies Phase 1 records for every item (D5): when the writer serializes an item whose base lives in part p, p joins the current part's list. The list is used:
- to know which parts a seek needs (read-ahead, §5.3.4);
- to compute the frames a damaged record makes unreachable (I-4);
- to decide what a rolling file must copy forward (§5.3.5).

Part boundaries depend only on frame counts and content bytes, never on time or on how far the writer thread has got, so the same recording gives the same file (QR-3).

#### 5.2.4 The writer thread

```cpp
// core/src/debugger/ttd/engine/ttdsessionwriter.h
class TTDSessionWriter
{
public:
    bool Open(const std::string& utf8Path, const TTDFileHeader& header, std::string& error);
    void PublishFrame(uint64_t frame);      // emulator thread: one atomic store per frame
    TTDWriterStatus GetStatus() const;      // written bytes, lag, last error
    bool Finalize(std::string& error);      // drain, write index + trailer, fsync, close
    void Discard();                         // stop, close, delete the file
private:
    void Run();                             // writer thread
    std::atomic<uint64_t> _publishedFrame;
    std::atomic<uint64_t> _durableFrame;    // last frame whose part is written and fsynced
    TTDFileHandle _file;                    // platform handle, see 5.2.8
};
```

- **No queue copies.** The engine allocates items in arena blocks in capture order (Phase 1). An item is immutable once its frame is published. The emulator thread publishes "frame N is complete" with one atomic store; the writer thread walks each stream's log from where it stopped up to frame N and appends. Capture does no extra work per item: the counted work `bm2_work_*` does not change (D33).
- **Already compressed.** Pieces and device states arrive compressed from capture. Small structured records (checkpoints, reference blocks, events) are compressed by the writer thread when it closes a record, off the emulator thread. Nothing is compressed twice.
- **Large writes, no flush on the hot path.** The writer appends whole records and calls `fsync` once per part on its own thread.
- **Unwritten items stay in memory.** An arena block is pinned until the writer has made all of it durable. The bytes between `_durableFrame` and `_publishedFrame` are the *writer lag*.

**When the disk is slower than capture.** E6's heaviest rate, 37 MB per minute, is about 0.6 MB per second, which any local disk sustains. The real cases are stalls (a sleeping USB disk, a network share), a full disk and write errors.

| Writer lag | Behavior |
|---|---|
| below `lagSoftBytes` (default 64 MB) | normal |
| above the soft limit | recording continues; the lag counts against the memory budget; the status reports "file is behind by N MB" |
| above `lagHardBytes` (default: the memory budget) | recording **stops** at the next frame boundary with the reason "the disk cannot keep up"; history is kept, the writer keeps draining |
| write error or disk full | the writer stops; the file stays valid up to its last complete part; the session continues in memory only (§5.3.1) and reports why |

Capture never blocks, never waits on a lock the writer holds, and never drops data silently.

#### 5.2.5 Byte-identical files

- Every field is written explicitly, little-endian, field by field. No struct is written as raw memory; v1's `WritePod` of `TTDCpuState` / `TTDChipsetState` (TTM:3768-3769) is not reused.
- zstd is the embedded 1.5.7 (`CMakeLists.txt:391-400`), single-threaded, level 1: the same input gives the same bytes on every platform. The header records the zstd version.
- Map iteration order never reaches the file: device states sorted by type id and instance name, streams by id.
- QR-3: two recordings of the same scripted workload differ only in the UUID and the creation time. QR-2: CI records the same workload on macOS, Linux (Docker) and Windows and compares file hashes with those two fields masked.

#### 5.2.6 Crash recovery

Opening a file:
1. Read and check the header.
2. Read the trailer. If it is valid, read the index: done.
3. Otherwise **scan**: walk records from the end of the header by their sizes, checking each header CRC. Stop at the first bad header, the first `sequence` out of order or the end of the file. Keep every part whose part-end record was reached; drop records after the last one. Build the index in memory.

The scan reads only record headers (32 bytes each, about 10 per part), so an unfinished 1 GB file opens within PR-12's 2 s; BM-7's `bm7_first_seek_ms` gets a "no footer" case.

A recovered file opens **read-only**. "Repair" is an explicit action that truncates the incomplete tail and appends an index and trailer; nothing is changed by just opening.

#### 5.2.7 Location, save and delete (D28, D30)

- **Location (owner decision 2026-10-04).** A per-user folder on every system: `~/.unreal-ng/` (Windows: `%USERPROFILE%\.unreal-ng\`), with `ttd/` inside it:
  - each recording writes into its own folder, `ttd/<date-time>-<name>/` (for example `ttd/2026-10-04-153012-pentagon/`); `<name>` is the model's short name, `-2`, `-3` added on a collision. A black box keeps its ring of segment files there (§5.3.5);
  - finalized (saved) recordings are files in `ttd/` itself, `ttd/<date-time>-<name>.ttd`, unless the user picks another path (the UI part is Phase 5, Step 3).

  Recording folders keep the disk tidy: everything one session wrote is in one place and goes with it.
  The folders come from new cross-platform `FileHelper` methods (owner decision 2026-10-04): the user's home (`HOME`; Windows `USERPROFILE`, through the wide API), the per-user `.unreal-ng` folder and its subfolders, created on demand; UTF-8 paths in and out, as every `FileHelper` path.
- **Cleanup (owner decision 2026-10-04).** A `CleanupManager` in `core/src/common/` runs registered cleanup steps asynchronously at startup, on a background thread, so start-up never waits for it. TTD's step is the first one; other subsystems add theirs.
  - Every step catches its own errors and exceptions. A file that cannot be deleted (in use, permissions) is logged and skipped; the next run tries again.
  - The manager records its last run per step, and runs each step at least once a week.
  - Automation cleans up after itself: invalidating a session deletes its folder, and the recipes say so (`.recipe/analysis/ttd-recording.md`, Pitfalls). The startup step is only the safety net.
  - TTD's step deletes the folders of crashed recordings (no footer, no live owner: a lock file with the writing process's id) once they are older than 7 days. Until then they are listed and can be opened or repaired.
- **Save** = finalize, then rename to the chosen path. The writer closes its handle and every mapped view first, so the rename works on every platform; the read-only view is reopened at the new path. A rename across disks (`std::errc::cross_device_link`) falls back to a copy in the background with progress, then deletes the source. "Save" during a recording records the target name; the rename happens when the recording stops.
- **Delete**: discarding a session closes and deletes its file.
- **Leftovers**: a recording folder without a footer and without a live owner, found in `~/.unreal-ng/ttd/` at startup, is a crashed recording. It is listed (status, and the UI in Phase 5) and can be opened, repaired or deleted; the cleanup step removes it after 7 days.

#### 5.2.8 Platform layer

| Need | POSIX (macOS, Linux) | Windows |
|---|---|---|
| UTF-8 path | native | `FileHelper::ToFsPath(path)` → `wstring` → `CreateFileW` |
| Append handle | `open(O_WRONLY \| O_CREAT \| O_EXCL)`, `write` | `CreateFileW(GENERIC_WRITE, FILE_SHARE_READ \| FILE_SHARE_DELETE)`, `WriteFile` |
| Durable | `fsync` (`F_FULLFSYNC` is not used: cost) | `FlushFileBuffers` |
| Read-only view | `mmap(PROT_READ)` per window | `CreateFileMappingW` + `MapViewOfFile` per window |
| Read-ahead | `madvise(MADV_WILLNEED)` | `PrefetchVirtualMemory` |
| Rename | `std::filesystem::rename` on `ToFsPath` paths, after all handles are closed | same; with retries, since another process (indexer, antivirus) may briefly hold the file |

- Files: `core/src/common/mappedfile.h` (interface), `core/src/platform/posix/mappedfile_posix.cpp`, `core/src/platform/windows/mappedfile_windows.cpp`, following the existing platform layout (`core/src/platform/`).
- **Mapping a growing file**: the view maps fixed 64 MB windows, each only once the writer has made it durable. The window holding the tail is remapped when it grows. On Windows a mapping cannot extend past the file's size, so this rule is what makes it work there.
- Paths are UTF-8 `std::string` everywhere and become `std::filesystem::path` only through `FileHelper::ToFsPath` (the rule of `filehelper.h:10-14`).

### 5.3 Step 3 — Memory as a cache of the file

#### 5.3.1 Storage modes and retention

```cpp
enum class TTDStorageMode : uint8_t { FileBacked, MemoryOnly };
enum class TTDRetentionKind : uint8_t { MemoryBudget, TimeWindow };

struct TTDRetentionPolicy
{
    TTDRetentionKind kind = TTDRetentionKind::MemoryBudget;   // D11: the default
    uint64_t memoryBudgetBytes = 512ull << 20;                // proposed, see 5.3.6
    uint32_t windowFrames = 0;                                // TimeWindow: last N minutes, in frames
};
```

D28 makes the file the normal case and D11 makes the memory budget the default policy. They meet as follows:

| | FileBacked (default) | MemoryOnly |
|---|---|---|
| Used by | every recording users start | tests, a v1 file read into memory buffers (D31), and the fallback after a write error |
| MemoryBudget | the budget bounds the **cache**: old data is evicted and read back from the file; no history is lost | the budget bounds the **history**: the oldest history is released, with rebasing |
| TimeWindow (black box, D11, D29) | rolling files (§5.3.5): history older than N minutes is released from memory and disk | history older than N minutes is released, with rebasing |

The window is counted in frames from the frame table, not in host seconds, so a paused emulator keeps its history.

#### 5.3.2 What can leave memory

| State of an arena block | In memory | Can be evicted |
|---|---|---|
| unwritten (writer lag) | items | no |
| written, resident | items, and their slot entries | yes |
| evicted | slot entries only; payloads through the mapped view | slot table pages, too |
| cold (opened file, or slot pages evicted) | nothing but the part table and the frame table | — |

- Eviction works by **whole arena blocks**, oldest first, as the user's direction asks (target-architecture §6.1): the cost does not depend on how many items a block holds.
- An evicted item keeps its slot id; its payload pointer becomes a file location. The decoder decompresses straight from the mapped bytes; nothing is copied back into the arena.
- The slot table is cut into pages of 65,536 slot ids. A page whose items are all evicted can itself be dropped and rebuilt from the parts' item directories on demand: at the heaviest rates the slot table alone could otherwise reach about 100 MB per hour (16 bytes per slot, E6's slot header, at an assumed 100,000 new items per minute; to be measured, `bm4_heap_slot_table_bpf`).
- Never evicted: the newest block, unwritten blocks, the delta base (Phase 1, Step 4), the frame table, the part table.

#### 5.3.3 Real accounting (FR-16)

```cpp
struct TTDMemoryReport                       // engine counterpart of TTDHeapBreakdown (TTM.h:161)
{
    size_t pieceContent = 0, slotTable = 0, referenceBlocks = 0, deviceStates = 0;
    size_t checkpoints = 0, events = 0, writeJournal = 0, coverage = 0, frameStreams = 0;
    size_t deltaBase = 0, frameTable = 0, partDirectories = 0, scratch = 0;
    size_t arenaUnused = 0;                  // tail of the newest block, holes before compaction
    size_t writerLag = 0;                    // subset of the above: not yet durable
    size_t mappedResident = 0;               // OS page cache through the view: reported, NOT in Total()
    size_t Total() const;                    // what the budget counts
};
std::vector<TTDPartMemory> GetPartMemory() const;   // per part: residency, bytes per stream
```

- Every byte comes from the arena or from a fixed table, so the total is counted, not estimated; FR-16's ±5% is checked against the allocator in a test (§7).
- The benchmark reports each field as `bm4_heap_<field>_bpf`, with the E5 names where the part exists in both engines (`ram_payload` ↔ `pieceContent`, `page_refs` ↔ `referenceBlocks`, …), so `ttd_bench_compare.py` compares v1 and the engine part by part (D33).
- `mappedResident` is memory the operating system can drop at any time; it is shown, but the budget does not count it.

#### 5.3.4 Seeking into evicted history

1. The frame table gives the target's part.
2. The part's dependency list gives every earlier part the restore can touch. The view issues read-ahead for all of them at once (§5.2.8), so the disk reads them in a few large requests instead of thousands of 4 KB faults.
3. Restore runs as in memory (Phase 1, Step 5: only the pieces that differ), decoding from the view.

Target: PR-8, p99 ≤ 20 ms on an SSD. Measured by a new BM-5 variant, `bm5_cold_us_{p50,p99}`, after evicting everything but the newest block. PR-7 in file mode: seeks to positions still in memory stay flat with session length.

#### 5.3.5 Releasing history: rebasing and rolling files (D5)

When the session start moves from S to S′, an item still needed at or after S′ may depend on an item from before S′: the base of a chain, a reference block or device state shared since then, an event payload. Before anything is freed:

1. **Find survivors.** Walking the oldest arena block: items referenced by any checkpoint at or after S′ (reference counts, Phase 1).
2. **Rebase.** A surviving XorPrev piece whose chain reaches before S′ is decoded and re-encoded Full. A surviving Full item is copied as is. Either way it moves into the current arena block **under the same slot id**, so no reference changes.
3. **Free** the block.

- Steps 1 and 2 run on a background *history worker*; they only read immutable items. The emulator thread swaps the payload pointers at a frame boundary, one pointer per survivor; that count is reported (`bm2_work_rebased_opf`).
- In MemoryOnly mode this is how the memory budget and the time window release history. Releasing block by block also compacts the arena: survivors move to the newest block.
- **Rolling files** (FileBacked + TimeWindow, the black box): a session writes segment files `<name>.ttd`, `<name>.ttd.1`, …; a new segment starts every N minutes with a baseline part made of the survivors, rebased as above, and the segment two windows back is deleted. Disk use stays below about 2N minutes of recording. "Save" of a black box writes one file from the segments still in the window (items are copied, not re-encoded). This is a proposal, see the open question in §9.
- The earliest kept position is reported on every surface (FR-15, D12).
- Branches later: a pinned branch and the active branch's newest block are never released; nothing here assumes one line of history (FR-22).

#### 5.3.6 The budget

- Counted in arena blocks of 64 MB plus the fixed tables; status reports "blocks in use / limit" (target-architecture §6.1).
- Default proposed: **512 MB**, the figure the what-if design uses as its example ([model-what-if §4.6](../2026-09-29-model-what-if/design.md)); E6 puts a minute of heavy recording at 35–41 MB of memory, so 512 MB keeps the last 12–14 minutes in memory (arithmetic). The value is the user's decision (§9).
- Done-when check: a one-hour ZX-Evo recording stays within the budget (benchmark run, BM-4 sampled every minute).

### 5.4 Step 4 — Optional frame-boundary streams (D19)

```cpp
struct TTDFrameStreamDesc
{
    uint16_t streamId;                // 0x0100..0x01FF, fixed table, never reused
    const char* name;                 // "screenshot"
    uint16_t layoutVersion;
    // emulator thread, at the frame boundary: copy what the stream needs, nothing else
    void (*capture)(EmulatorContext& ctx, TTDFrameStreamBuffer& out);
    // writer side: turn the copy into a record payload (compression happens here)
    void (*encode)(const TTDFrameStreamBuffer& in, const TTDFrameStreamBuffer* previous, std::vector<uint8_t>& out);
};

class TTDFrameStreamRegistry
{
public:
    void Register(const TTDFrameStreamDesc& desc);
    bool SetEnabled(uint16_t streamId, bool on);   // takes effect at the next frame boundary
    uint64_t EnabledMask() const;                  // read once per frame
};
```

- **Zero cost when off.** At the frame boundary the engine reads one mask: `if (_frameStreamMask != 0) CaptureFrameStreams();`. Nothing is done per event and nothing on restore: these streams are ancillary and never needed to restore a position.
- **Switched at run time.** A change takes effect at the next frame boundary. The part-end record lists which frame-boundary streams each part holds, so "frame 90,000 has no screenshot, the stream was off" is answered without scanning.
- **Not cached in memory.** A frame-boundary stream is write-through: its copies are held only until the writer has written them, and they count in the report as `frameStreams`. In MemoryOnly mode they stay in memory and count against the budget.
- **Controls on every surface.** The engine provides `ListFrameStreams`, `SetFrameStreamEnabled`, `ReadFrameStream(streamId, frame)`. The surfaces (WebAPI `GET/PUT /api/v1/emulator/{id}/ttd/streams` and `GET .../ttd/streams/screenshot?frame=N` returning a PNG, MCP `time_travel` action `streams`, CLI `ttd stream`, Lua, Python, a Qt toggle in the TTD widget) are wired when the surfaces move to the engine in Phase 5, Step 1, with their docs, OpenAPI and a `.recipe/` entry. Until then the engine API is exercised by tests and the benchmark.

**The first stream: a screenshot per frame.**

- What: the final picture of frame N, the one a seek to "frame N" shows (D13).
- Capture: copy the framebuffer. On Pentagon and 48K it is 352 × 288 pixels (`core/src/emulator/video/screen.h:478-479`) of RGBA8, 405,504 bytes; the copy is the only work on the emulator thread.
- Encode, on the writer thread: XOR with the previous frame's copy, zstd level 1; a full picture every 50 frames, so reading one screenshot decodes at most 49 differences. Width, height and video mode go into each record, so mode changes (TS-Conf, Sprinter) need no special case.
- Size: **to be measured** per matrix case, as `bm3_stream_screenshot_bpf`. Reference points: an unchanged frame's XOR is all zero and compresses to a few dozen bytes; the clip export stored *Across the Edge* at about 5.6 KB per frame (RGBA and plane B, zstd level 3 over 500-frame chunks, §3.4). At 5.6 KB per frame the stream would add about 17 MB per minute (arithmetic), the size of the busiest other streams, which is why it is off by default.
- Cost when on: the 405 KB copy per frame, measured as BM-1 with the stream on (`bm1_overhead_screenshot_pct`).

### 5.5 Step 5 — v1 files into the engine (D31)

- The schema-1 reader of Phase 1, Step 1 already feeds the engine frame by frame from a v1 file. This step adds two sinks:
  - **an engine file**: the engine records the fed frames in FileBacked mode, so a v1 fixture becomes an engine file without re-recording;
  - **memory buffers**: MemoryOnly mode, for tests and the benchmark that want no file.
- v1 data keeps buffers of its own: the reader's decoded v1 checkpoints and pieces are held apart from the engine's, so the oracle can restore the same frame from both and compare them byte for byte (D33).
- What v1 does not store is marked, not invented: no device regions for NeoGS / MoonSound wave memory / EEPROMs, no configuration fingerprint, no media versions. The converted file records "converted from schema 1" in its header; replay from it reports "not bit-exact" where Phase 3's checks need data v1 lacks (FR-14).
- Exposed to the verification tools only (tests, `core-benchmarks`, the comparison scripts), not to users (D32).

### 5.6 Step 6 — `ttd.ksy` and the analyzer

- **`ttd.ksy`** describes schema 2: header and tables, records as a `repeat: eos` sequence switched on `streamId`, every known stream's payload, the part-end record, the index and the trailer (found through `instances` at `_io.size - 24`). Unknown stream ids parse as opaque bytes. The trailing-sections gap of schema 1 (`ttd.ksy:174-180`) is not repeated: every stream is modeled.
- The schema-1 description stays, renamed `ttdschema1.ksy`, as long as the schema-1 reader exists for verification (D31).
- **Analyzer** (`tools/verification/ttd-analyzer`): a new reader module `ttdcontainer.py` for schema 2 next to `ttd_format.py` (schema 1); `info`, `validate`, `render` and `search` dispatch on `schemaVersion`. `validate` checks exactly what the C++ reader checks: header, index and trailer CRCs, every record CRC, part completeness, dependency lists against the items' real bases, every piece's content CRC after decoding its chain, stream table flags. New commands: `parts` (parts, sizes per stream, dependencies), `recover` (what a scan keeps from an unfinished file), `screenshot` (writes PNGs from stream 0x0100).
- **Conformance (QR-5):** a test writes files with the C++ writer (finished, unfinished, with an unknown ancillary stream) and runs `validate` on them; a Kaitai-generated parser parses the fixtures.
- **Fuzz (QR-4):** random bit flips, truncation and oversized sizes in every stream, against the C++ reader and the analyzer: no crash, no allocation above the record's `rawSize` cap, and every flip in a CRC-covered byte reported by both, with the same location.

### 5.7 File-format consequences and fixtures

- The fixture corpus (`testdata/ttd/`, [README](../../../testdata/ttd/README.md)) is re-recorded in schema 2 at the end of the phase. The schema-1 fixtures stay for the schema-1 reader and the oracle.
- From Phase 5 on, the compatibility promise of Step 1 applies (V-6).

## 6. Performance

| Runs | Where | Cost |
|---|---|---|
| Per frame, recording | emulator thread | one atomic store (`PublishFrame`); one mask check for frame-boundary streams; a frame-table entry |
| Per item | emulator thread | nothing beyond Phase 1–3 capture; items are not copied for the file |
| Per part (about 1 s) | writer thread | record headers, compression of small records, one `fsync` |
| Release of history | history worker; emulator thread swaps pointers | decode + encode of survivors; one pointer per survivor at a frame boundary |
| Seek into evicted history | emulator thread | read-ahead of the dependency parts, decode from the view |

**Zero cost when off:** a frame-boundary stream that is off costs one mask check per frame and nothing per event; MemoryOnly mode starts no writer thread.

**How it is measured** (benchmark matrix, [ttd-bench](../../../tools/verification/ttd-bench/README.md)):
- `bm1_overhead_*_pct` in FileBacked and in MemoryOnly mode: the done-when "recording to the file costs no more frame time than recording to memory";
- `bm2_work_*` unchanged by the file (deterministic, CI gate); new `bm2_work_rebased_opf`;
- `bm4_heap_*_bpf` with the engine's parts, and `bm4_resident_bytes` sampled against the budget;
- `bm5_cold_us_*` for PR-8; `bm7_file_bpf` per stream against v1 (D33), `bm7_first_seek_ms` with and without a footer (PR-12);
- new: `bm7_writer_lag_bytes_max`, `bm7_writer_stall_events`, `bm3_stream_screenshot_bpf`, `bm1_overhead_screenshot_pct`.

Timings are taken on an idle host (load below 12) and run twice, per the benchmark rules.

## 7. Tests

Every test is checked by mutation: it must fail when the mechanism it guards is removed.

| Step | Test | Proves |
|---|---|---|
| 2 | Record → file → open in a fresh engine: every frame of the corpus restores byte for byte as from memory and as from v1 | the file holds everything (D33 correctness) |
| 2 | Kill test: a child process records and is killed (`SIGKILL` / `TerminateProcess`) at random moments; the parent opens the file | FR-13: loads up to the last complete part; mutation: write part-end before its records → fails |
| 2 | Truncate an unfinished file at every offset inside its last two parts | a torn record is never accepted |
| 2 | Slow sink: a writer that sleeps per record | capture never waits (`captureWaits == 0`), the lag is reported, recording stops at the hard limit with its reason; mutation: make `PublishFrame` wait → fails |
| 2 | Disk full / write error injected | the session continues in memory, the file is valid to its last part |
| 2 | Record the same workload twice | identical files except UUID and creation time (QR-3) |
| 2 | Same workload on macOS, Linux, Windows (CI) | identical file hashes, fields masked (QR-2) |
| 2 | A file with an unknown ancillary stream; with an unknown required stream | the first opens and restores, the second is refused naming the stream; mutation: ignore the flag → fails |
| 2 | A file with a synthetic 0x0200 stream | opens as its trunk (FR-23) |
| 2 | Save to a path with non-ASCII characters; save across volumes; discard | renamed / copied / deleted; FileHelper paths |
| 2 | Part boundaries | depend on frames and bytes only: the same recording with a slowed writer gives the same file |
| 3 | Small budget in FileBacked mode | blocks evicted, every frame still restores exactly through the view |
| 3 | MemoryOnly release | survivors rebased, every kept frame restores; mutation: skip rebasing → the first kept frame fails |
| 3 | Reference counts after release | no leak, no early free |
| 3 | Time window, MemoryOnly and rolling files | only the window is kept; disk use stays below 2 windows |
| 3 | Memory report | its parts sum to the arena blocks and tables; within ±5% of the allocator's own count (FR-16) |
| 3 | One-hour ZX-Evo recording (benchmark) | stays within the budget (done-when) |
| 4 | Stream off | the capture function is never called, no record is written; mutation: drop the mask check → the call count fails |
| 4 | Stream switched on and off mid-session | frames in the on-range have screenshots, the others report "not recorded" |
| 4 | Screenshot of frame N | equals the picture a seek to frame N shows, byte for byte (D13) |
| 5 | Every schema-1 fixture → engine file and → memory buffers | every frame restores as v1 restores it (D33) |
| 5 | Converted file size against the v1 file, per fixture | not larger for the same history kept (D33), or listed as a fixed ratio |
| 6 | Conformance: analyzer `validate` on C++-written files | QR-5 |
| 6 | Fuzz the C++ reader and the analyzer | QR-4: no crash, bounded allocation, same detections |

**Comparison with v1 (D33)** at the end of the phase, on the whole matrix: correctness by the oracle; file size per stream and per frame of history kept (v1's journal ring is accounted for as D33 describes); memory not larger than v1's for the same history kept; `bm2_work_*` not larger; seek within PR-5 and PR-8. The file's fixed costs (header tables, part-end records, frame table) are compared in bytes per minute; if they make a very short recording larger than v1's, that is the fixed-header ratio D33 already lists, and it is recorded there with its number.

## 8. Order of work

Each item is a working state that passes the full gate (build with zero warnings, `core-tests`, the benchmark gate).

1. **Step 1:** the user decides integrity and versioning (§5.1, §9).
2. **Container, written synchronously** (Step 2 without the thread): header, records, parts, index, reader with scan recovery. An engine session can be saved and loaded.
3. **`ttd.ksy` and the analyzer reader for it** (Step 6, first part), so the format has one source of truth from its first byte.
4. **v1 files in** (Step 5): the corpus converts to engine files; every later test has inputs without re-recording.
5. **Writer thread and crash safety** (Step 2): lag limits, kill test, deterministic part boundaries, location / save / delete, the platform layer on all three systems.
6. **Eviction and the mapped view** (Step 3, first part): FileBacked with a budget, cold seeks, the memory report.
7. **Release and rebasing** (Step 3, second part): MemoryOnly budget, time window, rolling files.
8. **Frame-boundary streams** (Step 4), screenshot first.
9. **Fuzz and conformance** complete (Step 6), fixtures re-recorded, full matrix run and stored as the Phase 4 baseline, results document.

## 9. Risks and open questions

| # | Risk / question | Plan | User's decision? |
|---|---|---|---|
| 1 | Integrity and versioning (§5.1): what is checked, open-with-holes | **decided 2026-10-04: the recommendation in §5.1** (CRC32C per record, open with holes). No compatibility promise before the release (owner decision 2026-10-03): the format changes freely until then, the v1 → v2 converter is temporary | decided |
| 2 | Default memory budget (512 MB proposed) and the black box's default window | measure the one-hour ZX-Evo run; propose numbers with it | **yes** |
| 3 | Black box on disk: rolling segment files (proposed) or MemoryOnly with a time window and a file only on save | **decided 2026-10-04: rolling segment files**, in a folder of their own per recording under `~/.unreal-ng/ttd/` (§5.2.7) | decided |
| 4 | The TTD folder grows: one folder per recording, crashed leftovers, about 2 GB per hour of heavy content | **decided 2026-10-04**: per-recording folders under `~/.unreal-ng/ttd/`, saved recordings as files there; an asynchronous `CleanupManager` at startup (steps from any subsystem, errors caught per step, each step at least weekly) removes crashed recordings older than 7 days (§5.2.7) | decided |
| 5 | Whether automation-started recordings are FileBacked like UI ones or MemoryOnly by default | **decided 2026-10-04: FileBacked everywhere** (D28), MemoryOnly as an explicit start option; tests use it or write under `scratch/` | decided |
| 6 | Cold seeks touch many parts: a piece's chain can reach parts from long ago | read-ahead of the dependency list; `bm5_cold_us_*` against PR-8; if it fails, the writer re-anchors pieces whose base is many parts back (stores them Full), at a byte cost D33 must allow | no |
| 7 | Windows: mapping a growing file, rename with open handles, other processes holding the file | 64 MB windows mapped only when durable; all handles closed before rename; retries; tested on Windows in CI, not only under Wine | no |
| 8 | `fsync` per part costs frame time on slow disks | it runs on the writer thread; BM-1 in FileBacked mode shows it; a setting can lower it to "on finalize only" for power-loss tolerance traded away | no |
| 9 | zstd version changes break byte identity | the version is in the header; QR-2/QR-3 compare files from the same build; a zstd update re-records fixtures | no |
| 10 | The write journal decides most of the file rate on active content | Settled by D40: off by default, kept only in the segments asked for | no |
| 11 | Frame-boundary stream controls on every surface (D19) while the surfaces still drive v1 | engine API and tests in Phase 4; surfaces wired in Phase 5, Step 1 | no |

## 10. Sources

- Code: master `8ddaf708e`, references above (`timetravelmanager.{h,cpp}`, `ttddumpformat.h`, `ttd.ksy`, `ttdcompression.h`, `ttdclipexport.cpp`, `core/src/common/filehelper.h`, `core/src/emulator/video/screen.h`, `core/src/platform/`).
- Decisions and requirements: [engine-decisions.md](engine-decisions.md), [requirements.md](requirements.md), [integrity-and-versioning.md](integrity-and-versioning.md), [target-architecture.md](target-architecture.md) §6–8 (its chunk layout and "cut at chain-cap boundaries" are superseded by D6 and §5.2), [current-state.md](current-state.md) §9 (bit-flip experiment).
- Measurements: [E5](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e5-heap-split/README.md) (heap split, `bm4_heap_*`), [E6](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e6-v1-v2-model/README.md) (engine file and memory per minute), [p0a-plane-b.md §8](../2026-09-27-zxdlss-gigascreen/p0a-plane-b.md) (clip export size), [ttd-bench README](../../../tools/verification/ttd-bench/README.md) (metric names).
- Prior art named by the investigation: PNG critical / ancillary chunks (stream flags), Matroska (unknown-element skipping, streaming), QEMU VMState (layout versions, optional subsections).
- Memory direction: [target-architecture §6.1](target-architecture.md#61-memory-as-linked-blocks-direction-2026-09-29), [model what-if §4.6](../2026-09-29-model-what-if/design.md).
