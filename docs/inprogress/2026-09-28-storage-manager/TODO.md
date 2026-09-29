# Unified media manager — TODO

**Status:** requirements, technical design, media history design and integration designs reviewed
(two rounds, 2026-09-28). Implemented (branch `media-manager`, merged to master 2026-09-28): **M1** (block slots, folders as
FAT volumes; ACC-1…ACC-4 with the ERS, TR-DOS and NedoOS), **M2** (floppies in the manager; ACC-7 on the real
TR-DOS ROM), **M3** (the tape deck in the manager, folders as tapes; ACC-8 on the real ROM), **M4** (the
media verbs on every surface and the Qt media panel) and **M5** (media follow a model switch; ACC-5). M6 and
H1-H5 not started. PLAN.md row **#58**.

## Documents

| File | Topic |
|---|---|
| [requirements.md](requirements.md) | Problem, actors, FR / NFR / acceptance |
| [technical-design.md](technical-design.md) | **Start here.** `MediaManager`, slots, media, format registry, block stack, folder pipeline, `HostFolderFat`, config, model switch, phases M1-M6 and H1-H5 |
| [media-history-design.md](media-history-design.md) | Immutable source + versioned change layer (spill to disk), block and file views, export, tracking API, TTD v2 / UNS |
| [integration-next.md](integration-next.md) | ZX Next SD cards (later machine) |
| [research.md](research.md) | WinUAE, xpeccy-plus, DOSBox-X, QEMU vvfat, and unreal-ng today (with the bugs found) |
| [integration-zxevo-sd.md](integration-zxevo-sd.md) | `sd.zc` on ZX-Evo: phase M1 = ZX-Evo E5b |
| [integration-neogs-sd.md](integration-neogs-sd.md) | `sd.ngs` when the `neogs` branch merges |
| [integration-tsconf-sd.md](integration-tsconf-sd.md) | `sd.zc` on TSConf (phase 6) |
| [integration-ide-cd.md](integration-ide-cd.md) | IDE units as slots (`ide0.*`, `ide1.*`), a CD as a unit configured `cdrom`, with IDE rollout 1 |
| [integration-floppy.md](integration-floppy.md) | `fdd.a-d` migration (WD1793, uPD765), folder as a TR-DOS disk |
| [integration-tape.md](integration-tape.md) | `tape` migration |
| [integration-automation-gui.md](integration-automation-gui.md) | the `media` verbs on every surface, the Qt media panel |
| [media-control-design.md](media-control-design.md) | **Implemented** (M4 surfaces): the drive collection (tags, aliases, selectors, auto slot, eject dispositions, detached media), one `MediaControl` layer for the GUI, WebAPI + OpenAPI, CLI, MCP, Lua, Python; replaces the surface part of M4 |
| [reuse-and-readiness.md](reuse-and-readiness.md) | Review round 2: reuse across BaseConf, TSConf, NeoGS, Scorpion, Profi, ATM2, Next, Sprinter; design changes G1-G12; readiness for M1 |
| [integration-ttd-snapshots.md](integration-ttd-snapshots.md) | TTD v1 rules (media-agnostic, barriers, recording guard); TTD v2 and UNS through media versions |

## Remaining

- [x] Review round 1 (2026-09-28): decisions folded into the documents
- [x] Review round 2 (2026-09-28): reuse across machines, readiness; G1-G12 folded in ([reuse-and-readiness.md](reuse-and-readiness.md)); M1 hook points to pin at its start
- [x] M1 block: core, folder pipeline, `HostFolderFat`, ZX-Evo `sd.zc` (= ZX-Evo E5b): ACC-1…ACC-4 (see "M1 as built" below)
- [x] M2 floppy: slots, migration (fixes the eject / save bugs in research §3), folder as a disk image: ACC-7 (see "M2 as built" below)
- [x] M4 surfaces (media-control-design.md S1-S7): `MediaControl`, WebAPI `/media` + OpenAPI, CLI `media`, MCP `media`, Lua / Python `media_*`, Qt media panel, docs (`docs/features/media.md`) and recipe (`.recipe/media/use-media-slots.md`); as built: media-control-design.md §7
- [x] M3 tape: slot, migration, folder as a tape: ACC-8 (see "M3 as built" below); the media verbs reach it without surface work
- [x] M5 media across model switch: ACC-5 (see "M5 as built" below)
- [ ] M6 IDE / CD slots (with PLAN #13a)
- [ ] H1-H5 media history: versioned change layer, spill, file views, tracking API, UNS / TTD v2

## M1 as built (2026-09-28)

Where the implementation differs from, or adds to, the design:

| Topic | As built | Why |
|---|---|---|
| FAT oracle | `FatVolumeReader` (`core/src/emulator/io/storage/fat/`), our own FAT12 / 16 / 32 reader written from the specification, replaces vendored ChaN FatFs. It is checked against images it did not build (`FatImageBuilder`, a hand-made FAT12) and is the oracle of every `HostFolderFat` test. H3's FAT file view builds on it | no third-party code |
| `FatLayout` | folded into `HostFolderFat` (the layout is computed in `Build`) | one class, one test file |
| Code pages | CP866 (default) and CP1251 for short names: `UnicodeHelper` (`core/src/common/`) converts UTF-8 / UTF-16 / code points both ways; `[MEDIA] <slot>.codepage`, manifest `codepage` | Russian software exists in both encodings |
| Free space | `[MEDIA] <slot>.free` (bytes, default 256 MiB) sets the room for guest writes on a folder volume | small volumes keep tests and exports fast |
| Legacy `[ZC]` | read by `MediaConfig` into the media set; the decoder no longer reads config at its first reset; `CONFIG::zc` is gone | one path for every slot |
| TTD | `PeripheralId::EvoSdCard` (15) records the Z-Controller latch and the card's protocol state; SD commands no longer end a recording; a guest write is one replay barrier per frame | integration-ttd-snapshots.md §2 |
| ACC-3 | NedoOS `sd_boot.$C` (the ZX-Evo build: PS/2 keyboard, NemoIDE, NeoGS SD) from `testdata/machines/zxevo/nedoos/sdcard/` (loader, `term.com`, `cmd.com`, our `autoexec.bat`) reaches the shell prompt `M:/bin>`. `osatm3sd.$C` stalls after "loading term.com": it needs NemoIDE (ZX-Evo E6). Typing into NedoOS needs its PS/2 keyboard (E2b) | the design named `osatm3sd.$C` before it was tried |
| Same image in SD and HDD (G5) | the rule is in the manager (`MediaManager_Test.OneSourceInOneSlotUnlessBothReadOnly`); the ZX-Evo case moves to M6, when the machine has an IDE slot | no HDD slot on ZX-Evo yet |

Tests (one file per source file):

| Layer | Tests |
|---|---|
| Common | `unicodehelper_test.cpp`, `inifile_test.cpp` (section entries) |
| Storage | `readonlyguard_test.cpp`, `rawimage_test.cpp`, `memorydisk_test.cpp`, `sessionwritemap_test.cpp`, `fat/fatvolumereader_test.cpp` |
| Folder pipeline | `hostfolder/{servicefilefilter,foldersnapshot,foldermanifest,fatnamemapper,hostfolderfat}_test.cpp` |
| Manager | `media/{mediatypes,medium,mediaformatregistry,mediamanager,mediaconfig}_test.cpp` |
| ZX-Evo | `portdecoder_atm3_test.cpp` (`ZXEvoSdSlot_Test`: 4 GiB sparse SDHC image, swap delay in card-detect; `ZXEvoSdCardTtd_Test`: the common TTD rule), `debugger/ttd/atm/ttdevosdcard_test.cpp` (blob round trip mid-transfer) |
| Real ROM | `zxevo_ers_test.cpp`: `SdCardBootFromAHostFolder` (ACC-1), `SdCardFolderTrdMountedReadWrittenAndExported` (ACC-2), `NedoOsBootsFromAHostFolder` (ACC-3), `ImageMntAutomountFromAHostFolder` (ACC-4) |

macOS `fsck_msdos` accepts the generated FAT16 and FAT32 volumes, and Finder mounts them.

## M2 as built (2026-09-28)

| Topic | As built |
|---|---|
| Ownership | `Medium` owns the `DiskImage`; `CoreState::diskImages[]` is gone. `CoreState::diskFilePaths[]` stays as a display mirror that the slots keep current, until the surfaces read the manager (M4) |
| Slots | `FloppyDriveSlot` (`core/src/emulator/io/fdc/floppydriveslot.{h,cpp}`): `fdd.a`-`fdd.d` with the WD1793, `fdd.a`/`fdd.b` on the +3 (uPD765). Swap delay 2 s, folders accepted, the write-protect switch and `ReadOnly` both set the drive's write-protect sense. Registered by `Emulator::Init` after `Core` (`FloppyDriveSlots`), unregistered before the drives are deleted |
| Formats | `FloppyFormats` (`core/src/emulator/media/floppyformats.{h,cpp}`): content first (UDI, FDI, DSK, SCP, HFE, SCL signatures, Hobeta checksum, TR-DOS volume sector, MGT size), the extension only for TRD / MGT / TD0 ties. HFE, SCP and Hobeta are reachable for the first time. Save dispatches by the target's extension and retargets to `<stem>.udi` when TRD / SCL refuse; a Hobeta file is never a target |
| Emulator API | `LoadDisk`, `CreateBlankDisk`, `SaveDisk` are thin wrappers (pause, manager, resume; no swap delay, as callers expect the disk at once); new `EjectDisk(drive, force)`. The seven copied swap blocks and both extension chains are gone, and with them the SCL unresolved-path bug |
| Manager | `Save(slot, {path, allowRetarget})`: a save to a new path rebases the medium (`IMediaSlot::SourceChanged`), a disk from a folder, a Hobeta file or a blank disk needs a path. `Export` of a floppy is a copy: dirty flags and file path are restored after the writer (`DiskImage::captureDirtyState`). `WriteThrough` floppies are saved at the frame boundary after a write; a format that refuses falls back to `Session`. `Discard` is not offered for floppies (eject with force, insert again). New topic `NC_MEDIA_SAVED` |
| Notifications | `NC_FDD_DISK_WRITTEN` is posted by `SaveDisk` with the real drive (the loaders no longer post it with drive 0) |
| TTD | WD1793 and uPD765 writes go through `MediaManager::NoteWrite` (one barrier per drive per frame, the command in the marker text); the +3 had none before. `LoadDisk` / `CreateBlankDisk` / `EjectDisk` are refused while a recording runs (master's B9 guard, `RecordingAllows`; the manager's `recording` code for eject) |
| Surfaces | WebAPI, CLI, Lua and Python eject call `EjectDisk(drive, force = true)`: only the drive asked for, the disk freed (the WebAPI no longer ejects the WD1793's selected drive too). Unsaved writes are still dropped there, as before; the dirty check comes with the M4 media verbs. Qt "Save as UDI / TRD / SCL" go through `SaveDisk` |
| Folder as a disk | `FolderDiskBuilder` + `DiskTypeMap` (`core/src/emulator/io/storage/hostfolder/folderdiskbuilder.{h,cpp}`), TRD only, on `LoaderHobeta::addFile`. Format name `folder-trd` |
| One source, one slot | applies to floppies too: the same image in drives A and B is refused unless both are read-only |

Not in M2 (moved or noted):
- Model switch with a dirty disk: M5 (the transfer and the prompt).
- Qt picks the drive to save from the WD1793's selected drive, also on the +3; the drive picker comes with the M4 media panel.
- Raw PC floppy images (720 KB / 1.44 MB, G9): no loader yet; with Profi CP/M / Sprinter.
- SCL and +3 DSK folder builders: later strategies of the same builder.
- Fixed (2026-09-28): 40-track disks in the Beta 128's 80-track drive. TR-DOS steps twice per track for them (it reads the disk type in sector 9), as on real hardware. The emulation now matches: `DiskImage::isFortyTrack` (48 tpi media, set for images of at most 42 cylinders), `FDD::trackUnderHead` (head position p reads cylinder p / 2 in an 80-track drive), the +3's drives are 40-track mechanics, the WD1793 moves the head by step pulses only (it re-synced the head to the track register), and a `.trd` file takes its geometry from the TR-DOS disk type (a single-sided 40-track file used to load as 20 x 2). Tests: `fdd_test.cpp` (real ROM, 40 x 1 and 40 x 2, folder and `.trd`), `WD1793_Ports_Test.SeekMovesTheHeadByStepsOnly`, `LoaderTRD_Test.GeometryFromTheDiskType`.

Tests: `floppydriveslot_test.cpp`, `floppyformats_test.cpp`, `folderdiskbuilder_test.cpp` (ACC-7 on the real TR-DOS ROM: manifest order in `LIST`, the file that does not fit is absent, `RUN "boot"`), `mediamanager_test.cpp` (floppy export / save / write-through / folder), `upd765_test.cpp` (`UPD765Media_Test`: +3 writes are barriers once per frame).

## M3 as built (2026-09-28)

| Topic | As built |
|---|---|
| Ownership | `Medium` owns the parsed `TapeImage` (format name = the loader's id: `tap`, `tzx`; `folder-tzx` for a folder). The deck plays a copy of its blocks: `Tape::AttachImage` / `DetachImage`, and `EnsureImageLoaded` installs the attached image once per attach (a generation counter, so re-inserting the same file starts a fresh tape). A transport stop or a reset drops the deck's copy; the next play installs it again. `CoreState::tapeFilePath` stays as a display mirror; a path written there by hand (older tests) still parses the file as before |
| Slot | `TapeSlot` (`core/src/emulator/io/tape/tapeslot.{h,cpp}`): id and alias `tape`, tag `cassette`, no swap delay, folders accepted, `ReadOnly` (a tape medium is always read-only). Registered by `Emulator::Init` after `Core`, unregistered before `Core` is deleted |
| Formats | the registry's tape path is `TapeLoaderRegistry`: content probe, the extension only breaks ties (a TZX named `.bin` loads). Every loader format reaches every surface: `tap spc sta ltp zxt tzx`. There is no CSW or WAV loader: the Qt dialog no longer offers them (WAV goes through the audio import) |
| Emulator API | `LoadTape(path, error)` inserts through the manager (pause, insert at once, resume; TTD guard and session drop as before, `NC_FILE_LOADED` kept), accepts folders, and says why it failed. New `EjectTape(error)`, `IsTapeExtension(ext)` |
| Eject | the four copies (WebAPI, CLI, Lua, Python) and the Qt "Stop & eject" call `EjectTape`: `NC_MEDIA_EJECTED` once, refused while a TTD recording runs (WebAPI 409). `tape stop` stays the transport stop |
| Export | `media export tape <path>`: `.tap` when every block is a ROM-standard byte block (`TapArchiveWriter` gate), `.tzx` otherwise (`TzxArchiveWriter`) |
| Extension gates | GDB `load`, video wall drops (two places), Qt `FileManager` and MCP `load_software` take the loaders' list (MCP keeps a copy, checked by `McpSlots_Test.TapeExtensionsMatchTheTapeLoaders`); `media insert auto` sends tape files to `tape` |
| Folder as a tape | `FolderTapeBuilder` (`core/src/emulator/io/storage/hostfolder/foldertapebuilder.{h,cpp}`): an in-memory TZX of #10 blocks, decoded by `LoaderTZX`. Hobeta headers keep name, type, start and length (and the TR-DOS autorun line); plain files get 10-character names, Program for `B` types, else Bytes at 32768 (a 6912-byte `.scr` at 16384); `.tap` / `.tzx` files go on unchanged; the manifest's `files:` and `tape.pause` (default 1000 ms) apply. A file over 65 533 bytes is skipped; so is a file that would pass one side of a C90 (45 minutes at ROM timings), and placing goes on. The file order and 10-character names are `FolderDiskBuilder::OrderFiles` / `CompatibleName(name, 10)`, shared with the disk builder |

Not in M3 (moved or noted):
- A tape uploaded to `/tape/load` is a `File` source: the staged file stays after eject, as before (a `media insert` upload is an `Upload` source and is deleted).
- Tape recording (`SAVE` to tape): not emulated; a blank tape would be a `Blank` source exported to TZX / TAP.

Tests: `tapeslot_test.cpp` (slot, stop and re-install, re-insert and swap, eject, TTD refusal, every loader format, export to TZX and TAP, `auto`), `foldertapebuilder_test.cpp` (layout, manifest overrides, limits; ACC-8 on the real ROM: `LOAD ""` autoruns a program from a folder, `LOAD "" CODE` loads the next file), `emulator_path_validation_test.cpp` and `tzxload_integration_test.cpp` (content decides, reasons), `mediaformatregistry_test.cpp` (tape extensions, no blank tape).

## M5 as built (2026-09-28)

| Topic | As built |
|---|---|
| Transfer | `MediaManager::TakeMediaSet` (old machine, stopped: pending changes applied, every medium detached from its slot, the detached ones too) and `AdoptMediaSet` (new machine: each medium into the slot with the same id and kind, replacing the configured one; a configured medium in another slot with the same source gives way; no slot: detached when dirty, else closed). Live `Medium` objects move, the write-protect switch with them. Report: `attached` / `detached` / `closed` slot ids and lines for people |
| Switch | `ModelSwitch::Run` (`core/src/emulator/media/modelswitch.{h,cpp}`): the new machine is created first, next to the old one; dirty media without a slot on it are "stranded". `StrandedMedia`: `Refuse` (default: `dirty`, the list, the new machine destroyed, the old one untouched), `Save`, `Discard`, `Keep` (detached on the new machine). A failed save also leaves the old machine running. Then the old machine stops, the GUI unbinds (`beforeRelease`), the old instance is removed, the media are adopted; a selected old instance hands its selection over. The caller starts the new one |
| Surfaces | WebAPI `POST /emulator/{id}/model` (`stranded`, reply `media`, 409 `dirty` with `stranded`) + OpenAPI; CLI `model <name> [--ram] [--stranded]` (new); MCP `emulator_manage` action `switch_model` (new); Qt Machine menu (prompt Save / Discard / Keep Detached / Cancel; a note when media were closed or detached). Lua and Python have no model switch: a script's emulator object would outlive its machine |
| Design change | §8's "kept in the transfer until the user decides" became the detached state of the new machine (media-control-design.md §3.6): no second holding place. Clean media without a slot are closed, not carried |

Not in M5:
- Export as a stranded decision (one path per medium): export through the media verbs before the switch.

Tests: `modelswitch_test.cpp` (ACC-5: Pentagon -> ZX-Evo keeps floppy A with its unsaved writes, the same live medium, and the tape; ZX-Evo -> Pentagon strands a dirty `sd.zc`: refused with nothing changed, then keep / discard; a card save fails and leaves the old machine; a clean card is closed), `mediamanager_test.cpp` (`AMediaSetMovesToAnotherManager`).
