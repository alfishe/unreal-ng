# Disk file systems

The files on any disk, read the same way for every file system and from every surface: the volumes of a disk, a
directory's entries, a file's bytes. Floppies and hard disks, SD and CF cards. Reading, writing, deleting and renaming
files on every file system the probe finds.

It all lives in **unreal-disk** (`core/src/3rdparty/unreal-disk`), the standard C++ library of disks: the emulator,
the unreal-asm tools and `zxdisk` link it, none of them needs the emulator's core for disk files.

| Layer | Where (`unrealdisk/`) | What |
|---|---|---|
| File systems | `trdosfs.h`, `plus3fs.h`, `isdosfs.h`, `fatvolumereader.h`, `fatfs.h` | TR-DOS and +3DOS, iS-DOS / TASiS over a 256-byte block reader and writer, FAT12 / 16 / 32 on a block device (read by `FatVolumeReader`, changed by `FatFs`) |
| Block devices | `blockdevice.h`, `subrangedevice.h`, `floppyblockdevice.h` | a disk as 512-byte sectors (core: `IBlockDevice`), a partition window, a floppy of uniform 512-byte sectors (FAT on a Sprinter DSS or MS-DOS floppy; writable when asked) |
| Code pages | `unicode.h` | CP866 / CP1251 names to UTF-8 (FAT short names, iS-DOS) |
| One interface | `diskfs.h` | `DiskFs`: probes the volumes, then `List` / `Stat` / `Read` / `Write` / `Delete` / `Rename` by volume and path |
| Command line | `tools/zxdisk/zxdisk.cpp` | `zxdisk volumes / ls / cat / put / rm / mv` on any image file |

In the emulator, `core/src/emulator/io/storage/fs/diskfs.h` adds `OpenDiskFs(Medium&)` (a slot's medium, block media
under the time-travel read tap), and the old core headers (`iblockdevice.h`, `fatvolumereader.h`, `subrangedevice.h`,
`common/unicodehelper.h`) name the library's classes.

| User | What it reads through DiskFs |
|---|---|
| Media verbs (`core/src/emulator/media/mediacontrol.h`) | `volumes`, `files`, `read`, `write`, `delete`, `rename` for CLI, REST, WebSocket, MCP, Python and Lua |
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

| Surface | Volumes | Directory | File | Write | Delete, rename |
|---|---|---|---|---|---|
| CLI | `media volumes <slot>` | `media files <slot> [dir] [--volume id] [--recursive]` | `media read <slot> <file> [--volume id] [--out path]` (a hex dump of up to 4 KiB without `--out`) | `media write <slot> <file> --in host-file \| --data base64 [--start n] [--header=false] [--volume id]` | `media delete <slot> <file> [--volume id]`, `media rename <slot> <file> <to> [--volume id]` |
| REST | `GET /media/{slot}/volumes` | `GET /media/{slot}/files?path=&volume=&recursive=` | `GET /media/{slot}/read?path=&volume=&out=&raw=` (`raw=true`: `application/octet-stream`); `POST /media/{slot}/{verb}` too | `POST /media/{slot}/write` (`path`, `data` \| `in`, `start`, `header`, `volume`), or the bytes as the body (`application/octet-stream`, `?path=`) | `POST /media/{slot}/delete` (`path`, `volume`), `POST /media/{slot}/rename` (`path`, `to`, `volume`) |
| WebSocket | the REST routes as a `call` | | | | |
| MCP | `media` `volumes` | `media` `files` (`volume`, `recursive`) | `media` `read` (`volume`, `out`; the text carries base64 up to about 16 KiB) | `media` `write` (`path`, `data` or `in`, `start`, `header`, `volume`) | `media` `delete` (`path`, `volume`), `media` `rename` (`path`, `to`, `volume`) |
| Python | `emu.media_volumes(slot)` | `emu.media_files(slot, path, volume=, recursive=)` | `emu.media_read(slot, path, volume=, out=)`; `emu.media_read_bytes(slot, path, volume)` gives `bytes` | `emu.media_write(slot, path, data, start=, header=, volume=)` (`data` bytes, or `in_="host file"`) | `emu.media_delete(slot, path, volume=)`, `emu.media_rename(slot, path, to, volume=)` |
| Lua | `media_volumes(slot)` | `media_files(slot, dir, {volume=, recursive=})` | `media_read(slot, file, {volume=, out=})`; `media_read_bytes(slot, file, volume)` gives a string or nil and the reason | `media_write(slot, file, bytes, {start=, header=, volume=})` (`bytes` nil: `{["in"] = "host file"}`) | `media_delete(slot, file, {volume=})`, `media_rename(slot, file, to, {volume=})` |
| C++ (analyzers) | `OpenDiskFs(medium)->Volumes()`, or `DiskFs(image)` / `DiskFs(device)` | `List(volume, path, recursive, out, error)` | `Read(volume, path, data, error)` | `Write(volume, path, data, options, error)` | `Delete(volume, path, error)`, `Rename(volume, path, to, error)` |

`read` answers base64 `data` up to 16 MiB; a bigger file needs `out`. An empty slot, a tape or a CD answers
`not-supported`; a disk with no file system found or a missing file `unreadable-source`.

The media verbs read a block medium under the time-travel read tap: nothing is journalled, the guest sees nothing. A
C++ caller reads at a coherent moment (the emulator paused, parked, or on its own thread).

`zxdisk` gives the same on image files: `zxdisk volumes <image>`, `zxdisk ls <image> [dir] [-r]`, `zxdisk cat <image>
<file> [-o out]`, `zxdisk put <image> <host-file> [name] [--start n]`, `zxdisk rm <image> <file>`, `zxdisk mv <image>
<file> <new-name>` (`rm` and `mv` on floppy images).

## Writing

`Write(volume, path, data, options)` puts a file on the volume, an older file of that name replaced:

- **TR-DOS**: `NAME.T` (a type byte that is no letter as `\xNN`); `start` is the start field, `lengthField` false
  leaves the length 0 (as XAS saves).
- **+3DOS**: `NAME.TYP` (1-8 characters, a type of up to 3), a +3DOS header unless `header` is false; `start` is the
  CODE address.
- **iS-DOS / TASiS**: a file into a catalogue (`SHELL/new.txt`), laid out as the kernel lays it out: the first free
  slot of the catalogue (a segmented catalogue grows by 32 bytes, a continuous one is full at its size), the blocks
  first fit from block 0, continuous when one run of at most 255 blocks holds it, else segmented (a table block and up
  to 85 runs). `start` is the load address; FSUM (the checksum at +26), the date and the time are set, NEFIL counts it.
  Names: letters, digits and ``#$&+-=_\`!@%'(){|}~``, Cyrillic as CP866; at most 126 files in a catalogue.
- **FAT12 / 16 / 32**: any path of an existing directory, a long name with its 8.3 alias; the clusters checked
  against the free space first, the date and time from the host's wall clock.

`Delete(volume, path)` removes a file; an empty iS-DOS or FAT directory goes too. `Rename(volume, path, to)` keeps a
bare name in the file's directory; on iS-DOS and FAT a path from the root (`/top.txt`, `/GAMES/x.com`) moves the file.
An iS-DOS catalogue is renamed in place only. On iS-DOS `device.sys`, `boot.sys`, the kernel file named by the
descriptor and files with the delete-protect bit (status bit 7) are refused.

Volumes say `writable`: a floppy volume always (the emulator decides about the disk), a hard disk or card volume when
its device takes writes. The others answer "the <system> volume '<id>' is read only".

The media verbs `write`, `delete` and `rename` do it on the disk in a slot: refused when the slot is write-protected,
the disk read only (`access readonly`) or the volume not writable (`not-supported`), a missing file
(`unreadable-source`), and for a bad name, a taken `to`, a missing `data` / `in` / `to` or a `start` above 65535
(`bad-request`). They run at a coherent moment (paused, stopped or between two frames) as a tool's edit
(`OpKind::ToolEdit`): a TTD session keeps it. A floppy's undo log keeps every track as it was; a hard disk or card is
read under the read tap and written through it, so its undo log keeps every sector the edit changed. A seek back
sets the disk to it. The reply's `access` says where the change went: `session` is dirty like after a guest write
(`media save`, `eject --save` keep it), `writethrough` is in the disk's file (a floppy at the next frame).

## Limits

- No verb sets a volume label, attributes or a file's time; no directory is made (FAT `mkdir`, an iS-DOS catalogue).
- iS-DOS: a continuous catalogue that is full stays full; files are written with no R / W / hidden bits.
- Not read: ATM CP/M partitions, MB-02, MDOS, other file systems of a hard disk.
- An iS-DOS device on a hard disk is found at a cylinder boundary only.
