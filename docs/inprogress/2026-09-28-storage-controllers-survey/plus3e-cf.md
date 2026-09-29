# +3e with 8-bit IDE, ZXATASP and ZXCF (and the +3e over SD)

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Machine** | Spectrum +2A / +3 (model `PLUS3` / `PLUS2A`, creatable) running Garry Lancaster's **+3e** ROMs, with one of: simple 8-bit IDE, ZXATASP, ZXCF, DivIDE, or an SD interface |
| **unreal-ng now** | the +3 machine and its uPD765 floppy; no IDE board on the +3 (`Scheme=NONE`), no +3e ROMs shipped |
| **Effort** | +3e over **Z-Controller SD**: **S**. Simple 8-bit IDE: **S** (ports to verify). ZXCF: **M**. ZXATASP: **M**. +3e with DivIDE: comes with [divide-divmmc-esxdos.md](divide-divmmc-esxdos.md) |

## 1. What the +3e is

The +3e is a replacement ROM set (64 KB, the same four 16 KB pages as the +3 ROM) that adds hard
disk and card support to +3 BASIC and +3DOS ("IDEDOS"). A disk holds a "PLUSIDEDOS" partition table
and +3DOS partitions; the +3e can also map a +3 floppy drive letter onto a disk file. **The interface
is chosen when the ROM is built**: there is one ROM image per interface, and the references show no
runtime probe between interfaces.

| Fact | Source |
|---|---|
| MAME has four +3e machines: `specpl3e` (no IDE), `sp3e8bit` (8-bit IDE), `sp3eata` (ZXATASP), `sp3ezcf` (ZXCF), each with its own 64 KB ROM (`3e8biten.rom`, `3ezxaen.rom`, `3ezcfen.rom`, ...) | MAME `src/mame/sinclair/specpls3.cpp:493-533` |
| MAME fits **no** IDE / 8255 / ZXCF device to them (plain `spectrum_plus3` config): ROM sets only | `specpls3.cpp:429-436` |
| ZXCF and ZXATASP page in their own memory when the CPU fetches the NMI entry `#0066` | Spectral `src/res/roms/gw03v33.txt` (note on the M.G.T. interfaces, ZXCF and ZXATASP) |
| A +3e build that talks to an SD card through the **Z-Controller ports** `#57` / `#77`: "128 +3e", "2000-2015 Garry Lancaster v1.4", "PLUSIDEDOS", "MMC unit"; code does `LD C,#57 : INI` | karabas-pro `firmware/src/fpga/profi_plus3e/rom/plus3en40mmc.rom` (64 KB; byte scan) |
| The matching Karabas-Pro "+3e" FPGA build decodes Z-Controller `#57` / `#77` and +3 `#1FFD` | karabas-pro `firmware/src/fpga/profi_plus3e/rtl/karabas_pro.vhd:1311, 1535-1537` |

(Paths are in the local reference tree, `emulators/github/...`.)

## 2. The interfaces

No local reference implements ZXATASP, ZXCF or the simple 8-bit IDE hardware (Fuse does, and Fuse
is not in the local tree). The port facts below are from Fuse's `peripherals/ide/zxatasp.c`,
`zxcf.c`, `simpleide.c` as remembered, and are **unverified** until checked against a Fuse checkout.

| Interface | Ports | Data path | Memory | Status |
|---|---|---|---|---|
| **Simple 8-bit IDE** | register number from high address bits (exact decode **unverified**) | 8 bits: only the low byte of each 16-bit word reaches the Z80; the high byte is lost, so a disk holds half its capacity | none | **unverified** |
| **ZXCF** | IDE registers at `#xxBF`, register number in A8..A10 (`#00BF` data ... `#07BF` status/command); memory control `#10BF` | the CF card is switched to **8-bit mode** (SET FEATURES `#01`), so the full byte stream arrives through one port | up to 1 MB (64 x 16 KB) paged into `#0000-#3FFF` by `#10BF`: bit 7 memory off, bit 6 write enable, bits 5..0 bank | **unverified** |
| **ZXATASP** | an 8255 PPI: `#009F` port A, `#019F` port B, `#029F` port C, `#039F` control (A8..A9 pick the register) | 16 bits: ports A and B carry the IDE data low and high; port C drives the IDE address lines, CS0 / CS1, /RD, /WR and reset (the Z80 "bit-bangs" each IDE cycle) | 128 KB or 512 KB paged into `#0000-#3FFF` (bank from port C in the RAM mode), Upload and write-protect switches | **unverified** |
| DivIDE | [divide-divmmc-esxdos.md](divide-divmmc-esxdos.md) §2 | toggle on `#A3` | `#E3` | verified (zxsp, pico-spec) |
| Z-Controller SD | `#57` / `#77` (Karabas +3e build) | SPI | none | verified (ROM byte scan + RTL) |

**Worked example: why an "8-bit" disk is half full.** On the simple 8-bit interface a 512-byte
sector is 256 words; the Z80 reads 256 bytes (the low halves) and the high halves are dropped. A
10 MB drive therefore stores 5 MB. RS-IDE `.hdf` images mark this with the "halved" flag (header
byte 8 bit 0): the file holds only the 256 useful bytes per sector. `HddImageFormats` already reads
that flag and `RawImage` expands such sectors, so these images open today.

**Worked example: ZXCF 8-bit mode.** The +3e sends `OUT (#01BF),#01` (features = 1), then
`OUT (#07BF),#EF` (SET FEATURES). From then on each `IN (#00BF)` returns the next byte of the sector,
and a sector is 512 `IN`s. A drive model that ignores feature 1 returns only low bytes and the +3e
reads garbage.

## 3. unreal-ng now vs gap

| Piece | Have (`ide-atapi` unless noted) | Need |
|---|---|---|
| Disk, formats, slots, TTD | `AtaDisk`, `IdeUnitSlot`, `HddImageFormats` incl. HDF "halved" | none |
| 8-bit transfer mode | `AtaDisk` accepts SET FEATURES but ignores the feature code (`atadisk.cpp:291`) | **new**: features `#01` / `#81` (8-bit on / off) in `AtaDevice`'s data engine: one byte per data-register access |
| Simple 8-bit IDE decode | - | a scheme `PLUS3E-8BIT`: register decode, low byte only |
| ZXCF | - | scheme `ZXCF` (decode + 8-bit mode) and a **memory board**: 1 MB of 16 KB pages in `#0000-#3FFF` (16 KB granularity: fits today's `Memory` windows, unlike DivIDE) plus the `#0066` trap |
| ZXATASP | - | an **8255 PPI** model (mode 0 ports A/B/C, the control word, bit set / reset), the IDE cycle driven from port C, the RAM board |
| +3e over Z-Controller | `ZControllerSpi`, `SdCardSpi`, slot `sd.zc` (master) - today only the ZX-Evo decoder registers them | the `PLUS3` / `PLUS2A` decoder fits a Z-Controller **add-on** (`#57` / `#77` arms + `sd.zc` slot, the add-on registration of the storage manager, G3) |
| +3e disk layout from a folder | `HostFolderFat` builds FAT | a PLUSIDEDOS / +3DOS partition builder is **not** planned; images only |
| ROMs | none shipped | `plus3en40mmc.rom` (karabas-pro tree) for the SD variant; MAME's `3e*.rom` set is not on disk |

## 4. Software to test with

| Item | Where |
|---|---|
| +3e v1.4x for Z-Controller SD | karabas-pro `firmware/src/fpga/profi_plus3e/rom/plus3en40mmc.rom` |
| +3e 8-bit IDE / ZXATASP / ZXCF ROMs | MAME ROM names `3e8biten.rom`, `3ezxaen.rom`, `3ezcfen.rom` (not on disk; from the +3e distribution) |
| Stock +3 ROMs | `data/rom/plus3.rom`, `data/rom/plus341.rom` |
| Disk images | none on disk; the +3e formats a blank image itself (`FORMAT TO 0,...`) |

## 5. Acceptance test ideas

1. **+3e SD** (cheapest proof): `PLUS3` model, ROM `plus3en40mmc.rom`, Z-Controller add-on, a blank
   64 MB card image; the +3e's own commands initialize the card (`FORMAT TO ...`) and create a +3DOS
   partition (`NEW ...`, exact syntax from the +3e manual); `CAT TAB` lists the partition (screen
   text). Re-insert the card: the partition is still there.
2. **8-bit mode unit test**: after SET FEATURES `#01`, 512 data reads return the 512 image bytes in
   order; after `#81` word mode is back.
3. **ZXCF**: the `3ezcfen.rom` +3e formats and lists a CF image; `#10BF` bank writes are visible at
   `#0000`.
4. **ZXATASP**: PPI control-word table (mode set, bit set / reset), then the same +3e format test.

## 6. TTD impact

The Z-Controller add-on reuses `EvoSdCard`-style state (a second registration on another model).
ZXCF / ZXATASP add a paging blob (bank register, PPI registers) and their RAM (128 KB-1 MB): a TTD v2
memory region, or the whole RAM in the blob before V1. 8-bit mode is one more byte in
`AtaDeviceState` (its layout test catches it).

## 7. Order

1. +3e over Z-Controller (S): proves the +3e itself with pieces that exist.
2. 8-bit transfer mode in the disk core (S): needed by ZXCF and the simple 8-bit interface.
3. Simple 8-bit IDE (S), after verifying its decode against Fuse.
4. ZXCF (M), then ZXATASP (M): only if there is demand; both need their memory boards.
