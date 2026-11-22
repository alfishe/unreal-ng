# MGT +D: hardware reference

Facts the emulation is built from, each with its source. Where the sources disagree the row says so and names the
check that settles it (open items H-1...H-8 at the end).

Sources:

| Id | Source | Where |
|---|---|---|
| S-MAME | MAME `src/devices/bus/spectrum/mgt.cpp` (TwistedTom; +D and DISCiPLE, written with PAL dumps) | `/Volumes/TB4-4Tb/Projects/emulators/github/mame` (0.289) |
| S-FUSE | Fuse `peripherals/disk/plusd.c`, `wd_fdc.c`, `z80/z80_ops.c`; libspectrum `szx.c` | `~/Projects/GitHub/fuse-emulator-svn` |
| S-ROM | G+DOS v1a ROM, `data/rom/gdos-pd.rom` (8192 bytes, SHA-1 `6b841dc5797ef7eb219ad455cd1e434ca3b9d30d`, equal to MAME `plusd_g.rom` and Fuse `plusd.rom`) | repository |
| S-TAPE | Plus D System Tape v2a (MGT 1988) | TOSEC |
| S-DISK | `plusd_gdos_clean_dsdd.mgt` (clean G+DOS 2a system disk) | `ZX-Spectrum-FD-Images-main/mgt_plus_d/` |
| S-WD | WD177X-00 data sheet (Western Digital) and Gahris' programming notes (1995) | `docs/WD1793/datasheeets/WD177X-00/` |
| S-WIKI | MGT filesystem, faqwiki.zxnet.co.uk | <https://faqwiki.zxnet.co.uk/wiki/MGT_filesystem> |

## 1. The device

The +D (Miles Gordon Technology, 1987; later Datel) is a disk and printer interface on the Spectrum edge connector:

| Part | What |
|---|---|
| ROM | 8 KB, G+DOS boot ROM (v1, v1a) or UNI-DOS. Only the boot code and the hooks: the full DOS is loaded from disk |
| RAM | 8 KB, holds the DOS loaded from the system file and its variables; survives a reset, lost at power-off |
| FDC | WD1772 at 8 MHz, `/DDEN` tied to ground: double density only (S-MAME header) |
| Drives | two Shugart drives (drive 1, drive 2), 3.5" or 5.25", 40 or 80 tracks, one or two sides |
| Printer | Centronics: data latch, strobe in the control latch, busy on a read |
| Button | "snapshot" button: an NMI; the ROM pages in at `0x0066` and offers 3 = SCREEN$, 4 = 48K, 5 = 128K, X = return |

Not on the +D (DISCiPLE only): joysticks, network, inhibit button, pass-through connector, `/DDEN` control,
16 KB ROM bank bit.

## 2. Memory map while paged in

`ROMCS` from the +D replaces the Spectrum ROM in `0x0000-0x3FFF`:

| Address | Read | Write |
|---|---|---|
| `0x0000-0x1FFF` | +D ROM | ignored |
| `0x2000-0x3FFF` | +D RAM | +D RAM |

On a 128K machine the paged-in +D hides whichever ROM `7FFD.4` selects; the bank latch itself is untouched.

## 3. Paging

| Event | Effect | Source |
|---|---|---|
| M1 opcode fetch at `0x0008` (RST 8, error handler) | page in | S-MAME, S-FUSE |
| M1 opcode fetch at `0x003A` (inside the IM 1 handler at `0x0038`) | page in | S-MAME, S-FUSE |
| M1 opcode fetch at `0x0066` (NMI) | page in | S-MAME, S-FUSE |
| M1 opcode fetch at `0x028E` (48K ROM `KEY-SCAN`) | page in in Fuse, not in MAME; the ROM has a matching hook (below) | H-1 |
| `IN` from port `0xE7` | page in | S-MAME, S-FUSE |
| `OUT` to port `0xE7` | page out | S-MAME, S-FUSE |
| Reset | Paged out after reset: Fuse's `plusd_reset()` sets `plusd_active` but clears `ram.romcs`, so the machine's ROM runs and the first trap (`#003A`) pages G+DOS in. Paged in, the +D `#0000` entry jumps to the 48K ROM's `#11CB`, which on a 128K is ROM 0 code: the boot hung at `#032C` on random power-on RAM (2026-11-18) | H-2 |

The trap reads the opcode of the trap address itself from the +D ROM (the page-in takes effect for the fetch that
triggered it). Checked against Fuse: `plusd_page()` runs before the opcode read in `z80_ops.c`; MAME does it in
`pre_opcode_fetch`. Both agree.

The hooks in S-ROM (disassembled with `z88dk-dis`) leave the +D with `OUT (0xE7),A` exactly where the Spectrum ROM
must continue, so the next fetch comes from the Spectrum ROM:

| Entry | +D ROM code | Hands over to the Spectrum ROM at |
|---|---|---|
| `0x0000` | `NOP; LD BC,0101; NOP; JP 0533`: border 2, `I = 3F`, `SP = 7FFF`, `IM 1`, ... | (initialisation; evidence that the +D is paged in at reset) |
| `0x003A` | `LD HL,(5C78); CALL 02E3` (bumps `FRAMES`, calls the hook at `02A2`); `OUT (E7),A` at `0040` | `0x0042` (`OR L` of the IM 1 handler) |
| `0x0066` | `PUSH AF; JR 0053`: the snapshot button | (DOS menu) |
| `0x028E` | `LD L,2F; CALL 0296` (hook `02A2`, then `LD L,2F; LD DE,FFFF; LD BC,FEFE`); `OUT (E7),A` at `0294` | `0x0296` (`KEY-LINE`, registers set as `KEY-SCAN` sets them) |

The `0x028E` code is only meaningful as a trap entry, which is strong evidence for Fuse's trap list (H-1).

## 4. Ports

Address decoding: MAME decodes `A1-A6` only (`offset & 0x7E`, "address lines 0 and 7-15 ignored"); Fuse decodes the
full low byte (mask `0x00FF`). The board's PAL decides (H-3). Both put the device at the same low bytes:

| Port | Read | Write |
|---|---|---|
| `0xE3` | WD1772 status | WD1772 command |
| `0xEB` | WD1772 track | WD1772 track |
| `0xF3` | WD1772 sector | WD1772 sector |
| `0xFB` | WD1772 data | WD1772 data |
| `0xEF` | (open bus) | control latch, below |
| `0xE7` | page in (data: open bus / 0) | page out |
| `0xF7` | bit 7 = printer busy, other bits H-4 | printer data |

Control latch `0xEF` (write only):

| Bit | Meaning | Source |
|---|---|---|
| 0 | drive 1 select | S-MAME (`data & 1` -> drive 0) |
| 1 | drive 2 select | S-MAME (`data & 2` -> drive 1); Fuse: drive 2 only when bits 1-0 = `10` |
| 2-5 | unused | |
| 6 | printer strobe | both |
| 7 | side select (head 1 when set) | both |

Bits 1-0 = `00` selects no drive (MAME: `set_floppy(nullptr)`; Fuse keeps drive 1: a known TODO in Fuse). `11`:
MAME takes drive 1 (H-5).

## 5. WD1772 against the WD1793 we have

| Item | WD1793 (Beta 128) | WD1772 (+D) |
|---|---|---|
| Clock, density | 1 MHz or 2 MHz; `/DDEN` input | 8 MHz; DD only on the +D |
| Step rates `r1 r0` = 00 / 01 / 10 / 11 | 6 / 12 / 20 / 30 ms (at 1 MHz) | WD1772-00: 6 / 12 / 2 / 3 ms (S-WD); Fuse uses 2 / 3 / 5 / 6 (H-6) |
| Head load | `h` flag, HLD / HLT pins, 15 ms | no HLD; `h` (bit 3 of Type I/II/III) = **disable spin-up** |
| Motor | external (Beta: always on while selected) | internal `MO` output: on at a command, off after 9 index pulses with no command |
| Spin-up | none | 6 index pulses before the command runs, unless `h = 1` or the motor is already on |
| READY | input, status bit 7 = not ready | no READY: status bit 7 = **motor on** |
| Type I status bit 5 | head loaded | **spin-up complete** |
| Type II/III status bit 5 | record type / write fault | record type (read) / write fault |
| Side | side compare flag `S`, `C` in Type II | no side compare; Type II bit 1 = `P` (precompensation disable); side from the board latch |
| Settle `E` (Type II/III) and `V` (Type I) | 30 ms at 1 MHz, 15 ms at 2 MHz | 15 ms (the 1770: 30 ms) (S-WD) |
| Force Interrupt | I0 ... I3 | only I2 (index) and I3 (immediate) |
| INTRQ / DRQ | wired to the Beta port `0xFF` | not wired on the +D (G+DOS polls the status register; Fuse leaves `set_intrq` / `set_datarq` null) |

## 6. Disk format (G+DOS, MGT file system)

| Item | Value | Source |
|---|---|---|
| Geometry | 80 cylinders x 2 sides x 10 sectors x 512 bytes = 819 200 bytes (also 40 cyl / 1 side variants) | S-WIKI, `loader_mgt.h` |
| Sector ids | 1 ... 10; side 1 tracks are numbered with bit 7 set in the file chains | S-WIKI |
| Directory | cylinders 0-3 of side 0, 80 entries of 256 bytes, 2 per sector | S-WIKI |
| Entry | 0: type (bits 0-5; 0 = free), 1-10: name, 11-12: sector count (big-endian), 13-14: first track / sector, 15-209: sector map, 210-219: ZX tape header (type, length, start, ...) | S-WIKI |
| File types | 1 BASIC, 2 number array, 3 string array, 4 CODE, 5 48K snapshot, 6 Microdrive, 7 SCREEN$, 8 special, 9 128K snapshot, 10 opentype, 11 execute | S-WIKI |
| Sector chain | 510 data bytes + next track + next sector | S-WIKI |
| Image files | `.mgt` cylinder-major (c0s0, c0s1, c1s0, ...); `.img` side-major | `loader_mgt.h` |

## 7. Boot (G+DOS)

Read in S-ROM:

- The ROM carries the boot names `+SYS*` (file type 4, CODE) at `0x035C` and `AUTO*` at `0x036A`, and the message
  `No "+SYS  " file` at `0x1942`.
- `RUN` (the error hook at `0x0008` catches it) loads the first `+SYS*` file from drive 1 and then runs a file whose
  name starts with `AUTO`.
- The system file is 6656 bytes (`0x1A00`) loaded at `0x2000`, that is into the +D RAM (S-DISK: `+SYS 2a`, 14
  sectors, start `0x2000`).
- The system file carries the user's configuration (printer, drives, step rate): S-TAPE's `CONFIG` program writes it
  from the template `CONFIG2_C` (6656 bytes). Bodies therefore differ between disks. The clean system disk S-DISK holds one configuration (body SHA-256 prefix `ba1582991bed6c7c`).
- With the DOS in RAM, `LOAD D1"name"` loads BASIC / CODE / SCREEN$, `LOAD D1"name" S` a 48K snapshot,
  `LOAD D1"name" K` a 128K snapshot, `LOAD P<n>` the n-th directory entry, `CAT 1` lists (S-MAME header, manual).

## 8. Snapshot formats that carry the +D

| Format | Field | Source |
|---|---|---|
| `.z80` v3 | hardware mode 3 = "48K + M.G.T.", 6 = "128K + M.G.T." | `core/src/loaders/snapshot/loader_z80.cpp` |
| `.szx` | block `PLSD`: flags (paged, compressed, seek direction), RAM length, ROM length, ROM type (G+DOS / UNI-DOS / custom), control latch, drive count, track, sector, data, status; then 8 KB RAM and an optional custom ROM. Block `PDSK` names the disk images | S-FUSE `libspectrum/szx.c` |

## 9. DISCiPLE (P7)

The DISCiPLE (MGT, 1986) is the +D's predecessor: the same WD1772 and 8K RAM, an 8K GDOS 3 ROM
(`data/rom/gdos.rom`), plus two joystick sockets, the network, an inhibit button, a pass-through edge and the
printer port. S-MAME `mgt.cpp` (`spectrum_disciple_device`, noted "GDOS v3 all ok") and S-FUSE
`peripherals/disk/disciple.c` agree on everything below; it is what the card `disciple` implements.

| Port (low byte, A8-A15 not decoded) | Read | Write |
|---|---|---|
| `#1B` / `#5B` / `#9B` / `#DB` | WD1772 status / track / sector / data (register = A7..A6) | command / track / sector / data |
| `#1F` | bits 0-4 the Kempston socket (`000FUDLR`, active high); bit 6 printer not busy (no printer: 0, S-FUSE `#BF`); bits 5, 7 low | control latch (below) |
| `#3B` | - | network (not emulated) |
| `#7B` | the ROM back at `#0000`, the RAM at `#2000` | the RAM at `#0000`, the ROM at `#2000` |
| `#BB` | page in | page out |
| `#FB` | - | printer data |

Control latch (`#1F` write): bit 0 drive 1 (1) or drive 2 (0): one drive is always selected; bit 1 side; bit 2
`/DDEN` (1 = single density, FM); bit 3 ROM bank (A13 of a 16K ROM; the 8K GDOS ignores it); bit 4 inhibit
override; bit 5 the expansion select; bit 6 printer strobe; bit 7 the network line.

Traps (M1 fetch while paged out): `#0001`, `#0008`, `#0066`, `#028E` (not the +D's `#003A`). Each is gated by
"inhibit released, or latch bit 4"; a latch write with the button pressed and bit 4 clear pages out.

`/RESET`: paged out, memory not swapped, the latch keeps its value; then a hold-off (S-MAME `m_reset_delay`,
10 µs, 35 T at 3.5 MHz) in which no trap counts and `#7B` / `#BB` do nothing. Without it the trap at `#0001` (the
reset's second fetch) would page GDOS in: its own reset (`DI; XOR A; OUT (#1F),A; JP #02F2`) ends at the 48K ROM's
`START` (`#11CB`), which on a 128K skips the menu. The emulator measures the hold-off from the first fetch after
the reset (the T-state counter is reset with the machine).

GDOS 3 boots on `RUN` from drive 1 with the first CODE file whose name starts with `SYS` (G+DOS: `+SYS`), loaded
into the card RAM. The system tape (GDOS 3d, `CONFIG` with `CONFIG1_C`-`CONFIG3_C`) writes that file:
`data/boot/disciple/README.md`. `.z80` v3 byte 83 = 0 / 1 names the DISCiPLE (Epson / HP printer); SZX has no
DISCiPLE block (S-FUSE `libspectrum` writes none).

## Open items

| # | Question | How to settle |
|---|---|---|
| H-1 | Does the +D page in at `0x028E`? Fuse traps it for +D and DISCiPLE, MAME only for DISCiPLE | The ROM has a `KEY-SCAN` hook there (sec. 3): we follow Fuse. Final word: equations of the PAL `alice_pal20l8.ic4` (MAME region `plds`) |
| H-2 | Paged in or out after reset | Paged out, as Fuse in effect (`romcs = 0`); paged in breaks the 128K boot. The ROM's `0x0000` entry is then reached only by a jump. Final word: PAL equations |
| H-3 | Port decode width (`A1-A6` vs full low byte) | PAL equations; a program that writes the latch through mirror addresses would show it on hardware only |
| H-4 | Bits 0-6 of a `0xF7` read | PAL / board; not needed by G+DOS |
| H-5 | Control latch `11` and `00` | PAL / board schematic (drive select lines) |
| H-6 | Fuse's 2 / 3 / 5 / 6 ms table matches no part in S-WD (the -00 is 6 / 12 / 2 / 3); is the board's chip a -00, and which `r1 r0` does G+DOS write? | board photos (chip marking); the step bits G+DOS writes in Restore / Seek (a trace in P2). Until then: the -00 table of S-WD |
| H-7 | The first boot on a 128K: MAME notes that DISCiPLE paging must be held off "for a while after reset" on 128K; does the +D need the same? | **Settled 2026-11-17: no.** MAME holds off only the DISCiPLE (`m_reset_delay`), not the +D; on our 128K G+DOS boots from the menu's 128 BASIC. The 128K's ROM 0 makes no M1 fetch at a trap address during its boot; its IM 1 goes through `#5B00` to ROM 1's `#0038`, so `#003A` pages the +D in each frame as on a 48K. An early "128K does not boot" was the emulator's `RESET=` setting (48 BASIC by default), not the card |
| H-8 | +2A / +3 compatibility | G+DOS v1a on +2A ROMs: test on our +2A once P2 runs; expected refused (fit `unrealistic`) |
