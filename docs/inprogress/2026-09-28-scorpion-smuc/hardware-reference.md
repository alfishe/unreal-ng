# SMUC hardware reference: sub-devices, ports, bits, and what the emulators do

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Scope** | The SMUC card as the Scorpion software sees it: every sub-device, the full port map with decode masks, the gating, the `#FFBA` / `#7FBA` bits, and the answer to IDE design question Q3 (reset polarity) |
| **Method** | Every hardware fact is tabulated across the references below and the consensus is taken. Where the emulators split evenly, the ProfROM 4.01 firmware itself (disassembled for this document) breaks the tie |

## 1. What SMUC is, in one paragraph

SMUC is an add-on card for the Scorpion ZS-256 (the name is expanded as "Scorpion & MOA Universal
Controller" in the Black Cat ports guide, and "Spectrum Multi Unit Controller" in ZXMAK2). It sits
on the Scorpion expansion bus and adds PC-style parts to the machine: an **IDE hard-disk port**
(two drives, master and slave), a **battery-backed clock** (Dallas DS1685), a **2 KB settings
memory** (a 24C16 serial EEPROM), an optional **interrupt controller** (Intel 8259), an **8-bit ISA
slot window**, and a small **"virtual floppy" register** that the ProfROM firmware uses to replace
floppy drives A and B with disk images stored on the hard disk. The card only answers while the
TR-DOS ports are switched on, so ordinary programs reach it by calling into the TR-DOS ROM
(the `#3D2F` trick, §4).

## 2. References

Paths are relative to each project's own checkout (the reference emulators are not part of this
repository; their files are named, not linked).

| Short name | What | Files |
|---|---|---|
| **Unreal** | UnrealSpeccy 0.39 (GitHub mirror) | `unreal-speccy/io.cpp` |
| **Unreal-nedopc** | UnrealSpeccy 0.39.0, NedoPC fork (pentevo SVN) | `tools/unreal_fix/0.39.0/nedopc/io.cpp`, `memory.cpp`, `config.cpp` |
| **Xpeccy** | Xpeccy | `src/libxpeccy/hdd.c`, `src/libxpeccy/hardware/common.c`, `src/xcore/profiles.cpp` |
| **MAME** | MAME ZX-BUS SMUC card and the Scorpion driver | `src/devices/bus/spectrum/zxbus/smuc.cpp`, `src/mame/sinclair/scorpion.cpp`, `src/devices/bus/spectrum/zxbus/bus.cpp` |
| **ZXMAK2** | ZXMAK2 | `src/ZXMAK2.Hardware/General/IdeSmuc.cs`, `src/ZXMAK2.Hardware.Circuits/Ata/AtaPort.cs`, `src/ZXMAK2/machines.config` |
| **Black Cat** | "BC Info Guide #4", ZX Spectrum ports guide (2008), in this repository | [zx-ports-full-table.txt](../../ports/zx-ports-full-table.txt) lines 92-104; prose in [ports.md](../../ports/ports.md) section SMUC |
| **ProfROM** | ProfROM 4.01, in this repository | [scorp_prof401.rom](../../../data/rom/scorp_prof401.rom); "page N" = file offset `N × #4000`; page 3 = TR-DOS slot of plane 0, page 7 = TR-DOS slot of plane 1 (the SMUC driver); pages 19 / 23 are duplicates |
| **savelij ROM** | the pentevo multi-machine ROM (ZX-Evo), SMUC driver and port constants | `rom/ports_ide.a80`, `rom/fat_boot/source/drivers/drv_smuc.a80`, `rom/mainmenu/src/hdd_cd_boot.a80` |
| **LW ProfROM** | ProfROM 4.xx builds by LW (SMUC and Nemo variants) and their change log | `Scorpion256TPlus/ROM/LW/README.md`, `Scorpion256TPlus/ROM/LW/changes.md` |
| **NedoOS** | Mr Rabbit client, SMUC clock driver | `src/mrabbit-fusion/drivers/rtc-smuc.asm` |

Earlier analysis in this repository: [profrom-smuc-not-found-and-driver-disassembly.md](../2026-09-07-scorpion-zs256-clone/profrom-smuc-not-found-and-driver-disassembly.md)
(the serial-link driver, page 7 `#0D51`-`#0F55`), [IDE design](../2026-09-21-profi/2026-09-25-ide-hdd-design.md) §4 (SMUC row) and Q3.

## 3. Sub-devices

| Sub-device | Chip (consensus) | What the firmware does with it | Evidence |
|---|---|---|---|
| IDE bridge | 16-bit ATA bus split into two 8-bit halves by a latch | ProfROM identifies the drive, reads the partition table, mounts disk images, boots | MAME `smuc.cpp:61-82`; Unreal-nedopc `io.cpp:261-276`, `1006-1020`; ZXMAK2 `IdeSmuc.cs:246-274` |
| Clock / CMOS | Dallas **DS1685** (MC146818-compatible, 32.768 kHz) | ProfROM menu clock; the RTC periodic interrupt drives the 8259 test | MAME `smuc.cpp:114` (`DS1685`); Black Cat line 102 ("DS1685RTC"); ZXMAK2 uses a DS12885 model (`IdeSmuc.cs:25`) |
| Settings memory | **24C16**, 2 KB serial (I²C) EEPROM, write-protect pin | ProfROM keeps its configuration there, checksummed at `#00FE/#00FF` | MAME `smuc.cpp:115` (`I2C_24C16`); UnrealSpeccy docs "SMUC RTC and NVRAM (24LC16)"; Unreal-nedopc `memory.cpp:488-540` (2 KB, `& 0x7FF`); Xpeccy keeps 256 bytes of it in the profile (`profiles.cpp:34-43`) |
| Interrupt controller | Intel **8259**, optional ("absent in 2.0", MAME) | ProfROM probes the mask register; if found, tests RTC → 8259 → Z80 IM 2 interrupts | MAME `smuc.cpp:93-94`; ProfROM page 7 `#1572-#15C6`, `#16CD-#1737` (§5.4) |
| Virtual FDD register | one latch, `#7FBA` | the ProfROM TR-DOS asks it whether drive A or B is a disk image on the hard disk | ProfROM page 3 `#0853-#0870`, `#08A7`, `#0904`, `#0A01`, `#0A31`, `#0A76` (§5.3) |
| Version / revision | two read-only constants | ProfROM decodes a 3-bit number from each (§5.5) | ProfROM page 7 `#242A-#2452` |
| ISA window | 8-bit ISA I/O `#200-#3FF` | ProfROM resets and probes an ISA card at `#7AFE` | Black Cat line 94; ProfROM page 7 `#15C7-#160D` |

## 4. Gating: the card answers only with the TR-DOS ports on

On the Scorpion the Beta 128 disk ports (`#1F`, `#3F`, `#5F`, `#7F`, `#FF`) are switched on only
while the TR-DOS ROM is paged in. SMUC uses the same "DOS" signal. Every reference agrees:

| Reference | Gate | Evidence |
|---|---|---|
| Unreal | inside `if (comp.flags & CF_DOSPORTS)` (read and write) | `io.cpp:161` (write block), `io.cpp:813` (read block); SMUC at `io.cpp:187-229`, `840-866` |
| Unreal-nedopc | same | `io.cpp:211`, `io.cpp:967`; SMUC at `io.cpp:235-276`, `995-1020`; for Scorpion `CF_DOSPORTS` follows `CF_TRDOS` (`memory.cpp:290-297`) |
| Xpeccy | `iorq` only when `dosen` (`comp->flgBDI`) | `hdd.c:719-731` (`ide_smuc_decode`), `hdd.c:968-981`; called with `comp->flgBDI` from `hardware/common.c:215`, `222` |
| MAME | the card maps into the Scorpion's shadow (DOS) I/O view | `smuc.cpp:84` (`shadow_io_map`); `scorpion.cpp:241` (`m_io_shadow_view.select(dos() ? 1 : 0)`), `scorpion.cpp:356` |
| ZXMAK2 | every handler returns unless `m_memory.DOSEN` | `IdeSmuc.cs:175`, `183`, `191`, `202`, `212`, `221`, `233`, `248`, ... |
| Software | the savelij SMUC driver and the NedoOS clock driver do every `IN`/`OUT` by jumping to TR-DOS `#3D2F` with return address `#3FF3` (`IN A,(C) : RET`) or `#3FF0` / `#2A53` (`OUT (C),A : RET`) | `drv_smuc.a80` (`SOUTPRT`, `SINPRT`); `rtc-smuc.asm` (`in3d2f`, `out3d2f`) |
| ProfROM | its SMUC driver lives in page 7 and its virtual-FDD checks in page 3, both TR-DOS slots: the code runs with the TR-DOS ports on | page offsets in §3 |

**Consensus: gated by the TR-DOS ports.** Worked example: a BASIC program that does
`OUT 65466,0` (`#FFBA`) outside TR-DOS reaches the ULA, not SMUC. The same `OUT` executed from the
TR-DOS ROM (through `#3D2F`) reaches the SMUC system register.

## 5. Port map

### 5.1 Decode

The Black Cat patterns (bit strings A15 … A0, `x` = not decoded):

| Port | Pattern | Sub-device | Read | Write |
|---|---|---|---|---|
| `#5FBA` | `0x011xxx101xx010` | version | yes | - |
| `#5FBE` | `0x011xxx101xx110` | revision | yes | - |
| `#7FBA` | `0x111xxx101xx010` | virtual FDD latch | yes | yes |
| `#7EBE` / `#7FBE` | `0x111xxN101xx110` | 8259 (A8 = the 8259's A0) | yes | yes |
| `#DFBA` | `1x011xxx101xx010` | DS1685 (address / data, see `#FFBA` D7) | yes | yes |
| `#D8BE` | `1x011xxx101xx110` | IDE data high byte latch | yes | yes |
| `#FFBA` | `1x111xxx101xx010` | system register | yes | yes |
| `#F8BE-#FFBE` | `1x111CBA101xx110` | IDE registers (A10-A8 = register 0-7) | yes | yes |
| `#18E6-#7FFE` | `0ED11CBA111GF110` | ISA I/O `#200-#3FF` | yes | yes |

Reading the table: the fixed bits are **A12 = A11 = 1, A7 = 1, A6 = 0, A5 = 1, A1 = 1, A0 = 0**; three
bits pick the sub-device: **A15, A13, A2**; A14, A10-A8 (except where they select a register),
A4 and A3 are not decoded. As one mask:

| | Mask | Match (for `#FFBA`) |
|---|---|---|
| Family (fixed bits) | `#18E3` | `#18A2` |
| Family + sub-device select | `#B8E7` | `#B8A2` |

| Reference | Decoded bits | Agrees with Black Cat? |
|---|---|---|
| MAME | `map(0xb8a2, 0xb8a2).mirror(0x4718)` etc.: decodes `~#4718` = A15, A13, A12, A11, A7, A6, A5, A2, A1, A0 | yes (`smuc.cpp:86-103`) |
| ZXMAK2 | `mask = 0xB8E7` | yes (`IdeSmuc.cs:67-81`) |
| Unreal / Unreal-nedopc | family `(port & 0x18A3) == 0x18A2`, then sub-device `(port & 0xA044)`: together A15, A13, A12, A11, A7, A6, A5, A2, A1, A0 | yes (`io.cpp:840-866`) |
| Xpeccy | family `0x18A3`, then an **exact 16-bit** port compare for the sub-devices and `(port & 0xF8FF) == 0xF8BE` for the IDE window | stricter (`hdd.c:719-731`, `733-761`) |
| unreal-ng (master, IDE rollout 1) | family `0x18FB` / `0x18BA` (the whole low byte) plus `(port & 0xA044)` | stricter: also decodes A4 and A3 (see [current-state-and-gaps.md](current-state-and-gaps.md) G2) |

**Consensus: mask `#B8E7`.** Worked example: `#FEFE` (keyboard row Caps-V) & `#B8E7` = `#B8E6`. The
eight sub-device matches are `#18A2` + one of `#0000`, `#0004`, `#2000`, `#2004`, `#8000`, `#8004`,
`#A000`, `#A004`: `#B8E6` is none of them, because A6 = 1 in every `#xxFE` port. The A6 bit is
what keeps the keyboard safe; decoding the whole low byte (unreal-ng's fix for the 2026-09-10
keyboard regression) is not needed once A6 is in the mask.

### 5.2 `#FFBA` system register

Write bits. The firmware evidence is ProfROM page 7; the power-on value it writes is `#F7`
(`#0D00-#0D07`), and the savelij ROM uses `#F7` / `#77` to open and close the control block
(`hdd_cd_boot.a80:173-187`: `hdduprON=0XFFBA`, `hddupr1=0XF7`, `hddupr0=0X77`).

| Bit | Role | Unreal | Xpeccy | MAME | ZXMAK2 | ProfROM evidence | Consensus |
|---|---|---|---|---|---|---|---|
| D7 | 1 = the IDE **control block**: `#FEBE` becomes device control / alternate status; 1 = `#DFBA` write is **data**, 0 = **address** | both | RTC only; no control block (`#FEBE` stays register 6) | both (CS1 for the whole `#F8-#FFBE` window) | both (even registers → control) | `#1DB8-#1DDE`: sets D7, writes `#0C` then `#00` to `#FEBE` (software reset), clears D7 | control block at `#FEBE`; RTC phase |
| D6 | EEPROM clock (SCL) out | yes | yes | yes | yes | `#0EF7`, `#0F2C` | SCL |
| D5 | EEPROM write protect | (unused) | yes | yes | (unused) | `#0F3E` | WP |
| D4 | EEPROM data (SDA) out | yes | yes | yes | yes | `#0EF7` | SDA |
| D3 | interrupt enable (8259 output to the Z80 `/INT`) | - | - | - | - | set only around the 8259 test and cleared after (`#170C-#1716`, `#172A-#1734`, `#019E-#01A8`) | interrupt enable (firmware evidence only) |
| D2, D1 | unknown; the firmware keeps them 1 | - | - | - | - | `OR #06` at `#15CD` (ISA reset path); `#F7` at power-on | keep, no effect |
| D0 | **IDE reset** (Q3, §5.6) | 1 resets | not modeled | **0 resets** | 1 resets | 0 then 1 pulse; 1 in normal use | **0 = reset** (§5.6) |

Read bits:

| Bit | Role | Unreal | Xpeccy | MAME | ZXMAK2 | Consensus |
|---|---|---|---|---|---|---|
| D6 | EEPROM data in (SDA; ACK = 0) | yes (`0xFF`/`0xBF`) | yes | yes | yes | SDA |
| D7 | IDE interrupt request (INTRQ) | always 1 | always 1, with the comment "TODO: b7: INTRQ from HDD/CF" (`hdd.c:746`) | always 1 | **INTRQ** (`IdeSmuc.cs:237-239`, `AtaPort.cs:61-66`) | emulator majority: constant 1; hardware intent (ZXMAK2 + Xpeccy TODO): INTRQ. No software in the reference trees reads D7 |
| D5-D0 | - | 1 | 1 | 1 | 1 | 1 |

### 5.3 `#7FBA` virtual FDD

The ProfROM TR-DOS (page 3) reads `#7FBA` before every disk operation: drive 0 looks at **D7**,
drive 1 at **D6**; a 1 means "the real drive", a 0 means "a disk image on the hard disk". Drives C
and D are handled by the firmware alone (the LW change log: without SMUC, mounted images work
only on drives C and D, `changes.md:440-441`). Page 3 `#0C01-#0C1B` also tests **D2** and, when it
is 1, waits on the Beta `#FF` status. The ProfROM writes `#FF` at power-on (`#0D09-#0D11`: "no
virtual drives").

| Reference | Read value | Evidence |
|---|---|---|
| Unreal | `latch | #3F` | `io.cpp:846` |
| Unreal-nedopc | `latch | #37` ("bit 3 seems to be used in profrom to indicate presence of HDD") | `io.cpp:1001` |
| Xpeccy | `(latch & #C0) | #3F` | `hdd.c:749`, `773` |
| MAME | `latch | #37` | `smuc.cpp:90-92` |
| ZXMAK2 | `latch | #37`; comment "D7 = 0 → fdd A virtual, D6 = 0 → fdd B virtual, D3 = 0 → HDD present" | `IdeSmuc.cs:189-198` |

**Consensus: `latch | #37`** (D7, D6 and D3 read back what was written; the other bits read 1).

Whether the real card also blocks the real floppy controller for a virtual drive (a hardware
trap) is **not known**: the LW change log says its WD1793 emulation "will not work on emulators"
(`changes.md:286`), which hints at hardware no emulator models. Open question Q5 in
[integration-plan.md](integration-plan.md).

### 5.4 `#7EBE` / `#7FBE` 8259

| Reference | Read | Write |
|---|---|---|
| Unreal | `#57` | ignored |
| Xpeccy | `#FF` ("pic (not used)") | ignored |
| MAME | `#57` ("i8259 - absent in 2.0") | ignored |
| ZXMAK2 | `#57` ("not installed") | ignored |

The ProfROM probe (page 7 `#1572-#15C6`) writes the 8259 mask register and reads it back: after
`#01` it expects `#00` or `#01`, after `#FF` it expects `#FF`. A constant `#57` (or `#FF` then
`#57`) fails the first check, so **in every emulator the ProfROM concludes that no 8259 is
fitted**. That is the consensus: the SMUC 2.x card without the 8259. When fitted, the firmware
programs it (`#16DA-#16EB`: ICW1 `#1A`, vector base `#F8`, mask `#FE` = only IR0), programs the
DS1685 periodic interrupt (registers `#0A` = `#26`, `#0B` = `#D6` then `#56`), sets `#FFBA` D3,
switches to IM 2 with `I = 0`, and waits for the interrupt handler (`#0148`, which sends EOI `#20`
to `#7EBE`) to set a flag.

### 5.5 `#5FBA` / `#5FBE` version and revision

The ProfROM decodes each byte `b` into a 3-bit number (page 7 `#2443-#2452`): bits D7, D6, D5
become bits 2, 1, 0, and D3 is ORed into bit 0. A read of `#FF` from `#5FBA` means "no version
register" (`#242A-#2430`).

| Reference | `#5FBA` | `#5FBE` | Decoded by ProfROM |
|---|---|---|---|
| Unreal | `#3F` | `#57` | 1.2 |
| Xpeccy | `#28` | `#40` | 1.2 |
| MAME | `#40` | `#28` | 2.1 |
| ZXMAK2 | `#57` | `#17` | 2.0 |

Four raw answers, but two of them decode to the same version: **1.2 is the only value two
references agree on**, and it is what unreal-ng already returns (`#3F` / `#57`). Worked example:
`#57` = `0101 0111`: D7 D6 D5 = `010` = 2, D3 = 0 → 2.

### 5.6 IDE reset polarity (Q3)

The IDE design left Q3 open: "Unreal / ZXMAK2 = 1 resets, MAME = 0 resets". The firmware settles
it. On an IDE cable the reset line is **active low** and a **level**: while it is asserted the
drive stays in reset.

1. **Normal operation keeps D0 = 1.** The ProfROM power-on value is `#F7` (`#0D00`); its IDE
   initialization (`#1E74-#1E9D`) does `OR #81`, writes device control `#00` to `#FEBE`, then
   `AND #7F : OR #01`, waits (`DJNZ`), and reads the status. The software reset at `#1DB8`
   keeps D0 as it is. The savelij ROM uses `#F7` / `#77` (D0 = 1). Every serial-link access
   (hundreds of `#FFBA` writes per NVRAM read) keeps D0 as well.
2. **The firmware pulses D0 low to reset.** Page 7 `#15C7-#15E2`: `OR #06 : AND #7E` (D0 = 0),
   `OUT`, a short delay, `OR #01` (D0 = 1), `OUT`, then a long delay before touching the bus.

If 1 meant "reset", point 1 would hold the drive in reset forever on real hardware. **Answer: D0
= 0 asserts the reset (MAME).** Unreal's and ZXMAK2's "1 resets" happens to work in those
emulators only because their reset is instantaneous: they reset the drive on every one of those
writes. unreal-ng inherited the Unreal polarity and switched to D0 = 0 in IDE rollout 1 (on master, `f5fc5f05`)
(G3 in [current-state-and-gaps.md](current-state-and-gaps.md)).

How to model it: MAME resets the ATA bus on **each write with D0 = 0** (`smuc.cpp:49-52`) and does
not hold the drive. That is the right choice here too. A held level would leave the drive dead
for any driver that never writes `#FFBA` (the savelij and Wild Commander drivers never do) after a
power-on latch of `#00`.

### 5.7 IDE window

| Access | Unreal | MAME | ZXMAK2 | Xpeccy | Consensus |
|---|---|---|---|---|---|
| `#F8BE` read | low byte of the data word; high byte into the latch | same | same | same | same |
| `#D8BE` read | the latched high byte | same | same | same | same |
| `#D8BE` write, then `#F8BE` write | high byte latched, word sent on the low-byte write | same | same | same | "Nemo order" |
| `#F9BE-#FFBE` | ATA registers 1-7 | same | same | same | same |
| `#FEBE` with `#FFBA` D7 = 1 | device control / alternate status (odd registers read `#FF`) | CS1 register 6 (device control / alternate status); the other CS1 registers | control / alternate status for even registers | head register (no control block) | device control / alternate status at `#FEBE` |

Worked example (the savelij driver, `drv_smuc.a80` and `ports_ide.a80:60-78`): to read one
sector by LBA, write `#E0 | head` to `#FEBE`, the count to `#FABE`, the LBA bytes to `#FBBE`,
`#FCBE`, `#FDBE`, the command `#20` to `#FFBE`, poll `#FFBE` until `(status & #88) = #08` (DRQ set,
BSY clear), then 256 times: `IN #F8BE` (low byte), `IN #D8BE` (high byte). Every one of these goes
through TR-DOS `#3D2F`. The image file stores the low byte first.

### 5.8 DS1685 register access

| | Unreal | Xpeccy | MAME | ZXMAK2 |
|---|---|---|---|---|
| `#DFBA` write | D7 = 0: address; D7 = 1: data | same | same | same |
| `#DFBA` read | data, whatever D7 is | data only when D7 = 0, else `#FF` | data, whatever D7 is | data only when D7 = 0 |

Split 2-2 and no firmware evidence (the ProfROM clock helpers around page 7 `#1FDD` were not
traced for this document). Keep the Unreal / MAME behavior (unreal-ng today).

## 6. Consensus summary (what unreal-ng should implement)

| Topic | Decision | Basis |
|---|---|---|
| Gating | TR-DOS ports on | all references + all software |
| Decode | mask `#B8E7` (A4, A3 not decoded) | Black Cat, MAME, ZXMAK2, Unreal |
| `#FFBA` D0 | write with D0 = 0 resets the IDE units (a pulse per write, no held level) | ProfROM firmware; MAME |
| `#FFBA` D7 write | control block at `#FEBE`; RTC data phase | Unreal, MAME, ZXMAK2 |
| `#FFBA` read D7 | INTRQ of the selected unit, open question (Q2) | ZXMAK2 + Xpeccy TODO vs a constant 1 in three emulators |
| `#7FBA` read | `latch | #37` | Unreal-nedopc, MAME, ZXMAK2 |
| 8259 | absent (reads `#57`, writes ignored) | all four emulators; the ProfROM probe then reports it missing |
| Version | `#3F` / `#57` (decodes to 1.2) | Unreal and Xpeccy decode to the same value |
| EEPROM | 24C16, 2 KB | MAME, Unreal |
| RTC | DS1685 | MAME, Black Cat |
| Default | card **not fitted** | Unreal (`SMUC=0`, `Scheme` default), MAME (empty ZX-BUS slots, `scorpion.cpp:513-515`); only ZXMAK2's `SCORP-PROF-ROM` machine fits it (`machines.config:192`) |
