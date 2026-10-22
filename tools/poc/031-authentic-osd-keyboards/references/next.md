# POC 031 references: ZX Spectrum Next (cased, KS1 Issue 2B / KS2 Issue 4)

Collected 2026-10-19. Photos are in `photos/next/` (references only, not for tracing). Anything marked
**UNVERIFIED** comes only from photo measurement or from my own reading, with no written source.

## 1. Case

### 1.1 Identity and design

- Industrial design: Rick Dickinson (Dickinson Associates, died during the keyboard's development), continued by
  Phil Candy (Candy Product Design). Its cues come from the 48K / Spectrum+ / 128, not the Amstrad +2/+3
  (Wikipedia, specnext.com/about).
- KS1 (Issue 2B) and KS2 (Issue 4) cases: "From the outside, the KS1 ... and KS2 models look almost identical",
  "have the same dimensions" (MakerSpace). The only external difference named is the screws: silver on KS1, black on
  KS2. The screws are on the bottom, so they do not show from the top. KS2 also ships with a power-cable switch that
  has rainbow stripes. **One drawing serves both.** Possible differences in the logo print, light pipes or keycap
  print: **UNVERIFIED** (no side-by-side top photo found).

### 1.2 Dimensions

No official figure found. specnext.com lists "50 × 30 × 30 cm, 5 kg" for the Next Plus, which is the shipping box,
not the case. Values below are **UNVERIFIED** and come from a perspective-rectified top-down photo
(`next-topdown-festival-commons.jpg`, rectified as `next-keyboard-rectified-grid.jpg`). The key pitch was
cross-scaled against the +2 lying next to it (+2 pitch taken as 19.05 mm).

| Item | Key units (u) | mm at u ≈ 19 mm |
|---|---|---|
| Key pitch (1u), horizontal = vertical | 1 | ≈ 19 (18.5–19.5) |
| Key grid (every row) | 13.5 × 5 | ≈ 257 × 95 |
| Keyboard well (opening incl. ~0.17u rim each side) | 13.85 × 5.13 | ≈ 263 × 97 |
| Case top, overall | ≈ 17.3 × 7.7 | ≈ 330 × 146 (±15 / ±8) |
| Back band (case back edge to well top) | ≈ 2.25 | ≈ 43 |
| Front band (well bottom to front edge) | ≈ 0.3 | ≈ 6 |
| Left band (case left edge to well) | ≈ 0.1–0.25 | ≈ 2–5 |
| Right section (well right edge to case end) | ≈ 3.4 (flat ≈ 2.4, then the curved roll-off ≈ 1.0) | ≈ 65 |
| Height | — | **UNVERIFIED** (looks ~35–40 mm at the back, thinner at the front: a wedge) |

A rough cross-check: the +2 beside it in the same photo gives a Next/+2 width ratio of ≈ 0.77. If the +2 is 440 mm
wide (**UNVERIFIED** figure), the Next is ≈ 338 mm. The footprint is close to the Spectrum+ (319 × 149 mm), which
fits the design brief.

### 1.3 Top-view layout (origin = left edge of the key grid, the BREAK key's left edge; y down from the well's top edge; u)

- **Keyboard well**: a rectangular recess, x −0.17 … 13.68, y 0 … 5.13. The keys sit on a black glossy deck. The
  gaps between keys are near-black (#060709).
- **Logo "sinclair"**: back-left. x ≈ 0.35 … 4.1, baseline y ≈ −1.5 (top ≈ −1.85). Lower-case Sinclair wordmark,
  glossy black on the matte black case, so it reads only by its sheen and edge highlights (photo
  `next-logo-closeup-timeextension.jpg`). Raised or embossed is **UNVERIFIED**: it looks like gloss print or a
  slight relief.
- **"ZX Spectrum Next"**: directly under the wordmark, left-aligned with it. x ≈ 0.35 … 2.8, y ≈ −1.25.
  Bold-italic sans (Sinclair-style slanted lettering), printed orange-red.
- **Vent grille**: on the back band at the very back edge. x ≈ 8.2 … 13.4, a row of ~30 short slots running
  front-to-back (≈ 0.25u long). At its left end there is a glossy black rectangular tab (≈ 0.8u × 0.3u, x ≈ 8.2–9.0);
  its function is **UNVERIFIED** (looks like a cover or plug). Right of the grille, at x ≈ 13.6, is a small round
  dot, probably an LED (the Issue 2 render shows a red dot there). **UNVERIFIED**.
- **Right section**: a flat panel continues the deck to the right of the well. **Horizontal grooves** continue the
  key-row boundaries across it (4 grooves at y ≈ 1.03, 2.05, 3.08, 4.1, plus the back and front edges, as on the
  128K "toast rack"). The panel then rolls down in a large convex curve to the right end.
- **Rainbow**: four short colour strips set into those four grooves where the curve starts (x ≈ 16.5 … 17.1,
  ≈ 0.6u long seen from the top; they wrap over the curve). Order back → front: **red, yellow, green, cyan-blue**.
  They are coloured inserts, not lit (the KS1/KS2 interior photo shows the coloured parts inside the case end).
  Lit or unlit is **UNVERIFIED**.
- Nothing else is printed on the top. Ports and buttons sit on the edges. Left side: RESET (red), DRIVE (green), SD
  slot, NMI (yellow). Front: two DB9 joystick ports (labelled "JOY1"/"JOY2", **UNVERIFIED** text). Back: DC 9V, the
  "DIGITAL VIDEO DEBUG" cover, HDMI, audio, EAR/MIC, VGA/RGB, PS/2, expansion edge (Time Extension review).

### 1.4 Colours (hex estimates, sampled from photos under different light; **UNVERIFIED** as paint values)

| Part | Estimate | Notes |
|---|---|---|
| Case top (matte, slightly textured black) | #2E2F31 (range #2C2E2D–#39393A) | dark charcoal, faint speckle |
| Keyboard deck / key frames (glossy black) | #18191C, highlights #4A4D52 | glossy, mirror-like edge lines |
| Key gaps | #060709 | |
| Keycap top surface (matte) | #3A3C40 (range #303136–#3C3F44) | lighter than the case |
| Legends | #F2F2F2 (white) | all legends white, none coloured (checked on BLUE/RED/… keys) |
| "sinclair" wordmark | glossy black ≈ #1A1A1C on the matte case | contrast by sheen only |
| "ZX Spectrum Next" text | orange-red ≈ #D9502A (range #C0503A–#F07E1E) | reads red in daylight, orange under warm light |
| Rainbow red | #C81E3C | |
| Rainbow yellow | #EBD52E | |
| Rainbow green | #4CC04C | |
| Rainbow cyan-blue | #0BA8E8 (darker #106A9F in the render) | |

## 2. Keyboard geometry

58 physical keys = the 8×7 membrane matrix (56 positions) plus a second CAPS SHIFT and a second SYMBOL SHIFT.
The duplicate shift keys share one position each (wired in parallel: inferred from the count, **UNVERIFIED** on the
membrane). Every row is 13.5u wide; there is no row stagger beyond what the key widths give. Positions were measured on
the rectified photo (±0.05u) and rounded to 1/8u. They agree with the ZEsarUX `keyboard_next.bmp` layout.

Rows: R1 back … R5 front. x = left edge in u from the grid's left edge; w = width in u. Row height 1u.

| Row | Keys left → right as (x, w) |
|---|---|
| R1 | BREAK (0,1) · EDIT (1,1) · 1 (2,1) · 2 (3,1) · 3 (4,1) · 4 (5,1) · 5 (6,1) · 6 (7,1) · 7 (8,1) · 8 (9,1) · 9 (10,1) · 0 (11,1) · DELETE (12,1.5) |
| R2 | TRUE VIDEO (0,1.25) · INV VIDEO (1.25,1.25) · Q (2.5,1) · W (3.5) · E (4.5) · R (5.5) · T (6.5) · Y (7.5) · U (8.5) · I (9.5) · O (10.5) · P (11.5,1) · ENTER upper leg (12.5,1) |
| R3 | CAPS LOCK (0,1.5) · GRAPH (1.5,1.25) · A (2.75,1) · S (3.75) · D (4.75) · F (5.75) · G (6.75) · H (7.75) · J (8.75) · K (9.75) · L (10.75,1) · ENTER lower part (11.75,1.75) |
| R4 | CAPS SHIFT (0,2.25) · EXTEND MODE (2.25,1.25) · Z (3.5,1) · X (4.5) · C (5.5) · V (6.5) · B (7.5) · N (8.5) · M (9.5,1) · ↑ (10.5,1) · CAPS SHIFT (11.5,2) |
| R5 | SYMBOL SHIFT (0,1) · ; (1,1) · " (2,1) · , (3,1) · . (4,1) · SPACE (5,4.5) · ← (9.5,1) · ↓ (10.5,1) · → (11.5,1) · SYMBOL SHIFT (12.5,1) |

Special shapes:
- **ENTER**: one key shaped like an inverted L (ISO-like, but the narrow leg is on top). Upper leg x 12.5–13.5 on R2;
  lower bar x 11.75–13.5 on R3. Both share the right edge 13.5. The label "ENTER" is centred on the lower bar.
- **Arrows**: an inverted T. ↑ on R4 sits directly above ↓ on R5 (both at x 10.5).
- **SPACE**: 4.5u, no legend.
- Gaps between keycap frames ≈ 0.04u (the frames almost touch). The matte top is inset ≈ 0.08u from the frame
  at the sides and bottom and ≈ 0.25u at the top (see 3).

## 3. Keycap look

- **Construction (from photos)**: two-tone. A square-sided **glossy black frame/skirt** (the full key cell) carries a
  **matte charcoal top** shaped like a "D on its back": a flat top edge, near-vertical sides and a fully rounded,
  semicircular bottom (front) edge. The rounded shape faces the user. On 1u keys it fills ≈ 0.85u × 0.75u, set low in
  the frame. The glossy strip above the matte top (≈ 0.25u) carries the E-mode legends. Wide keys have a stretched D
  (straight top, rounded front corners with radius ≈ 0.38u). The top looks flat to slightly dished; the profile is
  low and flat (laptop-like, as in the 2016 article). Sculpting/dish depth: **UNVERIFIED**.
- **Material**: membrane keyboard ("rubber membrane keyboard", Time Extension; domes and contact strips per the
  Kickstarter updates). The caps look like hard plastic. Their exact material (ABS vs rubber-topped) is
  **UNVERIFIED**.
- **Legend method**: printed white (pad print or similar, **UNVERIFIED**). All legends are white.
- **Font**: geometric rounded sans, bold, upper-case for keywords (the Spectrum+ / 128 keyboard style). The main
  letter/digit is large and bold, centred at the bottom of the matte top. The digit 0 is printed with a slash "Ø".
  The "sinclair" wordmark and the italic "ZX Spectrum Next" are the brand fonts. Exact typeface name:
  **UNVERIFIED**.

### 3.1 Legend placement per key type

- **Letter keys**: on the glossy top strip, two centred lines, the E-mode keyword over the SS+E-mode keyword (e.g.
  SIN / ASN). On the A, S, D, F, G and Y, U keys the top strip carries one keyword plus a small SS+E symbol set to
  its left (~ | \ { } [ ]). On the matte top: the K-mode keyword at the top, the symbol-shift item (symbol at the
  right, or an SS keyword as a second line under the keyword), and the big letter at the bottom.
- **Digit keys 1–8**: top strip = colour name / E-mode keyword (BLUE / DEF FN). Matte top = graphics-block icon
  (left), SS symbol (right), digit (bottom).
- **Named keys** (BREAK, EDIT, DELETE, TRUE VIDEO, INV VIDEO, CAPS LOCK, GRAPH, EXTEND MODE, CAPS SHIFT, SYMBOL
  SHIFT, ENTER): small upper-case text, one or two lines, centred on the matte top, set high. Arrow keys: an outline
  arrow (hollow, white stroke), centred. The ; " , . keys: the one character, centred, mid-size.

### 3.2 Every legend (all white)

| Key | Top strip (E-mode / SS+E) | Matte top: keyword, SS item | Main |
|---|---|---|---|
| 1 | BLUE / DEF FN | graphic ▘(block 1), ! | 1 |
| 2 | RED / FN | graphic ▝(block 2), @ | 2 |
| 3 | MAGENTA / LINE | graphic ▀(block 3), # | 3 |
| 4 | GREEN / OPEN # | graphic ▗(block 4), $ | 4 |
| 5 | CYAN / CLOSE # | graphic ▐(block 5), % | 5 |
| 6 | YELLOW / MOVE | graphic ▚(block 6), & | 6 |
| 7 | WHITE / ERASE | graphic ▜(block 7), ' | 7 |
| 8 | POINT | graphic □ (block 8, blank), ( | 8 |
| 9 | CAT | ) | 9 |
| 0 | BLACK (inverse: black text in a white box) / FORMAT | _ | Ø |
| Q | SIN / ASN | PLOT, <= | Q |
| W | COS / ACS | DRAW, <> | W |
| E | TAN / ATN | REM, >= | E |
| R | INT / VERIFY | RUN, < | R |
| T | RND / MERGE | RAND, > | T |
| Y | [ STR$ | RETURN / AND | Y |
| U | ] CHR$ | IF / OR | U |
| I | CODE / IN | INPUT / AT | I |
| O | PEEK / OUT | POKE, ; | O |
| P | TAB / (C) | PRINT, " | P |
| A | ~ READ | NEW / STOP | A |
| S | \| RESTR | SAVE / NOT | S |
| D | \ DATA | DIM / STEP | D |
| F | { SGN | FOR / TO | F |
| G | } ABS | GOTO / THEN | G |
| H | SQR / CIRCLE | GOSUB, ↑ | H |
| J | VAL / VAL$ | LOAD, − | J |
| K | LEN / SCRN$ | LIST, + | K |
| L | USR / ATTR | LET, = | L |
| Z | LN / BEEP | COPY, : | Z |
| X | EXP / INK | CLEAR, £ | X |
| C | LPRINT / PAPER | CONT, ? | C |
| V | LLIST / FLASH | CLS, / | V |
| B | BIN / BRIGHT | BORDER, * | B |
| N | INKEY$ / OVER | NEXT, , | N |
| M | PI / INVERSE | PAUSE, . | M |
| Named | BREAK · EDIT · DELETE · TRUE / VIDEO · INV / VIDEO · CAPS / LOCK · GRAPH · EXTEND / MODE · CAPS SHIFT (one line on both) · SYMBOL / SHIFT · ENTER | — | — |
| Others | ; · " (shown as ") · , · . · ↑ ↓ ← → (hollow arrows) · SPACE (blank) | — | — |

Read from the rectified top-down photo. Two details are **UNVERIFIED** because the photo is too small there: the exact
graphic-block glyph per digit (taken from the standard Spectrum block set) and the 0 key's inverse word (it reads as
a white box holding "BLACK").

## 4. Matrix mapping

### 4.1 Hardware (VHDL, `fpga/MiSTer-devel-ZXNext_MISTer/rtl/input/membrane/membrane.vhd`)

The membrane is scanned as 8 rows (A8..A15) × 7 columns. Columns 0–4 are the standard 8×5 Spectrum matrix; columns 5
and 6 are the extra keys (membrane.vhd:166–179). Each extra key also enters its composite (shift + key) into the
8×5 matrix (membrane.vhd:186–244, read-out 248–256). NextReg 0x68 bit 4 cancels those entries
(`i_cancel_extended_entries`, zxnext.vhd:5451). The shift is advanced one scan and held one scan longer so naive
software sees it first (membrane.vhd:181–182, 222). NR 0xB0/0xB1 report the extra keys separately, 1 = pressed
(zxnext.vhd:6155–6164; wiki Extended_Keys_0/1_Register, "since core 3.1.5").

Standard positions: half-row = port high byte (A8 = #FE … A15 = #7F); bit 0 is the outer key.

| Key | Membrane (row Ax, col) | 8×5 effect (port, bit) | NR 0xB0 / 0xB1 bit |
|---|---|---|---|
| CAPS SHIFT (both keys) | A8 col 0 | #FE b0 | — |
| Z X C V | A8 col 1–4 | #FE b1–b4 | — |
| A S D F G | A9 col 0–4 | #FD b0–b4 | — |
| Q W E R T | A10 col 0–4 | #FB b0–b4 | — |
| 1 2 3 4 5 | A11 col 0–4 | #F7 b0–b4 | — |
| 0 9 8 7 6 | A12 col 0–4 | #EF b0–b4 | — |
| P O I U Y | A13 col 0–4 | #DF b0–b4 | — |
| ENTER L K J H | A14 col 0–4 | #BF b0–b4 | — |
| SPACE, SYMBOL SHIFT (both keys), M N B | A15 col 0–4 | #7F b0–b4 | — |
| EXTEND MODE | A8 col 5 | CS + SS (#FE b0 + #7F b1) | B1 b0 |
| ↑ UP | A8 col 6 | CS + 7 | B0 b3 |
| CAPS LOCK | A9 col 5 | CS + 2 | B1 b1 |
| GRAPH | A9 col 6 | CS + 9 | B1 b2 |
| TRUE VIDEO | A10 col 5 | CS + 3 | B1 b3 |
| INV VIDEO | A10 col 6 | CS + 4 | B1 b4 |
| BREAK | A11 col 5 | CS + SPACE | B1 b5 |
| EDIT | A11 col 6 | CS + 1 | B1 b6 |
| ; | A12 col 5 | SS + O | B0 b7 |
| " | A12 col 6 | SS + P | B0 b6 |
| , | A13 col 5 | SS + N | B0 b5 |
| . | A13 col 6 | SS + M | B0 b4 |
| DELETE | A14 col 5 | CS + 0 | B1 b7 |
| → RIGHT | A14 col 6 | CS + 8 | B0 b0 |
| ← LEFT | A15 col 5 | CS + 5 | B0 b1 |
| ↓ DOWN | A15 col 6 | CS + 6 | B0 b2 |

NR 0xB0 = ; " , . UP DOWN LEFT RIGHT (b7…b0). NR 0xB1 = DELETE EDIT BREAK INV TRUE GRAPH CAPSLOCK EXTEND (b7…b0).
The membrane rows above are named after the scan index (index 0 = A8). That a row index equals the address line on
the physical membrane is inferred from the state machine (membrane.vhd:90–100, 117–135).

### 4.2 What unreal-ng implements (branch `controller-manager-design`, HEAD 98b69e223)

- `core/src/emulator/io/z80n/nextextendedinput.h:21-96`, `.cpp:68-84`: NR 0xB0/0xB1 (and 0xB2 pad extras) with the
  exact VHDL bit order (`enum Key`, h:25-30). Fed only by host PC keys through `IPs2KeySink` using the keymaps.vhd US
  assignments (cpp:7-23: LeftAlt = EXTEND, RightAlt = GRAPH, Tab = TRUE VIDEO, \ = INV VIDEO, Esc = BREAK, ` = EDIT,
  Backspace = DELETE, arrows, Caps Lock, ; ' , .). `SetKeys(uint16_t)` (h:51) lets a tool set the bits directly, so an
  OSD keyboard can use it.
- Read dispatch: `core/src/emulator/io/z80n/nextboard.cpp:133-136` → `PortDecoder_Next::ReadExtendedInput`
  (`core/src/emulator/ports/models/portdecoder_next.cpp:575-583`). The sink is installed at portdecoder_next.cpp:32-34.
  Port #FE masks membrane columns 5/6 out of the joystick-key path (portdecoder_next.cpp:556-562).
- The composite 8×5 entries come from the generic ZX key layer, `Keyboard::_zxExtendedKeyMap`
  (`core/src/emulator/io/keyboard/keyboard.cpp:48-71`, enum `keyboard.h:75-96`). It covers LEFT/RIGHT/UP/DOWN,
  DELETE, CAPS LOCK, BREAK, EDIT, ".", ",", '"' (plus the 128K-style + - * / = | \ symbols). **It has no entries for
  EXTEND MODE (CS+SS), GRAPH (CS+9), TRUE VIDEO (CS+3), INV VIDEO (CS+4) or ";" (SS+O).** An OSD key for these must
  press the two matrix keys itself and set the B0/B1 bit, or the enum needs new values. Pressing a ZX extended key
  through this layer does **not** set the NR 0xB0/0xB1 bit: only host PC keys reach `NextExtendedInput`.
- **NR 0x68 bit 4 (cancel extended entries) is not modelled.** No reference exists in `core/src/emulator/io/z80n/` or
  `ports/models/portdecoder_next*.cpp`; only bit 3 is used (portdecoder_next.cpp:485, 729). The VHDL shift-lead
  timing (membrane.vhd:222) is not modelled either; for an emulator it is not needed.

## 5. Photos saved (`photos/next/`)

| File | What | Source / licence |
|---|---|---|
| next-topdown-festival-commons.jpg | Near top-down Next next to a +2 and +3 (scale reference) — **the main geometry reference** | Commons, Thomas Vogt, 2025-10-12, CC BY 4.0 |
| next-keyboard-rectified-grid.jpg | The keyboard well from that photo, perspective-rectified, grid 0.5u (my derivative) | as above |
| next-top-threequarter-timeextension.jpg | High-res top view, slight angle: legends, rainbow, vent, logo | Time Extension review (copyright, reference only) |
| next-logo-closeup-timeextension.jpg | "sinclair" + "ZX Spectrum Next" and the R1/R2 keycaps close-up | Time Extension |
| next-rainbow-right-end-timeextension.jpg | The right-end curve with the four strips | Time Extension |
| next-left-side-buttons-timeextension.jpg | Left side: RESET / DRIVE / SD / NMI | Time Extension |
| next-rear-ports-timeextension.jpg | Back panel | Time Extension |
| next-front-joystick-ports-timeextension.jpg | Front edge DB9 ports, keycap profile from the front | Time Extension |
| next-issue2-render-commons.jpg | Official Issue 2 render (3/4 view; legends, groove lines, LED dot) | Commons, Olifiers, CC0 |
| next-ks1-vs-ks2-interior-makerspace.jpg | KS1 (2B) vs KS2 (4) opened cases: same shell | MakerSpace article (copyright, reference only) |
| next-zesarux-keyboard-bitmap.jpg | ZEsarUX's drawn Next keyboard (layout cross-check) | local collection, GPL |
| next-white-top-owner.jpg | The white edition, 3/4 top view: case, caps, print, wordmark, rainbow (section 8) | the owner, 2026-10-21 (reference only) |

## 6. Sources

Local:
- `zx-spectrum-data/zx-next/fpga/MiSTer-devel-ZXNext_MISTer/rtl/input/membrane/membrane.vhd`
  (lines 166–256)
- `zx-spectrum-data/zx-next/fpga/MiSTer-devel-ZXNext_MISTer/rtl/zxnext.vhd` (79–84, 1563, 5451,
  6155–6164)
- `zx-spectrum-data/zx-next/documents/specnext-wiki/Extended_Keys_0_Register.html`,
  `Extended_Keys_1_Register.html`, `Keyboard.html`
- `zx-spectrum-data/zx-next/emulators/zesarux/src/keyboards/keyboard_next.bmp`
- unreal-ng files cited in 4.2

Web:
- https://commons.wikimedia.org/wiki/File:Sinclair_ZX_Spectrum_%2B2,_%2B3_und_Next_(54854067085).jpg
- https://commons.wikimedia.org/wiki/File:SpecNext_06_Mid.png
- https://www.timeextension.com/reviews/zx-spectrum-next-this-fpga-powerhouse-does-sir-clive-sinclairs-legacy-proud
- https://www.makerspace-online.com/second-edition-of-the-fpga-based-sinclair-zx-spectrum-next/
- https://en.wikipedia.org/wiki/ZX_Spectrum_Next
- https://www.specnext.com/about/
- https://www.specnext.com/product/zx-spectrum-next-plus-computer/ (box size only)
- https://www.indieretronews.com/2016/06/zx-spectrum-next-new-era-for-speccy-big.html (membrane "like a modern
  laptop", via search summary)
- Kickstarter update "The Keyboard's Definitive Solution"
  (https://www.kickstarter.com/projects/1835143999/zx-spectrum-next/posts/2544900): HTTP 403, so it is known only
  from the search summary (third contact strip, composite keys as unique keys). **UNVERIFIED** first-hand.

## 7. Unverified, summary

- Every mm figure (no official dimensions exist; derived from a photo with an assumed 19.05 mm +2 pitch). Case height.
- All hex colours (photo samples under mixed light).
- Typeface names; keycap material; dish depth; legend print method.
- What the back tab next to the vents is, and whether the dot is an LED.
- Whether the rainbow strips are lit.
- Any top-visible KS1/KS2 difference (the sources say the cases look the same).
- Graphic-block glyph detail on the digit keys; the 0 key's inverse "BLACK".
- That the two CAPS SHIFT / SYMBOL SHIFT keys are wired in parallel (inferred from 58 keys on 56 positions).

## 8. White edition

From the owner's photo `next-white-top-owner.jpg` (2026-10-21, a 3/4 top view, 630 x 500 px). It is the same shell
and keyboard as the black Next: the geometry of sections 1-2 holds, only the colours differ. Every value below is a
photo reading, **UNVERIFIED** (one phone photo under room light, no written source).

| Part | White edition | Black (section 1.4) |
|---|---|---|
| Case | white, a little warm grey in shade (sampled `#e3e3e1`, `#d5d4d0` on the right end) | black |
| Caps | white two-tone caps, the matte top a shade cooler than the glossy strip (`#e8eced`) | black |
| Gaps between the caps | light grey: the well reads grey, not black | black |
| Legends | black, every one (the colour names too: no coloured print); the 0 key's BLACK is a black box with white print | white; BLACK white box, black print |
| "sinclair" | moulded white on white; the relief's flanks read as a thin grey contour (sampled ≈ `#8c8a8a`) | glossy black on matte black |
| "ZX Spectrum Next" | red, deeper than the black edition's orange red (sampled `#8f1220` on the antialiased stroke) | `#d9502a` |
| Vent ribs, tab | white | black |
| LED | a red dot (it may be lit in the photo) | dark |
| Rainbow inserts | red, yellow, green, blue at the right end of the four grooves | the same |

Drawn as `keyboards/zxnextwhite.py`, the Next's second picture (the black one stays the default).

