# Disk file systems

The files on any disk, read the same way for every file system and from every surface: the volumes of a disk, a
directory's entries, a file's bytes. Floppies and hard disks, SD and CF cards. Reading everywhere, writing on TR-DOS
and +3DOS floppies.

It all lives in **unreal-disk** (`core/src/3rdparty/unreal-disk`), the standard C++ library of disks: the emulator,
the unreal-asm tools and `zxdisk` link it, none of them needs the emulator's core for disk files.

| Layer | Where (`unrealdisk/`) | What |
|---|---|---|
| File system readers | `trdosfs.h`, `plus3fs.h`, `isdosfs.h`, `fatvolumereader.h` | TR-DOS and +3DOS (read and write), iS-DOS / TASiS over a 256-byte block reader, FAT12 / 16 / 32 on a block device |
| Block devices | `blockdevice.h`, `subrangedevice.h`, `floppyblockdevice.h` | a disk as 512-byte sectors (core: `IBlockDevice`), a partition window, a floppy of uniform 512-byte sectors (FAT on a Sprinter DSS or MS-DOS floppy) |
| Code pages | `unicode.h` | CP866 / CP1251 names to UTF-8 (FAT short names, iS-DOS) |
| One interface | `diskfs.h` | `DiskFs`: probes the volumes, then `List` / `Stat` / `Read` / `Write` by volume and path |
| Command line | `tools/zxdisk/zxdisk.cpp` | `zxdisk volumes / ls / cat / put` on any image file |

In the emulator, `core/src/emulator/io/storage/fs/diskfs.h` adds `OpenDiskFs(Medium&)` (a slot's medium, block media
under the time-travel read tap), and the old core headers (`iblockdevice.h`, `fatvolumereader.h`, `subrangedevice.h`,
`common/unicodehelper.h`) name the library's classes.

| User | What it reads through DiskFs |
|---|---|
| Media verbs (`core/src/emulator/media/mediacontrol.h`) | `volumes`, `files`, `read`, `write` for CLI, REST, WebSocket, MCP, Python and Lua |
| `disk catalog` (CLI), `GET /disk/{drive}/catalog` | the first file system of a drive (TR-DOS keeps its catalog fields) |
| The assembler surfaces (`disk:A/NAME.T`, the project drive) and `zxasm` / `symconv` | the files of a drive or an image: the shared code of `unreal-asm/tools/common/diskfiles.h` |

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

| Surface | Volumes | Directory | File | Write |
|---|---|---|---|---|
| CLI | `media volumes <slot>` | `media files <slot> [dir] [--volume id] [--recursive]` | `media read <slot> <file> [--volume id] [--out path]` (a hex dump of up to 4 KiB without `--out`) | `media write <slot> <file> --in host-file \| --data base64 [--start n] [--header=false] [--volume id]` |
| REST | `GET /media/{slot}/volumes` | `GET /media/{slot}/files?path=&volume=&recursive=` | `GET /media/{slot}/read?path=&volume=&out=&raw=` (`raw=true`: `application/octet-stream`); `POST /media/{slot}/{verb}` too | `POST /media/{slot}/write` (`path`, `data` \| `in`, `start`, `header`, `volume`), or the bytes as the body (`application/octet-stream`, `?path=`) |
| WebSocket | the REST routes as a `call` | | | |
| MCP | `media` `volumes` | `media` `files` (`volume`, `recursive`) | `media` `read` (`volume`, `out`; the text carries base64 up to about 16 KiB) | `media` `write` (`path`, `data` or `in`, `start`, `header`, `volume`) |
| Python | `emu.media_volumes(slot)` | `emu.media_files(slot, path, volume=, recursive=)` | `emu.media_read(slot, path, volume=, out=)`; `emu.media_read_bytes(slot, path, volume)` gives `bytes` | `emu.media_write(slot, path, data, start=, header=, volume=)` (`data` bytes, or `in_="host file"`) |
| Lua | `media_volumes(slot)` | `media_files(slot, dir, {volume=, recursive=})` | `media_read(slot, file, {volume=, out=})`; `media_read_bytes(slot, file, volume)` gives a string or nil and the reason | `media_write(slot, file, bytes, {start=, header=, volume=})` (`bytes` nil: `{["in"] = "host file"}`) |
| C++ (analyzers) | `OpenDiskFs(medium)->Volumes()`, or `DiskFs(image)` / `DiskFs(device)` | `List(volume, path, recursive, out, error)` | `Read(volume, path, data, error)` | `Write(volume, path, data, options, error)` |

`read` answers base64 `data` up to 16 MiB; a bigger file needs `out`. An empty slot, a tape or a CD answers
`not-supported`; a disk with no file system found or a missing file `unreadable-source`.

The media verbs read a block medium under the time-travel read tap: nothing is journalled, the guest sees nothing. A
C++ caller reads at a coherent moment (the emulator paused, parked, or on its own thread).

`zxdisk` gives the same on image files: `zxdisk volumes <image>`, `zxdisk ls <image> [dir] [-r]`, `zxdisk cat <image>
<file> [-o out]`, `zxdisk put <image> <host-file> [name] [--start n]`.

## Writing

`Write(volume, path, data, options)` puts a file on a TR-DOS (`NAME.T`, a type byte that is no letter as `\xNN`)
or +3DOS (`NAME.TYP`, a +3DOS header unless `header` is false) floppy, an older file of that name deleted first;
`start` is the TR-DOS start field or the +3DOS CODE address, `lengthField` false leaves a TR-DOS length 0 (as XAS
saves). The floppy changes in place; saving it is the caller's. Volumes say `writable`; the others answer "read only".

The media verb `write` does it on the floppy in a slot: refused when the slot is write-protected, the disk read only
(`access readonly`) or the volume not writable (`not-supported`), and for a bad name, a missing `data` / `in` or a
`start` above 65535 (`bad-request`). It runs at a coherent moment (paused, stopped or between two frames) as a tool's
edit (`OpKind::ToolEdit`): a TTD session keeps it, the medium's undo log keeps every track as it was, so a seek back
sets the disk to it. The disk is then dirty like after a guest write: `media save`, `eject --save` keep it, a
write-through disk is written to its file at the next frame.

## Limits

- Writing on TR-DOS and +3DOS floppies only; iS-DOS, FAT and hard disks are read only.
- Not read: ATM CP/M partitions, MB-02, MDOS, other file systems of a hard disk.
- An iS-DOS device on a hard disk is found at a cylinder boundary only.
