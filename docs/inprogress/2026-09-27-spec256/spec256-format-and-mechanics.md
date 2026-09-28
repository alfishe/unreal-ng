# Spec256 Format and Mechanics — Technical Reference

**Sources:** original Spec256 v1.2 `readme.txt` (Iñigo Ayo Blázquez, 09/1999,
retrieved from the archived `Sp256v12.zip`); EmuZWin `256_color_games.htm` and
`EZXFormat_Eng.htm` (Vladimir Kladov, 2004–2006); zxpoly source (verified
locally); GZX `video/spec256.c`; oozx `doc/wiki/Spec256-256-Colours.md`;
Wikipedia "ZX Spectrum graphic modes". Unverifiable items are marked ⚠.

## 1. Execution model — the Z80_GFX co-processor

While the ordinary Z80 is emulated normally, a fictitious second processor —
the **Z80_GFX**, an "8 planes graphic processor" — runs **in lockstep**:

- For every Z80 instruction the same instruction executes on the Z80_GFX, but
  with **64-bit-wide registers and 64-bit memory words** (Wikipedia: "extending
  the word size of the emulated Z80 from 8 bits to 64").
- It fetches the instruction stream from the Z80's memory (same PC, same
  flags-driven control flow) but all **data** reads/writes go to its own
  memory: **MEMORY_GFX = 65 536 addresses × 8 bytes = 512 KB**.
- "8 bytes of GFX machine correspond to each 8 bits of each byte in normal Z80
  machine" (EmuZWin). Concretely: each of the 8 shadow bytes of address *A* is
  one **colour plane** — shadow byte *p* holds bit *p* of the palette index of
  every pixel covered by the bitmap byte at *A*.

Worked examples:

| Z80 (bitmap machine) | Z80_GFX (colour machine) |
|:--|:--|
| `LD A,n` → A = *n* | A_gfx = 8 bytes, each = *n* (all planes get the same index bits) |
| `LD A,(HL)` → A = bitmap byte at (HL) | A_gfx = 8 plane bytes at GFX[HL] (the colours of those 8 pixels) |
| `LD (HL),A` → writes bitmap byte | writes the 8 plane bytes → **the blit moves the sprite's colours too** |
| `XOR (HL)` / RMW ops | per-plane 8-bit logical op on colour bytes — usually visually meaningless, see §6 |

Timing: per-instruction lockstep, same T-states. The original ran this on a
486DX4-100 minimum ("it's like emulating several Z80 processors
simultaneously" — original readme).

### Register alignment (per-game profiles) ⚠ named after zxpoly

Because flags/pointers can diverge between the bitmap machine and the colour
machine (a colour byte's zero flag is not the bitmap byte's zero flag),
real implementations realign the GFX machine's registers with the main CPU.
zxpoly exposes this as `zxpAlignRegs` (e.g. `1PSsT`): realign **P**C every 1
instruction, **S**P / **s**tack pointer semantics, **T** = take pointer
registers SP/HL/IX/IY/BC/DE from the main CPU. Defaults: PC, SP and F (minus
carry) realigned every iteration; some games need the full
`HLXxYyPSs`-style profile (see `spec256appbase.txt` DB in the survey doc).

## 2. GFX memory layout

- Total: 512 KB (64K addresses × 8 planes). Only the RAM/ROM pages visible
  through the standard 48K/128K window scheme have meaning; the `.gfx` file
  persists just the 48K-visible part.
- Window mapping (identical to standard machines — from zxpoly
  `readGfxMemory`):
  - `$0000–$3FFF`: ROM plane, selected by `#7FFD` bit 4 (128K) → GFX ROM A/B
  - `$4000–$7FFF`: RAM page 5 → `gfxRam[5 * 0x20000 + (addr−$4000)*8 + plane]`
  - `$8000–$BFFF`: RAM page 2
  - `$C000–$FFFF`: RAM page `#7FFD & 7` (128K) or page 0 (48K)
- One GFX "page" = 16 384 addresses × 8 = **0x20000 bytes** (131 072).
- 128K machines: 8 RAM pages × 0x20000 = 1 MB GFX RAM + 2 ROM pages × 0x20000
  = 256 KB GFX ROM (zxpoly `gfxRam`/`gfxRom` sizes).

### Screen fetch

- Displayed screen: standard ZX thirds address formula over RAM page 5 or 7
  (`#7FFD` bit 3), 256×192, **attribute file unused** (Wikipedia: "screen thus
  takes 48 KB of memory; attributes: none" — 6144 bytes × 8 planes = 49 152).
- Per bitmap byte, pixel *p* (0 = leftmost) uses mask `1 << (7 − p)`; its
  colour index = Σ over planes: bit *plane* of the plane's byte, shifted left
  by *plane* (zxpoly `readGfxVideo`).
- The attribute bytes at $5800–$5AFF also have 8 shadow bytes each (written by
  the shadow machine like any other address); renderers may use the *ordinary*
  attribute byte for ink-mixing effects (§5), never for basic colour.

### On-disk `.gfx` byte order — and the one open contradiction

zxpoly's `decodeGfx()`/`packGfxData()` (round-tripping real game archives)
establish: for the bitmap byte at offset *o* in a page, file bytes
`8·o … 8·o+7` are the **palette indices of pixels 0…7 left-to-right**
(pixel 0 = MSB of the bitmap byte). An independent web correlation test
against Cybernoid/Knight Lore measured only 94–96 % agreement for MSB-first
vs 85 % LSB-first — consistent with MSB-first being right, with the residual
caused by re-coloured borders/title art. **Action for implementation: verify
against the local zxpoly fixture archives (`Jetpac_spec256.zip`,
`Renegade_spec256.zip`, `TreeWeeks128k_spec256.zip`) before freezing the
codec.**

## 3. Palette

- 256 entries × 3 bytes (R,G,B, 8-bit channels) = **768 bytes** (`.pal` /
  `.pnn` files; zxpoly `Spec256Palette.size() = data.length / 3`). ⚠ Byte
  order within a triplet (RGB vs BGR) differs between tools — zxpoly's reader
  is `R<<16 | G<<8 | B`; verify against EmuZWin's loader before committing.
- Default palette structure (verified identical in EmuZWin
  `Bmp2RawBk256.dpr` `SystemPalette` and GZX `sp256.pal`):
  - indices 0–191: **24 smooth ramps × 8 entries** (e.g. blues
    `000000, 00009B, 172FAB, …, E7FFFF`; reds `B70000 … FFEFEF`; greens;
    oranges `FF4B00 … FFFFEB`; …)
  - 192–199: black block
  - 200–247: 6 hard ramps
  - 248–255: grey ramp `3F3F3F … FFFFFF`
  - Semantics: **index 0 = black, 0xFF = white** ("Clear GFX Memory" maps
    bitmap 0-bit → 0x00, 1-bit → 0xFF).
- **Runtime mixing** (EmuZWin defaults, zxpoly implements the same): the top
  64 indices (192–255) are averaged with the attribute ink colour at render
  time (`UpColorsMixed=64`; also `DownColorsMixed`, `Up/DownMixChgBright`,
  `UseBrightInMix`, `Up/DownMixPaper`); `HideSameInkPaper=1` hides pixels
  whose ink == paper; `Paper00InkFF` forces index 0 → paper colour / 0xFF →
  ink colour. These tricks let one 256-colour palette express attribute-driven
  brightness per cell and effectively exceed 256 on-screen colours.
- The ZX standard 16 colours are mapped onto the 256 palette by nearest colour
  (zxpoly `PALETTE_ALIGNED_ZXPOLY`) for the mixing paths.

## 4. Background layers (EmuZWin era)

- `.bnn` files: raw **320×200 bytes of palette indices = 64 000 bytes**; only
  the centred 256×192 window is displayed (4 px cropped top/bottom, 32 columns
  left/right); index 0x00 = never drawn; GFX pixels take precedence
  (`BkOverFF`/`BackOverFF` refine the edge cases).
- Each background may carry its own palette `.pnn`; the `.cfg` **probe lines**
  (`NN: addr [& masks] = [bytes] …`) select background+palette NN when all
  masked memory comparisons match — i.e. room-dependent backgrounds/palettes
  switched by game state.
- The original 1999 emulator had no backgrounds; they are an EmuZWin 2.4+
  addition (also present in the DivGMX FPGA core).

## 5. Activation

- **No I/O ports, no memory-mapped mode registers.** The mode is an
  emulator-level switch: F2/F3 keys (Spec256 DOS), `Effects | 256 Colors`
  menu (EmuZWin), F2 on FPGA cores; zxpoly activates `BoardMode.SPEC256` on
  loading a Spec256 archive.
- To the program the machine is a plain 48K or 128K Spectrum (`#7FFD` paging,
  `#FE` border). The 256-colour view is a rendering substitution; toggling
  back to the classic ULA view at any time is lossless — both machines have
  been executing in parallel all along.
- No other resolutions/modes exist (320×256, text, interlace: no). A planned
  16-plane true-colour "Z80_GFX" never shipped; zxpoly carries dead code
  stubs for it (`VIDEOMODE_SPEC256_16`, `adaptPageForColor16`) — evidence it
  was contemplated, not released.

## 6. RMW opcodes and correction semantics

Colour-plane values passing through bitmap-oriented arithmetic produce
garbage; implementations offer corrections (all per-game `.cfg` options):

- `GFXLeveledXOR` / `GFXLeveledOR` / `GFXLeveledAND`: replace the colour-plane
  XOR/OR/AND result with `max(a,b)` / `max(a,b)` / `min(a,b)` respectively.
- `GFXScreenXORbuffered`: recognize the sequence
  `XOR A,(HL); LD (HL),A` with HL in $4000–$57FF as a screen XOR and buffer
  it (saves the pre-XOR colour data to `.xor`).
- Per-game register-alignment profiles (§1) — the biggest correctness lever;
  the per-game DB of profiles is `spec256appbase.txt` (zxpoly), keyed by
  sha256(sna+gfx).

Content constraint (EmuZWin doc): games must be **sprite-based** (Elite-style
vector games are not convertible); double-indirect sprite tables are hard;
XOR-drawn sprites need the buffered option.

## 7. Container file formats

A Spec256 "snapshot" ships as a set of files sharing a basename (loose, or
zipped — zxpoly requires ZIP):

| Extension | Size / format | Meaning |
|:--|:--|:--|
| `.sna` / `.z80` | standard, **unmodified** (48K SNA = 49 179 B; some titles ship 128K SNAs = 131 103 B) | the game itself; mode works **only** with SNA/Z80. ⚠ Many adapted 48K games have PC patched to a stub near $FFxx ($FF38/$FFEE/$FFF3/$FFF8/$FFFA) — autostart trampolines, purpose undocumented |
| `.gfx` | **393 216 B** (0x60000) = 3 pages × 0x20000, page order **5, 2, top page** | shadow memory for the three visible 16K banks |
| `.gf0`–`.gf7` | 0x20000 each | extra per-page GFX for 128K titles (pages not already in `.gfx`) |
| `.gfa` / `.gfb` | 0x20000 each | shadow content of ROMs (A = 128K ROM, B = 48K ROM); historical `rom0.gfx`/`rom1.gfx` map to ROM pages **1 / 0** (swapped) |
| `.b00`… | 64 000 B | 320×200 background (§4) |
| `.pal` / `.p00`… | 768 B | palette per background (§3) |
| `.xor` | — | save buffer for XOR-buffered mode (parsed, unused by zxpoly's renderer) |
| `.cfg` | Java-`Properties`-style text | correction options + background probe lines (§4, §6) |
| `.ezx` | chunked binary, signature `Emuz`/`EZX` | EmuZWin's full-state format encapsulating game + graphics; chunk layout in `EZXFormat_Eng.htm` |
| screenshots | PCX (`IMAGEnnn.PCX`, original) / SCR,BMP,PNG (EmuZWin) | — |

`.cfg` keys (EmuZWin names; zxpoly consumes the same):
`GFXLeveledXOR|OR|AND`, `GFXScreenXORbuffered`, `Up/DownColorsMixed`,
`Up/DownMixChgBright`, `UseBrightInMix`, `Up/DownMixPaper`, `BkOverFF`,
`BackOverFF`, `HideSameInkPaper` (default 1), `Paper00InkFF`; zxpoly adds
`zxpAlignRegs`.
