# ZX-Evo BaseConf storage: SD card, NemoIDE hard disk, ATAPI CD-ROM — technical design

| | |
|---|---|
| **Date** | 2026-09-27 |
| **Status** | Design, ready for review. Nothing implemented |
| **Closes** | gaps **ST-1…ST-4**, the storage parts of **R-2**, **T-1**, **T-2** of [gap-analysis.md](gap-analysis.md); user items "IDE HDD (image, folder)", "CD ATAPI", and the SD/HDD sources for "mount TR-DOS from SD/HDD" |
| **Builds on (does not repeat)** | the shared IDE design [2026-09-25-ide-hdd-design.md](../2026-09-21-profi/2026-09-25-ide-hdd-design.md) (ATA core, adapters, media, ATAPI, TTD interim rule, automation); the SD card model of [neogs-tdd.md](../2026-09-19-general-sound/neogs-tdd.md) §5.4-§5.5; TSConf storage [technical-design.md](../2026-09-27-tsconf/technical-design.md) §3.11 and plan phase 6 |
| **Hardware source** | [baseconf-hardware-reference.md](baseconf-hardware-reference.md) §A.9 (IDE), §A.13 (SD), §C 3.3 and §C 4 (ERS devices and boot) |

## 0. Summary

The Evo has three storage paths. This design wires the shared components to ATM3 and records five
synchronization decisions so that Profi, Scorpion SMUC, Pentagon Nemo, ATM3 and TSConf end up with
**one** implementation of each piece.

| Path | Board hardware | Shared component | ATM3-specific part |
|---|---|---|---|
| SD card | Z-Controller SPI (`#77` CS, `#57` data) | `SdCardSpi` (NeoGS) | `ZControllerSpi` glue, **shared with TSConf** |
| Hard disk | NemoIDE with the Evo "divide" extension | `AtaChannel` + `AtaDisk` + media (IDE design §5-§7) | `IdeAdapterNemo` in Evo mode: a fourth latch pattern (§3.2) |
| CD-ROM | ATAPI drive on the same NemoIDE channel | `AtapiCdrom` + `IsoImage` (IDE design §7.5) | nothing: same adapter |

All file systems (FAT on SD/HDD, TR-DOS images inside them, ISO 9660) are handled by guest software:
the ERS, NedoOS, Wild Commander. The emulator provides block devices only, plus the **host folder**
feature that turns a PC folder into a FAT volume.

**What the user gets, end to end:**

| User goal | How |
|---|---|
| Boot NedoOS / Wild Commander from SD | `[ZC] SDCardImage=<file.img>` or a folder → ERS "5. SDcard boot" runs `SD_BOOT.$C` |
| Mount a TRD from a PC folder | put `game.trd` in a folder, mount the folder as SD (or HDD) → ERS "Mount A:" picks it → TR-DOS runs it through the virtual drive ([tdd-virtual-trdos.md](tdd-virtual-trdos.md)); writes stay in the session map, exportable |
| Mount a TRD from a real SD/HDD image | same, with an image file |
| Boot from HDD | `[HDD] Image0=` → ERS "B. HDD boot": LBA 2, 24 KB to `#6000` |
| Boot a CD | `[HDD] Image1=disc.iso` (slave; `.iso` auto-selects ATAPI) → ERS "D. CD boot": `AUTORUN.ZX` |

## 1. Synchronization decisions (cross-machine)

These change the shared designs slightly; the owning documents get a pointer to this section.

| # | Decision | Affects |
|---|---|---|
| **S1** | **One block interface.** The SD card reads and writes through the IDE design's `IBlockDevice` (512-byte sectors). TSConf's proposed `ISdBlockStore` is that interface; `FileBlockStore` = `RawImage`, `VirtualFatBlockStore` = `HostFolderFat` | TSConf td §3.11, IDE design §7.1, NeoGS `SdCardSpi` (constructor takes an `IBlockDevice`) |
| **S2** | **One host-folder FAT synthesizer** (`HostFolderFat`, IDE design §7.4) for SD and IDE. Defaults per consumer: SD → FAT32, MBR, partition at LBA 2048 (TSConf, xpeccy-plus); IDE → FAT16 below 2 GB (IDE design §7.4.2). Names in CP866 with LFN | TSConf VFAT-1…3 tests run against `HostFolderFat` |
| **S3** | **One session-write mechanism.** `SessionWriteMap` (IDE design §7.4.3) is a decorator over any `IBlockDevice`; `SdCardSpi` `WriteMode::Session` uses it instead of its own overlay. So a host folder mounted as SD is writable for the session (TSConf's draft said read-only) and can be exported to `.img` | NeoGS `SdCardSpi`, TSConf td §3.11 |
| **S4** | **One Z-Controller glue** (`ZControllerSpi`) for ATM3 and TSConf. TSConf adds only its DMA SPI path on top | TSConf td §3.11 (`TsConfSpi` becomes `ZControllerSpi` + DMA hook) |
| **S5** | **Evo NemoIDE latch pattern** (§3.2) is added to the IDE design's latch helpers as `EvoNemoLatch`; the IDE design's Evo row is corrected: **BaseConf has no IDE DMA** (only TSConf does) and the ports answer in and out of shadow | IDE design §4 table, §5 file list, §12.2 truth tables |

## 2. SD card (ST-1)

### 2.1 Port behavior (hardware reference §A.13)

| Port | When | Read | Write |
|---|---|---|---|
| `#xx77` | not in shadow | `#00` (card present, not write-protected; real presence is in the AVR register C) | D1 = /CS (1 = deselected), D0 ignored |
| `#xx57` | always, except shadow with A15 = 1 | the byte received by the **previous** exchange; starts a new exchange sending `#FF` | sends the byte; the received byte becomes the next read value |
| `#xx57` with A15 = 1 | shadow | as `#77` read | as `#77` write (CS) — NedoOS uses `#8057` |

Timing: an exchange is 16 FPGA clocks (8 CPU clocks at 14 MHz); the shortest back-to-back `IN`/`OUT`
is longer, so the model completes each byte instantly (same as every reference emulator).

### 2.2 Components

```
PortDecoder_ATM3  ──►  ZControllerSpi (shared with TSConf, S4)
                          │ select(bool), exchange(uint8_t)
                          ▼
                       SdCardSpi (NeoGS, emulator/io/sdcard)   ── SetSdStatus ──► EvoAvr register C bits 3/2
                          │ IBlockDevice (S1)
                          ▼
             RawImage | HostFolderFat (S2) | MemoryDisk      (+ SessionWriteMap, S3)
```

`ZControllerSpi` (`core/src/emulator/io/spi/zcontrollerspi.{h,cpp}`): holds `cs` and `rxLatch`;
`Write57(v)`: `rxLatch = card.exchange(v)` when selected, else `#FF`; `Read57()`: returns `rxLatch`,
then `rxLatch = card.exchange(0xFF)`; `Write77(v)`: `card.select(!(v & 2))`. POD state for TTD.

### 2.3 Configuration

```ini
[ZC]
SDCardImage=wc.img        ; file → RawImage; directory → HostFolderFat. Alias: SDCARD (existing ini key)
SDWrite=session           ; session | persist | off   (neogs-tdd §6 semantics)
SDWriteProtect=0          ; slot switch: reported in AVR register C bit 2
SDFolderFs=FAT32          ; FAT32 | FAT16 (folder volumes only)
```

`SDDelay` (existing ini key) is accepted and ignored (the card model has deterministic latency).

### 2.4 Presence in the ERS

The ERS and NedoOS find the card by protocol (CMD0/CMD8/ACMD41), not by `#77`. The AVR register C
bits tell the ERS whether to show "SD card lost"; `EvoAvr::SetSdStatus` is driven by the slot.

## 3. NemoIDE hard disk (ST-2, ST-3)

### 3.1 Port decode

Full low-byte decode, answers in and out of shadow, never gated (hardware reference §A.9):

| Low byte | ATA register | Notes |
|---|---|---|
| `#10` | CS0 reg 0 data | latch rules §3.2 |
| `#11` | — | the high-byte latch (not an IDE access) |
| `#30,#50,#70,#90,#B0,#D0,#F0` | CS0 regs 1-7 (A7..A5) | |
| `#C8` | CS1 reg 6: alternate status / device control | |
| `#08,#28,#48,#68,#88,#A8,#E8` | CS0 regs 0-7 (A7..A5) | RTL aliases (`(x & 7) == 0 && bit3 != bit4`); `#08` is a plain 16-bit data access whose high byte is lost |

Prerequisite: **P-1** (`#FE` exact decode), otherwise every even port above is the border.

### 3.2 `EvoNemoLatch` — exact rules (RTL `fpga/base_trdemu/trunk/z80/zports.v:554-654`)

Three flags: `rdTrig`, `wrLoTrig`, `wrHiTrig`. "Other IDE access" = any access to an IDE register
port or `#11` other than the case listed.

| Access | Condition | IDE bus | Returned / stored | Flags after |
|---|---|---|---|---|
| `IN #10` | `rdTrig = 0` | 16-bit read | returns low byte; high byte → latch | `rdTrig = 1` |
| `IN #10` | `rdTrig = 1` | none | returns latch ("divide" second read) | `rdTrig = 0` |
| `IN #11` | — | none | returns latch | all 0 |
| `OUT #11` | — | none | latch high ← value | `wrHiTrig = 1`, others 0 |
| `OUT #10` | `wrHiTrig = 1` | 16-bit write `{latchHi, value}` (Nemo order) | — | all 0 |
| `OUT #10` | `wrHiTrig = 0`, `wrLoTrig = 0` | none | latch low ← value ("divide" first write) | `wrLoTrig = 1` |
| `OUT #10` | `wrLoTrig = 1` | 16-bit write `{value, latchLo}` | — | all 0 |
| any other IDE port | — | register access | — | all 0 |

Worked example (divide read with `INIR`, B = 0, C = `#10`): 512 `IN`s alternate real read / latch, so
256 words arrive low byte first — the whole sector in one `INIR`.

Worked example (Nemo write): `OUT (#11),#AB : OUT (#10),#CD` → the drive receives `#ABCD`, and the
image bytes are `CD AB`.

### 3.3 Adapter and media

`IdeAdapterNemo` with options `{latch = EvoNemoLatch, cs1Port = #C8, aliases = true, gate = always,
intrq = not visible}` (IDE design §4.2 "per-machine differences"). Scheme `NEMO-DIVIDE` (existing enum,
already in `data/configs/atm3/unreal.ini`) selects it on ATM3; `IDE_SCHEME` validation accepts
NEMO/NEMO-A8/NEMO-DIVIDE on ATM3 and rejects the others with a warning (IDE design §8.1).
Media, geometry, write-through / write-protect, host folders and `[HDD]` keys are exactly the IDE
design's (§6-§8). IDE reset = machine reset (RTL `top.v:267`).

## 4. ATAPI CD-ROM (ST-4)

Nothing Evo-specific: `AtapiCdrom` + `IsoImage` in slot 1 (IDE design §7.5). The ERS CD boot is the
first **concrete guest software** for the IDE design's open question Q9. What it exercises
(`pentevo/rom/mainmenu/src/hdd_cd_boot.a80:118-360`): device on the slave (`#D0` = `#B0`), `#08`
DEVICE RESET, `#EC` aborted with signature `#EB14` in `#90/#B0`, PACKET `#A0` with READ TOC and
READ(10), ISO 9660 volume descriptor at LBA 16, root directory scan for `AUTORUN.ZX`, load to
`#6000`, entry with `A=#B0`, `C=0` (Nemo).

## 5. TTD and snapshots

Rollout 1 rule of the IDE design §10.0, extended to the SD card: while TTD records, the **first SD
command** (a CMD byte after CS goes low) or the **first IDE command register write** invalidates the
recording with the reason "storage activity: not covered by TTD yet". Controller state (latch flags,
`ZControllerSpi`, `SdCardSpi` protocol state) is POD from day one so rollout 2 can register it.
Snapshots do not carry storage state (all reference emulators agree).

## 6. Automation

No new verbs: ATM3 exposes the shared ones.

| Surface | Verbs | Owner |
|---|---|---|
| SD | `sd insert <image\|folder> [--write session\|persist\|off] [--wp]`, `sd eject`, `sd info`, `sd export <path>` | TSConf API-1 (one `SdCardState`) |
| HDD / CD | `hdd attach/detach/info/export/create/regs/sector`, `cd insert/eject` | IDE design §8.2 |
| Notifications | `NC_HDD_STATE_CHANGED`, the SD equivalent, activity LEDs | same |
| Recipes | `.recipe/machines/atm.md`: "boot NedoOS from a folder", "mount a TRD through the ERS" | T-2 |

## 7. Tests

Component tests belong to the owners (IDE L1-L3, NeoGS SD, TSConf VFAT) and are **not** repeated.
ATM3 adds:

| ID | Asserts |
|---|---|
| ZC-1 | `#77` read = `#00` outside shadow; `#77` in shadow is the ATM port, not SD |
| ZC-2 | `#57` write/read pipeline: the read returns the previous exchange's response and sends `#FF`; deselected card → `#FF` |
| ZC-3 | shadow: `OUT (#8057),#01` selects, `OUT (#0057),v` sends data |
| ZC-4 | `SDWriteProtect=1` → AVR register C bit 2 = 1; no card → bit 3 = 0 |
| ZC-5 | `ZControllerSpi` state round-trips through `SaveState`/`LoadState` mid-command |
| NIDE-1 | truth table of §3.1 through the shared L2 fixture (IDE design §12.2), both shadow states |
| NIDE-2 | every row of §3.2, including the "other port resets the flags" rows and `INIR` of a whole sector |
| NIDE-3 | Nemo order and divide order write the same image bytes (`CD AB` for `#ABCD`) |
| NIDE-4 | alias `#08` reaches register 0; `#18`, `#38` are not IDE ports |
| NIDE-5 | machine reset hard-resets the drives and clears the latch flags |
| ST-TTD-1 | first SD command / IDE command while recording invalidates the recording; idle attached media do not |
| **ERS-SD-1** | real ROM: SD image with `SD_BOOT.$C` → ERS "5. SDcard boot" reaches the program's known screen |
| **ERS-SD-2** | real ROM: the same from a **host folder** (S2) |
| **ERS-HDD-1** | real ROM: HDD image with a boot block at LBA 2 → ERS "B. HDD boot" runs it at `#6000` |
| **ERS-CD-1** | real ROM: ISO with `AUTORUN.ZX` on the slave → ERS "D. CD boot" runs it with `A = #B0` |
| **ERS-MNT-3** | real ROM: "Mount A:" of a TRD from a FAT partition on the HDD image; TR-DOS `CAT` lists it (with the virtual TR-DOS design) |
| **NOS-SD-1** | NedoOS Evo build (`sd_boot.$C`) boots from an SD image to the shell (screen text), and from a host folder |
| **NOS-HDD-1** | NedoOS on a Nemo HDD partition lists its files |

## 8. Implementation order

1. `ZControllerSpi` + `SdCardSpi` on `IBlockDevice` (S1, S3, S4): needs `SdCardSpi` on master (NeoGS
   merge, or lift it unchanged from the `neogs` branch, as TSConf plans).
2. `HostFolderFat` (S2): built once, by whichever of IDE rollout-1 phase R1-6 / TSConf phase 6 / this
   plan comes first.
3. `IdeAdapterNemo` + `EvoNemoLatch` (S5): after IDE R1-1 (disk core). Part of IDE R1-4 "other boards".
4. ATAPI: IDE R1-7; ATM3 only adds ERS-CD-1.
