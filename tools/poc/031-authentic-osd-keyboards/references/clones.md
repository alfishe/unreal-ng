# POC 031 references: ex-USSR clone keyboards

Scope: Pentagon 128, ZS Scorpion ZS-256 (plus PROF ROM and GMX), Nemo KAY-1024, Quorum 128, Orel BK-08 (LSY256),
Profi v3 / v5. Photos are in `photos/clones/` (paths below are relative to this file's directory).

Conventions:
- Matrix positions are written `A<n>/D<b>`. A8..A15 are the half-row address lines (port #FE high byte with a zero
  bit) and D0..D4 are the data bits. Combination keys are written `CS+x` (CAPS SHIFT) or `SS+x` (SYMBOL SHIFT).
- `u` is one key pitch. Unless noted, x is measured in u from the left edge of the key field to the key's left edge.
- Colours are hex estimates taken from photos of aged hardware under uneven light. Treat them as starting values,
  not measurements.
- **UNVERIFIED** marks anything not confirmed by a schematic, a manual or a clear photo.

## 0. Shared data: the ZX 8x5 matrix and the 40-key legend set

Several clones reuse the standard Sinclair 40-key matrix. This table is the reference for every section below.

| Half-row | D0 | D1 | D2 | D3 | D4 |
|---|---|---|---|---|---|
| A8 (#FEFE) | CAPS SHIFT | Z | X | C | V |
| A9 (#FDFE) | A | S | D | F | G |
| A10 (#FBFE) | Q | W | E | R | T |
| A11 (#F7FE) | 1 | 2 | 3 | 4 | 5 |
| A12 (#EFFE) | 0 | 9 | 8 | 7 | 6 |
| A13 (#DFFE) | P | O | I | U | Y |
| A14 (#BFFE) | ENTER | L | K | J | H |
| A15 (#7FFE) | SPACE | SYMBOL SHIFT | M | N | B |

Source: the 56-key keyboard schematic `photos/clones/pentagon-kbd56-schematic.png` (XP1 lines D0..D4 against
A8..A15), which agrees with the Profi 5.x sheet 9 (`zx-spectrum-data/profi/materials/v5-open-items/PROF5-0D.utf8.txt`).

The 48K legend set, per key (it is printed on the Soviet stickers and film keyboards):

| Key | K keyword | SS symbol/word | E-mode (above) | E+SS (below) | Digit-row extras |
|---|---|---|---|---|---|
| 1 | - | ! | EDIT | DEF FN | graphic block, BLUE |
| 2 | - | @ | CAPS LOCK | FN | block, RED |
| 3 | - | # | TRUE VIDEO | LINE | block, MAGENTA |
| 4 | - | $ | INV. VIDEO | OPEN # | block, GREEN |
| 5 | - | % | ← | CLOSE # | block, CYAN |
| 6 | - | & | ↓ | MOVE | block, YELLOW |
| 7 | - | ' | ↑ | ERASE | block, WHITE |
| 8 | - | ( | → | POINT | block, (none) |
| 9 | - | ) | GRAPHICS | CAT | - |
| 0 | - | _ | DELETE | FORMAT | BLACK |
| Q | PLOT | <= | SIN | ASN | |
| W | DRAW | <> | COS | ACS | |
| E | REM | >= | TAN | ATN | |
| R | RUN | < | INT | VERIFY | |
| T | RANDOMIZE | > | RND | MERGE | |
| Y | RETURN | AND | STR$ | [ | |
| U | IF | OR | CHR$ | ] | |
| I | INPUT | AT | CODE | IN | |
| O | POKE | ; | PEEK | OUT | |
| P | PRINT | " | TAB | © | |
| A | NEW | STOP | READ | ~ | |
| S | SAVE | NOT | RESTORE | \| | |
| D | DIM | STEP | DATA | \ | |
| F | FOR | TO | SGN | { | |
| G | GO TO | THEN | ABS | } | |
| H | GO SUB | ↑ | SQR | CIRCLE | |
| J | LOAD | - | VAL | VAL$ | |
| K | LIST | + | LEN | SCREEN$ | |
| L | LET | = | USR | ATTR | |
| ENTER | - | - | - | - | |
| CAPS SHIFT | - | - | - | - | |
| Z | COPY | : | LN | BEEP | |
| X | CLEAR | £ | EXP | INK | |
| C | CONTINUE | ? | LPRINT | PAPER | |
| V | CLS | / | LLIST | FLASH | |
| B | BORDER | * | BIN | BRIGHT | |
| N | NEXT | , | INKEY$ | OVER | |
| M | PAUSE | . | PI | INVERSE | |
| SYMBOL SHIFT | - | - | - | - | |
| SPACE | (BREAK) | - | - | - | |

On the 8 key the colour slot is empty: the 48K prints no colour name there. The 9 key carries no block.

## 1. Pentagon 128

### 1.1 What "the Pentagon keyboard" was

The Pentagon 128 was sold as a bare board ("Пентагон 128 это плата"). It came with no standard case or
keyboard, so each builder fitted their own. Three kinds turn up in photos:

- **A. The Soviet 40-key mechanical keyboard with stickers.** This is the owner's choice and the early-1990s
  default. Example: `pentagon-habr446366-kbd.jpg`.
- **B. A 40-key keyboard built into a homemade box**: `pentagon-interface1-pentfront.jpg`,
  `pentagon-interface1-pentside.jpg` and `pentagon-interface1-pentagon1.jpg`.
- **C. The 58-key Soviet film ("extended Spectrum") keyboard**: `pentagon-zxbyte-04.jpg` and
  `pentagon-zxbyte-17.jpg`. Section 1.4 describes it; Scorpion and Profi used it too.

Late Pentagons often ran an AT/XT keyboard through a Scorpion-style controller. The zxbyte author did this, and
the proboterror Pentagon interface board has PS/2 support. Even so, a native ZX keyboard was the norm for the
early-1990s Pentagon.

Sources: forum ruecm / alemorf, "Пентагон 128 это плата". https://habr.com/ru/articles/446366/ (the
sticker keyboard). http://www.interface1.net/ (the pentfront and pentside photos). https://zxbyte.org/ (Prusak's
Pentagon with the 58-key keyboard and the AT controller).

### 1.2 Variant A: 40-key mechanical keyboard with stickers (recommended for the Pentagon SVG)

**Housing**
- No case. The switches sit on a bare orange/yellow textolite plate: base `#d9a441`, edge shading `#b8862e`.
- No logo.
- Plate size: about 10.6u x 4.6u, i.e. about 200 x 88 mm at a 19 mm pitch. **UNVERIFIED**, estimated from the
  photo proportions.

**Geometry**
- Four rows of 10 keys on a square grid with **no row stagger**.
- Every key, including CAPS SHIFT, SYMBOL SHIFT, ENTER and SPACE, is 1u x 1u.
- Key x positions are 0..9 u in every row.
- Pitch: 19 mm is assumed for МП / ПКН switches. **UNVERIFIED**; no measurement was found.

**Keycaps**
- Black square low caps `#1a1a1a`, apparently with a clear protective cover over the sticker. The cover is
  **UNVERIFIED**.
- Each letter or digit key carries a paper sticker with three zones:
  - Top band, light grey `#c8c8c4`: the E-mode word in black, small sans-serif caps. For example SIN on Q, and
    EDIT, C.LOCK, TR.VIDEO, INV.VIDEO, ←, ↓, ↑, →, GRAPHICS, DELETE on the digits.
  - Middle zone, dark `#2a2a2a`, with a yellow disc `#f2c81e`:
    - the main letter in black bold inside the disc;
    - a white SS symbol (for example "<=");
    - the K keyword in red `#d23a2a`, for example PLOT.

    Note: this colour assignment is as observed, and it differs from the 48K (white keyword, red symbol).
    **UNVERIFIED** whether all sticker sets do this.
  - Bottom band, light grey: the E+SS word in black (ASN, ACS, ATN, VERIFY, MERGE, [, ], IN, OUT, ©, and so on).
- Digit keys show a white block-graphic square plus a red SS symbol.
- CAPS SHIFT, SYMBOL SHIFT and BREAK SPACE carry plain yellow `#f2c81e` stickers with black text. ENTER carries a
  red sticker `#c8392b` with white or black text (the text colour is **UNVERIFIED**).

**Mapping**
Pure ZX matrix (section 0).

| Row | x0 | x1 | x2 | x3 | x4 | x5 | x6 | x7 | x8 | x9 |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 1 A11/D0 | 2 A11/D1 | 3 A11/D2 | 4 A11/D3 | 5 A11/D4 | 6 A12/D4 | 7 A12/D3 | 8 A12/D2 | 9 A12/D1 | 0 A12/D0 |
| 1 | Q A10/D0 | W A10/D1 | E A10/D2 | R A10/D3 | T A10/D4 | Y A13/D4 | U A13/D3 | I A13/D2 | O A13/D1 | P A13/D0 |
| 2 | A A9/D0 | S A9/D1 | D A9/D2 | F A9/D3 | G A9/D4 | H A14/D4 | J A14/D3 | K A14/D2 | L A14/D1 | ENTER A14/D0 |
| 3 | CAPS SHIFT A8/D0 | Z A8/D1 | X A8/D2 | C A8/D3 | V A8/D4 | B A15/D4 | N A15/D3 | M A15/D2 | SYMBOL SHIFT A15/D1 | BREAK SPACE A15/D0 |

Sources: `photos/clones/pentagon-habr446366-kbd.jpg` (https://habr.com/ru/articles/446366/) and the schematic in
section 0.

**Soviet mechanical switches (МП / ПКН / reed).** There is no photographic proof of switch brands on a Pentagon
specifically: **UNVERIFIED**. The zx-pk thread 15581 shows a homemade reed (герконовая) keyboard with ЕС-1840 caps
(`profi-v32-homemade-reed-kbd-es1840-caps-zxpk26525.jpg`). It is good reference material for the "homemade
reed + salvaged caps" look:
- cream caps `#e6dcc0` and olive/khaki caps `#7d7a5a`;
- a green or brown PCB;
- sculpted cylindrical ЕС-1840 profile caps.

### 1.3 Variant B: 40-key keyboard in a homemade box (interface1.net)

- A grey sheet-metal box case `#9a9c9a` with vent slots.
- 40 cream caps `#e8e0c8` with stickers in staggered rows (Sinclair-like stagger: 0 / 0.5 / 0.75 / 0.25 u). The
  stagger values are **UNVERIFIED**; the photo is low resolution.
- CAPS SHIFT, SYMBOL SHIFT and BREAK SPACE are reddish `#b84a3a`.
- No readable logo.
- Mapping as in section 0.

Sources: `photos/clones/pentagon-interface1-pentfront.jpg`, `pentagon-interface1-pentside.jpg` and
`pentagon-interface1-pentagon1.jpg` (http://www.interface1.net/).

### 1.4 Variant C: the Soviet 58-key film keyboard (also Scorpion and Profi)

**Housing**
- One-piece dark grey plastic case `#3d3f3c`.
- Upper left: an embossed (not printed) logo, the same colour as the case, on two lines: "ZX SPECTRUM" over
  "SINCLAIR". Four raised horizontal ribs run to the right of it.
- The key field sits in a rectangular recess. The plate under the keys is light grey `#a8aaa6`.
- A small square blank sits at the top right, beside BREAK.
- Size: about 18.3u x 8.1u overall. At a 19 mm pitch that is about 348 x 155 mm. **UNVERIFIED**: estimated
  from the photo (key 1 to key 0 = 9 pitches = 920 px).
- The factory ("Копир") is a single-source claim: **UNVERIFIED**.

**Keycaps**
- Cylindrical-profile mechanical-feel caps over a film (membrane) contact sheet.
- Alphanumerics are cream `#ece4cc` (aged; when new, probably `#f4efe0`, **UNVERIFIED**).
- Modifiers and the space bar are mid grey `#8f908a`. BREAK is red `#c8392b`.
- Legends are black and printed (not stickers), in a condensed sans-serif. On the alphanumeric caps:
  - top left (small): the E+SS word;
  - top right (medium): the SS symbol;
  - centre left (large bold): the main character;
  - bottom right (small): the K keyword;
  - front (sloped) face: the E-mode word, or the colour name on digits (BLUE, RED, MGNTA, GREEN, CYAN, YELLOW,
    WHITE, BLACK).
- Digits 1-8 also carry the block graphic at their right.
- Modifiers are labelled in black caps: "TRUE VIDEO", "INV VIDEO", "DELETE", "GRAPH", "EXT MODE", "EDIT",
  "CAPS SHIFT", "CAPS LOCK", "SYMB SHIFT", "BREAK".
- The arrows are outline glyphs (⇦ ⇨ ⇧ ⇩).
- The Enter key shows a ↵ arrow. It is a tall key spanning rows 1-2: narrow (1.0u) in row 1 and wider (1.4u) in
  row 2.

**Geometry**
Measured from `pentagon-zxbyte-04.jpg` at 1u = 102 px. Widths and x values are ±0.1u.

| Row | Key | x (u) | w (u) | Mapping |
|---|---|---|---|---|
| 0 | TRUE VIDEO | 0.2 | 1.0 | CS+3 |
| 0 | INV VIDEO | 1.2 | 1.0 | CS+4 |
| 0 | 1 .. 0 | 2.2 .. 11.2 | 1.0 each | A11/A12 per section 0 |
| 0 | BREAK | 12.2 | 1.5 | CS+SPACE |
| 1 | DELETE | 0.1 | 1.5 | CS+0 |
| 1 | GRAPH | 1.6 | 1.0 | CS+9 |
| 1 | Q .. P | 2.6 .. 11.6 | 1.0 each | A10/A13 |
| 1 | ENTER (upper) | 12.6 | 1.0 | A14/D0 |
| 2 | EXT MODE | 0.0 | 1.5 | CS+SS |
| 2 | EDIT | 1.5 | 1.25 | CS+1 |
| 2 | A .. L | 2.75 .. 10.75 | 1.0 each | A9/A14 |
| 2 | ENTER (lower) | 11.75 | 1.4 | same key as row 1 |
| 3 | CAPS SHIFT | 0.0 | 2.25 | A8/D0 |
| 3 | CAPS LOCK | 2.25 | 1.0 | CS+2 |
| 3 | Z .. M | 3.25 .. 9.25 | 1.0 each | A8/A15 |
| 3 | . | 10.25 | 1.0 | SS+M |
| 3 | CAPS SHIFT | 11.25 | 2.25 | A8/D0 (parallel) |
| 4 | SYMB SHIFT | 0.0 | 1.0 | A15/D1 |
| 4 | ; | 1.0 | 1.0 | SS+O |
| 4 | " | 2.0 | 1.0 | SS+P |
| 4 | ← | 3.0 | 1.0 | CS+5 |
| 4 | → | 4.0 | 1.0 | CS+8 |
| 4 | SPACE | 5.1 | 4.75 | A15/D0 |
| 4 | ↑ | 9.9 | 1.0 | CS+7 |
| 4 | ↓ | 10.9 | 1.0 | CS+6 |
| 4 | , | 11.9 | 1.0 | SS+N |
| 4 | SYMB SHIFT | 12.9 | 1.0 | A15/D1 (parallel) |

The x values of row 4 are absolute in the photo. The rows shift left slightly toward the bottom because of the
camera perspective, and the field is really near-rectangular. When drawing, normalise all rows to start at x = 0.
That gives 58 keys (56 significant, since CS and SS are doubled). This is the Spectrum+ arrangement.

**Mapping of the combination keys.** These come from the second matrix (XP2) of
`photos/clones/pentagon-kbd56-schematic.png`:
- BREAK = CS+SPACE; EXTEND MODE = CS+SS; DELETE = CS+0; GRAPH = CS+9; EDIT = CS+1; CAPS LOCK = CS+2;
  TRUE VIDEO = CS+3; INV VIDEO = CS+4; ← = CS+5; ↓ = CS+6; ↑ = CS+7; → = CS+8;
- ";" = SS+O; "," = SS+N; "." = SS+M; '"' = SS+P.

Each combination key closes two contacts of the film at once (CS or SS plus the second key). Sources: zx-pk thread
19499 (the schematic attachment) and `pentagon-zxbyte-04.jpg` (https://zxbyte.org/).

## 2. ZS Scorpion ZS-256 (ZS-256, PROF ROM, GMX)

- Like the Pentagon, the Scorpion was sold mostly as a board. The firm also built units into IBM PC MiniTower
  cases; ru.wikipedia says the machines "не имеют собственного внешнего вида".
- Native keyboard: "стандартной 58-кнопочной клавиатуры", i.e. the Soviet 58-key film keyboard of section 1.4.
  The СБРОС (reset) and MAGIC (NMI) buttons are separate, on the board or case, not on the keyboard.
  - Use section 1.4 for the SVG.
  - Which case shell Scorpion owners used for that keyboard: **UNVERIFIED**. No Scorpion-branded keyboard photo
    was found.
- Port #FE: D0-D4 keyboard, D5 RS-232 input, D6 tape, D7 Centronics BUSY (`scorpion.doc`). The Centronics port
  #FFDD is documented as usable "для нестандартного расширения клавиатуры". No standard extended-key port exists.
- **PC keyboard: very common.** The "AT/XT keyboard + mouse controller" (Scorpion & Д.К., 1995-97) is a ZX-Bus
  card of 120 x 68 mm.
  - It emulates the 8x5 matrix from an XT/AT keyboard with QWERTY/ЙЦУКЕН layouts. Ctrl+Alt+Del gives a hardware
    reset.
  - Turbo+ configurations are listed with a "PC 101-key keyboard" (interface1.net).
  - ProfROM 3.0b (05.11.94): "Полностью изменена п/п обслуживания клавиатуры, добавлена поддержка
    мыши/джойстика".
- **PROF ROM and GMX: the same keyboard.** PROF ROM is a firmware variant of the same ZS-256 board. GMX is a
  memory/graphics expansion of the same board family, and it keeps the same #FE keyboard path. This is an
  inference from the board docs; no separate GMX keyboard exists in any source found. Treat it as **UNVERIFIED but
  likely**.

Sources: `zx-spectrum-data/scorpion/extracted/SCORPION/scorpion.doc` and `scorp.doc` (CP866),
https://ru.wikipedia.org/wiki/ZS_Scorpion, http://www.interface1.net/.

## 3. Nemo KAY-1024

- Made by Nemo, St Petersburg, 1998. It was supplied as a board and installed in various cases, notably cases of
  the Корвет (Korvet) computer (speccy.info).
- **PC keyboard: the norm.** The board family uses an IBM PC keyboard and mouse controller. Radiolyubitel 5/95
  published "Контроллер PC XT клавиатуры для KAY-256". The XT keyboard is translated into the ZX matrix, so the
  matrix mapping is section 0, plus combinations for the PC keys. That is essentially the same translation as the
  Profi XT map in section 6.4: **UNVERIFIED** for KAY specifically.
- Native 40/58-key connector on the KAY-1024: **UNVERIFIED** (not found in the sources checked).
- No KAY-branded keyboard exists.
- Recommendation for POC 031: draw the KAY as a PC/XT keyboard (shared asset), or fall back to the section 1.4
  58-key keyboard.
- The Корвет case look (if wanted) is **UNVERIFIED**: no photo of a KAY in a Корвет case was saved.

Sources: https://speccy.info/KAY-1024 (via the browser, Cloudflare blocks curl). Radiolyubitel 1995 No. 5
(cited by speccy.info).

## 4. Quorum 128 (Quorum 128+, the "Magic" keyboard family)

**Housing (Quorum 128+)**
- A one-piece cream/ivory plastic desktop case `#e4dcc8` with the keyboard built in. The top surface behind the
  keys is ribbed.
- Logo "КВОРУМ" at the top right: white letters on a black plate `#111111`, next to small red `#d02828`, green
  `#2a9a3a` and blue `#2848b0` squares.
- A small emblem at the top left (its content is **UNVERIFIED**, too small in the photo).
- Rear panel labels, from left to right (photo 2): "RGB", "МАГ ВЫХОД", "МАГ ВХОД", "SINCLAIR", "KEMPSTON",
  "MIDI", "ГРОМКОСТЬ", "TV", "ДИСКОВОД", "ПРИНТЕР", "РАСШИРЕНИЕ", "9В 1.5А, 17В 0.3А". The exact left-to-right
  order is **UNVERIFIED**.
- Case dimensions: **UNVERIFIED**.

**Keyboard construction**
- A film/membrane keyboard on ribbon cables (zx-pk 9796).
- Cream alphanumeric caps `#ece6d4` and grey function caps `#9a9a94`.
- Black printed legends: Latin, Cyrillic ЙЦУКЕН and BASIC keywords.

**Layout from photo 1 (approximate; per-key x and width are UNVERIFIED)**
- Top function row: F1..F5 (grey); [G] (graphics); NMI; INV; [speaker]; RES.
- Main block row 1: [E], 1..0, BS, DEL.
- Main block row 2: EDIT, Q..P.
- Row 3: CAPS LOCK, A..L.
- Row 4: CAPS SHIFT, Z..M.
- Row 5: RUS, LAT, FIRE, a long SPACE, SYMBOL SHIFT.
- To the right: an inverted-T arrow cluster, a large L-shaped ENTER and a numeric keypad (digits, ".", +, -, /,
  "*" style keys).

**Schematic: "KEYBOARD for QUORUM and MAGIC" (doc 190991, 20 September 1991, k2k@list.ru)**
88 switches, S1..S88. Each switch carries a hex code. The code's column group gives the data bit, and the low 3
bits give the address line: x7 = A8 ... x0 = A15 (rows A8..A15 top to bottom map to codes 7..0).

| Code range | Data line | Port | Contents |
|---|---|---|---|
| 20-27h | D0 | #FE | ZX column D0 (CS, A, Q, 1, 0, P, ENTER, SPACE) |
| 18-1Fh | D1 | #FE | ZX column D1 |
| 10-17h | D2 | #FE | ZX column D2 |
| 08-0Fh | D3 | #FE | ZX column D3 |
| 00-07h | D4 | #FE | ZX column D4 |
| 28-2Fh | extra | #7E | [>], 6 (keypad), 9 (keypad), -, [G], [{], 3 (keypad), + |
| 30-37h | extra | #7E | 2 (keypad), [.], 5 (keypad), 8 (keypad), F8, [}], [/], "." |
| 38-3Fh | extra | #7E | 1 (keypad), 4 (keypad), 7 (keypad), /, DEL, ..., 0 (keypad) |
| 40-47h | extra | #7E | [~], BS, [±], [\] (others **UNVERIFIED**) |
| 48-4Fh | extra | #7E | LAT, F2, F4, F5, -, F3, -, F1 |
| 50-57h | extra | #7E | RUS, CAPS LOCK, TAB, [E], 3 graphic keys |

- Port #7E: decoded on A7 = 0 and A0 = 0 with A4 = A3 = 1. Reading it returns D0..D5 = the six extra columns,
  scanned by the same A8..A15 lines. On Quorum, port #FE D5 always reads 1.
- **Which code group maps to which #7E bit is UNVERIFIED.** The connector X2 lists D0.. but the group-to-pin
  order was not legible. Also **UNVERIFIED**: the order of keys inside a group, which is copied in schematic
  order.
- Joystick: the arrow switches are wired in parallel with 5/6/7/8 (Sinclair-style cursor joystick), and FIRE is
  in parallel with 0 (S5/S6). The arrows therefore map to the matrix keys 5 (←), 6 (↓), 7 (↑), 8 (→) without CS.
  FIRE is 0 (A12/D0).
- Connector X1.2 carries NMI, INV, the speaker key (S87) and RES as separate lines. These are not matrix keys.
- zx-pk 13416 calls this keyboard a special version of the МС7007 keyboard that needs the extra 6-bit port.
- **PC keyboard**: uncommon. The Quorum had its own built-in 88-key keyboard. **UNVERIFIED** whether a PC
  keyboard adapter existed.

Sources: `photos/clones/quorum-kbd-extended-schematic.jpg` (the 1991 schematic);
https://github.com/UncleRus/quorum-reborn (resources/Doc/ports.md, the retropc.org photos);
`quorum128plus-photo-1.jpg` (case and keyboard), `quorum128plus-photo-2.jpg` (rear panel),
`quorum128plus-photo-7.jpg` (inside); zx-pk threads 9796 and 13416.

## 5. Orel BK-08 (LSY256)

**Housing**
- Made by Днепровский машиностроительный завод, Dnepropetrovsk; developed before 1990.
- Two pieces in a light warm grey plastic `#cfc9bc`:
  - The main unit sits behind, with a label at the left carrying the logo "ОРЕЛЬ БК-08". The exact label artwork
    is in `orel-bk08-chm-3.jpg`. It also has red NMI and RESET buttons `#c83a2e`, an LED and vent slots.
  - The keyboard unit sits in front, with ribbed sides.
- Dimensions: **UNVERIFIED**.

**Keyboard**
- Reed switches (герконовая), 67 keys.
- The alphanumeric zone is laid out like an AT keyboard: QWERTY with ЙЦУКЕН, switched by РУС / Р/А.
- 56 keys are visible to programs. The rest emulate CS+digit combinations or are read through unused bits of the
  keyboard ports. Which unused bits these are is **UNVERIFIED**; that needs the schematic `Orel_BK08_Schematic.djvu`,
  which was not decoded.
- Caps:
  - white/cream alphanumerics `#ece8dc`;
  - dark blue-grey modifiers `#4a5260`;
  - a white space bar;
  - an L-shaped ENTER;
  - an arrow cluster at the bottom right.

**Layout and legends (manual Рис. 5, "НАЗНАЧЕНИЕ КЛАВИШ КЛАВИАТУРЫ БК")**
- File: `photos/clones/orel-bk08-manual-fig5-key-legends.png`.
- Each key has zones:
  - COMMAND: the keyword, top left;
  - L: the Latin character, large, centre left;
  - CTRL: top right;
  - S: the SHIFT zone, bottom left;
  - ALT: bottom middle;
  - P: the Russian character, large, bottom right.
- Printed abbreviations: RAND = RANDOMIZE, IN K$ = INKEY$, SCR$ = SCREEN$, ATR = ATTR, CONT = CONTINUE,
  BRIG = BRIGHT, INV = INVERSE, RESTOR = RESTORE, FORM = FORMAT.
- A row of 8 block-graphic glyphs is printed above the digits on the case.

| Row | Keys left to right (legends: COMMAND / L / P / others) |
|---|---|
| 0 | `~` key (VERIFY, USR); 1 (ABS, !); 2 (EXP, @); 3 (LN, #); 4 (SGN, $); 5 (SQR, %); 6 (MOVE, ^); 7 (ERASE, &); 8 (POINT, *); 9 (CAT, ( ); 0 (FORM, ) ); `-` (MERGE, TAB, _); `=` (DEF FN, FN, +); BS / ВШ; GR / ГРАФ |
| 1 | TAB / © / ТАБ (1.5u); Q (PLOT, <=) Й; W (DRAW, <>) Ц; E (REM, >=) У; R (RUN, INT) К; T (RAND, RND) Е; Y (RETURN, AND) Н; U (IF, OR, ↑) Г; I (INPUT, AT) Ш; O (POKE, PEEK) Щ; P (PRINT, LEN) З; [ (OPEN #, IN K$, {) Х; ] (CLOSE #, LINE, OUT, }) Ъ; ENTER (L-shaped, rows 1-2); TRUE / НОРМ |
| 2 | CTRL / УПР; CAPS / ЗГЛ; A (NEW, STOP) Ф; S (SAVE, NOT) Ы; D (DIM, STEP) В; F (FOR, TO) А; G (GO TO, THEN) П; H (GO SUB, BIN) Р; J (LOAD, CODE) О; K (LIST, CHR$) Л; L (LET, STR$) Д; ; (LLIST, VAL$, VAL) Ж; ' (LPRINT, SCR$, ATR) Э; (ENTER); INV / ИНВ |
| 3 | SHIFT / РЕГ (wide); \ (CIRCLE, PI, \|); Z (COPY, BEEP) Я; X (CLEAR, INK) Ч; C (CONT, PAPER) С; V (CLS, FLASH) М; B (BORDER, BRIG) И; N (NEXT, OVER) Т; M (PAUSE, INV) Ь; , (READ, ASN, SIN) Б; . (RESTOR, ACS, COS) Ю; / (DATA, ATN, TAN, ?) Ё; SHIFT / РЕГ; ↑; EDIT / РЕД |
| 4 | ALT / АЛТ; LAT / РУС; Р/А; SPACE (long, about 6u); Р/А; ←; ↓; → |

The x positions and widths follow standard AT proportions: about 15 keys at 1u in row 0; TAB 1.5u; CTRL 1.0u plus
CAPS 1.0u; left SHIFT 1.25u; ALT 1.25u, LAT 1.25u, Р/А 1.25u. All of these are **UNVERIFIED** within ±0.25u; take
them from the figure.

**Mapping (manual)**
- АЛТ+1 = РЕД (EDIT, CS+1), +2 = ЗГЛ (CAPS LOCK, CS+2), +3 = НОРМ (TRUE VIDEO, CS+3), +4 = ИНВ (INV VIDEO, CS+4),
  +5 = ←, +6 = ↓, +7 = ↑, +8 = → (CS+5..8), +9 = ГРАФ (CS+9), +0 = ВШ (DELETE, CS+0).
- The dedicated keys НОРМ, ИНВ, РЕД, ГРАФ, ВШ and the arrows produce the same CS+digit codes.
- The ZX letters and digits sit at their section 0 matrix positions.
- УПР acts as the SS-like CTRL zone, РЕГ selects the S (shift) zone, and АЛТ the ALT zone.
- Table 2 of the manual (PDF page 21) gives the per-key zone codes. It was only partly read: **UNVERIFIED** per key.
- ENTER = A14/D0 and SPACE = A15/D0.
- The physical wiring of РЕГ, УПР and АЛТ to CS, SS or the extra bits is **UNVERIFIED** (schematic not decoded).

**PC keyboard**: no. The Orel had its own 67-key keyboard.

Sources: https://speccy.info/Орель_БК-08 (text; Orel_BK08_Manual.pdf and Orel_BK08_Schematic.djvu linked there);
the manual at https://speccy.info/w/images/0/07/Orel_BK08_Manual.pdf, Рис. 5 (PDF page 20);
computinghistory.org.uk photos `orel-bk08-chm-1..6.jpg` (1-2 case views, 3 logo close-up, 4-5 manual covers,
6 box).

## 6. Profi v3 / v5

### 6.1 Native keyboards

**v3.2 "KEYBOARD-ZX"**
- Album sheet 9, drawing number МО36.00.095. Its logo is "Profi" with hatching.
- Keys:
  - the 40-key ZX matrix;
  - a separate cursor cluster: LEFT / DOWN / RIGHT with UP above (an inverted T) to the left of the main block;
  - one extra key "*" at the right end of the ENTER row.
- The cursor keys use their own lines RIGHT / DOWN / UP / LEFT plus a common GND_J1 (pin 0B). This is
  joystick-style wiring to the board, not the matrix.
- "*" is the FIRE line, on pin 18, joined to line 1.
- The port the joystick lines land on is **UNVERIFIED**. The manuals (3.2cl sheet 7) say the Sinclair joystick is
  wired onto keyboard lines, so the likely mapping is the Sinclair 1 or cursor keys.
- A RESET button and an LED are on the keyboard.
- Connector: a 16-pin KEYB with no KD5. Its XT pads put DATAKEY on bit 7 and the clock on /INT.

**v5.0 "COMPUTER PROFI PLUS V5.0 KEYBOARD"**
- Album p29. Same layout: 40 + the 4-key inverted-T cursor cluster + "*".
- Logo: a stylised striped "P" with "COMPUTER … PROFI PLUS".
- The PC-20 to keyboard pin table includes 8 /HRESET, 9 /KBW and 10 /CSKBD.

**v5.x sheet 9 (PROF5-0D) extra keys on extra address rows.** The column mapping:
- left half KL0..KL4 = 1..5;
- right half 0, 9, 8, 7, 6 = KL0..KL4.

| Line | col1 | col2 | col3 | col4 | col5 | col6 | col7 | col8 | col9 | col0 |
|---|---|---|---|---|---|---|---|---|---|---|
| EKA11 / EKA12 | EDIT (CS+1) | LOC = CAPS LOCK (CS+2) | (blank; CS+3 = TRUE VIDEO not fitted) | INV (CS+4) | LEFT (CS+5) | DOWN (CS+6) | UP (CS+7) | RIGHT (CS+8) | GRAF (CS+9) | DEL (CS+0) |
| EKA15 | | | | | | | | | EXT (KL1 = SS column, CS+SS) | BREAK (KL0 = SPACE column, CS+SPACE) |

- MODE sits below EXT. Its wiring is **UNVERIFIED**.
- The physical position of these keys on the v5.x keyboard is **UNVERIFIED**: no photo of a v5.x native keyboard
  was found.
- X9 (IDC-20) pinout:

  | Pin | Signal | Pin | Signal |
  |---|---|---|---|
  | 1 | KA11 | 11 | +5V |
  | 3 | KL3 | 12, 13 | GND |
  | 4 | KL1 | 14 | KA15 |
  | 5 | KL2 | 15 | KA14 |
  | 6 | KL4 | 16 | KA12 |
  | 7 | KL0 | 17 | KA13 |
  | 8 | RESET | 18 | KA8 |
  | 9 | /KBW | 19 | KA9 |
  | 10 | CSKBD | 20 | KA10 |

  A RESET button is on the keyboard. KD5 (bit 5 of #FE) and /KBW WAIT are used by the XT controller.

**Housing and key colours of the native Profi keyboard: UNVERIFIED.** The only real-hardware photo found is a
Profi v3.x in a БК-0010 case (`profi-v3-in-bk0010-case-speccyinfo-screenshot.jpg`):
- a black case `#1e1e1e` with a "Profi///" badge at the top left (white "Profi" plus red, green and white
  stripes);
- inside, the Soviet 58-key film keyboard of section 1.4: cream alphanumerics, grey modifiers, red BREAK.

For the SVG, either draw the album layout (40 + inverted T + "*", RESET, LED) in a neutral style, or reuse
section 1.4 with the Profi badge.

### 6.2 Album layout key table (v3.2 / v5.0, geometry UNVERIFIED in u)

| Row | x (u) | Keys | Mapping |
|---|---|---|---|
| 0 | 2.5..11.5 | 1..0 | section 0 |
| 1 | 3.0..12.0 | Q..P | section 0 |
| 2 | 3.25..12.25 | A..L, ENTER | section 0 |
| 2 | 13.25 | * (FIRE) | joystick FIRE line (pin 18); port **UNVERIFIED** |
| 3 | 2.75..11.75 | CS, Z..M, SS, SPACE | section 0 |
| 2 | 1.0 | UP | joystick line UP |
| 3 | 0.0 / 1.0 / 2.0 | LEFT / DOWN / RIGHT | joystick lines |

The row offsets and the cluster position are read off the album drawing, which is schematic rather than to scale:
**UNVERIFIED**.

### 6.3 PC keyboard usage

- **Very common**, and on v5 it is the default configuration. The PROFI-XT controller is an 8035 MCU with the ROM
  "JV KRAMIS (C) 28.10.1992 vers 1.27".
- It emulates the matrix. EXT = KD5 on half-row A14.
- The v5.0 manual says the PC/XT keyboard works through the controller in both CP/M and Sinclair modes.
- caro's PS/2 adapter v3.2 (Kamil Karimov, 2004-2008) also exists.

### 6.4 XT to ZX map (controller firmware)

- Esc = CS+1; Backspace = CS+0; Tab = CS+I; Ctrl = CS; Shift = SS; LAlt = SS+Enter; RAlt = SS+Space;
  CapsLock = CS+SS.
- F1-F10 = A..J + EXT; Home / End = K / L + EXT; PgUp / PgDn = M / N + EXT; Ins / Del = O / P + EXT;
  F11 / F12 = SS+Q / SS+W.
- Up = CS+7, Down = CS+6, Left = CS+5, Right = CS+8.
- Punctuation goes through SS combinations; the full list is in `sim/keymap-default.txt`.

Sources:
- `zx-spectrum-data/profi/keyboard/README.md`;
- `zx-spectrum-data/profi/materials/keyboard/README.md` and `sim/keymap-default.txt`;
- `zx-spectrum-data/profi/materials/v5-open-items/PROF5-0D.utf8.txt`;
- `zx-spectrum-data/profi/materials/research/profi-manuals.md` §8;
- `docs/inprogress/2026-10-01-profi-v3-v5/research-profi-keyboard.md` (repo);
- album pages `profi-v50-keyboard-schematic-album-p29.png` (profi50.pdf p29) and ProfiV32il.pdf sheet 9;
- speccy.info Profi page (the BK-0010 case photo);
- zx-pk thread 15581 (the homemade reed keyboard).

## 7. Summary: native keyboard vs PC keyboard

| Machine | Native keyboard to draw | PC/XT/AT keyboard common? |
|---|---|---|
| Pentagon 128 | 40-key sticker mechanical (1.2); alternatively the 58-key film (1.4) | Later, via Scorpion-style AT controller / PS/2 boards |
| Scorpion ZS-256 / PROF ROM / GMX | 58-key film (1.4) | Yes, the own AT/XT controller (1995-97) |
| KAY-1024 | none specific; PC keyboard | Yes, the norm |
| Quorum 128 | own 88-key built-in (section 4) | No (**UNVERIFIED**) |
| Orel BK-08 | own 67-key reed (section 5) | No |
| Profi v3 / v5 | album layout 40 + cursor T + "*" (6.1), or 58-key film | Yes; v5 default XT via PROFI-XT |
