# Disk file systems

The files on any disk, read the same way for every file system and from every surface: the volumes of a disk, a
directory's entries, a file's bytes. Floppies and hard disks, SD and CF cards. Reading, writing, deleting and renaming
files on every file system the probe finds; making, moving and deleting directories on FAT, iS-DOS and
MB-02.

It all lives in **unreal-disk** (`core/src/3rdparty/unreal-disk`), the standard C++ library of disks: the emulator,
the unreal-asm tools and `zxdisk` link it, none of them needs the emulator's core for disk files.

| Layer | Where (`unrealdisk/`) | What |
|---|---|---|
| File systems | `trdosfs.h`, `plus3fs.h`, `isdosfs.h`, `cpmfs.h`, `mb02fs.h`, `mdosfs.h`, `fatvolumereader.h`, `fatfs.h` | TR-DOS and +3DOS, iS-DOS / TASiS over a 256-byte block reader and writer, ATM CP/M over a byte reader and writer, MB-02 over a 1024-byte sector reader and writer, MDOS over a 512-byte sector reader and writer, FAT12 / 16 / 32 on a block device (read by `FatVolumeReader`, changed by `FatFs`) |
| Block devices | `blockdevice.h`, `subrangedevice.h`, `floppyblockdevice.h` | a disk as 512-byte sectors (core: `IBlockDevice`), a partition window, a floppy of uniform 512-byte sectors (FAT on a Sprinter DSS or MS-DOS floppy; writable when asked) |
| Code pages | `unicode.h` | CP866 / CP1251 names to UTF-8 (FAT short names, iS-DOS) |
| One interface | `diskfs.h` | `DiskFs`: probes the volumes, then `List` / `Stat` / `Read` / `Write` / `Delete` / `Rename` / `MakeDirectory` / `SetAttributes` / `SetTime` / `SetLabel` by volume and path |
| Command line | `tools/zxdisk/zxdisk.cpp` | `zxdisk volumes / ls / cat / put / rm [-r] / mv / mkdir [-p] / attrib / label` on any image file, floppy or hard disk |

In the emulator, `core/src/emulator/io/storage/fs/diskfs.h` adds `OpenDiskFs(Medium&)` (a slot's medium, block media
under the time-travel read tap), and the old core headers (`iblockdevice.h`, `fatvolumereader.h`, `subrangedevice.h`,
`common/unicodehelper.h`) name the library's classes.

| User | What it reads through DiskFs |
|---|---|
| Media verbs (`core/src/emulator/media/mediacontrol.h`) | `volumes`, `files`, `read`, `write`, `delete`, `rename`, `mkdir`, `attrib`, `label` for CLI, REST, WebSocket, MCP, Python and Lua |
| `disk catalog` (CLI), `GET /disk/{drive}/catalog` | the first file system of a drive (TR-DOS keeps its catalog fields) |
| The assembler surfaces (`disk:A/NAME.T`, the project drive) and `zxasm` / `symconv` | the files of a drive or an image: the shared code of `unreal-asm/tools/common/diskfiles.h` |

## Volumes

`DiskFs::Volumes()` probes the medium once. Each volume has an id to pick it by, the file system, what it reads as,
the label, its offset and size on the disk and the free bytes (-1 when not known).

| Medium | Probe (in this order) | Volume ids |
|---|---|---|
| Floppy | ATM CP/M (the directory at #2000); TR-DOS (the system sector 9: label at +0xF5, free sectors; an empty catalogue on an ATM CP/M disk is no volume); MDOS (`SDOS` in the boot sector, the FAT reserving LSN 0-13; it ends the probe); FAT12 through `FloppyBlockDevice` (512-byte sectors, consecutive ids on track 0); +3DOS (skipped when FAT matched); MB-02 (the boot sector, the FAT, the root in the DIRS sector; it ends the probe); iS-DOS / TASiS (the descriptor in block 0) | `cpm`, `trdos`, `mdos`, `fat`, `plus3`, `mb02`, `isdos` |
| Hard disk, card | FAT: a superfloppy, else MBR partitions 1-4 (each through a sub-range device); ATM CP/M: the drives of the configuration record in sector 0, then the `.CDS` descriptors kept on them; MDOS3: the slots of each primary MBR partition of type #49, else of the whole device from the first sector in its first 4096 that starts with `SDOS` (an `.md3` file, slot files copied first onto a FAT16 card); BS-DOS 309: the floppies @1..@255 of the first primary MBR partition of type #47; iS-DOS: the whole disk from LBA 0, else a descriptor at each cylinder boundary outside the FAT and CP/M partitions | `fat`, `p1`..`p4`, `cpm-<cylinder>`, `mdos-<slot>`, `mb02-<n>`, `isdos`, `isdos-<cylinder>` |

A cylinder is the device's native geometry (heads x sectors), else 16 x 63 as the ATM-Turbo HDD driver lays out its
devices (`ATM_HDD.HDD` has devices at cylinders 70, 103, ...). At most 4096 cylinders are scanned. A device found
covers its blocks; the scan does not look inside it.

A volume is picked by its id, by its file system (`fat16`, `isdos`) or by its index; empty means the first. One that
is not there answers `no volume 'x' (volumes: ...)`.

## Paths and entries

Paths go from the volume's root, `/` between levels, no leading `/` needed; names match case-insensitively, the exact
case first. TR-DOS names are `NAME.T` (the type letter; the last entry of a name wins, as TR-DOS finds it). ATM CP/M
names are `NAME.TYP`: user 0 at the root, users 1-15 in the directories `USER1`..`USER15`. MB-02 names are the
tape header's 10 characters without trailing blanks (no type: it is the `type` field); a later file of a name already
in the directory gets `~<entry>` (`tma~10`), a file without a header is `~<entry>`, a directory that clashes
`~D<number>`; `/` and `\` in a name are `\x2f`, `\x5c`. MDOS names are `name.T` like TR-DOS (the type letter, the
10 characters without trailing zeroes; blanks inside stay), a later file of a name gets `~<slot>` before the type. iS-DOS
and FAT short names are decoded from CP866 (FAT: the medium's code page) to UTF-8.

| Field | Meaning |
|---|---|
| `path`, `name` | from the root; the last part |
| `directory`, `size` | the file's bytes (TR-DOS: the length field; +3DOS: past its header) |
| `start` | the load address: TR-DOS start, iS-DOS load, +3DOS CODE param 1, MB-02 param 1 (as the BS-DOS listing), MDOS +13 (a program's autostart line) |
| `attributes` | iS-DOS: the XiSD letters (`S` segmented, `R`, `W`, `H` hidden, `D` directory); MDOS: `HSPARWED`, `.` for a clear bit; FAT: `RHSVDA` |
| `mtime` | UTC seconds, 0 when the file system keeps none |
| `error` | a directory that did not open |
| `fields` | only this file system's: TR-DOS `sectors`, `track`, `sector`, `slot`; +3DOS `user`, `onDisk`, `header`, `basicType`, `param2`; iS-DOS `status`, `first`, `special`, `system`; ATM CP/M `user`, `extents`, `block` (the first); MB-02 `type` (0 BASIC, 3 CODE), `param2`, `address` (the body's), `entry`, `sector` (the first), `flags`, `attributes`, a directory `dir` (its number); MDOS `type` (the letter's code), `param2`, `sector` (the first), `slot` (the entry), `extra` (+19); FAT `cluster`, `attributes` |

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

## ATM CP/M layout

The ATM-Turbo BIOS CP/M 2.2 (`cpmfs.h`): 32-byte directory entries (user 0-15, #E5 free; name 8, type 3 with bit 7 of
t1' t2' t3' read only / system / archive; EX, S1, S2, RC; the block numbers), 2 KB blocks counted from the
directory's first one. Past 256 blocks a block number has 16 bits (8 an entry, 16 KB an extent), else 8 bits (16 an
entry, EXM 1). The size is the last extent's `(EX + 32 * S2) * 16384 + RC * 128`: CP/M keeps records, a text ends at
#1A. Only the pointers the entry's records need count; the others may be garbage.

- **Floppy**: the TR-DOS geometry (80 x 2 x 16 x 256), cylinder 0 reserved (#E5, an empty TR-DOS catalogue or a
  configuration record), the directory at #2000 with 128 entries, 316 blocks (DSM 315).
- **Hard disk**: no partition table. Sector 0 holds the configuration record: `C9 00 00 55 AA`, then records of tag,
  length (tag and length included) and body up to #7E, tag 0 ending them. Tag 3 is ASSIGN: the drive (0 = A), the
  channel and the channel descriptor from DTYP on (DTYP 4 a hard disk, DUS the unit, DHEADF heads, DCYLN cylinders,
  DSECTT sectors, DBYTES, DALTCYL the reserved tracks, DBEGCYL the start cylinder, DBLDR the size in 128-byte records,
  DDIRENT the directory entries); tag 6 is GO, the start command. More partitions come from the text descriptors
  (`.CDS`: `DRIVER = 4`, `UNIT`, `SECTORS`, `TRACKS`, `HEADSF`, `SYS`, `DIR`, `PATBEG`) that AUTOEXEC.SUB assigns with
  `AS E: 4 D:HDDE.CDS`; the probe reads every `.CDS` of user 0 on a partition it found (the same unit, a hard disk).
  The directory is at the start cylinder past the reserved tracks; DSM is `((DBLDR - SPT * OFF) >> 4) - 1` as the
  BIOS computes it. The BIOS addresses the disk by cylinder, head and sector of the descriptor; the image maps them
  with its own geometry (16 x 63 when it has none).

A volume is taken only when every used entry reads: user 0-15, printable name bytes (not all blank), EX <= 31, S1 0,
RC <= 128 and every block it needs inside the volume, past the directory, used once; at least one file. Scorpion
MOA CP/M (`testdata/machines/scorpion/cpm/`) fails it, and so does none of about 7500 TR-DOS and SCL images; all 104
ATM CP/M floppies of the collection pass. `ATM_HDD.HDD` gives D: (cylinder 1, from the record) and E:, F:, G:
(cylinders 6, 22, 38, from `HDDE.CDS`..`HDDG.CDS`); its descriptors of other disks (15 heads) find no directory.

Writing goes as the BDOS does: the data in the lowest free blocks, the last record padded with #1A (the size reads
back in whole records), then the entries in free directory slots (EX the entry's last logical extent, RC its records,
S1 0); an older file of the name goes free in the same directory write and its blocks are not reused for the new
data. A path `USERn/NAME.TYP` writes to user n (1-15), a bare one to user 0; names are upper-cased. `rename` with a
path moves the file to another user area. The written images pass `fsck.cpm` of cpmtools (a diskdef of 256-byte
sectors, 160 tracks of 16, 2 KB blocks, 128 entries, 2 boot tracks), and its `cpmcp` reads the files back.

## MB-02 layout

BS-DOS 199..309 (`mb02fs.h`; the raw `.mb2` / `.mbd` image: [MB-02 image](../../file-formats/disk-images/mb02.md)).
1024-byte sectors 1..SPT on two sides, addressed by LSN `2 * SPT * track + SPT * side + (sector - 1)`, one sector a
cluster.

- **Boot** (LSN 0; LSN 1 its backup): `18 xx` (JR), #02 at #03 (MB-02; #01 is the older MB-01, not read), tracks at
  #04, SPT at #06, sides (2) at #08, sectors a cluster (1) at #0A, the DIRS sector at #0C, FAT sectors (1..4) at #0E,
  the FAT length (#400 each) at #10, FAT1 and FAT2 at #12 / #14, their sectors 2..4 as bytes at #1A..#1F (FAT1, FAT2
  in turn), #00 at #20 and #25, the format date at #21, the disk name at #26 (10 characters, the volume's `label`).
- **FAT**: a 16-bit word a sector. Bits 15 and 14: the next LSN in bits 0-13; bit 15 alone: the last sector, bits
  0-13 the bytes used in it; high byte #FF: reserved or bad (#FF00 the boot and its backup); bit 15 clear: free. The
  reader takes FAT1, a sector that does not read from FAT2.
- **DIRS**: 256 slots of 4 bytes (#80 valid, the name's XOR, the first LSN). Directory 0 is the root ("System dir").
  A directory is a FAT chain of 32-byte entries; entry 0 its header (the parent number at #05, the name at #06), so
  the tree goes through the parent bytes (a directory whose parent is missing or a loop is listed at the root).
- **File entry**: #00 flags (bit 7 valid, 6 special, 5 has a body, 4 has a header), #01 MS-DOS time and #03 date
  (`mtime`), #05 the 17-byte tape header (type, name, length, param 1, param 2), #16 the body address, #18 the body
  length (32 bits, the `size`), #1C the body's flag byte, #1D attributes (undocumented: the `attributes` field), #1E
  the first body LSN; the body is its FAT chain.

A volume is taken when the boot marks read, the FAT covers the disk and reserves LSN 1, and the DIRS sector holds
the root. On `SYS308.MB2` (`testdata/loaders/mb02/`) the reader gives Busy's listing `SYS308.LST` (3 directories,
59 files, 1586 free sectors) and every body of the tapes `SYSDIR.TAP` and `MBDEMO.TAP` of the same archive.

Writing goes as BS-DOS does: the boot and directory sectors to LSN 1 (the backup) first, then to their place; both
FAT copies after the data. A file is a CODE header (type 3, the name blank-padded, length, param 1 the `start`,
param 2 #8000) with flags #B0, the body address the `start`, flag byte #FF; an empty file has flags #90 and no
body. Sectors are taken from LSN 2 (the FAT word of LSN 0 is not a reservation on every disk). A file of the same
name in the directory is written over in its entry; a full directory grows by a sector. `mkdir` takes the first
free DIRS slot (1..255) and writes the new directory's sector with its header; a directory is deleted only empty,
moved or renamed with its DIRS XOR, never into itself. The images were checked by an independent parser
(free sectors, chains, both FATs), not by BS-DOS.

**BS-DOS 309 on a hard disk** (divIDE, `bsdos309-hdd_bios.a80` of the BS-DOS collection): the first primary MBR
partition of type #47 holds the floppies @1..@255 back to back, 2 MiB (4096 disk sectors) each, as many as the
partition holds. BS-DOS turns an LSN into track, side and sector by the boot sector's SPT; the BIOS puts that sector
at `((track * 2 + side) * 11 + sector - 1) * 2` of the floppy, 11 sectors a track whatever the boot sector says
(93 x 2 x 11 fill the 2 MiB; a floppy of 11 sectors a track is the `.mb2` image as it is). Each floppy that opens
is a volume `mb02-<n>`; one never formatted (FF17 formats them one by one) is left out.

## MDOS layout

Didaktik D40 / D80, MDOS 1 / 2 and the MDOS3 virtual disks of divIDE (`mdosfs.h`; the raw `.d40` / `.d80` image:
[MDOS image](../../file-formats/disk-images/mdos.md)). 512-byte sectors 1..SPT, addressed by LSN
`(cylinder * sides + side) * SPT + (sector - 1)`, one sector a cluster.

- **Boot** (LSN 0): `SDOS` at 204, sides (2 when bit 4 is set) at #B1, cylinders at #B2, SPT at #B3, the disk name
  at 192 (10 characters, the volume's `label`).
- **FAT** (LSN 1-5): a 12-bit word a sector, 341 a FAT sector, two in three bytes (even `b0 | (b1 >> 4) << 8`, odd
  `(b1 & #0F) << 8 | b2`). #000 free, #DDD reserved (LSN 0-13), #DFF bad, #C00 an empty file, #E00 + n the last
  sector with n bytes (#E00: all 512), else the next LSN.
- **Directory** (LSN 6-13): 128 entries of 32 bytes. #00 the type letter (`P` program, `B` bytes, `N` / `C` arrays,
  `S` snapshot, `Q` sequential; #00 and #E5 a free entry), #01 the name (10), #0B the length (24 bits with #15), #0D
  the start, #0F the second parameter (a program's length without its variables), #11 the first LSN, #13 the flag
  byte of a headless file (type `H`, the tape emulator's), #14 the attributes `HSPARWED` (bit 7 to bit 0). Data from LSN 14 on.
- **MDOS3**: a partition (primary MBR entry of type #49) is slots of 1693 sectors: an info sector (`SDOS`, the slot's
  32-character name at 4: the volume's `label`, `0` / `1` at 36 for write protection, #E5 past it) and a 94 x 2 x 9
  disk (MDOS3 enlarges the 80 cylinders of a D80). An `.md3` file is such a partition alone; a slot whose info sector
  has no `SDOS` is empty. With no #49 partition the firmware (`nmi.a80`) takes the whole disk from its first sector
  that starts with `SDOS`: the how-to's card is FAT16 with 512-byte clusters whose first files, copied first onto an
  empty volume, are the slots in a row (`0bootdisk` a whole `.md3`, then `1infosec.bin` + `1moved.d80` +
  `1space.bin`). The volumes are numbered from that sector; with two #49 partitions or more, `mdos-p<n>-<slot>`.

A volume is taken when the boot sector has the mark and a geometry of 1..94 cylinders and 1..18 sectors a track, and
the FAT reserves LSN 0-13. A chain that loops, leaves the disk or holds fewer bytes than the length does not read. On
`mdos3-discs1.md3` (the MDOS3 distribution, 99 slots) the 19 disks give their 860 files; two on slot 13 have short
chains in the image itself (`TOOLS_3_27.P`, `zdrojak.B`). `testdata/loaders/mdos/` keeps its boot disk as a D80 and
its first 3 slots, and a FAT16 card built per the how-to from its `my_CF_16M` files (`MdosFs.Mdos3CardOfFat16Files`).

Writing: a file goes into the first free entry (type byte #00 or #E5) and the free sectors from LSN 14 up, the
data first, then the FAT, then the entry (#E5 at 22-31, 0 at 19, `RWED` as MDOS saves a file); an older file of the
name and type is deleted first. An empty file takes no sector: #C00 as its first LSN. A program's second parameter is
its length (no variables), any other type's #8000 as a tape header's CODE. Deleting writes #E5 over the type byte
and frees the chain. The 4-bit filler at the end of each FAT sector stays. A slot is written when the device takes
writes and its info byte 36 is not `1` (MDOS3's write protection).

## Surfaces

| Surface | Volumes | Directory | File | Write | Delete, rename, mkdir | Attributes, time, label |
|---|---|---|---|---|---|---|
| CLI | `media volumes <slot>` | `media files <slot> [dir] [--volume id] [--recursive]` | `media read <slot> <file> [--volume id] [--out path]` (a hex dump of up to 4 KiB without `--out`) | `media write <slot> <file> --in host-file \| --data base64 [--start n] [--header=false] [--volume id]` | `media delete <slot> <file> [--volume id] [--recursive]`, `media rename <slot> <file> <to> [--volume id]`, `media mkdir <slot> <dir> [--volume id] [--parents]` | `media attrib <slot> <file> [+R-H \| RA] [--time "YYYY-MM-DD HH:MM"] [--volume id]`, `media label <slot> <name> [--volume id]` |
| REST | `GET /media/{slot}/volumes` | `GET /media/{slot}/files?path=&volume=&recursive=` | `GET /media/{slot}/read?path=&volume=&out=&raw=` (`raw=true`: `application/octet-stream`); `POST /media/{slot}/{verb}` too | `POST /media/{slot}/write` (`path`, `data` \| `in`, `start`, `header`, `volume`), or the bytes as the body (`application/octet-stream`, `?path=`) | `POST /media/{slot}/delete` (`path`, `volume`, `recursive`), `POST /media/{slot}/rename` (`path`, `to`, `volume`), `POST /media/{slot}/mkdir` (`path`, `volume`, `parents`) | `POST /media/{slot}/attrib` (`path`, `attributes`, `time`, `volume`), `POST /media/{slot}/label` (`label`, `volume`) |
| WebSocket | the REST routes as a `call` | | | | | |
| MCP | `media` `volumes` | `media` `files` (`volume`, `recursive`) | `media` `read` (`volume`, `out`; the text carries base64 up to about 16 KiB) | `media` `write` (`path`, `data` or `in`, `start`, `header`, `volume`) | `media` `delete` (`path`, `volume`, `recursive`), `media` `rename` (`path`, `to`, `volume`), `media` `mkdir` (`path`, `volume`, `parents`) | `media` `attrib` (`path`, `attributes`, `time`, `volume`), `media` `label` (`label`, `volume`) |
| Python | `emu.media_volumes(slot)` | `emu.media_files(slot, path, volume=, recursive=)` | `emu.media_read(slot, path, volume=, out=)`; `emu.media_read_bytes(slot, path, volume)` gives `bytes` | `emu.media_write(slot, path, data, start=, header=, volume=)` (`data` bytes, or `in_="host file"`) | `emu.media_delete(slot, path, volume=, recursive=)`, `emu.media_rename(slot, path, to, volume=)`, `emu.media_mkdir(slot, path, volume=, parents=)` | `emu.media_attrib(slot, path, attributes=, time=, volume=)`, `emu.media_label(slot, label, volume=)` |
| Lua | `media_volumes(slot)` | `media_files(slot, dir, {volume=, recursive=})` | `media_read(slot, file, {volume=, out=})`; `media_read_bytes(slot, file, volume)` gives a string or nil and the reason | `media_write(slot, file, bytes, {start=, header=, volume=})` (`bytes` nil: `{["in"] = "host file"}`) | `media_delete(slot, file, {volume=, recursive=})`, `media_rename(slot, file, to, {volume=})`, `media_mkdir(slot, dir, {volume=, parents=})` | `media_attrib(slot, file, {attributes=, time=, volume=})`, `media_label(slot, label, {volume=})` |
| C++ (analyzers) | `OpenDiskFs(medium)->Volumes()`, or `DiskFs(image)` / `DiskFs(device)` | `List(volume, path, recursive, out, error)` | `Read(volume, path, data, error)` | `Write(volume, path, data, options, error)` | `Delete(volume, path, error, recursive)`, `Rename(volume, path, to, error)`, `MakeDirectory(volume, path, parents, mtime, error)` | `SetAttributes(volume, path, letters, error)`, `SetTime(volume, path, mtime, error)`, `SetLabel(volume, label, mtime, error)` |

`read` answers base64 `data` up to 16 MiB; a bigger file needs `out`. An empty slot, a tape or a CD answers
`not-supported`; a disk with no file system found or a missing file `unreadable-source`.

The media verbs read a block medium under the time-travel read tap: nothing is journalled, the guest sees nothing. A
C++ caller reads at a coherent moment (the emulator paused, parked, or on its own thread).

`zxdisk` gives the same on image files: `zxdisk volumes <image>`, `zxdisk ls <image> [dir] [-r]`, `zxdisk cat <image>
<file> [-o out]`, `zxdisk put <image> <host-file> [name] [--start n]`, `zxdisk rm <image> <file> [-r]`, `zxdisk mv <image>
<file> <new-name>`, `zxdisk mkdir <image> <dir> [-p]`, `zxdisk attrib <image> <file> [+R-H|RA] [--time t]`, `zxdisk
label <image> <name>`. The writing verbs save a floppy back (or to `-o`, its extension picking the format); a hard
disk or card image is changed in place, or copied to `-o` first and the copy changed.

## Writing

`Write(volume, path, data, options)` puts a file on the volume, an older file of that name replaced:

- **TR-DOS**: `NAME.T` (a type byte that is no letter as `\xNN`); `start` is the start field, `lengthField` false
  leaves the length 0 (as XAS saves).
- **+3DOS**: `NAME.TYP` (1-8 characters, a type of up to 3), a +3DOS header unless `header` is false; `start` is the
  CODE address.
- **iS-DOS / TASiS**: a file into a catalogue (`SHELL/new.txt`), laid out as the kernel lays it out: the first free
  slot of the catalogue (it grows by 32 bytes, see below), the blocks
  first fit from block 0, continuous when one run of at most 255 blocks holds it, else segmented (a table block and up
  to 85 runs). `start` is the load address; FSUM (the checksum at +26), the date and the time are set, NEFIL counts it.
  Names: letters, digits and ``#$&+-=_\`!@%'(){|}~``, Cyrillic as CP866; at most 126 files in a catalogue.
- **FAT12 / 16 / 32**: any path of an existing directory, a long name with its 8.3 alias; the clusters checked
  against the free space first, the date and time from the host's wall clock.

A full catalogue grows by 32 bytes; past its last block it takes a block. A segmented one adds the block to its
segment table, as the kernel does. A continuous one (TASiS makes them; the kernel answers "catalogue full") takes the
block right after it when that block is free and stays continuous; else it becomes segmented: a new segment table
with its blocks as the first run and the new block as the second, status bit 6 cleared and +17 naming the table in
both its entry 0 and its parent's entry. That needs two free blocks. A catalogue holds at most 128 entries
(16 blocks).

`Delete(volume, path)` removes a file; an empty iS-DOS or FAT directory goes too. With `recursive` a directory goes
with everything in it: every entry is checked first (a protected iS-DOS file or a read-only FAT file anywhere refuses
the whole delete, nothing removed), then the entries go deepest first. `Rename(volume, path, to)` keeps a bare name
in the file's directory; on iS-DOS and FAT a path from the root (`/top.txt`, `/GAMES/x.com`) moves the file or the
directory with its contents. A directory never moves into itself. On iS-DOS `device.sys`, `boot.sys`, the kernel
file named by the descriptor and files with the delete-protect bit (status bit 7) are refused.

`MakeDirectory(volume, path, parents)` makes a directory; `parents` makes the missing ones on the way (an existing
directory is then no error, a file of that name is). TR-DOS and +3DOS have none ("the <system> volume '<id>' has no
directories"). Per file system:

- **FAT**: a directory entry with its `.` and `..`, one cluster, the time from `mtime` (the host's wall clock);
  a move rewrites the moved directory's `..`.
- **iS-DOS / TASiS**: a catalogue as `mkdir.com` makes it (the kernel has no mkdir; `mkdir.com` makes it through
  `$crfil`): status #21 (segmented), the slot in the parent first (a segmented parent that grows takes its block
  first), then the table block, then one run of 2 blocks (Classic) or 3 (Chic / TASiS); the table `01, start, N`.
  Entry 0 holds the name, the parent's block 0 at +12, the length at +14, NFILE 1, NEFIL 0, the date at +30 and the
  level (the parent's + 1, at most 6) at +16 on a Classic disk or +23 on a Chic one; the other blocks are zeroed. A
  delete is `delete.com`'s: only an empty catalogue, its entry 0 loses bit 0, the parent's entry loses bits 0 and 5
  and its +16, the blocks go free. A move of a catalogue is this library's (no iS-DOS tool moves one): entry 0 gets
  the new parent, and the levels of it and of every catalogue below follow; refused when the deepest would pass 6.
  Names are upper-cased, the kernel compares all 11 bytes.

## Attributes, times, labels

`SetAttributes(volume, path, letters)` takes the volume's attribute letters (`DiskFsVolume::attributes`), any case:
`+R-H` sets and clears these, `RA` sets exactly these and clears the rest. `Stat` / `List` show them in the entry's
positional `attributes` string. `SetTime(volume, path, mtime)` sets a file's or directory's date and time, `mtime` the
UTC seconds of the DOS wall clock (the media verbs' `time` takes `YYYY-MM-DD HH:MM[:SS]`, `now` or seconds; 1980 to
2107). `SetLabel(volume, label, mtime)` names the volume. Per file system:

| File system | Letters (`attributes`) | Entry string | Time | Label |
|---|---|---|---|---|
| FAT12 / 16 / 32 | `RHSA`: read only, hidden, system, archive | `RHSVDA` | 2-second steps, the entry's write date and time | the root's volume entry (made in a free slot, the FAT32 root grows for it; `""` deletes it) and the boot sector's copy at +43 / +71 (FAT32 also in the backup boot sector; `NO NAME` when cleared); up to 11 short-name characters, upper-cased |
| iS-DOS / TASiS | `RWHD`: read-protect (status bit 2), write-protect (3), hidden (4), delete-protect (7) | `SRWHD` (`S` segmented) | +28 time, +30 date; a catalogue gets both in its parent's entry and its own entry 0 | the device name in block 0 at +2: 8 characters, 11 on a Chic disk with no `DSK` at +10 |
| ATM CP/M | `RSA`: read only, system, archive (bit 7 of t1', t2', t3'; a read-only file is not deleted, written over or renamed) | `RSA` | none | none (`label`: the drive `D:` or the `.CDS` file) |
| MB-02 | none | none (the byte is the `attributes` field) | 2-second steps, the entry's #01 time and #03 date; a directory's in its header | the disk name at #26 of the boot sector (10 characters, blanks after it) |
| MDOS | `HSPARWED`: hidden, system, protected, archive, read, write, execute, delete (entry +20, bit 7 to bit 0; a file without `D` is not deleted or written over) | `HSPARWED` | none | the disk name at 192 (10 bytes, zeroes after it); an MDOS3 slot's name in its info sector (32 characters, blanks after it) |
| +3DOS | `RSA`: bit 7 of the type bytes t1', t2', t3' (read only, system, archive) | `RSA` | none | none |
| TR-DOS | none | | none | the disk title, 8 printable ASCII characters at +#F5 of the info sector |

On iS-DOS `device.sys`, `boot.sys` and the kernel file named by the descriptor keep their entries ("is a system
file"). A file system without attributes, times or a label answers "the <system> volume '<id>' has no file
attributes" / "keeps no file times" / "has no label" (`not-supported` on the media verbs). Volumes say which they take:
`attributes` (the letters), `times`, `labels`.

Volumes say `writable`: a floppy volume always (the emulator decides about the disk), a hard disk or card volume when
its device takes writes. The others answer "the <system> volume '<id>' is read only".

The media verbs `write`, `delete`, `rename`, `mkdir`, `attrib` and `label` do it on the disk in a slot: refused when the slot is write-protected,
the disk read only (`access readonly`) or the volume not writable (`not-supported`), a missing file
(`unreadable-source`), a file system with no directories for `mkdir` or no attributes / times / label (`not-supported`), and for a bad name, a taken `to`, a missing `data` / `in` / `to`, a letter the volume does not have, a bad `time` or a `start` above 65535
(`bad-request`). They run at a coherent moment (paused, stopped or between two frames) as a tool's edit
(`OpKind::ToolEdit`): a TTD session keeps it. A floppy's undo log keeps every track as it was; a hard disk or card is
read under the read tap and written through it, so its undo log keeps every sector the edit changed. A seek back
sets the disk to it. The reply's `access` says where the change went: `session` is dirty like after a guest write
(`media save`, `eject --save` keep it), `writethrough` is in the disk's file (a floppy at the next frame).

## Limits

- iS-DOS: a catalogue is made segmented of the standard size (as `mkdir.com`); there is no continuous one (TASiS
  disks have them, but no tool at hand makes one, so its layout is not known).
- iS-DOS: files are written with no R / W / hidden bits (`attrib` sets them after).
- ATM CP/M: no directories (USER1..USER15 are user areas, there while a file is in one). A hard disk partition is
  found only when the configuration record or a `.CDS` file on a partition found names it.
- MB-02: files are written as CODE (type 3) with a tape header, no attributes. The BS-DOS 309 hard disk layout
  comes from its BIOS source, not from an image of a real disk. MB-01 (BS-DOS 52-105, #01 at #03) is not read: no
  MB-01 disk or BS-DOS 52-105 binary is at hand, and its description (`mb02form.htm`) leaves the checksums it
  mentions, the date encoding and how the two lists grow open.
- MDOS: no file times (MDOS keeps none). An MDOS3 partition is found by its MBR type #49 (`nmi.a80`); without one the first `SDOS` sector
  is looked for in the first 4096 sectors (2 MiB) only, the firmware searches the whole disk.
- Not read: other file systems of a hard disk.
- An iS-DOS device on a hard disk is found at a cylinder boundary only.
