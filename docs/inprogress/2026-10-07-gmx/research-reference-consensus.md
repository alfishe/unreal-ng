# GMX: reference consensus

**Date:** 2026-10-07 - part of [README.md](README.md) - used by [design.md](design.md) section 2

Every row below comes from a source that was read for this folder. A claim is taken as fact (H) when at least two
independent sources agree, or when it is a measured fact of one source with no contrary one (M). Where they differ the
table says which value the design takes and why.

## 1. Sources read

| Tag | Source | What it gave |
|:--|:--|:--|
| ART1 | [A. Apollonov, "Nemnogo o GMX", City #18 (1999)](https://zxpress.ru/article.php?id=10414&lng=eng), the same text in [ZX-Pilot #31](https://zxpress.ru/article.php?id=9138) | scheme list, boot menu, port list, board modifications, flash update, shortcomings |
| ART2 | [Trident, "Scorpion GMX ili neispolzovannyj potencial"](https://zxpress.ru/article.php?id=10696&lng=eng) | 7 MHz turbo, 2 MB, second window `#78FD`, flash 512K, 640x200x16 screen in banks `#39 / #79` (buffer `#3A / #7A`), firmware bugs of early flash versions |
| BC | [Black_Cat port table, 2008](https://wiki.speccy.org/_media/cursos/ensamblador/zx-ports-full-table.pdf) | address decode patterns of `#1FFD #78FD #7AFD #7CFD #7EFD #7FFD #DFFD` for computer "B - Scorpion GMX" |
| MAME | [`src/mame/sinclair/scorpion.cpp`](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/scorpion.cpp), class `scorpiongmx_state` | ports, read-backs, memory formula, planes, 640x200 renderer, ROM list with CRCs |
| UNR | [TS-Labs Unreal Speccy (zx-evo fork), `Unreal/io.cpp`, `drawers.cpp`, `config.cpp`, `vars.cpp`](https://github.com/tslabs/zx-evo/tree/master/pentevo/unreal/Unreal) | port writes and read-backs, lock rule, 640x200 drawer, ROM size check. **Its `memory.cpp` has no GMX paging**: `p78FD`, `pDFFD` and the plane are stored but never mapped, so Unreal gives no memory consensus |
| UNR0 | The original Unreal Speccy `memory.cpp` (a commented-out `profrom_bank = (p7EFD >> 4) & 3` block marked "gmx") | only shows that the idea of `#7EFD` bits 4-5 as plane was there |
| PICO | [pico-speccy](https://github.com/DnCraptor/pico-speccy) `src/speccy/core/Ports.cpp`, `machines/Scorpion.cpp` | a MAME-derived GMX with **real-firmware traces** (the ProfROM tap, `1FFD.2` edge, turbo authority), 640x200 renderer |
| WIKI | Wikipedia / SpeccyWiki GMX page | not read: SpeccyWiki returns 403; search snippets only |
| ZXMAK2, Xpeccy, Xpeccy-plus, UnrealSpeccyP, ZX-M8XXX, zxpoly | source trees searched for `gmx` (case-insensitive) | **no GMX machine in any of them** (hits are resource noise); the xpeccy reports in the downloads folder mention no GMX either |
| RTL | no schematic or FPGA scheme of the card was found | all gate-level statements are open |

Not read: the card's manual ("Description of the GMX", Scorpion Ltd, mentioned by ART1), the Zonov shadow monitor GMX
chapter, any flash dump other than `gmx.rom`.

## 2. Port decode

| Item | BC | UNR | MAME | PICO | ART | Take |
|:--|:--|:--|:--|:--|:--|:--|
| `#7FFD` decode | `0x1xx111xx1xxx01` (A14 free, A13 = 1, A12-11 free) | `(port & #C023) == #4021` | `#4021` mask `#3FDC` | as MAME | - | the Scorpion decode already in the repo; BC's looser A14 is noted (Q15) |
| `#1FFD` | `0x0xx111xx1xxx01` (A15 = 0, A13 = 0, A10-8 = 111) | `p2 == 1F` and `(port & #23) == #21` | `#0021` mask `#3FDC` | as MAME | named | A5 = 1, A1 = 0, A0 = 1, high byte per Scorpion |
| `#78FD #7AFD #7CFD #7EFD #DFFD` | `0x1xx` + A10-8 = 000 / 010 / 100 / 110 and `1x0xx111xx1xxx01` for `#DFFD` | high byte exact (`p2` switch), low `(port & #23) == #21` | exact 16-bit address | exact 16-bit | listed | **exact high byte, low byte `A5 = 1 A1 = 0 A0 = 1`** (Unreal's mask is the BC pattern; MAME and pico-speccy are stricter; the stricter exact address is a subset of the loose one; Q15) |
| `#xx00` | `xx00` "global settings, always available" | `(port & #FF) == 0` | `0x0000` mirror `#FF00` | low byte 0 | ART1 | low byte 0, any high byte, before the BLKEXT test (H) |
| Register file block | - | not implemented | `#00` bit 5 disables the view | same | - | `#00` bit 5 (M, MAME and pico agree) |

## 3. Register behavior

| Register | UNR | MAME | PICO | Take |
|:--|:--|:--|:--|:--|
| `#78FD` write | stored (no bits cut) | `data & #7F`, window 2 page `(v & #7F) ^ 2 % pages` | same as MAME, out of range -> page 2 | MAME/PICO (H as to value, UNR has no mapping) |
| `#78FD` read | `(p78FD & #7F) \| ((pFE & 2) << 6) \| (magic & 1)`, then `magic >>= 1` | same | same | H |
| `#7AFD` write | `p7AFD = val` (8 bits) | `data & #F0` | `data & #F0` | **differs**. MAME/PICO agree with each other but PICO copied MAME; Unreal's full byte agrees with the articles ("8 bits" low byte) and with a line scroller where only the low nibble would be a quarter-line. Open (Q4); recommendation: all 8 bits are stored, and the renderer uses the semantic of the chosen scroll policy |
| `#7AFD` read | `(7FFD & 7) \| ((1FFD & #10) >> 1) \| ((DFFD & 7) << 4) \| ((pFE & 1) << 7)` | the same, or the value frozen at Magic when `magic_lock` | live | H (live form) |
| `#7CFD` write | `p7CFD = val` | `data & #3F` | `data & #3F` | `& #3F` (H as article: 6 bits) |
| `#7EFD` write bit 7 | turbo(2/1) | `m_turbo`, clock scale | turbo (authoritative, as TS-Conf ZCLK) | H |
| bits 6-4 | `val & #8F \| p7EFD & #70` when `#00` bit 4 (keeps them), else full | plane `= BIT(data, 4, 3)` unless fixrom | plane `= (data >> 4) & 7` unless fixrom | H |
| bit 3 | selects `M_GMX` in `draw.cpp` | `m_gfx_ext` | `gmxExtRequest` applied at the frame end | H |
| bit 2 | **disables the `#7FFD` lock** (`io.cpp`) | **"magic disabled"**: an NMI request is ignored | ignored ("D2 = magic_disabled") | **differs**: see Q3. MAME names it from the hardware note it was written from; Unreal's use is the one that exists only in Unreal. Recommendation: implement MAME's meaning (the NMI gate); do not lift the lock |
| bits 1, 0 | - | Vpp, EWR of the 28F400 | ignored | ignore (Q9) |
| `#7EFD` read | bit0 = 7FFD.5; bit1 = 7FFD.3; bit2 = `7EFD.7`; bit3 = `7EFD.3`; bit4 = `p00.7`; bit5 = `p00.5`; bit6 = 1FFD.0; bit7 = pFE.2 | the same (plus frozen copy when `magic_lock`) | the same | H |
| `#DFFD` write | `pDFFD = val` | `data & 7` | `data & 7` | `& 7` |
| `#00` write | stores `p00`; bit 3 -> magic `#88 \| (v & 7)`; reset CPU if bit 4 = 0 | same | same | H |
| Magic lock of `#7AFD`/`#7EFD` | not implemented | frozen at NMI (`& #7F`, `& #4F`), released by `IN #FF` | not modelled | MAME only (Q7); implement after phase 1 if a test needs it |

## 4. Memory

| Item | UNR | MAME | PICO | ART | Take |
|:--|:--|:--|:--|:--|:--|
| Window 3 page | not mapped | `((DFFD & 7) << 4) \| ((1FFD & #10) >> 1) \| (7FFD & 7)` mod pages | same, `1FFD` bits 6-7 only for the ZS-1024 romset | "expansion split into `#1FFD` and `#DFFD`" | H |
| Window 2 | not mapped | `(78FD ^ 2) % pages` | the same | "second window `#8000-#C000`, `#78FD`" | H |
| `#1FFD` bit 2 | `CF_TRDOS` flag (MM_SCORP generic branch; memory.cpp not present for GMX) | DOS page of the plane at `#0000` and Beta on, over bit 0 | same, plus clears the DOS latch on the falling edge when PC >= `#4000` | - | MAME/PICO (H) |
| ROM plane | `profrom_bank = (p7EFD >> 4) & 3` (commented-out original) | `prof_plane` = `7EFD` bits 4-6 | same | "flash 512K, 7 variants" | `7EFD` bits 4-6 (H) |
| ProfROM read strobe at `#0100-#010F` | - | inherited from the Turbo+ class, changes the two low plane bits | **disabled on GMX** (hardware trace: the service monitor checksums its own 16 KB through `#0100-#010F` and the strobe threw the CPU into plane 0) | - | disabled (M, a real-firmware trace outranks inheritance; Q2) |
| Page count | `RAM_2048` | 2 MB default | up to 128 pages | 256K..2 MB SIMMs | 2048 KB |

## 5. Video

| Item | UNR (`draw_gmx`) | MAME | PICO | ART2 | Take |
|:--|:--|:--|:--|:--|:--|
| Pixel page | `#7FFD & 8 ? #3B : #39` | `#7FFD.3 ? #3B : #39` | same pages | `#39`, buffer `#3A` | `#39 / #3B` (H, three code sources; ART2 differs, Q11) |
| Attribute page | pixel page `+ #40` | `+ #40` (`0x40 << 14`) | `+ 64` | `#79`, buffer `#7A` | `+ #40` |
| Line size | 80 | 80 | 80 | 80 characters | 80 (H) |
| Rows | wraps at 16000 bytes (200 rows) | `% (25 * 8)` | `% 200` | 25 x 80 text mode | 200 |
| Scroll | byte offset `((7CFD << 8) + 7AFD) & #3FFF`, added to the byte pointer, wrap at 16000 | `((hi << 8) \| lo) / 80` lines | same as MAME | "line scroller" | **differs** (Q4): the line scroller is the documented one; the article's name for it says "line" |
| Colors | bright from bit 6 for both, flash bit 7 | same | fixed ZX 16 | "same bits as the standard screen" | same (H) |
| Pixel clock | `hires_draw`: two pixels per column pair | `(SPEC_CYCLES_PER_LINE * 2) << gfx_ext` | packed pairs | "the whole TV width" | 640 pixels per line, double dot clock |

## 6. Timing and turbo

| Item | Sources | Take |
|:--|:--|:--|
| Frame / INT | The Scorpion's (Unreal preset `PRESET.SCORPION=69888,14344,224,50,32`; the repo's `profscorp` ini uses `intstart 1815`); ART1: INT can be switched to the Pentagon's from the shadow monitor | Scorpion defaults; the Pentagon INT option as a config key (Q13) |
| Turbo | ART1 / ART2: 7 MHz, software or a board button; UNR / MAME / PICO: `#7EFD` bit 7 | as Scorpion Turbo+ overlay, switch = `#7EFD` bit 7 (H) |
| `IN #7FFD / #1FFD` strobes | MAME inherits from `scorpiontb_state`; UNR does not for GMX; PICO has its own `turboPlusRead` for the Scorpion arch | Q5 |
| "INT stretched to 132000 clocks in turbo" | ART2 only | not understood: 132000 is not a frame length at 7 MHz (139776); treated as an author's slip, Q13 |

## 7. Not in any source

The FPGA schemes (what exactly differs in the Pentagon and Composit scheme beyond "full analogue"), the exact pixel
timing of the 640x200 mode, the border width in the extended mode, what the A15 side of the BC `#7FFD` pattern
means on this card, flash write timing, what the Magic shift readout is for. See [TODO.md](TODO.md).
