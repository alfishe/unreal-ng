# C10e — session journal: arenas in memory, a recoverable journal on disk

**Status:** done, 2026-10-06 (as built: §7; the benchmark sweep of §5 still to run). Follows [c10d-session-spill.md](c10d-session-spill.md). Owner decisions 2026-10-06:

- "If we keep it in a map, better not to keep that much in memory and fragment the heap that way": chunks (arenas)
  instead of a node per sector, flushed when a chunk boundary is crossed or on a timeout.
- "Why accumulate 128 MB of changes? If something goes wrong we either stall or lose it all": a few MiB in memory,
  the rest on disk; the disk copy survives a crash of the emulator.
- "A journal, on by default, but one can say explicitly: mount the disk without replaying the journal".
- "The limit can be 16 MB and 30 seconds".

Exit: a session holds at most `SessionMemoryLimit` (default **16 MiB**) of guest writes in memory, in 1 MiB arenas
that go back to the OS when freed; the rest is in a journal file next to the medium, written at most
`SessionFlushSeconds` (default **30 s**) after the guest wrote; after a crash of the emulator the next insert of the
same medium replays the journal (unless told not to); C10d's tests and the C6-C8 flows on both tiers pass
unchanged; A/B against C10d: write time, peak RSS, RSS after a discard.

## 1. What changes against C10d

| | C10d | C10e |
|---|---|---|
| In-memory tier | `std::map<lba, 512 bytes>`: a heap node per sector (~560 bytes) | 1 MiB arenas of 2048 sector slots, a per-group index (§2) |
| Memory limit | 128 MiB | 16 MiB (16 arenas) |
| When the disk is written | past the limit, down to 7/8 (16 MiB at once) | an arena at a time (1 MiB, about 1 ms into the OS cache) when the limit is crossed, and anything older than 30 s |
| The disk file | a temp spill file, unlinked on POSIX: lost on a crash | the journal next to the medium (§3), replayed by the next insert (§4) |
| Heap after a discard / save | hundreds of thousands of freed nodes stay in the heap (RSS does not drop) | arenas are freed whole (1 MiB allocations are `mmap`-backed in glibc and go back to the OS) |

Why a few MiB are enough (the owner chose 16): a sector read back from the journal costs about 1 µs from the OS page cache, the same as a
sector of an image file (c10-sparse-memory.md §7); the page cache is the memory tier, for free and under the OS's
own pressure. The in-memory arenas only absorb rewrites of the same sectors (FATs, directories) and batch the file
writes.

## 2. The in-memory tier

- **Arenas**: 1 MiB, 2048 slots of 512 bytes, in a list in the order they were opened; a pointer to the next free
  slot of the newest arena. A new changed sector takes the next slot (a new arena when the newest is full). A
  rewrite of a sector in memory writes its slot in place (no garbage); a sector written back to the base's data
  frees its slot into the arena's free list (reused before the end pointer moves).
- **Flush**: when the arenas exceed the limit, the oldest arena moves to the journal: its live sectors are written
  into their groups' journal slots (§3), the arena is freed. A sector of that arena written again later comes back
  into the newest arena.
- **Timeout**: `MediaManager::ApplyPending` (once a frame) ticks every session; a session whose oldest unflushed
  write is older than `SessionFlushSeconds` flushes every arena with unflushed sectors (the arenas stay, their
  sectors marked clean: a rewrite only dirties them again). Nothing is dirty: the tick is one comparison.
- **Index**: per group of 128 sectors (64 KiB): a mask of sectors in memory, a mask of sectors in the journal, and a
  small array of slot references for the in-memory ones (by rank in the mask: one allocation per group, not per
  sector). Groups live in a two-level table, allocated lazily per 1 GiB of the disk (the same table as
  `SparseMemoryDisk`, §5). `NextChanged` walks masks; whole empty GiB are skipped at the top level.

## 3. The journal file

`<source>.usession` next to the medium's source (an image file, a folder, a composite's descriptor). Media without
a writable place for it (no source: a blank medium; a read-only folder; the source on a CD) get one in
`[MEDIA] SpillFolder` as in C10d, unlinked on POSIX: not recoverable, and `info` says so.

```
header (4 KiB): "UNGSESSN", u32 version 1, u32 flags, u64 base content id, u64 sector count,
                the layers' identities (as the session delta, sessiondelta.h), u32 slot bytes (65536 + 512)
slot n (64.5 KiB): 512-byte slot header: "UNGSLOT!", u64 group, u64 sequence, u64 mask[2], u32 checksum (FNV of
                   the header); then the group's 128 sectors at their offsets (absent ones are holes)
```

- A group gets a slot on its first flush and keeps it; later flushes of the group write its sectors in place, then
  the slot header with the new mask and a higher sequence. A sector written back to the base's data clears its bit
  at the next flush of its group.
- **Crash consistency**: the sector data is written before the slot header that names it. A crash between the two
  leaves the old mask: the sector reads as before the flush, or (a sector already in the mask, rewritten in place)
  as the new data. Each sector is old or new; a 512-byte write is not split by the file systems in use.
- **Durability**: every flush ends with a stream flush (the data is in the OS: it survives a crash of the
  emulator); `fsync` (`_commit` on Windows) at most once per `SessionFlushSeconds`, on the timeout tick (it survives
  a crash of the OS older than that window). At risk on a crash of the emulator: the writes of the last 30 s still in
  memory (at most 16 MiB).

## 4. Insert, replay, eject

| Event | The journal |
|---|---|
| insert, journal present, same base identity | replayed: the session starts with its sectors (dirty, unsaved); the report says `session journal replayed: N sector(s), written <time>`; a composite's `.delta` is not loaded (the journal holds everything since that insert, the delta's sectors included) |
| insert with `journal: discard` | the old journal is deleted unread; a new one starts |
| insert with `journal: off` | no journal for this medium (the C10d temp spill file); an old journal is left as it is |
| insert, journal of other sources (identity differs) | not replayed; renamed to `<source>.usession.<n>.stale`, reported (never silently lost) |
| insert, damaged journal (bad header) | as other sources: renamed aside, reported; damaged slots inside a good one are skipped and counted |
| save, export of everything, discard, eject with a disposition | the journal is deleted (its writes went where they had to, or were dropped on purpose) |
| emulator exit with dirty media | the journal stays: the next insert replays it (`media list` and the GUI show the dirty medium as today) |
| a commit / write-back (C8) | the session is emptied: the journal is deleted |

`[MEDIA] SessionJournal = on | off` (default on) sets the default; the insert option `journal: replay | discard | off`
overrides it per insert. Surfaces: MediaControl, MCP, OpenAPI, CLI (`--journal`), Lua / Python pass options through.

Time travel: a TTD seek that rewinds a session (C10d: `Discard` and rewrites) goes through the same write path, so
the journal follows the session's state; nothing special.

## 5. Other parts

- `SparseMemoryDisk`: the two-level table (1 GiB leaves of 16384 chunk pointers, 128 KiB each, on the first write
  into that GiB): a blank card of any size costs about 1 KiB until written.
- **Everything is configurable** (owner, 2026-10-06: "all of it configurable; we will play with it on the
  benchmarks and maybe change it"). The defaults are a starting point, tuned by `sparsemedia_benchmark.cpp`:

  | `[MEDIA]` key | Default | Range | Meaning |
  |---|---|---|---|
  | `SessionMemoryLimit` | 16 | 0 (no limit), 1 ... MiB | guest writes held in memory per session |
  | `SessionArenaKiB` | 1024 | 64 ... 16384, a power of two | the arena (chunk) size: the unit of a flush |
  | `SessionFlushSeconds` | 30 | 0 (only at the limit), 1 ... | the longest a write stays only in memory |
  | `SessionSyncSeconds` | 30 | 0 (never `fsync`), 1 ... | how often a dirty journal is synced to the disk |
  | `SessionJournal` | on | on, off | the default of the insert option `journal` |
  | `SpillFolder` | the system temp folder | a folder | where journals of media without a place of their own go |

  Read at config load (process-wide, as in C10d), taken by sessions created afterwards; `SessionWriteMap` has a
  setter for each, so tests and benchmarks set them per session. Bad values are reported and the default kept.
  The benchmark sweeps limit x arena size x flush interval (time, peak RSS, flush count, the longest single flush)
  so the defaults can be chosen from numbers.

## 6. Tests

| Test | Checks |
|---|---|
| `SessionArena_Test.*` | slots reused after a revert; a rewrite in place; arenas freed whole after a flush and a discard; the limit holds for the six write patterns of `SessionMemory_Test` |
| `SessionJournal_Test.ReplayAfterCrash` | writes, a flush, the session destroyed without eject (a crash); a new session over the same base replays the same sectors and content id |
| `SessionJournal_Test.TimeoutFlushes` | a write, ticks before and after `SessionFlushSeconds` (an injected clock): the journal holds the sector only after |
| `SessionJournal_Test.TornSlotKeepsOldOrNew` | a slot whose header was not rewritten after its data: every sector reads old or new |
| `SessionJournal_Test.InsertOptions` | `journal: replay / discard / off`; a journal of other sources renamed aside and reported; a damaged one too |
| `SessionJournal_Test.CleanEndsDeleteIt` | save, discard, eject with a disposition, commit, write-back delete the journal |
| `MediaManager` / composite tests | a composite with a journal and a delta: the journal wins |
| `MediaMemory_Test.ResidentGrowthPerMode` | adds: RSS drops after a discard of a 64 MiB session (arenas back to the OS) |
| existing | `SessionSpill_Test`, `SessionMemory_Test`, the C6-C8 suites on both tiers (`SessionSpillGuard` limits to one arena) |
| benchmark | `SessionSpillWrite` (512 MiB) with 16 MiB / 128 MiB / no limit, against C10d's numbers: time, peak RSS, RSS after discard; `SessionJournalSweep`: limit x arena size x flush interval (time, peak RSS, flushes, the longest flush) |
| `MediaConfig_Test` | every key parsed, bad values reported and the default kept |

## 7. As built

- `SessionWriteMap` (`io/storage/sessionwritemap.{h,cpp}`): arenas are pages from the OS (`mmap` / `VirtualAlloc`),
  freed whole; the index is a two-level table of groups (a leaf of 16384 group pointers per GiB touched), each group
  with a hot mask, a journal mask and its refs by rank. A sector can be in both tiers (a clean copy in memory after
  a timeout flush, or a newer one written since); memory wins on reads.
- The journal file (`SessionJournalFile`, `FILE*` with 64-bit seeks; `fsync` / `_commit`): a 4 KiB header (magic,
  version, base content id, sector count, slot size, checksum) and slots of 512 + 64 KiB. On replay the slot count
  is rounded up: the last slot ends where its last written sector ends. Duplicate slots of a group (the higher
  sequence wins) and damaged slot headers are wiped so they cannot come back later.
- Replay reads every journaled sector once (for the content hash); the data stays in the journal, not in memory.
- **Who keeps the journal** (a deviation from §4's table, simpler and stricter): an eject or a swap always deletes
  it (a dirty medium leaves only with a disposition, so its writes are decided); only the manager going away with a
  dirty medium (the emulator exits, `~MediaManager`, parked media included) keeps it. A discard, a save, a commit and
  a write-back empty the session, which deletes the journal.
- One owner per journal: a process-wide registry of the named journals of live sessions; a second session asking
  for the same path gets a temp journal (`InUse`, reported). Case-insensitive file systems may see one file under
  two spellings as two keys (not handled).
- The test runner (`core/tests/main.cpp`) overrides the journal default to off, so no `.usession` lands next to
  test data; journal tests ask for `journal: replay`. `SessionSpillGuard` runs the C6-C8 suites with one arena of
  2 sectors (arenas below 64 KiB are for tests only; the config takes 64 KiB ... 16 MiB).
- Surfaces: insert / swap option `journal` (MediaControl, MCP, OpenAPI, CLI `--journal`); `info` →
  `sessionWrites.journalBytes / journalFile / journalRecoverable / journalFailed`; `[MEDIA] SessionMemoryLimit /
  SessionArenaKiB / SessionFlushSeconds / SessionSyncSeconds / SessionJournal / SpillFolder`.
- `SparseMemoryDisk`: the two-level table; a blank 128 GiB card holds 1 KiB of table.
- Tests: `SessionArena_Test`, `SessionJournal_Test` (replay after a crash from a snapshot of the file, timeout on an
  injected clock, a torn slot, stale and damaged journals, ownership, insert options, which ends delete it),
  `MediaMemory_Test` (RSS drops after a discard), `MediaConfig_Test.SessionJournalSettings`, the C10d suites and
  the C6-C8 suites on both tiers. Full suite green on Linux gcc 13 (5 shards, about 8400 tests).
