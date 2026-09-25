# Unreal Speccy Debugger (Monitor): Text-Mode UI Specification

**Document:** TDD-DBG-01 · **Variant:** Classic UnrealSpeccy 0.39.x (SMT, with Alone Coder / Deathsoft patches)
**Status:** Reverse-engineered from source. Normative for re-implementations.
**Companion:** TDD-DBG-02 (TSConf / pentevo fork). That document refers back to sections here.

**Sources analysed**

| Source | Revision | Files |
|---|---|---|
| `github.com/alfishe/unreal-speccy` (mirror of the 0.39.0 source) | `a685b6f` | `debug.cpp/.h`, `dbgpaint.cpp`, `dbgreg.cpp`, `dbgtrace.cpp`, `dbgmem.cpp`, `dbgoth.cpp`, `dbgcmd.cpp`, `dbgbpx.cpp`, `dbglabls.cpp`, `dbgrwdlg.cpp`, `z80asm.cpp`, `keydefs.cpp`, `util.cpp` (dispatch), `draw.cpp` (palette), `font16.cpp`, `x32/unreal.ini`, `doc/unreal_e.txt` |

**Purpose.** This document describes every cell of the Unreal Speccy debugger screen: coordinates, lengths, formats, colours, data sources, value ranges, when each value is refreshed, and how errors and peripheral states are shown. The goal is that an engineer or code-generating agent can rebuild the debugger UI from this document alone, in any language and on any platform: a terminal TUI, a Dear ImGui or Qt pixel grid, or a web canvas. The result should match the original cell for cell, or pixel for pixel when rendered with the original font.

**Conventions**

- Coordinates are **zero-based character cells**: `x` is the column (0 = left) and `y` is the row (0 = top).
- A rectangle is written `(x, y, w, h)`: its top-left cell and its size in cells.
- Hex numbers are shown as `0x1F` in prose and as the emulator prints them (upper case, no prefix) in on-screen formats.
- `printf`-style format strings are exact. Examples: `%04X` means 4 upper-case hex digits, zero-padded; `%6u` means right-aligned in 6 cells.
- **MUST / SHOULD / MAY** follow RFC 2119.
- Lines marked **[quirk]** describe behaviour of the original that looks unintended. A re-implementation SHOULD reproduce it in "faithful" mode and MAY fix it in "improved" mode. §15 lists all quirks.

---

## 1. Rendering model

### 1.1 Text screen

| Property | Value |
|---|---|
| Grid | **80 × 30** cells |
| Cell size | 8 × 16 px |
| Native pixel size | 640 × 480 (shown at 1× scale; the emulator saves the video scale on entry and restores it on exit) |
| Buffers | `txtscr[80*30]` holds character codes. `txtscr[80*30 .. 2*80*30)` holds attribute bytes. |
| Clear state | Every repaint starts by filling **all** cells with char `0xB1` (▒) and attribute `0x50` (cyan paper, black ink). |
| Paint order | `regs → trace → memory → watches → stack → AY → pages → ports → beta128 → time`. Later writes overwrite earlier ones; with the documented coordinates nothing overlaps. |
| Frames | Frames are collected in a list during painting and drawn last, **over** the text, as 1-pixel lines (§1.5). |
| Transparent attribute | Attribute `0xFF` means "do not draw this cell". The screen preview uses it (§4.3.2). |

The whole screen is a pure function of the emulator state plus the UI state (§13). There are no partial repaints: every repaint recomputes all panels (§9.3).

### 1.2 Font

- 256 glyphs, 8 × 16 px, 1 bit per pixel, 16 bytes per glyph (row 0 first). Bit 7 is the leftmost pixel.
- This is the IBM VGA ROM font with the **CP866** (Russian DOS) upper half.
- Codes `0x00–0x1F` are the CP437 pictographs: `0x11` ◄, `0x18` ↑, `0x19` ↓, `0x1A` →, `0x1B` ←, `0x01` ☺, and so on.
- `0x7F` is ⌂. `0xB1` is the 50% checker ▒. Code `0x00` is blank.
- The exact glyph data (4096 bytes) is in Appendix C (base64). SHA-256: `ad971c0771f76d1c5e24ed69e120f10efdab85445395cad3ef3a07a37570635c`.
- **Memory bytes are shown with their raw glyph.** In the ASCII columns every byte except `0x00` is drawn as its code point in this font, including control codes and CP866 letters. Only `0x00` is replaced by `.`. A terminal implementation MUST map bytes through the CP437 table for `0x00–0x1F` and `0x7F`, and through CP866 for `0x80–0xFF` (Appendix D), not through Latin-1.

### 1.3 Attribute byte

```
bit  7   6   5   4   3   2   1   0
    [   PAPER (4 bits)  ][   INK (4 bits)   ]
    [ I ][ G ][ R ][ B ][ I ][ G ][ R ][ B ]      I = bright
```

- A font bit of 1 is drawn in the INK colour; 0 is drawn in PAPER.
- This is **not** the ZX attribute format: there is no FLASH bit (flash is forced off in the monitor), and the paper has its own bright bit.
- The colour nibble uses ZX channel order: bit 0 = Blue, bit 1 = Red, bit 2 = Green, bit 3 = Intensity.

### 1.4 Palette

| Idx | Name | RGB (canonical) | Idx | Name | RGB (canonical) |
|---|---|---|---|---|---|
| 0 | black | `#000000` | 8 | bright black | `#000000` |
| 1 | blue | `#0000C0` | 9 | bright blue | `#0000FF` |
| 2 | red | `#C00000` | A | bright red | `#FF0000` |
| 3 | magenta | `#C000C0` | B | bright magenta | `#FF00FF` |
| 4 | green | `#00C000` | C | bright green | `#00FF00` |
| 5 | cyan | `#00C0C0` | D | bright cyan | `#00FFFF` |
| 6 | yellow | `#C0C000` | E | bright yellow | `#FFFF00` |
| 7 | white | `#C0C0C0` | F | bright white | `#FFFFFF` |

**Channel levels.** Classic Unreal draws the monitor with the emulator's active palette (`[COLORS] color=` in `unreal.ini`). A channel is `ZZ` when off, `NN` when on without bright, and `BB` when on with bright. The result is then passed through the 3×3 colour matrix of that palette entry. Levels for the common entries:

| Palette entry | Off (`ZZ`) | Normal (`NN`) | Bright (`BB`) |
|---|---|---|---|
| built-in `default` | `00` | `C0` | `FF` |
| shipped `alone` (default in the shipped ini) | `00` | `A0` | `FF` |
| pentevo fork | `00` | `C0` (hard-coded) | `FF` |

A re-implementation SHOULD use the canonical table above and MAY offer the `alone` variant (normal level `0xA0`) as a theme. Bright black is still black.

### 1.5 Frames

`frame(x, y, w, h, c)` records a rectangle **around** the content cells `(x, y, w, h)`. It is drawn as four 1-px lines in colour index `(c | 8)`, that is, always the bright variant.

| Line | Pixel position |
|---|---|
| top | row `16*y - 1` (the last pixel row of cell row `y-1`), columns `8*x - 1` … `8*(x+w) - 1` |
| bottom | row `16*(y+h)` (the first pixel row of cell row `y+h`), same columns |
| left | column `8*x - 1` (the last pixel column of cell column `x-1`), rows `16*y` … `16*(y+h) - 1` |
| right | column `8*(x+w)` (the first pixel column of cell column `x+w`), same rows |

So every frame sits inside the **gutter cells** around the content: row `y-1`, row `y+h`, column `x-1` and column `x+w`. It overwrites 1 px of whatever glyph is there, including the window title (see the golden PNG).

- All panel frames use `c = 0x01`, which draws **bright blue** (`#0000FF`).
- Dialog frames use `c = 0x04`, which draws **bright green** (`#00FF00`).
- The frame list holds 20 entries in the original. `filledframe()` (dialogs) **clears the list first**, so while an in-screen dialog is open **only the dialog frame is visible**; all panel frames disappear. A faithful implementation MUST reproduce this.

Terminal back-ends cannot draw between cells; §14.2 gives a box-drawing mapping.

### 1.6 Named attributes

Every colour used by the monitor is one of these constants. Panels reference them by name.

| Constant | Byte | Paper / Ink | Used for |
|---|---|---|---|
| `BACKGR` | `50` | cyan / black | background fill (char `B1` ▒) |
| `W_NORM` | `07` | black / white | content of a **non-focused** focusable window (regs, trace, memory) |
| `W_SEL` | `17` | blue / white | content of the **focused** window |
| `W_CURS` | `30` | magenta / black | cursor cell(s) in the focused window |
| `W_TITLE` | `59` | cyan / bright blue | window titles |
| `W_OTHER` | `40` | green / black | passive info panels (watches, stack, ports, beta128, time value) |
| `W_OTHEROFF` | `47` | green / white | labels in passive panels; the "disabled" state of beta128 |
| `W_AYNUM` | `4F` | green / bright white | AY register index digit |
| `W_AYON` | `41` | green / blue | AY register value, the currently latched register |
| `W_AYOFF` | `40` | green / black | AY register value, other registers |
| `W_BANK` | `40` | green / black | page name, readable and writable |
| `W_BANKRO` | `41` | green / blue | page name, read ≠ write mapping (ROM or write-protected) |
| `W_DIHALT1` | `1A` | blue / bright red | "DiHALT" in the focused regs window |
| `W_DIHALT2` | `0A` | black / bright red | "DiHALT" in the unfocused regs window |
| `W_TRACEPOS` | `70` | white / black | trace line at PC |
| `W_48K` | `20` | red / black | `7FFD` line when 48K lock is active |
| `W_DOS` | `20` | red / black | "DOS" indicator |
| `W_INPUTCUR` | `60` | yellow / black | cursor cell of an input field |
| `W_INPUTBG` | `40` | green / black | other cells of an input field |
| `W_TRACE_JINFO_CURS_FG` | `D` (ink only) | bright cyan ink | jump info on the PC line when the cursor is on it in asm column |
| `W_TRACE_JINFO_NOCURS_FG` | `2` (ink only) | red ink | jump info on the PC line otherwise |
| `W_TRACE_JARROW_FOREGR` | `D` (ink only) | bright cyan ink | ◄ marker on the jump-target line |
| `FRAME` | `01` → bright blue | line colour | panel frames |
| `FFRAME_FRAME` | `04` → bright green | line colour | dialog frame |
| `FFRAME_INSIDE` | `50` | cyan / black | dialog body |
| `FFRAME_ERROR` | `52` | cyan / red | dialog error text ("not found") |
| `FRM_HEADER` | `D0` | bright magenta / black | dialog title bar |
| `MENU_INSIDE` / `MENU_ITEM` | `70` | white / black | menu body, enabled item |
| `MENU_HEADER` | `F0` | bright white / black | menu title |
| `MENU_CURSOR` | `E0` | bright yellow / black | selected menu item |
| `MENU_ITEM_DIS` | `7A` | white / bright red | disabled menu item |

**Derived attributes**
- **Changed-value highlight:** `attr | 0x08` sets the ink bright bit. For example `W_NORM` → `0F` (bright white on black), `W_SEL` → `1F`, `W_CURS` → `38` (magenta / bright black, which is visually black).
- **Execution breakpoint line:** `(attr & ~0x07) | 0x02` replaces the ink with red and keeps the ink bright bit and the paper. For example `W_NORM` → `02`, `W_SEL` → `12`, `W_TRACEPOS` → `72`.
- **`tprint_fg(…, ink)`:** keeps the paper nibble already in the cell and sets the ink nibble to `ink`.

---

## 2. Screen map

### 2.1 Region table

"Content" is the rectangle passed to `frame()`. The title is printed on the row above the content. The frame occupies the surrounding gutter cells.

| # | Panel | Content (x,y,w,h) | Title text @ (x,y) | Title attr | Content attr |
|---|---|---|---|---|---|
| 1 | Registers | (1, 1, 32, 4) | `regs` @ (1,0) | `W_TITLE` | `W_SEL`/`W_NORM` (+cursor, +changed) |
| 2 | Trace / disassembly | (1, 6, 32, 21) | `Z80(n)` @ (1,5) and `%04X` last branch @ (9,5) | `W_TITLE` | `W_SEL`/`W_NORM`, `W_TRACEPOS`, bp red |
| 3 | Watches *or* screen preview | (34, 1, 37, 13) | `watches` / `screen memory` / `ray-painted` @ (34,0); `DOS` @ (68,0) | `W_TITLE`; `W_DOS` | `W_OTHER` |
| 4 | Memory / disk / CMOS / NVRAM / palette editor | (34, 15, 37, 12) | dynamic (§4.4.4) @ (34,14) | `W_TITLE` | `W_SEL`/`W_NORM` |
| 5 | Ports | (72, 1, 7, 4) | `ports` @ (72,0) | `W_TITLE` | `W_OTHER` / `W_48K` |
| 6 | Beta 128 (WD1793) | (72, 6, 7, 5) | `beta128` @ (72,5) | `W_TITLE` | `W_OTHER` / `W_OTHEROFF` |
| 7 | Stack | (72, 12, 7, 10) | `stack` @ (72,11) | `W_TITLE` | `W_OTHER` |
| 8 | Pages (memory map) | (72, 23, 7, 4) | `pages` @ (72,22) | `W_TITLE` | `W_OTHEROFF` + `W_BANK`/`W_BANKRO` |
| 9 | Time delta | (1, 28, 26, 1) | none | — | `W_OTHEROFF` + `W_OTHER` |
| 10 | AY registers | (31, 28, 48, 1) | `AY:` / `AY0` / `AY1` @ (28,28) | `W_TITLE` | `W_AYNUM` + `W_AYON`/`W_AYOFF` |

**Gutter usage**
- **Columns** 0, 33, 71 and 79 are pure gutters/frame lines.
- **Rows** 0, 5, 14, 11 (right column), 22 (right column), 27 and 29 are gutters. Title rows share cells with the top frame line.
- **Always background:** row 27, row 29, column 79, and any cell not listed above.

### 2.2 Golden screen

Appendix A has the full 80 × 30 character dump of a reference state, with column and row rulers. Appendix B has the attribute legend for the same state. `classic.png` (companion file) is the pixel render at 2× produced by the reference renderer `unreal_dbg_render.py` (companion file, Appendix E).

---

## 3. Focus and Interaction Model

### 3.1 Focusable Widgets and Traversal
In full parity with original Unreal Speccy and PentEvo debuggers, exactly **four widgets** are selectable/focusable:
1. **Registers (`regs`, ID 0):** CPU registers, flags, and interrupt modes.
2. **Disassembly (`trace`, ID 1):** Instruction disassembly stream and breakpoints.
3. **Memory (`memory`, ID 2):** Memory hex and ASCII dump / editors.
4. **Pages (`pages`, ID 3):** Memory page/bank slots (0..3).

All other panels—**`watches`**, **`ports`**, **`beta128`**, **`stack`**, and **`bottom`** (time delta and AY registers)—are **non-focusable passive displays**. They never receive input focus, never turn blue (`W_SEL`), and cannot be focused via mouse click.

- **Forward Cycle (`Tab`):** `regs` (0) → `trace` (1) → `memory` (2) → `pages` (3) → `regs` (0).
- **Reverse Cycle (`Shift+Tab`):** `regs` (0) ← `trace` (1) ← `memory` (2) ← `pages` (3) ← `regs` (0).
- **Mouse Focus & Field Selection:**
  - Clicking anywhere inside one of the 4 focusable widgets immediately switches focus (applying the blue background `W_SEL = 0x17`) and positions the cursor:
    - `regs`: clicking on any register label or value cell selects that field in `regsCurs`.
    - `trace`: clicking on an instruction row selects that instruction's address in `traceCurs`.
    - `memory`: clicking in the hex dump ($x \in [39, 62]$) sets `memCurs` and activates Hex mode (`memAscii = false`); clicking in the ASCII column ($x \in [63, 70]$) sets `memCurs` and activates ASCII mode (`memAscii = true`).
    - `pages`: clicking on a bank row ($y \in [23, 26]$) sets `pagesCurs` ($0..3$) and `selBank`.
  - Clicking on any passive panel (`watches`, `ports`, `beta128`, `stack`, `bottom`) does **not** steal focus and is ignored.
  - If in-place editing is currently active, clicking away validates and commits the pending buffer on `Enter` rules, or discards it cleanly if invalid.

### 3.2 Visual Focus States
- **Activated Widget:** The currently focused widget receives the **blue background (`W_SEL = 0x17`)** across its body and labels. Its active field displays the **cursor highlight (`W_CURS = 0x30`)**.
- **Inactive Widgets:** Unfocused widgets display their default background colours:
  - Focusable primary panels (`regs`, `trace`, `memory`): neutral dark background `W_NORM = 0x07`.
  - Info panels (`watches`, `ports`, `beta128`, `stack`, `pages` when unfocused, `bottom`): default green background `W_OTHER = 0x40` (or `W_OTHEROFF = 0x47` for labels / disabled states).
  - No cursor is drawn in inactive widgets, but each widget retains its internal cursor position when focus returns.

### 3.3 Internal Field Navigation
Within the active widget, arrow keys (`↑`, `↓`, `←`, `→`) navigate between traversable fields:
- `regs`: 26 fields (A, F, BC, DE, HL, AF', BC', DE', HL', SP, PC, IX, IY, I, R, IM, IFF1, IFF2, SF..CF) navigated via the adjacency table `regs_layout[]` (§4.1.2).
- `trace`: `↑` / `↓` move instruction cursor; `PgUp` / `PgDn` scroll by 21 instructions.
- `memory`: `←` / `→` move by byte; `↑` / `↓` move by 8-byte row; `PgUp` / `PgDn` by 96 bytes; `m` / `Ctrl+Tab` toggle Hex and ASCII sides.
- `pages`: `↑` / `↓` move through bank slots 0..3 for inspection (`selBank = pagesCurs`). Strictly read-only navigation.

### 3.4 Editability Matrix and In-Place Editing

| Widget | Field(s) | Editability | Input Format | Validation & Commit Rules |
|---|---|---|---|---|
| **`regs`** | 8-bit regs (A, F, I, R) | **Editable** | 1–2 hex digits | Validated on `Enter` (`<= 0xFF`); commits via `WriteReg`. |
| | 16-bit regs (BC, DE, HL, alt regs, SP, PC, IX, IY) | **Editable** | 1–4 hex digits | Validated on `Enter` (`<= 0xFFFF`); commits via `WriteReg`. |
| | IM | **Editable** | Digit 0..2 / Space | Digit or Space cycles mode 0→1→2→0; commits via `WriteReg`. |
| | IFF1, IFF2, SF..CF | **Editable** | Space / Enter / 0..1 | Toggles bit (0 or 1); commits via `WriteReg`. |
| **`trace`** | Trace cursor address / Goto | **Editable** | 1–4 hex digits | Validated on `Enter` (`<= 0xFFFF`); sets PC / jump cursor. `Space`/`F9` toggles BPX. |
| **`memory`** | Hex side bytes | **Editable** | 1–2 hex digits | Validated on `Enter` or typed hex nibbles; writes memory via `WriteMemory`. |
| | ASCII side chars | **Editable** | Printable char | Writes byte (`0x20..0x7E`) directly to memory; advances cursor. |
| **`pages`** | Page slots (0..3) | **Read-Only** | None | Memory paging mapping is strictly read-only; typing is rejected. |
| **`watches`** | Watch lines (PC..HL', user watches) | *Passive / Read-Only* | — | Passive display panel; non-focusable. |
| **`ports`** | Port lines (FE, 7FFD, cmos, EFF7) | *Passive / Read-Only* | — | Passive display panel; non-focusable. |
| **`beta128`** | WD1793 lines (CD, STAT, SECT, T, S) | *Passive / Read-Only* | — | Passive display panel; non-focusable. |
| **`stack`** | Stack words (-2, SP, +2..+10) | *Passive / Read-Only* | — | Passive display panel; non-focusable. |
| **`bottom`** | Time delta & AY registers (0..15) | *Passive / Read-Only* | — | Passive display panel; non-focusable. |

### 3.5 Editing Lifecycle
1. **Initiation:** Typing a valid digit/character on an editable field or pressing `Enter` initiates editing mode. The field activates an in-place buffer with cursor highlight.
2. **Buffer Display:** As the user types, the pending characters replace the field content in real time.
3. **Editing Keys:** `Backspace` removes the last typed character.
4. **Validation (`Enter`):** The input buffer is parsed and range-checked according to the field's data type. If valid, the new value is applied to the backend immediately. If invalid, the edit is rejected without corrupting state.
5. **Cancellation (`Esc`):** Exits editing mode, discards the buffer, and restores the original value.

---

## 4. Panels

Each panel lists: **Layout** (exact cells), **Fields** (source, format, range), **States**, **Refresh**, and **Interaction**. "Current CPU" means the CPU selected with `mon.cpu` (§4.2.6). All memory reads (`DirectRm`) are **side-effect-free reads through the current CPU's live 16-bit memory map** (current paging), unless stated otherwise.

### 4.1 Registers — `(1,1,32,4)`

#### 4.1.1 Layout

The window is first painted with a template, then values overwrite the `*` placeholders:

```
col:  0         1         2         3
      01234567890123456789012345678901      (relative to x=1)
row0  af:**** af'**** sp:**** ir: ****
row1  bc:**** bc'**** pc:**** t:******
row2  de:**** de'**** ix:**** im?,i:**
row3  hl:**** hl'**** iy:**** ########
```

Template labels are drawn in the window attribute (`W_SEL`/`W_NORM`). **Absolute x = 1 + relative column.**

#### 4.1.2 Fields (`regs_layout[]`, in index order: this is also the cursor order)

| Idx | Field | Width (bits) | Rel (x,y) | Cells | Format | Source | Nav L,R,U,D |
|---|---|---|---|---|---|---|---|
| 0 | A | 8 | (3,0) | 2 | `%02X` | `A` | 0,1,0,2 |
| 1 | F | 8 | (5,0) | 2 | `%02X` | `F` | 0,5,1,2 |
| 2 | BC | 16 | (3,1) | 4 | `%04X` | `BC` | 2,6,0,3 |
| 3 | DE | 16 | (3,2) | 4 | `%04X` | `DE` | 3,7,2,4 |
| 4 | HL | 16 | (3,3) | 4 | `%04X` | `HL` | 4,8,3,4 |
| 5 | AF' | 16 | (11,0) | 4 | `%04X` | `AF'` | 1,9,5,6 |
| 6 | BC' | 16 | (11,1) | 4 | `%04X` | `BC'` | 2,10,5,7 |
| 7 | DE' | 16 | (11,2) | 4 | `%04X` | `DE'` | 3,11,6,8 |
| 8 | HL' | 16 | (11,3) | 4 | `%04X` | `HL'` | 4,12,7,8 |
| 9 | SP | 16 | (19,0) | 4 | `%04X` | `SP` | 5,13,9,10 |
| 10 | PC | 16 | (19,1) | 4 | `%04X` | `PC` | 6,10,9,11 |
| 11 | IX | 16 | (19,2) | 4 | `%04X` | `IX` | 7,15,10,12 |
| 12 | IY | 16 | (19,3) | 4 | `%04X` | `IY` | 8,18,11,12 |
| 13 | I | 8 | (28,0) | 2 | `%02X` | `I` | 9,14,13,16 |
| 14 | R | 8 | (30,0) | 2 | `%02X` | `R` = `(R & 0x7F) \| R7` (full 8-bit R) | 13,14,14,17 |
| 15 | IM | 2 | (26,2) | 1 | `%X` (0,1,2) | interrupt mode | 11,16,13,20 |
| 16 | IFF1 | 1 | (30,2) | 1 | `%X` (0/1) | `IFF1` | 15,17,13,24 |
| 17 | IFF2 | 1 | (31,2) | 1 | `%X` (0/1) | `IFF2` | 16,17,14,25 |
| 18 | S flag | 1 | (24,3) | 1 | see flags | F bit 7 | 12,19,15,18 |
| 19 | Z flag | 1 | (25,3) | 1 | | F bit 6 | 18,20,15,19 |
| 20 | F5 (Y) | 1 | (26,3) | 1 | | F bit 5 | 19,21,15,20 |
| 21 | H flag | 1 | (27,3) | 1 | | F bit 4 | 20,22,15,21 |
| 22 | F3 (X) | 1 | (28,3) | 1 | | F bit 3 | 21,23,15,22 |
| 23 | P/V flag | 1 | (29,3) | 1 | | F bit 2 | 22,24,16,23 |
| 24 | N flag | 1 | (30,3) | 1 | | F bit 1 | 23,25,16,24 |
| 25 | C flag | 1 | (31,3) | 1 | | F bit 0 | 24,25,17,25 |

- The navigation columns are the target index for Left, Right, Up and Down (`reg.left/right/up/down`). They are not symmetric and some keys lead back to the same field. Reproduce the table exactly.
- **Flags row:** 8 cells at rel x = 24…31. For bit `q` (7 − q from bit 7) the cell shows `"SZ5H3PNC"[q]` when the bit is set and `"sz.h.pnc"[q]` when clear. That is, an upper-case letter when set, and a lower-case letter (or `.` for bits 5 and 3) when clear.
- **T-state counter** at rel (26,1), 6 cells: `%6u` of `cpu.t` (T-states since the start of the current frame, 0 … frame length − 1). Values ≥ 1,000,000 overflow the field and overwrite the frame gutter. This does not happen with real frame lengths.
- **DiHALT state:** if the CPU is halted **and** IFF1 = 0 (a HALT with interrupts disabled, which is a hang), the 6 cells show `DiHALT` in `W_DIHALT1` (focused) or `W_DIHALT2` (unfocused) instead of the T counter.

#### 4.1.3 Colours per value cell

1. Base: `W_SEL` if regs has focus, otherwise `W_NORM`.
2. If regs has focus and the field is the cursor field (`regs_curs`), use `W_CURS`.
3. If the value (masked to its width) differs from the **previous snapshot** (§9.5), OR in `0x08` (bright ink). Flags are compared bit by bit.

#### 4.1.4 Refresh

Every repaint. The values come from the current CPU. The previous snapshot is per CPU.

#### 4.1.5 Interaction (regs focused)

| Action | Default key | Effect |
|---|---|---|
| `reg.left/right/up/down` | ← → ↑ ↓ | move cursor via the nav table |
| `reg.edit` | Enter | edit the field under the cursor (below) |
| hex digit `0–9`, `A–F` (unbound keys) | — | on 8/16-bit fields, start editing and use the typed digit as the first character (§6.1). On IFF, IM and flag fields the key runs the same toggle or cycle as Enter. |
| `reg.a` `reg.f` `reg.bc` `reg.de` | Ctrl+A, Ctrl+F, Ctrl+B, Ctrl+D | move the cursor to the field. **[quirk]** In 0.39.0 these handlers set `lastkey = 0` before calling `renter()`, which then returns early (`ToAscii(0)` fails). So they only **move the cursor** and do not open the editor. The manual says "edit". The TSConf fork removed the early return, so there they do edit. |
| `reg.hl` `reg.pc` `reg.sp` `reg.ix` `reg.iy` `reg.i` `reg.r` | H, P, S, X, Y, I, R | same as above (cursor only) |
| `reg.im` | M | cursor to IM and cycle it 0 → 1 → 2 (no text field, so it works) |
| `reg.iff1` `reg.iff2` | Ctrl+1, Ctrl+2 | toggle |
| `reg.SF` `ZF` `F5` `HF` `F3` `PF` `NF` `CF` | Alt+S, Alt+Z, Alt+5, Alt+H, Alt+3, Alt+P, Alt+N, Alt+C | toggle the flag |
| `reg.codejump` | `'` (QUOTE) | if the cursor field is 16-bit: trace cursor = trace top = value; focus → trace |
| `reg.datajump` | `;` (COL) | if the cursor field is 16-bit: memory cursor = value; editor = memory; focus → memory |

**Editing semantics** (`renter`)
- 8-bit fields open a 2-char hex field and 16-bit fields a 4-char hex field, in place at the value's cells (§6.1). Esc cancels; Enter writes.
- A 1-bit field toggles with no dialog.
- `IM` cycles 0 → 1 → 2 → 0.
- A flag toggles its bit.
- After any edit, `R7` is re-derived from the written R value.

**[quirk]** Tables are sorted with the most keys first (§7.2), so `Alt+S` correctly toggles SF rather than editing SP. But the window's 2-key actions also **shadow the global 2-key actions** that use the same combination. In the regs window, `Alt+S` / `Alt+C` / `Alt+P` toggle flags and never reach `mon.scrshot` / `mon.bpdialog` / `mon.pokedialog`, and `Ctrl+D` is `reg.de`, not `mon.switchdump`.

### 4.2 Trace (disassembly) — `(1,6,32,21)`

#### 4.2.1 Title row (y = 5)

- `Z80(%u)` at (1,5): index of the current CPU (0 = main Z80, 1 = General Sound Z80 when compiled in).
- `%04hX` of `cpu.last_branch` at (9,5): address of the last executed control-transfer instruction (JP/JR/CALL/RET/RST/DJNZ taken; the emulator core records it).
- Columns 7–8 and 13–32 of row 5 stay background.

#### 4.2.2 Line format (21 lines, rel columns 0…31)

```
0000 ..DDCB0106 rr   (ix+01)
│    │          └ col 16..31: mnemonic (≤16 visible chars, truncated)
│    └ col 5..15: opcode bytes (see below) OR label
└ col 0..3: address %04X, col 4 = space
```

**Bytes column**, when labels are off:
- Up to 4 bytes are shown as `%02X` each, concatenated, starting at column 5.
- If the instruction is longer than 4 bytes, show `..` followed by the **last 4** bytes (10 chars).

**Labels on** (`cpu.labels`, Ctrl+L): columns 5… show the label attached to the instruction's address (first 10 chars) instead of the bytes. If there is no label, the column is blank. Operand addresses in the mnemonic are also replaced by labels (up to 20 chars).

The line is padded with spaces to 32 characters and cut at 32.

**Mnemonic format** (Unreal disassembler):
- lower-case, operands after the mnemonic padded to **5 columns** (`ld   a,07`, `call 8020`, `ldir`)
- numbers upper-case hex with no prefix
- 8-bit numbers are 2 digits, 16-bit and addresses are 4 digits
- indexed operands as `(ix+05)` / `(iy-7F)`
- JR/DJNZ targets as absolute 4-digit addresses
- unknown opcodes as `???` (length 1)

**Line addresses.** `trpc[0..21]` holds the start address of each line; line 0 = `trace_top`. Each next line = previous + instruction length, wrapping at 16 bits. `trpc[21]` (one past the last line) is kept for paging.

#### 4.2.3 Per-line colours (evaluated in order)

1. `atr0` = `W_SEL` if trace has focus, otherwise `W_NORM`.
2. If line address == PC, use `W_TRACEPOS`.
3. If the line address has an execution breakpoint (`membits[addr] & BPX`), apply the red-ink transform (§1.6).
4. The whole 32-cell line is printed with that attribute.
5. **Cursor:** if the line address == `trace_curs` and trace has focus, set the cells of the current cursor column to `W_CURS`:

| `trace_mode` | Column | Rel start | Length |
|---|---|---|---|
| 0 | address | 0 | 4 |
| 1 | opcode bytes | 5 | 10 |
| 2 | mnemonic | 16 | 16 |

6. **Jump information** is only shown when the instruction at PC branches (§4.2.4):
   - **On the PC line:** an arrow `0x18` ↑ if target ≤ PC, else `0x19` ↓.
     - If the target is a *computed* address (RET/RETI/RETN/RET cc taken, `JP (HL/IX/IY)`), print `%04X` target plus the arrow at rel cols 27–31.
     - Otherwise print only the arrow at rel col 31.
     - Ink is `W_TRACE_JINFO_CURS_FG` if the cursor is on this line and trace is focused and `trace_mode == 2`; otherwise `W_TRACE_JINFO_NOCURS_FG`.
     - `tprint_fg` is used, so the paper stays.
   - **On the line whose address == target** (if visible): `0x11` ◄ at rel col 31, ink `W_TRACE_JARROW_FOREGR`, paper kept.

#### 4.2.4 Branch analysis of the instruction at PC (`tracewndflags`)

The flags are computed from the current registers **at paint time**:

| Instruction | Taken when | Target | Flags |
|---|---|---|---|
| `HALT` | always | IM0/1: `0x0038`; IM2: word at `(I<<8)\|IntVec` | `HALTCMD` (no arrow drawn; used for step-over) |
| `LDIR/LDDR/CPIR/CPDR/INIR/INDR/OTIR/OTDR` | — | — | `BLKCMD` |
| `RET`, `RETI`, `RETN` | always | word at `(SP)` | `BRANCH` + `BRADDR` |
| `RET cc` | condition true | word at `(SP)` | `BRANCH` + `BRADDR` |
| `JP nn` | always | nn | `BRANCH` |
| `CALL nn` | always | nn | `BRANCH` + `CALLCMD` |
| `CALL cc,nn` | condition true | nn | `BRANCH` + `CALLCMD` |
| `JP cc,nn` | condition true | nn | `BRANCH` + `LOOPCMD` |
| `JP (HL/IX/IY)` | always | register (depends on prefix) | `BRANCH` + `BRADDR` |
| `RST n` | always | n | `BRANCH` + `CALLCMD` |
| `JR e` | always | PC-relative | `BRANCH` |
| `DJNZ e` | `B ≠ 1` | PC-relative | `BRANCH` + `LOOPCMD` |
| `JR cc,e` | condition true | PC-relative | `BRANCH` + `LOOPCMD` |
| anything else, or condition false | — | — | none |

`nextpc` = for `HALTCMD` the interrupt target above; otherwise `PC + length(instr at PC)`. It is used by step-over (§9.4).

#### 4.2.5 Scrolling and cursor rules

- `trace_curs` and `trace_top` are 16-bit and **per CPU**.
- After painting: if `trace_curs < trace_top`, or `trace_curs ≥ trpc[21]`, or the cursor did not land on a line start, then set `trace_top = trace_curs` and repaint. The cursor line becomes the first line.
- `cpu.up` (↑): if the cursor is not on the top line, move it to the previous line. Otherwise `trace_top = trace_curs = prev_instr(trace_curs)`.
- `cpu.down` (↓): move to the next line. If the cursor was on the last line, `trace_top = trpc[1]` (scroll one line).
- `cpu.pgdn`: `trace_top = trpc[21]`; the cursor keeps its line index.
- `cpu.pgup`: apply `prev_instr` 21 times to `trace_top`; the cursor keeps its line index.
- `prev_instr(ip)` (`cpu_up`) is a heuristic. Disassemble forward from `max(ip−16, 0)` and return the start of the last instruction that begins before `ip`. It may land mid-instruction on data; that is accepted behaviour.
- `cpu.left` / `cpu.right`: `trace_mode` = (mode ∓ 1) mod 3.

#### 4.2.6 Interaction (trace focused)

| Action | Default key | Effect |
|---|---|---|
| `cpu.findpc` | Home | `trace_top = trace_curs = PC` |
| `cpu.here` | F4 | run to cursor (§9.4) |
| `cpu.findtext` / `cpu.findcode` | Alt+F7 / Ctrl+F7 | find dialogs (§6.3), searching memory from cursor + 1; the result becomes top and cursor |
| `cpu.goto` | G | 4-hex input **at (1,6)**, the first trace line's address cells; initial value = `trace_top`; result → top and cursor |
| `cpu.bpx` | Space | toggle an execution breakpoint at `trace_curs` |
| `cpu.asm` | Enter | edit the cursor column in place (below) |
| letters `A`…`Y` not bound to another action | — | start asm edit with that letter as the first char |
| `cpu.setpc` | Z | `PC = trace_curs` |
| `cpu.up/down/left/right/pgup/pgdn` | ↑ ↓ ← → PgUp PgDn | §4.2.5 |
| `cpu.save1..8` | Ctrl+1…Ctrl+8 | save `(trace_top, trace_curs)` to slot n |
| `cpu.rest1..8` | 1…8 | if slot n is set: push the current position on the jump stack, then restore slot n |
| `cpu.back` | Backspace | pop the jump stack (32 entries, oldest dropped) |
| `cpu.context` | `'` | take the **last** 4-hex-digit group in the cursor instruction's mnemonic text, push the position, go there |
| `cpu.datajump` | `;` | same address → memory cursor; focus memory; editor = memory |
| `cpu.labels` | Ctrl+L | toggle label display |
| `cpu.importl` | Ctrl+A | label import menu (§11) |

**In-place edit** (`center`)
- The field is at (1 + col_start, 6 + line index), with the cursor column's length. Hex-only for modes 0 and 1; free text for mode 2.
- Enter pre-fills: mode 0 with the address (`%04X`), mode 1 with up to 5 bytes (`%02X`…), mode 2 with the mnemonic text. Any other key clears the field and inserts that key.
- **Mode 0 (address):** push the position; cursor = the value; the top is moved back so the cursor stays on the same screen row (`prev_instr` applied line-index times).
- **Mode 1 (bytes):** the text must be whole hex pairs; trailing spaces are ignored. On a parse error the edit **re-opens** with the text unchanged (no message). On success the bytes are written from the cursor.
- **Mode 2 (asm):** assemble at the cursor address. On success write the bytes and move the cursor down one line. On failure the field **re-opens silently** with the typed text, so the user can fix it. Esc leaves.

### 4.3 Watches — `(34,1,37,13)`, and screen preview

#### 4.3.1 Watch lines (`show_scrshot == 0`)

13 lines, all in `W_OTHER`. Each line is a **hex line** of 37 characters:

```
rel col: 0         1         2         3
         0123456789012345678901234567890123456
          PC: CD 20 80 18 FE 00 00 00 ═ А↑■...
         └5┘└ 8 × "XX " (cols 5..28) ┘└8 raw┘  (cols 29..36)
```

- **Prefix** (cols 0–4): named lines use `%3s: ` (right-aligned name, colon, space). User lines use `%04X ` (the address).
- **Bytes:** for `i` in 0…7, `mem[ptr+i]` as `%02X` at col `5+3i`, followed by a space.
- **Chars:** col `29+i` = byte glyph (`0x00` → `.`).

| Row | Name | Pointer |
|---|---|---|
| 0 | `PC` | PC |
| 1 | `SP` | SP |
| 2 | `BC` | BC |
| 3 | `DE` | DE |
| 4 | `HL` | HL |
| 5 | `IX` | IX |
| 6 | `IY` | IY |
| 7 | `BC'` | BC' |
| 8 | `DE'` | DE' |
| 9 | `HL'` | HL' |
| 10–12 | — (address shown) | `user_watches[0..2]`, defaults `4000`, `8000`, `C000` |

Title at (34,0): `watches`. **DOS indicator:** if the DOS ports are enabled (TR-DOS ROM paged; `comp.flags & CF_DOSPORTS`), show `DOS` at (68,0) in `W_DOS`.

`mon.setwatch` (Ctrl+U) first forces watches mode, then opens three consecutive 4-hex fields at (34, 11+i) for i = 0…2. Esc at any field stops (earlier fields stay changed).

#### 4.3.2 Screen preview (`show_scrshot ∈ {1,2}`)

- `mon.scrshot` (Alt+S) cycles 0 → 1 → 2 → 0.
- The 37 × 13 cell area (296 × 208 px) is made transparent (attr `0xFF`) and replaced by a **centre crop of the emulator's rendered frame**:
  - Mode 1: "screen memory", the normal render of video memory.
  - Mode 2: "ray-painted", the frame drawn only up to the current beam position, with border, multicolour and 2-screen effects.
- The crop is centred on the rendered frame (border included), at 1:1 pixel scale and in the emulator palette.
- Title: `screen memory` / `ray-painted`.
- A TUI back-end SHOULD render this with a terminal graphics protocol (kitty, sixel, iTerm2). With none available, it MAY show a quadrant/half-block approximation, or the text `[screen preview unavailable]` centred in the area.

**Full-screen views** (`mon.screen` F9, `mon.altscreen` Shift+F9, `mon.rayscreen` Alt+F9) temporarily replace the whole debugger with the emulator screen at the normal scale: normal, the *other* 128K screen, or ray-painted. They wait for any key, then return.

#### 4.3.3 Refresh

Every repaint. Memory is read through the live map.

### 4.4 Memory / editors — `(34,15,37,12)`

#### 4.4.1 Editor sources (`editor`)

| Mode | Title prefix | Address space `mem_max` | Byte source |
|---|---|---|---|
| `ED_MEM` | `memory` | 0x10000 | current CPU's live 16-bit map |
| `ED_PHYS` | `disk …` | track length in bytes | raw MFM-decoded track image of drive `mem_disk`, track `mem_track` |
| `ED_LOG` | `disk …` | sum of sector data lengths | concatenated sector data of that track |
| `ED_CMOS` | `cmos` | 256 | RTC CMOS RAM |
| `ED_NVRAM` | `nvram` | 2048 | NVRAM (ATM/ZX-Evo) |
| `ED_COMP_PAL` | `comppal` | 64 | computer palette (ULA+/ATM `comp_pal`) |

- `mon.switchdump` (Ctrl+D) cycles through this order using its own static index. **[quirk]** The index is not re-synced when `mem.mem`, `mem.diskphys` or `mem.disklog` change the mode directly.
- Addresses wrap modulo `mem_max` (`memadr`).

#### 4.4.2 Line formats

**Hex mode** (`mem_dump == 0`, 8 bytes per line, 12 lines) uses the same 37-char hex line as the watches: `%04X ` + 8×`XX ` + 8 glyphs.

**Text dump mode** (`mem_dump == 1`, toggled by `mon.dump` Alt+D; 32 bytes per line) is `%04X ` + 32 glyphs (cols 5–36), with `0x00` → `.`. In dump mode the ASCII side is always the editing side.

- The line address is `%04X` of the editor offset (disk offsets can exceed `FFFF`; only the low 4 digits are shown).
- Lines: `min(ceil(mem_max / bytes_per_line), 12)`. Unused rows (for example comppal: 8 lines) keep the background ▒ fill inside the frame.
- The attribute for all lines is `W_SEL` if memory has focus, otherwise `W_NORM`.

#### 4.4.3 Cursor

`mem_curs` (the byte offset) and `mem_second` (nibble: 0 = high, 1 = low) are per CPU. `mem_ascii` is global.

When memory has focus, one cell gets `W_CURS`:
- hex side: col `5 + 3*i + mem_second`
- ASCII side: col `29 + i`
- dump mode: col `5 + i`

If the cursor is not on screen, `mem_top = mem_curs & ~(bytes_per_line − 1)` and the window is redrawn.

#### 4.4.4 Title (y = 14, `W_TITLE`)

| Mode | Format |
|---|---|
| mem, cmos, nvram, comppal | `"%s: %04X gsdma: %06X"` (prefix, cursor offset, General Sound DMA address; the GS DMA value is shown even when GS is absent) |
| disk physical | `"disk %c, trk %02X, offs %04X"` (drive `A`–`D`, track, offset) |
| disk logical | `"disk %c, trk %02X, sec %02X[%02X], offs %04X"` (sector *index* in the track, `[sector ID R]`, offset inside the sector) |

The title is 24–37 characters wide. The longest (logical disk) is exactly 37, so it always fits `x = 34…70`.

#### 4.4.5 States and errors

- **Disk modes when the track cannot be read** (no disk, or track outside the image): all 12 rows are blanked (37 spaces) and row 6 (y = 21) shows `          track not found            ` in the window attribute. `mem_max = 0`, and all cursor movement and editing keys are ignored.
- **Editing a disk byte:** physical mode marks the image as modified (raw). Logical mode marks it modified **and recomputes the sector's CRC**.

#### 4.4.6 Interaction (memory focused)

| Action | Key | Effect |
|---|---|---|
| `mem.left/right` | ← → | hex: move by nibble (crosses bytes); ASCII: move by byte; scrolls down when leaving the bottom |
| `mem.up/down` | ↑ ↓ | ± bytes_per_line (down scrolls when leaving the bottom; up relies on the auto-recentre) |
| `mem.pgup/pgdn` | PgUp/PgDn | cursor and top ± 12 × bytes_per_line |
| `mem.stline/endline` | Home/End | first/last byte of the line (nibble 0/1) |
| `mem.switch` | Ctrl+Tab | toggle the hex/ASCII side |
| `mem.goto` | Ctrl+G | 4-hex input at (34,15), initial = `mem_top`; top = value aligned down, cursor = value |
| `mem.findtext/findcode` | Alt+F7/Ctrl+F7 | find dialogs (§6.3) in the current editor space |
| `mem.pc/sp/bc/de/hl/ix/iy` | Ctrl+P/S/B/D/H/X/Y | cursor = register value |
| `mem.mem/diskphys/disklog` | Ctrl+M/V/O | select the editor mode |
| `mem.diskgo` | Ctrl+T | ignored only in `ED_MEM`. **[quirk]** It also runs in the cmos, nvram and comppal editors, where it edits `mem_disk` and `mem_track` without visible effect. It edits the title in place, in sequence: drive letter at (39,14) (1 char, A–D, repeats until valid); track at (46,14) (2 hex); for logical mode, sector index at (54,14) (2 hex, repeats until < sector count; cursor = start of that sector) |
| typing, hex side | `0–9`,`A–F` | replace the current nibble and advance |
| typing, ASCII side | printable 0x20–0x7F | write the byte and advance |

Writes go to memory immediately. A write that does not change the value is skipped.

### 4.5 Ports — `(72,1,7,4)`

| Row y | Format | Source | Attr |
|---|---|---|---|
| 1 | `"  FE:%02X"` | last value written to port `#FE` | `W_OTHER` |
| 2 | `"7FFD:%02X"` | last value written to port `#7FFD` | `W_48K` if bit 5 (48K lock) is set **and not** (Pentagon-1024 model, or Profi with `DFFD` bit 4); else `W_OTHER` |
| 3 | `"%04X:%02X"` model extended port, **or** `"cmos:%02X"` (CMOS address register) when the model has none | see table | `W_OTHER` |
| 4 | `"EFF7:%02X"` | last value written to `#EFF7` (Pentagon / ZX-Evo) | `W_OTHER` |

| Memory model | Extended port shown |
|---|---|
| Kay, Scorpion, Profi-Scorpion, +3 | `1FFD` |
| Profi | `DFFD` |
| ATM 450 | `FDFD` |
| ATM 710, ATM3 | the current `FF77` address (full 16-bit port, as latched) with its value |
| Quorum | `0000` (port `#00`) |
| others (Pentagon, 128, 48, …) | none → `cmos:` line |

**Actions**
- `mon.setbank` (Alt+B): 2-hex input at (77,2), initial = current `7FFD`. On accept, write to `7FFD` and remap the banks.
- `mon.sethimem` (Alt+M): 2-hex input at (77,3). On accept, perform an **OUT** to the extended port with full side effects. Ignored when the model has no extended port.

### 4.6 Beta 128 (WD1793) — `(72,6,7,5)`

The 5 × 7 layout:

| Row y | Format | Source |
|---|---|---|
| 6 | `"CD:%02X%02X"` | WD command register, WD data register |
| 7 | `"STAT:%02X"` | status register as read by the CPU (`RdStatus()`; for type-I commands the head-loaded bit is merged in) |
| 8 | `"SECT:%02X"` | sector register |
| 9 | `"T:%02X/%02X"` | selected drive's **physical head track** / WD **track register** |
| 10 | `"S:%02X/%02X"` | Beta system register (`#FF` write: drive select, side, HLT, reset, density) / request status (`#FF` read: bit 7 INTRQ, bit 6 DRQ) |

- **State:** all rows use `W_OTHER` if TR-DOS / Beta 128 is present in the configuration, otherwise `W_OTHEROFF` (grey-out). The values are still printed.
- **Title** `beta128` at (72,5).
- **Refresh:** every repaint. The debugger does **not** advance the WD state machine before painting.

### 4.7 Stack — `(72,12,7,10)`

Ten lines, `W_OTHER`, each exactly 7 chars:

| i | Label (2 chars) | Word address |
|---|---|---|
| 0 | `-2` | SP−2 |
| 1 | `SP` | SP |
| 2…8 | `+2`,`+4`,`+6`,`+8`,`+A`,`+C`,`+E` | SP+2…SP+14 |
| 9 | `10` (no plus sign) | SP+16 |

Format: `label + ":" + "%02X%02X"` of (hi = mem[a+1], lo = mem[a]). This is the little-endian word shown big-endian. Addresses wrap at 16 bits.

### 4.8 Pages — `(72,23,7,4)`, title `pages` at (72,22)

Four lines, one per 16 KB CPU window `i = 0…3` (addresses `0000`, `4000`, `8000`, `C000`):
- cols 72–73: `"%d:"` in `W_OTHEROFF`
- cols 74–78: 5-char name in `W_BANKRO` if the read and write mappings differ (ROM, or write-protected RAM), otherwise `W_BANK`

**Name resolution** (later rules override earlier ones):
1. `?????` (unknown mapping)
2. RAM page → `RAM%2X` (hex page, space-padded to 2: `RAM 5`, `RAM1F`)
3. ROM page → `ROM%2X`
4. Special ROMs: `BASIC` (48K BASIC ROM), `TRDOS`, `B128K` (128 menu/editor ROM)
5. `SVM  ` (Scorpion service monitor ROM, Scorpion and Profi-Scorpion only)
6. On Scorpion/Profi-Scorpion, ROM pages > 3 revert to `ROM%2X`
7. Cache RAM: `CACHE` (or `CACH0` for a 32K cache) and `CACH1`

### 4.9 Time delta — `(1,28,26,1)`

- `time delta:` at cols 1–11 in `W_OTHEROFF`
- `%14I64d` (signed 64-bit, right-aligned 14) at cols 12–25 in `W_OTHER`
- `t` at col 26 in `W_OTHEROFF`
- **Value:** `(frame_start_T + cpu.t) − debug_last_t`, in T-states elapsed since the last "mark". The mark is set **before every single step** and **on leaving the debugger** (§9.5). After F7 it therefore shows the cost of the instruction just executed, including contention. After resuming and re-breaking it shows the cycles between the two breaks.

### 4.10 AY registers — `(31,28,48,1)`

- The panel is **hidden entirely** (not painted, no frame) if no AY is configured (`ay_scheme == none`).
- **Chip label** at (28,28), 3 chars, `W_TITLE`:
  - `AY:` for a single chip. In this case `active_ay` is forced to 0.
  - `AY0` or `AY1` in TurboSound / quadro schemes.
  - `mon.switchay` (Alt+Y) flips the active chip.
- For register `r = 0…15`:
  - at col `31 + 3r`: the hex digit `"0123456789ABCDEF"[r]` in `W_AYNUM`
  - at col `32 + 3r`: `%02X` of the register value, in `W_AYON` if `r` is the chip's currently latched register (last write to `#FFFD`), otherwise `W_AYOFF`
- There are no separators. The row reads `01C10120030…FFFBF`.

---

## 5. (reserved)

Numbering is kept aligned with TDD-DBG-02.

---

## 6. Modal input subsystems

All modal UIs run a **nested event loop**. While it runs:
- the main screen is not recomputed
- the emulator stays stopped
- mouse movement or a click (`mousepos != 0`) **cancels** the modal (same as Esc)

### 6.1 Line input field (`inputhex(x, y, width, hex)`)

- **Geometry:** `width` cells starting at `(x, y)`, drawn over whatever is there. The cell under the cursor is `W_INPUTCUR`; the other cells are `W_INPUTBG`. The rest of the screen is frozen as last painted.
- **Buffer:** a fixed-width string, space-padded. It is pre-filled by the caller. The cursor starts at 0. The mode is **overwrite**.

| Key | Effect |
|---|---|
| printable | write at the cursor and advance. In hex mode only `0–9 A–F` are accepted and lower case is upper-cased. In text mode any character that maps to CP866 is accepted. At the last cell the cursor stays (the last char is overwritten repeatedly). |
| ← / → | move (clamped) |
| Home | cursor → 0 |
| End | cursor → the first of the trailing spaces; if there are none, the last cell |
| Backspace | delete left, shift the rest left, pad with space |
| Delete | delete at the cursor, shift left |
| Insert | insert a space at the cursor, shift right, drop the last char |
| Enter | accept; trailing spaces are removed; returns the string |
| Esc, or mouse | cancel |

- **Wrappers:** `input2` (2 hex digits) and `input4` (4 hex digits) parse with `%x`. An empty result parses as the unchanged initial value; this is original behaviour because `sscanf` fails and keeps `val`.
- **First-key forwarding:** when an edit is started by typing, the key is re-posted into the field's queue, so it becomes the first typed character.

### 6.2 Pop-up menu (`handle_menu`)

- **Size:** width `maxlen + 2`, where `maxlen` = the longest of the title and all item texts. Height `n_items + 3`.
- **Position:** centred: `x = (80 − w) / 2`, `y = (30 − h) / 2` (integer division).
- **Drawing:** `filledframe` (body `MENU_INSIDE`, green dialog frame; all other frames are removed).
  - Row `y`: the title, centred in `maxlen`, with one space of padding each side, in `MENU_HEADER`.
  - Row `y+1`: blank.
  - Rows `y+2+i`: item `i`, aligned per its flag (LEFT/RIGHT/CENTER) inside `maxlen` with 1-space padding. Colour `MENU_ITEM_DIS` if disabled, `MENU_CURSOR` if selected, otherwise `MENU_ITEM`. Text is truncated to `maxlen`.
- **Keys:**
  - ↑/← previous and ↓/→ next, skipping disabled items and wrapping around.
  - Home/PgUp: first enabled item. End/PgDn: last enabled item.
  - Enter or Space selects. Esc or mouse cancels.
- If the initial item is disabled, the selection moves to the next enabled item.

### 6.3 In-screen dialogs

All use `filledframe(x, y, w, h)`: the body is filled with spaces in `FFRAME_INSIDE` and framed in bright green; the header uses `FRM_HEADER`. Field positions are absolute.

#### Find string — `filledframe(10,10,16,4)`

| Cell | Content |
|---|---|
| (10,10) | `"  find string   "` (16 chars, `FRM_HEADER`) |
| (11,12) | `text:` |
| (17,12) | 8-char text input, pre-filled with the last search |

- The search runs from `start+1`, wrapping around the editor space, for an exact byte match (case-sensitive; characters are CP866 bytes).
- **Found:** move to the match.
- **Not found:** `"  not found   "` at (11,12) in `FFRAME_ERROR`. Wait for any key, then close.

#### Find data — `filledframe(10,10,16,5)`

| Cell | Content |
|---|---|
| (10,10) | `"   find data    "` |
| (11,12) | `code: %08X` |
| (11,13) | `mask: %08X` |

- Code and mask are 4 bytes, shown in memory order.
- Inputs: 8-hex code at (17,12), then 8-hex mask at (17,13).
- Match: `(mem[p+i] & mask[i]) == (code[i] & mask[i])` for i = 0…3.
- The defaults persist across uses (initially code `F3000000`, mask `FF000000`, which finds `DI`).
- **Not found:** `"  not found   "` at (11,12) and 14 spaces at (11,13), both `FFRAME_ERROR`. Wait for a key.

#### Fill memory block (`mon.fillblock`, Alt+F) — `filledframe(6,10,26,5)`

| Cell | Content |
|---|---|
| (6,10) | `"    fill memory block     "` |
| (7,12) | `pattern (hex):` |
| (22,12) | 8-hex pattern (1–4 bytes; an odd digit count is padded with `0`; empty → `00`) |
| (7,13) | `start: %04X end: %04X` |

- Start input at (14,13), then end input at (24,13).
- The pattern is repeated over `[start, end]` in the current 16-bit map.
- Start and end are shared with the load/save dialogs.

#### Ripper's tool (`mon.rip`, Alt+T) — `filledframe(18,12,17,6)`

| Cell | Content |
|---|---|
| (18,12) | `"  ripper's tool  "` |
| (19,14) | `trace reads:` with a 1-char input at (33,14), default `Y` |
| (19,15) | `trace writes:` with a 1-char input at (33,15), default `N` |
| (19,16) | `unref. byte:` with a 2-hex input at (32,16), default `CF` |

- `Y`, `y` or `1` enable an option. On accept the read/write access bits are cleared and **ripper mode is armed**.
- The next `mon.rip` opens a "Save ripped data" file picker (`*.bin`). It writes 64 KB where each byte is the memory value if it was read/written (per the selected options) since arming, otherwise the unref byte. Then it disarms.

#### Load / save memory block (`mon.loadblock` Alt+R, `mon.saveblock` Alt+W)

First a menu (§6.2):

| Menu | Title | Items | Notes |
|---|---|---|---|
| Load | `Load data to memory...` | `from binary file`, `from TR-DOS file`, `from TR-DOS sectors` | A 4th item, `from raw sectors of FDD image`, exists but `n_items = 3`, so it is **not shown**. |
| Save | `Save data from memory...` | `to binary file`, `to TR-DOS file`, `to TR-DOS sectors`, `as Z80 disassembly` | A 5th item (raw sectors, disabled) is hidden (`n_items = 4`). |

Then one of these dialog layouts (base `X = 6`, `Y = 10`, width 25). Titles are 25 chars, `FRM_HEADER` at (6,10). Labels are at (7, …).

| Dialog | h | Title | Rows |
|---|---|---|---|
| binary file / disasm | 5 | ` Read from binary file   ` / ` Write to binary file    ` / ` Disasm to text file     ` | (7,12) `file:` + 16-char text input at (13,12). (7,13) `start: %04X` (load) or `start: %04X end: %04X` (save): start input at (14,13), end input at (24,13); end < start re-asks. **Load:** re-asks the file name until the file exists; end = start + file size − 1, clamped to `FFFF`. |
| TR-DOS sectors | 7 | ` Read from TR-DOS sectors` / ` Write to TR-DOS sectors ` | (7,12) `drive:` + 1-char input at (14,12) (A–D, must have a disk, else re-ask). (7,13) `trk (00-9F): %02X` with input at (20,13). (7,14) `sec (00-0F): %02X` with input at (20,14). (7,15) `start: %04X end: %04X`. Transfers 256-byte sectors consecutively (sector 15 → next track, sector 0). |
| TR-DOS file (save) | 6 | ` Write to TR-DOS file    ` | (7,12) drive as above. (7,13) `file:  %-8s %s`: name input (8) at (14,13), extension (1) at (23,13). (7,14) start/end. Creates a catalogue entry (start address = load address; length; sectors = ceil(len/256)). |

- **Disasm to file** writes one line per instruction for `[start, end]`: address, bytes/label padded to column 16, then the full mnemonic. Unlike the trace window, the line is **not** padded or cut to 32 characters.
- **Errors** are native message boxes titled `Error` (the TUI SHOULD show a modal error box):
  - `track #%02X not found`
  - `track #%02X, sector #%02X not found`
  - `track #%02X, sector #%02X is not 256 bytes`
  - `write error` (disk full or catalogue full)
  - `file selector\r\nis not implemented` (Load → "from TR-DOS file")
- File open failures are **silent**: nothing happens.

### 6.4 Native (non-grid) dialogs

These are Win32 dialogs in the original. A re-implementation SHOULD provide equivalent modal overlays. There are no grid coordinates to match.

| Action (key) | Dialog | Contents / behaviour |
|---|---|---|
| `mon.bpdialog` (Alt+C) | **Breakpoints Manager** | Three columns. (1) Conditional: expression edit box, Add/Del, list of expressions (shown decompiled, fully parenthesised). (2) Execution: address or `XXXX-YYYY` range edit, Add/Del, list of ranges merged from the bitmap. (3) Memory access: range edit plus `R` and `W` checkboxes, Add/Del, list of `XXXX[-YYYY] R\|W\|RW`. Double-click moves an item into the edit box and deletes it (edit-in-place). Errors: `Error in expression\nPlease do RTFM`, `Invalid breakpoint address / range`, `Invalid watch address / range`. Add is enabled only when the edit is non-empty (and, for memory, R or W is ticked); conditional breakpoints are capped at `MAX_CBP` = 16 per CPU (each compiled to at most 128 RPN slots). |
| `mon.osw` (Alt+O) | **On-screen watches** | 4 × (checkbox + expression), plus `Trace RAM banks` / `Trace ROM banks`. The watches are shown on the **emulation** screen (LEDs overlay), not in the monitor. The expressions are validated on close; an error names the watch and keeps the dialog open. |
| `mon.labels` (Ctrl+J) | **Jump to Label** | Incremental filter (A–Z, 0–9, `_`, Backspace; case-insensitive substring; a beep when no match or the buffer is full) over the labels visible in the **current** 64K map, shown as `XXXX name`. Enter or double-click pushes the position and moves the trace cursor and top to the label; focus goes to trace. |
| `mon.gs` (Alt+G) | **General Sound** | Sample list (smp, volume, note, priority, freq, length), MOD play/stop, play sample, save sample to `.pcm`, reset GS, list of unknown GS commands (hex) with Clear. If high-level GS is not active: error `high-level GS emulation\nis not initialized`. |
| `mon.pokedialog` (Alt+P) | POKE entry | as in the emulator |
| `mon.memsearch` (Alt+F6) | cheat / changed-value search | as in the emulator |
| `mon.tapebrowser` (Shift+F7), `mon.settings` (Alt+F1), `mon.help` (F1: shows the `monitor_keys` help page) | emulator dialogs | — |
| `mon.save`/`load`/`savesound`/`qsave1-3`/`qload1-3`, resets, NMIs | emulator functions | executed while stopped; the screen repaints afterwards |

---

## 7. Keyboard

### 7.1 Action catalogue and default bindings

Global actions are active in every window. The window tables (§4.1.5, §4.2.6, §4.4.6) are searched **before** these, and each window table contains the global table at its end.

| Action | Default | Function |
|---|---|---|
| `mon.emul` | Esc | leave the debugger and resume emulation (§9.4) |
| `mon.exit` | Alt+F4 | quit the emulator |
| `mon.step` | F7 | single step |
| `mon.stepover` | F8 | step over |
| `mon.exitsub` | F11 | run until return (PC = word at SP) |
| `mon.next` / `mon.prev` | Tab / Shift+Tab | focus cycle |
| `mon.dump` | Alt+D | memory hex ↔ text dump |
| `mon.switchdump` | Ctrl+D | cycle the editor source |
| `mon.setbank` / `mon.sethimem` | Alt+B / Alt+M | port writes (§4.5) |
| `mon.loadblock` / `saveblock` / `fillblock` | Alt+R / Alt+W / Alt+F | §6.3 |
| `mon.bpdialog` | Alt+C | breakpoints manager |
| `mon.osw` | Alt+O | on-screen watches |
| `mon.labels` | Ctrl+J | jump to label |
| `mon.setwatch` | Ctrl+U | user watches |
| `mon.scrshot` | Alt+S | watches / screen / ray preview |
| `mon.screen` / `altscreen` / `rayscreen` | F9 / Shift+F9 / Alt+F9 | full-screen views |
| `mon.switchay` | Alt+Y | AY0/AY1 |
| `mon.cpu` | Ctrl+` (TIL) | next CPU (main Z80 ↔ GS Z80) |
| `mon.rip` | Alt+T | ripper |
| `mon.gs` | Alt+G | GS dialog |
| `mon.pokedialog` | Alt+P | pokes |
| `mon.memsearch` | Alt+F6 | cheat search |
| `mon.tapebrowser` | Shift+F7 | tape browser |
| `mon.help` | F1 | key help |
| `mon.settings` | Alt+F1 | settings |
| `mon.save` / `mon.load` | F2 / F3 | snapshot or disk save/load |
| `mon.savesound` | F5 | WAV/VTX |
| `mon.qsave1..3` | Alt+F2 / Ctrl+F2 / Shift+F2 | quick save |
| `mon.qload1..3` | Alt+F3 / Ctrl+F3 / Shift+F3 | quick load |
| `mon.reset` | F12 | reset (per `[MISC] Reset=`) |
| `mon.reset128` | Ctrl+Shift+F12 | reset to 128 BASIC |
| `mon.resetsys` | Alt+F12 | reset to service ROM |
| `mon.reset48` | Alt+Shift+F12 | reset to 48 BASIC, 128K locked |
| `mon.resetbasic` | Shift+F12 | reset to 48 BASIC, unlocked |
| `mon.resetdos` | Ctrl+F12 | reset to TR-DOS |
| `mon.resetcache` | Alt+Ctrl+F12 | reset to cache |
| `mon.nmi` / `nmidos` / `nmicache` | Alt+Shift+F11 / Ctrl+F11 / Alt+F11 | NMI variants |

The ini also contains `mon.setrange=F6` and `mon.resetrange=SHIFT F6`, but these actions have **no handler** in 0.39.0 and do nothing.

Emulation-mode key `main.monitor` (default **Esc**) enters the debugger.

### 7.2 Dispatch algorithm (normative for faithful mode)

1. Wait for a key event (polling every 20 ms). Meanwhile, handle mouse input and pending repaints.
2. Use the focused window's table (regs, trace or mem; each is the window actions followed by the global actions).
   - **When the configuration is loaded, each table is re-sorted by the number of keys in the binding, most keys first** (`loadkeys()`, a selection sort; the order among entries with the same key count is whatever that sort produces from the declaration order, so it is not stable).
   - Dispatch walks the sorted table. The **first** entry whose 1–4 keys are **all currently held** fires.
   - Modifiers are not exclusive, but because combinations sort first, `Alt+S` hits a 2-key `…=ALT S` entry before a 1-key `…=S` entry.
   - `ALT`, `CONTROL` and `SHIFT` match either the left or the right key. Unused key slots are "always pressed".
   - **Effect with the shipped ini:** a window's own combination bindings win over global ones with the same key count. So in regs, Alt+S/C/P toggle flags and Ctrl+D edits DE. In memory, Ctrl+D is `mem.de`, so `mon.switchdump` (Ctrl+D) only works while **trace** has focus.
3. If nothing fired, the window's **fallback typing handler** runs:
   - regs: hex digit → edit
   - trace: `A`…`Y` → asm edit
   - memory: hex digit or printable → write
4. Repaint.

Bindings are user-configurable in `unreal.ini` using the syntax `action=key1 [key2 [key3 [key4]]]`. A re-implementation SHOULD load the same action names.

---

## 8. Mouse

Cell coordinates are `mx = px / 8`, `my = py / 16` (at 1× scale).

| Where | Left click |
|---|---|
| Trace content (x 1–32, y 6–26) | focus trace; cursor = line address. Column: `rel_x < 5` → mode 0; `< 16` → mode 1; else mode 2 |
| Memory content (x 34–70, y 15–26) | focus memory. Dump mode: `rel_x ≥ 5` → byte `rel_x − 5 + row × 32`. Hex mode: `rel_x ≥ 29` → ASCII byte `rel_x − 29 + row × 8` (ASCII side); `5 ≤ rel_x < 29` and `(rel_x−5) mod 3 ≠ 2` → byte `(rel_x−5)/3`, nibble `(rel_x−5) mod 3` (hex side) |
| Regs content (x 1–32, y 1–4) | focus regs; the cursor goes to the field whose cells contain the click (widths 4, 2 or 1 cells) |

- **Right and middle click** first run the same hit-testing as a left click (they move focus and the cursor), then open a native context menu. Its content depends on the **focused window after hit-testing**, not on the click position:
  - Trace focused: one item, `breakpoint`, which toggles a BPX at the cursor.
  - Otherwise: four dummy lines (`I don't know` / `what to place` / `to menu, so` / `No Stuff Here`) that do nothing. **[quirk]**
- Any mouse event while a modal (§6) is open **cancels** the modal.

---

## 9. Lifecycle, execution control and refresh

### 9.1 Entering the debugger

The debugger is **modal**: emulation is fully stopped while it is shown. It is entered, per CPU (the main Z80 or the GS Z80), when any of these happens:

| Trigger | Checked |
|---|---|
| `main.monitor` key | next instruction boundary |
| execution breakpoint: `membits[PC] & BPX` | before the instruction executes |
| memory read / write breakpoint on an accessed address | during the access; stops after the current instruction |
| a conditional breakpoint evaluates non-zero | before each instruction (when any exist) |
| `PC == dbg_stophere` | step-over, run-to-cursor and run-until-return targets |
| `SP == dbg_stopsp` and PC outside `[loop_r1, loop_r2]`, or PC within 256 bytes after the stop address | step-over-call safety net |

**On entry**
- current CPU = the CPU that broke
- `trace_curs = PC` (`trace_top` is kept, so the auto-scroll rule decides)
- stop targets cleared
- `IN`/`OUT` breakpoint variables reset to `FFFFFFFF` after evaluation
- the screen is switched to monitor video mode

### 9.2 Event loop

```
loop while in debugger:
    if labels shown: reload user labels file if it changed on disk
    repaint()                         # full screen (§1.1); plus auto-scroll re-paint
    present()
    wait:
        poll input every 20 ms
        on mouse → handle, set needclr
        on needclr → repaint
        on key  → dispatch (§7.2) → repaint
```

### 9.3 Refresh policy

- The screen is recomputed **completely** after every handled key, mouse event, step and dialog close.
- There is **no timer-driven refresh** while stopped, because emulated state cannot change.
- In-screen modals repaint only their own cells.
- Values that come from outside the stopped machine are read at repaint time: file-changed labels, and disk images after `mon.load` (F3).
- **Networked or remote clients** SHOULD request a full snapshot after (a) every stop event, (b) every mutating command, and (c) any focus, cursor or scroll change that needs new memory windows. They SHOULD NOT poll while stopped.

### 9.4 Execution commands

| Command | Behaviour |
|---|---|
| **Step** (`mon.step`, F7) | Set the time mark (§9.5). Copy the current CPU into its previous snapshot. Execute one instruction. Deliver a pending maskable interrupt if it is enabled (`iff1`), not right after EI, and allowed by the hardware gate; the interrupt INT line is released once `t ≥ int_length`. Handle end of frame. Set `trace_curs = PC`. **Stay in the debugger.** |
| **Step over** (`mon.stepover`, F8) | If the PC instruction is CALL/CALL cc (taken)/RST: `dbg_stopsp = SP`, `dbg_stophere = nextpc`, then **resume**. If it is a block instruction or HALT: `dbg_stophere = nextpc` (**for HALT this is the interrupt handler entry**), then resume. Otherwise it behaves as Step. (A loop-skipping variant for backward `JR cc` / `JP cc` exists in the source but is disabled.) |
| **Run to cursor** (`cpu.here`, F4) | `dbg_stophere = trace_curs`, resume |
| **Run until return** (`mon.exitsub`, F11) | `dbg_stophere = word at (SP)`, resume |
| **Continue** (`mon.emul`, Esc) | Set `dbgchk` for **all** CPUs if any breakpoints exist, clear the break flags, leave |
| **Set PC** (`cpu.setpc`, Z) | `PC = trace_curs` (no execution) |

When resuming, the debugger closes; the emulator window returns to the normal video mode and scale.

### 9.5 Previous snapshot and time mark

- `prev[cpu]` is the register set used for change highlighting.
- It is updated (a) at the start of each single step, to the state **before** the instruction, and (b) when leaving the debugger, to the state **at leave**.
- So after F7 the changed registers are those modified by that instruction. After re-entering from a breakpoint, the changed registers are those modified since the last leave.
- The time mark (`debug_last_t`) is set at the same two points.

### 9.6 Multiple CPUs

`mon.cpu` cycles through the CPUs: 0 = main, 1 = GS Z80 when compiled in. Per CPU the debugger keeps: registers, prev snapshot, `trace_top`, `trace_curs`, `trace_mode`, `mem_curs`, `mem_top`, `mem_second`, the breakpoint bitmap (`membits`), and the conditional breakpoints.

On switching:
- uninitialised cursor, top or mode are set to the PC (mode 0)
- the switched-from CPU's break flag is cleared
- the screen repaints

---

## 10. Breakpoints

### 10.1 Address breakpoints

- There is one 64 KB bitmap per CPU, indexed by the **16-bit logical address**. Breakpoints are *not* bank-aware: they fire on any page mapped at that address.
- Bits: `BPX` (execute), `BPR` (read), `BPW` (write). The bitmap also stores the `R`/`W`/`X` access-trace bits used by the ripper and coverage.
- Execution breakpoints are toggled with Space in trace, or set as ranges in the manager.

### 10.2 Conditional breakpoints (expression language)

Expressions are compiled to RPN when added. They are evaluated with 32-bit unsigned arithmetic **before every instruction** of that CPU while any exist; any non-zero result breaks.

| Precedence (high → low) | Operators |
|---|---|
| 1 | `!` `~` `M(x)` (memory byte) `a->b` (= `M(a+b)`) |
| 2 | `*` `%` `/` (division or modulo by 0 leaves the left operand) |
| 3 | `+` `-` |
| 4 | `>>` `<<` |
| 5 | `>` `<` `=` `==` `>=` `<=` `!=` |
| 6 | `&` |
| 7 | `^` |
| 8 | `\|` |
| 9 | `&&` |
| 10 | `\|\|` |

Parentheses group.

| Operands | Meaning |
|---|---|
| `A B C D E F H L` `AF BC DE HL` | main registers |
| `A' B' … L'` `AF' BC' DE' HL'` | alternate registers |
| `PC SP IX IY I R` | control registers (`R` is the full 8-bit R) |
| `FD` | last value written to `#7FFD` |
| `OUT` / `IN` | port address of the OUT/IN executed **by this instruction**, or `FFFFFFFF` if none |
| `VAL` | the value written or read by that port access |
| `DOS` | 1 if the TR-DOS ports are active |
| numbers | **hex only, must start with a digit**: `0DFFD`, not `DFFD`. A leading letter is parsed as a register name. |
| `'c'` | a character constant |

**Parsing details**
- The text is upper-cased **except any character that is immediately followed by `'`**. This keeps `'c'` constants intact, but it also means alternate registers must be typed in upper case (`A'`, `HL'`). **[quirk]** A lower-case `hl'` stays lower case, fails the register lookup, and gives the expression error.
- Whitespace is ignored.
- Register names match longest-first per table order.
- A syntax error, unbalanced parentheses, or a leftover stack when test-evaluated gives the error `Error in expression\nPlease do RTFM`.

**Decompiled display** is fully parenthesised binary operations `(a+b)`, unary `!(x)` and `M(x)`. Numbers are printed as `0%X`, with the leading `0` stripped if the next char is a digit.

### 10.3 Persistence

`bpx.ini`, in the emulator directory, is loaded at startup and saved at exit. One range per line:

```
<type><cpu>=0x<start>[-0x<end>]      e.g.  x0=0x8000   r0=0x4000-0x57FF   w1=0x0000
```

- `type` ∈ `r`, `w`, `x`; `cpu` = 0 or 1.
- Conditional breakpoints are **not** persisted.

---

## 11. Labels

**Storage:** the label table maps **host physical addresses**, that is, a RAM or ROM page plus offset, to names. The label shown for a 16-bit address therefore depends on the current paging.

**Label sources**

1. **User file `user.l`** (emulator directory).
   - It is loaded on the first repaint with labels on, and re-loaded automatically when the directory changes while labels are shown.
   - Line formats:
     - `XXXX name`: a 16-bit address in the 64K RAM image. Offset `XXXX` is relative to RAM base page 0 (so `4000` = page 1 offset 0).
     - `PP:XXXX name`: page `PP`, offset `XXXX & 3FFF`.
   - Lines are trimmed. A bad line prints `error in <file>, line N` to the **console** (not the UI).
   - A missing file for explicit loads gives `can't find label file %s` (console / error log).
   - A successful load prints `loaded N labels from …` to the console.
2. **Import menu** (`cpu.importl`, Ctrl+A), a §6.2 menu titled `import labels`:
   - **XAS7:** found in bank `#06`, or `#46` on Pentagon > 128K. The item text is `XAS labels from bank #%02X`, or disabled `XAS labels not found in bank #06` / `…#06,#46`. After import, an info box `imported N labels`.
   - **ALASM 4.42–5.0x:** tables are scanned in all RAM. Items are `N ALASM labels in page P, offset #XXXX`, or disabled `No ALASM labels in whole NK memory`.
   - Then a disabled blank item and a centred `CANCEL`.

**Display:**
- trace bytes column (max 10 chars)
- operand addresses in the mnemonic (max 20 chars)
- the Jump to Label dialog

---

## 12. Error, status and peripheral-state catalogue

| Situation | Presentation |
|---|---|
| No AY configured | AY panel absent (no label, no frame) |
| TurboSound | `AY0`/`AY1` label; Alt+Y switches |
| TR-DOS not present | beta128 values in `W_OTHEROFF` |
| TR-DOS ROM / ports active | `DOS` at (68,0) in `W_DOS` |
| 48K lock (7FFD bit 5) | `7FFD` line in `W_48K` (red paper) |
| Page read-only / ROM | page name in `W_BANKRO` |
| Halted with interrupts disabled | `DiHALT` replaces the T counter (bright red) |
| Execution breakpoint on a visible line | red ink on that line |
| PC line | white paper |
| Branch at PC | arrow (and target address) on the PC line; ◄ on the target line |
| Changed register or flag | bright ink |
| Disk editor, no track | `track not found` centred in the memory window; editing disabled |
| Find, no match | `not found` in red on the dialog, wait for a key |
| Asm or byte-edit parse error | field re-opens with the text kept (no message) |
| Invalid drive, end < start, file missing on load, sector ≥ count | the field re-prompts (no message) |
| Expression / range errors | modal error box (texts in §6.4) |
| Disk transfer errors | modal `Error` box (texts in §6.3) |
| GS dialog without HLE GS | error box |
| Label file errors | console only |
| Ripper armed | no on-screen indicator **[quirk]**; a TUI SHOULD show one, for example `RIP` in the gutter |

---

## 13. Data contract (for client/server implementations)

The screen is a function of the following state. A remote debugger protocol that serves this UI MUST provide at least these fields, per emulator instance and per CPU.

```text
Snapshot {
  cpu_index, cpu_count
  regs: { a f bc de hl af' bc' de' hl' sp pc ix iy i r(8-bit) im iff1 iff2 halted t(frame T) last_branch }
  prev_regs: same shape (taken at step start / debugger leave)
  time_delta: int64  (T since mark)
  branch_at_pc: { target, flags: BRANCH|BRADDR|CALL|LOOP|BLK|HALT } | null ; next_pc
  memory_window(addr, len)          -> bytes via live 16-bit map (side-effect free)
  bp_bitmap(addr, len)              -> BPX/BPR/BPW bits
  disasm(addr, n_lines, labels:bool)-> [{addr, len, bytes_text, label?, mnemonic}]
  prev_instruction(addr)            -> addr   (backward heuristic, §4.2.5)
  ports: { fe, p7ffd, lock48:bool, ext:{port,value}|null, cmos_addr, eff7 }
  dos_ports_active: bool
  beta128: { present:bool, cmd, data, status_read, sector, head_track, track_reg, system, rqs }
  ay: { present:bool, quadro:bool, active_chip, regs[16], latched_reg }
  pages[4]: { name(5 chars), read_only:bool }
  gs_dma_addr
  editors: cmos[256], nvram[2048], comp_pal[64], disk(drive,track)->{phys bytes | sectors[{id,len,data}]} | "no track"
  screen_preview(mode 1|2) -> 296x208 indexed pixels (centre crop)
  labels: lookup(phys) and list(visible in current map)
}
Commands: step, step_over, run_to(addr), run_until_return, continue, set_pc,
          write_reg(field,value), toggle_flag, write_mem(space,addr,bytes), out(port,val),
          set_7ffd(val), bp_{add,del,list}(exec|read|write range | condition text),
          assemble(addr,text)->bytes|error, find(space,start,pattern,mask)->addr|none,
          load/save block, disasm_to_file, select_cpu, labels_import/reload
```

UI-only state stays in the client:
- focus
- `regs_curs`, `trace_mode`, jump stack and 8 slots
- `mem_ascii`, `mem_dump`, `editor`, disk drive, track and sector selection
- `show_scrshot`, `user_watches`, `trace_labels`

`trace_top`, `trace_curs`, `mem_top` and `mem_curs` are per CPU **in the original**, so a client SHOULD keep them per CPU.

---

## 14. Porting guidance

### 14.1 Pixel back-ends (ImGui, Qt, canvas, SDL)

- Render the `(char, attr)` grid with the Appendix C font and the §1.4 palette, then draw the frames as 1-px lines (§1.5). This is exact.
- Integer scaling (1×, 2×, …) SHOULD be nearest-neighbour.
- The golden PNG (`classic.png`) is 2× nearest-neighbour; a correct implementation matches it exactly.

### 14.2 Terminal back-ends (ncurses, ratatui, FTXUI, Textual, …)

**Size.** The grid needs **≥ 80 × 30**. If the terminal is smaller, show a centred message `terminal too small: need 80x30` rather than wrapping.

**Colour.** Prefer 24-bit truecolor with the §1.4 RGB values. For 16-colour terminals, map the ZX index (bit 0 B, bit 1 R, bit 2 G, bit 3 I) to the ANSI index (bit 0 R, bit 1 G, bit 2 B, bit 3 bright):

| ZX idx | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| ANSI | 0 | 4 | 1 | 5 | 2 | 6 | 3 | 7 |

Add 8 for the bright variants. The bright paper colours need `100–107` (aixterm) or truecolor. On terminals without bright backgrounds, draw bright paper as the normal paper plus bold.

**Frames.**
- Draw them in the gutter cells as box-drawing characters in bright blue on the gutter's background. For each gutter cell, compute which of the four directions (N/E/S/W) a frame line passes through, then choose the glyph: `─ │ ┌ ┐ └ ┘ ├ ┤ ┬ ┴ ┼`.
- **Text has priority** over frame glyphs. Titles, `DOS`, `AY:`, and the TSConf LEDs stay visible and "interrupt" the line. This matches the pixel original, where the line only grazes the text.
- Where two frames share a gutter (column 33, column 71, row 5 left, row 14 middle, and so on), merge the directions into one glyph.
- Dialog frames are drawn in bright green and replace all panel frames while open (§1.5).

**Glyphs.** Map through CP437 (0x00–0x1F, 0x7F) and CP866 (0x80–0xFF) to Unicode (Appendix D). The background fill ▒ (U+2592) SHOULD be kept, because it is the signature look. A "clean" theme MAY replace it with a space.

**Screen preview.** §4.3.2.

**Keys.** Terminals cannot report "all keys held". Implement dispatch as an exact match of the key plus its modifier set, checking the focused window's table before the global table. This reproduces the sorted-table behaviour of §7.2, including the shadowing. Ctrl+Tab and some Alt+F-keys may be swallowed by terminal emulators, so provide the alternative bindings from the ini.

**Mouse.** Enable SGR mouse reporting (1006) and map the cell coordinates directly.

### 14.3 Themes

Theme `unreal-classic` = §1.4 canonical palette. Theme `unreal-alone` = normal level `0xA0`. Every named attribute (§1.6) SHOULD be themeable; the layout MUST NOT depend on the theme.

---

## 15. Known quirks (faithful vs improved)

| # | Quirk | Faithful | Improved suggestion |
|---|---|---|---|
| Q1 | Window combination bindings shadow global ones with the same key count (in regs: Alt+S/C/P toggle flags instead of scrshot/bpdialog/pokes; Ctrl+D is `reg.de` / `mem.de`, so switchdump only works from trace) | reproduce | give global actions unique combinations, or let a configurable precedence decide |
| Q2 | Frames vanish while an in-screen dialog is open | reproduce | keep panel frames dimmed |
| Q3 | `reg.a`…`reg.r` shortcuts only move the cursor (no edit) in 0.39.0 | reproduce | open the editor, as the fork does |
| Q4 | `mon.switchdump` index is independent of the direct mode keys | reproduce | derive from the current mode |
| Q5 | Hidden 4th load item and 5th save item | reproduce | hide as well |
| Q6 | Step-over on HALT stops at the interrupt handler, not after the HALT | reproduce | offer both |
| Q7 | `prev_instr` heuristic may mis-sync on data | reproduce | same (inherent) |
| Q8 | Right-click menu outside trace has joke entries | omit (it is a no-op) | context menus per panel |
| Q9 | Breakpoints are not bank-aware | reproduce | optional page-qualified breakpoints |
| Q10 | No indicator while the ripper is armed | reproduce | `RIP` badge |
| Q11 | `mon.setrange` / `mon.resetrange` bound but unimplemented | ignore | — |
| Q12 | T counter `%6u` can overflow 6 cells only on abnormal frame lengths | reproduce | clamp |

---

## 16. Conformance checklist

1. With the Appendix A state, the text dump matches Appendix A **character for character**, and the attributes match Appendix B.
2. The pixel render at 2× matches `classic.png` exactly.
3. Tab from trace focuses memory, then regs, then trace. Unfocused windows show no cursor.
4. F7 on `CALL 8020`: PC = 8020, SP decremented by 2, the changed registers (PC, SP) are bright, and time delta = 17 in uncontended memory.
5. F8 on `CALL`: runs to the return and re-enters with the cursor at the next instruction.
6. Space on a line toggles red ink. The breakpoint appears in the manager list, merged into ranges.
7. With trace or memory focused, Alt+S cycles watches → screen memory → ray-painted → watches, and the titles change accordingly. With regs focused, Alt+S toggles SF instead (§7.2).
8. The disk editor on an empty drive shows `track not found` and ignores typing.
9. Find a missing string: `not found` in red, any key closes it, and all frames reappear.
10. Every §12 state can be reproduced.

---

## Appendix A — Golden text dump (80 × 30)

Reference state used for the golden dumps (from `sample_state()` in `unreal_dbg_render.py`):

- Current CPU 0. PC=`8011` (`CALL 8020`), SP=`BFFE` (word `8014` on stack), A=`07` F=`44` (Z,P set), BC=`17FF`, DE=`4001`, HL=`4000`, IX=IY=`5C3A`, I=`3F`, R=`12`, IM 1, IFF1=IFF2=0, T=`17023`, last branch `800F`, not halted.
- Previous snapshot differs in BC (0000), PC (800F), F (40) and R (10), so these are highlighted bright.
- Focus = trace, `trace_top=8000`, `trace_curs=8011`, `trace_mode=2` (mnemonic column). Execution breakpoint at `8022`. Branch at PC: CALL → target `8020` (arrow ↓ on PC line, ◄ on `8020`).
- Memory window: editor=memory, top=`C000`, cursor=`C003` hex side high nibble (cursor not drawn because memory is unfocused). Text `UNREAL SPECCY DEBUGGER` at `C000`; bytes `00..0F` at `4000`.
- Watches mode; user watches `4000 8000 C000`. DOS ports inactive.
- Ports FE=`07`, 7FFD=`10`, no extended port (Pentagon) → `cmos:00`, EFF7=`00`. TR-DOS present; WD cmd=80 data=00 status=20 sector=01 tracks 00/00 system=3C rqs=80.
- AY single chip, regs `1C 01 00 00 00 00 00 38 0F 00 00 00 00 00 FF BF`, latched reg 7. Time delta 12.

Column ruler: first line = tens, second line = units. Row number at left. Glyphs mapped via Appendix D.

```text
     0         1         2         3         4         5         6         7         
     01234567890123456789012345678901234567890123456789012345678901234567890123456789
  0  ▒regs▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒watches▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒ports▒▒▒
  1  ▒af:0744 af'0000 sp:BFFE ir: 3F12▒ PC: CD 20 80 18 FE 00 00 00 ═ А↑■...▒  FE:07▒
  2  ▒bc:17FF bc'0000 pc:8011 t: 17023▒ SP: 14 80 55 4E 52 45 41 4C ¶АUNREAL▒7FFD:10▒
  3  ▒de:4001 de'0000 ix:5C3A im1,i:00▒ BC: 00 00 00 00 00 00 00 00 ........▒cmos:00▒
  4  ▒hl:4000 hl'0000 iy:5C3A sZ.h.Pnc▒ DE: 01 02 03 04 05 06 07 08 ☺☻♥♦♣♠•◘▒EFF7:00▒
  5  ▒Z80(0)▒▒800F▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒ HL: 00 01 02 03 04 05 06 07 .☺☻♥♦♣♠•▒beta128▒
  6  ▒8000 F3         di              ▒ IX: 00 00 00 00 00 00 00 00 ........▒CD:8000▒
  7  ▒8001 3100C0     ld   sp,C000    ▒ IY: 00 00 00 00 00 00 00 00 ........▒STAT:20▒
  8  ▒8004 210040     ld   hl,4000    ▒BC': 00 00 00 00 00 00 00 00 ........▒SECT:01▒
  9  ▒8007 110140     ld   de,4001    ▒DE': 00 00 00 00 00 00 00 00 ........▒T:00/00▒
 10  ▒800A 01FF17     ld   bc,17FF    ▒HL': 00 00 00 00 00 00 00 00 ........▒S:3C/80▒
 11  ▒800D 3600       ld   (hl),00    ▒4000 00 01 02 03 04 05 06 07 .☺☻♥♦♣♠•▒stack▒▒▒
 12  ▒800F EDB0       ldir            ▒8000 F3 31 00 C0 21 00 40 11 є1.└!.@◄▒-2:0000▒
 13  ▒8011 CD2080     call 8020      ↓▒C000 55 4E 52 45 41 4C 20 53 UNREAL S▒SP:8014▒
 14  ▒8014 18FE       jr   8014       ▒memory: C003 gsdma: 000000▒▒▒▒▒▒▒▒▒▒▒▒+2:4E55▒
 15  ▒8016 00         nop             ▒C000 55 4E 52 45 41 4C 20 53 UNREAL S▒+4:4552▒
 16  ▒8017 00         nop             ▒C008 50 45 43 43 59 20 44 45 PECCY DE▒+6:4C41▒
 17  ▒8018 00         nop             ▒C010 42 55 47 47 45 52 00 00 BUGGER..▒+8:5320▒
 18  ▒8019 00         nop             ▒C018 00 00 00 00 00 00 00 00 ........▒+A:4550▒
 19  ▒801A 00         nop             ▒C020 00 00 00 00 00 00 00 00 ........▒+C:4343▒
 20  ▒801B 00         nop             ▒C028 00 00 00 00 00 00 00 00 ........▒+E:2059▒
 21  ▒801C 00         nop             ▒C030 00 00 00 00 00 00 00 00 ........▒10:4544▒
 22  ▒801D 00         nop             ▒C038 00 00 00 00 00 00 00 00 ........▒pages▒▒▒
 23  ▒801E 00         nop             ▒C040 00 00 00 00 00 00 00 00 ........▒0:BASIC▒
 24  ▒801F 00         nop             ▒C048 00 00 00 00 00 00 00 00 ........▒1:RAM 5▒
 25  ▒8020 3E07       ld   a,07      ◄▒C050 00 00 00 00 00 00 00 00 ........▒2:RAM 2▒
 26  ▒8022 D3FE       out  (FE),a     ▒C058 00 00 00 00 00 00 00 00 ........▒3:RAM 0▒
 27  ▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒
 28  ▒time delta:            12t▒AY:01C10120030040050060073880F900A00B00C00D00EFFFBF▒
 29  ▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒
```

## Appendix B — Golden attribute map (run-length)

Format per row: `x0-x1:AA` = cells x0..x1 have attribute byte AA (hex). `FF` would mean transparent.

```text
 0  0:50 1-4:59 5-33:50 34-40:59 41-71:50 72-76:59 77-79:50
 1  0:50 1-5:07 6-7:0F 8-30:07 31-32:0F 33:50 34-70:40 71:50 72-78:40 79:50
 2  0:50 1-3:07 4-7:0F 8-19:07 20-23:0F 24-32:07 33:50 34-70:40 71:50 72-78:40 79:50
 3  0:50 1-32:07 33:50 34-70:40 71:50 72-78:40 79:50
 4  0:50 1-29:07 30:0F 31-32:07 33:50 34-70:40 71:50 72-78:40 79:50
 5  0:50 1-6:59 7-8:50 9-12:59 13-33:50 34-70:40 71:50 72-78:59 79:50
 6  0:50 1-32:17 33:50 34-70:40 71:50 72-78:40 79:50
 7  0:50 1-32:17 33:50 34-70:40 71:50 72-78:40 79:50
 8  0:50 1-32:17 33:50 34-70:40 71:50 72-78:40 79:50
 9  0:50 1-32:17 33:50 34-70:40 71:50 72-78:40 79:50
10  0:50 1-32:17 33:50 34-70:40 71:50 72-78:40 79:50
11  0:50 1-32:17 33:50 34-70:40 71:50 72-76:59 77-79:50
12  0:50 1-32:17 33:50 34-70:40 71:50 72-78:40 79:50
13  0:50 1-16:70 17-31:30 32:3D 33:50 34-70:40 71:50 72-78:40 79:50
14  0:50 1-32:17 33:50 34-59:59 60-71:50 72-78:40 79:50
15  0:50 1-32:17 33:50 34-70:07 71:50 72-78:40 79:50
16  0:50 1-32:17 33:50 34-70:07 71:50 72-78:40 79:50
17  0:50 1-32:17 33:50 34-70:07 71:50 72-78:40 79:50
18  0:50 1-32:17 33:50 34-70:07 71:50 72-78:40 79:50
19  0:50 1-32:17 33:50 34-70:07 71:50 72-78:40 79:50
20  0:50 1-32:17 33:50 34-70:07 71:50 72-78:40 79:50
21  0:50 1-32:17 33:50 34-70:07 71:50 72-78:40 79:50
22  0:50 1-32:17 33:50 34-70:07 71:50 72-76:59 77-79:50
23  0:50 1-32:17 33:50 34-70:07 71:50 72-73:47 74-78:41 79:50
24  0:50 1-32:17 33:50 34-70:07 71:50 72-73:47 74-78:40 79:50
25  0:50 1-31:17 32:1D 33:50 34-70:07 71:50 72-73:47 74-78:40 79:50
26  0:50 1-32:12 33:50 34-70:07 71:50 72-73:47 74-78:40 79:50
27  0-79:50
28  0:50 1-11:47 12-25:40 26:47 27:50 28-30:59 31:4F 32-33:40 34:4F 35-36:40 37:4F 38-39:40 40:4F 41-42:40 43:4F 44-45:40 46:4F 47-48:40 49:4F 50-51:40 52:4F 53-54:41 55:4F 56-57:40 58:4F 59-60:40 61:4F 62-63:40 64:4F 65-66:40 67:4F 68-69:40 70:4F 71-72:40 73:4F 74-75:40 76:4F 77-78:40 79:50
29  0-79:50
```

## Appendix C — Font (8×16, 256 glyphs, 4096 bytes, base64)

Glyph `c` occupies bytes `c*16 … c*16+15`, top row first, bit 7 = leftmost pixel. SHA-256 `ad971c0771f76d1c5e24ed69e120f10efdab85445395cad3ef3a07a37570635c`. Identical in the pentevo fork.

```text
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
AAAAAAAeNmZmZmbmAAAAAAAAAAAAxu7+/tbWxgAAAAAAAAAAAMbGxv7GxsYAAAAAAAAAAAB8xsbG
xsZ8AAAAAAAAAAAA/sbGxsbGxgAAAAARRBFEEUQRRBFEEUQRRBFEVapVqlWqVapVqlWqVapVqt13
3Xfdd9133Xfdd9133XcYGBgYGBgYGBgYGBgYGBgYGBgYGBgYGPgYGBgYGBgYGBgYGBgY+Bj4GBgY
GBgYGBg2NjY2NjY29jY2NjY2NjY2AAAAAAAAAP42NjY2NjY2NgAAAAAA+Bj4GBgYGBgYGBg2NjY2
NvYG9jY2NjY2NjY2NjY2NjY2NjY2NjY2NjY2NgAAAAAA/gb2NjY2NjY2NjY2NjY2NvYG/gAAAAAA
AAAANjY2NjY2Nv4AAAAAAAAAABgYGBgY+Bj4AAAAAAAAAAAAAAAAAAAA+BgYGBgYGBgYGBgYGBgY
GB8AAAAAAAAAABgYGBgYGBj/AAAAAAAAAAAAAAAAAAAA/xgYGBgYGBgYGBgYGBgYGB8YGBgYGBgY
GAAAAAAAAAD/AAAAAAAAAAAYGBgYGBgY/xgYGBgYGBgYGBgYGBgfGB8YGBgYGBgYGDY2NjY2NjY3
NjY2NjY2NjY2NjY2NjcwPwAAAAAAAAAAAAAAAAA/MDc2NjY2NjY2NjY2NjY29wD/AAAAAAAAAAAA
AAAAAP8A9zY2NjY2NjY2NjY2NjY3MDc2NjY2NjY2NgAAAAAA/wD/AAAAAAAAAAA2NjY2NvcA9zY2
NjY2NjY2GBgYGBj/AP8AAAAAAAAAADY2NjY2Njb/AAAAAAAAAAAAAAAAAP8A/xgYGBgYGBgYAAAA
AAAAAP82NjY2NjY2NjY2NjY2NjY/AAAAAAAAAAAYGBgYGB8YHwAAAAAAAAAAAAAAAAAfGB8YGBgY
GBgYGAAAAAAAAAA/NjY2NjY2NjY2NjY2NjY2/zY2NjY2NjY2GBgYGBj/GP8YGBgYGBgYGBgYGBgY
GBj4AAAAAAAAAAAAAAAAAAAAHxgYGBgYGBgY/////////////////////wAAAAAAAAD/////////
///w8PDw8PDw8PDw8PDw8PDwDw8PDw8PDw8PDw8PDw8PD/////////8AAAAAAAAAAAAAAAAAANxm
ZmZmZnxgYPAAAAAAAAB8xsDAwMZ8AAAAAAAAAAAAfloYGBgYPAAAAAAAAAAAAMbGxsbGfgYGxnwA
AAAAPBh+29vb29t+GBg8AAAAAAAAxmw4ODhsxgAAAAAAAAAAAMbGxsbGxv8DAwAAAAAAAADGxsbG
fgYGAAAAAAAAAAAA1tbW1tbW/gAAAAAAAAAAANbW1tbW1v4DAwAAAAAAAAD4sLA+MzN+AAAAAAAA
AAAAxsbG9t7e9gAAAAAAAAAAAPBgYHxmZvwAAAAAAAAAAAB8xgY+BsZ8AAAAAAAAAAAAztvb+9vb
zgAAAAAAAAAAAH/Gxn42ZucAAAAAbAD+ZmJoeGhgYmb+AAAAAAAAAGwAfMbG/MDGfAAAAAAAAHzG
wMj4yMDAxnwAAAAAAAAAAAB8xsD4wMZ8AAAAAGYAPBgYGBgYGBgYPAAAAAAAAABsADgYGBgYGDwA
AAAAbDjGxsbGxn4GBsZ8AAAAAAAAAGw4xsbGxsZ+BgbGfAAAOGxsOAAAAAAAAAAAAAAAAAAAAAAA
ABgYAAAAAAAAAAAAAAAAAAAYAAAAAAAAAAAADgwMDAwMDOxsPBwAAAAAAADPze/s/9zczMzMAAAA
AAAAAMZ8xsbGxsZ8xgAAAAAAAAAAfn5+fn5+fgAAAAAAAAAAAAAAAAAAAAAAAAAAAA==
```

## Appendix D — Byte → Unicode glyph table (for terminal back-ends)

CP437 pictographs for `00–1F` and `7F`, ASCII for `20–7E`, CP866 for `80–FF`. `00` is shown as a space (the monitor prints `.` for zero bytes in dumps anyway).

```text
     0 1 2 3 4 5 6 7 8 9 A B C D E F
 0_    ☺ ☻ ♥ ♦ ♣ ♠ • ◘ ○ ◙ ♂ ♀ ♪ ♫ ☼
 1_  ► ◄ ↕ ‼ ¶ § ▬ ↨ ↑ ↓ → ← ∟ ↔ ▲ ▼
 2_    ! " # $ % & ' ( ) * + , - . /
 3_  0 1 2 3 4 5 6 7 8 9 : ; < = > ?
 4_  @ A B C D E F G H I J K L M N O
 5_  P Q R S T U V W X Y Z [ \ ] ^ _
 6_  ` a b c d e f g h i j k l m n o
 7_  p q r s t u v w x y z { | } ~ ⌂
 8_  А Б В Г Д Е Ж З И Й К Л М Н О П
 9_  Р С Т У Ф Х Ц Ч Ш Щ Ъ Ы Ь Э Ю Я
 A_  а б в г д е ж з и й к л м н о п
 B_  ░ ▒ ▓ │ ┤ ╡ ╢ ╖ ╕ ╣ ║ ╗ ╝ ╜ ╛ ┐
 C_  └ ┴ ┬ ├ ─ ┼ ╞ ╟ ╚ ╔ ╩ ╦ ╠ ═ ╬ ╧
 D_  ╨ ╤ ╥ ╙ ╘ ╒ ╓ ╫ ╪ ┘ ┌ █ ▄ ▌ ▐ ▀
 E_  р с т у ф х ц ч ш щ ъ ы ь э ю я
 F_  Ё ё Є є Ї ї Ў ў ° ∙ · √ № ¤ ■  
```

## Appendix E — Reference renderer

`unreal_dbg_render.py` (companion file, Python 3, Pillow for PNG) is a line-by-line port of the painting code of both variants. Run `python3 unreal_dbg_render.py font16.cpp|font16.bin outdir` to regenerate `classic.txt`, `classic_attr.txt`, `classic.png`, `tsconf.txt`, `tsconf_attr.txt`, `tsconf.png`. Use it as an executable oracle: feed the same state to your implementation and diff.
