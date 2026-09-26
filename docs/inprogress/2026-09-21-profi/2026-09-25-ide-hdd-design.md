# IDE Hard Disk Support — Technical Design (Profi first, shared by all IDE machines)

| | |
|---|---|
| **Status** | Design / draft for review |
| **Date** | 2026-09-25 |
| **Branch** | `profi` (baseline `f4e35ee6`) |
| **Closes** | Reconciliation gaps **G1** (Profi IDE) and **T3** (IDE state in TTD), [2026-09-25-profi-reconciliation.md](2026-09-25-profi-reconciliation.md) §4; roadmap items **ST-1..ST-3, ST-5** ([01-roadmap-and-machine-state.md](../2026-09-21-roadmap/01-roadmap-and-machine-state.md) §6) |
| **Replaces** | The unimplemented UnrealSpeccy skeleton in `core/src/emulator/io/hdd/` and the fake SMUC IDE window in `PortDecoder_Scorpion256` |
| **Scope** | (1) the Profi IDE controller, fully; (2) a shared hard-disk core reusable by the Scorpion SMUC, ATM, Nemo, ZX-Evo and DivIDE controllers; (3) disk contents from **image files** or **host folders**, plus **ATAPI CD-ROM** from `.iso` images; (4) time-travel (TTD) and snapshot support; (5) a test method that covers every controller variant |
| **Rollout** | **Rollout 1 — parity with other emulators**: IDE on all boards, image files, host folders, ATAPI CD-ROM, automation. **Rollout 2 — TTD + disk change layers**: ⚠️ *requires further investigation* (§7.3, §10); the sections describing it are a direction, not a decided design |

Evidence tags used below: **[CONS]** UnrealSpeccy + ZXMAK2 agree (the reference emulators for original Profi behaviour); **[RTL]** Karabas-Pro FPGA/CPLD source (a clone board: good evidence for *how the wiring works*, not authoritative where it contradicts [CONS]); **[ROM]** seen in real Profi firmware disassembled for this document; **[?]** unverified, listed in §13.

---

## 0. Summary in five sentences

1. Every Spectrum IDE interface puts the same kind of standard PC hard disk behind the Z80. Only three things differ between them: which I/O port numbers are used, when those ports are switched on, and how the disk's 16-bit data word is split into two 8-bit Z80 transfers.
2. So we build **one shared ATA disk core** (registers, commands, identify data, master/slave, reset) and a **small adapter per machine** that only translates port numbers. The Profi adapter is about 80 lines.
3. The disk core reads and writes 512-byte sectors through **one block-device interface**. That interface is implemented by a raw image file, headered images (HDF/HDI), an in-memory disk for tests, and a **host folder shown to the guest as a FAT16/FAT32 disk** (like VMware shared folders, but at the sector level).
4. The same channel can hold an **ATAPI CD-ROM** (`.iso`), as UnrealSpeccy, pico-spec and MAME already offer.
5. Delivery is in two rollouts. **Rollout 1** matches what other emulators do: image writes go straight to the file (write-protect option), folder volumes keep guest writes in memory for the session. **Rollout 2** (⚠️ needs further investigation) adds a copy-on-write change layer and TTD rewind across disk writes; until then, TTD treats IDE the way it already treats floppy writes (§10.0). Testing has seven layers: disk-core conformance, per-machine port truth tables, the byte-order latch, storage backends, real-ROM boot tests (the Profi SYS ROM has an HDD boot loader, found for this document), TTD, and folder-mount round trips.

---

## 1. Glossary (plain language)

| Term | Meaning here |
|---|---|
| **IDE / ATA** | The standard PC hard-disk interface from about 1990. The disk has 8 one-byte "task file" registers plus one control register. The computer writes an address and a command, then moves 512-byte sectors through a 16-bit data register. "IDE" is the cable/interface, "ATA" the command protocol. We use them interchangeably. |
| **Task file** | The 8 main disk registers: 0 data, 1 error/features, 2 sector count, 3 sector number, 4 cylinder low, 5 cylinder high, 6 drive/head, 7 status/command. Selected by the disk's **CS0** chip-select line. |
| **Control block** | The second register group, selected by **CS1**. Only register 6 matters: *device control* on write (bit 2 = software reset **SRST**, bit 1 = interrupt disable **nIEN**) and *alternate status* on read (status without side effects). |
| **Adapter** | The glue logic on each Spectrum board that turns Z80 port accesses into disk register accesses. This is the only machine-specific part. |
| **High-byte latch** | The Z80 moves 8 bits per `IN`/`OUT`, but the disk data register is 16 bits. Every board adds a one-byte holding register. A read fetches 16 bits, returns the low byte and parks the high byte in the latch for a second `IN`. A write parks the high byte first; the low-byte `OUT` then sends both bytes together. |
| **CHS / LBA** | Two ways to address a sector. **CHS** = cylinder/head/sector (old; sector numbers start at 1). **LBA** = one linear sector number starting at 0. Conversion: `LBA = (C × Heads + H) × SectorsPerTrack + (S − 1)`. |
| **Geometry** | The Heads and SectorsPerTrack numbers used in that conversion. If the emulator's geometry differs from the one the disk was formatted with, CHS software reads the wrong sectors. |
| **IDENTIFY** | ATA command `#EC`. The disk returns a 512-byte self-description: model name, geometry, capacity, LBA support. |
| **BSY / DRDY / DRQ / ERR** | Status register bits: 7 busy, 6 ready, 3 "data request" (a sector is waiting to be moved through the data register), 0 error. |
| **INTRQ** | The disk's "command finished" interrupt line. No Spectrum IDE board wires it to the Z80 interrupt. ATM exposes it as a pollable bit. |
| **EXT mode (Profi)** | The Profi state in which the "extended" peripherals (IDE, RTC, remapped floppy ports) answer: `#DFFD` bit 5 (CP/M) = 1 **and** `#7FFD` bit 4 (ROM14) = 1. Implemented today as `PortDecoder_Profi::IsExtMode()`. |
| **Block device** | Anything that can read or write numbered 512-byte sectors: an image file, a memory buffer, a synthesized folder volume. |
| **COW (copy-on-write) layer** *(rollout 2)* | A sparse table "sector number → new 512 bytes" placed over a read-only block device. Reads check the table first; writes only go into the table. The original never changes until someone commits. |
| **Commit / discard** *(rollout 2)* | Commit writes the COW layer back into the original (image or folder). Discard throws it away. VMware calls these "persistent" and "non-persistent" disks. |
| **Host folder volume** | A FAT16/FAT32 disk that does not exist as a file. It is generated on the fly from a folder on the host, so files built on the PC show up inside the Spectrum OS with no image repacking. |
| **TTD** | Time-travel debugging: unreal-ng's record/rewind engine. Every piece of machine state must be saved and restored bit-exactly. |
| **ATAPI** | "ATA Packet Interface": how CD-ROM drives sit on the same IDE cable. The drive uses the same registers, but instead of ATA read/write commands the computer sends a 12-byte **SCSI command block** (a "packet") through the data register, via ATA command `#A0` (PACKET). Data blocks are 2048 bytes, not 512. |
| **ISO image** | A file holding a CD's 2048-byte data blocks in order (ISO 9660 file system; the text `CD001` at byte `#8001` identifies it). |

---

## 2. What exists in unreal-ng today

| Item | State | Where |
|---|---|---|
| ATA device / port model | **Declarations only** (UnrealSpeccy `ATA_DEVICE`/`ATA_PORT` header copied, no `.cpp`) | `core/src/emulator/io/hdd/hdd.h` |
| Physical-drive passthrough | Win32 `HANDLE` stubs, dead | `core/src/emulator/io/hdd/hddio.h` |
| `HDD` object | Created by `Core`, `Reset()` only clears 5 latch bytes | `core/src/emulator/cpu/core.cpp:309`, `io/hdd/hdd.cpp` |
| Latch bytes | `ide_hi_byte_r/w/w1`, `ide_read`, `ide_write` in `EmulatorState` | `core/src/emulator/platform.h:1018` |
| Config | `IDE_SCHEME` enum (NONE/ATM/NEMO/NEMO_A8/NEMO_DIVIDE/SMUC/PROFI/DIVIDE), `IDE_CONFIG ide[2]` in `CONFIG`; **the `[HDD]` ini section is not parsed** (`config.cpp:299` is an empty comment) | `platform.h:290-297, 373-379, 496-498` |
| `data/configs/profi/unreal.ini` | `[HDD] Scheme=NEMO-DIVIDE` (wrong for Profi, currently ignored). **The file has CRLF line endings; stage it with `-c core.autocrlf=false`** | line 486 |
| Profi port decoder | Has the EXT-mode gate, RTC, FDC port sets. **No IDE decode**: `#xx8B/#xxAB/#xxCB/#xxEB` fall through to the floating bus (`#FF`) | `core/src/emulator/ports/models/portdecoder_profi.cpp` |
| Scorpion SMUC | Stub IDE window: status always `#50`, data always `#00`, task-file registers read back. Enough for ProfROM's "IDE controller found" probe, nothing more | `portdecoder_scorpion256.cpp:858-950` |
| Port-trace category | `PortTag::StorageIde` already reserved | `core/src/emulator/ports/portdecoder.h:95` |
| TTD | No IDE `PeripheralId`. Disk media are not in TTD at all: WD1793 floppy writes invalidate a recording (`wd1793.h:1171-1190`) | `core/src/debugger/ttd/ttdserializable.h:42-56` |
| FAT / filesystem code | None | — |
| Floppy write-back | Per-sector dirty flags, explicit `Emulator::SaveDisk()`, no COW | `io/fdc/diskimage.h`, `emulator.cpp` |

**Conclusion:** there is nothing worth keeping except the `IDE_SCHEME` enum and the config struct shape. The skeleton headers get replaced.

---

## 3. The Profi IDE controller in detail

### 3.1 Port decode

The board reacts when the low address byte matches `A7=1` and `A4..A0 = 01011`, i.e. `(port & 0x9F) == 0x8B`. That family is `#8B, #AB, #CB, #EB`. Address bits **A10..A8 carry the disk register number (0–7)** [CONS][RTL]. A15..A11 are not decoded.

The two remaining low-byte bits, A6 and A5, choose the function:

| Low byte | A6 A5 | Z80 **read** (`IN`) | Z80 **write** (`OUT`) |
|---|---|---|---|
| `#CB` | 1 0 | task-file register A10..A8. For register 0 (data): performs the 16-bit disk read, returns the **low** byte, stores the high byte in the read latch | stores the value in the **write latch** (A10..A8 ignored) |
| `#EB` | 1 1 | returns the **read latch** (A10..A8 ignored) | task-file register A10..A8. For register 0: sends `latch << 8 \| value` to the disk as one 16-bit write |
| `#AB` | 0 1 | *nothing drives the bus* → `#FF` [RTL]. UnrealSpeccy/ZXMAK2 do not decode it either. pico-spec returns alternate status here (unconfirmed) | control block register A10..A8. Only `#06AB` (device control: SRST / nIEN) is meaningful |
| `#8B` | 0 0 | no disk access (Karabas puts its own `#008B/#018B/#028B` config registers here; out of scope) | no disk access |

Note the **mirror-image roles** of `#CB` and `#EB`: reads use `#CB` for "register" and `#EB` for "latch"; writes use the opposite. A naive "A5 = high byte" decode gets exactly one direction wrong. Xpeccy expresses this as `if (wr) port ^= 0x20` (`Xpeccy/src/libxpeccy/hdd.c:790`).

Full port table (A15..A11 shown as 0):

| Disk register | Read port | Write port |
|---|---|---|
| 0 data | `#00CB` (low byte, triggers transfer) then `#00EB` (high byte) | `#00CB` (high byte into latch) then `#00EB` (low byte, sends word) |
| 1 error / features | `#01CB` | `#01EB` |
| 2 sector count | `#02CB` | `#02EB` |
| 3 sector / LBA 7:0 | `#03CB` | `#03EB` |
| 4 cylinder low / LBA 15:8 | `#04CB` | `#04EB` |
| 5 cylinder high / LBA 23:16 | `#05CB` | `#05EB` |
| 6 drive/head / LBA 27:24 | `#06CB` | `#06EB` |
| 7 status / command | `#07CB` | `#07EB` |
| control block 6 | (`#06AB` → `#FF`) | `#06AB` device control |

**Worked example: reading one 16-bit word.** Suppose the disk's next data word is `#1234`.

```
IN A,(#00CB)   ; disk transfers #1234; A = #34 (low); read latch = #12
IN A,(#00EB)   ; A = #12 (from the latch; no disk access)
```

**Worked example: writing one word.** To send `#ABCD`:

```
LD BC,#00CB : LD A,#AB : OUT (C),A   ; write latch = #AB (no disk access)
LD BC,#00EB : LD A,#CD : OUT (C),A   ; disk receives #ABCD
```

### 3.2 When the ports answer (gating)

| Source | IDE answers when |
|---|---|
| UnrealSpeccy `io.cpp:261/869`, ZXMAK2 `IdeProfi.cs:133-142` [CONS] | CP/M (`#DFFD.5`) **and** ROM14 (`#7FFD.4`) = EXT mode |
| Karabas-Pro RTL `karabas_pro.vhd:1420-1425` [RTL] | EXT mode **or** (DOS latch on **and** ROM14 = 0), i.e. also while the SYS ROM runs |
| pico-spec `Ports.cpp:873` | like RTL, plus `!cpm` on the DOS term |
| Xpeccy | always (no gate; a bug) |

**Decision: IDE uses the existing `IsExtMode()` predicate** ([CONS]; matches the RTC and extended-FDC gates already in the decoder). Reasons:

- The real firmware we have (`profi_mainrom_standart.rom`, §3.6) explicitly sets `#7FFD=#19` and `#DFFD=#B8` (CP/M + ROM14) before its first IDE access. It works under the [CONS] gate.
- Widening only the IDE gate is unsafe. With the DOS latch on and ROM14 = 0, our decoder maps the floppy system port with `(p1 & 0xE3) == 0xE3`, so **`#EB` aliases the Beta128 system port `#FF`**. In the RTL this conflict cannot happen, because the RTL moves *all* floppy ports to the extended set in that state (`karabas_pro.vhd:1464-1472`). The Karabas behaviour is a whole-decoder variant, not an IDE tweak.
- The existing technical design already rejected the Karabas EXT variant for the RTC and floppy ports (technical-design.md §4, reconciliation §5.2).

A single future switch (`[MISC] ProfiExtMode=Classic|Karabas`) would change `IsExtMode()` and `DecodeFDCPort()` together. Only then would the IDE follow automatically. That switch is **out of scope** here and tracked as open question Q2 (the Karabas BIOS's HDD self-test needs it; pico-spec comment `Ports.cpp:865-872`).

### 3.3 Latch rules (exact)

| Rule | Choice | Evidence |
|---|---|---|
| Read latch updated by | **data-register reads only** (`#00CB`) | [CONS]. RTL also captures D15..8 on non-data `#xxCB` reads, but the drive does not drive D15..8 for 8-bit registers, so the value would be undefined. No known software reads `#EB` after a non-data read. Emulating "undefined" as "unchanged" is the least surprising choice |
| Write latch set by | any `OUT` to `#xxCB` (any A10..A8) | [CONS][RTL] |
| Read latch read by | any `IN` from `#xxEB` (any A10..A8) | [CONS][RTL]. Xpeccy only accepts `#00EB`, which is stricter than hardware |
| Machine reset | clears both latches and hard-resets the disks | Unreal `z80.cpp:174-177`; RTL `IDE_RESET_N <= NRESET` (`ide_controller.vhd:172`). ZXMAK2 does not reset (a bug) |
| Separate or shared latch | **two separate latches** (read, write) | [CONS][RTL: `wd_reg_out` vs `wd_reg_in`] |

### 3.4 Interrupt, timing, master/slave

- **INTRQ is not wired** on Profi [CONS][RTL]. The disk core still keeps the flag (the ATM adapter needs it).
- **Timing:** every reference emulator completes commands instantly (BSY never seen, DRQ set during the command-register `OUT`). The Profi ROM polls BSY and DRQ, so instant completion is safe. The disk core offers an optional busy-delay model for robustness testing (§6.5); it is off by default.
- **Master and slave:** the hardware is a standard 40-pin channel, so both drives are supported. The drive/head register bit 4 selects the drive. Profi software found so far only uses the master.

### 3.5 Byte order on the disk

Image files store each disk word **low byte first** (byte 0 = D7..D0), exactly as the drive sees it. **The emulator never swaps bytes.**

The Profi BIOS copies each word into memory **high byte first**. The disassembly in §3.6 shows `IN E,(#00CB)` / `IN D,(#00EB)` / `LD (HL),D` / `INC HL` / `LD (HL),E`. So Profi-formatted disks look "byte-swapped" when inspected on a PC: pico-spec notes the signature `ProfiHiDD` appears as `rPfoHiDD` in images (`pico-spec/src/IDE.cpp:558-563`). That is correct and expected. Tools that inspect Profi disks from the host (§9) must know this; the disk core must not.

### 3.6 Real-firmware evidence: the SYS ROM HDD boot loader

`testdata/machines/profi/rom/profi_mainrom_standart.rom`, page 0 (SYS ROM), contains an HDD boot loader. It was disassembled with `z80dasm` for this document:

- **`#28CE`** is the entry point. It sets `#DFFD=#80`, copies `#28E2..#2AA1` (448 bytes) to `#8000`, then `RET`s into `#8000` via a pushed `DE`. No direct `CALL #28CE` exists in the page, so it is reached through a jump table [?Q5].
- **`#8000`** (ROM `#28E2`) executes: `DI`, `LD SP,#849B`, `CALL #8077` (sets `#7FFD=#19`, `#DFFD=#B8`: CP/M + WOROM + SCO + DS80 + ROM14; **EXT mode on**), `CALL #80B0` (reset drive).
- **Reset** (`#80B0`): `OUT (#06AB),#0E` (SRST + nIEN), delay, `OUT (#06AB),#0A` (release SRST, keep nIEN), then poll `IN (#07CB)` up to 1000 times for BSY = 0. **With no drive, status reads `#FF` (BSY stuck), the loop times out, and the loader jumps to its error exit `#808B`: `#DFFD=#87`, `#7FFD=#0A`, fill `#C000..#FFFF` with `#C2`, then hang at `#80A4` (`JR $`).** The ROM also contains the string "HDD error, press RESET !!!" (`#28B3`); whether another caller prints it is unverified.
- **Diagnostics / recalibrate:** `OUT (#07EB),#90` (EXECUTE DIAGNOSTIC), expects `IN (#01CB) = #01`. Then `#02EB..#06EB ← 1,1,0,0,#40`, `OUT (#07EB),#10` (RECALIBRATE). `OUT (#06EB),#40` and a wait for DRDY + DSC.
- **Sector read** (`#8179`, args HL = buffer, DE = cylinder, B = sector, C = head): `#05EB←D, #04EB←E, #02EB←1, #03EB←B, #06EB←#A0|C` (CHS mode, master), `#07EB←#21` (READ SECTORS without retry), wait DRQ, then 256 × (`IN E,(#00CB)` / `IN D,(#00EB)` / store D then E).
- **Boot:** read C1/H0/S1 into `#0100`. The byte at `#0100` must be < 17 (heads) and is saved to `#8075`; the byte at `#0102` must be < 128 (sectors per track) and is saved to `#8076`. Then read C1/H0/S6 into `#00FE`; the byte now at `#00FE` is a sector count N. Read N more sectors from C1/H0/S7 onward into `#02FE`, wrapping the sector number at `#8076` into the next head. Finally `JP #0100`.

This gives us a **fully specified acceptance test** (§12.5, test R1). It also confirms: [CONS] gate, port roles, the latch order, CHS addressing, commands `#90 #10 #21`, and the no-drive behaviour. The loader does **not** send IDENTIFY or INITIALIZE DEVICE PARAMETERS, so **the configured CHS geometry must match the geometry the disk was formatted with** (§8.3).

A second HDD-boot ROM exists for cross-checking: `ZXMAK2/ROMS/PROFI/profi-hddboot.rom` (a different build; md5 `a51f8bc8…`).

### 3.7 How the reference emulators compare (Profi only)

| Aspect | UnrealSpeccy | ZXMAK2 | Xpeccy | pico-spec | Karabas RTL | **unreal-ng plan** |
|---|---|---|---|---|---|---|
| Gate | EXT | EXT | none | EXT ∪ (DOS∧¬ROM14∧¬CPM) | EXT ∪ (DOS∧¬ROM14) | EXT (§3.2) |
| `#06AB` write | device control | device control | **dropped** (bug) | device control | CS1 write | device control |
| `#06AB` read | not decoded | not decoded | `#FF` | alt status | `#FF` | `#FF` |
| Write latch port | any `#xxCB` | any `#xxCB` | `#00CB` only | any `#xxCB` | any `#xxCB` | any `#xxCB` |
| Reset on machine reset | yes | **no** | yes | yes | yes | yes |
| Drives | 2 | 1 (config) | 2 | 2 | 2 (hardware) | 2 |
| Images | raw | raw | raw, HDI | raw, HDF, VHD, ISO | — | raw, HDF, HDI, VHD, ISO (CD-ROM), folder (§7) |
| Read-only write | ABRT | silent success | — | — | — | ABRT (§6.4) |
| Timing | instant | instant | instant | instant | real strobes | instant + optional delay model |

---

## 4. All Spectrum IDE controllers side by side

Sources: UnrealSpeccy `io.cpp`, ZXMAK2 `Hardware/*/Ide*.cs`, Xpeccy `libxpeccy/hdd.c`, MAME `bus/spectrum/zxbus/{nemoide,smuc}.cpp`, ZX-Evo RTL `fpga/current/z80/zports.v`, NedoOS drivers `src/kernel/{main,fatfsdrv}.asm`.

| Controller | Ports | Register number from | Control block (CS1) | High-byte method | Active when | INTRQ visible | Extra devices on the board |
|---|---|---|---|---|---|---|---|
| **Profi** | `#xx8B/AB/CB/EB` | A10..A8 | port `#06AB` (write only) | separate read/write latches, **mirror roles** (§3.1) | Profi EXT mode | no | shares the decode with RTC `#9F` and floppy `#83` families |
| **Nemo** (Pentagon) | `#10,#30…#F0`, `#11`, `#C8` | A7..A5 | `#C8` (A4=0, A3=1) = reg 6 | latch port `#11` (A0=1): read `#10`→`#11`; write `#11`→`#10` | DOS ports **off** | no | — |
| **Nemo-A8** | same, latch `#110` | A7..A5 | `#C8` | latch selected by A8 | DOS ports off | no | — |
| **Nemo-DivIDE / ZX-Evo** | same as Nemo | A7..A5 | `#C8` only (Evo: other `#x8` alias to CS0) | Nemo latch **or** same-port toggle: `#10` twice = low then high; any other register resets the toggle | always (Evo RTL has no DOS term) | no | Evo: IDE DMA, PIO4 bus timing |
| **SMUC** (Scorpion) | `#F8BE…#FFBE`, `#D8BE` | A10..A8 | **bit 7 of `#FFBA`** (a latch bit, not an address bit); then `#FEBE` = reg 6 | latch at `#D8BE` (A13=0); Nemo order | TR-DOS active (shadow ports) | ZXMAK2 only: `#FFBA` D7 | `#FFBA` sys (bit 0 IDE reset, I²C EEPROM), `#DFBA` RTC, `#7FBA` virtual FDD, `#5FBA/#5FBE` version, 8259 stub |
| **ATM Turbo 2+** | `#xx0F…#xxEF` (`(p & 0x1F) == 0x0F`) | A7..A5 | none known | latch at A8=1 (`#FF0F`); Nemo order | DOS ports on (TR-DOS or ATM3 shadow) | **yes**: `#7FFD`-class read, bit 6 | shares the port with DAC/ADC |
| **DivIDE** | `#A3…#BF` (`(p & 0xA3) == 0xA3`) | A4..A2 | none | same-port toggle on `#A3` | DOS ports off | no | `#E3` memory paging + automap (separate project) |

### 4.1 What is the same everywhere → shared code

- The disk itself: task file + control block, command set, IDENTIFY, CHS/LBA28/LBA48, SRST, error codes, master/slave selection (writes go to both drives, the addressed drive answers, an empty channel reads `#FF`).
- A **16-bit data interface** (`ReadData() → uint16`, `WriteData(uint16)`). Every adapter splits words its own way, so the core must not know about bytes.
- INTRQ as a *state* (set on completion/DRQ, cleared by a status read, masked by nIEN). Whether and where it becomes visible is adapter business.
- Hard reset (machine reset line) and soft reset (SRST).
- Media: block devices and write handling (rollout 1: write-through / session write map; rollout 2: COW layer, commit/discard, TTD/snapshot state).
- The byte-split helpers. Three latch patterns cover every board: **(a)** fixed latch port (Nemo, Nemo-A8, ATM, SMUC); **(b)** mirror-role latch ports (Profi); **(c)** same-port toggle (Nemo-DivIDE, Evo, DivIDE). These are shared helper classes the adapters use.

### 4.2 What differs per machine → adapter code

- Address decode: mask/value, which bits give the register number, how CS1 is chosen.
- Which latch pattern it uses and which port holds the latch.
- The gate (EXT mode, DOS ports on/off, `#FFBA.7`, ATM3 shadow).
- INTRQ visibility (ATM `#7FFD` D6; SMUC `#FFBA` D7 [?]).
- Board extras that are not disks: SMUC's RTC/EEPROM/version registers, Evo DMA, DivIDE paging. These stay small separate devices owned by the adapter or decoder, as they are today (`SMUCNvram`, `ProfiCMOS`).

---

## 5. Architecture

```
 Z80 IN/OUT
     │
 PortDecoder_<Model>                 (existing, per machine)
     │  owns
 IdeAdapter<Board>  ─────────────────  per board, ~50–150 lines: decode + gate + latch pattern
     │  uses
 AtaChannel                          shared: 2 drive slots, bus rules, reset, INTRQ OR
     │
 AtaDevice ×2                        shared: one slot = AtaDisk (hard disk) or AtapiCdrom (CD-ROM)
     │  reads/writes sectors through
 IBlockDevice                        shared interface (512-byte sectors; ISO = 2048-byte blocks)
     │  Rollout 1:  RawImage | HdfImage | HdiImage | MemoryDisk | HostFolderFat (+ session write map) | IsoImage
     │  Rollout 2:  CowBlockDevice on top of any of them  ⚠️ needs further investigation (§7.3)
```

`AtaDevice` is split internally into a common register/transfer engine and two personalities: `AtaDisk` (ATA command set, §6.3) and `AtapiCdrom` (packet command set, §7.5). Adapters never know which kind sits in a slot.

**Ownership.** `Core` owns one `AtaChannel` (it replaces the current `HDD` object, same place: `core.cpp:309`). The media survive a model switch only if the new model has an IDE scheme; otherwise they are detached. Each `PortDecoder_<Model>` owns its adapter and gets the channel from the context, the same way `PortDecoder_Profi` owns `ProfiCMOS _cmos` today. Models without IDE never touch the channel.

**File layout** (replaces the current `io/hdd/` contents):

```
core/src/emulator/io/hdd/
  ata/atadevice.{h,cpp}          register/transfer engine (one slot)
  ata/atadisk.{h,cpp}            ATA hard-disk command set
  ata/atapicdrom.{h,cpp}         ATAPI packet command set (SCSI CDBs)
  ata/atachannel.{h,cpp}         master/slave bus + reset + INTRQ
  ata/ataregisters.h             register numbers, status/error bits, command codes
  ata/atastate.h                 POD state for TTD/snapshots (static_assert on size)
  adapters/idelatch.h            the three byte-split helpers (FixedLatch, MirrorLatch, ToggleLatch)
  adapters/ideadapter.h          small base: gate hook, trace tagging, Reset()
  adapters/ideadapter_profi.{h,cpp}
  adapters/ideadapter_nemo.{h,cpp}     (Nemo, Nemo-A8, Nemo-DivIDE/Evo via options)
  adapters/ideadapter_smuc.{h,cpp}
  adapters/ideadapter_atm.{h,cpp}
  storage/iblockdevice.h
  storage/rawimage.{h,cpp}
  storage/hdfimage.{h,cpp}       RS-IDE .hdf (incl. "8-bit halved" flag)
  storage/hdiimage.{h,cpp}       Xpeccy-style .hdi (header offset + geometry)
  storage/memorydisk.{h,cpp}     tests, "new blank disk"
  storage/isoimage.{h,cpp}       ISO 9660 image, 2048-byte blocks (read-only)
  storage/sessionwritemap.{h,cpp} rollout 1: in-memory sector overrides for folder volumes
  storage/cowblockdevice.{h,cpp} rollout 2 (needs further investigation)
  storage/hostfolder/fatbuilder.{h,cpp}     layout computation (pure, no I/O)
  storage/hostfolder/hostfolderfat.{h,cpp}  IBlockDevice over the built layout
  storage/hostfolder/foldersnapshot.{h,cpp} directory scan via std::filesystem
core/src/debugger/ttd/hdd/ttdatachannel.{h,cpp}   rollout 2 (needs further investigation, §10)
```

`hddio.h` (Win32 physical-drive passthrough) is deleted: physical drives are out of scope. `std::filesystem` covers the folder scan on all three OSes. If an OS-specific piece becomes necessary (e.g. case-insensitive name collision rules), it goes under the platform subfolder per repo convention.

---

## 6. Shared disk core specification

### 6.1 `AtaDevice` interface

```cpp
class AtaDevice
{
public:
    uint8_t  ReadRegister(uint8_t reg);             // 1..7 task file; kAltStatus for CS1 reg 6
    void     WriteRegister(uint8_t reg, uint8_t v); // 1..7 task file; kDeviceControl for CS1 reg 6
    uint16_t ReadData();                            // register 0, one 16-bit word
    void     WriteData(uint16_t w);
    bool     Intrq() const;                         // after nIEN masking
    void     HardReset();                           // power-on / machine reset
    bool     IsSelected() const;                    // drive/head bit 4 matches this slot
    void     Attach(std::unique_ptr<IBlockDevice> media, const DriveConfig& cfg);
    void     Detach();
    void     SaveState(AtaDeviceState& out) const;  // POD, for TTD and snapshots
    void     LoadState(const AtaDeviceState& in);
};
```

### 6.2 Channel rules (`AtaChannel`)

| Situation | Behaviour | Evidence |
|---|---|---|
| Register write | goes to **both** drives (each latches the task file; only the addressed one executes a command) | ATA spec; Unreal `hdd.cpp:191-207` |
| Register read, one drive present | that drive answers even when the other is addressed, **except** status, which reads `#00` for an absent slave (ATA: "device 1 not present → device 0 answers with status 0") | Unreal `hdd.cpp:158-179, 268-271` |
| Register read, no drive | `#FF` (floating bus) | [CONS]; the Profi ROM relies on it (§3.6) |
| Data read/write to the unselected drive | ignored / reads `#FFFF` | Unreal `hdd.cpp:181-189` |
| Machine reset | both drives `HardReset()`; adapter latches cleared | §3.3 |

### 6.3 Commands (rollout 1)

| Code | Name | Notes |
|---|---|---|
| `#90` | EXECUTE DEVICE DIAGNOSTIC | error = `#01`, signature; accepted even when the drive is not selected |
| `#91` | INITIALIZE DEVICE PARAMETERS | changes the *current* heads/sectors used for CHS translation (IDENTIFY words 54–56) |
| `#10–#1F` | RECALIBRATE | the whole range (obsolete codes), like Xpeccy; Unreal only accepts `#10` |
| `#70–#7F` | SEEK | range as above |
| `#20/#21` | READ SECTORS | `#21` "without retry" used by the Profi ROM and IS-DOS |
| `#24` | READ SECTORS EXT | LBA48 |
| `#30/#31` | WRITE SECTORS | ABRT if the drive is configured write-protected |
| `#34` | WRITE SECTORS EXT | LBA48 |
| `#40/#41/#42` | READ VERIFY (EXT) | |
| `#50` | FORMAT TRACK | accepts 256 words, writes nothing |
| `#C4/#C5/#C6` | READ/WRITE MULTIPLE, SET MULTIPLE MODE | not in Unreal. Added because NedoOS FatFs and esxDOS-era drivers may use them. Block size ≤ 16 |
| `#E7` | FLUSH CACHE | no-op, success |
| `#EC` | IDENTIFY DEVICE | §6.6 |
| `#EF` | SET FEATURES | accept 8-bit PIO / cache sub-commands as no-ops |
| `#E0–#E5, #E6` | power commands | success, no-op (Xpeccy) |
| other | — | status ERR, error ABRT, INTRQ |

A slot holding a CD-ROM uses the ATAPI command set instead (§7.5). Commands above that make no sense for a CD (`#20`, `#30`, `#EC` …) are aborted with the ATAPI signature in the cylinder registers, which is how drivers tell a CD-ROM from a hard disk.

### 6.4 Where guest writes go

- **Write-protected drive** (`HD0RO=1`): WRITE commands fail with ABRT, like a jumper on a real drive (Unreal behaviour). ZXMAK2's "pretend success and drop the data" is rejected: it hides guest bugs.
- **Rollout 1, image files:** writes go **straight to the image file** (write-through), exactly like UnrealSpeccy, ZXMAK2 and Xpeccy. The file is flushed on detach, model change and shutdown.
- **Rollout 1, folder volumes:** writes are kept in an in-memory sector map for the session (DOSBox-X approach, §7.4.3); the folder is never touched.
- **Rollout 2** (⚠️ needs further investigation): every medium opened read-only, writes into a COW layer, explicit commit/discard (§7.3).

### 6.5 Timing model

- **Default: instant.** DRQ/DRDY appear during the command `OUT`; BSY is never visible. This matches every reference emulator and keeps TTD replay trivially deterministic.
- **Optional `[HDD] BusyTStates=<n>`** (test / robustness aid): BSY stays set for *n* T-states after a command, measured on the emulated T-state clock, **never host time**, so TTD stays deterministic. Guest drivers that skip the BSY poll are exposed this way. The state blob carries the deadline T-state.

### 6.6 IDENTIFY contents

Word 0 `#045A`; words 1/3/6 default C/H/S; words 10–19 serial `UNREALNG-<hash>`; 23–26 firmware `ung1`; 27–46 model `UNREAL-NG HDD <backend>` (space-padded, byte-swapped per ATA string rules); 47 `#8010` (multiple ≤ 16); 49 `#0200` (LBA); 53 `#0001`; 54–58 current geometry/capacity; 60–61 LBA28 capacity; 80–88 ATA-5; 83/86/100–103 LBA48 when capacity ≥ 2²⁸; word 255 checksum `#A5`. All values are overridable per drive in the config (serial/model), so software that checks drive names can be satisfied.

### 6.7 CHS translation

`LBA = (C × H_cur + h) × S_cur + (s − 1)`, where `H_cur/S_cur` start from the configured geometry and change with `#91`. Out-of-range CHS or LBA → status ERR, error IDNF. After each sector the address registers advance (CHS with wrap, or LBA +1), so software that reads the registers after a transfer sees the next address (Unreal `update_regs`).

---

## 7. Media layer

### 7.1 `IBlockDevice`

```cpp
class IBlockDevice
{
public:
    virtual ~IBlockDevice() = default;
    virtual uint64_t SectorCount() const = 0;
    virtual bool ReadSector(uint64_t lba, uint8_t* dst512) = 0;   // false -> ATA UNC/IDNF
    virtual bool WriteSector(uint64_t lba, const uint8_t* src512) = 0;
    virtual bool IsWritable() const = 0;                          // false for ISO, write-protected, or (rollout 2) any base under COW
    virtual std::optional<Geometry> NativeGeometry() const = 0;   // from HDI/HDF header, if any
    virtual std::string Describe() const = 0;                     // UI / IDENTIFY model suffix
    virtual uint64_t ContentId() const = 0;                       // stable hash for snapshots/TTD
};
```

### 7.2 Backends

| Backend | Format | Rollout | Parity reference |
|---|---|---|---|
| `RawImage` | `.img` / `.hdd` / `.ima`: sector *n* at byte `n × 512`. Size not a multiple of 512 → last partial sector padded with zeros on read | 1 | all emulators |
| `MemoryDisk` | zero-filled buffer of N sectors; used by tests and "create new disk" | 1 | — |
| `HdfImage` | RS-IDE `.hdf` (`"RS-IDE\x1A"`, data offset at +9, embedded IDENTIFY, flag bit 0 = "only low bytes stored", used by 8-bit DivIDE images) | 1 | zxsp, pico-spec |
| `HdiImage` | Xpeccy `.hdi` (header with data offset, bytes/sector, sectors/track, heads, cylinders) | 1 | Xpeccy |
| `VhdImage` | fixed VHD (a raw image + 512-byte footer). Dynamic/differencing VHD not planned | 1 (late) | pico-spec |
| `HostFolderFat` | host folder as a FAT volume (§7.4) | 1 | xpeccy-plus, DOSBox-X |
| `IsoImage` | ISO 9660 CD image, 2048-byte blocks, read-only (§7.5) | 1 | UnrealSpeccy, pico-spec, MAME |

### 7.3 COW change layer and commit policy — ⚠️ Rollout 2, requires further investigation

> [!WARNING]
> This section is a **direction, not a decided design**. Open points to investigate before rollout 2:
> page granularity vs. the TTD v2 page store; memory ceiling and spill-to-disk for long sessions
> (roadmap open question 2); whether the write journal can reuse `ttdwritejournal`; how commit
> interacts with a running TTD recording; whether floppies (ST-4) should share the same layer;
> the user-facing semantics of `Persistent`/`Manual`/`Discard` versus rollout 1's write-through.
> Rollout 1 does not depend on any of it.

`CowBlockDevice` wraps every base:

- Sparse map `lba → slot`, slots in 4 KB pages (8 sectors) so TTD can reference pages cheaply (same page granularity as the TTD v2 page store).
- **A write equal to the base sector frees the slot** (DOSBox-X trick, `bios_disk.cpp:922-967`), so guests that rewrite unchanged sectors cost nothing.
- Every write is also appended to a **media write journal**: `{t-state, lba, old slot contents or "base", new contents}`. TTD uses it to move the layer back and forth (§10.2).
- **Commit**: write every slot to the base (image: in place; folder: §7.4.3), then empty the layer. **Discard**: empty the layer. **Export**: write base + layer to a new raw image.

Commit policy, `[HDD] WriteBack0/1=`:

| Value | Meaning | VMware analogue |
|---|---|---|
| `Persistent` (default for images) | automatic commit on eject, model change and clean shutdown; manual commit any time | persistent disk |
| `Manual` | commit only on explicit request; ask/warn in the UI on eject | — |
| `Discard` | never commit; every session starts from the original | non-persistent |

**Rollout 1 behaviour for comparison:** images write-through (§6.4), folders session-only (§7.4.3). Rollout 2 must not silently change what users already rely on: `Persistent` is meant to feel like rollout 1's write-through.

### 7.4 Host folder as a disk

#### 7.4.1 Why the folder must become sectors

On an Amiga, WinUAE can mount a host folder at the *file-system* level: AmigaOS loads a file-system handler, and WinUAE answers "open file / read file" requests (`WinUAE/filesys.cpp`). A Spectrum OS offers no such hook. NedoOS, esxDOS, CP/M and IS-DOS all talk to the IDE **registers** and read raw sectors. So the folder must be presented as a disk whose sectors *look like* a FAT file system that contains the folder's files. Two emulators in the local corpus already do this: **xpeccy-plus** (`src/libxpeccy/vfat.c`, read-only FAT32 for IDE and SD) and **DOSBox-X** (`src/ints/bios_disk.cpp:374-1100`, FAT12/16/32 with an in-memory sector diff).

#### 7.4.2 How the volume is built (worked example)

Folder `~/zx/build/` contains `HELLO.COM` (3 000 bytes) and `data/LEVEL1.BIN` (10 000 bytes). At mount:

1. **Snapshot** the tree once: names, sizes, mtimes. Later host changes are ignored until an explicit *refresh*, which is allowed only when the session write map (rollout 2: the COW layer) is empty.
2. **Choose the format** by total size: FAT16 below 2 GB (default; most compatible with Spectrum FAT drivers), FAT32 above or when forced (`FolderFs=FAT32`). 4 KB clusters. One MBR partition at LBA 2048 (type `#06` FAT16 or `#0C` FAT32); a "superfloppy" (no MBR) option exists for software that wants it.
3. **Lay out files contiguously**: root directory, then `data/` directory cluster, `HELLO.COM` gets clusters 3–3 (1 cluster), `LEVEL1.BIN` gets clusters 4–6 (3 clusters). Because every file is one contiguous run, the FAT is **computed**, not stored: entry *k* = *k*+1 inside a run, end-of-chain at its last cluster.
4. **Generate directory sectors** in memory: short 8.3 names (unique `~1` suffixes), long-file-name entries, timestamps from mtimes. Names are encoded in **CP866** for NedoOS (`NedoOS src/fatfs4os/ffconf.h:60`); configurable.
5. **Serve reads on demand**: a sector inside a file's cluster run → `pread` from the host file (a small LRU of open handles); a directory/FAT/boot sector → generated.

Nothing is copied at mount time; a 2 GB folder mounts instantly.

#### 7.4.3 What happens to guest writes

- **Rollout 1: writes go to a session write map** (`SessionWriteMap`: sparse `lba → 512 bytes` in memory, DOSBox-X `bios_disk.cpp:922-967`, including "a write equal to the original frees the entry"). The guest sees its changes until the folder is detached or the emulator exits. The folder is never touched. The user can *export* the volume (folder + changes) to an `.img`. This covers the main use case: build on the PC, run in the Spectrum OS, no repacking. It is already more than xpeccy-plus (which drops folder writes) and equal to DOSBox-X. A `Folder0ReadOnly=1` option reproduces the xpeccy-plus behaviour (writes rejected with ABRT).
- **Rollout 2 (⚠️ requires further investigation): commit back to the folder, as a diff, never live.** The session write map is then replaced by the general COW layer (§7.3). On commit (emulator paused): parse the original synthesized volume and base + COW with a real FAT reader (ChaN FatFs, as NedoOS and jnext use; wrapped as a read-only block device), diff the two trees, and apply new/changed/deleted/renamed files to the folder. A file is only overwritten if its host mtime and size are unchanged since the snapshot; otherwise it is reported as a conflict. Then rebuild the volume and empty the layer. Commit is a TTD session boundary, because rebuilding moves clusters.
- **Rejected: QEMU-`vvfat`-style live write-through**, which interprets every sector write as it happens. It is known to be fragile under guest caching and write ordering, and it makes host side effects depend on timing, which breaks TTD determinism.

#### 7.4.4 Which guest file systems can use folder mode

| Guest file system | Used by | Folder mode |
|---|---|---|
| FAT16 / FAT32 with MBR | NedoOS, ZX-Evo Wild Commander, esxDOS (DivIDE), NextZXOS | **yes**: the whole target |
| `.trd` / `.scl` files stored on FAT | NedoOS `trdosfs`, WC, esxDOS | **yes**: they are ordinary files in the folder |
| Profi CP/M HDD partitions ("ProfiHiDD" header, CP/M directory) | Profi | **no**, raw image only. A synthesizer would need the Profi BIOS disk parameter format; possible later as a separate base |
| +3e / ResiDOS PLUSIDEDOS partitions | +3e, DivIDE | no (image only) |
| IS-DOS, "TR-DOS on HDD" containers (Nemo/SMUC era) | Nemo, SMUC, ATM | no (image only; layouts vary by tool) |

**Consequence for Profi:** folder mode is useful on Profi only for running FAT-aware software, of which none is known today. **Profi's path is raw images.** Folder mode is built for Nemo/Evo/ATM/DivIDE users and comes after the Profi adapter in the plan.

### 7.5 ATAPI CD-ROM

#### 7.5.1 Who supports it

| Emulator | ATAPI CD-ROM | Details |
|---|---|---|
| **UnrealSpeccy** (pentevo fork, `hdd.cpp`, `hddio.cpp`, `hddaspi.cpp`) | **yes** | `[HDD] CD0/CD1=1`. Real drives via Windows SPTI/ASPI pass-through; file images through an emulated subset: TEST UNIT READY, READ(10), READ TOC, START/STOP UNIT, SET CD SPEED (`hdd.cpp:705-782`), plus REQUEST SENSE and MODE SELECT handling. History: "IDE/ATAPI cdrom", several ATAPI DRQ/sense fixes (`doc/history.txt:640-759`) |
| **pico-spec** (`src/IDE.cpp`, 1 189 lines) | **yes, the most complete** | `.iso` detected by extension or `CD001` at `#8001`. ATA side: `#08` DEVICE RESET, `#A1` IDENTIFY PACKET, `#A0` PACKET. SCSI side (`IDE.cpp:873-1020`): TEST UNIT READY `#00`, REQUEST SENSE `#03`, INQUIRY `#12`, MODE SENSE(6) `#1A`, START STOP `#1B`, PREVENT/ALLOW `#1E`, READ CAPACITY `#25`, READ(10) `#28`, SEEK(10) `#2B`, SYNC CACHE `#35`, READ TOC `#43`, GET EVENT STATUS `#4A`, MODE SENSE(10) `#5A`, READ(12) `#A8`, SET CD SPEED `#BB`; sense keys ILLEGAL REQUEST / UNIT ATTENTION (medium changed) |
| **MAME** (`bus/spectrum/zxbus/nemoide.cpp:67`, `smuc.cpp:109`) | **yes** | the Nemo and SMUC cards accept MAME's generic `ATAPI_CDROM` device on either IDE slot (full SCSI multimedia command set from MAME's shared ATA bus) |
| ZXMAK2 (`AtaDevice.cs:382-423, 703-710`) | stub | ATAPI signature, `#A1`, `#A0` state machine ported from Unreal, but `handle_atapi_packet*` "not implemented" and the loader never sets the CD flag |
| Xpeccy (`hdd.h:35`) | enum only | `IDE_ATAPI` type, no command handling |
| zxsp (`IdeDevice.cpp:16, 1234`) | TODO | "ATAPI … TODO ESXDOS" |

**Conclusion:** three implementations work (UnrealSpeccy, pico-spec, MAME), so ATAPI is part of **rollout 1 parity**. pico-spec's command list is the target; it is a superset of UnrealSpeccy's.

#### 7.5.2 How it fits the design

- **Same channel, different personality.** A slot holds either `AtaDisk` or `AtapiCdrom` (§5). Adapters are unchanged: a CD-ROM is reached through the same ports on every board. Profi, Nemo, SMUC, ATM, Evo and DivIDE all get CD support for free.
- **Detection by guest drivers:** after reset or `#90`, an ATAPI device puts `#14/#EB` into cylinder low/high (the "ATAPI signature"). `#EC` IDENTIFY is aborted with that signature; drivers then send `#A1` IDENTIFY PACKET DEVICE.
- **Packet protocol (worked example: read one 2048-byte block from LBA 16):**
  1. Guest writes byte-count limit `2048` into cylinder low/high, then command `#A0` (PACKET).
  2. Drive sets DRQ, interrupt reason = "command"; guest writes the 12-byte CDB as 6 data words: `28 00 00 00 00 10 00 00 01 00 00 00` (READ(10), LBA 16, 1 block).
  3. Drive loads the block, sets cylinder low/high = 2048 (bytes available), interrupt reason = "data to host", DRQ.
  4. Guest reads 1024 data words; drive clears DRQ and reports completion (interrupt reason "status").
  5. Errors: status ERR, error register bits 7:4 = SCSI sense key; guest issues REQUEST SENSE (`#03`) for details.
- **Transfer buffer** grows from 512 bytes to one 2048-byte block per refill (pico-spec) rather than Unreal's 64 KB buffer: multi-block READs refill block by block, which keeps the state blob small.
- **Media change:** attaching a new ISO raises UNIT ATTENTION / "medium may have changed" once, as real drives do (pico-spec `ASC_MEDIUM_CHANGED`).
- **Read-only always.** No write commands. Audio-CD commands (PLAY AUDIO, READ CD raw) are out of scope; data CDs only.
- **Physical drive pass-through** (Unreal SPTI/ASPI): out of scope, like physical hard disks.

#### 7.5.3 Guest software

Known consumers: NedoOS and other ZX-Evo / Nemo system software with CD drivers (NedoOS ships Unreal-style `CD0=` config, `nedoos_en.md:799`), and IS-DOS-era CD tools. Collecting concrete test software is open question Q9.

---

## 8. Configuration and automation

### 8.1 `[HDD]` ini section (UnrealSpeccy-compatible keys + new ones)

```ini
[HDD]
Scheme=PROFI            ; NONE/ATM/NEMO/NEMO-A8/NEMO-DIVIDE/SMUC/PROFI/DIVIDE (existing enum)
Image0=profi_cpm.img    ; file path, or a directory path -> folder volume
CHS0=1024/16/16         ; geometry. 0/0/0 = auto (8.3)
LBA0=0                  ; 0 = from file size
HD0RO=0                 ; write-protect jumper (6.4)
CD0=0                   ; 1 = slot is an ATAPI CD-ROM (auto when Image0 is an .iso / has CD001) - Unreal key
FolderFs0=Auto          ; Auto / FAT16 / FAT32 / FAT16-NoMBR (folder volumes only)
Folder0ReadOnly=0       ; 1 = reject guest writes to a folder volume (xpeccy-plus behaviour)
;WriteBack0=Persistent  ; rollout 2 only: Persistent / Manual / Discard (7.3)
Model0=                 ; optional IDENTIFY model string override
Image1=                 ; slave, same keys with suffix 1
BusyTStates=0           ; 6.5
```

The `Scheme` must be valid for the machine: PROFI → Profi; SMUC → Scorpion/ProfROM; ATM → ATM 2+/ATM3; NEMO* / DIVIDE → Pentagon and similar; ZX-Evo has Nemo-DivIDE built in. An invalid pairing logs a warning and disables IDE. `data/configs/profi/unreal.ini` changes `Scheme=NEMO-DIVIDE` → `PROFI`.

### 8.2 Automation surfaces (parity with `disk`)

| Surface | Commands |
|---|---|
| CLI (rollout 1) | `hdd attach <0\|1> <path\|folder\|iso> [--ro] [--chs C/H/S] [--cd]`, `hdd detach <n>`, `hdd info`, `hdd export <n> <path>` (folder volume + session changes → `.img`), `hdd create <path> <MB> [--chs]`, `hdd regs` (debug: task file of both drives + adapter latches), `hdd sector <n> <lba>` (hex dump as the guest sees it), `cd insert <n> <iso>` / `cd eject <n>` (media change without detaching the drive) |
| CLI (rollout 2) | `hdd commit <n>`, `hdd discard <n>`, `--writeback persistent\|manual\|discard` |
| WebAPI | `/api/v1/emulator/{id}/hdd/...` mirroring the CLI |
| Lua / Python / MCP | same verbs as methods/tools |
| Notifications | `NC_HDD_STATE_CHANGED` (attach/detach/media change, commit in rollout 2; activity LED on reads/writes, diff-gated like `NC_FDD_STATE_CHANGED`) |
| Qt UI | rollout 1: an HDD menu with Attach image / Attach folder / Insert CD / Eject / Export, and an activity LED; rollout 2 adds Commit / Discard |

### 8.3 Geometry: why it matters and how "auto" works

The Profi loader (§3.6) addresses sectors by CHS and never asks the drive for its geometry. If an image was formatted on a drive with 16 heads × 16 sectors and the emulator claims 16 × 63, the CHS→LBA conversion differs and the ROM reads garbage. Order of resolution:

1. Explicit `CHS0` from the config / CLI.
2. Geometry stored in the image header (HDF/HDI).
3. **Profi auto-detect** (Profi scheme only): look for the byte-swapped `ProfiHiDD` header at LBA 256 (original SYS ROM: H=16, S=16) and at LBA 1008 (Karabas ROMs: H=16, S=63); the header's first words give H/S/C (pico-spec `IDE.cpp:238-276, 518-575`) [?Q4].
4. Default: largest standard CHS for the size (≤ 16383/16/63), as UnrealSpeccy.

---

## 9. Profi adapter pseudocode (complete)

```cpp
// PortDecoder_Profi::DecodePortIn, placed after the RTC arm and before the FDC arm
else if ((port & 0x9F) == 0x8B && IsExtMode())
{
    result = _ide.In(port);                   // IdeAdapterProfi
    _lastPortDecoded = true;                  // tag PortTag::StorageIde
}

uint8_t IdeAdapterProfi::In(uint16_t port)
{
    const uint8_t low = port & 0xFF, reg = (port >> 8) & 0x07;
    if (low == 0xEB) return _readLatch;                          // high byte of the last data read
    if (low == 0xCB)
    {
        if (reg != 0) return _channel.ReadRegister(reg);
        const uint16_t w = _channel.ReadData();
        _readLatch = w >> 8;
        return w & 0xFF;
    }
    return 0xFF;                                                 // #AB read, #8B: not driven
}

void IdeAdapterProfi::Out(uint16_t port, uint8_t v)
{
    const uint8_t low = port & 0xFF, reg = (port >> 8) & 0x07;
    if (low == 0xCB) { _writeLatch = v; return; }
    if (low == 0xEB)
    {
        if (reg != 0) _channel.WriteRegister(reg, v);
        else          _channel.WriteData(static_cast<uint16_t>((_writeLatch << 8) | v));
        return;
    }
    if (low == 0xAB && reg == 6) _channel.WriteRegister(kDeviceControl, v);
}
```

Placement check against existing Profi decode arms (no overlap): AY `(port & 0xC002)` needs A1=0 (`#8B` has A1=1); `#7FFD`/`#DFFD` need A1=0; RTC `(port & 0x9F) == 0x9F`; extended FDC `(p1 & 0x9F) == 0x83` and `(p1 & 0xE3) == 0x23`; `#FE` needs A0=0; palette `#xx7E` needs A0=0. In EXT mode the normal-mode FDC system-port alias `(p1 & 0xE3) == 0xE3` is not active, because `DecodeFDCPort()` returns early in the `rom14 && cpm` branch. **Order matters only for documentation; the decode sets are disjoint.** A static test enumerates all 65 536 ports × 8 mode combinations to prove this (§12.2).

---

## 10. TTD and snapshots — ⚠️ Rollout 2, requires further investigation

### 10.0 Rollout 1 interim rule (decided)

Rollout 1 ships **without** IDE TTD support, but must never produce a silently wrong recording. The precedent already exists: a WD1793 floppy write invalidates a TTD session (`wd1793.h:1171-1190`, TDD §12.2). Rollout 1 applies the same rule to IDE:

- While TTD is recording, the **first IDE command register write** (any drive, any command) invalidates the session with the reason "IDE activity: not covered by TTD yet". Reason: even a read changes controller state (registers, transfer position), which checkpoints would not capture.
- A machine with a drive attached but never accessed records normally.
- The `ttdmodelstatecontract_test` gets an explicit allow-list entry "IDE: invalidates on use (rollout 1)", so the contract stays enforced and the exception is visible.
- The exact hook point (adapter vs. channel) and user messaging are implementation details of rollout 1 phase R1-2.

Everything below is the **rollout 2 direction**. Open points to investigate first: blob layout and size with the 2048-byte ATAPI buffer; whether `mediaGeneration` + an undo journal or page references into the TTD v2 page store is the better model; replay of folder-volume reads (host file contents must be frozen at mount for determinism: is the folder snapshot enough?); snapshot file format for media references; cost on long recordings with heavy disk I/O.

### 10.1 Controller state

New `PeripheralId::AtaChannel = 10` (appended; never renumber). Fixed-size POD blob:

```cpp
struct AtaDeviceState            // per drive
{
    uint8_t  regs[16];           // task file + HOB copies (LBA48) + control + feature + command
    uint8_t  status, error, state /*idle/read/write/identify*/, intrq;
    uint16_t transferPos, transferCount;
    uint32_t curC; uint8_t curH, curS, heads, sectors;   // current translation (#91 changes it)
    uint64_t curLba, busyUntilTState;
    uint8_t  multipleCount, present, writeProtected, kind /*none/ata/atapi*/;
    uint8_t  atapiPhase, senseKey, asc, ascq;            // ATAPI packet state + sense (7.5)
    uint32_t atapiLba, atapiBlocksLeft;
    uint8_t  cdb[12];
    uint8_t  buffer[2048];                               // 512 used by ATA disks
};
struct AtaChannelState
{
    AtaDeviceState dev[2];
    uint8_t adapterLatch[4];     // read latch, write latch, toggle flags - meaning per adapter
    uint32_t mediaGeneration[2]; // COW journal position (10.2)
};
static_assert(sizeof(AtaChannelState) == /* fixed */, "AtaChannelState layout changed");
```

About 4.2 KB per checkpoint before compression (the registry compresses blobs; an idle buffer compresses to almost nothing). The ATAPI buffer size is one of the rollout 2 investigation points. `TTDHashState()` covers everything except `buffer` bytes beyond `transferCount`. Registered through `GetTTDModelStateIds()` of every decoder with an IDE scheme, so `ttdmodelstatecontract_test` enforces it.

The Profi `ProfiPagingState` does **not** grow: the reconciliation doc's §6.3 idea of adding `ideWriteLatch/ideReadLatch` there is superseded. The latches live in the shared channel blob, so every IDE model gets them.

### 10.2 Disk contents under TTD

- The base media never change during a session (§7.3), so a checkpoint only needs `mediaGeneration` = the COW journal length at that moment.
- **Seek backward**: undo journal entries newer than the target generation (each entry holds the previous slot contents). **Seek forward / replay**: the guest re-executes the same writes deterministically, or entries are re-applied.
- Commit during recording is a session boundary (it changes the base). The UI blocks it or warns while recording.
- This replaces the floppy-style "disk write invalidates the recording" rule for IDE, which fulfils roadmap ST-1..ST-3. The same journal can later serve floppies (ST-4).

### 10.3 File snapshots

The in-RAM snapshot serializer reuses the TTD blob (as for other peripherals). Snapshots store the media **reference** (path + `ContentId()`) plus the COW slots, not the full disk. Loading a snapshot whose base `ContentId` does not match warns and refuses by default.

**Rollout 1:** snapshot formats (`.sna`, `.z80`, `.szx`) carry no IDE state, as in every reference emulator (none of UnrealSpeccy, ZXMAK2, Xpeccy saves ATA state). Loading a snapshot leaves attached media as they are and hard-resets the drives.

---

## 11. Changes to existing code

| File | Change |
|---|---|
| `io/hdd/hdd.{h,cpp}`, `hddio.h` | delete; replaced by §5 layout |
| `platform.h` | keep `IDE_SCHEME`, extend `IDE_CONFIG` (writeback, folder fs, model); remove the five `ide_*` latch bytes from `EmulatorState` (they move into adapters + TTD blob) |
| `config.cpp` | parse `[HDD]` (§8.1) |
| `cpu/core.{h,cpp}` | `_hdd` → `AtaChannel` |
| `portdecoder_profi.{h,cpp}` | `IdeAdapterProfi _ide`; IN/OUT arms (§9); TTD id list |
| `portdecoder_scorpion256.cpp` | phase 3: replace the `_smucIdeRegs` stub with `IdeAdapterSmuc` (keeps `SMUCNvram`); board-absent behaviour unchanged |
| Pentagon / ATM / Evo decoders | phase 3–4: add adapters per `Scheme` |
| `ttdserializable.h` | rollout 2: `AtaChannel = 10` |
| `wd1793`-style TTD invalidation hook, `ttdmodelstatecontract_test` | rollout 1: "IDE activity invalidates recording" + allow-list entry (§10.0) |
| automation (`cli`, `webapi`, `lua`, `python`, `mcp`) | `hdd` verbs (§8.2) |
| `data/configs/profi/unreal.ini` | `Scheme=PROFI` (CRLF file) |

---

## 12. Test methodology

The goal is to test every behaviour once, at the lowest layer where it can be seen, and to cover every controller variant with the **same** table-driven tests wherever possible. All tests are in `core/tests/emulator/io/hdd/`. They use synthetic images built in the test (no binary fixtures) except the ROM-driven layer. Temporary files go to a per-test scratch directory, removed on teardown.

### 12.1 Layer 1: disk-core conformance (`atadevice_test.cpp`, `atachannel_test.cpp`)

Drives `AtaDevice`/`AtaChannel` directly over a `MemoryDisk` whose sector *n* is filled with a known pattern (`lba` in the first 4 bytes, then a counter).

| Group | Cases |
|---|---|
| Power-on / reset signature | after hard reset, SRST pulse, `#90`: count=1, sector=1, cyl=0, error=`#01`, status `DRDY\|DSC` |
| IDENTIFY | word 0, C/H/S, LBA28 capacity (incl. images ≥ 8 GB → LBA48 words), checksum byte 511 makes the sector sum 0, strings byte-swapped per ATA |
| Read/write, CHS and LBA | 1 sector, N sectors, count=0 means 256, crossing a head and a cylinder boundary, register values after the transfer |
| Geometry | `#91` changes the translation; out-of-range CHS/LBA → IDNF; count/sector=0 edge cases |
| Multiple | `#C6` then `#C4/#C5` with block 1/2/16; invalid block → ABRT |
| Errors | unknown command → ABRT; write to a write-protected drive → ABRT; command while not selected is ignored |
| Master/slave | both present; master only (slave status reads `#00`); slave only; none (`#FF`); writes land on both register files |
| INTRQ | set after command/DRQ block, cleared by status read, not by alt-status read; nIEN masks it |
| Data-phase discipline | data read without DRQ returns `#FFFF`/no side effects; partial sector then new command resets the transfer |
| Busy model | with `BusyTStates=n`: BSY visible for exactly *n* T-states; DRQ only after |
| State round-trip | `SaveState` → fresh device → `LoadState` mid-transfer (e.g. after word 100 of 256) → identical remaining data. Written in rollout 1 (the POD state exists from day one); consumed by TTD in rollout 2 |

**ATAPI conformance (`atapicdrom_test.cpp`)**, over a synthetic ISO (a `MemoryDisk` with `CD001` at block 16 and numbered blocks):

| Group | Cases |
|---|---|
| Signature | after reset / `#90` / `#08`: cylinder = `#EB14`; `#EC` aborts with the signature; `#A1` returns IDENTIFY PACKET (word 0 = `#85C0`: ATAPI, CD-ROM, 12-byte packets) |
| Packet protocol | `#A0` → DRQ + interrupt reason "command"; 6 CDB words; then data phase with byte count in cylinder registers; completion with interrupt reason "status" |
| SCSI commands | each command in the pico-spec list (§7.5.1): TEST UNIT READY, INQUIRY (vendor/product strings), READ CAPACITY (last LBA, 2048), READ(10)/(12) single + multi-block + byte-count limit smaller than a block, READ TOC (one data track, lead-out), MODE SENSE(6/10), REQUEST SENSE after an error, START STOP, SET CD SPEED |
| Errors | unknown opcode → CHECK CONDITION, sense ILLEGAL REQUEST / invalid opcode; LBA beyond capacity → LBA OUT OF RANGE; no medium → NOT READY |
| Media change | eject + insert → first TEST UNIT READY reports UNIT ATTENTION, the next one succeeds |
| Mixed channel | master = hard disk, slave = CD-ROM: each answers with its own personality; writes to the CD are rejected |

### 12.2 Layer 2: per-adapter port truth tables (`ideadapter_<board>_test.cpp`)

**One shared parameterized fixture**, instantiated once per board. Each board provides a descriptor table:

```cpp
struct IdePortCase { const char* name; uint16_t port; bool isWrite; MachineMode mode; Expect expect; };
// Expect = {DataLow, DataHigh, LatchHigh, TaskReg n, ControlReg, NotDecoded}
```

For each board the fixture runs:

1. **Decode matrix:** every task-file register read and write through the board's documented port, in every mode (Profi: EXT on/off × DOS latch × DS80; SMUC: TR-DOS on/off × `#FFBA.7`; Nemo: DOS ports on/off; ATM: shadow on/off). Expected register reached, or "not decoded" when gated off.
2. **Exhaustive no-collision sweep:** all 65 536 ports × all modes. Assert every port is claimed by **at most one** decode arm of that machine's `PortDecoder` (IDE, RTC, FDC, AY, `#FE`, paging…). This catches alias bugs such as the `#EB` / Beta128 `#FF` alias (§3.2) before they ship.
3. **Word order:** write the word `#ABCD` then read it back through the board's latch pattern; the disk must see `#ABCD`, and the image bytes must be `CD AB`.
4. **Latch edge cases:** Profi: `#xxCB` write with A10..A8 ≠ 0 still latches; `#xxEB` read with any A10..A8 returns the latch; the read latch is unchanged by a non-data register read. Nemo-DivIDE/Evo/DivIDE: the toggle resets on any other register access; Nemo `#11` sequences still work on DivIDE-Nemo.
5. **Reset:** machine reset clears latches and resets drives; SRST via each board's control path (Profi `#06AB`, Nemo `#C8`, SMUC `#FEBE` with `#FFBA.7`).
6. **Gate transitions mid-sequence:** leave EXT mode between the two halves of a word; the latch keeps its value and the next word is still correct.

The same fixture file runs for every board, so a new board is "write the table, get ~200 checks".

### 12.3 Layer 3: storage backends (`blockdevice_test.cpp`, `cowblockdevice_test.cpp`, `hostfolderfat_test.cpp`)

| Backend | Cases |
|---|---|
| Raw | size not a multiple of 512; read past end → error; write-through lands at `lba × 512` and survives detach/re-attach; write-protected image rejects writes with ABRT and the file hash is unchanged |
| HDF/HDI/VHD | header parsing, data offset, embedded geometry, 8-bit halved HDF (`#AA` stored as `AA`, read back as `AA 00`), VHD footer ignored for data, rejected when not "fixed" |
| ISO | `CD001` detection with and without `.iso` extension; 2048-byte blocks; writes impossible |
| Session write map (rollout 1) | write → read-back; folder untouched; write-equal-to-original frees the entry; detach drops changes; export = folder volume + changes, byte-exact |
| COW (rollout 2) | write → read-back; base untouched (file hash unchanged); commit (image hash changes as expected); discard; export → byte-exact image |
| Journal (rollout 2) | apply/undo to generation N reproduces the exact overlay |
| Host folder (build) | a generated volume is **parsed by an independent FAT reader** (bundled ChaN FatFs in the test build): same file list, sizes, contents, directory tree, long and 8.3 names, CP866 names; FAT16/FAT32 thresholds; empty folder; 0-byte file; > 512 files in one directory; deep nesting; name collisions (`LONGNAME1.TXT`/`LONGNAME2.TXT` → `LONGNA~1`/`~2`) |
| Host folder (behaviour) | host file changed after mount → volume unchanged (snapshot rule); guest writes → folder unchanged; `Folder0ReadOnly=1` → writes aborted; refresh blocked while the session write map is not empty |
| Host folder commit (rollout 2) | guest creates/renames/deletes/modifies files (driven through FatFs on the COW device) → commit → folder matches; host-side conflict is detected and reported |

### 12.4 Layer 4: TTD and snapshots (`ttdatachannel_test.cpp`)

**Rollout 1 (interim rule, §10.0):**

- Recording with a drive attached but idle → session stays valid.
- First IDE command while recording → session invalidated with the IDE reason; same for a CD-ROM PACKET.
- Contract test: every IDE-capable model is on the explicit "invalidates on use" allow-list; removing the entry without registering a serializer fails the test.
- Snapshot load hard-resets the drives and keeps the attached media.

**Rollout 2 (⚠️ needs further investigation; the list is provisional):**

- Contract: every model with an IDE scheme lists `PeripheralId::AtaChannel` (extends `ttdmodelstatecontract_test`).
- **Rewind across a write:** record; guest writes sector X (value A → B); seek to before the write; read X → A; replay forward → B. Also mid-transfer checkpoints (half a sector moved).
- Hash sensitivity: changing any register, latch, or `mediaGeneration` changes `TTDHashState()`; the transfer buffer beyond `transferCount` does not.
- Snapshot save/load round trip with a COW layer present; mismatched base `ContentId` is refused.

### 12.5 Layer 5: real firmware (`core/tests/emulator/machines/profi/profi_hdd_test.cpp`, etc.)

These need the ROMs in `testdata/machines/` (currently untracked for Profi; committing them is a separate decision). The tests skip cleanly when a ROM is absent.

| Id | Machine / ROM | Test |
|---|---|---|
| **R1** | Profi, `profi_mainrom_standart.rom` | Build an image, geometry 16/16 (Q4): C1/H0/S1 header with heads=4 and spt=16 in the *byte-swapped* positions (image byte 1 = heads, byte 3 = spt); C1/H0/S6 with count N=2 at image byte 1 and a payload; S7–S8 more payload. Page in the SYS ROM, set PC=`#28CE`, run until PC=`#0100` (frame budget), assert memory `#00FE..` equals the payload in Profi byte order. Proves gate, port roles, latch order, CHS, `#90/#10/#21`, SRST |
| **R2** | Profi, same ROM, **no drive** | same entry; assert the loader reaches its error exit (PC parked at `#80A4`, `#C000..#FFFF` filled with `#C2`) within the frame budget. Proves `#FF` on an empty channel |
| **R3** | Profi, `ZXMAK2/ROMS/PROFI/profi-hddboot.rom` | cross-check boot with a second ROM build (needs a boot image; source TBD, Q6) |
| **R4** | Scorpion ProfROM + SMUC | the existing "IDE controller found" probe now passes through the real disk core, and prints the IDENTIFY model string (ProfROM page 4 `#0BC0` flow) |
| **R5** | ZX-Evo / Pentagon + Nemo, NedoOS | boot NedoOS from a FAT image; then from a **host folder** containing the NedoOS system files. Screen-hash or port-trace checkpoint |
| **R6** | Profi CP/M | if a real Profi HDD CP/M image is found (Q6): boot to the `A>` prompt, `DIR` lists files |
| **R7** | ZX-Evo / Pentagon + Nemo, NedoOS | CD-ROM on the slave: NedoOS lists the files of a test ISO (depends on Q9: which NedoOS build/driver reads CDs) |

### 12.6 Layer 6: cross-emulator differential (tooling, not CI)

A small Z80 test program (assembled with the in-repo assembler) runs a fixed IDE script: reset, identify, write a pattern to LBA 0..63, read it back, CHS walk, error cases. It dumps the task file after each step to RAM. Run the same `.sna` in unreal-ng and ZXMAK2/Xpeccy (per board); compare the dumps. Differences are either a documented choice (§3.7 table) or a bug. Results go into `docs/inprogress/.../ide-differential-results.md`.

### 12.7 Layer 7: automation and robustness

- CLI/WebAPI smoke: attach, info, sector dump, export, CD insert/eject, detach (commit/discard in rollout 2) on each surface (same pattern as the `disk` command tests).
- Fuzz: random port I/O sequences (seeded) against each adapter + disk core: never crash, never write outside the image size, the state blob always round-trips.
- Performance: sustained 1 MB read through the Profi adapter costs < 1 % extra host CPU over an idle machine (the port-decode arm must not show up in profiles).

### 12.8 Coverage matrix (what proves what)

| Variance | L1 | L2 | L3 | L4 | L5 |
|---|---|---|---|---|---|
| Port decode per board | | ✔ | | | ✔ |
| Gating per mode | | ✔ | | | ✔ (R1 EXT) |
| Latch pattern (fixed/mirror/toggle) | | ✔ | | | ✔ |
| CS1 path per board | | ✔ | | | ✔ (R1 SRST) |
| Command set / errors | ✔ | | | | ✔ |
| CHS/LBA/geometry | ✔ | | | | ✔ (R1) |
| Master/slave/empty | ✔ | ✔ | | | ✔ (R2) |
| Image formats | | | ✔ | | |
| Folder volume | | | ✔ | | ✔ (R5) |
| ATAPI CD-ROM | ✔ | ✔ (same ports) | ✔ (ISO) | ✔ (invalidate) | ✔ (R7) |
| Write-through / session map (R1) | | | ✔ | | |
| TTD interim rule (R1) | | | | ✔ | |
| COW / commit / discard (R2) | | | ✔ | ✔ | |
| TTD / snapshots (R2) | ✔ (state) | | | ✔ | |

---

## 13. Open questions

| # | Question | Default until answered |
|---|---|---|
| Q1 | `#06AB` read on real Profi: `#FF` (RTL) or alternate status (pico-spec)? | `#FF` |
| Q2 | Karabas EXT variant (DOS ∧ ¬ROM14) as a whole-decoder switch: needed for Karabas BIOS HDD self-test | not implemented (§3.2) |
| Q3 | SMUC IDE reset polarity (`#FFBA` bit 0: Unreal/ZXMAK2 = 1 resets, MAME = 0 resets) and version-register values (four emulators, four answers) | Unreal, matching the existing SMUC stub |
| Q4 | Profi disk geometry conventions: confirm 16/16 (original SYS ROM) vs 16/63 (Karabas) and the `ProfiHiDD` header offset/layout against a real image | configurable; auto-detect behind a flag |
| Q5 | Which SYS ROM menu path reaches `#28CE` (for an end-to-end "press key → boots from HDD" test) | R1 enters `#28CE` directly |
| Q6 | Source of a real Profi HDD image (CP/M) for R3/R6 | synthetic images only |
| Q7 | ATM 2+: any CS1 access at all? | none |
| Q8 | Folder-mode default FAT type for NedoOS/esxDOS | FAT16 < 2 GB |
| Q9 | Which Spectrum software actually reads CDs (NedoOS driver build, IS-DOS tools, ZX-Evo utilities) for the R7 test | ATAPI tested at conformance level only |
| Q10 | ⚠️ **Rollout 2 investigation**: COW layer design (§7.3) and TTD integration (§10) — granularity, memory ceiling / spill, journal reuse, commit during recording, folder-read determinism, snapshot format, floppy (ST-4) unification | rollout 1 interim rule (§10.0) |

---

## 14. Implementation plan — phased rollout

The plan is also summarized in [2026-09-25-profi-reconciliation.md](2026-09-25-profi-reconciliation.md) §9. Each phase ends with a green `core-tests` run; nothing is committed without an explicit request.

### 14.1 Rollout 1 — IDE on par with other emulators

Goal: everything UnrealSpeccy / ZXMAK2 / Xpeccy / xpeccy-plus / pico-spec / DOSBox-X offer for Spectrum IDE, in one coherent implementation: all boards, image files, host folders, ATAPI CD-ROM, automation. TTD safety comes from the interim rule (§10.0), not from full support.

| Phase | Content | Tests | Size |
|---|---|---|---|
| **R1-1 Disk core** | `ataregisters.h`, `AtaDevice` engine + `AtaDisk`, `AtaChannel`, `IBlockDevice`, `RawImage` (write-through), `MemoryDisk`, POD state; delete the skeleton; `Core` owns the channel | L1 (ATA part) | M |
| **R1-2 Profi adapter** | `IdeLatch` helpers, `IdeAdapterProfi`, decoder arms, `[HDD]` parsing, `Scheme=PROFI` in the Profi config (CRLF file), `PortTag::StorageIde`; **TTD interim rule** (§10.0) | L2 Profi + collision sweep; R1, R2; L4 rollout-1 cases | S–M |
| **R1-3 Automation** | CLI / WebAPI / Lua / Python / MCP `hdd` and `cd` verbs, `NC_HDD_STATE_CHANGED` | L7 smoke | S–M |
| **R1-4 Other boards** | Nemo (+A8, DivIDE/Evo toggle), SMUC (replaces the Scorpion stub), ATM (+INTRQ bit); per-model scheme validation | L2 per board; R4, R5 (image) | M |
| **R1-5 Image formats** | HDF, HDI, fixed VHD; Profi geometry auto-detect (§8.3) | L3 | S |
| **R1-6 Host folders** | `FolderSnapshot`, `FatBuilder`, `HostFolderFat`, `SessionWriteMap`, export to `.img`, `Folder0ReadOnly`; FatFs in the test build | L3 folder cases; R5 (folder) | M–L |
| **R1-7 ATAPI CD-ROM** | `AtapiCdrom` (pico-spec command list), `IsoImage`, `CD0/CD1`, `cd insert/eject` | ATAPI conformance; R7 (if Q9 answered) | M |
| **R1-8 Tooling + UI** | differential harness (§12.6), fuzz, perf check; Qt HDD/CD menu + activity LED | L6, L7 | S–M |

**Profi-critical path:** R1-1 → R1-2 closes **G1**. R1-3 gives automation parity. R1-4 … R1-8 can be scheduled independently in any order; R1-6 (folders) and R1-7 (CD) only depend on R1-1.

**Parity check at the end of rollout 1:**

| Capability | Best reference | unreal-ng after rollout 1 |
|---|---|---|
| Boards | UnrealSpeccy (8 schemes) | Profi, Nemo, Nemo-A8, Nemo-DivIDE/Evo, SMUC, ATM (DivIDE adapter possible, but DivIDE paging/automap is a separate project) |
| Image formats | pico-spec (raw, HDF, VHD, ISO) | raw, HDF, HDI, fixed VHD, ISO |
| Host folder | DOSBox-X (FAT, in-memory writes) / xpeccy-plus (FAT32 read-only) | FAT16/FAT32, in-memory writes or read-only, export |
| CD-ROM | pico-spec (15 SCSI commands) | same list |
| Write behaviour | write-through + write-protect (Unreal) | same |
| Snapshots / TTD | none of them | TTD safe (invalidates on IDE use); snapshots reset drives |

### 14.2 Rollout 2 — TTD and disk change layers (⚠️ requires further investigation)

Starts with an investigation phase; the later phases are provisional and will be re-planned from its findings.

| Phase | Content | Output |
|---|---|---|
| **R2-0 Investigation** | Answer Q10: COW granularity vs. the TTD v2 page store, memory ceiling / spill-to-disk, journal reuse (`ttdwritejournal`), commit during recording, folder-read determinism, snapshot media references, floppy (ST-4) unification, blob size with the ATAPI buffer | an addendum to this document + a revised R2 plan |
| R2-1 (provisional) | `CowBlockDevice` + write journal; `WriteBack` policy (`Persistent` / `Manual` / `Discard`) replacing write-through and `SessionWriteMap` | L3 COW / journal |
| R2-2 (provisional) | `PeripheralId::AtaChannel` serializer + hash; remove the interim invalidation rule | L4 rollout-2 cases; closes **T3** |
| R2-3 (provisional) | snapshot media references; folder commit-back (FatFs diff + conflict detection) | L3 folder commit cases |
