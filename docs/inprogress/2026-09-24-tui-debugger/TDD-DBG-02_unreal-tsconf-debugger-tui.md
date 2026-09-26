# Unreal Speccy Debugger — TSConf / ZX-Evo (pentevo) Fork: Text-Mode UI Specification

**Document:** TDD-DBG-02 · **Variant:** Unreal in `tslabs/zx-evo`, path `pentevo/unreal/Unreal` (TSConf-capable fork)
**Status:** Reverse-engineered from the source code. This document is normative for re-implementations.
**Base document:** TDD-DBG-01 (Classic Unreal Speccy). Sections of TDD-DBG-01 that this document does not override apply unchanged. §0.2 lists them.

**Sources analysed**

| Source | Revision | Files |
|---|---|---|
| `github.com/tslabs/zx-evo` | `4cd2b81` (2026-09-23) | `Unreal/debugger/*.cpp/.h` (`debug`, `dbgpaint`, `dbgreg`, `dbgtrace`, `dbgmem`, `dbgoth`, `dbgcmd`, `dbgbpx`, `dbglabls`, `dbgrwdlg`, **`dbgwidgets`**, **`dbgtsconf`**), `tsconf.h`, `vars.cpp`, `keydefs.cpp`, `Unreal.rc`, `cfg/Unreal.ini` |

---

## 0. How this variant differs, and what it inherits

### 0.1 Summary of differences from Classic

| Area | Classic (TDD-DBG-01) | TSConf fork (this document) |
|---|---|---|
| Host window | Replaces the emulator screen inside the main window | **Separate top-level window** titled `UnrealSpeccy debugger`, with a menu bar. The main window stays visible and keeps showing the emulated screen. |
| Grid | 80 × 30 | Buffer is **157 × 30**. Visible width is **157** when the machine is TSConf (`MM_TSL`) and **88** otherwise. |
| Scale | Fixed 1× | 1× or 2× (menu `Monitor → Scale 2x`), nearest-neighbour |
| Palette | Emulator palette (levels depend on the ini file) | **Fixed**: normal level `C0`, bright level `FF` (the canonical table in TDD-DBG-01 §1.4) |
| Focusable windows | regs, trace, memory | regs, trace, memory, **pages** |
| New panels | — | **PC history** `(80,1,7,28)` and the **TSConf register board** `x = 88…156` |
| Screen preview | 3-state: watches / screen / ray-painted, taken from the emulator render | 2-state: watches / ZX screen drawn by the debugger itself. Can show the alt screen. |
| Memory editors | mem, disk-phys, disk-log, cmos, nvram, comppal | mem, disk-phys, disk-log, cmos, nvram (**no comppal**) |
| beta128 `STAT` | `RdStatus()` (includes the HLD bit) | raw `status` register |
| Step | Z80 step + INT | Z80 step + NMI logic (ATM3 / Scorpion) + **TSConf frame, line and DMA interrupts** + video/TSU/DMA update + main-window flip |
| Step over on HALT | Stops at the interrupt handler entry | Stops at `PC+1`, after the handler returns |
| Continue key | Esc | **`` ` `` (TIL)**; closing the debugger window also continues |
| Condition variables | … | adds `RD`, `WR`, `MDT`, `PG0`–`PG3` |

### 0.2 Inherited unchanged from TDD-DBG-01

These sections apply as written, with `80` replaced by `157` wherever a row stride appears:

- §1.2 Font, §1.3 Attribute byte, §1.5 Frames (the frame list capacity is 50 here), §1.6 Named attributes
- §4.1 Registers, with these differences:
  - The format is `%6d` instead of `%6u`, which gives identical output for valid values.
  - `reg.a`…`reg.r` (Ctrl+A, H, P, …) **do** open the editor. The early return of the classic version is gone, so Classic quirk Q3 does not apply.
- §6.1 differs in one point: hex fields accept only the **raw virtual-key codes** `0–9` and `A–F`. Numpad digits are ignored, and there is no keyboard-layout translation.
- §4.2 Trace, except the HALT row in §4.2.4 (see §4.2 below)
- §4.3.1 Watches, §4.7 Stack, §4.9 Time delta, §4.10 AY
- §6.1 Input field, §6.2 Menu (see the centring note in §6.2 below), §6.3 In-screen dialogs (same coordinates), §6.4 Native dialogs
- §7.2 Dispatch algorithm, §8 Mouse (with the extensions in §8 below)
- §9.1–§9.3, §9.5, §9.6 Lifecycle, refresh, snapshots, multiple CPUs
- §10 Breakpoints (with the extra operands in §10 below), §11 Labels, §13 Data contract (extended in §13 below), §14 Porting guidance

---

## 1. Rendering model (overrides)

| Property | Value |
|---|---|
| Buffer | `txtscr[157*30]` characters + `txtscr[157*30…]` attributes |
| Visible columns | `conf.mem_model == MM_TSL` → **157** columns (1256 × 480 px). Otherwise **88** columns (704 × 480 px). Columns 88–156 are still painted, but they fall outside the client area. |
| Clear state | All 157 × 30 cells get `0xB1` with attribute `0x50` |
| Paint order | `regs → trace → memory → watches → stack → AY → pages → ports → beta128 → PC-history → TSConf board → time` |
| Transparent attribute `0xFF` | Used by the screen preview (§4.3) |
| Presentation | The debugger draws into an 8-bpp DIB and invalidates the window. `WM_PAINT` stretches it by `windowScale` (1 or 2). |
| Palette (fixed) | For index `i`: `level = (i & 8) ? 0xFF : 0xC0`, `R = (i & 2) ? level : 0`, `G = (i & 4) ? level : 0`, `B = (i & 1) ? level : 0`. Bright black is therefore black. |
| Frame pixels | Palette index `(c | 8) * 0x11`. Only the low nibble matters, so the colour is the bright variant of `c`. |

### 1.1 Debugger window and menu bar

The window is created at start-up. It is shown when the debugger is entered and hidden when it is left. Style: overlapped, caption, system menu, minimise button, not resizable. The client size is fixed by the visible grid × scale.

| Menu | Item | Action |
|---|---|---|
| **Monitor** | Load block | `mon_load` (TDD-DBG-01 §6.3) |
| | Save block | `mon_save` |
| | Fill block | `mon_fill` |
| | — | |
| | Ripper's tool | `mon_tool` |
| | — | |
| | Scale 2x ✓ | Toggle `windowScale` between 1 and 2, resize the window, show a checkmark when 2× |
| **Debug** | Continue | `mon_emul` |
| | — | |
| | Step into | `mon_step` |
| | Step over | `mon_stepover` |
| | Till return | `mon_exitsub` |
| | Run to cursor | `chere` |
| **Breakpoints** | Toggle breakpoint | `cbpx` (at the trace cursor) |
| | Manager | `mon_bpdialog` |

After any menu command the screen repaints. The window close button (`WM_CLOSE`) runs **Continue**.

A TUI re-implementation SHOULD offer the same commands, either as a top menu line or through a command palette. The row above the grid is not part of the 30-row layout.

---

## 2. Screen map

### 2.1 Column bands

```
x:  0 ─ 79  classic 80-column debugger (identical coordinates to TDD-DBG-01 §2.1)
    79      gutter
    80 ─ 86 PC history (content), title row 0
    87      gutter
    88 ─ 109  TSConf column 0 content (22 wide)   110 gutter
    111 ─ 132 TSConf column 1 content (22 wide)   133 gutter
    134 ─ 155 TSConf column 2 content (22 wide)   156 gutter
```

When the machine is not TSConf, the visible area ends after column 87, so the PC history panel and its right frame line (pixel column 696, in cell 87) are still visible.

### 2.2 Region table

| # | Panel | Content (x,y,w,h) | Title | Notes |
|---|---|---|---|---|
| 1–10 | All classic panels | as in TDD-DBG-01 §2.1 | as classic | Differences are listed in §4 |
| 11 | PC history | (80, 1, 7, 28) | `PC hist` @ (80,0) `W_TITLE` | new |
| 12 | TSConf board | 16 framed controls in 3 columns, base (88,0) | per control | §5; visible only when the model is TSConf |

Rows 28–29: the PC history frame bottom sits in row 29 (pixel row 464). In the TSConf band, row 28 is the gutter under the last controls. Row 29 is background everywhere.

The golden screens are in Appendix A (text) and Appendix B (attributes), plus the companion `tsconf.png` rendered at 2×.

---

## 3. Focus and Interaction Model (overrides TDD-DBG-01 §3)

### 3.1 Focusable Widgets and Traversal
In full parity with original Unreal Speccy and PentEvo debuggers, exactly **four widgets** are selectable/focusable:
1. **Registers (`regs`, ID 0):** CPU registers, flags, and interrupt modes.
2. **Disassembly (`trace`, ID 1):** Instruction disassembly stream and breakpoints.
3. **Memory (`memory`, ID 2):** Memory hex and ASCII dump / editors.
4. **Pages (`pages`, ID 3):** Memory page/bank slots (0..3).

All other panels—**`watches`**, **`ports`**, **`beta128`**, **`stack`**, **`bottom`** (time delta and AY registers), and **`tsboard`** (TSConf register and control card block)—are **non-focusable passive displays**. They never receive input focus, never turn blue (`W_SEL`), and cannot be focused via mouse click.

- **Forward Cycle (`Tab`):** `regs` (0) → `trace` (1) → `memory` (2) → `pages` (3) → `regs` (0).
- **Reverse Cycle (`Shift+Tab`):** `regs` (0) ← `trace` (1) ← `memory` (2) ← `pages` (3) ← `regs` (0).
- **Mouse Focus & Field Selection:**
  - Clicking anywhere inside one of the 4 focusable widgets immediately switches focus (applying the blue background `W_SEL = 0x17`) and positions the cursor:
    - `regs`: clicking on any register label or value cell selects that field in `regsCurs`.
    - `trace`: clicking on an instruction row selects that instruction's address in `traceCurs`.
    - `memory`: clicking in the hex dump ($x \in [39, 62]$) sets `memCurs` and activates Hex mode (`memAscii = false`); clicking in the ASCII column ($x \in [63, 70]$) sets `memCurs` and activates ASCII mode (`memAscii = true`).
    - `pages`: clicking on a bank row ($y \in [23, 26]$) sets `pagesCurs` ($0..3$) and `selBank`.
  - Clicking on any passive panel (`watches`, `ports`, `beta128`, `stack`, `bottom`, `tsboard`) does **not** steal focus and is ignored.
  - In-place edits are validated and committed if valid, or cleaned up if invalid, before switching focus.

### 3.2 Visual Focus States
- **Activated Widget:** The active widget receives the **blue background (`W_SEL = 0x17`)** across its body and labels. Its active field displays the **cursor highlight (`W_CURS = 0x30`)**.
- **Inactive Widgets:** Unfocused widgets display neutral dark background (`W_NORM = 0x07`) or default green background (`W_OTHER = 0x40`).
- No cursor is drawn in inactive widgets, while internal cursor coordinates are retained across focus switches.

### 3.3 Internal Field Navigation
Within the active widget, arrow keys (`↑`, `↓`, `←`, `→`) navigate between traversable fields:
- `regs`: 26 fields navigated via `regs_layout[]` adjacency table.
- `trace`: `↑` / `↓` move instruction cursor; `PgUp` / `PgDn` scroll by 21 instructions.
- `memory`: `←` / `→` move by byte/nibble; `↑` / `↓` move by 8-byte row; `PgUp` / `PgDn` by 96 bytes; `m` / `Ctrl+Tab` toggle Hex and ASCII sides.
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
| **`tsboard`** | Board control cards / registers | *Passive / Read-Only* | — | Passive display panel; non-focusable. |

### 3.5 Editing Lifecycle
1. **Initiation:** Typing a valid digit/character on an editable field or pressing `Enter` initiates editing mode. The field activates an in-place buffer with cursor highlight.
2. **Buffer Display:** As the user types, the pending characters replace the field content in real time.
3. **Editing Keys:** `Backspace` removes the last typed character.
4. **Validation (`Enter`):** The input buffer is parsed and range-checked according to the field's data type. If valid, the new value is applied to the backend immediately. If invalid, the edit is rejected without corrupting state.
5. **Cancellation (`Esc`):** Exits editing mode, discards the buffer, and restores the original value.

---

## 4. Panels: differences from Classic

### 4.1 Registers

Identical to TDD-DBG-01 §4.1.

### 4.2 Trace

- The layout, colours and scrolling are identical to TDD-DBG-01 §4.2.
- **Branch analysis:** `HALT` is classified as `BLKCMD`, not `HALTCMD`. The PC line gets no arrow, and `nextpc = PC + 1`.
- Everything else in the TDD-DBG-01 §4.2.4 table applies.

### 4.3 Watches and screen preview (overrides TDD-DBG-01 §4.3.2)

#### Modes

`show_scrshot ∈ {0, 1}`. The flag `scrshot_page_mask ∈ {0x00, 0x08}` selects the alternative screen.

| Action | Default key | Effect |
|---|---|---|
| `mon.scrshot` | Alt+S | Toggle 0 ↔ 1. Entering mode 1 resets the alt flag to 0. |
| `mon.scrshot_alt` | Alt+Shift+S | Toggle the alt flag (bit 3) |
| `mon.screen`, `mon.altscreen` | F9, Shift+F9 | Both behave the same way. If the preview is not on, turn it on with the normal screen. If it is already on, toggle the alt screen. |
| `mon.rayscreen` | Alt+F9 | **no operation** (the function body is commented out) |

- **Titles:** `watches`, `screen memory`, `screen memory (alt)`. The last one is 19 characters at (34,0).
- **`DOS` indicator:** the same as in Classic.

#### Preview pixels

The preview is drawn directly into the 296 × 208 px area of cells (34…70, 1…13), that is, pixel origin (272, 16).

1. Fill the whole area with palette index `ts.border & 7`. This is the TSConf border register, used for **every** model. **[quirk]**
2. Draw the 256 × 192 ZX screen at offset (+20, +8), which is absolute pixel (292, 24).
3. **Source page:** RAM page **7** if `((port7FFD XOR alt_mask) & 0x08) != 0`, otherwise page **5**. Standard ZX bitmap and attribute layout.
4. **Pixel colour:** if the bitmap bit is 1, `ink = (attr & 7) | ((attr & 0x40) >> 3)`. If it is 0, `paper = ((attr >> 3) & 7) | ((attr & 0x40) >> 3)`.
   - FLASH is ignored.
   - TSConf 16c, 256c and text modes are **not** rendered. The preview is always the ZX screen.
5. Then the text layer is drawn. The preview cells have attribute `0xFF` and are skipped. The frames are drawn last.

A TUI implementation SHOULD show the preview through a terminal graphics protocol; see TDD-DBG-01 §4.3.2 for the fallback.

### 4.4 Memory editors

- The cycle order for `mon.switchdump` (Ctrl+D) is `ED_MEM, ED_PHYS, ED_LOG, ED_CMOS, ED_NVRAM`. `ED_COMP_PAL` does not exist.
- **The window always paints 12 lines.** There is no `min(lines, 12)` clamp. All remaining spaces are at least 96 bytes, so rows are never left empty.
- Titles, cursor, editing and the disk editor behave as in Classic.

### 4.5 Ports

| Row y | Content |
|---|---|
| 3 | The **extended-port** table from TDD-DBG-01 §4.5, with two additions: **GMX** and **Phoenix** also use `1FFD`. The TSConf model has no entry, so it shows `cmos:%02X`. |

- `mon.setbank` (Alt+B) writes `7FFD` and also sets `ts.vpage = ts.vpage_d = (value & 8) ? 7 : 5`, then remaps.
- `mon.sethimem` (Alt+M) is the same as in Classic.

### 4.6 Beta 128

Row 7, `STAT:%02X`, shows the raw WD `status` register. The type-I HLD merge is not applied.

### 4.7 Stack, 4.9 Time, 4.10 AY

Identical to Classic.

### 4.8 Pages (interactive) — `(72,23,7,4)`, title `pages` at (72,22)

#### Names

Page names are resolved as in TDD-DBG-01 §4.8, with one addition: GMX also gets the `SVM  ` rule. On TSConf, RAM pages print as `RAM%2X`, where the page number is 0…FF.

#### Attributes per row `i` (0…3)

| Condition | Label `"%d:"` at cells 72–73 | Name at cells 74–78 |
|---|---|---|
| `showbank` and `i == selbank` (the bank cursor) | `(W_OTHEROFF & 0x0F) \| W_CURS` = `37` (magenta paper, white ink) | `W_CURS` = `30` |
| otherwise, and focus == BANKS | `W_OTHEROFF \| 0x10` = `57` (cyan paper, white ink) | `(W_BANKRO or W_BANK) \| 0x10` = `51` or `50` |
| otherwise | `W_OTHEROFF` = `47` | `W_BANKRO` = `41` or `W_BANK` = `40` |

#### Interaction when pages has focus

`pages` is strictly **read-only** (consistent with `ports` and `beta128`). Typing and `Enter` are rejected. Cursor keys allow inspecting the four slots:

| Action | Default key | Effect |
|---|---|---|
| `pages.up` / `pages.down` | ↑ / ↓ | `selbank = (selbank ∓ 1) & 3` |
| `reg.edit` / hex typing | Any | **Rejected (Read-Only)** |

#### Mouse

- A click on rows 23–26, columns 72–78, focuses BANKS, sets `selbank = row − 23`, and sets `showbank = 1`.
- **Any other click sets `showbank = 0`**, which hides the bank cursor even if focus stays on BANKS.

### 4.11 PC history (new) — `(80,1,7,28)`, title `PC hist` at (80,0)

| Property | Value |
|---|---|
| Lines | 28. Line `i` shows ring entry `(ptr − i) mod 32`. Line 0 (y = 1) is the **most recent**. |
| Format | `"%02X:%04X"` = page : address, 7 characters, attribute `W_OTHER` |
| Address | PC at every **M1 opcode fetch** of the main CPU. Every M1 cycle adds one entry, so prefix bytes such as `DD`, `FD`, `ED` and `CB` each add their own entry. |
| Page | `ts.page[(PC >> 14) & 3]`, the TSConf page register for that 16K window, captured at fetch time. On non-TSConf models this value is meaningless. **[quirk]** |
| Ring | 32 entries. Only the 28 newest are shown. Entries that have never been written show `00:0000`. |
| Refresh | Every repaint. When the debugger stops before an instruction, the newest entry is the last M1 cycle that already executed, **not** the current PC. |
| Interaction | None |

---

## 5. TSConf register board

### 5.1 Layout engine (`dbg_canvas` / `dbg_column` / `dbg_control`)

The board is built once, at start-up, by `init_regs_page()`.

**Placement:**

- **Canvas** base = (88, 0).
- **Columns** are created in order, each **23 cells wide**. Column `k` starts at `x = 88 + 23k`: 88, 111, 134.
- **Controls** are stacked in a column. Each control has a height `h` = number of content rows.
  - The first control's **title row** is `y = 0`.
  - After placing a control, the column's cursor moves down by `h + 1`.
  - So for each control: title row `y`, content rows `y+1 … y+h`, and the next title row is `y + h + 1`.
  - The title row doubles as the gutter holding the previous frame's bottom line and this frame's top line.

**Frame of each control:** `frame(colx, y+1, 22, h)`. The content is 22 cells wide. Cell `colx+22` is the gutter column.

**Drawing a control** starts with `draw_reg_frame(title[, regval])`:

1. Set the tab stops to `cols = {colx+0, colx+3, colx+12, colx+14}`.
2. Print the `title` at `(colx, y)` in `W_TITLE`.
3. If there is a raw register value:
   - print `=` in `W_TITLE` right after the title
   - print `%02X` in `W_REGVAL` (`51`, cyan paper, blue ink) right after the `=`
4. Move to row `y+1`, tab stop 0.
5. Record the frame, then fill the content rectangle `(colx, y+1, 22, h)` with spaces in `W_NORM` (`07`), or `W_SEL` (`17`) if the control is **active** (§5.3).

**Cursor primitives** (canvas-relative):

| Primitive | Effect |
|---|---|
| `next_row` | Tab stop 0, `y + 1` |
| `next_col` | Advance to the next of the 4 tab stops, wrapping modulo 4. The row does not change. |
| `move_x_to_len` | Advance `x` by the length of the last printed string |
| `move(dx, dy)` | Relative move |
| `set_xy(x, y)` | Relative to the control's column x and title row y |

Every printed string is truncated to 31 characters.

**Field primitives.** In this table, "active" means the control is highlighted. `N` = `W_NORM`/`W_SEL`. `EQ` = `W_EQ` `04` (black paper, green ink), or `W_EQ_ACTIVE` `14` (blue paper, green ink) when the control is highlighted. `BITS` uses the same pair (`04` / `14`).

| Primitive | Cells produced, relative to `colx`, on the current row | Ends with |
|---|---|---|
| `bits_range(b)` | At +0, 2 chars in `BITS`: the tens digit of `b` or a space, then the units digit (`0` if `b % 10 == 0`). At +2, `:` in `BITS`. If `b < 0`, 3 spaces instead. | Moves to tab 1 |
| `draw_bit(title, b, v)` | `bits_range(b)`; `title` at +3 (`N`); `=` at +12 (`EQ`); at +14, `ena` if `v & 1` else `dis` (`N`) | `next_row` |
| `draw_bit(title, b, table[], v)` | Same as above, but the value is `table[v]` | `next_row` |
| `draw_bit_h(title, b, v)` | Same as above, but the value is `%02X` | `next_row` |
| `draw_bit_d(title, b, v)` | Same as above, but the value is `%03d` (not used by the board) | `next_row` |
| `draw_hex16(title, b, v)` | Same as above, but the value is `%04X` | `next_row` |
| `draw_port(title, v)` | `title` at +0 (`N`, may contain leading spaces); `=` at +12 (`EQ`); `%02X` at +14 (`N`) | `next_row` |
| `draw_hex24(title, v)` | `title` at +0; `=` at +12; `%02X` of `v >> 14` at +14; `:` at +16 (`EQ`); `%04X` of `v & 0x3FFF` at +17 | `next_row` |
| `draw_hex8_inline(title, v)` | From the **current x**: `title` (`N`), `"= "` (`EQ`), `%02X` (`N`), `" "` (`N`). The cursor advances past what was printed. | stays on the row |
| `draw_dec_inline(title, v)` | Same as above, with `%03d` | stays on the row |
| `draw_hl_port(p, hi, lo, v)` | From the current x: `pH= hh ` `pL= ll ` `p= vvvv `. The labels and values are `N`; each `"= "` is `EQ`. | `next_row` |
| `draw_led(label, on)` | Move right 1. If `on & 1`, print `label` in `W_LEDON` (`50`, cyan paper, black ink) and advance. If off, print nothing and advance by `len(label)`, so the background `▒` shows through. | stays |

LEDs are always placed on the control's **title row**, to the right of the title.

### 5.2 Controls: exact cells, sources and decoding

All coordinates are **absolute**. "Reg" is the TSConf register number; the register is accessed through port `#nnAF`. Values are read live from the emulator's TSConf state at every repaint.

Some field names are the same in every row of a control, so they are given once above the table:

- **Column 0** (`colx = 88`): bits label at x = 88–90, name at x = 91, `=` at x = 100, value at x = 102.
- **Column 1** (`colx = 111`): bits label at x = 111–113, name at x = 114, `=` at x = 123, value at x = 125. `draw_port` titles start at x = 111.
- **Column 2** (`colx = 134`): bits label at x = 134–136, name at x = 137, `=` at x = 146, value at x = 148. `draw_port` titles start at x = 134.

#### Column 0

##### VConfig (Reg 00) — title row 0, content rows 1–6, `VConfig=` + `%02X`

| y | Bits label | Name | Value | Source / decoding |
|---|---|---|---|---|
| 1 | `76:` | `RRES` | 7 chars | bits 7:6 → `256x192`, `320x200`, `320x240`, `360x288` |
| 2 | ` 5:` | `NOGFX` | `ena`/`dis` | bit 5 |
| 3 | ` 4:` | `NOTSU` | `ena`/`dis` | bit 4 |
| 4 | ` 3:` | `GFXOVR` | `ena`/`dis` | bit 3 |
| 5 | ` 2:` | `FT_EN` | `ena`/`dis` | bit 2 |
| 6 | `10:` | `VMODE` | 4 chars | bits 1:0 → `ZX  `, `16c `, `256c`, `text` |

##### TSConfig (Reg 06) — title 7, rows 8–13

| y | Bits | Name | Source |
|---|---|---|---|
| 8 | ` 7:` | `S_EN` | bit 7 (sprites) |
| 9 | ` 6:` | `T1_EN` | bit 6 |
| 10 | ` 5:` | `T0_EN` | bit 5 |
| 11 | ` 3:` | `T1Z_EN` | bit 3 |
| 12 | ` 2:` | `T0Z_EN` | bit 2 |
| 13 | ` 0:` | `TS_EXT` | bit 0. **[quirk]** In the TSConf register map bit 0 is T0YS_EN. The label is misleading. |

Bits 4 (Z80_LP) and 1 (T1YS_EN) are not shown.

##### SysConfig (Reg 20) — title 14, rows 15–16

| y | Bits | Name | Source |
|---|---|---|---|
| 15 | ` 2:` | `CACHE_EN` | bit 2 |
| 16 | `10:` | `ZCLK` | bits 1:0 → `3.5M`, `7M`, `14M`, `unk` |

Bits 4:3 (AY clock) are not shown.

##### CacheConfig (Reg 2B) — title 17, rows 18–21

| y | Bits | Name | Source |
|---|---|---|---|
| 18 | ` 4:` | `EN_C000` | bit 3 |
| 19 | ` 4:` | `EN_8000` | bit 2 |
| 20 | ` 4:` | `EN_4000` | bit 1 |
| 21 | ` 4:` | `EN_0000` | bit 0 |

**[quirk]** The bits label says ` 4:` on every row. The values are correct. Improved mode: ` 3:`, ` 2:`, ` 1:`, ` 0:`.

##### MemConfig (Reg 21) — title 22, rows 23–27

| y | Bits | Name | Source |
|---|---|---|---|
| 23 | `76:` | `LCK128` | **[quirk]** The original prints `ena`/`dis` of **TSConfig.S_EN**. The real field is 2 bits: `00` = 512, `01` = 128, `10` = auto, `11` = 1024. The source code already has the unused table `d_lock = {"512","128","aut","1MB"}`. Improved mode should print `d_lock[bits 7:6]`. |
| 24 | ` 3:` | `W0_RAM` | `ena` when bit 3 == **0** (the original shows the inverted bit). **[quirk]** The meaning is suspicious. |
| 25 | ` 2:` | `W0_MAP` | `ena` when bit 2 (W0_MAP_N) == 0. The inversion is intentional: the field is negative logic. |
| 26 | ` 1:` | `W0_WE` | bit 1 |
| 27 | ` 0:` | `ROM128` | bit 0 |

#### Column 1

##### Bitmap (no raw value) — title `Bitmap` at (111,0)

- **LED** `EN` at (131,0) when `NOGFX == 0`.
- Content rows 1–3.

| y | Cells | Source |
|---|---|---|
| 1 | `VPage` at 111; `=` at 123; `%02X` at 125–126 | Reg 01, video page |
| 2 | 111 `XH`, 113 `= `, 115 `hh`, 117 ` `, 118 `XL`, 120 `= `, 122 `ll`, 124 ` `, 125 `X`, 126 `= `, 128 `vvvv`, 132 ` ` | Graphics X offset. Regs 03 (bit 0) and 02. `v` is 9 bits. |
| 3 | Same layout with `Y` | Graphics Y offset. Regs 05 and 04. |

##### Tiles0 — title row 4

- **LEDs** `Z_EN` at (126..129,4) when TSConfig.T0Z_EN is set, and `EN` at (131..132,4) when T0_EN is set.
- Content rows 5–7.

| y | Cells | Source |
|---|---|---|
| 5 | `T0GPage` at 111; `=` at 123; `%02X` at 125 | Reg 17. The display shows the line-delayed copy index 2 (`t0gpage[2]`). |
| 6 | `16:` at 111–113; `X` at 114; `=` at 123; `%04X` at 125–128 | Tile layer 0 X offset (9 bits, Regs 41:40). **[quirk]** The bits label `16:` is meaningless. |
| 7 | Same with `Y` | Regs 43:42 |

##### Tiles1 — title row 8, content rows 9–11

Same as Tiles0 with `T1…` names. LEDs come from T1Z_EN and T1_EN. `T1GPage` is Reg 18. The offsets are Regs 45:44 and 47:46.

##### PalSel (Reg 07) — title 12, rows 13–15

| y | Bits | Name | Value | Source |
|---|---|---|---|---|
| 13 | `76:` | `T1PAL` | `%02X` of `T1PAL << 2` | bits 7:6 × 4 |
| 14 | `54:` | `T0PAL` | `%02X` of `T0PAL << 2` | bits 5:4 × 4 |
| 15 | `30:` | `GPAL` | `%02X` | bits 3:0 |

##### Misc (no raw value) — title 16

- **LEDs** on row 16: `FDD` at 118–120 (FDDVirt bit 3), `FDC` at 122–124 (bit 2), `FDB` at 126–128 (bit 1), `FDA` at 130–132 (bit 0).
- Content rows 17–19. The titles are printed with 3 leading spaces, starting at x = 111, so the text starts at x = 114.

| y | Title | Value | Source |
|---|---|---|---|
| 17 | `   TMPage` | `%02X` at 125 | Reg 16, tile map page |
| 18 | `   Border` | `%02X` | Reg 0F, border colour (TSConf palette index) |
| 19 | `   FDDVirt` | `%02X` | Reg 29, virtual FDD mask |

##### FMAddr (Reg 15) — title 20, rows 21–22

| y | Bits | Name | Value | Source |
|---|---|---|---|---|
| 21 | ` 4:` | `FM_EN` | `ena`/`dis` | bit 4 |
| 22 | `30:` | `FM_MAPS` | `%04X` of `FM_ADDR << 12` | bits 3:0. This is the CPU address where the FPGA memory (palette, sprites, and so on) is mapped. |

##### MemPages (no raw value) — title 23, rows 24–27

The rows are `   Page0` … `   Page3`, each with `%02X`. They show Regs 10–13 (`ts.page[0..3]`), the RAM page in each CPU window.

#### Column 2

##### Sprites — title row 0, content row 1

Row 1: `SGPage` at 134, `=` at 146, `%02X` at 148. Source: Reg 19.

##### DMA — title row 2, content rows 3–17 (h = 15)

- **LED** `ACTIVE` at (150..155,2) when the DMA state is not `NOP`.

| y | Content | Source |
|---|---|---|
| 3 | `     SRC` at 134; `=` at 146; `PP` at 148; `:` at 150; `OOOO` at 151 | Programmed source address (Regs 1C:1B:1A), shown as `addr >> 14` : `addr & 3FFF` |
| 4 | `CURR SRC` … | Live source address of the running transfer |
| 5 | blank | |
| 6 | `     DST` … | Programmed destination (Regs 1F:1E:1D) |
| 7 | `CURR DST` … | Live destination |
| 8 | blank | |
| 9 | 134 `     NUM`, 142 `= `, 144 `hh`, 146 ` `, 147 ` LEN`, 151 `= `, 153 `hh`, 155 ` ` | DMANUM (Reg 28), DMALEN (Reg 26) |
| 10 | Same layout with `CURR NUM` | Live counters, low 8 bits |
| 11 | blank | |
| 12 | `CTRL` at 134; `=` at 146; `%02X` at 148 | Last DMA control byte (Reg 27) |
| 13 | ` 6:` `OPT` | ctrl bit 6 |
| 14 | ` 5:` `S_ALIGN` | ctrl bit 5 |
| 15 | ` 4:` `D_ALIGN` | ctrl bit 4 |
| 16 | ` 3:` `A_SZ` | ctrl bit 3 |
| 17 | `20:` `DDEV` | 7-character string from the table below, indexed by `(dev[2:0] << 1) + rw(bit 7)` |

DDEV table, index 0…15:

| 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| `unk` | `unk` | `RAM-RAM` | `BLT-RAM` | `SPI-RAM` | `RAM-SPI` | `IDE-RAM` | `RAM-IDE` |

| 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|
| `FIL-RAM` | `RAM-CRM` | `unk` | `RAM-SFL` | `unk` | `unk` | `unk` | `unk` |

Each entry is padded with spaces to 7 characters.

##### Interrupt — title 18, rows 19–23

| y | Content | Source |
|---|---|---|
| 19 | `HSINT` at 134; `=` at 146; `%02X` at 148 | Reg 22 |
| 20 | ` h` at 134, `= ` at 136, `%03d` at 138, ` ` at 141 | `HSINT × 2`, decimal |
| 21 | `VSINTH` … `%02X` | Reg 24 (bit 0) |
| 22 | `VSINTL` … `%02X` | Reg 23 |
| 23 | ` v` at 134, `= ` at 136, `%03d` at 138, ` ` at 141; then ` inc` at 142, `= ` at 146, `%03d` at 148, ` ` at 151 | `VSINT` (9-bit), decimal; `VSINT >> 4`, decimal |

##### IntMask (Reg 2A) — title 24, rows 25–27

| y | Bits | Name | Source |
|---|---|---|---|
| 25 | ` 2:` | `DMA` | bit 2 |
| 26 | ` 1:` | `LINE` | bit 1 |
| 27 | ` 0:` | `FRAME` | bit 0 |

**Column bottoms:** column 0 ends with a frame bottom in row 28; column 1 ends at row 28; column 2 ends at row 28. Row 29 is background.

### 5.3 Selection (mouse only)

When the left button is pressed at a cell with `mx ≥ 88`:

1. Find the first column with `colx ≤ mx ≤ colx + 23`. The right edge is inclusive and reaches one cell past the gutter. **[quirk]** The first cell of the next column therefore selects the column on its left.
2. Within that column, find the first control with `y ≤ my ≤ y + h`. This range includes the title row and the last content row.
3. That control becomes **active** and every other control becomes inactive. If the active state changed, the screen repaints.

A click with `mx < 88` deactivates all controls.

An active control:
- fills its content with `W_SEL`
- changes `EQ` and `BITS` cells to `14`

There is no editing and no keyboard interaction on the board.

**Improved mode:** give the board keyboard focus (as a fifth window) and allow editing a register by issuing `OUT (#nnAF), value`.

### 5.4 Refresh

- Every repaint reads the live TSConf state. The board shows the **current** register values, not the line-delayed `_d` copies. The one exception is `T0GPage`/`T1GPage`, which show index 2 of the delay line.
- Single-stepping in this fork advances the TSU/DMA/video state. The DMA `CURR` fields and the `ACTIVE` LED therefore update per instruction.

---

## 6. Modal subsystems (overrides)

### 6.2 Menus

Menus are centred on the **157-column** buffer:

- `x = (157 − w) / 2`
- `y = (30 − h) / 2`

**[quirk]** On non-TSConf models only 88 columns are visible, so centred menus are partly or fully clipped. For example, the 26-wide `Save data from memory...` menu sits at x = 65…90 and loses its right edge. Improved mode MUST centre within the **visible** width.

The in-screen dialogs (find, fill, ripper, load/save) use the fixed coordinates of TDD-DBG-01 §6.3, all at x < 40, so they are visible in both widths.

---

## 7. Keyboard (overrides)

### 7.1 Differences from the Classic catalogue

| Action | Default (this fork) | Note |
|---|---|---|
| `mon.emul` | **`` ` ``** (TIL) | Continue. `main.monitor` is also TIL. |
| `mon.cpu` | Ctrl+`` ` `` | Same as Classic |
| `mon.scrshot` | Alt+S | Toggles watches / screen memory |
| `mon.scrshot_alt` | Alt+Shift+S | **New.** Toggles the alt screen. |
| `mon.screen`, `mon.altscreen` | F9, Shift+F9 | Both toggle the in-window preview (see §4.3) |
| `mon.rayscreen` | Alt+F9 | No operation |
| `mon.maxspeed` | (unbound) | **New action.** Toggles max emulation speed. |
| `mon.switchdump` | Ctrl+D | Cycles through 5 editors |
| `mem.up` / `mem.down` / `reg.edit` | ↑ / ↓ / Enter | Also used by the **pages** window (§4.8) |

The window tables are **regs**, **trace**, **mem** and **banks**. Each one ends with all `mon.*` actions.

**Dispatch order:**
1. The focused window's action table
2. The fallback handler, which is one of `dispatch_regs`, `dispatch_trace`, `dispatch_mem` or `dispatch_banks`

**Leaving the debugger:** the emulator ignores keyboard input for 20 polls (`input.nokb = 20`). This stops the key that resumed execution from leaking into the emulated machine. A re-implementation SHOULD debounce in the same way.

---

## 8. Mouse (extensions)

- Cell coordinates are `mx = px / (8 × scale)` and `my = py / (16 × scale)`.
- The right-click context menu opens at the global cursor position. Its contents are the same as in Classic.
- **Pages** click: see §4.8.
- **TSConf board** click: see §5.3.
- Regs, trace and memory clicks: same as Classic.

---

## 9. Execution control (overrides TDD-DBG-01 §9.4)

**Step (F7)**

1. Set the time mark and the previous-state snapshot.
2. Reset the memory-cycle accounting for the new instruction.
3. Execute one instruction.
4. Handle the end of frame.
5. **NMI:**
   - Baseconf (ATM3) NMI trap when `PC == pBD` with `pBF` bit 4 set.
   - Pending NMI: on ATM3, enter the NMI immediately. On Scorpion/Profi-Scorpion, enter it once `PC > 0x4000` (the TR-DOS variant).
   - Baseconf NMI-exit countdown (`pBE`).
6. **INT:**
   - On TSConf, evaluate the frame, line and DMA interrupt sources, but not while the virtual DOS is active. `int_pend` is the OR of the pending sources.
   - On other models, `int_pend` is true while `t` is inside `[intstart, intstart + intlen)`, with wrap-around at the end of the frame.
   - Deliver the interrupt if `IFF1` is set, the previous instruction was not EI, and the ATM gate allows it.
7. Update the screen and TSU, the DMA and the ray position, then **refresh the main emulator window**. The user sees the beam advance.
8. Set `trace_curs = PC`.

**Step over (F8)**

- CALL, CALL cc or RST: same as Classic.
- LDIR, LDDR, CPIR, CPDR, INIR, INDR, OTIR, OTDR **and HALT**: run until `PC == PC + len`. For HALT, execution resumes after the interrupt handler returns.

**Continue:** `mon.emul`, the menu item Debug → Continue, or closing the window.

**Other commands:** run-to-cursor (F4), until-return (F11) and set-PC (Z) are the same as in Classic.

---

## 10. Breakpoints (extensions)

Additional operands for conditional expressions (TDD-DBG-01 §10.2):

| Operand | Meaning |
|---|---|
| `RD` | Address of a memory **read** performed by the current instruction, or `FFFFFFFF` if there was none |
| `WR` | Address of a memory **write**, or `FFFFFFFF` |
| `MDT` | The byte that was read or written by that access |
| `PG0` `PG1` `PG2` `PG3` | TSConf page registers for windows 0–3 |

**When conditions are evaluated.** In this fork the conditional breakpoints are evaluated **on every memory read and write** (`rm()`/`wm()`, including opcode fetches) as well as at each instruction boundary. So `RD`/`MDT` also see M1 fetches. `RD`, `WR`, `IN` and `OUT` are reset to `FFFFFFFF` once per instruction, in the instruction-boundary check, not after each evaluation.

Examples:
- `WR>=04000 && WR<05B00` breaks on a write to screen memory.
- `PG3==20 && PC==0C000` breaks on execution at C000 while page 0x20 is mapped. This gives a bank-aware execution breakpoint.

---

## 13. Data contract (extensions to TDD-DBG-01 §13)

```text
Snapshot += {
  model_is_tsconf: bool            // decides visible width 157 vs 88
  pc_history[32]: {page, addr}, pc_history_ptr
  tsconf: {
    vconf, tsconf, sysconf, cacheconf, memconf, palsel, fmaddr, intmask,
    vpage, g_xoffs(9), g_yoffs(9), t0gpage, t1gpage, t0_xoffs, t0_yoffs, t1_xoffs, t1_yoffs,
    sgpage, tmpage, border, fddvirt, page[4],
    dma: { saddr(prog), daddr(prog), num(prog), len(prog), ctrl, state_active:bool,
           cur_saddr, cur_daddr, cur_num, cur_len },
    hsint, vsint(9)
  }
  screen_preview_zx(page5|7) -> 256x192 indexed + border index
}
Commands += set_ts_page(window, value), out(port, value)   // board editing in improved mode
```

UI-only state added by this fork:
- `selbank`
- `showbank`
- the active board control
- `scrshot_page_mask`
- `windowScale`

---

## 14. Porting notes specific to this fork

- **Terminal width.** A TUI needs **≥ 157 × 30** for the TSConf layout. If the terminal is narrower, a TUI MAY:
  1. show the 88-column layout and move the TSConf board to a toggleable overlay or page, or
  2. reflow the three board columns below the main area. This needs ≥ 88 × 60.

  The golden layout is the 157-column one. Any reflow must keep each control's internal layout (22 × h) unchanged.
- **Board rendering.** Render the board from a **data table**: control → title, register, fields, where each field has a primitive, a label, bits and a decoder. Do not hard-code print calls. §5.2 is written so that it can be transcribed directly into such a table. The same table can then drive the ImGui, Qt and web clients.
- **LEDs in terminals.** An "off" LED MUST leave the background glyph visible, not blank it, to match the original.
- **Frames.** Apply TDD-DBG-01 §14.2 as-is. The gutter columns here are 79, 87, 110, 133 and 156, and there is a stacked gutter row between each pair of controls, which is the control's title row.

---

## 15. Known quirks (additions)

| # | Quirk | Faithful | Improved |
|---|---|---|---|
| T1 | Menus centred on 157 columns and clipped in 88-column mode | reproduce | centre within the visible width |
| T2 | `LCK128` shows S_EN instead of the lock mode | reproduce | `d_lock[bits 7:6]` |
| T3 | CacheConfig bits labels all ` 4:` | reproduce | 3, 2, 1, 0 |
| T4 | `TS_EXT` label for bit 0 (T0YS_EN) | reproduce | `T0YS_EN`; add T1YS_EN and Z80_LP rows if the height allows |
| T5 | Tiles X/Y rows labelled `16:` | reproduce | `  :` or none |
| T6 | Preview border uses the TSConf border register on every model | reproduce | the model's border (port FE bits 2:0) |
| T7 | Bank edit initial value undefined for named ROMs | — | use `ts.page[i]` |
| T8 | PC-history page meaningless on non-TSConf | reproduce | use the model's page for the window |
| T9 | `mon.rayscreen` / `mon_scr1` are no-ops | reproduce | ray-painted preview |
| T10 | `W0_RAM` shown inverted | reproduce | verify against the TSConf documentation before changing |
| T11 | Clicking outside the pages panel hides the bank cursor even though BANKS keeps focus | reproduce | keep the cursor visible while focused |

---

## 16. Conformance checklist (additions)

1. With the Appendix A state and `model = TSConf`, the 157 × 30 text dump and the attribute map match Appendices A and B exactly. With a non-TSConf model, columns 0–87 are identical to the TSConf case.
2. Tab cycles REGS → TRACE → MEM → BANKS. With BANKS focused, the pages panel shows cyan paper. ↑ and ↓ move the magenta bank cursor, and typing `2` then `0` then Enter sets page window N to 0x20 (TSConf only).
3. Clicking on DMA highlights the whole DMA control (blue content, `14` for the `=` cells). Clicking in the classic area clears the highlight.
4. Alt+S shows the ZX screen with a border of `ts.border & 7`. Alt+Shift+S switches to page 7 and the title becomes `screen memory (alt)`.
5. F8 on `HALT` stops after the interrupt handler returns, at `PC+1`.
6. The condition `WR==04000` breaks on the first write to 0x4000.
7. Scale 2× doubles the window with nearest-neighbour scaling, and mouse hit-testing still works.

---

## Appendix A — Golden text dump (157 × 30)

Reference state used for the golden dumps (from `sample_state()` in `unreal_dbg_render.py`):

- Current CPU 0. PC=`8011` (`CALL 8020`), SP=`BFFE` (word `8014` on stack), A=`07` F=`44` (Z,P set), BC=`17FF`, DE=`4001`, HL=`4000`, IX=IY=`5C3A`, I=`3F`, R=`12`, IM 1, IFF1=IFF2=0, T=`17023`, last branch `800F`, not halted.
- Previous snapshot differs in BC (0000), PC (800F), F (40) and R (10), so these are highlighted bright.
- Focus = trace, `trace_top=8000`, `trace_curs=8011`, `trace_mode=2` (mnemonic column). Execution breakpoint at `8022`. Branch at PC: CALL → target `8020` (arrow ↓ on PC line, ◄ on `8020`).
- Memory window: editor=memory, top=`C000`, cursor=`C003` hex side high nibble (cursor not drawn because memory is unfocused). Text `UNREAL SPECCY DEBUGGER` at `C000`; bytes `00..0F` at `4000`.
- Watches mode; user watches `4000 8000 C000`. DOS ports inactive.
- Ports FE=`07`, 7FFD=`10`, no extended port (Pentagon) → `cmos:00`, EFF7=`00`. TR-DOS present; WD cmd=80 data=00 status=20 sector=01 tracks 00/00 system=3C rqs=80.
- AY single chip, regs `1C 01 00 00 00 00 00 38 0F 00 00 00 00 00 FF BF`, latched reg 7. Time delta 12.
- TSConf variant: page names `ROM 0 / RAM 5 / RAM 2 / RAM 0`; PC history = the 28 most recent M1 fetches `800F` descending to `7FF4`, where the page is `ts.page` of the window (`02` for 8000–800F, `05` for 7FF4–7FFF); TSConf registers: VConfig=00, TSConfig=00, SysConfig=02 (14 MHz), CacheConfig=00, MemConfig=04, VPage=05, PalSel=0F, Border=07, Page0..3=00/05/02/00, IntMask FRAME=1, DMA idle; board has no active control; Bitmap EN LED lit (NOGFX=0).

Column ruler: first line = tens, second line = units. Row number at left. Glyphs mapped via Appendix D.

```text
     0         1         2         3         4         5         6         7         8         9         0         1         2         3         4         5      
     0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456
  0  ▒regs▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒watches▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒ports▒▒▒PC hist▒VConfig=00▒▒▒▒▒▒▒▒▒▒▒▒▒Bitmap▒▒▒▒▒▒▒▒▒▒▒▒▒▒EN▒Sprites▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒
  1  ▒af:0744 af'0000 sp:BFFE ir: 3F12▒ PC: CD 20 80 18 FE 00 00 00 ═ А↑■...▒  FE:07▒02:800F▒76:RRES     = 256x192 ▒VPage       = 05      ▒SGPage      = 00      ▒
  2  ▒bc:17FF bc'0000 pc:8011 t: 17023▒ SP: 14 80 55 4E 52 45 41 4C ¶АUNREAL▒7FFD:10▒02:800E▒ 5:NOGFX    = dis     ▒XH= 00 XL= 00 X= 0000 ▒DMA▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒
  3  ▒de:4001 de'0000 ix:5C3A im1,i:00▒ BC: 00 00 00 00 00 00 00 00 ........▒cmos:00▒02:800D▒ 4:NOTSU    = dis     ▒YH= 00 YL= 00 Y= 0000 ▒     SRC    = 00:0000 ▒
  4  ▒hl:4000 hl'0000 iy:5C3A sZ.h.Pnc▒ DE: 01 02 03 04 05 06 07 08 ☺☻♥♦♣♠•◘▒EFF7:00▒02:800C▒ 3:GFXOVR   = dis     ▒Tiles0▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒CURR SRC    = 00:0000 ▒
  5  ▒Z80(0)▒▒800F▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒ HL: 00 01 02 03 04 05 06 07 .☺☻♥♦♣♠•▒beta128▒02:800B▒ 2:FT_EN    = dis     ▒T0GPage     = 00      ▒                      ▒
  6  ▒8000 F3         di              ▒ IX: 00 00 00 00 00 00 00 00 ........▒CD:8000▒02:800A▒10:VMODE    = ZX      ▒16:X        = 0000    ▒     DST    = 00:0000 ▒
  7  ▒8001 3100C0     ld   sp,C000    ▒ IY: 00 00 00 00 00 00 00 00 ........▒STAT:20▒02:8009▒TSConfig=00▒▒▒▒▒▒▒▒▒▒▒▒16:Y        = 0000    ▒CURR DST    = 00:0000 ▒
  8  ▒8004 210040     ld   hl,4000    ▒BC': 00 00 00 00 00 00 00 00 ........▒SECT:01▒02:8008▒ 7:S_EN     = dis     ▒Tiles1▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒                      ▒
  9  ▒8007 110140     ld   de,4001    ▒DE': 00 00 00 00 00 00 00 00 ........▒T:00/00▒02:8007▒ 6:T1_EN    = dis     ▒T1GPage     = 00      ▒     NUM= 00  LEN= 00 ▒
 10  ▒800A 01FF17     ld   bc,17FF    ▒HL': 00 00 00 00 00 00 00 00 ........▒S:3C/80▒02:8006▒ 5:T0_EN    = dis     ▒16:X        = 0000    ▒CURR NUM= 00  LEN= 00 ▒
 11  ▒800D 3600       ld   (hl),00    ▒4000 00 01 02 03 04 05 06 07 .☺☻♥♦♣♠•▒stack▒▒▒02:8005▒ 3:T1Z_EN   = dis     ▒16:Y        = 0000    ▒                      ▒
 12  ▒800F EDB0       ldir            ▒8000 F3 31 00 C0 21 00 40 11 є1.└!.@◄▒-2:0000▒02:8004▒ 2:T0Z_EN   = dis     ▒PalSel=0F▒▒▒▒▒▒▒▒▒▒▒▒▒▒CTRL        = 00      ▒
 13  ▒8011 CD2080     call 8020      ↓▒C000 55 4E 52 45 41 4C 20 53 UNREAL S▒SP:8014▒02:8003▒ 0:TS_EXT   = dis     ▒76:T1PAL    = 00      ▒ 6:OPT      = dis     ▒
 14  ▒8014 18FE       jr   8014       ▒memory: C003 gsdma: 000000▒▒▒▒▒▒▒▒▒▒▒▒+2:4E55▒02:8002▒SysConfig=02▒▒▒▒▒▒▒▒▒▒▒54:T0PAL    = 00      ▒ 5:S_ALIGN  = dis     ▒
 15  ▒8016 00         nop             ▒C000 55 4E 52 45 41 4C 20 53 UNREAL S▒+4:4552▒02:8001▒ 2:CACHE_EN = dis     ▒30:GPAL     = 0F      ▒ 4:D_ALIGN  = dis     ▒
 16  ▒8017 00         nop             ▒C008 50 45 43 43 59 20 44 45 PECCY DE▒+6:4C41▒02:8000▒10:ZCLK     = 14M     ▒Misc▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒ 3:A_SZ     = dis     ▒
 17  ▒8018 00         nop             ▒C010 42 55 47 47 45 52 00 00 BUGGER..▒+8:5320▒05:7FFF▒CacheConfig=00▒▒▒▒▒▒▒▒▒   TMPage   = 00      ▒20:DDEV     = unk     ▒
 18  ▒8019 00         nop             ▒C018 00 00 00 00 00 00 00 00 ........▒+A:4550▒05:7FFE▒ 4:EN_C000  = dis     ▒   Border   = 07      ▒Interrupt▒▒▒▒▒▒▒▒▒▒▒▒▒▒
 19  ▒801A 00         nop             ▒C020 00 00 00 00 00 00 00 00 ........▒+C:4343▒05:7FFD▒ 4:EN_8000  = dis     ▒   FDDVirt  = 00      ▒HSINT       = 00      ▒
 20  ▒801B 00         nop             ▒C028 00 00 00 00 00 00 00 00 ........▒+E:2059▒05:7FFC▒ 4:EN_4000  = dis     ▒FMAddr=00▒▒▒▒▒▒▒▒▒▒▒▒▒▒ h= 000               ▒
 21  ▒801C 00         nop             ▒C030 00 00 00 00 00 00 00 00 ........▒10:4544▒05:7FFB▒ 4:EN_0000  = dis     ▒ 4:FM_EN    = dis     ▒VSINTH      = 00      ▒
 22  ▒801D 00         nop             ▒C038 00 00 00 00 00 00 00 00 ........▒pages▒▒▒05:7FFA▒MemConfig=04▒▒▒▒▒▒▒▒▒▒▒30:FM_MAPS  = 0000    ▒VSINTL      = 00      ▒
 23  ▒801E 00         nop             ▒C040 00 00 00 00 00 00 00 00 ........▒0:ROM 0▒05:7FF9▒76:LCK128   = dis     ▒MemPages▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒ v= 000  inc= 000     ▒
 24  ▒801F 00         nop             ▒C048 00 00 00 00 00 00 00 00 ........▒1:RAM 5▒05:7FF8▒ 3:W0_RAM   = ena     ▒   Page0    = 00      ▒IntMask▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒
 25  ▒8020 3E07       ld   a,07      ◄▒C050 00 00 00 00 00 00 00 00 ........▒2:RAM 2▒05:7FF7▒ 2:W0_MAP   = dis     ▒   Page1    = 05      ▒ 2:DMA      = dis     ▒
 26  ▒8022 D3FE       out  (FE),a     ▒C058 00 00 00 00 00 00 00 00 ........▒3:RAM 0▒05:7FF6▒ 1:W0_WE    = dis     ▒   Page2    = 02      ▒ 1:LINE     = dis     ▒
 27  ▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒05:7FF5▒ 0:ROM128   = dis     ▒   Page3    = 00      ▒ 0:FRAME    = ena     ▒
 28  ▒time delta:            12t▒AY:01C10120030040050060073880F900A00B00C00D00EFFFBF▒05:7FF4▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒
 29  ▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒
```

## Appendix B — Golden attribute map (run-length)

Format per row: `x0-x1:AA` = cells x0..x1 have attribute byte AA (hex). `FF` would mean transparent.

```text
 0  0:50 1-4:59 5-33:50 34-40:59 41-71:50 72-76:59 77-79:50 80-86:59 87:50 88-95:59 96-97:51 98-110:50 111-116:59 117-133:50 134-140:59 141-156:50
 1  0:50 1-5:07 6-7:0F 8-30:07 31-32:0F 33:50 34-70:40 71:50 72-78:40 79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-122:07 123:04 124-132:07 133:50 134-145:07 146:04 147-155:07 156:50
 2  0:50 1-3:07 4-7:0F 8-19:07 20-23:0F 24-32:07 33:50 34-70:40 71:50 72-78:40 79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-112:07 113-114:04 115-119:07 120-121:04 122-125:07 126-127:04 128-132:07 133:50 134-136:59 137-156:50
 3  0:50 1-32:07 33:50 34-70:40 71:50 72-78:40 79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-112:07 113-114:04 115-119:07 120-121:04 122-125:07 126-127:04 128-132:07 133:50 134-145:07 146:04 147-149:07 150:04 151-155:07 156:50
 4  0:50 1-29:07 30:0F 31-32:07 33:50 34-70:40 71:50 72-78:40 79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-116:59 117-133:50 134-145:07 146:04 147-149:07 150:04 151-155:07 156:50
 5  0:50 1-6:59 7-8:50 9-12:59 13-33:50 34-70:40 71:50 72-78:59 79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-122:07 123:04 124-132:07 133:50 134-155:07 156:50
 6  0:50 1-32:17 33:50 34-70:40 71:50 72-78:40 79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-113:04 114-122:07 123:04 124-132:07 133:50 134-145:07 146:04 147-149:07 150:04 151-155:07 156:50
 7  0:50 1-32:17 33:50 34-70:40 71:50 72-78:40 79:50 80-86:40 87:50 88-96:59 97-98:51 99-110:50 111-113:04 114-122:07 123:04 124-132:07 133:50 134-145:07 146:04 147-149:07 150:04 151-155:07 156:50
 8  0:50 1-32:17 33:50 34-70:40 71:50 72-78:40 79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-116:59 117-133:50 134-155:07 156:50
 9  0:50 1-32:17 33:50 34-70:40 71:50 72-78:40 79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-122:07 123:04 124-132:07 133:50 134-141:07 142-143:04 144-150:07 151-152:04 153-155:07 156:50
10  0:50 1-32:17 33:50 34-70:40 71:50 72-78:40 79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-113:04 114-122:07 123:04 124-132:07 133:50 134-141:07 142-143:04 144-150:07 151-152:04 153-155:07 156:50
11  0:50 1-32:17 33:50 34-70:40 71:50 72-76:59 77-79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-113:04 114-122:07 123:04 124-132:07 133:50 134-155:07 156:50
12  0:50 1-32:17 33:50 34-70:40 71:50 72-78:40 79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-117:59 118-119:51 120-133:50 134-145:07 146:04 147-155:07 156:50
13  0:50 1-16:70 17-31:30 32:3D 33:50 34-70:40 71:50 72-78:40 79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-113:04 114-122:07 123:04 124-132:07 133:50 134-136:04 137-145:07 146:04 147-155:07 156:50
14  0:50 1-32:17 33:50 34-59:59 60-71:50 72-78:40 79:50 80-86:40 87:50 88-97:59 98-99:51 100-110:50 111-113:04 114-122:07 123:04 124-132:07 133:50 134-136:04 137-145:07 146:04 147-155:07 156:50
15  0:50 1-32:17 33:50 34-70:07 71:50 72-78:40 79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-113:04 114-122:07 123:04 124-132:07 133:50 134-136:04 137-145:07 146:04 147-155:07 156:50
16  0:50 1-32:17 33:50 34-70:07 71:50 72-78:40 79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-114:59 115-133:50 134-136:04 137-145:07 146:04 147-155:07 156:50
17  0:50 1-32:17 33:50 34-70:07 71:50 72-78:40 79:50 80-86:40 87:50 88-99:59 100-101:51 102-110:50 111-122:07 123:04 124-132:07 133:50 134-136:04 137-145:07 146:04 147-155:07 156:50
18  0:50 1-32:17 33:50 34-70:07 71:50 72-78:40 79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-122:07 123:04 124-132:07 133:50 134-142:59 143-156:50
19  0:50 1-32:17 33:50 34-70:07 71:50 72-78:40 79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-122:07 123:04 124-132:07 133:50 134-145:07 146:04 147-155:07 156:50
20  0:50 1-32:17 33:50 34-70:07 71:50 72-78:40 79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-117:59 118-119:51 120-133:50 134-135:07 136-137:04 138-155:07 156:50
21  0:50 1-32:17 33:50 34-70:07 71:50 72-78:40 79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-113:04 114-122:07 123:04 124-132:07 133:50 134-145:07 146:04 147-155:07 156:50
22  0:50 1-32:17 33:50 34-70:07 71:50 72-76:59 77-79:50 80-86:40 87:50 88-97:59 98-99:51 100-110:50 111-113:04 114-122:07 123:04 124-132:07 133:50 134-145:07 146:04 147-155:07 156:50
23  0:50 1-32:17 33:50 34-70:07 71:50 72-73:47 74-78:41 79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-118:59 119-133:50 134-135:07 136-137:04 138-145:07 146-147:04 148-155:07 156:50
24  0:50 1-32:17 33:50 34-70:07 71:50 72-73:47 74-78:40 79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-122:07 123:04 124-132:07 133:50 134-140:59 141-156:50
25  0:50 1-31:17 32:1D 33:50 34-70:07 71:50 72-73:47 74-78:40 79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-122:07 123:04 124-132:07 133:50 134-136:04 137-145:07 146:04 147-155:07 156:50
26  0:50 1-32:12 33:50 34-70:07 71:50 72-73:47 74-78:40 79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-122:07 123:04 124-132:07 133:50 134-136:04 137-145:07 146:04 147-155:07 156:50
27  0-79:50 80-86:40 87:50 88-90:04 91-99:07 100:04 101-109:07 110:50 111-122:07 123:04 124-132:07 133:50 134-136:04 137-145:07 146:04 147-155:07 156:50
28  0:50 1-11:47 12-25:40 26:47 27:50 28-30:59 31:4F 32-33:40 34:4F 35-36:40 37:4F 38-39:40 40:4F 41-42:40 43:4F 44-45:40 46:4F 47-48:40 49:4F 50-51:40 52:4F 53-54:41 55:4F 56-57:40 58:4F 59-60:40 61:4F 62-63:40 64:4F 65-66:40 67:4F 68-69:40 70:4F 71-72:40 73:4F 74-75:40 76:4F 77-78:40 79:50 80-86:40 87-156:50
29  0-156:50
```

## Appendix C — Widget placement computed by the layout engine

| Control | Column x | Title row y | Content rows | Frame (x,y,w,h) |
|---|---|---|---|---|
| vconfig | 88 | 0 | 1–6 | (88,1,22,6) |
| tsconfig | 88 | 7 | 8–13 | (88,8,22,6) |
| sysconfig | 88 | 14 | 15–16 | (88,15,22,2) |
| cacheconfig | 88 | 17 | 18–21 | (88,18,22,4) |
| memconfig | 88 | 22 | 23–27 | (88,23,22,5) |
| bitmap | 111 | 0 | 1–3 | (111,1,22,3) |
| tiles0 | 111 | 4 | 5–7 | (111,5,22,3) |
| tiles1 | 111 | 8 | 9–11 | (111,9,22,3) |
| palsel | 111 | 12 | 13–15 | (111,13,22,3) |
| misc | 111 | 16 | 17–19 | (111,17,22,3) |
| fmaddr | 111 | 20 | 21–22 | (111,21,22,2) |
| mempages | 111 | 23 | 24–27 | (111,24,22,4) |
| sprites | 134 | 0 | 1–1 | (134,1,22,1) |
| dma | 134 | 2 | 3–17 | (134,3,22,15) |
| interrupt | 134 | 18 | 19–23 | (134,19,22,5) |
| intmask | 134 | 24 | 25–27 | (134,25,22,3) |


Font, glyph table and reference renderer: see TDD-DBG-01 Appendices C, D, E (identical font; same renderer script, variant `tsconf`).
