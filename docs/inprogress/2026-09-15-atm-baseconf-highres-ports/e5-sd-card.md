# Phase E5 — the Z-Controller SD card, and one SD card model for every machine

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Done for image files (E5a). Folder volumes (`HostFolderFat`) are the next step, E5b. Plan: [implementation-plan.md](implementation-plan.md) phase E5 |
| **Gaps closed** | ST-1 (Z-Controller SD card) of [gap-analysis.md](gap-analysis.md), for image files. Synchronization decisions S1, S3, S4 of [tdd-storage-sd-ide-cd.md](tdd-storage-sd-ide-cd.md) §1 are built |
| **Design** | [tdd-storage-sd-ide-cd.md](tdd-storage-sd-ide-cd.md) §2 ("As built" notes in §2.5) |

## 1. What the user gets

Put a disk image in `[ZC] SDCardImage=` (or call `PortDecoder_ATM3::InsertSdCard`), and the
unmodified ZX-Evo software finds a card in the slot:

| Goal | How it works now | Proven by |
|---|---|---|
| Boot a program from SD | ERS "5. SDcard boot" finds `SD_BOOT.$C` in the FAT root, loads the Hobeta body at its start address and runs it | `ZXEvoErs_Test.SdCardBootRunsSdBootFromAFatVolume` |
| Browse the card | ERS "F. File browse" lists the FAT root of the card | same test file |
| **Mount a TR-DOS image from SD** | "F. File browse" → ENTER on `game.trd` → "Mount B:". TR-DOS then reads and **writes** drive B. Each sector goes through the virtual TR-DOS trap (E4) into the ERS, and from there to the card | `ZXEvoErs_Test.SdCardTrdMountedAsDriveBReadAndWrittenByTrdos` |

Worked example (the mount test, step by step):

```
ERS menu   F            -> "File browser": EYEACHE.TRD
           ENTER        -> "TRD to:" 0 Only load, 1-4 Ramdisk A-D, 5-8 Mount A-D, ...
           6            -> "Mount B: EYEACHE .TRD", #13BD = %0010
           S            -> NEO-DOS "Virtual Drive: B"
NEO-DOS    *"b"  LIST   -> Disk Drive: B, 2 File(s), Free Sector 2392,
                           EYEACHE-<B>141 35916 35916, boot <B> 11 02568 02568
           SAVE "t" CODE 32768,256
           LIST         -> 3 File(s), Free Sector 2391
image file              -> the TRD inside the FAT volume now holds entry "t       C"
                           at #8000, 256 bytes, and the saved bytes in its sector
```

Nothing of this is special-cased in the emulator. The ERS does the FAT, the TRD mounting and the
WD1793 emulation in Z80 code, as it does on the board. The emulator provides the card, the SPI
port and the trap.

## 2. The pieces: one SD card, reusable by every machine

```
PortDecoder_ATM3 (#77 / #57 decode)        NeoGS (on the neogs branch)      TSConf (later)
        |                                          |                              |
  ZControllerSpi  (io/spi)  ---------------------------------------------- same glue + DMA
        |  select() / exchange()                   |
  SdCardSpi       (io/sdcard) <------------ same class ----------------------------
        |  IBlockDevice (512-byte sectors)
  RawImage | MemoryDisk | (HostFolderFat, E5b)          (io/storage)
        +  SessionWriteMap for "writes for this session only"
```

| Piece | File | What it is |
|---|---|---|
| `IBlockDevice` | `core/src/emulator/io/storage/iblockdevice.h` | A disk as numbered 512-byte sectors. The one seam for SD cards **and** the IDE disks to come (IDE design §7.1) |
| `RawImage` | `io/storage/rawimage.{h,cpp}` | An image file, read-only or write-through; a partial last sector reads zero-padded |
| `MemoryDisk` | `io/storage/memorydisk.{h,cpp}` | A zero-filled disk in memory (tests, "new blank card") |
| `SessionWriteMap` | `io/storage/sessionwritemap.{h,cpp}` | Writes kept in memory over any medium. The medium never changes. Writing the original data back frees the entry. `ExportTo()` writes medium + changes to a new image |
| `SpiDevice` | `io/spi/spidevice.h` | One SPI device: `select()`, `exchange()` (unchanged from the `neogs` branch) |
| `SdCardSpi` | `io/sdcard/sdcardspi.{h,cpp}` | The SD card in SPI mode (SDSC/SDHC, CMD0...CMD59, ACMD41, byte-counted latency). Lifted from the `neogs` branch with its API kept. Only its storage changed, from a `FILE*` + private overlay to `IBlockDevice` + `SessionWriteMap`. New: `insert(medium, mode)`, `media()`, `sessionWrites()`, `setCommandListener()` |
| `ZControllerSpi` | `io/spi/zcontrollerspi.{h,cpp}` | The Z80 side of the Z-Controller: /CS in bit 1 of the config port, the one-byte read pipeline of the data port, reset = deselected. 2-byte POD state |
| `statebytes.h` | `core/src/common/statebytes.h` | Little-endian field helpers for the card's state blob (unchanged from `neogs`) |

**Reuse.** The card has no machine-specific code. A new host needs only its port decode, in about
ten lines: call `ZControllerSpi::WriteConfig/WriteData/ReadData` from the decoder, or drive
`SdCardSpi::select/exchange` from its own SPI master (NeoGS). The medium can be anything that
implements `IBlockDevice`.

**Merging `neogs`.** The branch adds the same `sdcardspi.*`, `spidevice.h`, `statebytes.h` and
`fatimagebuilder.h`. At merge, take master's `sdcardspi.{h,cpp}`: they are a superset of the
branch API (`open(path, mode, type)`, `STATE_SIZE`, `saveState`/`loadState`, the listeners). The
other three files are identical.

## 3. ATM3 wiring

| Port | Arm | Behavior (zports.v:449-452, :808-836; spihub.v) |
|---|---|---|
| `#77` write, outside shadow | `SdConfig` | D1 = /CS |
| `#77` read, outside shadow | `SdConfig` | `#00` (presence is in the AVR, register C) |
| `#57` write | `SdData` | sends the byte |
| `#57` write in shadow with A15 = 1 (`#8057`) | `SdConfig` | D1 = /CS, what NedoOS uses |
| `#57` read | `SdData` | the byte of the previous exchange; starts a new one sending `#FF` |

- **Power-on and reset.** `[ZC] SDCardImage` is inserted at the first reset (power-on). A Z80 reset
  keeps the card and its session writes and only deselects it, like the reset button on the board.
- **AVR.** Register C bit 3 = card present, bit 2 = the slot's write-protect switch
  (`[ZC] SDWriteProtect`). As on hardware the switch is only reported; the card does not enforce it.
  Use `SDWrite=off` for a card that refuses writes.
- **Configuration** (`data/configs/atm3/unreal.ini`):

  ```ini
  [ZC]
  ;SDCardImage=wc.img     ; SDCARD= (UnrealSpeccy) is an alias
  SDWrite=session         ; session | persist | off
  SDWriteProtect=0
  ```

  The shipped config no longer points at a `wc.img` that does not exist. The two unused
  UnrealSpeccy fields `CONFIG::zc` / `zc_sd_card_path` were replaced by `CONFIG::zc` (`sd_image_path`,
  `sd_write_mode`, `sd_write_protect`).

## 4. TTD

Storage is not in the TTD checkpoints yet. The rule is rollout 1 of the IDE design §10.0: a recording
must never be silently wrong. The first SD **command** of a recording (from the first valid CMD0 on)
ends it: `TimeTravelManager::RequestInvalidation(reason)` is called from the port, and the next
`OnFrameBoundary` applies it on the frame thread before that frame's checkpoint is taken. Never
from inside a port handler, never from another thread. An idle card in the slot, or clocking
without a command, records normally. `ZControllerSpi::State` (2 bytes) and `SdCardSpi::saveState`
are ready for rollout 2.

## 5. Tests

| Test | Design ID | Pins |
|---|---|---|
| `RawImage_Test.*` (5) | IDE L3 "Raw" | padding, bounds, read-only leaves the file, write-through at `lba × 512` survives reopen, refusals with a reason, content id |
| `MemoryDisk_Test.*` (2) | — | zero fill, bounds, read-only, distinct ids |
| `SessionWriteMap_Test.*` (4) | IDE L3 "Session write map" | read-back, medium untouched, write-equal-frees, discard, byte-exact export |
| `SdCardSpi_Test.*` (13) | NeoGS SD | the 9 lifted protocol tests + any medium via `insert`, Persist on a read-only medium answers a write error, session export, command listener |
| `ZControllerSpi_Test.*` (4) | ZC-2, ZC-5 | previous-byte pipeline, /CS bit 1 and reset, empty slot, state round trip in the middle of a multi-block read |
| `PortDecoder_ATM3_Test.ZController_*` (5) | ZC-1, ZC-2, ZC-3, ZC-4 | `#77` read `#00` / ATM port in shadow; a sector read through the ports; `#8057` chip select in shadow; AVR register C bits; a reset keeps the card and deselects it |
| `ZXEvoSdCardTtd_Test.FirstSdCommandEndsTheRecording` | ST-TTD-1 (SD) | idle card and clocking record on; the first command requests the end, the frame boundary applies it |
| `ConfigZcSdWrite_Test.*` (2) | — | `SDWrite=` values |
| `FatImageBuilder_Test.*` (3) | — | the test FAT images (helper lifted from `neogs`) |
| `ZXEvoErs_Test.SdCardBootRunsSdBootFromAFatVolume` | ERS-SD-1 | real ROM, §1. Fails without the card (mutation check) |
| `ZXEvoErs_Test.SdCardTrdMountedAsDriveBReadAndWrittenByTrdos` | ERS-MNT-1 (SD) | real ROM, §1 worked example, including the saved bytes in the image file |

## 6. Verification (2026-09-28)

- Main tree: full build (all targets) with zero compiler warnings; `core-tests` **4114 passed, 0 failed**.
- Isolated worktree = HEAD `005771c8` + only the E5 files: `core-tests` 4113 passed, 1 failed:
  `INTTiming_Test.ApplyDefaults_Plus3`. That failure is on HEAD and unrelated: the committed test
  expects the +3 INT length 36 T, the committed code still gives 32 T, and the fix is in
  uncommitted `ulacontention.*` work outside this phase.
- Mutation check: without the card the ERS SD boot test fails (the ERS never reaches the program).

## 7. Next (E5b and later)

E5b is designed as phase M1 of the unified media manager: [technical-design.md](../2026-09-28-storage-manager/technical-design.md), [integration-zxevo-sd.md](../2026-09-28-storage-manager/integration-zxevo-sd.md).


| Item | Where |
|---|---|
| `HostFolderFat` (a PC folder as the card, S2), shared with IDE R1-6; `SDFolderFs=` | E5b |
| ERS-SD-2 (SD boot from a folder), NOS-SD-1 (NedoOS `osatm3sd.$C` boots to its shell from a folder: it needs `bin/`, `ini/`... so it waits for folder volumes, whose headline use case it is) | E5b |
| ERS-MNT-2 (`IMAGE.MNT` automount) | E5b (easy with a folder volume) |
| Automation `sd insert/eject/info/export` (TSConf API-1 shape), GUI menu | E10 |
