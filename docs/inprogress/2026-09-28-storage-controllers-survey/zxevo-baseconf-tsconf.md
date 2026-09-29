# ZX-Evo: BaseConf and TSConf storage (NemoIDE, ATAPI, Z-Controller SD, TSConf DMA)

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Machines** | ZX-Evo with the BaseConf FPGA (model `ATM3`, creatable) and with TSConf (model `TSL`, not creatable yet, PLAN #41) |
| **Designs** | [tdd-storage-sd-ide-cd.md](../2026-09-15-atm-baseconf-highres-ports/tdd-storage-sd-ide-cd.md) (SD §2, NemoIDE §3, ATAPI §4); [baseconf-hardware-reference.md](../2026-09-15-atm-baseconf-highres-ports/baseconf-hardware-reference.md) §A.9, §A.13; TSConf [hardware-spec.md](../2026-09-27-tsconf/hardware-spec.md) §6.2, §8.1, §8.3 and [technical-design.md](../2026-09-27-tsconf/technical-design.md) §3.11; [integration-zxevo-sd.md](../2026-09-28-storage-manager/integration-zxevo-sd.md), [integration-tsconf-sd.md](../2026-09-28-storage-manager/integration-tsconf-sd.md) |
| **Effort** | BaseConf: **done** (SD and NemoIDE + ATAPI on master). TSConf SD: **S** inside TSConf phase 6. TSConf IDE: scheme `NEMO-DIVIDE` set and the DMA word API ready (2026-09-29); left **S** (decoder `TryIdePortIn/Out` + DMA codes `#3`/`#B` hook-up) |

## 1. BaseConf

### 1.1 Hardware

| Device | Ports and rules | Source (FPGA RTL) |
|---|---|---|
| **NemoIDE** | `#10` data, `#11` high-byte latch, `#30`-`#F0` registers 1-7 (A7..A5), `#C8` alternate status / device control (CS1); RTL aliases `#08 #28 ... #E8`; answers in and out of shadow, never gated | `fpga/base_trdemu/trunk/z80/zports.v:544-654` ([baseconf-hardware-reference.md](../2026-09-15-atm-baseconf-highres-ports/baseconf-hardware-reference.md) §A.9) |
| NemoIDE latch | the "Evo combined latch": `OUT #11` arms a Nemo-order write (high first); two `IN #10` / two `OUT #10` form a DivIDE-order pair (low first); any other IDE port resets both | `zports.v:554-654` ([tdd-storage-sd-ide-cd.md](../2026-09-15-atm-baseconf-highres-ports/tdd-storage-sd-ide-cd.md) §3.2) |
| IDE reset / INTRQ | IDE /RESET = system reset; INTRQ not visible to the CPU | `top.v:267` |
| **Z-Controller SD** | `#77` write: D1 = /CS; read `#00` outside shadow. `#57`: write sends a byte; read returns the byte of the **previous** exchange and starts a new one sending `#FF`. In shadow `#8057` (A15 = 1) is the chip select (NedoOS) | `zports.v:812-836`; [tdd-storage-sd-ide-cd.md](../2026-09-15-atm-baseconf-highres-ports/tdd-storage-sd-ide-cd.md) §2.1 |
| Card detect / WP | not in the ports: AVR register C bits 3 / 2 (Gluk CMOS extension) | [e5-sd-card.md](../2026-09-15-atm-baseconf-highres-ports/e5-sd-card.md) |

**Worked example: a DivIDE-order sector read on the Evo.** `LD BC,#0010 : INIR : INIR` (B = 0, so
each `INIR` moves 256 bytes). The first `IN #10` performs the real 16-bit read and returns the low
byte; the second returns the latched high byte; and so on. Two `INIR`s move a whole 512-byte sector,
low byte first, with no port change. The same drive read the Nemo way (`IN A,(#10) : IN A,(#11)` per
word) gives the same bytes.

### 1.2 unreal-ng now

| Piece | Where | State |
|---|---|---|
| SD card | `SdCardSpi` + `ZControllerSpi` + slot `sd.zc` (card detect, WP switch, folder via `HostFolderFat`) | **master** (E5, E5b = media manager M1). ERS boots `SD_BOOT.$C`; NedoOS boots from an image and from a folder |
| NemoIDE | `IdeAdapter` scheme `NEMO-DIVIDE` (`EvoIn` / `EvoOut`), slots `ide0.master` / `ide0.slave` | **master** (`f5fc5f05`). ERS "HDD boot" and "CD boot" on the real ROM (`zxevo_ers_test`) |
| ATAPI CD | `AtapiCdrom` + ISO format, a CD unit by `CD1=1` or an `.iso` | **master** (`f5fc5f05`) |
| TTD | `EvoSdCard = 15` (Z-Controller + card protocol), `AtaChannel = 17`; guest writes are barriers | master |

Gap: none for the hardware. Open items belong to other rows: the shipped config first had no CD
unit (an empty ATAPI drive changes the ERS boot); since `087b9ec7` it ships
the CD drive on the IDE slave (`CD1=1`), and the ERS "D. CD boot" reports no medium and retries
until a disc is inserted (`ZXEvoErs_Test.CdBootSeesTheDiscEjectedAndInsertedAgain`); NedoOS from a Nemo HDD image (NOS-HDD-1)
is a test still to add.

## 2. TSConf

### 2.1 Hardware

| Device | Ports and rules | Source |
|---|---|---|
| SD | same Z-Controller `#57` / `#77` as BaseConf; `#77` also carries FT812 (bit 2), SD2 (bit 3, build option `SD_CARD2`, off) and ESP (bit 4) selects; `#77` read = `#00`; decoded in every mode | [hardware-spec.md](../2026-09-27-tsconf/hardware-spec.md) §8.1 ([V] `zports.v:296-302, 460-465, 689-717`) |
| SD **DMA** | `DMACTRL` (`#27AF`) = `#02` (SPI → RAM) / `#82` (RAM → SPI) (bit 7 = direction, bits 2..0 = device 2; the spec's 4-bit codes `#2` / `#A`), little-endian, two SPI bytes per word, about 8 DRAM slots per word; shares the SPI master with the CPU | [hardware-spec.md](../2026-09-27-tsconf/hardware-spec.md) §6.2 ([V] `dma.v:223-227`) |
| NemoIDE | built in the standard `quartus` build, same ports as BaseConf | [hardware-spec.md](../2026-09-27-tsconf/hardware-spec.md) §8.3 ([V] `zports.v:256-274, 766-861`) |
| IDE **DMA** | `DMACTRL` = `#03` (IDE → RAM) / `#83` (RAM → IDE) (codes `#3` / `#B`), device-paced; not built without `IDE_HDD` (then the DMA hangs: busy stays 1) | [hardware-spec.md](../2026-09-27-tsconf/hardware-spec.md) §6.2 |
| Boot | TS-BIOS lists SD, IDE Nemo and IDE SMUC as boot devices; it looks for `BOOT.$C` on a **FAT32** volume; SD reads use CMD18 + DMA `#02` (256 words), writes CMD25 + DMA `#82`; it **refuses writes when `IN (#77)` bit 1 = 1**; its IDE path sends ATAPI DEVICE RESET (`#08`) first, which an ATA disk must abort harmlessly | TS-BIOS `pentevo/rom/src/tsfat.asm:184-185, 1071-1205, 1392-1466, 1586-1726`, `booter.asm:1636` (zx-evo tree) |
| Emulators | zx-evo-unreal: every DMA mode, SPI DMA two Z-Controller exchanges per word, low byte first, IDE DMA one word per free DRAM slot (`tsconf.cpp:106-140, 330-420, 525-541`); MAME `tsconf`: no RAM → SPI and no IDE DMA (`tsconf_dma.cpp:26-31`), and its `#77` read value (`tsconf_m.cpp:855-864`) makes TS-BIOS refuse writes; Xpeccy: `#02/#82/#03/#83` (`hardware/tslab.c:388-420`) | reference tree |

**Worked example: a DMA sector read.** The driver selects the card, sends CMD17, waits for the data
token `#FE` by CPU `IN (#57)`, then starts DMA `#2` with `DMA_LEN` = 255 (256 words) and
`DMA_NUM` = 0 (one block): the engine clocks 512 bytes from `#57` into RAM, two bytes per word, low
byte first. The CPU then reads the two CRC bytes itself.

### 2.2 unreal-ng now vs gap

| Piece | Reusable as is | New |
|---|---|---|
| SD | `ZControllerSpi`, `SdCardSpi`, slot `sd.zc` (same id as BaseConf, so a model switch keeps the card), `HostFolderFat` (the TSConf design's `VirtualFatBlockStore`), `EvoAvr` card-detect / WP | the TSConf decoder arm for `#57`/`#77` (the ATM3 one without the shadow `#8057` rule; `#77` read must stay `#00`, the FPGA value, `zx-evo-tsconf .../z80/zports.v:460-465`), the DMA `#2`/`#A` path: a loop over `ZControllerSpi::WriteData` / `ReadData` inside `TsConfDma`, budgeted in DRAM slots |
| IDE | `IdeAdapter` scheme `NEMO-DIVIDE` unchanged (same RTL family) | done 2026-09-29: `IdeController::SchemeFits(IDE_NEMO_DIVIDE, MM_TSL)` is true (tested) and the shipped `ts-conf` config has `[HDD] Scheme=NEMO-DIVIDE`; `IdeAdapter::DmaReadWord` / `DmaWriteWord` move one whole word from / to the data register past the Z80 latches. Left: the decoder calls `TryIdePortIn/Out` first, and DMA `#3`/`#B` call `GetIdeAdapter().DmaReadWord/DmaWriteWord` per word, paced by DRQ |
| TTD | `EvoSdCard`, `AtaChannel` blobs | DMA state is TSConf's own blob (id 16); a DMA write to the card or disk is a barrier through the same `NoteWrite` |

### 2.3 Software to test with

| Software | Where | Proves |
|---|---|---|
| TS-BIOS | `data/rom/ts-bios.rom`, `ts-bios-gluk.rom`, `ts-bios-qc311.rom`, `ts-bios-rc196.rom` | boot from SD image and folder; IDE boot menu |
| NedoOS, Wild Commander | NedoOS SD tree under `testdata/machines/zxevo/nedoos/` (Evo build); raw images `NedoOS/tools/vhd/sd_nedo.vhd.xz`, `hdd_nedo.vhd.xz`; Wild Commander `zx-evo/pentevo/soft/WC/wc.zip` (reference tree; no prebuilt `wc.img` exists, the kit builds a 100 MB one) | FAT32 through DMA `#02`; NemoIDE |
| MAME `tsconf`, Xpeccy, UnrealSpeccy TS | references | differential: DMA timing, `#77` read value |

### 2.4 Acceptance test ideas

1. DMA `#2` reads one sector into RAM, byte-exact with `SdCardSpi::readBlock`, in the budgeted
   number of lines.
2. DMA `#A` writes one sector; `blocksWritten()` grows by one and one TTD barrier is recorded.
3. TS-BIOS boots NedoOS / WC from `sd.zc` holding a folder; a BaseConf → TSConf model switch keeps
   the card and its session writes.
4. NemoIDE on TSConf: the BaseConf truth table (`ideadapter_test`, scheme `NEMO-DIVIDE`) reused on
   the `TSL` decoder; DMA `#3` reads a sector equal to eight `INIR`s.

### 2.5 Order

TSConf phases up to the decoder (PLAN #41) → SD arm (hours) → DMA SPI (with the DMA engine) →
NemoIDE arm (hours; `ide-atapi` is on master, scheme and DMA word API ready) → DMA IDE.
