// MCP resources implementation — see mcp-resources.h
//
// Static reference documents are embedded as markdown raw string literals.
// The dynamic emulator-state resource formats GET /api/v1/emulator as text.

#include "mcp-resources.h"

#include <sstream>

namespace mcp
{

/// region <Embedded content>

namespace
{

constexpr const char* kKeyboardLayout = R"md(# ZX Spectrum Keyboard (Unreal-NG key names)

Use these exact key names with the type_input tool ("key"/"keys" arguments).

## Letters and digits
`a`..`z` (lowercase), `0`..`9`

## Modifiers
| Key name | Aliases | Notes |
|:--|:--|:--|
| `caps` | `shift`, `capsshift`, `caps_shift`, `cs` | CAPS SHIFT — BASIC keyword mode |
| `symbol` | `sym`, `symshift`, `sym_shift`, `ss` | SYMBOL SHIFT — symbol mode |

## Special keys
| Key name | Aliases |
|:--|:--|
| `enter` | `return` |
| `space` | ` ` |

## Extended keys (emulated on host keyboard)
| Key name | Aliases |
|:--|:--|
| `up` / `down` / `left` / `right` | cursor keys (real hardware: cs+5..8) |
| `delete` | `backspace`, `del` (real hardware: cs+0) |
| `break` | cs+space |
| `edit` | cs+1 |
| `dot` | `.` |
| `comma` | `,` |
| `plus` | `+` |
| `minus` | `-` |
| `multiply` | `*` |
| `divide` | `/` |
| `equal` | `=`, `equals` |
| `dblquote` | `"`, `quote` |

## BASIC keyword entry (original 40-keyboard matrix)
Each key produces a BASIC keyword in K-mode: G (THEN), H (GOTO), K (LIST), L (LET), M (RUN), N (NEXT), O (POKE), P (PRINT),
Q (PLOT), R (INPUT), S (SAVE), T (LOAD), U (RANDOMIZE), V (RETURN), W (BORDER), X (NEXT), Y (RETURN), Z (COPY).
Symbol-shift combinations produce the punctuation/symbols above.

## Named macros (type_input action:'macro')
`e_mode` (ENTER E-mode), `g_mode` (G-mode), `format`, `cat`, `erase`, `move`, `break`.

## Tips
- `type` with `tokenized: true` types BASIC commands using tokenized keyword entry (fast and cursor-accurate).
- `combo` presses several keys simultaneously, e.g. keys:["cs","ss"] for true video mode.
)md";

constexpr const char* kBasicReference = R"md(# ZX Spectrum BASIC Quick Reference (48K/128K ROM)

## Program control
| Command | Example | Notes |
|:--|:--|:--|
| RUN | `RUN 5000` | Start program (optionally at line) |
| LIST | `LIST 100-200` | List (line range) |
| NEW | `NEW` | Erase program |
| CLEAR | `CLEAR 30000` | Reset variables, set RAMTOP |
| STOP / CONTINUE | | Break / resume |
| GO TO / GO SUB / RETURN | `GO TO 100`, `GO SUB 5000` | Jump / call / return |
| IF / THEN | `IF x>3 THEN GO TO 100` | No ELSE — use two IFs |
| FOR / NEXT / STEP | `FOR i=1 TO 10 STEP 2: NEXT i` | Loops |
| PAUSE | `PAUSE 50` | Wait frames (50 = 1s); 0 = until key |
| RANDOMIZE | `RANDOMIZE 1` | Seed (1 → fixed for POKE 23672 tricks) |
| REM | | Comment |

## Data and variables
LET (assignment), DIM (arrays), INPUT, PRINT (AT y,x / TAB n; ',' separates print zones), ATTR/POINT/SCREEN$,
PEEK/POKE (memory), IN/OUT (ports), USR (machine code call: `RANDOMIZE USR 32768`), STR$/VAL/CODE/CHR$ (strings),
INT/ABS/SGN/SQR/SIN/COS/TAN/ASN/ACS/ATN/LN/EXP/PI/RND (math), BIN/HEX-not-native (use decimal).

## System interface
| Command | Purpose |
|:--|:--|
| LOAD "" / SAVE "name" | Tape (128K: also LOAD *"m";1;"name" for +3 DOS) |
| BORDER n / PAPER n / INK n / BRIGHT / FLASH / OVER / INVERSE | Colors: 0 black, 1 blue, 2 red, 3 magenta, 4 green, 5 cyan, 6 yellow, 7 white |
| PLOT / DRAW / CIRCLE | Graphics (256x192, origin bottom-left) |
| BEEP duration,pitch | `BEEP 1,0` = 1 second middle C; pitch in semitones |
| CLS / SCROLL | Clear / scroll |
| VERIFY / MERGE | Tape verify / merge |
| CAT / ERASE / FORMAT / MOVE (TR-DOS) | Disk via macros: `RANDOMIZE USR 15616` enters TR-DOS |

## Key system variables (POKE/PEEK targets)
| Address | Meaning |
|:--|:--|
| 23606/23607 | CHARS — font address (minus 256) |
| 23617 | FLAGS — keyboard shift state |
| 23658 | ATTR_P persistent color |
| 23672-23674 | FRAMES — 3-byte frame counter (~50 Hz) |
| 23732 | RAMTOP |
| 23552-23560 | KSTATE keyboard buffer |

## Entry points
- `RANDOMIZE USR 0` → reset. `RANDOMIZE USR 15616` → TR-DOS 128K.
- Interrupt mode 1 vector: address 0x38; IM 2 vector table at I*256.
)md";

constexpr const char* kZ80Isa = R"md(# Z80 Instruction Set Quick Reference

Registers: A F B C D E H L (AF' BC' DE' HL' shadow), IX IY SP PC I R. Flags: S Z Y H X PV N C.

## Loading
`LD r,r'` `LD r,n` `LD r,(HL)` `LD (HL),r/n` `LD r,(IX+d)/(IY+d)` `LD A,(BC)/(DE)/(nn)` `LD (nn),A/HL/BC/DE/IX/IY`
`LD A,I` `LD I,A` `LD A,R` `LD R,A` `LD SP,HL/IX/IY` `PUSH/POP AF/BC/DE/HL/IX/IY` `EX DE,HL` `EX AF,AF'` `EXX` `EX (SP),HL/IX/IY`
`LDI/LDD/LDIR/LDDR` (block transfer BC bytes HL→DE) `CPI/CPD/CPIR/CPDR` (block compare) `INI/IND/INIR/INDR` `OUTI/OUTD/OTIR/OTDR`

## Arithmetic / logic
`ADD/ADC/SUB/SBC/AND/XOR/OR/CP` A,src (src: r n (HL) (IX+d)); 16-bit: `ADD HL,rr` `ADC/SBC HL,rr` `ADD IX/IY,pp`
`INC/DEC r/(HL)/rr` `DAA` (BCD adjust) `NEG` `CPL` `CCF` `SCF`

## Rotates / shifts
`RLCA RLCA` variants: `RLCA RRCA RLA RRA` (A only), `RLC/RRC/RL/RR/SLA/SRA/SLL/SRL m` (m: r (HL) (IX+d)), `RLD/RRD` (BCD via HL)

## Control flow
`JP nn` `JP cc,nn` `JR e` `JR cc,e` `DJNZ e` `CALL nn` `CALL cc,nn` `RET` `RET cc` `RETI` `RETN` `RST p` (p: 00 08 10 18 20 28 30 38)
Conditions: NZ Z NC C PO PE P M

## I/O, CPU
`IN A,(n)` `IN r,(C)` `OUT (n),A` `OUT (C),r` `DI/EI` `IM 0/1/2` `HALT` `NOP` `SET/RES b,m` `BIT b,m` (b: 0-7)

## Common timings (T-states)
4 NOP · 13 CALL · 10 RET · 7 JR taken · 12 JR not taken · 21 LDIR iteration · 4+16 IM1 interrupt.
Timing-critical effects: every instruction matters; use `inspect_state aspects:["timing"]` and the profiler tools.

## Pointers into this emulator
- Disassembly: inspect_state aspects:["disasm"] or debug_code action:"disassemble".
- Assembling patches: debug_code action:"assemble".
)md";

constexpr const char* kTrdosCommands = R"md(# TR-DOS 5.03 / Beta Disk Interface Reference

Enter TR-DOS from 128K BASIC: `RANDOMIZE USR 15616` (or the `cat` macro via type_input). Drives A/B, up to 80 tracks,
16 sectors/track, 256 bytes/sector (TRD image = 655360 bytes for 2 sides).

## Commands
| Command | Purpose |
|:--|:--|
| CAT (or `.`) | Catalog listing |
| FORMAT "name" | Initialize disk |
| SAVE *"name" CODE start,len | Save memory block |
| SAVE *"name" LINE n | Save BASIC with autostart |
| LOAD *"name" | Load BASIC program |
| LOAD *"name" CODE [addr] | Load code block (optionally relocated) |
| LOAD *"name" DATA v$() | Load array |
| MERGE *"name" | Merge BASIC |
| VERIFY *"name" | Verify file |
| ERASE "name" | Delete file |
| MOVE "old" TO "new" | Rename file |
| COPY | File/disk copy utility |
| RUN *"name" | Load and run |
| CAT 1 / CAT 2 | Extended catalog (per drive) |

## TRD file system facts
- Directory: 128 entries at track 0, sectors 1-8 (2048 bytes).
- File types: BASIC (48), NUMBER ARRAY (49), CHAR ARRAY (50), CODE (51+; start/length in directory).
- Disk parameters block at 0x1E1E (894): free sectors, first free track/sector, file count, free directory slots.
- System tracks: 0-1 reserved (SERVICE sector at track 0 sector 8).

## Via WebAPI instead of typing
- Disk catalog: invoke_api GET /api/v1/emulator/{id}/disk/A/catalog
- Disk info/sysinfo: GET .../disk/A/info, .../disk/A/sysinfo
- Raw sectors: GET .../disk/A/sector/{cyl}/{side}/{sec}
)md";

constexpr const char* kMemoryMap = R"md(# ZX Spectrum Memory Map

## 48K address space
| Range | Size | Content |
|:--|:--|:--|
| 0x0000-0x3FFF | 16K | ROM (fixed; 128K bankable later models) |
| 0x4000-0x5AFF | 6912B | Screen bitmap: 0x4000-0x57FF bitmap (6144B), 0x5800-0x5AFF attributes (768B) |
| 0x5B00-0x5BFF | 256B | System variables (SYSVARS area from 0x5C00 down: CHARS 0x5C36...) |
| 0x5C00 | — | VARS start (BASIC variables) |
| 0x8000 (typ.) | — | BASIC program area / user code (RAMTOP up to 0xFF57 stock) |
| 0xFF58-0xFFFF | 136B | Printer buffer (0x5B00), stack below RAMTOP, spare |

System area: 0x4000-0x5BFF is the "lower 16K" fixed screen/vars area on all models.

## Screen layout quirk
Bitmap thirds (0x4000/0x4800/0x5800−256... precisely: thirds at 0x4000, 0x4800, 0x5000), each third has 8 character
rows; within a row, scanlines are interleaved in 256-byte steps. Address = base | (y&7)<<8 | (y&0x38)<<2 | (y&0xC0)<<5 | x>>3.
Attributes: 0x5800 + (y<<5) + (x>>3); byte = FLASH BRIGHT PAPER(0-2) INK(0-2) → 0xF8 paper | 0x07 ink.

## 128K / +2 paging (ports 0x7FFD value bits)
| Bit | Meaning |
|:--|:--|
| 0-2 | RAM bank at 0xC000 (banks 0-7; bank 3 at 0xC000 default, banks 5/2 fixed: 0x4000=5, 0x8000=2) |
| 3 | Screen select: 0 → bank 5, 1 → bank 7 (shadow screen) |
| 4 | ROM select: 0 → 48K editor ROM (bank 0), 1 → 128K editor (bank 1) |
| 5 | Paging disabled when set (write until reset) |
| 6-7 | unused |

Write with bits 0x10 set twice pattern: `LD BC,0x7FFD: LD A,value: OUT (C),A` — note bit 3 shadow screen enables
double-buffering (screen_digest hashes banks 5 and 7 for exactly this reason).

## Pentagon specifics
64/128/256/512K: paging via port 0x7FFD (same bits) + 0x EFF7 for >128K extensions; 71680 T-states/frame (320 lines)
vs Sinclair 69888 (311 lines) — 2.27% faster frame; INT every 71680 T; no M1 wait contended differences.

## Emulator notes
- Memory inspection: inspect_state aspects:["memory"] (CPU view; hexdump default, format=full|sparse) or aspects:["memory_banks"] (bank state).
- Sparse block overview: inspect_state aspects:["memory_map"] with view=address|ram (GET /memory/map under the hood).
- Bank pages: invoke_api GET /api/v1/emulator/{id}/memory/page/ram/{n} (add ?filter=sparse to compress 0x00/0xFF runs).
)md";

const char* const kMachineProfi = R"md(# Profi: v5 (model PROFI, alias PROFI5) and v3 (model PROFI3)

Two board families, one decoder (design: docs/inprogress/2026-10-01-profi-v3-v5):
- `emulator_manage action=create model=PROFI`: v5 (Kondor 5.0x), 1024K, config `data/configs/profi`, ROM `data/rom/profi.rom`
- `emulator_manage action=create model=PROFI3`: v3 (Kramis 3.x), 512K, config `data/configs/profi3`, ROM
  `data/rom/profi/kramis-v02.rom` (the factory BIOS V0.2 + TR-DOS 5.03; other factory images in `data/rom/profi/`)

| | v3 (PROFI3) | v5 (PROFI) |
|:--|:--|:--|
| Hi-res 512x240 | monochrome (ink = border colour) | 16 colours from a 256-colour palette |
| Palette `OUT #xx7E` | none | A7=0, A0=0, while DS80 is set |
| `#FE` read bit 7 | always 1 | GX0 from the palette in DS80 |
| Extended port map (CP/M + ROM14) | none: CP/M keeps #1F..#7F + #BF | FDC #83/#A3/#C3/#E3, system #3F, 8255/Covox #A7/#C7, IDE, RTC |
| RTC, IDE | none | RTC #BF/#FF address, #9F/#DF data; Profi IDE (`[HDD] Scheme=PROFI`) |
| Frame (default sync PROM) | 69888 T, INT 12580 T before paper | 69888 T, INT 14368 T before paper |

`[PROFI] SyncProm=` picks the board's sync PROM: `0a1d` (69888 T, 12580 T), `samx6` (69888 T, 12592 T),
`fb0579b6` (71680 T, INT 48 T before paper), `v503` (69888 T, 14368 T); empty = the board's own.

## ROM pages (16K each, both boards)
| Page | Content |
|:--|:--|
| 0 | SYS / menu ROM (BIOS) |
| 1 | TR-DOS |
| 2 | 128K (the 128 editor; the STS monitor in `profi.rom`) |
| 3 | 48K BASIC |

## Paging ports (write decode, both boards)
| Port | Decode | Bits |
|:--|:--|:--|
| #7FFD | A15=0 and A1=0 | 2:0 RAM low bits, 3 screen select, 4 ROM14, 5 lock |
| #DFFD | A15=1, A13=0, A1=0 | 2:0 RAM high bits, 3 SCO, 4 WOROM, 5 CPM, 6 SCR, 7 DS80 (512x240 hi-res) |
| AY #FFFD / #BFFD | A15=1, A13=1, A1=0 | the AY decodes A13, so IN #DFFD does not read it |

The DOS latch is the CF_TRDOS flag. WD1793 ports (#1F/#3F/#5F/#7F, system #FF) answer only while the disk
interface is on the bus (DOS latch or CP/M mode); in CP/M the system port is #BF.

## Video
Standard mode `PROFI`: 256x192 in a 352x288 framebuffer. Hi-res mode `PROFIHR` (#DFFD bit 7): 512x240 in a 608x288
framebuffer. Both use the 312-line x 224 T raster; the frame length and INT position come from the sync PROM.
`inspect_state aspects:["video"]` reports `video_mode` = PROFI or PROFIHR with resolution.

## State
`inspect_state aspects:["paging"]` reports `profi_board` (v3 / v5), `profi_sync_prom` and the tagged latches: p7FFD and pDFFD with
decoded fields `extended_ram_bank`, `sco`, `worom`, `cpm`, `scr`, `video_512x240`. TTD persists the #DFFD latch and
palette as PeripheralId ProfiPaging (9); v5 adds the clock (Ds12887).

## Peripherals
Covox DAC (8255): #5F left, #3F right while the disk interface is off the bus; on v5, #C7 left / #A7 right in the
extended map. Kempston joystick at #1F outside the DOS port set. NMI (magic button) raises the DOS latch while DS80
is off. v5 only: the RTC (MC146818 / DS12887, 256 cells; inspect_state aspect rtc) and the Profi IDE.

## TURBO switch (both boards)
A front-panel switch, not a port: `invoke_api GET /api/v1/emulator/{id}/switches`, `POST .../switches`
`{"name":"turbo","on":true}` (CLI `switch turbo on`, Lua/Python `set_switch("turbo", true)`; `[PROFI] Turbo=1` sets
it at power-on). TTD records a flip like a key. 7 MHz while on; on the v3 a loaded floppy head (WD1793 HLD) holds
3.5 MHz. The status line shows the clock. The v5 also has the CP/M switch (`"name":"cpm"`, CLI `switch cpm on`,
`[PROFI] CpmSwitch`): while it is on, #DFFD is held at #00 and writes to it are lost. `[PROFI] DffdDecode=` picks
the #DFFD decode: `emulators` (A15=1, A13=0, A1=0, default), `v50` (A13=0, A1=0), `v506` (high byte #DF, not from
`OUT (n),A`).

## Wait states and the floating bus (feature `contention`)
| Board, clock | Waits |
|:--|:--|
| v5, 3.5 MHz | RAM accesses: 1 on every other T of the paper fetch window (192 lines x 128 T), none in the border; `[PROFI] WaitPhase=0..3` (power-on phase; 1 = none), `WaitConfig=pentagon` (jumper SB8: none), `RomWait=1` (ROM reads +1) |
| v5, 7 MHz | approximation: RAM 1 in the border, 2 in the paper; ROM reads 1 |
| v3, 3.5 MHz | none |
| v3, 7 MHz | RAM opcode fetch / read / write: 2 clocks from an even 7 MHz clock, 3 from an odd one; ROM none (RAM code runs ~1.33x, ROM 2x) |

v3 floating bus: an unanswered `IN` with A0=1 reads the pixel byte the video latch holds (one 4-T tick ahead of the
displayed byte, page 5 or 7 per #7FFD bit 3), `#FF` in the border; no attribute bytes. The v5 reads `#FF`.

## Known limitations
The 512x240 hi-res mode (DS80) has no waits, no floating bus and no 15 MHz third crystal. The v5 turbo waits are an
approximation. The BIOS menu entries (TR-DOS, Sinclair, 128) are verified on both boards; CP/M boots from a disk.
)md";

const char* const kMachineTsConf = R"md(# TS-Conf (model TSL, alias TSCONF)

ZX-Evo board with the TS-Labs FPGA configuration. Create with `emulator_manage action=create model=TSL`
(4096K only; config `data/configs/ts-conf/unreal.ini`, ROM `rom/zxevo.rom`, TS-BIOS in page 0).
With a blank CMOS the BIOS opens its Setup Utility (text mode); ENTER changes an option.

## Loading software
- `.spg` (TS-Conf SDK program, v1.0 / v1.1, MegaLZ / Hrust blocks): `load_software path=...`. On another model
  the machine is switched to TSL first; the answer's `emulator_id` is then the new instance (use it from now on).
- SD card: media slot `sd.zc` (image or host folder) - Wild Commander / NedoOS live there.
- IDE: Nemo (`[HDD] Scheme=NEMO-DIVIDE`); floppies via Beta-128 as usual.

## Registers: port #nnAF, nn = register number
| nn | Register | nn | Register |
|:--|:--|:--|:--|
| 00 | V_CONFIG (mode [1:0] ZX/16C/256C/TXT, geometry [7:6], NOTSU [5], NOGFX [4]) | 20 | SYS_CONFIG ([1:0] 3.5/7/14 MHz) |
| 01 | V_PAGE | 21 | MEM_CONFIG (W0 RAM / write enable / map, LCK128) |
| 02-05 | G_X_OFFS / G_Y_OFFS | 22-24 | HS_INT, VS_INT (frame INT position) |
| 06 | T_CONFIG (TSU: sprites, tile layers) | 1A-1F, 25-28, 2D | DMA source / dest / len / ctrl / num |
| 07 | PAL_SEL | 29 | FDD_VIRT (virtual drives) |
| 0F | BORDER (CRAM index) | 2A | INT_MASK (frame / line / DMA) |
| 10-13 | PAGE0..PAGE3 (4 MB windows) | 2B | CACHE_CONFIG |
| 15 | FMAPS (CRAM / SFILE / register window) | 40-47 | T0 / T1 X/Y offsets |
| 16-19 | T_MAP_PAGE, T0/T1_G_PAGE, SG_PAGE | | |

`#7FFD` pages as on 128K (LCK128 picks 512K / 128K / auto / 1024K); `#EFF7`, `#xxF7` Gluk CMOS as on ZX-Evo.

## Video
ZX, 16C (4 bpp), 256C (8 bpp) and TXT modes in 256x192 / 320x200 / 320x240 / 360x288; the framebuffer is 720x288
for every mode. TSU: two tile layers + 85 sprites. `inspect_state aspects:["video"]` reports e.g. `TS16C 320x200`
with the active pages; `inspect_state aspects:["tsconf"]` decodes memory, video, TSU, interrupts, DMA and the clock;
`aspects:["tsconf_tsu"]` lists the TSU objects (tile layers, 85 sprites decoded) and the 256 CRAM cells.
Video debug mapping (`invoke_api` /video/pixel, /video/address): layer 0 = the graphics mode, layer 1 = "tsu"
(which sprite / tile drew a pixel, its SFILE / tilemap words, graphics byte and CRAM cell).

## Sound
AY / TurboSound as usual. The board has ONE 8-bit DAC: Covox `#FB` and the `#FE` beeper bit share it (the beeper
writes 0 / 255). Tape-in to the speaker is off (AVR default).

## Interrupts / DMA
Frame INT at VS_INT/HS_INT (32 clocks), line INTs, DMA INT; vectors #FF/#FD/#FB. DMA tasks: RAM copy, BLT1, fill,
CRAM, SFILE, SPI (SD), IDE; the DMA gets what video, TSU and CPU reads leave of each line's DRAM budget.

## Known limitations
Wait states at 14 MHz (cache miss, external I/O) and the VDAC color curves are not emulated yet; TS-specific
debugger views are pending (the model-first debugger).
)md";

const char* const kMachineSprinter = R"md(# Peters Plus Sprinter Sp2000 (model SPRINTER)

A PC-like Spectrum clone: Z84C15 CPU (3.5 or 21 MHz), 4 MB RAM, a PLD (ACEX EP1K30) that the BIOS loads at power-on,
256 KB flash BIOS, its own video modes (text 40 / 80 columns, 320 x 256 x 256 colors, 640 x 256 x 16 colors, chosen
per 8 x 8 square from the mode table in video RAM), WD1793 floppy (720 KB and 1.44 MB), DS12887A CMOS, AT keyboard on
the Z84C15 SIO. Create with `emulator_manage action=create model=SPRINTER` (config `data/configs/sprinter/unreal.ini`).

## BIOS and start
`[ROM] SPRINTER=` picks the image: `rom/sprinter/sp2k-3.04.rom` (default), `sp2k-3.06-hf2.rom`, `sp2k-3.07-beta1.rom`.
`[SPRINTER] FastStart=0` (default) runs the PLD loader (~1.7 s emulated), `1` starts configured. The BIOS waits ~31 s
per empty IDE unit: press F4 (`type_input action=key key=f4`) at "[Press F4". `inspect_state aspects:["sprinter"]`
lists the images and which one runs (`bios`).

## Ports
Every port goes through the port table the BIOS writes to RAM page #40: index = map << 12 | PN5 << 11 | /DOS << 10 |
/WR << 9 | A15 A14 A6 A5 A13 A7 A2 A1 A0, the byte is the device code (#10-#17 WD1793 + density, #1C-#1E CMOS, #20-#2B
IDE, #40 keyboard, #C0 #1FFD, #C1 #7FFD, #C3 ALL_MODE, #C4 PORT_Y, #C5 RGMOD, #C6 CNF, #E8-#EA / #F0 window pages ...).
`inspect_state aspects:["sprinter_ports"]` decodes the current table; `invoke_api GET
/api/v1/emulator/{id}/state/sprinter/ports/lookup?port=21BC&rw=w` resolves one port. The Z84C15 answers #10-#1F,
#EE/#EF, #F0/#F1, #F4 itself. Port trace events carry the code (`code`, `code_name`); Z84C15 ports as #100 + low byte.

## Memory
Four windows, each a physical page with a kind: ROM (system ROM), fast RAM (IN #FB), vROM (Spectrum ROM image in RAM),
RAM, graphics (pages #50-#5F, PORT_Y row), ISA. `inspect_state aspects:["sprinter"]` (windows) or `["paging"]`.

## Software
DSS (Estex DSS, the Sprinter's DOS) boots from a 1.44 MB FAT12 floppy in drive B (SETUP's default alternative start):
`load_software path=testdata/machines/sprinter/dss_1_62_92.img drive=B` before the boot, then F4 at both IDE waits.
Spectrum mode: DSS `SPECTRUM.EXE PENT128.ZX` (A:\ZX), then TR-DOS from drive A.

## Known limitations
No IDE adapter yet (phase S3b); Flex Navigator (DSS shell) crashes; accelerator and Covox-Blaster pending; TTD refuses
to record this machine (phase S7). In the GUI F4 is bound to a speed shortcut.
)md";

struct StaticResource
{
    const char* uri;
    const char* name;
    const char* description;
    const char* mimeType;
    const char* content;
};

const StaticResource kStaticResources[] = {
    {"unreal://keyboard-layout", "keyboard-layout", "ZX Spectrum keyboard: exact key names for type_input, BASIC keyword entry, named macros", "text/markdown", kKeyboardLayout},
    {"unreal://basic-reference", "basic-reference", "ZX BASIC quick reference: commands, colors, system variables, entry points", "text/markdown", kBasicReference},
    {"unreal://z80-isa", "z80-isa", "Z80 instruction set reference: load/arithmetic/rotate/control-flow blocks and T-state notes", "text/markdown", kZ80Isa},
    {"unreal://trdos-commands", "trdos-commands", "TR-DOS 5.03 commands, TRD file system layout, disk inspection via WebAPI", "text/markdown", kTrdosCommands},
    {"unreal://memory-map", "memory-map", "48K/128K/Pentagon memory maps, screen layout math, 0x7FFD paging bits", "text/markdown", kMemoryMap},
    {"unreal://machine/profi", "machine-profi", "Profi v3 (PROFI3) and v5 (PROFI): board differences, ROM pages, #7FFD/#DFFD/palette ports, 512x240 hi-res, sync PROM timing, limitations", "text/markdown", kMachineProfi},
    {"unreal://machine/tsconf", "machine-tsconf", "TS-Conf (ZX-Evo TS-Labs) machine: #nnAF registers, video modes, TSU, DMA, sound DAC, SPG loading, limitations", "text/markdown", kMachineTsConf},
    {"unreal://machine/sprinter", "machine-sprinter", "Peters Plus Sprinter Sp2000: BIOS images and start, the PLD port table and codes, windows, DSS from a floppy, Spectrum mode, limitations", "text/markdown", kMachineSprinter},
};

constexpr size_t kStaticResourceCount = sizeof(kStaticResources) / sizeof(kStaticResources[0]);

} // namespace

/// endregion </Embedded content>

/// region <McpResources>

Json::Value McpResources::ListJson()
{
    Json::Value resources(Json::arrayValue);
    for (size_t i = 0; i < kStaticResourceCount; ++i)
    {
        const StaticResource& resource = kStaticResources[i];
        Json::Value entry;
        entry["uri"] = resource.uri;
        entry["name"] = resource.name;
        entry["description"] = resource.description;
        entry["mimeType"] = resource.mimeType;
        resources.append(entry);
    }

    Json::Value state;
    state["uri"] = "unreal://emulator-state";
    state["name"] = "emulator-state";
    state["description"] = "Live overview of all running emulator instances (dynamic)";
    state["mimeType"] = "text/markdown";
    resources.append(state);

    return resources;
}

void McpResources::Read(const std::string& uri, IApiCaller& caller, ReadCallback done)
{
    for (size_t i = 0; i < kStaticResourceCount; ++i)
    {
        const StaticResource& resource = kStaticResources[i];
        if (uri == resource.uri)
        {
            Json::Value content;
            content["uri"] = uri;
            content["mimeType"] = resource.mimeType;
            content["text"] = resource.content;

            Json::Value result;
            result["contents"].append(std::move(content));
            done(true, std::move(result));
            return;
        }
    }

    if (uri == "unreal://emulator-state")
    {
        caller.Call("GET", "/api/v1/emulator", nullptr, [done](int status, Json::Value body) {
            if (status != 200 || !body.isObject() || !body["emulators"].isArray())
            {
                done(false, Json::Value("Cannot fetch emulator state (HTTP " + std::to_string(status) +
                                            "). Is the emulator process running?"));
                return;
            }

            std::ostringstream out;
            out << "# Emulator instances\n\n";
            const Json::Value& emulators = body["emulators"];
            if (emulators.size() == 0)
            {
                out << "No instances. Create one: emulator_manage action:'create' model:'128k'.\n";
            }
            for (Json::ArrayIndex i = 0; i < emulators.size(); ++i)
            {
                out << "- **" << emulators[i]["id"].asString() << "** — state: " << emulators[i]["state"].asString()
                    << ", running: " << (emulators[i]["is_running"].asBool() ? "yes" : "no")
                    << ", debug: " << (emulators[i]["is_debug"].asBool() ? "yes" : "no") << "\n";
            }
            out << "\nUse 'target' with these ids (or 'auto' when only one instance exists).\n";

            Json::Value content;
            content["uri"] = "unreal://emulator-state";
            content["mimeType"] = "text/markdown";
            content["text"] = out.str();

            Json::Value result;
            result["contents"].append(std::move(content));
            done(true, std::move(result));
        });
        return;
    }

    done(false, Json::Value("Unknown resource: " + uri));
}

/// endregion </McpResources>

} // namespace mcp
