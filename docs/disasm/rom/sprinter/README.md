# Sprinter BIOS 3.04 disassembly

Annotated disassemblies of the Peters Plus **Sprinter Sp2000** BIOS 3.04 (build 253, 17.06.2003),
the default ROM of the planned `SPRINTER` model
([design](../../../inprogress/2026-09-28-sprinter/README.md), PLAN row #59, phase S0). No public
source of 3.04 exists, so these listings are the reference for every BIOS trace in the later phases.

| ROM | Size | CRC32 | Source |
|---|---|---|---|
| [`data/rom/sprinter/sp2k-3.04.rom`](../../../../data/rom/sprinter/sp2k-3.04.rom) | 262 144 bytes (16 pages of 16 KB) | `1729cb5c` (MAME) | `fw/bios/sp2k-3.04.253.bin` of [zxgit.org/Sprinter/2000](https://zxgit.org/Sprinter/2000) |

## Listings

Every listing is byte-complete: its instruction and `defb` lines rebuild the binary exactly
(`scripts/checkcov.py`).

| Listing | What | CPU address | Notes |
|---|---|---|---|
| [exp/bios304-p8-exp.asm](exp/bios304-p8-exp.asm) | ROM page 8: the BIOS proper ("EXP") | `#0000-#3FFF` | [exp/README.md](exp/README.md) |
| [rom/bios304-p0-drivers.asm](rom/bios304-p0-drivers.asm) | ROM page 0: disk drivers, SETUP stub, packed SETUP | `#0000-#3FFF` | [rom/README.md](rom/README.md) |
| [rom/bios304-p0-setupstub.asm](rom/bios304-p0-setupstub.asm) | page 0 `#1000-#115E` where it runs (the stub and the Hrust depacker) | `#8000-#815E` | |
| [rom/bios304-setup.asm](rom/bios304-setup.asm) | SETUP, unpacked (boot screen, setup menu, **the boot**) | `#8000-#B644` | [rom/README.md](rom/README.md) |
| [loader/bios304-pc-loader.asm](loader/bios304-pc-loader.asm) | ROM page `#C`, first 256 bytes: the PLD configuration loader | `#0000-#00FF` | [loader/README.md](loader/README.md) |

Symbol files for the emulator's label loader are in
[`data/symbols/sprinter/`](../../../../data/symbols/sprinter/) (one per listing, see
[Symbols](#symbols)).

## ROM map

ROM pages are numbered by file offset / `#4000`.

| ROM page | File offset | Contents |
|---|---|---|
| 0 | `#00000` | disk drivers (floppy, IDE, ATAPI CD-ROM, RAM disk) at `#0000-#0EBB`; the SETUP stub at `#1000`; SETUP packed with Hrust 1.x at `#115F-#3209`; page stubs at `#3FD0-#3FFF`; the ROM checksum in bytes 4-7 |
| 1-7 | `#04000` | empty (`#FF`) |
| 8 | `#20000` | the BIOS proper: cold start and self test, the port table (packed at `#1400`), BIOS functions `#40-#48` and `#80-#FF` (table at `#3000`), the 8x8 font (`#2800`), page stubs at `#3FD0-#3FFF` |
| 9-`#B` | `#24000` | empty (`#FF`) |
| `#C` | `#30000` | the PLD loader (`#0000-#009B`, strings to `#00BD`), then the ACEX bitstream from `#0100` |
| `#D`-`#F` | `#34000` | the rest of the bitstream (59 215 bytes in all, ends at `#3E84E`); a build stamp `17.06.0331 _SPRIN.BIN 11:31:43,13 17.06.2003` at `#3FFD0` |

Unlike the newer community BIOS builds, 3.04 carries **no Spectrum ROM images**: Spectrum mode needs
them loaded from disk ("Spectrum ROM not installed. Use spectrum.exe", page 8 `#04C1`).

## How the machine starts (step by step)

1. **Power-on.** The ACEX PLD is empty. The small CPLD shows ROM page `#C` (and `#D-#F` above it) to
   the CPU, which starts at `#0000`: the **PLD loader**. It writes each bitstream byte 8 times, bit 0
   first, into `#FE00-#FEFF`; the CPLD turns each write into one configuration clock. 59 215 bytes ×
   8 = **473 720 writes** ([loader/README.md](loader/README.md)).
2. **The PLD is configured** and resets the CPU. Now ROM page 8 is in window 0: `Reset` (`#0000`)
   jumps to `ColdStart` (`#0100`).
3. **Cold start** (page 8): a "restart through RAM" signature at `#FFE0` is checked; the Z84C15
   devices are set up; the **self test** (POST) runs with a progress code on the PIO for each step:
   RAM data bus, RAM address bus, **port table** (`DcpInit`, `#0CA1`, unpacks the table into RAM
   page `#40`, then `IN A,(#E2)` opens the port decoder), RAM pages, data bus.
4. `PortsInit` (`#036E`): ISA reset, SIO A/B (keyboard, mouse), CTC, PIO, Covox-Blaster muted, port
   map 3, `OUT (#BC),#21` (primary IDE channel), `#7FFD`/`#1FFD`.
5. `RunSetup` (`#035D`): ROM page 0 `#1000-#3FFF` is copied to RAM `#8000`; the SETUP stub unpacks
   SETUP (Hrust 1.x) to `#8000` and starts it.
6. **SETUP** (`rom/bios304-setup.asm`): keyboard on, CMOS settings (defaults if the checksum is bad),
   boot screen (logo, version, memory, clock), floppy and IDE detection. **DEL** enters the setup
   menu, **ESC** goes to Spectrum mode.
7. **Boot device** from CMOS register `#10` (low nibble; the high nibble is the alternative):
   0 = floppy A, 1 = floppy B, 2 = IDE master, 3 = IDE slave, 4 = RAM disk. A floppy is reset first
   (function `#51`), which runs the **density probe** (READ ADDRESS at the current density, flip
   `#BD` on time-out, 4 tries).
8. **Boot sector**: function `#55` reads **LBA 1** (`HL:IX = 0:1`) to `#7E00`. It must start with
   `Starting...` + `#00`; it is copied to `#8000` and run at `#800C` with A = device. Example: the
   DSS 1.62 floppy ([testdata/machines/sprinter](../../../../testdata/machines/sprinter/README.md))
   has exactly that at cylinder 0, side 0, sector 2.

## Calling the BIOS

`RST #18` (from ROM code) or `RST #08` (from RAM programs such as DSS), function number in C:

| Numbers | Served by | How |
|---|---|---|
| `#80-#FF` | page 8 | word table at `#3000`: entry `#3000 + 2 × (C − #80)`; unused numbers point to `FnNotImplemented` (`#315D`, Carry = 1) |
| `#40-#48` | page 8 | `HddFnDispatch` (`#0571`): the old HDD API |
| `#50-#5F` | page 0 | the disk API (A = device, HL:IX = sector): page 8 switches to page 0 through the page stubs |
| `#00-#3F` | — | Carry = 1 |

**Page stubs.** `OUT (#7C),A` selects ROM page 0 (bit 0 = 1) or 8 (bit 0 = 0) for window 0 *under the
running code*; the next instruction comes from the other page at the next address. Both pages have
matching code at `#3FD0-#3FFF`. Worked example, a call to function `#55`: page 8 `#0571` → `JP #3FE8`
→ `PUSH AF / LD A,1 / OUT (#7C),A` → the CPU fetches page 0 `#3FED` = `JP #0100` → page 0 dispatches
`#55` → page 0 `#3FE8`: `LD A,0 / OUT (#7C),A` → page 8 `#3FED`: `POP AF / RET`.

## The port table

The BIOS writes the 16 KB port table (hardware-reference §4) from a packed copy at page 8 `#1400`.
[`tools/sprinter/dcp-table.py`](../../../../tools/sprinter/dcp-table.py) unpacks it the same way and
prints it; the comparison with the design's table is in
[hardware-reference.md §4.4](../../../inprogress/2026-09-28-sprinter/hardware-reference.md).

## Symbols

`data/symbols/sprinter/*.map` use the emulator's MAP label format (`LabelManager::ParseMapFile`):

```text
ROM8:0CA1  DcpInit                      (CODE)
ROM0:0669  FddProbeDensity              (CODE)
8451       LoadBootSector               (CODE)
```

`ROMn:` is the physical ROM page (8, 0, 12); SETUP and the stub run from RAM and have no page. A
carried name keeps its source reference as the comment. Checked 2026-10-01 with a throwaway test:
the page 8, page 0, SETUP and loader files load with `LabelManager::LoadLabels` (every line becomes a
label, the ROM page is kept); the stub file has the same format.

## Where the names come from

1. **Hand names** (`scripts/dict_*.py`): entry points, dispatchers, the routines the emulator work
   needs (port table, density, boot, CMOS, keyboard), each with a comment block.
2. **`FnNN_NAME`**: the handler of BIOS function `NN`, read from the dispatch table or chain; `NAME`
   from Tolik-Trek `Shared_Includes/constants/BIOS_equ.inc`, else the carried name.
3. **Carried names**, matched by byte pattern (`scripts/transfer.py`), shown as
   `; = NAME (src: FILE:LINE, SOURCE)`:
   - page 8 ← BIOS-TT **0271ac3** (2023-06-12, `src/bios/exp/*.asm`): 70 % of the code bytes match;
   - page 0 drivers ← BIOS-PP **1273243** (`SETUP/EXTENDED.ASM`, drivers of BIOS 2.17 / Setup
     2.41) plus BIOS-TT: 97 % match;
   - SETUP ← BIOS-PP 1273243 `SETUP/DSETUP.ASM`: 80 % match.

   A `~` marks a name placed by a short unique byte window instead of a long run (less certain).
   The source comments are Russian and are not copied; the `src:` reference points to them.
   Regions of 3.04 code with no counterpart in the source are listed in each page README
   ("differs from the source").

Limits: code reached only through computed jumps or RAM copies may remain as data (`defb`); names
carried into repeated, look-alike code can be one copy off (the four device switches of page 0 were;
they are suppressed in `dict_rom.py`). The PP sources are older than 3.04 (Setup 2.41 vs 2.53), the
BIOS-TT ones newer.

## How these were made

The pipeline is in [scripts/](scripts/README.md) (Python 3, `z80dasm` 1.2.0): `extract.py` slices the
ROM and unpacks SETUP, `analyze.py` finds the code, `gen.py` writes the listings and the symbol files,
`checkcov.py` verifies them. `build-refs.sh` rebuilds the reference sources with sjasmplus and
re-runs the name transfer (it needs the source repositories, see [scripts/README.md](scripts/README.md)).
