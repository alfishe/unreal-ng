# ZX-Evo "D. CD boot": booting a Spectrum program from a CD

What the ZX-Evo BaseConf firmware does when you pick **D. CD boot** in its start menu, what a disc
needs to boot this way, which real software uses it, and how to try it in unreal-ng.

Status: research note, 2026-09-29. Ground truth is the pentevo ROM source; every claim about the
firmware cites a file and line. Claims we could not check are marked **(unverified)**.

Related documents:

- [BaseConf hardware reference, §4 IDE HDD and CD-ROM](../2026-09-15-atm-baseconf-highres-ports/baseconf-hardware-reference.md#4-ide-hdd-and-cd-rom)
  (short summary of the same code)
- [Storage technical design, §4 ATAPI CD-ROM](../2026-09-15-atm-baseconf-highres-ports/tdd-storage-sd-ide-cd.md#4-atapi-cd-rom-st-4)
- [IDE + ATAPI implementation plan](../2026-09-28-ide-atapi/implementation-plan.md) and its [TODO](../2026-09-28-ide-atapi/TODO.md)
- [Media guide: hard disks and the CD-ROM drive](../../features/media.md#hard-disks-and-the-cd-rom-drive-ide)
- Open questions and follow-ups for this note: [TODO.md](TODO.md)

## Contents

- [Glossary](#glossary)
- [The short answer](#the-short-answer)
- [1. What the ERS does, step by step](#1-what-the-ers-does-step-by-step)
- [2. What can be booted, and how to make a bootable disc](#2-what-can-be-booted-and-how-to-make-a-bootable-disc)
- [3. Real software that uses it](#3-real-software-that-uses-it)
- [4. How to try it in unreal-ng](#4-how-to-try-it-in-unreal-ng)
- [5. Emulator coverage: which drive commands are needed](#5-emulator-coverage-which-drive-commands-are-needed)
- [Sources](#sources)

## Glossary

| Term | Meaning |
|---|---|
| **ERS** | EVO Reset Service: the start menu of the ZX-Evo BaseConf ROM (the screen with "Z. TR-DOS boot", "B. HDD boot", "D. CD boot", ...). |
| **BaseConf** | The standard ZX-Evo FPGA configuration (Pentagon / ATM Turbo compatible). The other main one, TS-Conf, has its own BIOS without a CD boot. |
| **IDE, master / slave** | The ZX-Evo has one IDE connector ("NemoIDE" port layout). Two drives share it; a jumper on each drive makes it the *master* (device 0) or the *slave* (device 1). |
| **ATAPI** | How a CD drive talks over IDE: the computer sends a 12-byte *packet* (a SCSI command such as "read sector") through the IDE data port. |
| **Signature `#EB14`** | When asked "who are you?" with the hard-disk command IDENTIFY (`#EC`), a CD drive refuses and leaves `#14` / `#EB` in two IDE registers. That is how software tells a CD drive from a hard disk. |
| **Sector / LBA** | A CD data sector holds 2048 bytes. Sectors are numbered from 0; the number is the LBA (logical block address). |
| **TOC** | Table of contents of the disc: the list of tracks and the sector where each one starts. |
| **ISO 9660** | The standard CD file system. Sector 16 holds the *primary volume descriptor* (PVD); it points to the *root directory*, which is a list of *directory records* (name, first sector, size). |
| **Sense / unit attention** | After a failed command the drive keeps an error code (the *sense*). "Not ready, medium not present" means no disc; "unit attention, medium changed" means a disc was just inserted. |
| **`#6000`** | Hex numbers use the Spectrum `#` prefix: `#6000` = 24576. |

## The short answer

- **D. CD boot** looks for a CD drive on the IDE **slave**, reads the first data session of the disc,
  finds the file **`AUTORUN.ZX`** in the **root directory**, loads it to address **`#6000`** and jumps
  there with interrupts off.
- The file is raw Z80 code (no header), at most **32 KB** by the published standard (the ERS
  loader physically fits 34,816 bytes). Anything bigger must be loaded by the program itself, with
  its own CD driver.
- It is the ZX-Evo copy of the **"CD autorun" standard** Alone Coder published in 2006 with *Time Gal*,
  the first Spectrum game on CD. Real users: *Time Gal* (game, 185 MB of video), **DNA OS** (an OS
  shipped as a bootable ISO with its own ISO 9660 driver), Maksagor's NedoVIDEO Player and the
  *ZX-video CD No. 1* built on it, and `CDBOOT.COM` for iS-DOS.
  NedoOS does **not** boot from or read CDs; it has only an audio CD player.
- There is **no way out** once started: no disc, the wrong disc or a hard disk on the slave means the
  ERS retries forever until you insert a good disc or press reset.
- unreal-ng supports everything the ERS sends to the drive; the shipped ZX-Evo config has the CD drive
  on the slave.

## 1. What the ERS does, step by step

Code: `CDBOOT` in `pentevo/rom/mainmenu/src/menu_execute.a80:317-323`, the loader `CDBOOTGO` in
`pentevo/rom/mainmenu/src/hdd_cd_boot.a80:118-620`. The menu entry is
`pentevo/rom/mainmenu/src/menu_data.a80:211` ("D.CD boot"), bound to key `d` at `:220` and to
`CDBOOT` at `:226`, whose comment calls it "the old loader from the CD". The last change to the loader
file is ERS 0.58.03 (2019).

The loader is not ZX-Evo-specific code: it is the 2006 CD loader from *Time Gal*, byte for byte in the
parts that talk to the drive (compare `SEND_ATAPI`, `READCD` and the packets in
`pentevo/z80_soft/timegal/source/gal5evo.a80:1603-1698` with `hdd_cd_boot.a80:506-613`).

### Step 0: prepare memory

`CDBOOT` (`menu_execute.a80:318-323`):

1. Clears the screen (`CALL CLS`).
2. `MEMSET` (`pentevo/rom/mainmenu/src/main.a80:630-660`): **zeroes RAM pages 0, 1, 3, 4, 6 and 7**
   (`CLEAR_128K`, `main.a80:909-933`), sets the turbo from the Setup value (3.5 / 7 / 14 MHz,
   `:631-644`) and applies the Setup memory mode (full, 128K or 48K lock, `:645-660`).
3. `HDDBINI` (`hdd_cd_boot.a80:15-20`): writes `#10` to port `#7FFD` (RAM page 0 at `#C000`, normal
   screen, "48K" ROM bit) and copies the loader from ROM to **`#E800`** (the block assembled with
   `PHASE #E800`, `:50` to `DEPHASE` at `:621`). From here on the loader runs from RAM at `#E800`.
4. Interrupts on (`EI`), jump to `CDBOOTGO`.

The ERS stack is just below `#6000` (`main.a80:85` sets `SP=#6000` before the menu starts).

### Step 1: find the drive

The drive number is fixed at assembly time: `device EQU #B0` (`hdd_cd_boot.a80:137`), which is the
**slave**. There is no fallback to the master and no menu to choose. Every register access first
writes `#B0` to the drive/head register `#D0` (`SELDEVICE`, `:111-116`).

The IDE ports are the Nemo set (`:2-11`): data `#10`/`#11`, error `#30`, count `#50`, sector `#70`,
cylinder low `#90`, cylinder high `#B0`, drive/head `#D0`, status/command `#F0`.

| # | What the ERS does | Code |
|---|---|---|
| 1 | Wait until the drive is not busy (status bit 7 = 0). | `iniini`, `:229-233`; `NO_BSY`, `:100-103` |
| 2 | Send ATA **DEVICE RESET** (`#08`), wait not busy. | `:240-241` |
| 3 | Send ATA **IDENTIFY DEVICE** (`#EC`), wait not busy. A CD drive refuses it and leaves the signature. | `:247-248` |
| 4 | Wait 30 frames (`HALT` x 30, about 0.6 s). | `:251-253` |
| 5 | Read cylinder low/high (`#90`/`#B0`); if they are not `#14`/`#EB`, **start again at row 1**. | `:255-259`, `LEN_TO_HL` `:470-477` |

So a hard disk on the slave, or an empty slave, never gets past this loop (the ERS keeps resetting
the device). Note it does not use the IDE control register `#C8` here; only HDD boot does
(`:27-35`).

### Step 2: wake the drive up

| # | What the ERS does | Code |
|---|---|---|
| 6 | Send packet **TEST UNIT READY** (all zeros); the answer is ignored. | `:263-264`, packet `AP_00` `:577-579` |
| 7 | Send packet **SET CD SPEED** to 176 KB/s ("1x"). | `:266-267`, packet `AP_1x` `:582-586` |
| 8 | Wait 30 frames again. | `:268-270` |
| 9 | **Interrupts off** (`DI`). They stay off until the program runs. | `SKIPINI`, `:271-272` |

Every packet goes out the same way (`SEND_ATAPI`, `:507-534`): byte-count limit 2048 into `#90`/`#B0`,
command **PACKET** (`#A0`) into `#F0`, wait not busy, then six 16-bit words through `#11` (high byte
first) and `#10`. The ERS does not wait for the drive's "ready for data" bit before the packet words
and does not look at the error bit afterwards; real drives tolerate this (it was written for them).

### Step 3: read a sector until the disc answers

| # | What the ERS does | Code |
|---|---|---|
| 10 | Read sector 0 into `#6000` with **READ(10)**, one sector. The comment says READ TOC does not work without this first read. | `:277-278`; `READCD` `:537-570`; packet `AP_READ` `:598-603` |

How a sector read works (`READCD`): send READ(10) for the sector number in `SECTOR`, wait not busy,
then poll the "data ready" (DRQ) status bit up to 1024 times. If DRQ never comes (no disc, disc still
spinning up, read error), send TEST UNIT READY and **retry the same read, forever** (`:555-566`,
"waiting for DRQ by Budder's recipe"). When DRQ comes, copy 2048 bytes (`TRANS_IN`, `:481-504`).

This retry loop is what the user sees with an empty drive: the screen stays blank and the ERS
waits. Inserting a disc ends it: the next TEST UNIT READY clears the drive's "medium changed" report
and the following READ(10) succeeds.

### Step 4: find the last data track

| # | What the ERS does | Code |
|---|---|---|
| 11 | Send **READ TOC** (`#43`), format 0, sector numbers (not minutes/seconds/frames), up to 2048 bytes; copy the answer to `#6000`. | `:279-291`, packet `AP_READTOC` `:605-613` |
| 12 | Take the start sector of the **last track** listed before the lead-out. | `:297-300` |

Worked example. A one-track data CD answers READ TOC with 20 bytes:

```
00 12  01 01   | length #0012 (18 bytes follow), first track 1, last track 1
00 14 01 00  00 00 00 00   | track 1: data track, starts at sector 0
00 14 AA 00  00 00 5D C0   | lead-out (track #AA), starts at sector 24000
```

The ERS takes the low byte of the length, `#12`, subtracts 10 and gets `#08`: the address `#6008`
is where the start sector of the last real track sits (here track 1, sector 0). With more tracks the
same formula lands on the last one. The comment says "the last session"; it is really the last
track. Consequences:

- a normal ISO (one data track) works;
- a multi-session disc works when its last session is the one with the files;
- a "mixed mode" disc (data track 1 plus audio tracks after it) **does not**: the ERS picks the last
  audio track, the READ(10) of an audio sector fails, and step 3's retry loop never ends;
- only the low byte of the TOC length is read, so the formula is valid up to 30 tracks.

### Step 5: read the ISO 9660 volume and the root directory

| # | What the ERS does | Code |
|---|---|---|
| 13 | Read **17 sectors** from the track start to `#6000`-`#E7FF` (34,816 bytes; the comment says "#8800 bytes"). Sector 16 of the track, the primary volume descriptor, lands at **`#E000`**. | `:301-303`; `LOADER` `:358-391` |
| 14 | From the PVD's root directory record (PVD offset 156), take the root directory's start sector (big-endian copy, offset 156+6) and its size (the low 16 bits of the little-endian copy, offset 156+10). | `:305-306` (`#6000 + #80A2 = #E0A2`) |
| 15 | Read the root directory to `#6000`, rounded up to whole sectors. | `:308` |

`LOADER` turns a byte size into a sector count by rounding up (`:372-377`) and reads the sectors one by
one with `READCD`, adding 1 to the big-endian sector number each time (`:380-389`).

The ERS does **not** check for the `CD001` marker or the descriptor type. It reads the fixed offsets
of whatever is at sector 16. It ignores Joliet and Rock Ridge (the Joliet descriptor is at sector 17
and later, which the ERS never looks at), subdirectories and the path table.

### Step 6: find AUTORUN.ZX

`FNDIDLOOP` (`:314-356`) walks the directory records from `#6000`:

- Compare the **first 10 characters** of the name (record offset 33) with `AUTORUN.ZX` (`:316-324`,
  name at `:572-574`). Consequences:
  - upper case only (ISO 9660 level 1 names are upper case anyway);
  - the `;1` version suffix that mastering tools add is not checked, so `AUTORUN.ZX;1` matches;
  - the name length byte is not checked either, so a name that merely *starts* with `AUTORUN.ZX`
    also matches.
- Not a match: step to the next record by its length byte (`:340-352`). A zero length means padding
  to the end of the sector; the ERS then skips to the next 256-byte boundary and continues, which
  works because padding is zeros.
- The walk stops only when the address passes `#FFFF` and wraps below `#4000` (`:353-355`). It does
  not stop at the end of the directory. After the directory it walks over the leftovers of step 13
  (the zero-filled system area, then the PVD copy at `#E000`, whose second byte `C` makes it jump out
  of range). An ISO whose system area (sectors 0-15) is not empty, such as a "hybrid" ISO with a
  hard-disk boot record, can make this walk do odd things **(unverified in practice)**.
- **No match: `RST 0`** (`:356`), a jump to address 0. With the mapping the ERS runs in, that is most
  likely a restart of the ERS menu **(unverified; see [TODO](TODO.md))**. There is no message.

### Step 7: load and start

| # | What the ERS does | Code |
|---|---|---|
| 16 | From the matched record take the start sector (big-endian, offset 6) and the size (low 16 bits of offset 10); read that many sectors, rounded up, to **`#6000`**. | `:325-333` |
| 17 | Push `#6000`, load the entry registers, `RET` into the program. | `:334-339` |

The loader itself sits at `#E800`. A file bigger than 17 sectors (34,816 bytes) overwrites the loader
while it is still running and crashes. The size is taken modulo 64 KB, so a 70,000-byte file is
loaded as a 4,464-byte one.

### Error and retry behavior, in one table

| Situation | What happens | Why |
|---|---|---|
| No device on the slave, or a hard disk there | The screen stays blank; the ERS resets and re-identifies the slave forever (with no device at all it can also hang in the busy wait). | Step 1, row 5 jumps back to `iniini` (`:259`). |
| CD drive, no disc | Blank screen; READ(10) + TEST UNIT READY forever. Insert a disc and it boots. | Step 3 retry (`:555-566`). |
| Audio CD, or a mixed-mode disc | Same endless retry (the data read of an audio track fails). | Steps 3/4. |
| Data CD without `AUTORUN.ZX` in the root | `RST 0`: back to address 0, most likely the ERS menu **(unverified)**. | `:356` |
| `AUTORUN.ZX` bigger than 34,816 bytes | Crash while loading. | Loader at `#E800`. |
| Good disc | Program starts in well under a second after the disc is ready. | |

No step reads the keyboard: the only way out of a retry loop is the reset button (or inserting a
good disc).

## 2. What can be booted, and how to make a bootable disc

### The disc

| Requirement | Detail |
|---|---|
| File system | ISO 9660, primary volume descriptor at sector 16 of the (last) data track. Joliet / Rock Ridge extras are harmless and ignored. |
| Tracks | One data track (or data in the last session). No audio tracks after the data. |
| File name | `AUTORUN.ZX` in the **root directory**, upper case (the `;1` suffix is fine). |
| File format | Raw Z80 code, no header (no Hobeta or TAP header). The first byte is the first instruction. |
| File size | **At most 32,768 bytes** (`#8000`) by the published standard; the ERS loader fits 34,816. |
| Root directory | At most 17 sectors (a few hundred files); a normal disc has a 1-sector root. |
| Drive | Jumpered as **slave** on the ZX-Evo IDE connector. |

### What the program finds when it starts

From the code (`hdd_cd_boot.a80:120-142` for the documented convention, `:334-339` for the values
actually passed):

| Register / state | Value on ZX-Evo | Meaning |
|---|---|---|
| `PC` | `#6000` | start of the file |
| `A` | `#B0` | the CD drive is the IDE slave (`#A0` would be master); use it for your own driver |
| `B` | `0` | computer type: 0 Pentagon, 1 ATM, 2 Scorpion, 3 Profi, 4 Sprinter. ZX-Evo says **Pentagon** |
| `C` | `0` | IDE controller: 0 Nemo, 1 ATM, 2 SMUC, 3 Profi, 4 Sprinter. ZX-Evo says **Nemo** |
| `D` | `1` | language: 0 English, 1 Russian. **Always 1** (a constant in the loader, `:140`) |
| `E` | `#FB` | Covox port (`#FB` = ATM / Pentagon; `#FF` would mean none) |
| `HL` | `4` | video modes available, a bit set: 1 = 512x192 mono, 2 = 384x304, 4 = 256x192 16-color, 8 = 320x200 16-color ATM. ZX-Evo passes only **4** (the source has `;8` commented next to it, `:142`) |
| Interrupts | **off** (`DI`) | the standard adds "IM 1" |
| `SP` | just below `#6000` | the ERS menu stack (the standard says `SP=#6000`); **set your own** |
| Port `#7FFD` | `#10` | RAM page 0 at `#C000`, normal screen (page 5), ROM bit set |
| RAM | pages 0, 1, 3, 4, 6, 7 **zeroed** before loading; page 5 holds the ERS screen and stack; page 2 is `#8000`-`#BFFF` | `CLEAR_128K` |
| `#6000`-... | your file | |
| `#E000`-`#E7FF` | the volume descriptor copy, unless your file is bigger than 32 KB | step 13 |
| `#E800`-... (page 0) | the ERS CD loader code | no documented entry points: do not call it |
| CPU speed, memory mode | as set in the ERS Setup | `MEMSET` |
| ROM at `#0000` | whatever the ERS runs in: most likely the ERS page, **not** BASIC **(unverified)** | do not call ROM routines without checking |

The 2006 standard, as published in *Info Guide* #09, says (translated): "the root directory holds a
file `AUTORUN.ZX` no longer than `#8000` bytes. It is loaded at `#6000` (port `#7FFD` = `#10`) and
started at `#6000` with interrupts disabled, IM 1. SP = `#6000`, IY = 23610, A = `#A0` (master) or
`#B0` (slave), B = computer type, C = IDE controller type, D = language, E = Covox port, HL = video
modes." The ERS follows it except for the stack and the fixed B/C/D/HL values above; `IY` and `IM`
are not set by the CD loader itself **(unverified whether the ERS leaves IY = 23610 and IM 1)**.

**The program gets no disc services.** To read anything else from the CD it needs its own ATAPI
driver, which is exactly what *Time Gal* and *DNA OS* do: the `AUTORUN.ZX` is a small loader that
reads the rest of the disc. `A` and `C` are passed precisely so that the program can find the drive.

### Worked example: a bootable ISO

A program that turns the border red, paints the screen and stops, assembled with sjasmplus:

```asm
        DEVICE ZXSPECTRUM128
        ORG #6000
start:  di                      ; already off; stay off (no IM 2 table set up)
        ld sp,#6000             ; own stack below the program
        ld (drive),a            ; #B0: the CD drive is the IDE slave
        ld a,2
        out (#fe),a             ; red border: the program is running
        ld hl,#5800             ; fill the 768 attribute bytes
        ld de,#5801
        ld bc,767
        ld (hl),%00010110       ; paper red, ink yellow
        ldir
        jr $                    ; stop here
drive:  db 0
        SAVEBIN "cd/AUTORUN.ZX",start,$-start
```

```bash
mkdir -p cd
sjasmplus hello.asm                              # writes cd/AUTORUN.ZX (27 bytes)

# Any of these builds the same kind of image: ISO 9660 level 1, names in upper case.
xorriso -as mkisofs -V ZXBOOT -o hello.iso cd    # xorriso
genisoimage -V ZXBOOT -o hello.iso cd            # Debian / Ubuntu
mkisofs -V ZXBOOT -o hello.iso cd                # cdrtools

isoinfo -l -i hello.iso                          # should list AUTORUN.ZX;1 in "/"
```

Things to avoid when mastering:

- `-isohybrid-mbr` or other "hybrid" options: they put boot code in sectors 0-15 (see step 6).
- Options that keep lower-case or long names (`-allow-lowercase`, `-iso-level 4`, `-l` with a
  lower-case source name): the ERS compares upper case `AUTORUN.ZX`. Name the source file
  `AUTORUN.ZX` and keep the defaults.
- Adding audio tracks after the data track (step 4).

On macOS, `hdiutil makehybrid -iso -default-volume-name ZXBOOT -o hello.iso cd` also makes an ISO 9660
image **(unverified: the name mapping it applies)**.

If you do not have an assembler, the 12-byte program the unreal-ng test uses works as well
(`DI; LD (#9000),A; LD HL,#C0DE; LD (#9001),HL; JR $`):

```bash
mkdir -p cd
printf '\xF3\x32\x00\x90\x21\xDE\xC0\x22\x01\x90\x18\xFE' > cd/AUTORUN.ZX
xorriso -as mkisofs -V ZXBOOT -o tiny.iso cd
```

For a program larger than 32 KB, follow DNA OS's pattern: its 14.5 KB `AUTORUN.ZX` starts with
`LD HL,#600E / LD DE,#BE00 / PUSH DE / LD BC,#4000 / LDIR / EI / RET` (moves itself to `#BE00` and
continues there), then reads the rest of the system from the CD with its own driver.

The minimal ISO the unreal-ng test builds by hand (`MakeIso` in
`core/tests/emulator/machines/zxevo/zxevo_ers_test.cpp`) shows the least the ERS needs: the PVD
at sector 16 (type 1, `CD001`, root directory record at offset 156 pointing to sector 18), a
terminator at sector 17, the root directory at sector 18 with `.`, `..` and `AUTORUN.ZX;1`, and the
file at sector 20.

## 3. Real software that uses it

### Verified (we inspected the files or the source)

| Software | What it is | Evidence |
|---|---|---|
| **Time Gal** (Alone Coder, Shiru; January 2006) | Full-motion-video game, "the first CD game for the Spectrum". The disc is a 185 MB ISO; the program is an `AUTORUN.ZX`-convention loader that streams video from the CD. For Pentagon + 16-color, ATM Turbo 2/2+, ZX-Evo. | Source in `pentevo/z80_soft/timegal/source/gal5evo.a80`: `ORG #6000` (`:113`), reads the entry registers of the standard (`:123-128`, `:155-171`), own ATAPI driver (`:1603-1698`), finds its data files on the CD by name (`SCANCAT`, `:1313-1362`; the file table `TFILES` in `galscrip.a80:722-1254`). ZX-Evo build `timegal_evo.a80` (savelij, 2014). ISO on the Internet Archive (item `timegal-zx`), download on Alone Coder's site. |
| **Alone Coder's CD autorunners** `CDRUNATM.$B`, `CDRUNPEN.$B` (2006) | Tiny TR-DOS BASIC programs that do the CD boot on an ATM Turbo 2+ or a Pentagon with a Nemo IDE: "insert the disc, reset, run the launcher". The ERS "D. CD boot" is the ROM version of these. | `TGCDBOOT.zip` from Alone Coder's site (2 files, 785 bytes each); *Info Guide* #09 article. |
| **DNA OS** (ZET-9; ISO dated 2007) | A disk operating system for ATM Turbo / Nemo IDE, distributed as a **bootable CD image** `dna_nemo.iso` (324 KB). Its root holds `AUTORUN.ZX` (14,592 bytes), `DNA_OS.SYS`, `SHELL.SEP` and a set of viewers/players. The kernel has a CD driver and an **ISO 9660 file system** (strings `CDR_DRV`, `CD_DFS`, `ISO-9660` in `AUTORUN.ZX`). | `dna_nemo_iso.zip` from Alone Coder's site, root directory listed by us. Whether it runs on ZX-Evo / in unreal-ng: **not tried yet** ([TODO](TODO.md)). |
| **NedoVIDEO Player v1.00** (`VPLAYER.ZX`, Maksim Timonin "Maksagor", July 2006) | The Time Gal video engine plus a menu of up to 16 clips, for ATM Turbo 2+ with a slave CD drive; rename to `AUTORUN.ZX` to make a video disc (the clip names are patched into the binary, its help says where). **Not for ZX-Evo:** it drives the CD through the ATM IDE ports (`#FE0F`/`#FF0F` data, `#FECF`, `#FEEF`, ...), opened by `OUT (#4177)` through the TR-DOS `#3D2F` trap (CP/M mode, `#FF77` A9 = 0). BaseConf has only NemoIDE; its `#F8EF-#FFEF` are the RS232 port (`base_trdemu/trunk/z80/zports.v:212`). In unreal-ng on `ATM3` the ERS boots it and its menu runs; starting a clip loops forever on `IN (#FEEF)`. A ZX-Evo version would need its IDE driver moved to the Nemo ports, as the Time Gal ZX Evo fix did. On `ATM710` with the CD on the slave it **plays** in unreal-ng (2026-09-29): ATM BIOS → SPECTRUM 128 → TR-DOS → `RUN "boot"` (the hobeta name of `CDRUNATM.$B`); the launcher resets the drive, reads the TOC and the root directory, loads `AUTORUN.ZX`; after ENTER the player streams `ZXTVCLIP.ZXV` with READ (10) at consecutive blocks, about 3.8 KB per frame. | `vplayer.zip` (`VPLAYER.ZX`, 4,938 bytes, and `vplayer.hlp`) from atmturbo.nedopc.com, `download/cdsoft/zxvid1/`. |
| **ZX-video CD No. 1** (NedoPC, July 2006) | A bootable ISO (455 MB): NedoVIDEO Player as `autorun.zx` and 16 clips (`.zxv`: up to 15 fps, Covox 17.5 kHz, 224x160). | `zxvid1.zip` from atmturbo.nedopc.com, `download/cdsoft/zxvid1/`; ISO listed by us. |
| **CD-Pack for iS-DOS** (Yuri Korsunin, Maksim Timonin; TASiS / Chic / Classic) | `CDPLAYER.COM` (audio CD), `CDCOPY.COM` (CD to iS-DOS devices, mounts disc images), `CDIMG.BLK` (driver for iS-DOS device images on a CD), `CDTUNE.COM`, and **`CDBOOT.COM`**, which finds and starts `AUTORUN.ZX` from iS-DOS. ATM2 IDE only. | `cd_pack.ipc` and its description page, atmturbo.nedopc.com `download/isdos/cd_pack/`. |

### Reported but not checked

| Software | Claim | Source |
|---|---|---|
| **xBIOS** (ATM Turbo 2+ firmware) | Has its own CD boot. In 2024 lvd added READ CD (`#BE`), MODE SENSE(10) and READ CAPACITY to NedoPC's Unreal Speccy fork so that "timegal now boots through xbios" (`zxevo.pentevo/changelog.md:203`, emulation in `tools/unreal_fix/0.39.0/nedopc/hdd.cpp:857-1040`). That suggests xBIOS uses READ CD. | nedoos.ru "xBIOS v1.36" page (not reachable: TLS error); the changelog. |
| **Time Gal on ZX-Evo** | Users on zx-pk.ru (2012) and the TS-Labs forum report a freeze after the title with some drives, fixed by the "ZX Evo fix" (two files replaced in the ISO), and strong dependence on drive model. | zx-pk.ru thread 19310; TS-Labs forum topic 887; Alone Coder's page lists "90k ZX Evo fix". |

### Checked and not found

| Where | Result |
|---|---|
| **NedoOS** (local checkouts `NedoOS`, `NedoOS-dev`, `svn/nedoos`) | **No CD boot and no ISO 9660 driver.** The FatFs IDE driver recognizes a CD drive (signature `#EB14`, `NedoOS/src/fatfs4os/savelij.asm:124-126`, `:383-389`) only to skip it: it initializes hard disks only (`:127-132`). The one CD program is **`cdplay.com`**, an **audio CD player** (Kulich, May 2026; `NedoOS/src/kapps/cdplay/main.c`, shipped in `release/bin`). It works only with a slave drive and uses READ TOC in MSF form (`:483-484`), PLAY AUDIO MSF `#47` (`:575`), PAUSE/RESUME `#4B` (`:606`), READ SUB-CHANNEL `#42` (`:615-616`), STOP PLAY `#4E` (`:105`), START STOP UNIT eject/close `#1B` (`:98-102`), IDENTIFY PACKET DEVICE `#A1` (`:306`). NedoOS's `CD0=`/`CD1=` lines (`us/emul.ini:488-497`) are Unreal Speccy settings for the emulator, not OS features. |
| **ERS FAT browser** (`pentevo/rom/page5/source/fat/nemo_drv.a80`) | Defines error 7 "CD/DVD detected" (`:19`, `:235`) but the check is commented out (`:274-283`). It reads hard disks only. |
| **TS-Conf BIOS** (`zx-evo/pentevo/rom/src/tsfat.asm:1690-1691`) | The ATAPI signature check is commented out; no CD boot. |
| **zx-evo-docs**, **zx-evo-tsconf** | No mention of CD, ATAPI or `AUTORUN.ZX`. |
| **Wild Commander**, TR-DOS CD images, other demos | Nothing found. Wild Commander's sources are not in the local checkouts; we found no report of CD support **(unverified)**. We found no demo or TR-DOS collection distributed as an `AUTORUN.ZX` disc. |
| speccy.info (SpeccyWiki) | Not consulted (it blocks automated fetching). |

In short: the standard has a handful of real users, all from 2006-2007 and all from the NedoPC /
ATM Turbo circle. *Time Gal* is the one people still try on ZX-Evo; *DNA OS* is the only operating
system we found that boots from a CD and reads ISO 9660.

## 4. How to try it in unreal-ng

The shipped ZX-Evo config (model `ATM3`, `data/configs/atm3/unreal.ini:217-234`) uses the
`NEMO-DIVIDE` IDE board with **`CD1=1`**: the slave is a CD drive, where the ERS looks for it.

1. Make an ISO ([worked example](#worked-example-a-bootable-iso)).
2. Put it in the drive:
   - CLI: `media insert cd hello.iso` (`cd` is the alias of the first CD unit, here `ide0.slave`);
   - Qt: the media panel, CD unit;
   - WebAPI / MCP / Lua / Python: the same media insert verb on slot `ide0.slave`
     ([media guide](../../features/media.md#hard-disks-and-the-cd-rom-drive-ide)).
3. Reset to the ERS menu and press **D**.

What the emulator does, checked by tests in `core/tests/emulator/machines/zxevo/zxevo_ers_test.cpp`:

- `ZXEvoErs_Test.CdBootRunsAutorunFromAnIso`: with the disc in, "D. CD boot" loads `AUTORUN.ZX` to
  `#6000` and enters it with `A = #B0`.
- `ZXEvoErs_Test.CdBootSeesTheDiscEjectedAndInsertedAgain`: with the drive empty, the drive reports
  "not ready, medium not present" and the ERS keeps retrying READ(10); inserting the disc (with the
  3-second swap delay a user would see) produces "unit attention, medium changed", the ERS clears it,
  and `AUTORUN.ZX` runs.

`state ide` (WebAPI `/state/ide`, MCP aspect `ide`) shows the drive's last packet and sense data,
which is the quickest way to see where a boot is stuck.

Limits of the emulated drive: data CDs only (`.iso`, ISO 9660 image of 2048-byte sectors), read-only,
one data track (READ TOC always reports track 1 plus the lead-out). No audio tracks, no CUE/BIN, no
multi-session images.

## 5. Emulator coverage: which drive commands are needed

The emulated drive (`core/src/emulator/io/ide/ata/atapicdrom.cpp`) accepts: TEST UNIT READY, REQUEST
SENSE, INQUIRY, MODE SENSE (6 and 10), START STOP UNIT, PREVENT ALLOW, READ CAPACITY, READ(10)/(12),
SEEK, READ TOC (formats 0 and 1, LBA or MSF), GET EVENT STATUS, SET CD SPEED; plus the ATA commands
DEVICE RESET, IDENTIFY PACKET DEVICE and PACKET (IDENTIFY DEVICE is refused with the signature, as on
a real drive).

| Software | Commands it sends | unreal-ng |
|---|---|---|
| **ERS "D. CD boot"** | ATA `#08`, `#EC`, `#A0`; packets TEST UNIT READY `#00`, SET CD SPEED `#BB`, READ(10) `#28`, READ TOC `#43` format 0 LBA | **All supported.** READ CD (`#BE`) appears only in commented-out code (`hdd_cd_boot.a80:590-596`). |
| **Time Gal** (ZX-Evo build) | Same set as the ERS (`gal5evo.a80:1661-1698`; READ CD commented out at `:1675-1681`) | Supported as far as the source shows; the full game **not tried** ([TODO](TODO.md)). |
| **DNA OS** CD driver | Unknown (binary only) | **Not tried** ([TODO](TODO.md)). |
| **xBIOS** CD boot (ATM Turbo 2+) | Probably READ CD `#BE`, MODE SENSE(10), READ CAPACITY (inferred from lvd's Unreal change) | **READ CD not supported.** Only matters on an ATM Turbo 2+ running xBIOS; the shipped ATM710 config has no CD drive (`CD1=0`). |
| **NedoOS `cdplay.com`** | PLAY AUDIO MSF `#47`, PAUSE/RESUME `#4B`, READ SUB-CHANNEL `#42`, STOP PLAY `#4E`, READ TOC (MSF), START STOP UNIT | **Not supported:** no audio tracks and none of `#42`/`#47`/`#4B`/`#4E`. The drive answers ILLEGAL REQUEST. |

Nothing the ERS CD boot needs is missing. The gaps are all outside it (audio CD, READ CD).

## Sources

Local source trees (paths relative to the checkouts under the emulators folder):

- `pentevo/rom/mainmenu/src/hdd_cd_boot.a80` (the loader), `menu_execute.a80:317-323`,
  `menu_data.a80:211-226`, `main.a80:77-91`, `:623-660`, `:909-933`
- `pentevo/z80_soft/timegal/source/gal5evo.a80`, `timegal_evo.a80`, `make_hobeta.a80`, `galscrip.a80`
- `pentevo/rom/page5/source/fat/nemo_drv.a80`, `zx-evo/pentevo/rom/src/tsfat.asm`
- `NedoOS/src/fatfs4os/savelij.asm`, `NedoOS/src/kapps/cdplay/main.c`
- `zxevo.pentevo/changelog.md:202-203`, `zxevo.pentevo/tools/unreal_fix/0.39.0/nedopc/hdd.cpp`

unreal-ng:

- `core/tests/emulator/machines/zxevo/zxevo_ers_test.cpp` (`MakeIso`, the CD boot tests)
- `core/src/emulator/io/ide/ata/atapicdrom.cpp`, `data/configs/atm3/unreal.ini`

Web (fetched 2026-09-29):

- *Info Guide* #09 (2006): "about the game Time Gal, the first CD game for ZX" (the CD autorun
  standard) — <http://zxpress.ru/article.php?id=8645>; "Video Player for ATM" —
  <https://zxpress.ru/article.php?id=8646>
- Alone Coder's ZX page (Time Gal, `TGCDBOOT.zip`, `dna_nemo_iso.zip`) — <http://alonecoder.nedopc.com/zx/>
- Direct downloads (checked 2026-09-29): Time Gal ISO <https://archive.org/download/timegal-zx/timegal.iso>
  (the same image, identical SHA-256, is in <http://atmturbo.nedopc.com/download/cdsoft/time_gal/timegal.7z>);
  ZX Evo fix <http://alonecoder.nedopc.com/zx/TGAL_SRC.rar>; launchers
  <http://alonecoder.nedopc.com/zx/TGCDBOOT.zip>; DNA OS ISO
  <http://alonecoder.nedopc.com/zx/dnaos/dna_nemo_iso.zip>; NedoVIDEO Player
  <http://atmturbo.nedopc.com/download/cdsoft/zxvid1/vplayer.zip>; ZX-video CD No. 1
  <http://atmturbo.nedopc.com/download/cdsoft/zxvid1/zxvid1.zip>; iS-DOS CD-Pack
  <http://atmturbo.nedopc.com/download/isdos/cd_pack/cd_pack.ipc>
- Time Gal on the Internet Archive — <https://archive.org/details/timegal-zx>; on pouët —
  <https://www.pouet.net/prod.php?which=64437>
- zx-pk.ru, "Time Gal - ZX Evolution (PentEvo)" —
  <https://zx-pk.ru/threads/19310-time-gal-zx-evolution-(pentevo).html>
- TS-Labs forum, "how to run time gal on the zx evolution" —
  <https://forum.tslabs.info/viewtopic.php?f=11&t=887>
