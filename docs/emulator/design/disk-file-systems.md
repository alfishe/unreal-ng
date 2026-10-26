# Disk file systems

The files on any disk the emulator holds, read the same way for every file system and from every surface: the
volumes of a medium, a directory's entries, a file's bytes. Floppies and hard disks, SD and CF cards. Read only.

| Layer | Where | What |
|---|---|---|
| iS-DOS / TASiS reader | `core/src/3rdparty/unreal-disk/include/unrealdisk/isdosfs.h` | `IsdosFileSystem` over a 256-byte block reader: a floppy (`OnDisk`), a byte dump (`OnBytes`), any block device |
| TR-DOS, +3DOS readers | `unrealdisk/trdosfs.h`, `unrealdisk/plus3fs.h` | as before (these two also write) |
| FAT reader | `core/src/emulator/io/storage/fat/fatvolumereader.h` | FAT12 / 16 / 32, MBR or superfloppy |
| Floppy as sectors | `core/src/emulator/io/storage/fs/floppyblockdevice.h` | a floppy of uniform 512-byte sectors as an `IBlockDevice`, so FAT reads a Sprinter DSS or MS-DOS floppy |
| One interface | `core/src/emulator/io/storage/fs/diskfs.h` | `DiskFs`: probes the volumes, then `List` / `Stat` / `Read` by volume and path |
| Media verbs | `core/src/emulator/media/mediacontrol.h` | `volumes`, `files`, `read` for CLI, REST, WebSocket, MCP, Python and Lua |

## Volumes

`DiskFs::Volumes()` probes the medium once. Each volume has an id to pick it by, the file system, what it reads as,
the label, its offset and size on the disk and the free bytes (-1 when not known).

| Medium | Probe (in this order) | Volume ids |
|---|---|---|
| Floppy | TR-DOS (the system sector 9: label at +0xF5, free sectors); FAT12 through `FloppyBlockDevice` (512-byte sectors, consecutive ids on track 0); +3DOS (skipped when FAT matched); iS-DOS / TASiS (the descriptor in block 0) | `trdos`, `fat`, `plus3`, `isdos` |
| Hard disk, card | FAT: a superfloppy, else MBR partitions 1-4 (each through a sub-range device); iS-DOS: the whole disk from LBA 0, else a descriptor at each cylinder boundary outside the FAT partitions | `fat`, `p1`..`p4`, `isdos`, `isdos-<cylinder>` |

A cylinder is the device's native geometry (heads x sectors), else 16 x 63 as the ATM-Turbo HDD driver lays out its
devices (`ATM_HDD.HDD` has devices at cylinders 70, 103, ...). At most 4096 cylinders are scanned. A device found
covers its blocks; the scan does not look inside it.

A volume is picked by its id, by its file system (`fat16`, `isdos`) or by its index; empty means the first. One that
is not there answers `no volume 'x' (volumes: ...)`.

## Paths and entries

Paths go from the volume's root, `/` between levels, no leading `/` needed; names match case-insensitively, the exact
case first. TR-DOS names are `NAME.T` (the type letter; the last entry of a name wins, as TR-DOS finds it). iS-DOS
and FAT short names are decoded from CP866 (FAT: the medium's code page) to UTF-8.

| Field | Meaning |
|---|---|
| `path`, `name` | from the root; the last part |
| `directory`, `size` | the file's bytes (TR-DOS: the length field; +3DOS: past its header) |
| `start` | the load address: TR-DOS start, iS-DOS load, +3DOS CODE param 1 |
| `attributes` | iS-DOS: the XiSD letters (`S` segmented, `R`, `W`, `H` hidden, `D` directory); FAT: `RHSVDA` |
| `mtime` | UTC seconds, 0 when the file system keeps none |
| `error` | a directory that did not open |
| `fields` | only this file system's: TR-DOS `sectors`, `track`, `sector`, `slot`; +3DOS `user`, `onDisk`, `header`, `basicType`, `param2`; iS-DOS `status`, `first`, `special`, `system`; FAT `cluster`, `attributes` |

`recursive` lists the whole tree below the directory, each directory right before its contents. `Notes()` carries
what the probe and the last listing found: loops cut, cross-linked or stale entries, a partition that did not read.

## iS-DOS / TASiS layout

256-byte blocks. Block 0 is the descriptor: `DSK` at +13 (Chic) or +10 (Classic); the block count at +18, the root
catalogue at +20, blocks per sector at +24 (1, 2 or 4), sectors per track at +25, the sector table at +64. The
allocation bitmap starts at block 1 (a set bit is a used block).

A catalogue is 32-byte entries; entry 0 describes the catalogue itself (parent at +12, its block count at +19, the
number of files at +21). A file is continuous (status bit 6) or segmented: a table of up to 85 runs. `device.sys` and
`boot.sys` (status and special #FF) are files, not catalogues. The reader follows the zxarc `isdos.py` rules and gives
the same files and bytes on the test images (`testdata/loaders/isdos/`).

## Surfaces

| Surface | Volumes | Directory | File |
|---|---|---|---|
| CLI | `media volumes <slot>` | `media files <slot> [dir] [--volume id] [--recursive]` | `media read <slot> <file> [--volume id] [--out path]` (a hex dump of up to 4 KiB without `--out`) |
| REST | `GET /media/{slot}/volumes` | `GET /media/{slot}/files?path=&volume=&recursive=` | `GET /media/{slot}/read?path=&volume=&out=&raw=` (`raw=true`: `application/octet-stream`); `POST /media/{slot}/{verb}` too |
| WebSocket | the REST routes as a `call` | | |
| MCP | `media` `volumes` | `media` `files` (`volume`, `recursive`) | `media` `read` (`volume`, `out`; the text carries base64 up to about 16 KiB) |
| Python | `emu.media_volumes(slot)` | `emu.media_files(slot, path, volume=, recursive=)` | `emu.media_read(slot, path, volume=, out=)`; `emu.media_read_bytes(slot, path, volume)` gives `bytes` |
| Lua | `media_volumes(slot)` | `media_files(slot, dir, {volume=, recursive=})` | `media_read(slot, file, {volume=, out=})`; `media_read_bytes(slot, file, volume)` gives a string or nil and the reason |
| C++ (analyzers) | `DiskFs::Open(medium)->Volumes()` | `List(volume, path, recursive, out, error)` | `Read(volume, path, data, error)` |

`read` answers base64 `data` up to 16 MiB; a bigger file needs `out`. An empty slot, a tape or a CD answers
`not-supported`; a disk with no file system found or a missing file `unreadable-source`.

The media verbs read a block medium under the time-travel read tap: nothing is journalled, the guest sees nothing. A
C++ caller reads at a coherent moment (the emulator paused, parked, or on its own thread).

## Limits

- Read only. Writing stays with TR-DOS and +3DOS in unreal-disk (`TrdosFileSystem`, `Plus3FileSystem`).
- Not read: ATM CP/M partitions, MB-02, MDOS, other file systems of a hard disk.
- An iS-DOS device on a hard disk is found at a cylinder boundary only.
