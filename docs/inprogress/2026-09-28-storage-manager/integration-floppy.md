# Integration: floppy drives (`fdd.a` … `fdd.d`) — Beta Disk WD1793 and the +3 uPD765

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Reviewed; manager phase M2: migration and folder as a disk image |
| **Layers** | port decoder → port adapter → device → medium, with the slot and the manager beside them: [technical-design.md §1.1](technical-design.md#11-layers-from-the-guests-port-to-the-medium) |
| **Today** | research §3: `CoreState::diskImages[4]` / `diskDrives[4]`; `Emulator::LoadDisk` / `SaveDisk` / `CreateBlankDisk`; eject copied in four surfaces |

## 1. What changes for the user

- Eject finally frees the disk.
- The WebAPI eject of drive B never touches drive A.
- "Save" works from automation.
- Unsaved disks are never dropped silently on a model switch.
- The GUI shows every drive with its disk and a dirty mark, not only the drive the WD1793 happens
  to have selected.
- A PC folder as a TR-DOS disk image.

## 2. The slots

| Field | Value |
|---|---|
| ids | `fdd.a`, `fdd.b`, `fdd.c`, `fdd.d` on Beta Disk machines; `fdd.a`, `fdd.b` on the +3 |
| kind | `Floppy` (the medium is a `DiskImage`): TR-DOS 256-byte sectors, and 512 / 1024-byte PC-style layouts for Profi CP/M (720 KB), Sprinter (720 KB / 1.44 MB) and the +3 (review round 2, G9) |
| removable | yes, swap delay 2 s (WinUAE), configurable |
| default access | `Session` with explicit Save (today's behavior, now named); `WriteThrough` = save after every written track; `ReadOnly` = the drive's write-protect tab |
| registered by | the **active** controller: the WD1793 registers A-D except on +3; the uPD765 registers A-B on +3. Today the WD1793 is created and exposes four drives even on +3, so "drive present" checks pass wrongly (research §3); registration fixes what the surfaces see |

## 3. Migration (M2)

| Today | After |
|---|---|
| `Emulator::LoadDisk`: 7 copies of the swap block, an extension chain | `MediaManager::Insert("fdd.b", File)`; the registry probes TRD / SCL / FDI / UDI / TD0 / DSK / MGT / HFE / SCP by content; one swap path. `Emulator::LoadDisk` stays as a wrapper for compatibility |
| eject in WebAPI / CLI / Lua / Python, no free, wrong drive in WebAPI | `MediaManager::Eject`; surfaces call it; the medium is freed |
| `SaveDisk` extension chain; GUI "Save as" bypasses it | `Save(slot)` / `Export(slot, path, format)` through the registry's writers; the TRD-cannot-hold → UDI retarget is reported in `Result.report` |
| `CreateBlankDisk` (no pause, no notification, `<blank>` path) | `CreateBlank(slot, {format: trd, tracks: 80, sides: 2})`; source type `Blank`; Save asks for a path (never writes `<blank>`) |
| `NC_FDD_DISK_WRITTEN` drive hard-coded 0 | real drive, plus `NC_MEDIA_SAVED {slot}` |
| +3 writes: no dirty notification, no TTD marker | the uPD765 slot reports writes like the WD1793 |
| `.img` always MGT | content probe: 819 200 bytes with an MGT directory → floppy |

The FDD keeps its mechanics: motor, head, index, write-protect sense. `Attach` = `FDD::insertDisk(image)`
plus the disk-change line; `Detach` = `FDD::ejectDisk()`. The `DiskImage` is owned by the medium,
never by `CoreState`.

## 4. Folder as a disk image (M2)

WinUAE has no precedent (research §1): this is new. `FolderDiskBuilder` takes the shared folder
pipeline (technical design §6.0) and writes a `DiskImage` through a format strategy
(`IFolderDiskFormat`). TRD comes first; +3 DSK (+3DOS directory) and SCL are further strategies with
the same rules.

### 4.1 Rules

| Rule | TRD |
|---|---|
| **One folder** | top level only; subfolders are skipped and reported |
| **Order** | files listed in the manifest's `order` first, in that order; then the rest, byte-wise sorted |
| **Only what fits** | files are placed in order; a file that does not fit is skipped and reported, and placing continues with the next file. TRD limits: 128 catalog entries, 2 544 free sectors (80 tracks × 2 sides; manifest `disk` can pick 40 / 1), 255 sectors (65 280 bytes) per file |
| **Hobeta files** (`*.$B`, `*.$C`, … with a valid header checksum) | name, type, start, length and sector count from the header; the data follows |
| **Other files** | name and type made compatible (below); start 32768 for code (16384 for a 6912-byte `.scr`), manifest `files:` overrides name / type / start / BASIC autorun line |
| **Compatible names** | 8 bytes: printable ASCII; `"` (it breaks BASIC commands), control and non-ASCII characters become `_`; case kept (TR-DOS names are case-sensitive); cut or space-padded to 8 |
| **Types** | from the extension through `DiskTypeMap`, a collection in the class like the service-file rules: `B`/`bas` → `B`; `C`/`bin`/`cod`/`code`/`scr`/`rom` → `C`; `D` → `D`; `#` → `#`; anything else → `C` |
| **Collisions** | same name and type after mapping: the last character becomes `1` … `9`, then the file is skipped and reported |
| **Label** | the manifest `label`, else the folder name, made compatible, 8 bytes |

### 4.2 Layout and writes

- Catalog on track 0, sectors 1-8.
- Disk info in sector 9: first free sector / track, type `#16`, file count, free sectors, TR-DOS id
  `#10`, label.
- Files are laid out contiguously from track 1, sector 0, in the order above.
- **The build is one-way.** The whole disk image is built in memory; guest writes go into that
  image (through the change layer, so history and TTD v2 cover it); saving is the standard "save
  the disk image" request (TRD, or UDI when TRD cannot hold it). Nothing is ever spread back into
  the host folder.
- It is also a way to get PC files into TR-DOS without an SD card: drop a folder on drive A.

### 4.3 Worked example

Folder `~/zx/demo/` holds `boot.$B`, `game.$C` (40 KB), `intro.scr` (6 912 bytes), `notes.txt`,
`.DS_Store` and a manifest with `order: [boot.$B, intro.scr]` and `exclude: [notes.txt]`:

| # | TR-DOS entry | From |
|---|---|---|
| 0 | `boot    B` | Hobeta header |
| 1 | `intro   C` start 16384, 27 sectors | plain file, `scr` rule |
| 2 | `game    C` | Hobeta header |
| skipped | `.DS_Store` (service), `notes.txt` (manifest exclude) | report |

## 5. TTD

- Unchanged: a disk write is a replay barrier (WD1793 today; the uPD765 gets it too).
- Changed: insert and eject are refused while a recording runs, unless the caller asks to end it
  (the common rule, [integration-ttd-snapshots.md](integration-ttd-snapshots.md) §2). Today
  insert invalidates the session and eject does nothing.

## 6. Tests

- The existing disk tests run through the new path. The four surfaces' eject tests assert:
  - the image is freed;
  - the other drive is untouched.
- Model switch with a dirty disk → error unless `force`.
- `FolderTrd`:
  - catalog and data from Hobeta and plain files;
  - overflow report;
  - TR-DOS `LIST` / `LOAD` on the real ROM.
