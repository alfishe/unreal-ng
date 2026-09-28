# Unified media manager — research

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Sources** | `emulators/github/WinUAE` (+ FS-UAE, Amiberry), `xpeccy-plus`, `dosbox-x`, and this repository. Paths below are relative to each project's root |

## 1. WinUAE: one list of attachments, one block API per kind

**Configuration.** Every mounted hard-disk-like device is an entry in one list,
`uae_prefs.mountconfig[50]` of `uaedev_config_data` (`include/options.h:888`, `:174`, `:248`). The
entry type is `UAEDEV_DIR / HDF / CD / TAPE` (`options.h:176-179`). One `uaedev_config_info`
(`options.h:201-246`) carries:
- identity: device name, volume name, path
- access and boot: read-only, lock, boot priority
- geometry
- the controller it hangs on: type, board instance and unit
- a link to the CD image slot

The controller is an integer range: `UAE`, `IDE_AUTO..`, `SCSI_AUTO..`, `CUSTOM..`, whose offsets
index a ROM table (`include/filesys.h:99-114`). Floppies (`floppyN`, `floppyNwp`) and CD images
(`cdimageN`) are separate key families (`cfgfile.cpp:2123-2180`).

**Runtime.**
- `mountinfo.ui[]` (`filesys.cpp:248-252`) owns the units.
- Every hard-disk controller (IDE `ide.cpp`, Gayle `gayle.cpp`, SCSI `scsi.cpp`, the native
  `uaehf.device`) reads and writes through the same `hdf_read/hdf_write` over a `hardfiledata`
  (`filesys.h:129-135`).
- Formats are layers under that API (`hardfile.cpp:1090-1310`): host file I/O, then containers
  (VHD, CHD, compressed via `zfile`), then transforms (byte swap), then a synthetic prefix (the
  virtual RDB, `hardfile.cpp:387-425`).
- CDs have their own `device_functions` layer (`include/blkdev.h:134-160`).
- **Controllers never see formats.**

**Folders.**
- "Directory as a drive" is **file-level**: an AmigaDOS packet handler (`filesys.cpp:3878-6330`)
  maps guest file operations onto host files.
- A directory on an IDE/SCSI controller cannot be read as sectors (`scsi.cpp:473-497`).
- **WinUAE never synthesizes a sector image from a folder**, for hard disks, CDs or floppies. The
  closest thing is an in-memory ADF around one executable (`disk.cpp:563`). The reverse direction
  (images parsed into file trees: `archive_directory_adf/rdb/fat`) is well developed.

**Writes.**
- Read-only is enforced in the backend (`hardfile_win32.cpp:2686`).
- A failed open-for-write falls back to read-only (`:2234-2240`).
- Floppies that cannot be written in place get a per-track overlay file `<name>_save.adf`
  (`disk.cpp:1123-1300`). FS-UAE made overlays the default and writing to originals opt-in
  (`fs-uae od-fs/ChangeLog:1526`, `:1783`).
- There is no hard-disk undo layer; the `rootdirdiff` field is dead (`filesys.cpp:207`).

**Changes.**
- The GUI and scripts write `changed_prefs`; devices apply the difference in the emulation thread
  (`disk.cpp:3399`, `blkdev.cpp:617`).
- A swap is eject, a delay, then insert, so the guest sees the empty state: floppy 2 s
  (`disk.cpp:3195-3215`), CD 3 s (`blkdev.cpp:650-716`), removable hardfile `reinsertdelay`
  (`filesys.cpp:2314-2365`).
- Activity LEDs come from `gui_flicker_led(LED_HD|LED_CD, unit, 1 read / 2 write)` (`gui.h:22`).

**Savestates** store references only: path, CRC, head position (`disk.cpp:6000-6025`). A missing file
falls back cleanly (`:5955-5968`).

**GUI and automation.**
- One list view: type, device, volume, path, R/W, size, boot priority (`win32gui.cpp:5046-5059`).
- Buttons: Add Directory, Add Hardfile, Add Hard Drive, Add CD, Add Tape.
- Automation is "config text as the API": `cfgfile_modify` (`cfgfile.cpp:8173`), shared by the
  guest control library, IPC and Lua.

### 1.1 Copy / avoid

| Copy | Avoid |
|---|---|
| One list of attachments `{kind, source, access, slot}` shown as one GUI list | Three generations of config keys written twice (`filesystem=`, `filesystem2=`, `uaehfN=`) with a tri-state flag to de-duplicate (`cfgfile.cpp:5541-5678`) |
| One block API; formats as layers; controllers never see formats | Unit type inferred from field contents (`volname` set means a folder, `sectors==0` means RDB) and path-prefix magic (`:`) |
| Slot (drive) separate from medium (disc) | A 50-entry fixed array; an integer controller range tied to ROM-table indices |
| Changes applied in the emulation thread; eject → delay → insert | Read-only enforced in a different place per controller |
| Overlay for media that cannot be written in place; overlays as the default (FS-UAE) | Dead fields and stub caches |
| Savestates: reference + CRC, a clean "missing file" fallback | No media contents under savestates at all: wrong for TTD, which needs a journaled layer |

## 2. Folder as a FAT volume: xpeccy-plus, DOSBox-X, QEMU vvfat

| | xpeccy-plus `src/libxpeccy/vfat.c` | DOSBox-X `src/ints/bios_disk.cpp` |
|---|---|---|
| Partitioning | MBR, one type `#0C` partition at **LBA 2048** (`vfat.c:13`, `:363-373`) | MBR, partition at LBA 32 (`:384`, `:851`) |
| FAT type | Always FAT32; the volume is a power of two, 512 MB to 32 GB (`:16-17`, `:302-324`) | By size: FAT12 < 4 MB < FAT16 ≤ 2 GB < FAT32 (`:720-730`); cluster count padded to 4085 / 65525 (`:800-803`) |
| Clusters | Fixed 4 KB | 512 B – 32 KB by size |
| File layout | Contiguous run per file; the **FAT is computed** per sector by binary search (`:346-359`, `:414-431`) | Contiguous runs; the FAT is **stored** (`:434-451`); one `uint32` per data sector = 8 MB RAM per GB (`:404`, `:613`) |
| Directories | Generated per sector (`:464-512`) | Built in memory (`:453-656`) |
| 8.3 names | UTF-8 → CP866 uppercase for Cyrillic, `_` for illegal characters (`:76-145`); `~n` only on a real collision; tails can stack ("AB~1~2", a bug, `:178-180`) | DOSBox drive cache, `~N` (`drive_cache.cpp:587-670`) |
| LFN | Only when the 8.3 conversion was lossy (`:240`, `:265-271`) | For every entry (`:507`) |
| Reads | One cached `FILE*` (`:514-529`) | 8 handles, 256-sector cache (`:973-1071`) |
| Writes | SD refused with `#0D` (`sdcard.c:294-296`); IDE **silently discarded** (`hdd.c:150`) | In-memory sector diff; a write equal to the original frees the entry (`:922-971`); export to an image (`:1073-1096`); never written back |
| Host changes | Snapshot at mount, remount on machine switch | Built once at boot |
| Determinism | Yes, except `localtime()` timestamps depend on the host time zone (`:244`) | Serial is a content hash; order is host enumeration order: not guaranteed |
| Limits | 20000 nodes, depth 16, files ≥ 4 GB and symlinks skipped **silently** (`vfat_scan.cpp:10-26`) | Mount timeout 4 s; out of memory → read-only, files dropped |
| User surface | Same keys as images (`hdd.master`, `sdcard`); a folder is detected by `isDir` (`vfat_scan.cpp:48-74`) | `IMGMOUNT` of a folder; `convertdrivefat` |

**QEMU vvfat** (general knowledge, not verified locally): read-only by default. Its `rw` mode
interprets FAT and directory writes **live** onto host files. It is documented as experimental and
known to corrupt data under cluster reallocation. This is why live write-back is out of scope.

**Chosen here:**
- MBR + partition at LBA 2048.
- FAT32 for SD, FAT16 for IDE disks below 2 GB, with the cluster count kept away from the type
  thresholds.
- Contiguous runs with a computed FAT (xpeccy-plus).
- Windows-style `~1` tails that never stack; LFN only when needed; CP866.
- Session writes through the existing `SessionWriteMap` (DOSBox-X).
- Byte-wise sorting, UTC timestamps, a fixed serial.
- Limits reported, never silent.

## 3. unreal-ng today (2026-09-28)

| Area | State | Evidence |
|---|---|---|
| Floppy owner | `CoreState::diskImages[4]` / `diskDrives[4]`; FDDs created by the WD1793 even on +3; the +3 uPD765 reuses them | `corestate.h:47-52`, `wd1793.cpp:89-92`, `upd765.cpp:363` |
| Floppy insert | `Emulator::LoadDisk`: an extension chain, the swap block copied 7×; SCL branch uses the unresolved path (bug) | `emulator.cpp:1700-2083`, `:1807` |
| Floppy eject | No `Emulator` method; 4 copies (WebAPI, CLI, Lua, Python). None frees the image; the WebAPI one also ejects the WD1793's selected drive, whatever drive was asked (bug) | `tape_disk_api.cpp:1458-1533`, `:1513-1515` |
| Floppy save | `SaveDisk` has a second extension chain and no automation caller. GUI "Save as" bypasses it. A blank disk could be saved as a file named `<blank>`. `NC_FDD_DISK_WRITTEN` always reports drive 0 | `emulator.cpp:2100-2246`, `:2118-2122`, `loader_trd.cpp:161` |
| +3 writes | No dirty notification, no TTD `DiskWrite` marker | `upd765.cpp:1121` |
| Tape | A path string; `LoadTape` accepts tap/tzx only while the registry and the GUI offer more; eject copied 4× without notification | `emulator.cpp:1556-1613`, `:1581` |
| SD | `IBlockDevice` seam, `SdCardSpi`, `ZControllerSpi`; config only; no notifications, automation or GUI | `io/storage/*`, `portdecoder_atm3.cpp:79-91` |
| HDD / CD | Declared and never defined; `[HDD]` is in every ini and parsed by nothing | `io/hdd/hdd.h:48-151` |
| Formats | Only tape has a content-probing registry; five extension tables disagree (szx, spc/sta/ltp/zxt, csw/wav) | `loader_tape.h:48-73`, `filemanager.cpp:7-33`, `mcp-tools.cpp:407-410` |
| Model switch | The whole emulator is recreated: every medium and every unsaved write is dropped; the prompt does not check dirty flags | `lifecycle_api.cpp:948-978`, `mainwindow.cpp:2791-2871` |
| GUI | No eject, no drive picker, no SD/HDD UI; "Recent Files" is a disabled placeholder; `.img` is fixed to MGT floppies (collides with SD/HDD images) | `menumanager.cpp:115-198`, `filemanager.cpp` |
| TTD | Insert invalidates (disk-load, tape-load); eject does not; WD1793 writes are barriers; SD commands invalidate | `emulator.cpp:1597`, `:1762`, `wd1793.cpp:1459` |
