# Sprinter Sp2000 — hardware reference for the emulator

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Draft for review. Facts are sourced; items marked **unverified** need a test or a second source |
| **Sources** | short names from [materials.md](materials.md): MAN (designer's manual), BIOS-TT, INC, DSS, DSS-162, PLD, MAME, ZXMAK2, SPRINTEM |
| **Glossary** | [README.md](README.md#glossary) |

This file describes the machine only as far as the emulator needs it. Addresses use the Z80
habit `#xxxx` for hex. "Page" always means a 16 KB block.

## 1. The machine on one page

The Sprinter is a ZX Spectrum-compatible computer whose logic lives in one reprogrammable chip
(an Altera ACEX EP1K30 "PLD"). The PLD is loaded from ROM at power-on, so almost everything,
including the port addresses, can change at run time (MAN §1.2-1.4).

| Part | Sp2000 value | Source |
|---|---|---|
| CPU | Zilog **Z84C15**: a Z80 with an on-chip timer (CTC), two serial ports (SIO), a parallel port (PIO), watchdog and chip-select logic | MAN §1.1; MAME `sprinter.cpp:1957`, `emulators/github/mame/src/devices/cpu/z80/tmpz84c015.cpp:22-27` |
| Clock | 3.5 MHz normal, **21 MHz** turbo (×6); master clock 42 MHz | MAN §1.1, §4.9; MAME `sprinter.cpp:178`, `:384-388` |
| RAM | 4 MB SIMM on the Sp2000 (board takes 4-64 MB); 256 pages addressed by one byte | MAN §1.1, §3.1; MAME `sprinter.cpp:254`, `:1529`. Pages above `#FF` are not addressable by the page registers (the registers are 8 bits); 64 MB support is **unverified** and out of scope |
| Fast RAM ("cache") | 64 KB, no wait states at 21 MHz | MAN §1.1, §7; MAME `sprinter.cpp:115`, `:320-335` |
| ROM | 256 KB flash, 16 pages | MAN §1.1; MAME `sprinter.cpp:2027` |
| Video RAM | 256 KB (512 KB option) | MAN §1.1, §4 |
| Video modes | per 8×8 square: 320×256×256 colors, 640×256×16 colors, text 80×32, Spectrum screen; 4+4 palettes of 256 × 24-bit | MAN §4.5-4.8 |
| Sound | AY-3-8910 clone in the PLD; beeper; 8-bit Covox; Covox-Blaster (buffered, up to 16-bit stereo); 16-bit stereo DAC TDA1543 | MAN §5; MAME `sprinter.cpp:2012-2020` |
| Floppy | КР1818ВГ93 (WD1793 clone) as a Beta Disk interface; 3.5" 1.44 MB / 720 KB, 5.25" 720 KB. HD by clocking the chip at 2 MHz with a 500 kbit/s separator (§10) | MAN §1.1, §10; MAME `sprinter.cpp:14-15`, `beta_m.cpp:26-39` |
| Hard disk | IDE, **two channels** × master/slave, ATAPI CD supported by the BIOS | BIOS-TT `EXTENDED/IDE/ATA_DRV.ASM:4` ("ADD SECONDARY CHANEL"); MAME `sprinter.cpp:1967-1969` |
| Keyboard | PC AT keyboard; the PLD turns it into a ZX key matrix, and the CPU's serial port A receives the raw scan codes | MAN §9.4; PLD `KBD.TDF`; DSS-162 `keyinter.asm:859-860` |
| Mouse | serial MS mouse on the CPU's serial port B, also presented as a Kempston mouse | MAN §9.1; INC `SP2000.inc:382-399`; MAME `sprinter.cpp:644-659` |
| Clock chip | Dallas DS12887A (MC146818-style RTC + CMOS) | MAN §12; MAME `sprinter.cpp:1966` |
| Expansion | two ISA-8 slots | MAN §1.1, §8 |

## 2. CPU, clock, wait states

| Rule | Detail | Source |
|---|---|---|
| Normal speed | 42 MHz / 12 = 3.5 MHz | MAME `sprinter.cpp:1957` |
| Turbo | ×6 = 21 MHz; switched by the CNF/SYS port (§5) when bit 1 = 1: bit 0 = turbo on/off; a keyboard "turbo" key (F12 in MAME) can force it off | INC `SP2000.inc:226-300`; MAME `sprinter.cpp:864-871`, `:1767-1771`; PLD `KBD.TDF` output `KB_F12` |
| Memory waits in turbo | main RAM is not fast enough for 21 MHz: every RAM access is stretched to the next 6-clock slot (MAME models "align to a multiple of 6 clocks, then +6 − cycle length"). Fast RAM has no waits | MAN §1 ("КЭШ … без тактов ожидания"); MAME `sprinter.cpp:1720-1731` |
| Port waits in turbo | the DCP lookup costs a RAM access; in turbo the PLD holds WAIT "depending on the needed cycle length" | MAN §13.1; MAME `sprinter.cpp:581`, `:704` (`do_mem_wait(4)`) |
| "Original ZX waits" | ALL_MODE bit 2 = 0 (and no turbo): accesses to `#4000-#7FFF`, or to window 3 while `#7FFD` bit 2 is set (`V_RAM = PN2`: pages 4-7), wait while the video counter bit `CT5` = 0: a fixed **4 T** cycle (`CT[2..0]` counts mod 6: 24 clocks low, 24 high), the same on every line, not the ULA pattern: 2, 1, 0 or 0 T by where T2 falls. Set by the launcher's `/origin` (ALL_MODE `#FA`) | PLD `SP2_ACEX.TDF:558-559` (`WAIT_ORIG`, the `UPDATE` build; that the release was built from it is unverified), `VIDEO2.TDF:280-298`, `DCP.TDF:577`; INC `SP2000.inc:550-560`; [research-zx-mode.md](research-zx-mode.md) §7.3. Modeled in unreal-ng since 2026-10-02 (`SprinterOrigWaits`, phase placeholder; S8 Z3), not by MAME |
| Interrupt vector | the INT acknowledge reads `#FF` (IM 2 tables must cover it) | MAME `sprinter.cpp:1961` |

## 3. Memory

### 3.1 Windows and pages

The CPU sees four 16 KB windows. Each window shows one physical page, chosen by a page register
that lives **inside the PLD** as one of its "internal ports" (§4.3):

| Window | CPU range | Register (internal code) | Standard external port | Notes |
|---|---|---|---|---|
| 0 | `#0000-#3FFF` | ROM / RAM / fast RAM, see §3.3 | `#82` (PAGE0) for RAM | RAM here is read-only unless the Scorpion `#1FFD` bit 0 says "RAM at 0" and the system mode allows it (MAME `sprinter.cpp:351-357`) |
| 1 | `#4000-#7FFF` | `#E9` | `#A2` (PAGE1) | |
| 2 | `#8000-#BFFF` | `#EA` | `#C2` (PAGE2) | |
| 3 | `#C000-#FFFF` | `#F0-#FF` (16 cells) | `#E2` (PAGE3) | the cell used is `#F0 + Spectrum page`, so `#7FFD`/`#1FFD` keep working (§3.2) |

Sources: MAN §3.1, §13.2 (p. 31); INC `SP2000.inc:527-534`, `:1520-1560`; MAME `sprinter.cpp:360-379`.
The page registers can be read back (`IN A,(#E2)`), which the BIOS uses to save and restore
windows (MAN §13.2: "Порты страниц ОЗУ открыты как на запись, так и на чтение").

### 3.2 Spectrum compatibility of window 3

A write to any internal code `#F0-#FF` lands in the cell `#F0 + p`, where `p` is the Spectrum
page selected by `#7FFD` and `#1FFD` (MAN §13.1 p. 31; INC `SP2000.inc:1540-1545`). MAME computes
the cell index `pg3` from `#7FFD` bits 0-2, 6-7 and `#1FFD` bit 4 (`sprinter.cpp:366`). So a
Pentagon program that writes `#7FFD` = 3 sees whatever physical page the BIOS stored in cell `#F3`.

**Worked example.** The BIOS sets cell `#F3` = `#23`. A game does `LD BC,#7FFD : LD A,#13 : OUT (C),A`
(page 3, ROM 48). The decoder routes `#7FFD` to internal code `#C1`, which stores `#13`; window 3
now shows physical page `#23`. If the game then does `OUT (#E2),A` with A = `#40`, the value goes
into cell `#F3` (not `#F0`), so the Spectrum page 3 is from now on backed by page `#40`.

### 3.3 Window 0: ROM, RAM, fast RAM

| Mode | Selected by | Window 0 shows | Source |
|---|---|---|---|
| System ROM | `#7C` write (SYS port "ROM on") | ROM page `ROM_RG[3:0] XOR (!SYS_PG << 3)`: 16 ROM pages, bit 3 inverted unless the "page" bit is set | INC `SP2000.inc:246-300`; MAME `sprinter.cpp:322-329`, `:691-703` |
| ROM page select | `#5C` write (only while the SYS ROM is on) or internal `#8F`; bit 4 = flash write enable | ROM page number | INC `SP2000.inc:350-380`; MAME `sprinter.cpp:698-702`, `:816-820` |
| Fast RAM | `IN A,(#FB)` = on, `IN A,(#7B)` = off ("nailed in the config"); page from `ROM_RG[1:0]` | one of 4 fast RAM pages | INC `SP2000.inc:350-354`; MAME `sprinter.cpp:576-580`, `:330-335` |
| Spectrum mode | `#3C` write (SYS port "RAM"), `#1FFD`, `#7FFD` bit 4, DOS | a RAM page from the cells `#E0-#E7`, `#EB`, `#EF` ("vROM": the Spectrum ROM images live in RAM pages, by default `#42-#47`) | INC `SP2000.inc:268-290` (table of `#E0-#EF`); MAME `sprinter.cpp:338-357` |

The "vROM" scheme is the key to Spectrum mode: the BIOS copies BASIC 128, BASIC 48, TR-DOS and
the extension ROM into RAM pages and points cells `#E0-#E7` at them; the PLD then picks the cell by
`#7FFD` bit 4, `#1FFD` bit 1 and the DOS signal (INC `SP2000.inc:268-290`). Those RAM pages are
write-protected while mapped as ROM (MAME `sprinter.cpp:355-357`). Pages `#42-#47` and "Spectrum page n =
physical page n" are the Peters Plus launcher's layout; the community BIOS (3.06+, `ZX_MEMORY_MANAGER`) allocates
both the vROM and the Spectrum RAM pages from free memory, so only the cells say where they are
([research-zx-mode.md](research-zx-mode.md) §5.3).

### 3.4 Special physical pages

| Page(s) | Meaning | Source |
|---|---|---|
| `#40` | **port table** (DCP, §4) | MAN §13.1; INC `SP2000.inc:627`; MAME `sprinter.cpp:1531` |
| `#41` | BIOS "special page" (reset intercept, flags) | INC `SP2000.inc:639-665` |
| `#42-#47` | default vROM images (BASIC 128/48, TR-DOS, extension, ZX BIOS) | INC `SP2000.inc:283-290` |
| `#50-#5F` | **graphics area**: while mapped, CPU addresses become video addresses (§6.2); bits 3 and 2 of the page number select sub-modes | MAN §3.2, §4.3; MAME `sprinter.cpp:1175-1205` |
| `#A0` | writing to it while `#1FFD` = `#10` (Scorpion extended page) resets the machine | MAN §14; INC `SP2000.inc:667-677`; MAME `sprinter.cpp:1190-1191` |
| `#D0-#DF` | ISA access instead of RAM, when `#1FFD` bit 4 is set (§11) | MAN §8; INC `SP2000.inc:596-602`; MAME `sprinter.cpp:369-373` |
| `#FD` | Covox-Blaster buffer page for accelerator transfers | INC `SP2000.inc:138`; MAME `sprinter.cpp:1069-1087` |
| `#FE` | BIOS system variables (`SYS_PAGE`) | INC `SP2000.inc:696` |
| `#FF` | page shared with DSS | INC `SP2000.inc:631` |

## 4. The programmable port decoder (DCP)

### 4.1 How a port access is decoded

Every external port access first reads **one byte from RAM page `#40`**. That byte ("DCP code")
says which internal device answers. Changing the byte moves or removes a port with no PLD reload
(MAN §13.1 p. 27-28). The byte address inside page `#40` is built from bus signals:

| Table address bit | Signal | Meaning |
|---|---|---|
| 13-12 | CNF[1:0] | which of the **4 maps** (CNF port values `#04`, `#0C`, `#14`, `#1C`) |
| 11 | PN5 | `#7FFD` bit 5 (the "48K lock"): lets a map hide `#7FFD` after the lock |
| 10 | /DOS | 0 = TR-DOS ROM active, 1 = not |
| 9 | /WR | 1 = read (`IN`), 0 = write (`OUT`) |
| 8 | A15 | |
| 7 | A14 | |
| 6 | A6 | |
| 5 | A5 | |
| 4 | A13 | |
| 3 | A7 | |
| 2-0 | A2-A0 | |

Sources: MAN §13.1 p. 27-28 (the signal list, a worked example for port `#7785`); BIOS-TT
`bios/exp/DCP.ASM:1-19` (record format, same bit order); MAME `sprinter.cpp:584` (read) and `:706`
(write), identical formula. ZXMAK2 builds the index without the CNF and PN5 bits
(`SprinterMMU.cs:534`, `:569`), which is a simplification.

Address lines **A3, A4 and A8-A12 never take part**. So `#50` and `#58` are always the same port
(MAN p. 28), and A8 is free for devices to use themselves (the IDE latch uses it, §9).

**Worked example (MAN p. 28).** Port `#7785` in map 0: A0=1, A1=0, A2=1, A7=1, A13=1, A5=0, A6=0,
A14=1, A15=0 → index bits 8-0 = `0 1001 1101` = `#09D`. The byte for "write, DOS on, PN5=0" is at
`#C000 + #09D` when page `#40` sits in window 3; reads add `#200`, "DOS off" adds `#400`.

**Worked example (IDE channel select).** The BIOS does `LD A,#21 : OUT (#BC),A` to pick the primary
channel (INC `SP2000.inc:1935-1940`). An `OUT (n),A` puts A on A15-A8, so the address is `#21BC`:
A13 = 1, A14 = A15 = 0, low byte `#BC`. The BIOS table maps that pattern (write only) to internal
code `#2B`; with A = `#01` A13 = 0 and the code is `#2A`, the secondary channel (BIOS-TT
`DCP.ASM:224-234`).

### 4.2 Start-up state

- After a reset, window 3 shows page `#40` and **writes to ports are ignored until the first port
  read** (MAME `m_starting`: `sprinter.cpp:367`, `:572`, `:688-689`). The BIOS fills the table
  with `LDIR`/record expansion through window 3, then executes `IN A,(SLOT3)` "First IN command -
  OPEN DCP" (BIOS-TT `DCP.ASM:467-505`).
- MAME also initializes the internal cells (`sprinter.cpp:1533-1539`): `#Cx` = 0, `#Dx` = `#10-#1F`,
  `#Ex` mostly `#41`, `#Fx` = `#00-#0F`.

### 4.3 Internal device codes

Codes `#00-#BF` are devices; `#C0-#FF` are PLD registers ("RAM cells") that also hold page numbers.
Table merged from MAN §13.1 p. 30-31 (Sp97 list), INC `SP2000.inc:1279-1560` (Sp2000, current
BIOS), MAME `dcp_r`/`dcp_w` (`sprinter.cpp:587-914`). Disagreements are in §4.5.

| Code | Device | R/W | MAME |
|---|---|---|---|
| `#00` | no port (reads `#FF`) | | `:589`, `:908` |
| `#10-#13` | WD1793 command/status, track, sector, data | RW | `:593-604`, `:712-723` |
| `#14` | Beta system register (drive, side, density bit, reset) | W | `:724-726` |
| `#15` | joystick + WD1793 DRQ/INTRQ | R | `:605-607` |
| `#16` / `#17` | floppy **720 KB / 1.44 MB** select (write strobe; data bit 1 = 1 disables the FDC in MAME) | W | `:727-734` |
| `#1B` | ISA control: RESET, AEN, A19-A14 (ext. `#9FBD`) | W | `:736-746` |
| `#1C` / `#1D` / `#1E` | CMOS data read / address write / data write | R / W / W | `:609-611`, `:748-753` |
| `#20` | IDE data (16-bit, latch by A8, §9.1) | RW | `:613-622`, `:755-760` |
| `#21-#27` | IDE task file 1-7 (reads need A8=0, writes A8=1) | RW | `:623-626`, `:761-764` |
| `#28` | IDE PC `#3F6`: alternate status (R, A8=0) / device control (W, A8=1) | RW | `:627-630`, `:765-768` |
| `#29` | IDE PC `#3F7`: drive address (R) | R | `:631-634` |
| `#2A` / `#2B` | IDE select **secondary** / **primary** channel | W | `:769-774` |
| `#2C` / `#2D` | 320-line (49 Hz) / 312-line (50 Hz) frame | W | `:775-778` |
| `#2E` | reload the PLD configuration (hard reset) | W | `:779-783` |
| `#30-#33` | ISA slot memory / ports (Sp97 list); `#32` = ISA_Control in INC | | not used by MAME |
| `#40` | ZX keyboard matrix (port `#FE` read) | R | `:636-638`, `:1669-1704` |
| `#52` | AY data read | R | `:640-642` |
| `#58` | Kempston mouse (`#FADF`/`#FBDF`/`#FFDF` by A8-A15) | R | `:644-659` |
| `#88` | Covox / Covox-Blaster sample | W | `:785-793` |
| `#89` | Covox-Blaster control (ext. `#4E` / `#0046`) | RW | `:661-663`, `:795-814` |
| `#8F` | ROM / fast RAM page (same as `#5C`) | W | `:816-820` |
| `#90` / `#91` | AY register select (`#FFFD`) / AY data (`#BFFD`) | W | `:822-827` |
| `#C0` (`#C8`) | Scorpion `#1FFD` | RW | `:829-834` |
| `#C1` (`#C9`) | Pentagon `#7FFD` | RW | `:835-842` |
| `#C2` | border (`#FE` write) | W | `:843-846` |
| `#C3` | ALL_MODE (ext. `#204E`) | W | `:847-849` |
| `#C4` (`#CC`) | PORT_Y / RGADR (ext. `#89`) | RW | `:854-856` |
| `#C5` (`#CD`) | RGMOD (ext. `#C9`) | RW | `:857-862` |
| `#C6` (`#CE`) | CNF / SYS port (ext. `#7C/#3C`, `#74/#24`) | W | `:864-885` |
| `#C7` (`#CF`) | accelerator "scale" / alternate addressing (ext. `#FC`) | W | `:887-893` |
| `#CB` | HOLD: screen position counters | W | `:850-852` |
| `#D0-#DF` | user cells ("virtual ports"); Pentagon-512 page cells | RW | |
| `#E0-#E7` | vROM pages (expansion, TR-DOS, BASIC 128, BASIC 48, and their "2" copies) | RW | `:665-671` |
| `#E8` / `#E9` / `#EA` | RAM page in window 0 / 1 / 2 | RW | `:708-709` |
| `#EB` / `#EF` | ZX BIOS pages | RW | |
| `#EC` / `#ED` | user cells used by TR-DOS in Spectrum mode | RW | |
| `#EE` | page entered after a soft reset | RW | |
| `#F0-#FF` | window 3 page for the current Spectrum page (§3.2) | RW | `:673-676`, `:901-905` |

### 4.4 The standard map (map 0, as written by BIOS-TT)

Decoded from BIOS-TT `bios/exp/DCP.ASM` records by expanding every record into the 16 KB page.
`x` = not decoded. The canonical address is what software uses. The page dump
`doc/DCP_PAGE.bin` in the same tree differs in 24 bytes: it predates the "collision between `#1F`
and CBL" fix (it still maps Covox writes to `#07/#0F/#17/#1F` with DOS off, see the commented-out
record at `DCP.ASM:324-327`) and has no read-back for ALL_MODE. BIOS versions therefore differ in
details, one more reason to decode from the table the running BIOS wrote (D1); the S0 capture from
BIOS 3.04 is the reference for the tests.

| Canonical port | Decoded pattern A15..A0 | Code | When |
|---|---|---|---|
| `#1F`, `#3F`, `#5F`, `#7F` | `xxxx xxxx 0nnx x111` | `#10-#13` | DOS only, R/W; the pattern also matches `#0F`, `#2F`… |
| `#FF` | `xxxx xxxx 111x x111` | `#14` W / `#15` R | DOS only |
| `#1F` (Kempston) | `xxxx xxxx 000x x111` | `#15` | read, DOS off |
| `#00BD` / `#20BD` | `00Ax xxxx 101x x101` | `#16` / `#17` | write, DOS on (or map 3) |
| `#9FBD` | `100x xxxx 101x x101` | `#1B` | write |
| `#FFBD` R, `#BFBD`/`#FFBD` W, `#DFBD` W | `1x1x…`, `110x… 101x x101` | `#1C` / `#1E` / `#1D` | |
| `#50` (+ `#150`) | `xxxx xxxx 010x x000` | `#20` | R/W, any DOS state |
| `#51-#55` (+ `#15x`) | `00xx xxxx 010x x0nn` | `#21-#25` | |
| `#4052`, `#4053` (+ `#415x`) | `01xx xxxx 010x x01n` | `#26`, `#27` | |
| `#4054`, `#4055` | `01xx xxxx 010x x10n` | `#28`, `#29` | |
| `#01BC` / `#21BC` | `00Ax xxxx 101x x100` | `#2A` / `#2B` | write |
| `#41BD` / `#61BD` | `01Ax xxxx 101x x101` | `#2C` / `#2D` | write |
| `#40BC` | `010x xxxx 101x x100` | `#2E` | write |
| `#FE` | `xxxx xxxx 111x x110` | `#40` R (DOS off) / `#C2` W | |
| `#FFFD` | `111x xxxx 111x x101` | `#52` R / `#90` W | |
| `#BFFD` | `101x xxxx 111x x101` | `#91` W | |
| `#FADF`, `#FBDF`, `#FFDF` | `111x xxxx 110x x111` | `#58` | read |
| `#FB`, `#4F` and others | several cubes | `#88` | write, DOS off (avoids `#1F`, see the comment at `DCP.ASM:304-336`) |
| `#4E` (`#0046`) | `000x xxxx 010x x110` | `#89` | write |
| `#1FFD` | `000x xxxx 111x x101` | `#C0` | R/W |
| `#7FFD` | `01xx xxxx 111x x101` | `#C1` | R/W, hidden when PN5 = 1 except as the map says |
| `#204E` | `001x xxxx 010x x110` | `#C3` | R/W (the older dump: write only; MAME has no read) |
| `#89` | `xxxx xxxx 100x x001` | `#C4` | R/W |
| `#C9` | `xxxx xxxx 110x x001` | `#C5` | R/W |
| `#24`, `#3C`, `#74`, `#7C` | `xxxx xxxx 0x1x x100` | `#C6` | R/W |
| `#82`, `#A2`, `#C2`, `#E2` | `xxxx xxxx 1nnx x010` | `#E8`, `#E9`, `#EA`, `#F0` | R/W |

Map 3 (CNF = `#1C`) additionally opens the floppy ports without the DOS signal ("for access to
the WD1793 without DOS_ON", INC `SP2000.inc:239`, `:1315`).

#### Checked against BIOS 3.04 (S0, 2026-10-01)

**Verified statically against 3.04.** BIOS 3.04 does not build the table from records: it unpacks
a packed copy from ROM page 8 `#1400` (writer `DcpInit` at page 8 `#0CA1`, see
[docs/disasm/rom/sprinter/exp/README.md](../../disasm/rom/sprinter/exp/README.md)), then makes map 3
four copies of map 0's first KB (the "DOS on, PN5 = 0" quarter). The tool
[`tools/machines/sprinter/dcp-table/dcp-table.py`](../../../tools/machines/sprinter/dcp-table/dcp-table.py) unpacks it the same way and
prints or compares tables (`--rom`, `--page`, `--records`). Results:

- The 3.04 table equals **byte for byte** `src/bios/old_files/DCP_PAGE.bin` of BIOS-TT `0271ac3`
  (2023). It differs from the beta `doc/DCP_PAGE.bin` in 64 bytes and from the `DCP.ASM` records the
  table above was decoded from in 88 bytes:

  | Map | Pattern A15..A0 | When | 3.04 | `DCP.ASM` (beta) |
  |---|---|---|---|---|
  | 0, 3 | `xxxx xxxx 111x x100` (`#E4`, `#EC`, `#F4`, `#FC`) | write | **`#C7`** (SCALE / accelerator addressing) | none (the record is commented out) |
  | 0 | `xxxx xxxx 000x x111` (`#07`, `#0F`, `#17`, `#1F`) | write, DOS off | **`#88`** (Covox) | none (the "#1F and CBL" collision fix) |
  | 0, 3 | `001x xxxx 010x x110` (`#204E`) | read | none | `#C3` (ALL_MODE read-back, beta f546c4e) |

  So with 3.04 a Spectrum program writing port `#1F` outside TR-DOS (the PLD rewrites `#1F` to `#0F`,
  §4.4 fixed ports) feeds the Covox, `OUT (#FC),A` reaches the `#C7` register, and ALL_MODE cannot be
  read back.
- Codes present in both 3.04 and the records but missing from the table above: `#2F` (write,
  `011x xxxx 101x x100`, e.g. `#60BC`) and `#32` (read/write, DOS off, `xxxx xxxx 101x xx11`, e.g.
  `#A3`, `#AB`, `#BB`: ISA_Control in INC). Map 0 holds 48 codes in all.
- Maps 1 and 2 (CNF `#0C` Scorpion, `#14` Pentagon) hold 31 and 34 codes and differ from map 0 in 556
  and 572 bytes; map 2 additionally maps `#80A5-#E0A5` (DOS on) to `#18-#1B`. `dcp-table.py --map 1`
  prints them.

**Runtime capture (MAME 0.289, 2026-10-01).** The page `#40` dump taken from MAME's `sprinter` driver
with BIOS 3.04 after POST (the boot screen with no media, 10.4 s after power-on), `page40.bin` in
[testdata/machines/sprinter/reference/](../../../testdata/machines/sprinter/reference/README.md), is **byte for byte the statically unpacked table**
(CRC `b7f09600`, `dcp-table.py --rom ROM --compare-page page40.bin`: 0 bytes differ). The table is the
same at the first port read that opens the decoder (0.677 s) and at the boot screen: nothing changes it
on that path. Function `#F4` (`DCP_CONFIG`) and SETUP (`ApplyScreenPosition` patches offset `#0400`
with `#CB` and restores it) still change entries at run time, but only when a program or the user calls
them, so the tests (R-1) can use either the dump or `dcp-table.py --rom`.

**Fixed ports.** The Z84C15 decodes its own ports before the PLD sees them: `#10-#13` (CTC),
`#18-#1B` (SIO A data, A control, B data, B control), `#1C-#1F` (PIO), `#EE/#EF` (system control:
wait states, chip selects), `#F0/#F1` (watchdog), `#F4` (interrupt priority) (MAN §13.2;
MAME `sprinter.cpp:1449-1454`, `tmpz84c015.cpp:22-27`, `z84c015.cpp:19-20`). These are **8-bit
decoded**, so port `#1F` belongs to the PIO (port B control), not to the WD1793 or the Kempston
joystick. To keep Spectrum software working, the PLD **rewrites the operand** of `OUT (n),A` and
`IN A,(n)` from `#1F` to `#0F` when the instruction runs from RAM (vROM included), so those reach
the WD1793 / joystick through the table (pattern `000x x111` matches `#0F`); `OUT (C),A` /
`IN r,(C)` with C = `#1F` are not rewritten and reach the PIO, which is why some programs miss the
joystick (MAN §9 p. 21, §10). MAME implements the rewrite on the operand fetch after an unprefixed
`#D3`/`#DB` opcode (`sprinter.cpp:1009-1015`, `:1323`).

### 4.5 Where the sources disagree

| Topic | MAN (Sp97 list) | INC / BIOS-TT (Sp2000) | MAME | ZXMAK2 | Decision |
|---|---|---|---|---|---|
| AY codes | `#90` = `#BFFD`, `#91` = `#FFFD` | `#90` = `#FFFD` (register select), `#91` = `#BFFD` (data) | `#90` address, `#91` data | follows MAN | follow INC/MAME: the BIOS table maps `#FFFD` to `#90` (decoded above) |
| `#C3` | reserved | ALL_MODE | ALL_MODE | — | ALL_MODE |
| `#C7` | reserved | SCALE (`#FC`) | alternate accelerator addressing | — | one register; semantics from PLD `ACCELER.TDF` in S5 |
| ISA access byte | bit 1 = port/memory, bit 2 = slot | bit 2 = memory/port, bit 1 = slot ("fixed bug … functional exchange", `SP2000.inc:596-602`) | bit 2 io, bit 1 slot (`:1254`) | — | follow INC/MAME |
| Palette byte order | Blue, Green, Red | BIOS palette data is stored B, G, R (`Shared_Includes constants/standart_colors.inc`) | reads offset+0 as **red** (`:1241-1242`) | — | **video RAM holds R, G, B (MAME is right; settled in S2, 2026-10-01).** B, G, R is the order of the BIOS's palette *function* `#A4` input (and of BMP palettes), which it writes reversed: 3.04 page 8 `#0E10` stores byte 0 → `(IX+2)`, byte 2 → `(IX+0)`; BIOS-TT `FUNC_SCREEN.ASM` `PIC_SET_PAL` / `SET_TXT_PALETTE` (`(IX) = red`). The PLD agrees: offset 0 of a 4-byte group is RAM bank 3 (`VIDEO2.TDF` `MODE0 = VDM3`, `V_EN3` for A1 A0 = 00), and its own colour output drives bank 3 as RED, 2 GREEN, 1 BLUE (`SP2_1K30.TDF:455-466`). The BIOS logo (LOGPAL `#AD,#08,#08` = B, G, R) renders blue and equals MAME's `logo.png` (`SprinterVideoBoot_Test`) |
| Kempston mouse | `#FADF/#FBDF/#FFDF` | same | same | `#1A/#1B` SIO path | both paths exist (serial raw + PLD Kempston view) |

## 5. System ports: CNF, SYS, ALL_MODE

| Port | Bits | Source |
|---|---|---|
| **SYS** `#7C` / `#3C` (write) | `#7C` puts the system ROM in window 0, `#3C` removes it. bit 1 = 1: bit 0 = turbo on/off; bit 1 = 0: bit 0 = BIOS page half (ROM 0 / ROM 8). bit 2 = 1 enables bits 3-7: bits 4-3 = map number (CNF 0-3), bit 5 = reset Pentagon port bits 0-5, bit 6 = reset Scorpion port, bit 7 = 0 resets Pentagon-512 bits 6-7. A CPU reset of the running configuration (Ctrl+Alt+Del, a write to page `#A0`) presets the turbo bit: the CPU restarts at 21 MHz if the front-panel switch allows | INC `SP2000.inc:226-300`; MAME `sprinter.cpp:691-697`, `:864-885` (MAME keeps `m_turbo` across a reset); PLD `DCP.TDF:649-663` (`TB_SW.prn = /RESET`) |
| **CNF** `#74` / `#24` | same bits; bit 0 selects vROM set (`#E0-#E3` vs `#E4-#E7`, `#EB` vs `#EF`) when bit 1 = 0 | INC `SP2000.inc:268-290` |
| **ALL_MODE** `#204E` | bit 0 = 1: accelerator on, keyboard interrupt on, Spectrum screen addressing off; bit 2: Spectrum memory waits; bit 3: keyboard interrupt separate from the accelerator. Every `/RESET` (Ctrl+Alt+Del, page `#A0`, the RESET button) presets it to `#FF`; RGMOD and PORT_Y are cleared by the same `/RESET` | INC `SP2000.inc:550-560`; MAME `sprinter.cpp:197`, `:1208`, `:1708` (MAME keeps all three across a reset); PLD `SP2_ACEX.TDF:1041` (`ALL_MODE[].prn = /RESET`), `:958` (`RGMOD[].clrn`), `ACCELER.TDF:204` (`AGR[].clrn`, PORT_Y) |

## 6. Video

### 6.1 Timing

- Master 42 MHz → 14 MHz pixel clock (640 mode) / 7 MHz (320 mode); 7 MHz / 8 = one 8-pixel square;
  56 squares per line; lines grouped by 8; **39 or 40 square rows = 312 or 320 lines** (MAN §4.9).
- Line = 64 µs = **224 T-states at 3.5 MHz**; frame = 320 × 224 = **71 680 T** (48.83 Hz, the Pentagon
  frame) or 312 × 224 = 69 888 T (50.08 Hz). Selected by the ports `#41BD` (320) / `#61BD` (312)
  and the CMOS "V-Sync" setting (INC `SP2000.inc:604-606`; MAME `sprinter.cpp:390-395`, `:1981`).
- Visible field: 40 × 32 squares = 320 × 256 (or 640 × 256); MAME shows 736 × 288 with border
  (`sprinter.cpp:180-190`).
- **INT** is not at a fixed position: it fires on the 8th line of a square whose mode byte says
  "blank + interrupt" (`Mode0` = `%1111 11x1`). The BIOS moves INT to the Pentagon, Scorpion or
  Spectrum position by rewriting those bytes (MAN §4.6; BIOS-TT `doc/changes.txt`, FN_SINC `#F2`;
  MAME `sprinter.cpp:1278-1313`). **End of the pulse** (PLD `SP2_1K30.TDF:744`, `INT_X`): the INT
  flip-flop is preset (INT off) by the **acknowledge** (`/IO` and `/M1` low together) or two rising
  edges of `CTH2` after it went on. `CTH` counts the 56 squares of a line, one per 4 T, so `CTH2`
  rises every 32 T and an unacknowledged pulse lasts 32-64 T depending on where it starts. MAME
  keeps a fixed 32 T and ignores the acknowledge (`:1736`); unreal-ng ends the pulse at the
  acknowledge (as the PLD) and keeps MAME's 32 T otherwise (which `CTH` value a MAME pixel x is
  has not been tied down, so the 32-64 T rule is not modeled yet).
- **Measured on MAME** (BIOS 3.04, `FN_SYNC` called at the boot screen, `int.csv` in
  [testdata/machines/sprinter/reference/](../../../testdata/machines/sprinter/reference/README.md)): one INT per
  320-line frame, at the same horizontal position (pixel 768 of 896, T 192 of the line) and on MAME
  screen line 271 (Scorpion, also the cold-start default of 3.04), 287 (Pentagon, 16 lines later) or
  295 (Spectrum, 8 lines after Pentagon); MAME's paper is lines 16-271.
- **The PLD's edge is 10 T before MAME's place** (2026-10-03): `INTT = DFF(!(INTTX & CTV[2..0] = 7), CT5)`
  (`VIDEO2.TDF`) changes on `CT5` rising, 2 T into the 4-T period of the square the video logic is reading,
  and `INT_X` above is clocked by `INTT` rising - the first square after the INT run. That square is read at
  T `12 + 4a` of the line in MAME's (and unreal-ng's) picture coordinates, so the edge is at T `14 + 4a`;
  MAME's `scr_a = a + 6` gives T `24 + 4a`. In the Spectrum mode the PLD's edge makes INT to the first
  Spectrum cell 17 990 T, the Pentagon's 17 988 within 2 T (research-zx-mode §7.1).
- HOLD register (`#CB`) shifts the picture by up to 7 squares horizontally (2-pixel steps) and 7 lines
  vertically (MAME `sprinter.cpp:850-852`).

### 6.2 Video RAM and how the CPU writes it

Video RAM (256 KB) is a **write-only shadow**: CPU writes go to main RAM and, in parallel, to video
RAM; reads always come from main RAM (MAN §4). Two ways to address it:

| Addressing | When | VRAM address | Source |
|---|---|---|---|
| **Graphics** | the CPU writes into a window that shows page `#50-#5F` | line = PORT_Y (0-255), column = CPU address bits 9-0 → `PORT_Y × 1024 + (A & #3FF)`. Page bit 3 = 1: bytes `#FF` are not written (transparent sprites). Page bit 2 = 1: write video RAM only, keep main RAM (cursor). Reads from these pages return main RAM at the same video address | MAN §3.2, §4.2-4.3; MAME `sprinter.cpp:1175-1205` |
| **Spectrum** | ALL_MODE bit 0 = 0, a write to `#4000-#5FFF` of any page (and `#C000-#DFFF` when window 3 holds Spectrum page 5/7) | block (8 KB) from RGADR = PORT_Y bits 4-0 (bit 0 XOR `#7FFD` bit 3), laid **across** graphics lines: `(A7..A0) << 10 \| (block bits) << 5 \| A12..A8`. RGADR bit 6 = 1 (or ≥ `#C0`) disables the shadow | MAN §4.1; MAME `sprinter.cpp:1208-1219` |

VRAM layout (MAN §4.4): lines `#0000-#02FF` of each 1 KB row are free for programs (the BIOS uses
them for Spectrum screens, two graphics screens and fonts); `#0300-#039F` hold the **mode
table**; `#03E0-#03FF` hold the **palettes**.

### 6.3 The mode table

Each 8×8 square `(a, b)` (a = 0..55, b = 0..39) has 4 mode bytes at VRAM row
`Line1 = 1 + 2a + #80 × PM`, column `#300 + 4b` (PM = RGMOD bit 0, the mode page); a second set at
`Line2 = Line1 + 1` overrides the right half of a square in 640-pixel modes (MAN §4.5; MAME
`as_mode` `sprinter.cpp:547-553`).

| Mode0 bit 4 | Mode | Mode0 | Mode1 | Mode2 | Source |
|---|---|---|---|---|---|
| 1 | **text / Spectrum** (ZX-40 = 1 char per square, ZX-80 = 2) | bits 3-0: font block bits 4-1 (bit 0 from `#7FFD` bit 3); bit 5: 320 (1) / 640 (0); bits 7-6: bits 12-11 inside the block; `%1111` in bits 7-4 = border/sync square: bits 3-2 = `11` blank, bit 0 = INT | character address (low 8 bits) | attribute address (Spectrum) or the attribute itself (text) | MAN §4.6; MAME `draw_symbol` `:453-497` |
| 0 | **graphics** 8×8 × 256 colors (320) or 16×8 × 16 colors (640, **high nibble = left pixel**: MAME, and the PLD shows `DCOL[7..4]` first, `VIDEO2.TDF` `BRVA`; settled in S2) | bits 3-0: tile column high bits; bit 5: 320/640; bits 7-6: palette 0-3 | bits 1-0 column low, bit 2 block low bit, bits 7-3 tile row | unused (bit 2 in MAME: low-res 2×2) | MAN §4.7; MAME `draw_tile` `:430-451` |

Palettes (MAN §4.8): color *n* of palette *k* sits at VRAM row *n*, columns `#3E0 + 4k` .. `+2`;
four graphics palettes (`#3E0`, `#3E4`, `#3E8`, `#3EC`) and four text palettes (`#3F0` paper,
`#3F4` ink, `#3F8` flash paper, `#3FC` flash ink). 8 bits per component, **red, green, blue** in
that order (§4.5). A blank square (`Mode0` = `%1111 11xx`) shows text paper colour 0 (pen `#400`):
the PLD clears its colour register and the palette address is `#400` (`VIDEO2.TDF` `DCOL.clrn =
!BLANK`; MAME the same), not a forced black. HOLD (code `#CB`) after power-on = `#77` (no shift,
MAME `m_hold = {0, 0}`).

The "Game" configuration (`Thunder in the Deep`) uses a different renderer with per-square
scroll (MAME `sprinter.cpp:499-545`); it is a separate PLD bitstream, out of scope for v1.

## 7. Accelerator

A 256-byte buffer inside the PLD that repeats one memory access many times (MAN §6; MAME
`sprinter.cpp:917-1137`; ZXMAK2 `SprinterMMU.cs:444-525`). Enabled by ALL_MODE bit 0.

| Opcode (not prefixed) | Meaning |
|---|---|
| `LD B,B` (`#40`) | accelerator off |
| `LD D,D` (`#52`) | next memory read's value sets the block length (0 = 256) |
| `LD C,C` (`#49`) | fill: the next write stores the byte *length* times |
| `LD E,E` (`#5B`) | vertical fill in graphics pages (PORT_Y increments instead of the address) |
| `LD L,L` (`#6D`) | copy: the next read loads the buffer, the next write stores it |
| `LD A,A` (`#7F`) | vertical copy |
| `LD H,H` (`#64`) | reserved (MAN); "double byte" writes in MAME |
| `AND (HL)`, `OR (HL)`, `XOR (HL)` (`#A6`, `#B6`, `#AE`) | combine the loaded bytes with the buffer; `CP (HL)` (`#BE`) = plain load |

Speed: about 7 MB/s; interrupts should be off (newer firmware suspends the accelerator on INT
and resumes on RETI) (MAN §6 p. 21). While a block runs the CPU is held in WAIT (MAME
`:1026-1037`).

## 8. Sound

| Device | Ports | Detail | Source |
|---|---|---|---|
| AY-3-8910 | `#FFFD` / `#BFFD` | clock 42 MHz / 24 = 1.75 MHz; stereo A left, B center, C right | MAME `sprinter.cpp:2013-2017` |
| Beeper | `#FE` bit 4 | mixed into the same DAC | MAN §5.2; INC `SP2000.inc:124` |
| Covox | `#FB`, `#4F` | 8-bit, mono (both channels) | MAN §5.2 |
| Covox-Blaster (CBL) | data `#FB`/`#4F`; control `#4E` (16-bit port `#0046`) | control bits: 7 = CBL on, 6 = stereo, 5 = 16-bit, 4 = interrupt on, 3-0 = rate. 256-entry ring (2 banks of 128), filled with `OTIR`; port `#FE` bit 7 shows the bank being played; with bit 4 set, an INT (vector `#FF`) asks for the next half. Rates 7.8-109 kHz (table `SP2000.inc:201-219`) | MAN §5.3; INC `SP2000.inc:136-220`; MAME `sprinter.cpp:785-814`, `:1696-1701`, `:1748-1765` |
| Sprinter Sound Card | — | never shipped in a PLD build | MAN §5.4 |

## 9. IDE

### 9.1 Registers

| Register | Read | Write | Source |
|---|---|---|---|
| Data (16-bit) | `IN (#0050)` returns the low byte and latches the high byte; `IN (#0150)` returns the latch | `OUT (#0050)` stores the low byte in the latch; `OUT (#0150)` sends latch + high byte as one word | MAME `sprinter.cpp:613-622`, `:755-760`; ZXMAK2 `IdeSprinter.cs:150-195` |
| Error/features … cylinder high | `#0051-#0055` (A8 = 0) | `#0151-#0155` (A8 = 1) | BIOS-TT `ATA_DRV.ASM:9-31`; MAME `:623-626`, `:761-764` |
| Device/head, status/command | `#4052`, `#4053` | `#4152`, `#4153` | same |
| Alternate status / device control (PC `#3F6`) | `#4054` | `#4154` | INC `SP2000.inc:1331-1341`; MAME `:627-630`, `:765-768` |
| Drive address (PC `#3F7`) | `#4055` | — | MAME `:631-634` |
| Channel select | — | `OUT (#BC),A` with A = `#21` → primary, A = `#01` → secondary | INC `SP2000.inc:1935-1940`; MAME `:769-774` |

Rules: the **data latch is one register shared** by reads and writes: the PLD has a single `HDDR`
register loaded from the CPU on writes and from the drive's high byte on reads (PLD
`SP2_1K30.TDF:181`, `:360-373`); MAME agrees (one `m_ata_data_latch`), ZXMAK2 keeps two bytes. On a
write the latch holds the **low** byte (the reverse of the Nemo order). Reads of
registers 1-7 with A8 = 1 and writes with A8 = 0 do nothing (MAME). The interrupt line is not
connected (the BIOS polls BSY/DRQ, `EXTENDED/shared.asm:6-33`). **With no drive** the emulator reads `#7F`
(BSY = 0) from every register of the channel: the ATA host pull-down on DD7 (owner decision 2026-10-02,
tdd-storage §3.4). The IDE data lines reach the CPU side through two K555AP6 (74LS245) transceivers (U6, U9
on the Sp2000 schematic, `zxgit/2000` `pcad_import/PAGE1.pdf`); the drawing shows no pull-down at the
connectors X6/X9, and S3b first modeled an undriven bus as `#FF` (BSY set), on which SETUP waits 1 550 frames
(#060E HALTs, ~31.7 s) per unit unless F4 is pressed (SETUP `#9663`-`#967E`). The community BIOS shows that a
real board does not read `#FF` either: 3.06 / 3.07 `AUTOIDE.asm` `DETECTORS.CheckChanel` recognizes an empty
channel by three status reads returning `#78`, `#68`, `#ED` - the bytes left on the data bus by its own
`IN` opcodes, so BSY reads 0 there as well. With `#7F` that check does not match, but the next one does
(the sector count does not echo), and every BIOS reports "None" at once. MAME's default slots hold an IDE hard
disk without an image (primary master: status `#52`, IDENTIFY aborted with `#51`, "None" at once) and an ATAPI
CD (primary slave: status `#10`/`#11`, polled for 280 frames). The channel select is a latch that
survives until changed; reset selects primary (MAME `:1582`).

### 9.2 Units and BIOS numbering

- Four units: primary master/slave, secondary master/slave. BIOS drive codes: `#8n` hard disk,
  `#Cn` ATAPI CD, where n bit 0 = master/slave and bit 1 = primary/secondary ("physical numbering",
  BIOS-TT `doc/changes.txt`; INC `SP2000.inc:1772`, `:679-691`). `#0n` floppy, `#6n` RAM disk.
- BIOS disk API: `RST #08` (from RAM), `RST #18` (from the BIOS ROM) or `CALL #3D13` (TR-DOS), function
  in C: `#51` reset, `#52/#53` long read/write (to a page), `#54` verify, `#55/#56` read/write,
  `#57` detect, `#58/#59` get/set parameters, `#5A` version, `#5E` extended (CD eject/close),
  `#5F` device list. **32-bit LBA in HL:IX**, count in B, buffer in DE (INC
  `Docs/BIOS functions.asm:1250-1436`; MAN §20).
- MAME wires a CD on the primary slave (`sprinter.cpp:1967-1968`), which is also where the CD audio
  is routed (`:31`).

### 9.3 Disks DSS can use

- FAT12 and FAT16 only (DSS-162 `fat_x.asm`; DSS `DOSBOOT4.ASM:351-375` recognizes the `FAT12`/`FAT16`
  strings at BPB `+#36` or falls back to the media byte).
- PC MBR: primary types `#01`, `#04`, `#06`, `#0E`; extended `#05`, `#0F` walked; `#07`, `#0B`, `#82`,
  `#83`, `#EB` skipped (DSS-162 `ide_drv0.asm:596-640`). DSS scans all four units `#80-#83`
  (`ide_drv0.asm:199-221`).
- **The boot loader is stricter**: it checks the partition type of **entry 0 only** (its loop
  advances IY but reads IX, `DOSBOOT4.ASM:237-248`), so the DSS partition must be the first
  entry. Found by reading the source; **unverified** on hardware.

## 10. Floppy

| Rule | Detail | Source |
|---|---|---|
| Controller | WD1793 as Beta Disk: `#1F`(→`#0F`)/`#3F`/`#5F`/`#7F`/`#FF`, only while DOS is active (maps 0-2) or always (map 3) | §4.4; MAN §10 |
| DOS ROM switching | TR-DOS is entered by fetching from `#3D00-#3DFF` while the 48 BASIC vROM is in window 0; left by fetching from `#4000` up | MAME `sprinter.cpp:1383-1400`; ZXMAK2 `SprinterFdd.cs` (`BusReadMem3D00_M1`) |
| Density | `OUT (#BD),A` with A = `#21` → 1.44 MB (500 kbit/s), A = `#01` → 720 KB (250 kbit/s); the high address byte, not the data, selects (codes `#17`/`#16`). HD doubles both the WD1793 clock (1 → 2 MHz, the chip's 8" mode, so it also writes at 500 kbit/s) and the data separator (7 → 14 MHz source); in DD the PLD runs the chip at 2 MHz only while stepping (`TURBING`, STEP until the read/write strobe). MAME doubles the WD1793 clock for HD. unreal-ng: `Latched` clock policy + `WD1793::SetLatchedClock` ([WD1793_Clock_And_Data_Rate.md](../../WD1793/WD1793_Clock_And_Data_Rate.md)) | MAN §10.1; INC `SP2000.inc:608-610`; BIOS-TT `FDD_DRIVER.asm:597-618`; PLD `SP2_MAX.TDF:272-306, 396-402`; MAME `beta_m.cpp:196-202` |
| Density detection | the BIOS issues READ ADDRESS; on failure it flips the density and retries. It works because a separator at the wrong rate never finds an address mark: Record Not Found | BIOS-TT `FDD_DRIVER.asm:626-650` |
| Formats | PC FAT12 720 KB (80×2×9×512) and 1.44 MB (80×2×18×512); TR-DOS TRD (80×2×16×256) with TR-DOS 5.04Em; 5.25" drives | MAN §1.1, §23; BIOS-TT `rom/SETUP/MAIN.asm:1206-1225` (drive tables); MAME `beta_m.cpp:26-39` |
| DSS floppies | FAT12, BPB media `#F0`/`#F9`; the boot loader needs 3 reserved sectors after the boot sector, so `BOOT.EXE` removes one FAT copy and enlarges the reserved area | DSS `SYS.ASM:97-128`; DSS-162 floppy: 10 reserved sectors, 1 FAT |
| Default boot drive | a blank CMOS (SETUP defaults) boots the IDE master, then **floppy B** (CMOS `#10` = `#12`); the Beta drive bits select drive B (`OUT (#FF),#3D`) | BIOS 3.04 SETUP `DEFVAL` (`#9C00`), `S_FDD` (ROM page 0 `#07ED`) |
| Spectrum mode ROMs | BIOS 3.04 holds none: ESC at SETUP prints "Spectrum ROM not installed. Use spectrum.exe". DSS `ZX\SPECTRUM.EXE <mode>.ZX [image.TRD]` loads BASIC 128 / 48, Sprinter TR-DOS 7.0x and the expansion ROMs and starts the 128 menu; it sets the latch to 720 KB. BIOS 3.06+ carries the ROMs (ESC works) | DSS 1.62 floppy `DOCS\SPECTRUM\README.ENG`, `ZX\*.ZX`; S3a test `Dss162_SpectrumModeTrDosReadsATrd`; [research-zx-mode.md](research-zx-mode.md) |
| TR-DOS drives in the Spectrum mode | TR-DOS 7.0x reads a BIOS drive table per drive: floppy 0-3 (the WD1793), RAM disk (4-19: a TRD / SCL the launcher or `/LOAD` put into RAM pages; TR-DOS asks the BIOS through `#3FF0` / `#3FF8`), hard-disk partition (`#40`+n). No PLD trap: programs that drive the WD1793 themselves read the real floppy | [research-zx-mode.md](research-zx-mode.md) §5.4 |

## 11. ISA

Two 8-bit slots reached through memory: set `#1FFD` = `#10` (Scorpion extended page), then put
`#D0`/`#D2`/`#D4`/`#D6` in window 3; CPU A13-A0 become ISA A13-A0, port `#9FBD` gives A19-A14, AEN and RESET.
ISA interrupts arrive on the CPU's PIO port B (MAN §8, §9.3; INC `SP2000.inc:596-602`; MAME `sprinter.cpp:1246-1276`).
MAME fits a ZX-bus adapter in slot 0 (`:1975`).

Corrections from the ISA research ([2026-10-02-sprinter-isa/research.md](../2026-10-02-sprinter-isa/research.md) §10),
applied when ISA phase I1 landed (2026-10-03, branch `sprinter-isa-network`):

- **Page byte:** bit 2 = I/O (1) / memory (0), bit 1 = slot: `#D0` memory slot 1, `#D2` memory slot 2, `#D4` I/O slot 1,
  `#D6` I/O slot 2 (INC / MAME, confirmed by the schematic: DD7 74ALS138). MAN §8 and INFO_012 have it the other way.
- **`#9FBD`** (port-table code `#1B`, a 74HC374, DD6): bits 0-5 = A14-A19, bit 6 = AEN, bit 7 = RESET DRV to both slots.
  The latch has no reset input: a machine reset keeps it; power-on starts it at 0 in the emulator (MAME too).
- **No direct port path** to ISA on the Sp2000 (ISA research §4.4), although BIOS 3.04 maps `#A3-#BF` (DOS off) to
  code `#32`: it reaches no slot, an `IN` reads `#FF`.
- **ISA memory exists** (ISA RAM cards: Shaos's TIMER runs code from one): "ISA memory reads `#FF`" is a MAME gap, not
  the hardware. In unreal-ng window 3 in ISA mode is a real cycle of the slot's card (memory and I/O, opcode fetches
  too; an empty slot reads `#FF`); tool reads peek without side effects (`SprinterIsaBus`, `/state/isa`).
- **Interrupts and DRQ / DACK** reach PIO port B (bits 0-5); the IRQ pins of each slot are tied together (ISA phase I4).

**Network and serial cards (research 2026-10-02,
[2026-10-02-sprinter-network](../2026-10-02-sprinter-network/research.md)).** The main board has no free serial port:
SIO A is the keyboard, SIO B the mouse ("not standard RS-232", Peters Plus FAQ). Every network adapter is an ISA card:
NE2000-class Ethernet (RTL8019AS and clones, I/O `#200-#3E0`, default `#300`), the SprinterESP Wi-Fi card (TL16C550C at
`#3E8`, 14.7456 MHz, ESP8266 with AT 2.2.1), the 3Com 3C509B (ID port `#110`), ISA Hayes modems (16450 / 16550 at
`#3F8` / `#2F8` / `#3E8` / `#2E8`, 1.8432 MHz) and SprinterSerial (PC16552D at `#3F8` + `#2F8`). The 2026 network kits
poll; only BC-Term uses the ISA interrupt (PIO port B bit 0 / 1, IM 2). BIOS 2.13 pulses ISA RESET at boot
(`#9FBD` <- `#FF`, then `#00`). Bit assignment of the page byte and `#9FBD`: see the
[ISA research](../2026-10-02-sprinter-isa/research.md) §4 (bit 2 = I/O, bit 1 = slot; confirmed again by BC-Term, ESPT
and the network kits).

## 12. CMOS and clock

DS12887A: address write `#DFBD`, data write `#BFBD`, data read `#FFBD` (MAN §12; INC
`SP2000.inc:959-962`; ZXMAK2 `SprinterRTC.cs:33-35`). Standard MC146818 registers `#00-#0D`,
century `#32`; the BIOS keeps its settings in `#0E-#3F` with a checksum in `#3F`: boot drives
(`#10`), floppy types and IDE modes (`#11`, `#20`), IDE geometries, screen/INT/V-sync (`#1A`),
turbo (`#1B`), TR-DOS drive mapping (`#1E`) (INC `SP2000.inc:1013-1160`).

## 13. Keyboard, mouse, joystick

| Path | Detail | Source |
|---|---|---|
| ZX matrix | the PLD decodes the AT keyboard's serial stream into an 8×5 matrix read at `#FE` (code `#40`); PC keys map to ZX combinations (arrows = CS+5..8, etc.); Ctrl+Alt+Del resets | PLD `KBD.TDF` (`KB_RESET`, `KB_F12`); INC `SP2000.inc:422-517` (key map); MAME approximates with a host-key matrix (`sprinter.cpp:1669-1704`) |
| Raw scan codes | the same stream enters SIO channel A (`#18` data, `#19` control), set-2 codes, a 3-byte FIFO; the BIOS and DSS read keys here | MAN §9.4; DSS-162 `keyinter.asm:859-860`; MAME `sprinter.cpp:1987-1991` |
| Wiring | connector KBD_CLK / KBD_DAT, 3.9k pull-ups, 150 Ω + KC147 clamp -> KBD_CLKR to the Z84C15's /RXCA and /TXCA (pins 33, 34), KBD_DATR to RXDA (32); the same lines reach the PLD as KBD_CC / KBD_DD through the multiplexed XA0 / XA1 bus. The only drivers onto the lines are DD17C / DD17D (open-collector NANDs) fed from latch DD16 KR1533TM9 Q3 / Q4 (KBD_CX / KBD_DX, written from XA0 / XA1 on WR_AWG) | schematic `SPRINT_3` / `PAGE1.pdf` (Sp2000, zxgit); MAME `sprinter.cpp:1987-1991` |
| No hold-off | the PLD writes KBD_CX = KBD_DX = GND (the LED-command sender `KEY_D` / `K_DATA` and `KBD_BLK` are commented out): the clock is never inhibited, the keyboard is never told to wait. /RTSA, /DTRA, /W/RDYA do not touch the keyboard. MAME's `pc_kbdc` leaves the host side of both lines idle too | `SP2_1K30.TDF:338-352`, `:729`, `:780-781` (both PLD source sets agree); schematic |
| Byte rate | the keyboard clocks 11-bit frames at 10-16.7 kHz: 0.66-1.1 ms a byte (model: 12 kHz, 917 µs); typematic 500 ms / 10.9 per second (power-on default; no command can reach the keyboard) | IBM AT keyboard reference; `Ps2KeyboardStream` |
| SIO overrun | a character completing with three in the FIFO overwrites the newest one and carries the overrun flag; RR1 bit 5 shows it when that character reaches the top, latched until Error Reset (WR0 command 6); in the interrupt-on-first-character mode the FIFO does not advance past it until Error Reset | Zilog Z80 SIO technical manual (RR1 D5); Toshiba TMPZ84C015B data book §3.6 (RR1 D5); MAME `z80sio.cpp` `queue_received`, `data_read` |
| Software on overrun | BIOS SETUP up to 3.05 and DSS up to 1.62.93: one key event per frame INT, RR1 never read, so bytes are lost when keys come faster than one event per frame or the CPU does not poll; BIOS 3.06 / 3.07 and DSS 1.71: drain the FIFO every INT, and on RR1 bit 5 empty it, Error Reset, clear the shift flags | BIOS-TT `SETUP/KEY.asm:166-254`, `:771-789`; DSS-TT `KEYINTER.ASM:520-629`, `:1223-1238` (2024-02-18 / -29); the RR1 test `D3 19 DB 19 E6 20` is in BIOS 3.06 / 3.06 HF2 / 3.07 beta 1 and the DSS 1.71 floppy, not in 2.17 / 3.04 / 3.05 or DSS 1.60-1.62.93 |
| PLD keyboard block | decodes the wire, not the SIO, at the end of each byte: Ctrl `#14`, Alt `#11`, Shift `#12` / `#59` set on make, cleared after `#F0` (E0 prefixes kept transparent); `#71` with Ctrl + Alt -> /RESET; each `#07` not after `#F0` with no Shift / Ctrl / Alt toggles TEST_SWITCH = TURBO_HAND (so F12's typematic repeats toggle again); /RESET presets TEST_SWITCH | `KBD.TDF` (KB_CT, KB_OFF, KB_EXT, KB_CTRL_X, KB_ALT_X, KB_SH_X, KB_F12, KB_RESET); `SP2_1K30.TDF:296`, `:521-523` |
| Keyboard interrupt | ALL_MODE bits 0/3 enable an INT after each 11-bit frame | MAME `sprinter.cpp:1706-1718` |
| Mouse | MS serial mouse on SIO channel B (`#1A`/`#1B`, clocked by CTC channel 0 in MAME `:2006-2007`); Kempston view `#FADF` buttons, `#FBDF` X, `#FFDF` Y | MAN §9.1; MAME `sprinter.cpp:2000-2008`, `:644-659` |
| Joystick | Kempston at `#1F` (DOS off) and `#FF` (DOS on, with DRQ/INTRQ); extended 3-button pads through the CPU's serial DTR / PIO lines | INC `SP2000.inc:382-399`; MAME `sprinter.cpp:1330-1374` |

## 14. Reset, configuration loading and boot

```text
power-on / RESET button
  │ PLD empty; CPU runs the loader in ROM (page #1C / 12) with only ROM and fast RAM visible
  │ loader: "ACEX_30K_LOADING" at fast RAM #FEF0? → bitstream from fast RAM, else from ROM
  │ streams 59 215 bytes to the PLD, 8 writes per byte (one bit each) = 473 720 writes (S0, static;
  │ confirmed at run time on MAME: no other writes before the last bitstream byte)
  ▼ PLD configured → the small EPM7064 CPLD resets the CPU
BIOS (ROM, system mode)
  │ POST, DCP_INIT fills page #40, IN A,(SLOT3) opens the port decoder
  │ memory test, logo, CMOS checks, IDE auto-detect; DEL = setup, ESC = Spectrum mode
  ▼ boot device from CMOS #10: FDD A/B, IDE #80-#83, RAM disk, ROM recovery, (CD in BIOS-TT)
OS_LOAD: DRV_READ LBA 1 (512 bytes; LBA 17 × 2048 for a CD)
  │ must start with "Starting...\0" → copied to #8000, jump #800C, A = drive code
  ▼
DSS loader (DOSBOOT4): loads LBA 2-3 to #8200; LBA 0: MBR (HDD) or BPB (FDD);
  partition boot sector; FAT12/16 root: SYSTEM.DOS (`DOSBOOT4.ASM:650`) → a BIOS-allocated page in window 0;
  DSS init (RST #10 fn 0); set boot drive; CHDIR X:\; EXEC "\SYSTEM.EXE /P"
```

BIOS 3.04 (S0, statically from the disassembly, [docs/disasm/rom/sprinter/](../../disasm/rom/sprinter/README.md)):
the loader is ROM page `#C` `#0000-#009B` and runs with ROM pages `#C-#F` at `#0000-#FFFF`; the
BIOS cold start is ROM page 8 `#0100`; the boot (SETUP, unpacked from ROM page 0) reads the boot
device from CMOS `#10` (low nibble: 0 floppy A, 1 floppy B, 2 IDE master, 3 IDE slave, 4 RAM disk;
high nibble: the alternative), resets a floppy first (function `#51`, which runs the density
probe), then reads LBA 1 with function `#55` to `#7E00`, checks the 12 bytes `Starting...`+`#00`,
copies the sector to `#8000` and jumps to `#800C`, A = device code. 3.04 has no CD boot (an ATAPI
driver exists, SETUP refuses a CD as boot device).

Sources: MAN §1.4, §14; BIOS-TT `bios/loader/loader.asm`, `rom/SETUP/MAIN.asm:770-830` (menu keys),
`:1019-1191` (boot), `bios/mem_map.txt` (ROM pages); DSS `utils/BOOT/DOSBOOT4.ASM:36-220`; MAME
`sprinter.cpp:1139-1166` (configuration shortcut: counts 4 096 writes, hashes them to recognize the
"Game" bitstream, then soft-resets), `:1549-1601` (reset state). Whether `SYSTEM.EXE` runs
`SYSTEM.BAT` automatically is **unverified** (the installer copies `SYSTEMX.BAT` to `SYSTEM.BAT`,
`release/SYSCOPYA.BAT`).

Other resets: writing page `#A0` (§3.4) = soft reset; code `#2E` (`OUT` to `#40BC`) = reload the PLD
(MAME `sprinter.cpp:779-783`); Ctrl+Alt+Del = hardware reset from the keyboard block.

What the board's `/RESET` does to the configured PLD (`SP2_ACEX.TDF:294-306`: the keyboard's Ctrl+Alt+Del and the
page-`#A0` counter both pull the `/RESET` pin, which the PLD reads back; 2026-10-02):

| Register | On `/RESET` | Source |
|---|---|---|
| ALL_MODE | `#FF` (accelerator, keyboard INT on, Spectrum screen addressing off, no original waits) | `SP2_ACEX.TDF:1041` |
| RGMOD, PORT_Y | 0 | `SP2_ACEX.TDF:958`; `ACCELER.TDF:204` |
| Turbo | on (if the front-panel switch allows) | `DCP.TDF:663` |
| CNF, SYS, ROM_RG, AROM16, `#7FFD` / `#1FFD` (clean rules), DOS, CASH_ON, STARTING | cleared / set as in MAME's `machine_reset` | `DCP.TDF:662-713`, `SP2_ACEX.TDF:563-837` |
| Accelerator mode, ALT_ACC, the Covox-Blaster | off | `ACCELER.TDF:221`, `:270-271`; `SP2_ACEX.TDF:1161-1162` |
| HOLD | kept: reset by the configuration's own `/RES` only (`#77` after a load) | `SP2_ACEX.TDF:827-830`, `DCP.TDF:258` |
| Border, the cells `#C0-#EF` | kept | `SP2_ACEX.TDF:313-315` (no reset term) |

BIOS 3.07 BETA 1 depends on the ALL_MODE preset: its reset intercept (`EXP.asm` `Setup_Starter`, the beta's "ALL_MODE
readable") reads the register back and writes what it read.
