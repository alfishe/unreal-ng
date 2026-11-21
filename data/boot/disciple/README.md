# MGT DISCiPLE system disk

`gdos-3d-system.mgt`: the GDOS 3d system disk for the MGT DISCiPLE (`48K-DISCIPLE`, `128K-DISCIPLE`), written by the
emulator from the GDOS 3d system tape (`docs/inprogress/2026-11-15-mgt-plus-d/`, P7.2). The DISCiPLE's ROM loads the
first CODE file named `SYS*` from drive 1 on `RUN`; a work disk without one needs this disk first (the autostart does
it).

| File | sha256 | md5 |
|------|--------|-----|
| `gdos-3d-system.mgt` (819200 bytes) | `6e1218acd71a512172a8a76a681bd00a4fd04af3a03c4824176275962080f3a5` | `e7fd5e9ae99dbcdd1e55e82006c2c064` |
| source tape: `GDOS 3d.tzx` (archive.org `mgt-disciple`, not in the repository) | `83cd7c86e4a3692c50186a4db6d82f865eee4fac7499b231ef9f98b6d973ee1d` | |

## Contents

| Directory slot | Type | Name | Sectors | Start (T/S) | Header |
|----------------|------|------|---------|-------------|--------|
| 1 | 4 (CODE) | `SYSTEM 3d` | 14 | 4/1 | length `#1A00`, start `#0000` |
| 2 | 1 (BASIC) | `CONFIG` | 32 | 5/5 | |
| 3 | 4 (CODE) | `CONFIG1_C` | 4 | 8/7 | length 1750, start 40300 |
| 4 | 4 (CODE) | `CONFIG2_C` | 14 | 9/1 | length 6656, start 42240 |
| 5 | 4 (CODE) | `CONFIG3_C` | 1 | 10/5 | length 60, start 49000 |

## Recipe

`DiscipleSystemDisk_Test.ConfigWritesTheSystemDisk` (`core/tests/emulator/slots/cards/plusd/disciplesystemdisk_test.cpp`):

1. Boot `48K-DISCIPLE`, insert a blank 800K `.mgt` (all zero) into `fdd.disciple1`, load the tape with fast tape on,
   `LOAD ""`.
2. Answer every CONFIG question with its default (ENTER): 2 drives, 80 tracks, double sided, step 12 ms, flashing
   border, the printer port with the Epson FX80 codes. ENTER at `ENSURE DISC IN DRIVE 1`, `Y` at
   `Are you SURE ? (y/n)` (FORMAT), `N` at `DO YOU WISH TO REPEAT?`. CONFIG formats the disk, saves `SYSTEM 3d` and
   itself.
3. Export `fdd.disciple1`.

```bash
cd cmake-build-agent-release/bin
UNREAL_DISCIPLE_SYSTEM_TAPE=<GDOS 3d.tzx> UNREAL_DISCIPLE_SYSTEM_OUT=<out.mgt> \
    ./core-tests --gtest_filter='DiscipleSystemDisk_Test.ConfigWritesTheSystemDisk'
```
