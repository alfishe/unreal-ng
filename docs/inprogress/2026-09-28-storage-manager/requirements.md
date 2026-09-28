# Unified media manager — requirements

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Reviewed (two rounds, 2026-09-28) |
| **Origin** | ZX-Evo plan phase E5b ("host folder as the SD card"), widened on request into one manager for every storage peripheral |
| **Design** | [technical-design.md](technical-design.md) |
| **Research** | [research.md](research.md): WinUAE, xpeccy-plus, DOSBox-X, and what unreal-ng does today |

## 1. The problem in one paragraph

Today every kind of medium is wired its own way. A floppy is inserted by `Emulator::LoadDisk`, a
chain of extension checks with the same insert block copied seven times. Eject is written four
times (WebAPI, CLI, Lua, Python), never frees the image, and in the WebAPI can eject the wrong
drive. A tape is a path string. The new SD card can only be set from the config file. HDD and CD
are dead code. Five extension tables disagree with each other. A model switch silently drops every
inserted medium, including unsaved disk writes. And none of it can present a **folder on the PC**
to the emulated machine, which is the main thing users of SD-card and hard-disk operating systems
(NedoOS, Wild Commander, esxDOS, the ZX-Evo ERS) want: build on the PC, run on the Spectrum,
without repacking an image.

## 2. Who uses it

| Actor | Needs |
|---|---|
| **User in the GUI** | One place that shows every drive of the current machine and what is in it; insert / eject / swap; "use this folder as the SD card"; save or export what the guest wrote |
| **Automation** (WebAPI, CLI, MCP, Lua, Python) | The same operations, with the same names, on every surface; scripted setups ("boot NedoOS from this folder") |
| **Tests** | Media built in memory or in the scratch folder; deterministic volumes; no GUI |
| **Peripheral authors** (SD, IDE, floppy, tape, CD) | Register a drive slot once; receive a medium through one interface; never parse formats or paths |
| **TTD / snapshots** | Know which medium is where and whether its contents changed |

## 3. Glossary

| Term | Meaning |
|---|---|
| **Slot** | A place a medium goes: floppy drive A, the tape deck, the Z-Controller SD slot, IDE master. Owned by a peripheral, registered with the manager |
| **Medium** | What is in a slot: built from a **source** and an **access mode** |
| **Source** | Where the contents come from: an image file, a host folder, a blank medium in memory, an uploaded file |
| **Access mode** | What happens to guest writes: `ReadOnly` (refused), `Session` (kept in memory, the source never changes, exportable), `WriteThrough` (written to the source file) |
| **Media set** | The list "slot → source + access" for one machine: what the config file, the GUI list and a model switch carry |
| **Folder volume** | A host folder presented as a FAT disk, sector by sector (`HostFolderFat`) |

## 4. Functional requirements

### 4.1 Slots and media

| ID | Requirement |
|---|---|
| FR-1 | Every storage peripheral registers its slots with one per-emulator manager: id (`fdd.a`, `tape`, `sd.zc`, `sd.ngs`, `ide0.master`, ...), kind (floppy, tape, block, optical), what it accepts, whether it is removable |
| FR-2 | One operation set for every slot: **list**, **info**, **insert** (source + access), **eject**, **create blank**, **save** (write back), **export** (write the current contents to a new file), **discard** (drop session writes) |
| FR-3 | The slot is separate from the medium: ejecting and inserting changes only the medium; the peripheral keeps its own state (WinUAE drive vs `cdslots`) |
| FR-4 | Format detection happens once, in the manager, by content first and extension second, from **one** table of formats that every surface uses (GUI filters, drag-and-drop, MCP, WebAPI errors) |
| FR-5 | A peripheral receives a ready medium through one interface per kind (`IBlockDevice` for SD/IDE; the existing `DiskImage` for floppies; the tape image for the tape) and never opens files itself |
| FR-6 | Insert and eject requested from any thread take effect on the emulation thread at an instruction-safe point; a removable slot stays "empty" between eject and insert for a time configurable per slot (defaults: floppy 2 s, SD 0.5 s, CD / ATAPI 3 s) |
| FR-7 | One source is in one slot at a time; a second insert is refused (naming the slot that has it) unless every slot using it is read-only |
| FR-8 | Configured media attach before the machine's first reset, so firmware can boot from them; a slot can be marked required (ZX Next SD) |
| FR-9 | Slots that belong to add-on cards (NeoGS, SMUC, Z-Controller add-on, divMMC) appear and disappear with the card; the medium of a removed card is parked, session writes included, and offered back |

### 4.2 Host folders

| ID | Requirement |
|---|---|
| FR-10 | A host folder can be inserted into any **block** slot (SD card, IDE disk) and is seen by the guest as a FAT volume with the folder's files and subfolders: FAT16 by default, FAT32 when asked for when the medium is created |
| FR-11 | Unmodified guest software reads it: the ZX-Evo ERS FAT driver, NedoOS (ChaN FatFs), Wild Commander, esxDOS, the NeoGS SD loader |
| FR-12 | Long names are visible to LFN-aware software; 8.3 names are generated Windows-style (`LONGNA~1.TXT`), Cyrillic in CP866 for NedoOS |
| FR-13 | Guest writes to a folder volume go into the change layer and never touch the folder; the result can be exported as an image or as a new folder |
| FR-14 | The folder is scanned once at insert (a snapshot). Host changes are picked up only by an explicit **rescan**, allowed only while there are no session writes |
| FR-15 | Limits are enforced and reported, never silent: files ≥ 4 GiB, symlinks, depth, count, files that do not fit; each skipped item is listed in the insert result and the log. Host service files (`.DS_Store`, `Thumbs.db`, `.git` ...) are skipped automatically on every host; the rules are named collections in one class, not ad-hoc checks |
| FR-16 | Nothing is ever written back into a source folder or a source image (unless the user asked for write-through on an image file). Results come out by **export**: a block image, or a file tree written to a new folder, at the current or any earlier version |
| FR-17 | A folder can be inserted into a **floppy** slot as a disk image (TRD first): files from that one folder only (no subfolders), only the files that fit, names made compatible with the target DOS, order from an optional manifest |
| FR-18 | A folder can be inserted into the **tape** deck: the same rules as FR-17; no size limit except an artificial capacity, one side of a C90 cassette (45 min) |
| FR-19 | The folder manifest (`.unreal-media.yaml` or `.json`, optional) sets order, names, types, start addresses, excludes, label, disk geometry and tape pauses; errors in it are reported, never fatal |

### 4.3 Persistence, switching, safety

| ID | Requirement |
|---|---|
| FR-20 | The media set of a machine is config-driven (one unified ini form, legacy keys still read) and can be saved |
| FR-21 | A **model switch** re-inserts every medium whose slot id exists on the new model; media that cannot follow are listed to the user; unsaved writes are never dropped silently (prompt in the GUI, error in automation unless `force`) |
| FR-22 | Unsaved changes are tracked per slot (dirty flag, number of changed sectors / tracks) and shown |
| FR-23 | A write-protected medium refuses writes with the peripheral's proper error (SD data response `#0D`, ATA ABRT, WD1793 WRITE PROTECT); nothing ever pretends to write |
| FR-24 | Relative paths in the config resolve against the config file's folder |

### 4.4 Surfaces

| ID | Requirement |
|---|---|
| FR-30 | One automation vocabulary: `media list | info | insert | eject | create | save | export | discard | rescan` on WebAPI, CLI, MCP, Lua, Python, with the slot id as the address. Existing `disk` / `tape` verbs stay as aliases |
| FR-31 | Notifications: one topic family for media changes (inserted, ejected, written/dirty, saved, activity) carrying the slot id; existing `NC_FDD_*` keep firing during migration |
| FR-32 | GUI: a media panel listing the machine's slots (kind, content, access, dirty, activity LED) with insert / folder / eject / save / export actions; drag-and-drop routes by the format table; recent media |

### 4.5 Time travel and snapshots

| ID | Requirement |
|---|---|
| FR-40 | TTD never produces a silently wrong replay. TTD v1 is media-agnostic (port-level sessions); the media set is fixed while a recording runs (changes refused, or the recording ended on request); every guest write is a replay barrier; controller state is in the TTD blobs ([integration-ttd-snapshots.md](integration-ttd-snapshots.md)) |
| FR-41 | The universal snapshot records the media set (source reference, `ContentId`, format, access, change layer at its version); on load mismatches are reported, media are never swapped automatically (roadmap UNS-6) |
| FR-42 | TTD v2 checkpoints reference media versions, so seeks cross writes (roadmap ST-6) |

### 4.6 Media history

| ID | Requirement |
|---|---|
| FR-25 | Every medium keeps a **history of versions** of its guest writes: an immutable source plus a versioned change layer, in memory first and spilled to disk when large; nothing is dropped silently |
| FR-26 | The history is readable at block level and at file level: contents of any block or whole file at any version, what changed between versions, the history of one file or block |
| FR-27 | Automation sees every medium at all times: source, format, parameters, whether and what it changed; with block and file operations on any version |
| FR-28 | TTD v2 and the universal snapshot restore the whole disk subsystem at any moment by referencing change-layer versions ([media-history-design.md](media-history-design.md) §7) |

## 5. Non-functional requirements

| ID | Requirement |
|---|---|
| NFR-1 | **Determinism**: the same folder (same files, sizes, mtimes) gives a byte-identical volume on every host and time zone: sorted byte-wise names, fixed serial, timestamps from mtime in UTC; an option for fixed timestamps in tests |
| NFR-2 | **Mount cost** proportional to the number of files, not their size: no file contents read at insert; a 2 GB folder mounts in well under a second |
| NFR-3 | **Memory** proportional to the number of files and session-changed sectors, never to the volume size |
| NFR-4 | **Cross-platform**: `std::filesystem` only; no OS-specific code outside `src/platform/<os>/` |
| NFR-5 | **Thread safety**: surfaces never touch peripheral state directly; one request queue per emulator |
| NFR-6 | **No hot-path cost**: sector reads from the peripheral are a virtual call; nothing on the Z80 instruction path |
| NFR-7 | **Testability**: every layer testable without an emulator; the folder volume checked by an independent FAT reader (ChaN FatFs in the test build) and by real guest ROMs |

## 6. Out of scope

- Physical host drives (raw `/dev/disk`, `\\.\PhysicalDrive`): rejected in the IDE design as unsafe.
- Live write-through of folder volumes (QEMU vvfat style): fragile under guest caching, breaks determinism.
- A file-level (AmigaDOS-style) folder mount: Spectrum operating systems have no filesystem hook, so the folder must become sectors.
- Archives (zip / 7z) as sources: possible later as another source kind; not in this plan.

## 7. Acceptance (end to end, real ROMs)

| ID | Scenario |
|---|---|
| ACC-1 | ZX-Evo ERS "5. SDcard boot" from a **folder** holding `SD_BOOT.$C` |
| ACC-2 | ZX-Evo ERS "Mount B:" of a TRD in a **folder** SD; TR-DOS lists and saves; the export contains the save; the folder is unchanged |
| ACC-3 | NedoOS `osatm3sd.$C` boots from a folder SD to its shell |
| ACC-4 | ZX-Evo `IMAGE.MNT` automount from a folder SD |
| ACC-5 | Model switch Pentagon → ZX-Evo keeps floppy A; the SD folder follows to a model with the same slot; unsaved floppy writes prompt |
| ACC-6 | Every automation surface inserts and ejects a floppy, a tape and an SD folder by slot id; the WebAPI eject of drive B never touches drive A |
| ACC-7 | A folder with Hobeta and plain files, and a manifest giving the order, inserted into drive A: TR-DOS `LIST` shows them in that order with compatible names; a file that does not fit is reported and absent; `RUN "boot"` works |
| ACC-8 | A folder inserted into the tape deck loads on 48K with `LOAD ""` in manifest order; a folder longer than 45 minutes of tape is cut with a report |
