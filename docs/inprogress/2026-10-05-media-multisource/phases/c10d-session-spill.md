# C10d — session spill: guest writes bounded in memory

**Status:** design, 2026-10-06. Follow-up of [c10-sparse-memory.md](c10-sparse-memory.md) §8 (findings: a session
holds every sector the guest wrote in RAM, without a bound). Owner decision 2026-10-06: "no point holding more than
128 MB without flushing to disk".

Exit: a session's memory stays under `[MEDIA] SessionMemoryLimit` (default 128 MiB) plus a small index, whatever the
guest writes; reads, saves, deltas, commit, write-back and `media changes` give the same results as before; the
single-sector paths are not slower while the session is under the limit (A/B, `sparsemedia_benchmark.cpp`).

## 1. Today

`SessionWriteMap` keeps `std::map<lba, std::array<uint8_t, 512>>`: about 560 bytes of RAM per changed sector, until
the medium is saved or discarded. Its map is also its API: `Changes()` returns it, and the C6-C8 code reads it
directly:

| User | Uses |
|---|---|
| `BlockFormats::Save` (in-place save of the raw family) | iterates every change |
| `SessionDelta::Write` (C6c) | iterates every change |
| `MediaManager::CommitComposite` (C8a) | iterates; `size()` |
| `ListMediumChanges`, `ChangeAttributor::Attribute`, `IComposedLayout::ForEachChangedOwner` (C6b) | iterate; `lower_bound` (is a range changed); `size()` |
| `SessionWriteMap::ZeroRun`, `ChangedIn` | `lower_bound` |
| `SessionWriteMap::ContentId` | hashes every changed sector |

## 2. Two tiers

```
            write(lba)                       read(lba)
                |                                |
        +-------v--------+   over the limit   +--v-------------------------+
        |  hot: per      |  ---------------->  |  hot? -> memory           |
        |  sector map    |   oldest chunks     |  spilled? -> spill file   |
        |  (as today)    |   move out          |  else -> base             |
        +----------------+                     +----------------------------+
                                 |
                       +---------v----------------------------+
                       | spill file: 64 KiB slots, one per    |
                       | chunk of 128 sectors; index in RAM:  |
                       | chunk -> {slot, 128-bit mask}        |
                       +--------------------------------------+
```

- **Hot tier**: the per-sector map as today. Accounting: `kHotEntryBytes` = 512 + the map node (measured, §8.2 of
  C10: about 560 bytes a sector).
- **Spill tier**: a host file; a chunk of 128 sectors (64 KiB, the `SparseMemoryDisk` chunk) owns one 64 KiB slot,
  allocated on its first spill and reused by later spills of the same chunk. A sector is written at its offset in
  the slot; sectors never spilled are not written (a hole on file systems with sparse files, zeros elsewhere: disk
  space, never RAM). The index is `std::map<chunk, {uint64_t slot; uint64_t mask[2]}>`: about 70 bytes per 64 KiB
  touched, 0.1 % of what was spilled (20 GB written: about 22 MB of index).
- **A sector is in one tier.** A write goes to the hot tier and clears the sector's spilled bit; a spilled bit is set
  only by a spill. A write that makes the sector equal to the base again drops it from both (as today).
- **Spill**: after a write that takes the hot tier over the limit, the chunks written longest ago move out until the
  hot tier is under 7/8 of the limit (hysteresis: one spill moves about 16 MiB, a few ms of sequential writes, not
  a stall per write). Write order is a FIFO of chunk numbers with a per-chunk stamp, so a chunk written again is not
  spilled early.
- **The spill file** is created on the first spill, in `[MEDIA] SpillFolder` (default: the system temp folder), named
  `unreal-ng-session-<pid>-<n>.spill`, removed when the session ends (destructor, discard, save that empties the
  session). On POSIX it is unlinked right after opening, so a crash leaves nothing. On Windows it is opened with
  delete-on-close where available; a crash can leave a file, which the next start removes (same name prefix, a pid
  that is not running).
- **A spill write fails** (disk full, no folder): the sectors stay hot, the session is flagged (`SpillFailed`), the
  medium's `info` and `layers` report it, and the limit is not enforced until a spill succeeds. Writes never fail
  because of the spill: the guest's data is not lost.

`[MEDIA] SessionMemoryLimit=128` (MiB; 0: no limit, the old behavior). Read at session creation; the automation
`config` can change it for new sessions.

## 3. The API instead of the map

`Changes()` goes. In its place, on `SessionWriteMap`:

| Member | Does |
|---|---|
| `size_t ChangedSectors() const` | both tiers (already exists) |
| `std::optional<uint64_t> NextChanged(uint64_t lba) const` | the first changed sector at or after `lba` (hot `lower_bound` and the spill index's masks) |
| `bool ChangedIn(first, count) const` | via `NextChanged` (exists) |
| `bool ReadChanged(uint64_t lba, uint8_t* dst)` | a changed sector's data from either tier |
| `bool ForEachChange(std::function<bool(uint64_t lba, const uint8_t* data)>)` | every change in sector order; the spilled ones read a chunk at a time; false: a callback stopped it or a spill read failed |

The C6b attribution takes a `const ChangeView&` (an interface with `Count`, `NextChanged`, `Read`, `ForEach`) instead
of the map; `SessionWriteMap` is one, and a `MapChangeView` over a `std::map` keeps the existing unit tests
unchanged in substance.

`ContentId` no longer hashes the data: it hashes the base id, the session's generation and a per-session random
nonce. Equal content written twice gives two ids; nothing relies on that (ids key caches and the delta's source
check, which compares the base id, not the session's).

## 4. Other parts

- **`SparseMemoryDisk`**: the chunk map becomes a flat table of chunk pointers (§8.2 of C10: O(1) lookup;
  2 GiB = 32768 pointers, 256 KiB).
- **`media create`** lifts its 2 GiB cap to 128 GiB (an SDXC card; FAT32 / exFAT guest limits are the guest's
  business): a blank medium is a session over an empty sparse disk, now bounded.
- **Reports**: `info` / `layers` of a medium with a session show `sessionBytes` (hot), `spilledBytes`, `spillFile`
  (path, or `deleted` on POSIX) and `spillFailed`.

## 5. Tests

| Test | Checks |
|---|---|
| `SessionSpill_Test.ReadsAcrossTiers` | a 1 MiB limit, 8 MiB written in a scattered pattern: every sector reads back; hot bytes stay under the limit; the spill file exists while the session lives and is gone after |
| `SessionSpill_Test.RewriteMovesBack` | a spilled sector written again reads the new data; written back to the base's data it is no longer changed (both tiers) |
| `SessionSpill_Test.IteratesInOrder` | `ForEachChange` and `NextChanged` across tiers equal a reference `std::map` (property, random writes) |
| `SessionSpill_Test.SaveDeltaCommitAcrossTiers` | in-place save, `SessionDelta` write + restore, and a commit of a spilled session give the same bytes as an unlimited one |
| `SessionSpill_Test.SpillFailureKeepsData` | a spill folder that cannot be written: writes keep working, `SpillFailed` set |
| `SparseMemoryDisk_Test.*` | unchanged (the table is internal) |
| existing | `ChangeAttributor_Test`, `ComposeDelta_Test`, `ComposeCommit_Test`, `ComposeWriteBack_Test`, Sprinter ACC tests over the new API |
| benchmark | `FullCardRandRead/2`, `FullCardRewrite/2/1` before and after (under the limit: no loss); a new `SessionSpillWrite` (512 MiB written through a 128 MiB limit: time, peak RSS) |
