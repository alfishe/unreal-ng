# TTD container format: streams, frames and muxing

> TTD index: [docs/emulator/design/debugger/time-travel-debug/README.md](README.md)

| | |
|---|---|
| **Status** | History. Section 2 describes v1's file, which nothing reads since v1 was deleted (2026-10-30). Sections 4-6 are the proposal that led to the engine's container, built differently: see [As built](#as-built-2026-10-31) |
| **Last updated** | 2026-11-23 (seek data of segment files; chunked streams 2026-11-16; as-built note 2026-10-31; text 2026-08-20) |
| **Parent TDD** | [time-travel-debugging-tdd.md](./time-travel-debugging-tdd.md) §6, §9 |
| **Schema** | `core/src/debugger/ttd/ttd.ksy` (authoritative for the current layout: the engine's schema 2, not the layout in section 2) |

---

### As built (2026-10-31)

The engine's session file is a container of numbered streams, as proposed here, built in
`core/src/debugger/ttd/engine/ttdcontainer.*` (schema 2, [`ttd.ksy`](../../../../../core/src/debugger/ttd/ttd.ksy)):

- a header (magic `TTDD`, schema 2, session UUID, stream table, session tables), then records grouped into parts,
  each part closed by a part-end record naming its frames and the parts it depends on, then an index and a trailer
  (finished files only);
- every stream is required or ancillary: an unknown required stream refuses the file, an unknown ancillary one is
  skipped;
- CRC32C on the header, on each record's header and payload, and on the index; a file without its trailer opens by
  scanning its records, and a damaged part is a hole (it and the parts that depend on it are unreachable);
- a recording is written as it records, one file per segment in its recording folder (`engine/ttdrecordingwriter.*`);
- a record holds at most 1 GiB raw. A larger payload of a stream S is stored as several records of S's *chunked
  twin*, stream `S | 0x8000` (see [Chunked streams](#chunked-streams-2026-11-16)).

### Chunked streams (2026-11-16)

A session of 139 382 frames could not be dumped: its write journal, written whole with the session's last part,
passed the record limit (about 8.3 KB raw per frame, so 1 GiB at about 130 000 frames; 96 503 frames gave 797 MB).
The container now splits such a payload:

| Rule | |
|---|---|
| Twin | stream `0x8000 + S`, declared in the header as ancillary, with S's layout and the name `chunked-<S's name>` |
| Records | limit-sized pieces, back to back in one part, consecutive sequence numbers; each compressed or not on its own |
| Flags | bit 2 (*continued*) on every record but the last |
| Readers | join the run into one payload of S (C++ `TTDContainerReader`, Python `ttdlib`, the analyzer); `TTDPartRef::records` is the joined view, `physical` every record as stored (verbatim copies, joins of a recording's segment files) |
| Damage | a run cut before its last record, or a continued record outside a twin, makes the part damaged |
| Older readers | skip the twin as an unknown ancillary stream: the file reads as one without that payload, never a chunk as the whole |
| Declared | by the session writer for the write journal and every holder stream (coverage, bookmarks, facts, branch state) and their branch twins: the streams written whole with the last part. Required streams are not declared (an older reader would refuse every new file); their records are per part and stay small |

Writer failures carry a reason (`TTDContainerWriter::Error()`, `TTDWriteError`): `Io` with the system's words,
`NoSpace` only for a full disk or quota (ENOSPC / EDQUOT, `ERROR_DISK_FULL`), `TooLarge` with the stream and the
sizes (a stream over the limit without a declared twin), `Layout` for calls out of order. The record limit is
settable for tests (`SetRecordLimit`, `TTDSessionSaveParams::recordLimit`). Tests:
`core/tests/debugger/ttd/engine/ttdcontainer_chunks_test.cpp`, `ttdsessionfile_chunks_test.cpp`,
`tools/python/ttd/tests/test_chunks.py`; fixture `testdata/ttd/v2/chunked/`.

### Seek data of a recording folder's segment file (2026-11-23)

A seek into history that left memory reads its segment's file back. The file was read from its start up to the
checkpoint wanted: on average half a segment's records decompressed, checked, imported and their reference tables
rebuilt (4 ms of a 5 ms seek at 240k frames). A segment file now carries what lets a reader take one stretch of it
(P1.S10 seek format; owner decision 2026-11-23: the format may change in place, schema 2 is not bumped, older
recordings need not stay readable; in fact a file without the seek data still reads, from its start).

| Part | Where | What |
|---|---|---|
| Piece tables | stream 2, ancillary (`piece-tables`; `0x0202` in a branch's file), one record in each part that has a table | For every checkpoint of the part that holds a full reference table of every region (the segment's first, a branch's fork copy, every checkpoint whose number is a multiple of the snapshot interval, 64): the checkpoint's place in the part, then per region every piece's current version as its number in the file + 1 (0: none). `payload_piece_tables` in `ttd.ksy` |
| Seek index | the container's index extra | Magic `0x53`, version 1, the offset the container's index starts at, the file's line, then per part: offset and stored size of its piece, checkpoint and table records, the number of its first version and how many it holds, its first frame, the frames its tables are of. `seek_index` in `ttd.ksy` |

- **Read** (`TTDSessionFile::ReadSegmentWindow`, `TimeTravelEngine::ReadBackSegmentWindow`): the trailer and the
  index (two reads), the table of the last full-table checkpoint at or before the one wanted, the checkpoint records
  of the parts from there to the next full-table checkpoint, then the piece records of the parts that hold a version
  the table or those checkpoints name, or a version one of them builds on, found from the last part back. Only those
  versions are imported. Each record is read in one read at the offset the index gives, its header (CRC, stream,
  size) and payload CRC checked as any record's.
- **Which files:** the files a recording folder writes (`TTDSessionSaveParams::fileLine` set). A saved file
  (`Save`, a join) has neither stream 2's declaration nor a seek index, and is byte for byte what it was.
- **A copy whose records moved** (a join, a part dropped by a tool) fails the index-offset check: the seek index is
  ignored and the file read from its start, as an unfinished file (no trailer) and a file written before
  2026-11-23 are. The Python tools copy stream 2 like any ancillary stream; `join` writes no index extra.
- **Cost in the file:** a table is a few bytes a piece (about 0.4 KB for a 128K machine with its devices), one per
  64 checkpoints; the seek index about 20 bytes a part.
- **Checks:** `check_ksy.py` compares every table with the versions the checkpoints before it give each piece and
  every seek index entry with the records it names; tests `core/tests/debugger/ttd/engine/ttdsegmentwindow_test.cpp`
  (every checkpoint of an evicted segment read back alone, a branch's file, a file without the index, a copy whose
  records moved, damage) and `TTDSeekScaling_Folder_Test.ASeekIntoEvictedHistoryCostsAboutASeekInMemory`.

Design: [phase-4-session-file-tdd.md](../../../../inprogress/2026-09-25-ttd-v2-migration/phase-4-session-file-tdd.md),
[integrity-and-versioning.md](../../../../inprogress/2026-09-25-ttd-v2-migration/integrity-and-versioning.md). The text below is kept as written.

## 1. Why this document exists

A `.ttd` file started as "the timeline, serialized". It has since grown a
second payload (the write journal) behind a header flag, and a third is now
measured and waiting (the reverse-search coverage index). Adding each one as a
bespoke trailing section works until it doesn't: there is no way to append a
stream to an existing file, no way to skip a stream a reader does not
understand, and no way to read a time window without walking everything before
it.

That is the same problem media containers solved, so the vocabulary is
borrowed deliberately: **streams** (independent kinds of data), **frames**
(the unit each stream is chunked into), and **muxing** (how those chunks are
laid out relative to each other in the file).

## 2. What the format was (v1, history)

Strictly ordered sections, written in one pass:

```
+---------------------------------------------------+
| header            fixed layout, ends with 8 B     |
|                   peripheral mask (flag bit 9)    |
+---------------------------------------------------+
| page store        page_store_count slot records   |
|                   each: encoding, refcount,       |
|                   prev_slot, crc32c, payload_size,|
|                   zstd payload                    |
+---------------------------------------------------+
| checkpoints       checkpoint_count records, each  |
|                   carrying cpu + chipset state,   |
|                   peripheral blobs and            |
|                   model_ram_pages x 4 slot refs   |
+---------------------------------------------------+
| write journal     present iff header.flags bit 1  |
|                   u64 count, magic, block dir,    |
|                   zstd columnar block payloads    |
+---------------------------------------------------+
```

Properties worth naming, because the proposal below is judged against them:

* **One stream is optional and it is signalled by a flag bit.** That is already
  a container mechanism in miniature — the precedent this design extends.
* **One stream is already chunked.** The write journal is stored as a directory
  of zstd-compressed 2048-record blocks, each carrying its globalT range. That
  is the chunk shape section 4 generalises, arrived at independently because the
  journal was 89% of the file uncompressed.
* **Sections are segregated, not interleaved.** All page-store data precedes all
  checkpoints. Reading one frame of history means seeking into a section whose
  size is not known until the previous section has been walked.
* **The file is written whole.** There is no append path, so a long recording
  cannot be flushed incrementally, and adding a stream to an existing file means
  rewriting it.
* **Every replay input is persisted now** (2026-09-29, O-1 of
  ttd-offline-analysis). The input journal and the external-event markers are
  flag-gated sections (header bits 6 and 7), written after the bookmarks. The
  writer always writes them, empty or not, so a file without them is known to
  predate them: it loads, and the session reports its input history
  incomplete. Before that they lived in memory only, and a loaded session
  replayed inside frames without the recorded input. The coverage index is
  persisted the same way, as a flag-gated section — the pattern this design
  generalises.
* **There is no session identity.** `emulator_id` is a symbolic instance name and
  `captured_at_unix_ms` is a timestamp; neither identifies *this recording*, so
  nothing outside the file can be bound to it.

## 3. What each stream actually looks like

Before deciding how to mux, it is worth being precise about how differently
these streams are shaped. They do not share a natural frame size.

| Stream | Unit | Rate | Compressed size | Notes |
|---|---|---|---|---|
| Checkpoints | 1 per emulated frame | 50/s | ~1.5 KB/frame | I-frame every 50, deltas between |
| Page store | 4 KB sub-page slot | on write | content-addressed | Referenced *by* checkpoints, not time-ordered |
| Write journal | 2048-record block | ~114 000/s | 0.8–3.5 B/record | zstd columnar blocks; ring holds 47 s in memory |
| Input journal | input event (key, mouse, GS host stimulus) | sparse | 21 B/event, uncompressed | Flag bit 6, after the bookmarks; always written |
| External events | marker | sparse | 14 B + reason (≤ 63 B) | Flag bit 7, after the input journal; always written |
| Port journals (IN, OUT) | access: time, PC, port, value; 32768-record zstd block of five columns | up to ~140 000/s; 74 000/s loading from tape | ~2.7 KB/s loading from tape | Flag bit 8, after the external events; every IN and OUT of the session, so replay needs no media and "when did the program ..." needs no replay ([ttd-port-read-journal.md](ttd-port-read-journal.md)) |
| Coverage / executed | 64-frame block | 50/s | ~17 B/frame | Flag bit 2, written after the journal |
| Coverage / written | 64-frame block | 50/s | ~5 B/frame | Same section |
| Coverage / read | 64-frame block | 50/s | ~23 B/frame | Same section |

The page store is the awkward one: it is **not a time-ordered stream at all**.
It is a content-addressed heap that checkpoints point into, and the same slot
may be referenced by checkpoints thousands of frames apart. Any interleaving
scheme has to treat it as a stream whose chunks are emitted when new content
first appears, with references resolved by slot index rather than by position.

## 4. Proposed container

### 4.1 Chunked layout

```
+---------------------------------------------------+
| header            magic, schema version, session  |
|                   uuid, model, geometry           |
+---------------------------------------------------+
| chunk             stream_id, flags, first_frame,  |
| chunk             frame_count, raw_size,          |
| chunk             comp_size, payload              |
|   ...             appended in production order    |
+---------------------------------------------------+
| cue table         per stream: (frame -> offset)   |
|                   every Nth chunk                 |
+---------------------------------------------------+
| footer            cue table offset + magic        |
+---------------------------------------------------+
```

Chunk header, 32 bytes:

| Field | Type | Meaning |
|---|---|---|
| `stream_id` | u16 | Which stream this chunk belongs to |
| `flags` | u16 | bit 0 = zstd-compressed payload; bit 1 = keyframe/self-contained |
| `first_frame` | u64 | Emulated frame of the first item |
| `frame_count` | u32 | Items covered (0 for non-time-ordered streams) |
| `raw_size` | u32 | Decompressed payload size |
| `comp_size` | u32 | Payload size as stored |
| `crc32c` | u32 | Of the stored payload |

Stream ids:

| Id | Stream | Chunking |
|---|---|---|
| 0 | Page store slots | Batch of slots, emitted as content appears |
| 1 | Checkpoints | Run of frames, cut at each I-frame |
| 2 | Write journal | Fixed record count per chunk |
| 3 | Input journal | Run of frames |
| 4 | External events | Run of frames |
| 5 | Coverage — executed | 64 frames (`kFramesPerBlock`) |
| 6 | Coverage — written | 64 frames |
| 7 | Coverage — read | 64 frames |

Everything above 7 is reserved. **A reader skips any `stream_id` it does not
know** using `comp_size`, which is the property the current format lacks and the
main reason to make this change.

### 4.2 Muxing

Chunks are appended in the order they are produced, so the file is time-ordered
by construction and a live recording can be flushed incrementally instead of
buffered whole. Because streams have wildly different rates, "time-ordered"
means only that a chunk's `first_frame` is non-decreasing *within* a stream, not
across streams — the write journal will emit many chunks between two coverage
chunks.

The coverage index already produces exactly this shape. Its in-memory form is a
sequence of `{baseFrame, frameCount, compressed payload}` blocks, which maps
onto a chunk one-for-one with no reformatting.

### 4.3 Cue table

Written at close: for each stream, a sorted list of `(first_frame, file_offset)`
entries, one per K chunks. A reader seeking to frame N binary-searches the cue
list for each stream it cares about and starts reading from there, instead of
walking the file.

If the process dies mid-recording the cue table is absent — the footer magic
will not be found. A reader detects that and rebuilds cues by scanning chunk
headers, which costs one pass but recovers the recording. **A crashed session
stays readable**, which the current whole-file writer cannot promise.

## 5. In-file section versus sidecar file

This is the open decision for the coverage index, and it is worth stating the
trade honestly rather than asserting a winner.

Measured inputs:

* Index volume: **3.0–4.6 MB per hour** of recording (ROM startup / real
  program), against a session whose page data runs to hundreds of megabytes. By
  volume the index is noise.
* Rebuild cost if not persisted: **~1.05 ms per frame**, i.e. **~32 s for a
  ten-minute session, ~3 minutes for an hour**, before the first reverse search
  can be accelerated.

**In-file section.** Coherence is structural: the index cannot be stale or
mismatched because it travels inside the artifact it describes. It needs no
session identity, no checksums, no staleness policy. The cost is that adding an
index to an existing recording means rewriting the file.

**Sidecar file.** Its single real advantage is exactly that case: indexing a
recording that already exists, without rewriting a possibly very large file.
The price is that coherence becomes a protocol the code has to enforce:

* a **session UUID** stamped in both — which does not exist today and would have
  to be added to the header regardless;
* the **frame range** the index claims to cover;
* a **content hash** over the timeline, so an edited or truncated session
  invalidates its index;
* the **index schema version**;
* and a hard rule that on any mismatch the sidecar is ignored silently and the
  search falls back to unindexed. An index that is trusted when stale produces
  confidently wrong answers, which is worse than having no index.

**Recommendation:** the in-file chunk, because the index is small, the container
already needs chunking for other reasons, and the sidecar's coherence protocol
is real work that buys one workflow. If that workflow matters, the honest move
is to add the session UUID to the header in the same change as the chunk format
— so a sidecar remains possible later without a second format break.

Either way the header should gain a session UUID: it is cheap, and nothing
outside the file can currently refer to a specific recording.

## 6. Migration

The format has not shipped, so the current layout does not have to be preserved
indefinitely — sessions recorded against it are already expected to be
re-recorded after format changes (see `ttd_dump_format.h`). The chunked layout
is nonetheless a breaking change and should carry a schema version bump when it
lands, along with:

* the `.ksy` rewritten in terms of chunks, so third-party parsers follow;
* `ttd-analyzer` updated to walk chunks and to report unknown stream ids rather
  than failing on them;
* a `ttd reindex` command, which is what makes the in-file choice tolerable for
  existing recordings.

## 7. Measurements behind this document

All numbers here come from benchmarks in the tree, not from estimates:

* `BM_TTD_CoverageIndex_Footprint` — bytes per frame, zstd ratio, projected
  MB/hour, per workload.
* `BM_TTD_CoverageIndex_RebuildPerFrame` — replay cost per frame, the upper
  bound on rebuilding an index that was not persisted.
* `BM_TTD_FindLastAccess_Coverage` — what the index buys on the query side
  (197 ms → 2.6 ms over 200 frames of history).
* `BM_Frame_TTD_Gaming` / `BM_Frame_TTD_Development` — the per-frame collection
  cost, including block compression.

Detailed results and their interpretation:
`docs/inprogress/2026-08-20-ttd-reverse-search-index/`.
