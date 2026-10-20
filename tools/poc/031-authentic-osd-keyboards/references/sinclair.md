# POC 031: references for the Sinclair / Amstrad case-top keyboards

Machines: ZX Spectrum 48K (rubber keys), ZX Spectrum+ (48K+), ZX Spectrum 128 (UK and Spanish "toastrack"),
Amstrad +2 (grey), +2A (black, Datacorder), +3 (black, 3" disc drive).

Conventions in this file:

- `u` = one key pitch. 48K: `u48`, its own pitch. +/128/+2/+2A/+3: `u`, the 58-key pitch.
- x is measured from the left edge of the keyboard block, y from its top. `x`/`w` are in `u`.
- Positions on the case are in mm from the case's **rear-left** corner (x to the right, y toward the user) unless
  stated otherwise.
- Hex colours are **estimates** from photos (white balance differs between photos). Ranges give the samples.
- **Measured** = I measured pixels on a photo or scan listed in Sources. **UNVERIFIED** = no primary source found,
  or the measurement is uncertain by more than about 5%.
- Photos are in `photos/sinclair/` (named `<machine>-<what>`); file names below are
  relative to that folder.

---

## 0. Matrix (shared by all six machines)

The Spectrum reads 8 half-rows × 5 bits. To read a half-row, one address line A8..A15 is driven low, then
`IN (C)` is executed with port low byte `#FE`. D0 is the key at the outer end of the half-row, D4 the key nearest
the middle. A pressed key reads as 0. Source: the 48K BASIC manual, chapter 23 (quoted: "D0 for the outside key, D4 for
the one nearest the middle. The bit is 0 if the key is pressed").

| Half-row | Address line | Port (hex / dec) | D0 | D1 | D2 | D3 | D4 |
|---|---|---|---|---|---|---|---|
| 0 | A8  | `FEFE` / 65278 | CAPS SHIFT | Z | X | C | V |
| 1 | A9  | `FDFE` / 65022 | A | S | D | F | G |
| 2 | A10 | `FBFE` / 64510 | Q | W | E | R | T |
| 3 | A11 | `F7FE` / 63486 | 1 | 2 | 3 | 4 | 5 |
| 4 | A12 | `EFFE` / 61438 | 0 | 9 | 8 | 7 | 6 |
| 5 | A13 | `DFFE` / 57342 | P | O | I | U | Y |
| 6 | A14 | `BFFE` / 49150 | ENTER | L | K | J | H |
| 7 | A15 | `7FFE` / 32766 | SPACE | SYMBOL SHIFT | M | N | B |

The 128, +2, +2A and +3 have no extra keyboard lines: the same 40 positions, the same ULA/gate-array port.
The 128K's external numeric keypad (Spanish 128, serial via the AY port) is a separate device and out of scope.

### Extra keys of the 58-key keyboards (+, 128, +2, +2A, +3)

Each extra key closes **two** matrix positions at once (dual-contact membrane). The table is the TBBlue
firmware `KeyboardTester.asm` (local, see Sources). The meaning is from the 48K ROM / 48K keyboard plate, where the
same combination prints the legend above the number key (EDIT above 1 and so on).

| Extra key | = keys | Matrix positions |
|---|---|---|
| TRUE VIDEO | CAPS SHIFT + 3 | (A8,D0)+(A11,D2) |
| INV VIDEO | CAPS SHIFT + 4 | (A8,D0)+(A11,D3) |
| BREAK | CAPS SHIFT + SPACE | (A8,D0)+(A15,D0) |
| DELETE | CAPS SHIFT + 0 | (A8,D0)+(A12,D0) |
| GRAPH | CAPS SHIFT + 9 | (A8,D0)+(A12,D1) |
| EXTEND MODE | CAPS SHIFT + SYMBOL SHIFT | (A8,D0)+(A15,D1) |
| EDIT | CAPS SHIFT + 1 | (A8,D0)+(A11,D0) |
| CAPS LOCK | CAPS SHIFT + 2 | (A8,D0)+(A11,D1) |
| ⇦ / `<` (cursor left) | CAPS SHIFT + 5 | (A8,D0)+(A11,D4) |
| ⇩ / `v` (cursor down) | CAPS SHIFT + 6 | (A8,D0)+(A12,D4) |
| ⇧ / `^` (cursor up) | CAPS SHIFT + 7 | (A8,D0)+(A12,D3) |
| ⇨ / `>` (cursor right) | CAPS SHIFT + 8 | (A8,D0)+(A12,D2) |
| `;` | SYMBOL SHIFT + O | (A15,D1)+(A13,D1) |
| `"` | SYMBOL SHIFT + P | (A15,D1)+(A13,D0) |
| `,` | SYMBOL SHIFT + N | (A15,D1)+(A15,D3) |
| `.` | SYMBOL SHIFT + M | (A15,D1)+(A15,D2) |

The two CAPS SHIFT keys and the two SYMBOL SHIFT keys of the 58-key layout are wired in parallel to the same
position.

Dual-contact construction on the Spectrum+: "multiple-layer membrane with two different pairs of electrical
contacts" (sinclairzxworld forum, a secondary source), and Crash #12, which says the extra keys "increased the
layers in the membrane". For the +2/+2A/+3 the logical result is the same (no extra lines exist), but the contact
construction is **UNVERIFIED** (no schematic read).

---

## 1. ZX Spectrum 16K/48K (1982, rubber keys)

### 1.1 Case

| Fact | Value | Source |
|---|---|---|
| Dimensions | 233 × 144 × 30 mm (W × D × H), about 552 g | Wikipedia; Sinclair 1982 leaflet (spec_technical, nvg) |
| Designer | Rick Dickinson | Wikipedia |
| Plan view | Rectangle with rounded vertical corners. A **raised rear section (the "hump")** runs the full width, ≈40 mm deep (measured 40–43 mm). It steps down to the keyboard plate, ≈100 mm deep, which reaches the front edge. The plate's front edge wraps over the case front as a thin rounded lip. | measured on `48k-k1-top2.jpg`, `48k-rubber-mat.jpg` |
| Case plastic | Black, fine matte stipple texture. Est. `#1C1C1B` (samples `#1e1e1d`–`#2d3029`) | photos |
| Keyboard plate | Metal (aluminium) plate, black print, matte-satin. Est. `#121212` (samples `#141414`–`#161815`) | photos; Wikipedia ("metal" plate: UNVERIFIED material wording) |
| "sinclair" logo | Sinclair logotype in outline/inline (double-contour letters), **moulded into the hump** (no colour, black on black, slightly glossier than the stipple). Top-left of the hump, ≈2–4 mm from the left edge and ≈1–3 mm from the rear edge, ≈89 mm wide, ≈9 mm tall. | `48k-k1-logo.jpg`, `48k-k1-top2.jpg` (measured) |
| "ZX Spectrum" | Printed white/very light grey (`#E8E8E8`), upright sans (Helvetica-like medium weight), on the hump under the logo. Left aligned with the logo at ≈10 mm from the left edge; baseline ≈18 mm from the rear; ≈32 mm long, cap height ≈4 mm | `48k-k1-logo.jpg`, `48k-k1-top2.jpg` (measured) |
| Rainbow stripe | 4 parallel diagonal stripes, from the left/top: **red, yellow, green, cyan**, printed on the plate in its bottom-right corner. They rise from the plate's front edge (x ≈204–221 mm, the whole band ≈17 mm wide horizontally, each stripe ≈4 mm) to the right edge (y ≈63–87 mm from the rear). Angle ≈65–70° to the horizontal (measured in perspective). They pass **behind** the right part of ENTER and the bottom-right corner of BREAK/SPACE (the keys overlap the stripes). | `48k-k1-top2.jpg` (measured), `48k-top.jpg` |
| Stripe colours (est.) | red `#E2341F` (samples `#eb3528`, `#df351e`), yellow `#E0A20A` (warm, slightly orange; samples `#e29f0a`, `#d19e04`), green `#5E9418` (yellow-green; samples `#69910a`, `#6e8900`), cyan `#1A88B4` (samples `#1487b4`, `#1b87ae`) | `48k-k1-top2.jpg` |
| Underside | `sinclair / ZX SPECTRUM / PERSONAL COMPUTER / USE ONLY APPROVED AC ADAPTOR / PATENT PENDING` moulded, 4 rubber feet, speaker holes at front-left (not needed for the top view) | `48k-bottom.jpg` |
| Issue 1 | Light grey keys (`#B8BCB8`-ish), otherwise identical | Wikipedia ("light grey for Issue 1, blue-grey for later machines"), `48k-issue1-grey-keys.jpg` |

### 1.2 Keycaps

- Material: one-piece silicone rubber key mat, blue-grey, matte. Each key is a raised rounded-rectangle pad that comes
  up through a cut-out in the plate. Est. **`#5A6C6C`** (samples `#5b6063`, `#46524e`, `#5a7a74`; ZEsarUX draws
  `#4f6563` with highlight `#819795` and shadow `#3d5452`). The mat between the keys (seen only without the plate)
  is `#374546`.
- Shape: the top face is flat with softly rounded edges; corner radius ≈1.5 mm (UNVERIFIED). The key side shows as a
  darker skirt on the front edge.
- Size (measured): top face ≈ **0.72 u48 wide × 0.55 u48 deep** (≈14.5 × 10.7 mm). CAPS SHIFT ≈1.0 u48 wide,
  BREAK/SPACE ≈1.22 u48. ENTER and SYMBOL SHIFT are normal width.
- Pitch (measured): horizontal **u48 ≈ 20.2 mm** (19.5–20.4 across two photos); vertical row pitch
  **≈19.5 mm**. UNVERIFIED exact (no drawing found; 0.75" = 19.05 mm does not fit the measurements).
- Row centres from the case rear edge: **57.5, 77, 96.5, 116.5 mm** (measured on two photos, ±1 mm).
- Left edge of key "1" ≈11 mm from the case's left edge. The right edges of ENTER and SPACE line up, ≈5–6 mm from the
  right edge.

### 1.3 Geometry (all 40 keys)

x = left edge of the key's top face, in u48, from the left edge of key "1". Key width 0.72 unless stated.
Row y centres are in §1.2. Offsets per row: Q row +0.50, A row +0.75 (measured 0.74–0.78), Z row +1.25 (Z at 1.24–1.31).

| Row | Keys (x) |
|---|---|
| 1 | 1 (0.00), 2 (1.00), 3 (2.00), 4 (3.00), 5 (4.00), 6 (5.00), 7 (6.00), 8 (7.00), 9 (8.00), 0 (9.00) |
| 2 | Q (0.50), W (1.50), E (2.50), R (3.50), T (4.50), Y (5.50), U (6.50), I (7.50), O (8.50), P (9.50) |
| 3 | A (0.75), S (1.75), D (2.75), F (3.75), G (4.75), H (5.75), J (6.75), K (7.75), L (8.75), ENTER (9.75) |
| 4 | CAPS SHIFT (0.00, w 1.00), Z (1.25), X (2.25), C (3.25), V (4.25), B (5.25), N (6.25), M (7.25), SYMBOL SHIFT (8.25), BREAK SPACE (9.25, w 1.22) |

### 1.4 Legends (48K)

Three print zones per key: **above the key on the plate**, **on the key**, **below the key on the plate**. Font: a
Helvetica-like grotesque. The key letter/digit is bold, the rest medium, all caps (UNVERIFIED exact face).

Colours: on-key main = white `#F2F2F2`; on-key keyword = white; on-key symbol (SYMBOL SHIFT) = red `#E0302A`;
above = green `#3DBE3D` (E mode, unshifted). Exceptions in the number row: the colour names are printed in their own
colour and EDIT and the others in white. Below = red `#E0302A` (E mode + SYMBOL SHIFT). ZEsarUX uses green `#43d443` and red
`#d44343`/`#ed3b3d`.

On-key layout: main character large at top-left. The red symbol sits top-right, the white keyword bottom-right (small). On
number keys 1..8 a white mosaic glyph sits top-right and the red symbol is under it.

| Key | Above (colour) | On key: main / keyword (white) | On key: symbol (red) | Below (red) |
|---|---|---|---|---|
| 1 | BLUE (blue `#2F5FE0`), EDIT (white) | 1 / graphic chr 129 ▝ | ! | DEF FN |
| 2 | RED (red), CAPS LOCK (white) | 2 / graphic chr 130 ▘ | @ | FN |
| 3 | MAGENTA (magenta `#C030C0`), TRUE VIDEO (white) | 3 / graphic chr 131 ▀ | # | LINE |
| 4 | GREEN (green), INV. VIDEO (white) | 4 / graphic chr 132 ▗ | $ | OPEN # |
| 5 | CYAN (cyan `#20B8E8`), ⇦ (white outline arrow) | 5 / graphic chr 133 ▐ | % | CLOSE # |
| 6 | YELLOW (yellow `#F0D020`), ⇩ | 6 / graphic chr 134 ▚ | & | MOVE |
| 7 | WHITE (white), ⇧ | 7 / graphic chr 135 ▜ | ' | ERASE |
| 8 | ⇨ (white) | 8 / graphic chr 128 (solid white square on the key) | ( | POINT |
| 9 | GRAPHICS (white) | 9 | ) | CAT |
| 0 | BLACK (black text in a white box), DELETE (white) | 0 (slashed zero) | _ (drawn as a red bar) | FORMAT |
| Q | SIN | Q / PLOT | <= | ASN |
| W | COS | W / DRAW | <> | ACS |
| E | TAN | E / REM | >= | ATN |
| R | INT | R / RUN | < | VERIFY |
| T | RND | T / RAND | > | MERGE |
| Y | STR $ | Y / RETURN | AND | [ |
| U | CHR $ | U / IF | OR | ] |
| I | CODE | I / INPUT | AT | IN |
| O | PEEK | O / POKE | ; | OUT |
| P | TAB | P / PRINT | " | © |
| A | READ | A / NEW | STOP | ~ |
| S | RESTORE | S / SAVE | NOT | \| |
| D | DATA | D / DIM | STEP | \ |
| F | SGN | F / FOR | TO | { |
| G | ABS | G / GOTO | THEN | } |
| H | SQR | H / GOSUB | ↑ | CIRCLE |
| J | VAL | J / LOAD | - | VAL $ |
| K | LEN | K / LIST | + | SCREEN $ |
| L | USR | L / LET | = | ATTR |
| ENTER | — | ENTER (white, small, centred) | — | — |
| CAPS SHIFT | — | CAPS / SHIFT (white, 2 lines, centred) | — | — |
| Z | LN | Z / COPY | : | BEEP |
| X | EXP | X / CLEAR | £ | INK |
| C | L PRINT | C / CONT | ? | PAPER |
| V | L LIST | V / CLS | / | FLASH |
| B | BIN | B / BORDER | * | BRIGHT |
| N | IN KEY $ | N / NEXT | , | OVER |
| M | PI | M / PAUSE | . | INVERSE |
| SYMBOL SHIFT | — | SYMBOL / SHIFT (**red**, 2 lines) | — | — |
| SPACE | — | BREAK (small, upper line) / SPACE (large), white | — | — |

Mosaic glyph polarity on the 48K: white = paper, key colour = ink. Key 8 (chr 128, all paper) is a solid white
square. Key 1 is a white square with its top-right quadrant missing. The character codes follow the ROM: GRAPH +
1..8 gives 129..135, 128. The polarity was read from `48k-top.jpg`; per-key glyphs need a close-up check, so they are
UNVERIFIED in detail.

---

## 2. ZX Spectrum+ (1984, "48K+")

### 2.1 Case

| Fact | Value | Source |
|---|---|---|
| Dimensions | 319 × 149 × 38 mm | Wikipedia; Retro Isle |
| Style | QL-style wedge: a flat top sloping gently toward the front, a raised rear band, horizontal grooves across the side margins at the row boundaries (4–5 lines left and right of the keyboard, continuing to the case edges) | Wikipedia; `plus-top.jpg`, `plus-k1-top.jpg` |
| Case plastic | Black/charcoal, matte. Est. `#2A2A2A` (photo samples `#494847` under bright light) | photos |
| Rear edge | Vent comb (short ribs) along the rear edge from about the middle to the right end | `plus-top.jpg` |
| "sinclair" logo | Sinclair logotype **moulded/embossed** black on black, top-left: ≈13 mm from the left, ≈85 mm wide, on the rear band | `plus-top.jpg`, `plus-k1-top.jpg` (measured approx.) |
| "ZX Spectrum+" | **Red** (est. `#C8323A`, sample `#983a3c` in shade), thin-stroke sans, directly under the logo, left aligned, ≈60 mm long | `plus-top.jpg` |
| Badge | Rainbow badge: 4 parallel diagonal parallelograms **red, yellow, green, cyan** (no text), in the bottom-right corner of the top, right of the right SYMBOL SHIFT, ≈25 × 12 mm. Est. red `#E8392C`, yellow `#F5C518`, green `#5DAA3C`, cyan `#2FA8E0` | `plus-top.jpg` (sampled bright `#ff6b5e/#fee241/#9dd177/#9ae1ff`, overexposed) |
| Keyboard block | ≈257 × 95 mm (13.5 u × 5 u), left margin ≈10 mm, right margin ≈52 mm (the badge sits in that margin), top edge ≈40 mm from the rear | `plus-k1-top.jpg` (measured, ±3 mm) |
| Reset button | On the left side of the case (not on the top) | Wikipedia (reset button); UNVERIFIED position |

### 2.2 Keycaps (+ and 128)

- Material: hard black plastic, **double-shot (double injection) moulded** (Crash #12: "the excellent double
  injection moulded keys ... the doubleshot technique guarantees ... no fading of the lettering"). Satin finish.
  Est. `#1E1E1E`–`#262626`, highlights to `#5A5A5A`.
- Shape: rectangular blocks almost touching (gap ≈0.05 u ≈1 mm). The rear third of the top is a flat ledge with the
  small legends. The front two thirds is a convex rounded **pad** (a shiny half-pill bulge) that carries the main
  character, the symbol and the keyword. The 1985 scan `128k-k1-keyboard.jpg` shows the bright pill highlight on every
  key. Wide keys have the same pill stretched.
- Pitch: **u ≈ 19 mm** (measured 18.6–19.7; 0.75" = 19.05 mm is probable, UNVERIFIED). Square pitch: the row height
  is 1 u.
- All legends **white** (`#F0F0F0`), upright bold grotesque, all caps. There is no colour on the keys.

### 2.3 Geometry (58 keys; shared by +, 128, +2, +2A, +3)

Measured on the flat scan `128k-k1-keyboard.jpg` (1024 px = 13.5 u, 75.9 px/u) and the ZEsarUX bitmaps
(40 px/u); the two agree. y = row index (0 = top), height 1 u, except ENTER.

| Row | Key: x (w) |
|---|---|
| 0 | TRUE VIDEO 0 (1), INV VIDEO 1 (1), 1 2 (1), 2 3, 3 4, 4 5, 5 6, 6 7, 7 8, 8 9, 9 10, 0 11, BREAK 12 (1.5) |
| 1 | DELETE 0 (1.5), GRAPH 1.5 (1), Q 2.5, W 3.5, E 4.5, R 5.5, T 6.5, Y 7.5, U 8.5, I 9.5, O 10.5, P 11.5, ENTER (upper limb) 12.5 (1) |
| 2 | EXTEND MODE 0 (1.75), EDIT 1.75 (1), A 2.75, S 3.75, D 4.75, F 5.75, G 6.75, H 7.75, J 8.75, K 9.75, L 10.75, ENTER (lower limb) 11.75 (1.75) |
| 3 | CAPS SHIFT 0 (2.25), CAPS LOCK 2.25 (1), Z 3.25, X 4.25, C 5.25, V 6.25, B 7.25, N 8.25, M 9.25, `.` 10.25 (1), CAPS SHIFT 11.25 (2.25) |
| 4 | SYMBOL SHIFT 0 (1), `;` 1, `"` 2, ⇦ 3, ⇨ 4, SPACE 5 (4.5), ⇧ 9.5, ⇩ 10.5, `,` 11.5, SYMBOL SHIFT 12.5 (1) |

ENTER is an **inverted L ("⅃")**: a narrow 1 u upright in row 1 (x 12.5–13.5) on a 1.75 u foot in row 2
(x 11.75–13.5). It is narrow at the top and wide at the bottom, with its right edge flush with the block. Every row is 13.5 u wide.

### 2.4 Legends (+ / UK 128)

Layout on each key: line 1 (top, small) = the E-mode keyword (the 48K's green "above" legend). Line 2 (small) = the
E-mode + SYMBOL SHIFT legend (the 48K's red "below"). Then the K-mode keyword (left/middle), the SYMBOL SHIFT symbol
(right) and the main character (large, bottom centre). On the number keys, line 1 = colour name, line 2 = E+SS keyword,
then the mosaic glyph (left), the symbol (right) and the digit (bottom). All white. From `128k-k1-keyboard.jpg`:

| Key | Line 1 | Line 2 | Keyword | Symbol | Main |
|---|---|---|---|---|---|
| TRUE VIDEO | — | — | — | — | TRUE / VIDEO (2 lines) |
| INV VIDEO | — | — | — | — | INV / VIDEO |
| 1 | BLUE | DEF FN | graphic | ! | 1 |
| 2 | RED | FN | graphic | @ | 2 |
| 3 | MGNTA | LINE | graphic | # | 3 |
| 4 | GREEN | OPEN # | graphic | $ | 4 |
| 5 | CYAN | CLOSE # | graphic | % | 5 |
| 6 | YELLOW | MOVE | graphic | & | 6 |
| 7 | WHITE | ERASE | graphic | ' | 7 |
| 8 | POINT | — | graphic (empty square) | ( | 8 |
| 9 | CAT | — | — | ) | 9 |
| 0 | BLACK | FORMAT | — | _ (bar) | Ø |
| BREAK | — | — | — | — | BREAK |
| DELETE | — | — | — | — | DELETE |
| GRAPH | — | — | — | — | GRAPH |
| Q | SIN | ASN | PLOT | <= | Q |
| W | COS | ACS | DRAW | <> | W |
| E | TAN | ATN | REM | >= | E |
| R | INT | VERIFY | RUN | < | R |
| T | RND | MERGE | RAND | > | T |
| Y | STR$ | [ | RETURN | AND | Y |
| U | CHR$ | ] | IF | OR | U |
| I | CODE | IN | INPUT | AT | I |
| O | PEEK | OUT | POKE | — | O |
| P | TAB | (c) | PRINT | — | P |
| EXTEND MODE | — | — | — | — | EXTEND / MODE |
| EDIT | — | — | — | — | EDIT |
| A | READ | ~ | NEW | STOP | A |
| S | RESTR | \| | SAVE | NOT | S |
| D | DATA | \ | DIM | STEP | D |
| F | SGN | { | FOR | TO | F |
| G | ABS | } | GOTO | THEN | G |
| H | SQR | CIRCLE | GOSUB | ↑ | H |
| J | VAL | VAL$ | LOAD | — (minus) | J |
| K | LEN | SCRN$ | LIST | + | K |
| L | USR | ATTR | LET | = | L |
| ENTER | — | — | — | — | ENTER (on the foot) |
| CAPS SHIFT ×2 | — | — | — | — | CAPS SHIFT |
| CAPS LOCK | — | — | — | — | CAPS / LOCK |
| Z | LN | BEEP | COPY | : | Z |
| X | EXP | INK | CLEAR | £ | X |
| C | LPRINT | PAPER | CONT | ? | C |
| V | LLIST | FLASH | CLS | / | V |
| B | BIN | BRIGHT | BORDER | * | B |
| N | INKEY$ | OVER | NEXT | — | N |
| M | PI | INVERS | PAUSE | — | M |
| `.` | — | — | — | — | . |
| SYMBOL SHIFT ×2 | — | — | — | — | SYMBOL / SHIFT |
| `;` `"` `,` | — | — | — | — | ; / " / , |
| cursors | — | — | — | — | hollow outline arrows ⇦ ⇨ ⇧ ⇩ |
| SPACE | — | — | — | — | (blank) |

The `;` `"` `,` `.` symbols are on separate keys, so O, P, N and M carry no symbol. The mosaic glyphs are drawn as
an outlined square with the ink quadrants filled white. Key 8 is an empty outlined square (the polarity is the reverse
of the 48K's). Glyph details are UNVERIFIED per key.

---

## 3. ZX Spectrum 128 (1985 Spain / 1986 UK, "toastrack")

### 3.1 Case

| Fact | Value | Source |
|---|---|---|
| Body | The Spectrum+ case and the same 58-key keyboard. Dimensions given as 320 × 150 × 45 mm (Centre for Computing History; it is unclear whether this includes the heatsink) | CCH via search; photos |
| Heatsink | A large external **finned aluminium heatsink** for the 7805 regulator on the **right-hand end**: black-anodised vertical fins (≈20 fins, running front to back) over the full depth and height. ≈24 mm wide (measured: about 0.077 × the body width) | Wikipedia ("large external heatsink ... right-hand end ... The Toast Rack"); `128k-top.jpg`, `128k-spanish-k1-top.jpg`, `128k-heatsink-closeup.jpg` |
| Overall width with the heatsink | ≈343 mm (319 + 24, measured, UNVERIFIED) | measured |
| "sinclair" logo (UK) | **Red-painted** logotype, top-left (the + has it moulded black) | `128k-top.jpg`, `128k-view2.jpg` |
| "ZX Spectrum +" (UK) | Red, small, under the logo | `128k-top.jpg` |
| Badge (UK) | **"128K"** in red outline/thin sans, immediately left of the 4-stripe rainbow badge, bottom-right corner | `128k-top.jpg`, `128k-view2.jpg` |
| Spanish (Investrónica) | Same case, "sinclair" moulded black (as on the +), "ZX Spectrum +" red. The **"128K" is white**, beside the rainbow badge | `128k-spanish-k1-top.jpg`, `128k-spanish-k1-keyboard.jpg` |
| Case plastic | Black/charcoal as the +. Est. `#262829` (sample `#2d3639`) | photos |

### 3.2 Keyboard

- UK 128: identical to the Spectrum+ keyboard (§2.2–2.4); `128k-k1-keyboard.jpg` is the UK scan.
- Spanish 128 (`128k-spanish-k1-keyboard.jpg`, the same geometry and matrix), legends changed:

| Key | Spanish legend |
|---|---|
| TRUE VIDEO | "VIDEO" in black on a **white** keycap insert (the inverted look) |
| INV VIDEO | "VIDEO" in white inside a white outline box |
| BREAK | PARAR |
| DELETE | BORRAR |
| GRAPH | GRAF. |
| EXTEND MODE | MODO / EXTENDIDO |
| EDIT | EDITAR |
| CAPS SHIFT ×2 | hollow ⇧ arrow icon |
| CAPS LOCK | hollow ⇳ (up-down arrow) icon |
| ENTER | large hollow ↵ arrow icon |
| SYMBOL SHIFT ×2 | CAMBIO |
| colour names | AZUL, ROJO, MORADO, VERDE, CELESTE, AMRLLO, BLNCO, NEGRO (keys 1..7, 0) |
| number-row symbols | 1 `!`, 2 `¡`, 3 `$`?, 4 `Ñ`, 5 `ñ`, 6 `` ` ``, 7 `´`, 8 `(`, 9 `)`, 0 `_` (read from the scan; 3 is UNVERIFIED) |
| Y / U line 2 | `@` / `#` (UK `[` / `]`) |
| A, S, D, F, G line 2 | `~`, `%`, `&`, `ü`, `ç` (UK `~ \| \ { }`) |
| P line 2 | (c) |

The Spanish characters come from the Investrónica ROM's character set; which code each produces is **UNVERIFIED** (the
matrix position is unchanged). The external Spanish keypad is out of scope.

---

## 4. Amstrad ZX Spectrum +2 (1986, grey)

### 4.1 Case

| Fact | Value | Source |
|---|---|---|
| Depth on the top | 171 mm from the notched rear edge to the front (the key block's top 63.5 mm from the rear, its left 18 mm; the Datacorder lid x 300-421, y 41-109; the buttons x 308-412, y 137-157), measured on `plus2-owner-top.png` at 3.42 px/mm | owner photo |
| Dimensions | 440 × 175 × 55 mm, 1.525 kg (one spec sheet; the width fits the photo ratio: keyboard 13.5 u ≈257 mm = 58% of the case width) | spec sheet via search (UNVERIFIED primary); measured on `plus2-k1-top.jpg` |
| Case plastic | **Mid grey**, matte, neutral. `#6A6B69` (median over the case on the neutral-light owner photo; keys a little darker, `#56585A`; the earlier `#5E5F5E` came from the yellow-lit, front-shot `plus2-top.jpg` and is wrong) | `plus2-owner-top.png`, `plus2-owner-top2.png` (owner, 2026-10-19) |
| Form | Long flat slab. **Castellated (toothed) rear and front edges** (small rectangular ribs along the top edge). A raised rear strip with a horizontal groove line under the logos. Horizontal grooves across the margins left and right of the keyboard at the row boundaries | `plus2-top.jpg`, `plus2-view3.jpg` |
| "sinclair" | **Red-painted** logotype, rear strip left (≈20–100 mm from the left) | `plus2-top.jpg` |
| "128K" | Red bold italic, rear strip centre-right (≈270–300 mm), with a small red LED (power) just right of it | `plus2-top.jpg`, `plus2a-k1-datacorder.jpg` |
| "ZX Spectrum +2" | White bold italic, rear strip right, above the Datacorder | `plus2-top.jpg` |
| Keyboard block | Left part: ≈23 mm from the left edge to ≈280 mm; recessed in a frame | measured on `plus2-k1-top.jpg` |
| Datacorder | Built-in cassette deck, right part (≈300–417 mm): a smoked-clear window over the cassette well, under it a black band with **"DATACORDER"** in bold italic, its letters filled with **horizontal rainbow bands** (red, orange, yellow, green, cyan, blue from top to bottom), a small black hole (the microphone) under it. Below: **6 piano-key buttons** in a row (record, play, rewind, fast forward, stop/eject, pause). On the grey +2 the symbols are **on the buttons** (record = red dot ●, ▶, ◀◀, ▶▶, ⏏/▲, ‖) | `plus2-top.jpg`, `plus2-view3.jpg`; the +2 vs +2A button symbols from the k1.spdns.de fan site text |

### 4.2 Keyboard (+2, +2A, +3)

- Geometry and matrix: identical to §2.3 (the same 58-key layout, ENTER inverted L, the same widths), from the
  ZEsarUX `keyboard_p2.bmp`, which measures to exactly the layout of §2.3.
- Keycaps: Amstrad **sculpted, spring-loaded typewriter-style caps** (Wikipedia: "grey case with a spring-loaded
  keyboard"). Tapered sides with a top face ≈70% of the pitch and a slightly dished (cylindrical) top. Gap at the base ≈1.5 mm
  (UNVERIFIED). +2 caps: dark grey, est. `#3E4041` (samples `#3b3d3e`), on the lighter grey case.
- Legends: white `#F4F4F4`, **bold italic** grotesque (Helvetica Bold Oblique-like), printed on the key top. The main
  character is at the **lower left**, the symbol (SYMBOL SHIFT) small **above it**. Only three keys carry a keyword
  (Wikipedia: "keys lack keyword markings except LOAD, CODE and RUN").

| Key | Legends (white italic) |
|---|---|
| 1..9 | symbol above digit: `!` 1, `@` 2, `#` 3, `$` 4, `%` 5, `&` 6, `'` 7, `(` 8, `)` 9 |
| 0 | `_` above `Ø` |
| Q W E Y U O P A S D F G N M | main letter only |
| R | `<` above R, "RUN" small to the right |
| T | `>` above T |
| I | "CODE" small top-right, I |
| H | `↑` above H |
| J | `-` above J, "LOAD" small to the right |
| K | `+` above K |
| L | `=` above L |
| Z / X / C / V / B | `:` / `£` / `?` / `/` / `*` above the letter |
| TRUE VIDEO, INV VIDEO | 2 lines small: TRUE / VIDEO, INV / VIDEO |
| BREAK, DELETE, GRAPH, EDIT, EXTEND MODE | single line, small, lower left |
| CAPS LOCK | CAPS / LOCK |
| CAPS SHIFT ×2 | CAPS SHIFT (larger italic) |
| SYMB SHIFT ×2 | SYMB / SHIFT |
| ENTER | "ENTER" plus a ┘ return-hook line, on the foot |
| `.` `;` `"` `,` | the glyph |
| cursor keys | chevrons `<` `>` (left of SPACE), `^` `v` (right of SPACE); not hollow arrows |
| SPACE | blank |

---

## 5. Amstrad ZX Spectrum +2A (1987, black; +2B is similar)

| Fact | Value | Source |
|---|---|---|
| Case | The +2 mould in **black** (the +3's colour). Est. `#2A2C2E` | CCH ("housed in a black case"); `plus2a-k1-view.jpg` |
| Dimensions | As the +2/+3 (≈440 × 175 × 50–55 mm). A "34.0 × 17.4 × 5.0 cm" figure (k1.spdns.de) conflicts with the photo ratio → UNVERIFIED | photos |
| Badges | "sinclair" red-painted raised logotype top-left (`plus2a-k1-logo.jpg`), "128K" red bold italic, the red power LED, **"ZX Spectrum +2"** (not "+2A") white bold italic | CPCWiki (via search: "case reads ZX Spectrum +2"), `plus2a-k1-datacorder.jpg` |
| Datacorder | As the +2 (window, rainbow-filled "DATACORDER" band, microphone hole). The 6 button symbols are printed **on the case above the buttons** (● red, ▶, ◀◀, ▶▶, ▲, ‖); the buttons are plain black | `plus2a-k1-datacorder.jpg`; k1.spdns.de fan-site text |
| Keyboard | §2.3 geometry, §4.2 legends; keycaps **black/very dark grey** est. `#1E2022`, white italic legends | `plus2a-k1-view.jpg`, `plus2a-keyboard-closeup.jpg` |
| Anomaly | `plus2a-k1-datacorder.jpg` shows "DELETE" right of `Ø` (row 0) where BREAK belongs; probably a swapped cap. `plus2a-k1-view.jpg` and every other photo have BREAK there | photos |

---

## 6. Amstrad ZX Spectrum +3 (1987, black, 3" disc)

| Fact | Value | Source |
|---|---|---|
| Case | The +2A mould, black, est. `#2A2C2E` (samples `#2d3048`–`#404446`); the same castellated edges and grooves | `plus3-top.jpg`, `plus3-view4.jpg` |
| Dimensions | 440 × 174 × 50 mm, 1.6 kg (RigPix); a conflicting "34.0 × 17.4 × 5.0 cm" (k1) → the width is UNVERIFIED, 440 fits the photo ratio | search results |
| Badges | "sinclair" red top-left; "128K" red bold italic centre; **"ZX Spectrum+3"** white bold italic top-right, above the drive panel | `plus3-k1-floppy.jpg`, `plus3-top.jpg` |
| Disc drive | At the **right**, where the Datacorder sits on the +2. Top: a flat label panel (≈115 × 70 mm, UNVERIFIED) with **"128K"** in large thin-outline red/orange letters, under it a thin multi-colour horizontal stripe band (red→orange→yellow→green→cyan→blue), over which "FDD" sits in large dark outline letters, and "FLOPPY DISC DRIVE" small white caps under it. Front face: the drive bezel, a black rectangle recessed into the case front, with the 3" slot (upper), a shutter flap, a **red LED** bottom-left and the **eject button** bottom-right | `plus3-k1-floppy.jpg` |
| Keyboard | §2.3 geometry, §4.2 legends, black caps `#1E2022` (sample `#242e37`), white italic legends | `plus3-zesarux-osd.png`, `plus3-top.jpg` |
| Spanish +3 | Same with translated special keys (e.g. MAYUSCULAS for CAPS SHIFT; seen in `plus3-spanish-view.jpg`) | out of scope |

---

## 7. Shared-keyboard summary

| Machine | Keyboard data | Case picture |
|---|---|---|
| 48K | its own (40 rubber keys, §1) | its own |
| + | 58-key "+" (§2) | + case |
| 128 UK | = + (§2) | + case + red logo + "128K" badge + heatsink |
| 128 Spanish | + geometry, Spanish legends (§3.2) | as 128, white "128K" |
| +2 | 58-key Amstrad (§2.3 + §4.2), grey caps | +2 grey with Datacorder |
| +2A | = +2 legends, black caps | +2 mould black, button symbols on the case |
| +3 | = +2A keyboard | black, FDD panel instead of the Datacorder |

---

## UNVERIFIED (summary)

1. Exact mm pitch: 48K ≈20.2 × 19.5 mm (measured), 58-key ≈19 mm (18.6–19.7 measured). No drawing found.
2. Exact keycap face sizes and corner radii for all keyboards (from photos only).
3. +2/+2A/+3 case dimensions: sources conflict (440 × 175 × 55 vs 340 × 174 × 50 mm). 440 fits the photos.
4. 128K overall width with the heatsink, and the fin count (≈20).
5. All hex colours (photo estimates). The 48K stripe and colour-word colours are close to the photos but not
   colour-calibrated.
6. Fonts: no source names the typefaces (48K: Helvetica-like upright; +: bold upright grotesque; Amstrad: bold
   italic). The Sinclair logotype must be redrawn from photos.
7. Per-key mosaic glyph details (quadrant polarity on the +).
8. Spanish 128 key 3 symbol and the character codes of the Spanish legends.
9. Dual-contact construction of the +2/+2A/+3 membranes (logically certain, no schematic read).
10. Positions of the badges and logos in mm (±3–5 mm; from perspective photos except `48k-k1-top2.jpg`,
    `plus2-k1-top.jpg`, `128k-spanish-k1-top.jpg`, which are near top-down).
11. The Spectrum+ reset button position (left side).

---

## Sources

Matrix and key combinations

- ZX Spectrum BASIC manual, ch. 23 (half-row ports, D0 = outer key): https://worldofspectrum.org/ZXBasicManual/zxmanchap23.html
- TBBlue firmware KeyboardTester (extra-key → matrix pairs, bit masks):
  `zx-spectrum-data/zx-next/firmware/tbblue-firmware-sources/asm/KeyboardTester/KeyboardTester.asm`
  lines 382–399, masks 190–229
- +3 manual ch. 8 pt. 1 (key behaviour, EXTEND MODE / GRAPH / SYMB SHIFT): https://worldofspectrum.org/ZXSpectrum128+3Manual/chapter8pt1.html
- Crash #12 Spectrum+ keyboard review (double-shot caps, extra membrane layers): https://crashonline.org.uk/12/keyboard.htm
- Dual contacts on the + membrane (forum, secondary): https://sinclairzxworld.com/viewtopic.php?p=45225

Dimensions and case facts

- Wikipedia ZX Spectrum (48K 233 × 144 × 30, + 319 × 149 × 38, key colours, the 128 heatsink, the +2 spring keyboard and LOAD/CODE/RUN, the +2A black case, Dickinson): https://en.wikipedia.org/wiki/ZX_Spectrum
- Sinclair 1982 spec (233/144/30): https://rk.nvg.ntnu.no/sinclair/computers/zxspectrum/spec_technical.htm
- Retro Isle (+ 319 × 149 × 38): https://retroisle.com/sinclair/zxspectrum/general.php
- Centre for Computing History, 128 (320 × 150 × 45) and +2A (black case): https://www.computinghistory.org.uk/det/2584/sinclair-zx-spectrum-128 , https://computinghistory.org.uk/det/32520/Sinclair-ZX-Spectrum-2A
- RigPix +3 (440 × 50 × 174 mm, 1.6 kg): https://rigpix.com/vcomp/sinclair_spectrumplus3.htm
- k1.spdns.de archive (images, +2A/+2B notes, spec tables): https://k1.spdns.de/Vintage/Sinclair/

Photos and scans (saved under `photos/sinclair/`)

- Wikimedia Commons: `48k-top.jpg` (File:ZXSpectrum48k.jpg, Bill Bertram, CC BY-SA 2.5), `48k-keyboard-drawing.png`
  (File:ZX Spectrum 48K rubber keyboard.png), `48k-membrane-matrix.png`, `48k-rubber-mat.jpg` (File:ZX Spectrum showing
  rubber key mat.jpg), `48k-keyboard-closeup.jpg`, `48k-bottom.jpg`, `plus-top.jpg` (File:Sinclair-zx-spectrum-plus.jpg),
  `plus-view3.jpg`, `128k-top.jpg` (File:ZX Spectrum128K.jpg), `128k-view2.jpg`, `128k-view3.jpg`,
  `128k-heatsink-closeup.jpg`, `plus2-top.jpg` (File:ZX Spectrum Plus2.jpeg), `plus2-view-retouched.jpg`, `plus2-view3.jpg`,
  `plus2a-keyboard-closeup.jpg`, `plus3-top.jpg` (File:ZX Spectrum Plus3.jpeg), `plus3-view4.jpg`, `plus3-spanish-view.jpg`
- k1.spdns.de: `48k-k1-top.jpg`, `48k-k1-top2.jpg` (the best near top-down 48K, used for the measurements),
  `48k-k1-keyboard.jpg`, `48k-k1-logo.jpg`, `48k-issue1-grey-keys.jpg`, `plus-k1-top.jpg`, `128k-k1-keyboard.jpg` (flat
  scan of the +/128 keyboard, the main geometry source), `128k-k1-view.jpg`, `128k-spanish-k1-top.jpg`,
  `128k-spanish-k1-keyboard.jpg`, `128k-spanish-k1-keypad.jpg`, `plus2-k1-top.jpg`, `plus2-k1-keys.jpg`,
  `plus2a-k1-view.jpg`, `plus2a-k1-logo.jpg`, `plus2a-k1-datacorder.jpg`, `plus3-k1-view.jpg`, `plus3-k1-view2.jpg`,
  `plus3-k1-floppy.jpg`
- ZEsarUX on-screen keyboard bitmaps (local; converted to PNG): `48k-zesarux-osd.png`, `plus-zesarux-osd.png`,
  `128k-zesarux-osd.png`, `128k-spanish-zesarux-osd.png`, `plus2-zesarux-osd.png`, `plus3-zesarux-osd.png`, from
  `zx-spectrum-data/zx-next/emulators/zesarux/src/keyboards/keyboard_{48,48p,128,128s,p2,p3}.bmp`
  (a drawn emulator layout: used to cross-check the geometry, not as proof)
