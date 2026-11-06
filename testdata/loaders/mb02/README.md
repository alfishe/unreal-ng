# MB-02 test images

| File | What |
|---|---|
| `SYS308.MB2` | The BS-DOS 308 system disk by Busy: a raw MB-02 image (82 x 2 x 11 x 1024 = 1 847 296 bytes) in logical sector order. Disk "System 308", directories 0 "System dir" (29 files), 1 "To ramdisk" (22), 2 "Small demo" (8) |
| `SYS308.LST` | Busy's listing of it ("MB-02 disk image analyser 1.02"): directory, index, type, name, param 1, length |
| `SYSDIR.TAP` | Directory 0 as a tape: every body to compare with the disk's |
| `MBDEMO.TAP` | Directory 2 as a tape |

Source: https://busy.speccy.cz/download/sys308.rar (BS-DOS 308 system disk, freely distributed with its readme).
Used by `core/src/3rdparty/unreal-disk/tests/mb02fs_test.cpp`. Layout: `docs/emulator/design/disk-file-systems.md`.
