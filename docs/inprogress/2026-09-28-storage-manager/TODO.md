# Unified media manager — TODO

**Status:** requirements, technical design, media history design and integration designs reviewed
(two rounds, 2026-09-28). **M1 implemented** on branch `media-manager` (2026-09-28): ACC-1…ACC-4 pass
with real guest code (ERS, TR-DOS, NedoOS). M2-M6 and H1-H5 not started. PLAN.md row **#58**.

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
| [reuse-and-readiness.md](reuse-and-readiness.md) | Review round 2: reuse across BaseConf, TSConf, NeoGS, Scorpion, Profi, ATM2, Next, Sprinter; design changes G1-G12; readiness for M1 |
| [integration-ttd-snapshots.md](integration-ttd-snapshots.md) | TTD v1 rules (media-agnostic, barriers, recording guard); TTD v2 and UNS through media versions |

## Remaining

- [x] Review round 1 (2026-09-28): decisions folded into the documents
- [x] Review round 2 (2026-09-28): reuse across machines, readiness; G1-G12 folded in ([reuse-and-readiness.md](reuse-and-readiness.md)); M1 hook points to pin at its start
- [x] M1 block: core, folder pipeline, `HostFolderFat`, ZX-Evo `sd.zc` (= ZX-Evo E5b): ACC-1…ACC-4 (see "M1 as built" below)
- [ ] M2 floppy: slots, migration (fixes the eject / save bugs in research §3), folder as a disk image: ACC-7
- [ ] M3 tape: slot, migration, folder as a tape: ACC-8
- [ ] M4 automation verbs + Qt media panel: ACC-6
- [ ] M5 media across model switch: ACC-5
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
