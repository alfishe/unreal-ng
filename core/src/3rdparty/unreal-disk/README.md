# unreal-disk

ZX Spectrum floppy disk images: the track and sector model, the format drivers and the TR-DOS file system. Standard
C++20 only. Part of [unreal-ng](https://github.com/alfishe/unreal-ng): the emulator's media manager and disk
controllers use it, and so do the unreal-asm tools (`zxasm`, `symconv`). Design:
`docs/inprogress/2026-10-10-unreal-disk/`.

## What it does

| Part | Header | What |
|---|---|---|
| model | `unrealdisk/diskimage.h`, `fdc.h`, `flux/` | `DiskImage`: cylinders, sides, tracks with their sectors (ID fields, data, CRC), the raw MFM / FM stream, weak bits, dirty tracking |
| format drivers | `unrealdisk/loaders/loader_*.h` | TRD, SCL, FDI, UDI, TD0, DSK / EDSK, MGT (+D), raw PC floppy (720K / 1.44M), HFE, SCP, hobeta: `loadImage` / `writeImage(path)`, and `parse` / `serialize` on bytes where the format has them |
| registry | `unrealdisk/floppyformats.h` | `Probe` / `ProbeData` (marks, then sizes, then the extension), `Load` / `LoadData`, `Save` (the format the path names, with a lossless UDI fallback), `CreateBlank` |
| TR-DOS | `unrealdisk/trdoscatalog.h`, `trdosfs.h` | `TrdosCatalog` reads the catalog; `TrdosFileSystem` lists, reads, writes and deletes files on any image with TR-DOS tracks |
| +3DOS | `unrealdisk/plus3fs.h` | `Plus3FileSystem` lists, reads, writes and deletes files on a +3DOS (CP/M 2.2 layout) disk: the geometry from the disk's first sector, else the +3 standard; the +3DOS header read and written |
| iS-DOS / TASiS | `unrealdisk/isdosfs.h` | `IsdosFileSystem` lists and reads the files of an iS-DOS Chic / Classic or TASiS disk (floppy, or an HDD device through a block reader): the descriptor, the catalogue tree, continuous and segmented files, the free-block bitmap; read only |
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

## Build

Inside unreal-ng it is built by `core/src/CMakeLists.txt` (the emulator's `core` links it; `unreal-disk-tests` runs
in `test.sh`). Standalone:

```bash
cmake -S . -B build -G Ninja -DUNREAL_DISK_TESTS=ON && ninja -C build && ./build/unreal-disk-tests
```

The tests read the disk images of the repository's `testdata/loaders/`.
