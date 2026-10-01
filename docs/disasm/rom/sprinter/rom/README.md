# ROM page 0: disk drivers and SETUP

Listings: [bios304-p0-drivers.asm](bios304-p0-drivers.asm) (the page as it sits in the ROM),
[bios304-p0-setupstub.asm](bios304-p0-setupstub.asm) (page 0 `#1000-#115E` where it runs, `#8000`),
[bios304-setup.asm](bios304-setup.asm) (SETUP unpacked, `#8000`) · symbols:
[data/symbols/sprinter/](../../../../../data/symbols/sprinter/) · overview: [../README.md](../README.md)

## Layout of the page

| Range | Name | Contents |
|---|---|---|
| `#0000-#0001` | `RomStart` | `DI : HALT` (the page is never entered at `#0000`) |
| `#0004-#0007` | `RomChecksum` | `#3B #0D #05 #80` (ZXMAK2 copy: `#58 #7E #83 #D4`) |
| `#0038-#0085` | `IntHandler`, `CallUserInt` | interrupt handler with a user hook (system page `#C124-#C127`) |
| `#0100`, `#0107` | `FromPage8Call`, `FromRamCall` | entries from the page stubs |
| `#010E-#041F` | `OldHddDispatch`, `Fn40…_old` | the old HDD API `#40-#47` (page 8 serves these itself in 3.04) |
| `#0420-#05C0` | `DiskFnDispatch`, `Fn51`…`Fn5F` | the disk API `#50-#5F` and the per-device switches |
| `#05C1-#0980` | `FDRIVER2` names | floppy driver (WD1793), density probe `#0669` |
| `#0981-#0CCF` | `HDRIVER6` names | IDE hard-disk driver (LBA and CHS) |
| `#0CD0-#0EBB` | `CD_5x`, `AP_COM` | ATAPI CD-ROM driver (packet commands at `#0E8C`) |
| `#1000-#115E` | `SetupStub` | runs at `#8000`: see the stub listing |
| `#115F-#3209` | `SetupPacked` | SETUP, Hrust 1.x |
| `#3FD0-#3FFF` | page stubs | `OUT (#7C),0/1` and `OUT (#3C)` glue matching page 8 |

## The disk API (`#50-#5F`)

A = device (`#00-#0F` floppy, `#80-#8F` IDE, `#C0-#CF` CD-ROM), HL:IX = sector number, B = count,
DE = buffer. `#51` reset, `#52` long read, `#53` long write, `#54` verify, `#55` read, `#56` write,
`#57` detect, `#58`/`#59` get/set media parameters, `#5A` version, `#5F` device list; `#50`,
`#5B-#5E` return "not supported". `#5A` returns DE = `#0235`: disk subsystem **2.53** (D = 2,
E = `#35` = 53), the "253" in the build name (the 2.17 sources return 2.41).

**3.04 has an ATAPI CD-ROM driver** in this page (it matches BIOS-TT's `CD_DRIVER_0.asm`); each
device switch gained a `CP #C0 / CP #D0` branch that the 2.17 sources only have as comments. SETUP
cannot boot from a CD (`BootCdRom` prints a message and fails). The device list (`#5F`) counts only
two IDE units (`#C1C0` master, `#C1C8` slave) - one channel.

## Floppy density

The density is bit 7 of `#C1E0` in the system page (`#FE`): 1 = HD (1.44 MB, 500 kbit/s),
0 = DD (720 KB, 250 kbit/s). `FddSetDensityDD` / `FddSetDensityHD` (`#0977`/`#097C`) write it to the
hardware with `OUT (#BD),A`, A = `#01` / `#21` → ports `#01BD` / `#21BD` → port-table codes `#16` /
`#17`.

**The probe** (`FddProbeDensity`, `#0669`, "DISK_ID" in the 2.17 sources), called by reset and
detect:

1. apply the current density and seek (command `#18`);
2. READ ADDRESS (`#C0`) to port `#0F` (the WD1793 command register through the port table; the
   code runs from ROM, so the `#1F` operand rewrite does not apply), then poll port `#FF`
   (bit 7 INTRQ, bit 6 DRQ) for at most `#F000` loops;
3. on a time-out flip the density (`FddFlipDensity`, `#0638`) and go back to step 2: four tries in
   all, two per density.

Worked example: a 1.44 MB disk while the latch is at DD. Try 1 times out (no ID field can be read at
250 kbit/s), the density flips to HD, try 2 reads an ID field; the routine returns A = `#80` (HD) and
every later read of that drive runs at 500 kbit/s. The emulator's WD1793 `Latched` clock policy and
the new real-floppy tests (`LoaderRawPcFloppyDss_Test`) cover exactly this case.

## SETUP: how the BIOS boots

SETUP (13 893 bytes, run at `#8000`) is the 3.04 version of `DSETUP.ASM` (Setup 2.41 of BIOS 2.17);
its copyright string says 2002.

1. `SetupStart` (`#81BE`): keyboard (`KeyboardInit`, SIO channel A), clear the screen, read the CMOS
   settings; a bad checksum loads the defaults.
2. Clear the user interrupt hook, TR-DOS quick-start check, floppy drive table, clear memory.
3. Screen position from CMOS `#1F` (`ApplyScreenPosition`: a temporary port-table entry gives the
   PLD's screen position register, code `#CB`, a port).
4. Warm start (signature at `#F000` of the system page)? Skip the screen. Else draw it: logo, BIOS
   id, memory (function `#C0`), CMOS clock or "no CMOS".
5. Keys: **DEL** = setup menu, **ESC** = Spectrum mode.
6. IDE auto-detect, then the boot: `BootSysDevice` (CMOS `#10` low nibble), on failure
   `BootAltDevice` (high nibble); 0 floppy A (`#00`), 1 floppy B (`#01`), 2 IDE master (`#80`),
   3 IDE slave (`#81`), 4 RAM disk (`#6E`).
7. Floppy: function `#51` first (this runs the density probe).
8. `LoadBootSector` (`#8451`): function `#55`, sector **LBA 1** (`HL:IX = #0000:#0001`), 1 sector to
   `#7E00`; the first 12 bytes must be `Starting...` + `#00` (`BootSignature`, `#8486`).
9. `RunBootSector` (`#8492`): a 26-byte mover at `#7C00` copies the sector to `#8000` and jumps to
   `#800C` with A = device code. The DSS loader takes over (hardware-reference §14).

CMOS registers used here: `#0E` (options: memory test, safe RAM disks, start delay, language, quick
start), `#10` (boot devices), `#1F` (screen position), and `#3F` for the presence test (page 8).

**Keyboard.** The IM 2 handler (`SetupIntHandler`, `#8101`) calls `KeyboardInterrupt` (`#9EF0`): while
SIO A status (`#19`) bit 0 says a byte arrived, read it from `#18` (AT set 2 scan codes, `#E0`/`#F0`/
`#E1` prefixes), translate it and put it into the key buffer.

## Differs from the source

Code regions (≥ 24 bytes) with no matching run in the reference (`scripts/diffregions.py`).

Drivers vs BIOS-PP 1273243 `EXTENDED.ASM` + BIOS-TT 0271ac3 (97 % of 3 553 code bytes match):

| Range | Bytes | Near label |
|---|---|---|
| `#050C-#0532` | 39 | `Fn51_DRV_RESET` (the CD-ROM branches of the device switches) |

SETUP vs BIOS-PP 1273243 `DSETUP.ASM` (80 % of 4 742 code bytes match):

| Range | Bytes | Near label |
|---|---|---|
| `#81F3-#8215` | 35 | `SetupStart` (screen position from CMOS `#1F`) |
| `#8372-#83C3` | 82 | `ApplyScreenPosition`; the RTC control registers A-D (`#0A-#0D`) set to `#26 #02 #50 #80` when A or C differ |
| `#853E-#85A6`, `#85B6-#85E9`, `#85FA-#8617` | 187 | `FILLIDE` (IDE detection, CD-ROM units) |
| `#876F-#87B8` | 74 | `PIDNUM` (version / id printing) |
| `#8916-#8941` | 44 | `OPENDOS` |
| `#8B90-#8BCE` | 63 | `FADE` (logo) |
| `#9630-#964B` | 28 | `MASTERC` |
| `#9F1E-#9F83` | 102 | `RESCANN` (extra keys: Caps, Insert, Num, Pause, Scroll, reset combination) |
| `#A1D0-#A1E7` | 24 | `INPCODE` |
