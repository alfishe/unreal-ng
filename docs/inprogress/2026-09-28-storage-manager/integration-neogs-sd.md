# Integration: NeoGS SD card (`sd.ngs`)

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Implemented on `neogs` at the master merge (2026-09-28); see neogs-tdd.md §7.6 |
| **Layers** | port decoder → port adapter → device → medium, with the slot and the manager beside them: [technical-design.md §1.1](technical-design.md#11-layers-from-the-guests-port-to-the-medium) |
| **Today (branch)** | `SoundChip_NeoGS` owns `std::unique_ptr<SdCardSpi> _sd` and opens `[NGS] SDCardImage` with `open(path, mode, type)` after resolving the path against the executable; `insertSdCard` / `ejectSdCard` refuse while TTD records; card writes mark a replay barrier (`markSdWrite`) |
| **Master** | `[NGS] SDCardImage` is parsed into `ngs_sd_card_path` and used by nothing |

## 1. What changes for the user

- The NeoGS card shows up in the media panel and in `media list` as `sd.ngs`. It can be an image
  or a **folder**: NEOGS.ROM, MP3s and module files straight from a PC folder.
- The same insert, eject and export as every other slot.

## 2. The slot

| Field | Value |
|---|---|
| id | `sd.ngs` |
| kind | `Block` |
| removable | yes, swap delay 500 ms |
| signals | card detect and write-protect switch: SSTAT `#12` bits 1 and 2 (neogs-tdd §5) |
| lifetime | registered while the card is fitted; parked when it is removed (FR-9) |
| accepts folder | yes; FAT16 by default. The NeoGS loader and players are tested on FAT16 and FAT32 cards on the branch; FAT16 matches the loader's "bare boot sector" path best. To confirm in M1 |
| default access | `Session` |
| registered by | `SoundChip_NeoGS` constructor, only when the card is fitted (`[SOUND] GSType=NeoGS`) |

## 3. Code changes at merge

| Where | Change |
|---|---|
| `sdcardspi.{h,cpp}` | take **master's** version (a superset of the branch API; same `STATE_SIZE` and state layout). See [e5-sd-card.md](../2026-09-15-atm-baseconf-highres-ports/e5-sd-card.md) §2 |
| `SoundChip_NeoGS::openSdImage` | replaced by the slot's `Attach` (`_sd->attach(medium.Block(), type)` + reselect). The manager does the path resolution (relative to the config file, FR-24) and opens the file; the branch's "retry relative to the executable" goes away |
| `insertSdCard` / `ejectSdCard` | become wrappers over `MediaManager::Insert/Eject("sd.ngs")`. The "refuse while recording" rule moves to the manager, for every slot (see TTD) |
| `NeoGSConfig::sdWrite` / `sdType` | map to `sd.ngs.access` and a slot option `sdType` (SDSC / SDHC / auto) that the slot passes to `attach` |
| DMA (`_dma.attachSd`) | unchanged: it talks to the `SdCardSpi` object, which stays in the card |

## 4. TTD

Unchanged in behavior, because the NeoGS rule becomes the common one
([integration-ttd-snapshots.md](integration-ttd-snapshots.md)):
- protocol state in the blob;
- a write is a barrier, at most one per frame;
- insert and eject are refused while recording.

The barrier call moves from `SoundChip_NeoGS::markSdWrite` into the manager's per-slot activity
hook, so every block slot gets it.

## 5. Tests

- The branch's NeoGS SD tests (boot from FAT16 / FAT32 cards, players, flasher) run unchanged
  with images.
- New: the same boot from a **folder** holding `NEOGS.ROM`.
- New: `sd.ngs` appears in `media list` only when NeoGS is fitted.

## 6. As built (2026-09-28)

- `SoundChip_NeoGS::SdSlot` as in §2; tags `sd`, `neogs`, `addon`. `[NGS] SDType` is passed to
  `attach`; `[NGS] SDWrite` / `SDWriteProtect` map to `sd.ngs` access and switch in `MediaConfig`
  (legacy section, like `[ZC]`), and to the access of the card's own inserts.
- A card fitted after creation: `MediaManager` keeps the set `ApplyConfiguredMedia` applied, and
  `RegisterSlot` inserts a later slot's configured medium when no parked one comes back. At
  creation slots register before the set is applied, so nothing goes in twice.
- `neogsmedia.h` (GUI Audio Settings, CLI / WebAPI / MCP / Lua / Python `sd_insert` / `sd_eject`)
  passes SD requests to the manager: `done` when the machine is not running, `queued` otherwise.
- Without a media manager (bare unit-test contexts) the card still opens `NeoGSConfig::sdCardPath`
  itself.
- Tests: `SoundChip_NeoGS_SdBoot.LoaderBootsNeogsRomFromAHostFolder` (FAT16 and FAT32 folder
  volumes), `NeoGSMedia_Test.SdSlotListedOnlyWhileNeoGSIsFitted` (listing, parking, session
  writes kept), `MediaManager_Test.SlotRegisteredLaterGetsItsConfiguredMedium`,
  `MediaConfig_Test.LegacyNeoGSWriteModeAndSwitch`.
