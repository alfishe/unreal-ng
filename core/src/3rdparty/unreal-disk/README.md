# unreal-disk

ZX Spectrum disks: the floppy track and sector model, the format drivers, the block device seam of hard disks and
cards, and the file systems (TR-DOS, +3DOS, iS-DOS / TASiS, ATM CP/M, MB-02, MDOS, FAT) behind one interface, `DiskFs`.
Standard C++20 only.
Part of [unreal-ng](https://github.com/alfishe/unreal-ng): the emulator's media manager, disk controllers and media
verbs use it, and so do the tools (`zxdisk` here, the unreal-asm tools `zxasm`, `symconv`). Design:
`docs/inprogress/2026-10-10-unreal-disk/`.

## What it does

| Part | Header | What |
|---|---|---|
| model | `unrealdisk/diskimage.h`, `fdc.h`, `flux/` | `DiskImage`: cylinders, sides, tracks with their sectors (ID fields, data, CRC), the raw MFM / FM stream, weak bits, dirty tracking |
| format drivers | `unrealdisk/loaders/loader_*.h` | TRD, SCL, FDI, UDI, TD0, DSK / EDSK, MGT (+D), raw PC floppy (720K / 1.44M), MB-02 (.mb2 / .mbd), MDOS (Didaktik .d40 / .d80), HFE, SCP, hobeta: `loadImage` / `writeImage(path)`, and `parse` / `serialize` on bytes where the format has them |
| registry | `unrealdisk/floppyformats.h` | `Probe` / `ProbeData` (marks, then sizes, then the extension), `Load` / `LoadData`, `Save` (the format the path names, with a lossless UDI fallback), `CreateBlank` |
| TR-DOS | `unrealdisk/trdoscatalog.h`, `trdosfs.h` | `TrdosCatalog` reads the catalog; `TrdosFileSystem` lists, reads, writes and deletes files on any image with TR-DOS tracks |
| +3DOS | `unrealdisk/plus3fs.h` | `Plus3FileSystem` lists, reads, writes and deletes files on a +3DOS (CP/M 2.2 layout) disk: the geometry from the disk's first sector, else the +3 standard; the +3DOS header read and written |
| iS-DOS / TASiS | `unrealdisk/isdosfs.h` | `IsdosFileSystem` lists and reads the files of an iS-DOS Chic / Classic or TASiS disk (floppy, or an HDD device through a block reader): the descriptor, the catalogue tree, continuous and segmented files, the free-block bitmap; read only |
| block devices | `unrealdisk/blockdevice.h`, `subrangedevice.h`, `floppyblockdevice.h` | `BlockDevice`: a disk as 512-byte sectors (the emulator's `IBlockDevice`); `SubRangeDevice` a window of one (a partition); `FloppyBlockDevice` a floppy of uniform 512-byte sectors as one |
| FAT | `unrealdisk/fatvolumereader.h` | `FatVolumeReader`: FAT12 / 16 / 32 on a block device, MBR or superfloppy, long names, chains and extents |
| code pages | `unrealdisk/unicode.h` | `UnicodeHelper`: UTF-8 / UTF-16 / code points, CP866 and CP1251 |
| any file system | `unrealdisk/diskfs.h` | `DiskFs`: the volumes of a floppy or a block device (TR-DOS, +3DOS, iS-DOS, ATM CP/M, MB-02, MDOS and FAT floppies; FAT partitions, ATM CP/M partitions, iS-DOS devices at cylinder boundaries, MDOS3 virtual disks), `List` / `Stat` / `Read` by volume and path, `Write`, `Delete`, `Rename` and the Set calls on TR-DOS, +3DOS, iS-DOS, MDOS and FAT ([design](../../../../docs/emulator/design/disk-file-systems.md)) |
| host settings | `unrealdisk/options.h` | `LoaderOptions` (the TR-DOS sector interleave) |

## Example

```cpp
#include "unrealdisk/floppyformats.h"
#include "unrealdisk/trdosfs.h"

std::unique_ptr<unrealdisk::DiskImage> disk;
std::string format, error;
if (unrealdisk::FloppyFormats::Load({}, "game.scl", disk, format).Ok())
{
    unrealdisk::TrdosFileSystem fs(*disk);
    fs.Write("MAIN", 'H', bytes, {}, error);                     // an older MAIN.H is deleted first, as TR-DOS does
    unrealdisk::FloppyFormats::Save({}, *disk, "game.fdi", false); // the same disk as FDI
}
```

## zxdisk

The files of any image from the command line:

```bash
zxdisk volumes ATM_HDD.HDD                       # cpm-1 ATM CP/M "D:", cpm-6 ..., isdos-70 iS-DOS Chic "HARD B:" ...
zxdisk ls ISDOS50.FDI SHELL -r                   # the tree under SHELL
zxdisk cat dss_1_62_92.img pics/sample.bmp -o sample.bmp
zxdisk put game.trd loader.bin LOADER.C --start 0x6000   # onto a TR-DOS or +3DOS floppy, saved back
```

A floppy in any format the library reads; any other file is a raw hard disk image (`--offset n` skips a header).

## Build

Inside unreal-ng it is built by `core/src/CMakeLists.txt` (the emulator's `core` links it; `unreal-disk-tests` runs
in `test.sh`). Standalone:

```bash
cmake -S . -B build -G Ninja -DUNREAL_DISK_TESTS=ON && ninja -C build && ./build/unreal-disk-tests
```

The tests read the disk images of the repository's `testdata/loaders/`.
