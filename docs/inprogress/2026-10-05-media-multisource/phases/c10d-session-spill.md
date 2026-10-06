# C10d — session spill: guest writes bounded in memory

**Status:** done, 2026-10-06 (as built: §6, measured: §7). Follow-up of [c10-sparse-memory.md](c10-sparse-memory.md) §8 (findings: a session
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

`ContentId` no longer re-reads the data: an XOR of a hash per changed sector is kept up to date on every write (the
same changes give the same id in either tier; see §6).

## 4. Other parts

- **`SparseMemoryDisk`**: the chunk map becomes a flat table of chunk pointers (§8.2 of C10: O(1) lookup;
  2 GiB = 32768 pointers, 256 KiB).
- **`media create`** lifts its 2 GiB cap to 128 GiB (an SDXC card; FAT32 / exFAT guest limits are the guest's
  business): a blank medium is a session over an empty sparse disk, now bounded.
- **Reports**: `info` / `layers` of a medium with a session show `sessionBytes` (hot), `spilledBytes`, `spillFile`
  (path, or `deleted` on POSIX) and `spillFailed`.

## 5. Tests

Owner request 2026-10-06: "cover all of it with tests, memory use in every mode included".

| Test | Checks |
|---|---|
| `SessionSpill_Test.ReadsAcrossTiers` | a 1 MiB limit, 8 MiB written scattered: every sector reads back in order through every reader; the in-memory tier stays under the limit after each write; the spill file is gone with its session |
| `SessionSpill_Test.RewriteMovesBack` | a spilled sector written again reads the new data; written back to the base's data it is no change in either tier |
| `SessionSpill_Test.IteratesInOrder` | random writes, rewrites and reverts into a limited and an unlimited session over the same disk: the same changes, `NextChanged`, `ChangedIn` and content id (the id follows the content, not the tier) |
| `SessionSpill_Test.DeltaAcrossTiers` | a delta saved from a spilled session and loaded into a limited one: the same changes and id |
| `SessionSpill_Test.DamagedDeltaLeavesTheSession` | a truncated delta is refused before anything is applied |
| `SessionSpill_Test.DiscardDropsTheSpillFile` | discard removes the file and the session spills again afterwards |
| `SessionSpill_Test.SpillFailureKeepsData` | a spill folder that cannot exist: writes keep working, `SpillFailed`, the data stays in memory |
| `ChangeView_Test.MapAndWindow` | the map and window views: order, `ChangedIn`, reads, a visitor that stops |
| `SessionMemory_Test.InMemoryTierBoundedForEveryPattern` | six write patterns (sequential, one sector per chunk, random, a hot spot, two chunks in turn, write and revert): the in-memory tier never over the limit, the index a fraction of the chunks, every sector reads back |
| `SessionMemory_Test.UnlimitedHoldsEverySectorInMemory`, `LowerLimitSpillsAtOnce`, `NewSessionsTakeTheDefaultLimit` | limit 0, a lowered limit, the default of 128 MiB |
| `SparseMemory_Test.HoldsWrittenChunksAndItsTable` | a 128 GiB blank card: the chunk table only; whole and scattered chunks; zero writes free them |
| `MediaMemory_Test.ResidentGrowthPerMode` | the process's resident memory, measured per mode against an unlimited control: a limited session, a delta saved and loaded, exports to raw and dynamic VHD, a raw image and a dynamic VHD read and written through, a blank 8 GiB medium, a 4 GiB composite |
| `MediaConfig_Test.SessionSettingsAreNotSlots` | `[MEDIA] SessionMemoryLimit` / `SpillFolder` parsed as settings, bad values reported |
| `MediaControl_Test.BlankMediumSizeAndSessionWrites` | `create` up to 128 GiB; `info` reports `sessionWrites` |
| `Tiers/{ChangeAttributor,ComposeDelta,ComposeCommit,ComposeWriteBack}_Test.*/{Memory,Spilled}` | every attribution, delta, commit and write-back case run twice: with the change layer in memory, and with a two-sector limit so it is in the spill file (`SessionSpillGuard`); the key cases assert that a spill happened |
| benchmark | `SessionSpillWrite/128` and `/0` (512 MiB through a 128 MiB limit and without one); `FullCardRandRead/2`, `FullCardRewrite/2/1` before and after |

## 6. As built

- `io/storage/changeview.h`: `IChangeView` (`ChangedSectors`, `NextChanged`, `ReadChanged`, `ChangedIn`,
  `ForEachChange`), `MapChangeView`, `WindowChangeView` (a partition's window, used by `media changes` on
  partitioned composites instead of a copied map).
- `SessionWriteMap` is an `IChangeView`; `Changes()` is gone. Users moved: in-place save (`BlockFormats::Save`),
  `SessionDelta`, `CommitComposite`, `ListMediumChanges`, `ChangeAttributor`, `ForEachChangedOwner`.
- The write order is kept only while a limit is set; `SetMemoryLimit` from 0 to a limit rebuilds it in sector
  order. With limit 0 a session costs what it did before C10d.
- `ContentId` is the base's id mixed with an XOR of a hash per changed sector, kept up to date on every write: the
  same changes give the same id whichever tier holds them, and nothing is re-read to compute it.
- `SessionDelta::Save` / `Load` stream: the file is written as the changes are iterated (the run count patched in
  afterwards) and read in two passes over the chunks (checked, then applied), one 1 MiB chunk in memory at a time.
  Before, both built the whole file in memory. The format is unchanged (v1).
- A spill folder that cannot be used: `SpillFailed`, the sectors stay in memory, a retry every 4096 writes.
- Spill files are unlinked right after opening on POSIX; on Windows they are removed on close, and leftovers of a
  crash (`unreal-ng-session-*.spill`) are removed by the first spill of the next run.
- `SparseMemoryDisk`: a flat chunk table (`TableBytes`). `media create` takes up to 128 GiB.
- `[MEDIA] SessionMemoryLimit` (MiB) / `SpillFolder` are read by `MediaConfig::SettingsFromIni` and applied
  process-wide when a config is loaded; `info` reports `sessionWrites`.
- Known limit: a commit (C8a) still lists the sectors it writes in a `std::set` (about 40 bytes per sector) and
  journals them; a commit of many GB of guest writes holds that list in memory.

## 7. Measured (Release, Linux container, Xeon 2.1 GHz, `sparsemedia_benchmark.cpp`)

| Case | Before C10d | After |
|---|---|---|
| `SessionSpillWrite`: 512 MiB through a session, no limit | 1.09 s, peak RSS 568 MiB | the same path |
| the same, a limit that is never reached (the write-order bookkeeping alone) | — | 1.32 s (+0.23 µs a write; with limit 0 none) |
| the same, limit 128 MiB (about 400 MiB spilled) | — | 1.9-2.6 s, peak RSS 136 MiB |
| `FullCardRandRead/1` (`SparseMemoryDisk`, full 256 MiB) | 263 ns | 64 ns (`MemoryDisk`: 61 ns) |
| `FullCardRewrite/1/1` | 279 ns | 95 ns (`MemoryDisk`: 88 ns) |
| `FullCardRandRead/2` (a session over a full 256 MiB card; now half of it spilled) | 952 ns | 1308 ns |
| `BlankCard` 512 MiB | — | sparse: 0.08 ms, 1 MiB held; `MemoryDisk`: 309 ms, 512 MiB |

**Is it worth it** (owner question 2026-10-06). Speed: no. A Z80 guest moves at most a few thousand sectors a
second (about ten thousand in turbo); at 1-2.5 µs a sector that is at worst a few percent of one core in turbo
while spilling, and under 0.1 % at normal speed. Memory: yes, but only past 128 MiB of writes in one session,
which an interactive user does not reach; automation (headless, turbo, long runs copying archives to a card) and
many instances at once (video wall, test farms) do. The sparse disk itself is a plain win now (as fast as a flat
buffer, nothing held for blank space). The cost is code: about 400 lines in `SessionWriteMap` and the change-view
interface, covered by running every delta / commit / write-back / attribution case on both tiers. Release choice:
keep the 128 MiB default (no measurable cost until it is reached), or set `SessionMemoryLimit = 0` in the shipped
configs for the old behavior and enable it for automation configs; one line either way.
