# MGT +D system disk

`gdos-2a-system.mgt`: the G+DOS system disk for the MGT +D (`48K-PLUSD`, `128K-PLUSD`), written by the emulator from
the Plus D System Tape v2a (owner decision D-2, `docs/inprogress/2026-11-15-mgt-plus-d/`). G+DOS loads `+SYS 2a` from
drive 1 on `RUN`; a work disk without a `+SYS` file needs this disk first.

| File | sha256 | md5 | crc32 |
|------|--------|-----|-------|
| `gdos-2a-system.mgt` (819200 bytes) | `2d67374a082e3661cd62daa71414be6e5ce7853bcf2ac891894ad3b0e4f4c834` | `3c28ee8e4d21df5d1ef34b964b078494` | `ddd659e2` |
| source tape: Plus D System Tape v2a (`.tzx`, not in the repository) | `6b8b89ca88ef3370ad64c29ea59dcd33da09f8e4e67d70f8e68f36651e644bd7` | | |

## Contents

| Directory slot | Type | Name | Sectors | Start (T/S) | Header |
|----------------|------|------|---------|-------------|--------|
| 1 | 4 (CODE) | `+SYS 2a` | 14 | 4/1 | length `#1A00`, start `#2000` |
| 2 | 1 (BASIC) | `CONFIG` | 32 | 5/5 | |
| 3 | 4 (CODE) | `CONFIG1_C` | 4 | 8/7 | length 1750, start 40300 |
| 4 | 4 (CODE) | `CONFIG2_C` | 14 | 9/1 | length 6656, start 42240 |
| 5 | 4 (CODE) | `CONFIG3_C` | 1 | 10/5 | length 60, start 49000 |

## Recipe

`PlusDSystemDisk_Test.ConfigWritesTheSystemDisk` (`core/tests/emulator/slots/cards/plusd/plusdsystemdisk_test.cpp`):

1. Boot `48K-PLUSD`, insert a blank 800K `.mgt` (all zero) into `fdd.plusd1`, load the tape with fast tape on, `LOAD ""`.
2. Answer every CONFIG question with its default (ENTER): 2 drives, 80 tracks, double sided, step 8 ms, flashing
   border, printer options. `Y` at `Are you SURE ? (y/n)` (FORMAT; ENTER there skips it), `N` at
   `DO YOU WISH TO REPEAT?` (its default starts CONFIG over). CONFIG formats the disk, saves `+SYS 2a` and itself.
3. Export `fdd.plusd1`.

```bash
cd cmake-build-agent-release/bin
UNREAL_PLUSD_SYSTEM_TAPE=<Plus D System Tape v2a .tzx> UNREAL_PLUSD_SYSTEM_OUT=<out.mgt> \
    ./core-tests --gtest_filter='PlusDSystemDisk_Test.ConfigWritesTheSystemDisk'
```

The run is deterministic: two runs give the same file (the sha256 above).

## Comparison with S-DISK

S-DISK (catalog pool `87a3c8ae4a7ff742…`, the +D system disk found in the wild) holds only `+SYS 2a`. Its directory
entry (all 256 bytes) and the 14 sectors of `+SYS 2a` are identical to this disk's: the body sha256 is
`ba1582991bed6c7c…` on both. No configuration byte differs: S-DISK was written with the default answers too. The 53
sectors that differ are CONFIG's own four files and their directory entries.
