# Measurements 2026-10-30: banks, pages and the window's scale

Two requests of the owner after the follow-ups: show how the Z80's 64 KB are paged (the four 16 KB windows, which of
them the ports switch, what each page holds), and scale the whole window (fonts and everything measured in them)
without a performance cost. Raw data: `scratch/2026-10-30-poc-041-banks/` (`matrix/`, `measure-*.txt`,
`selfcheck-*.txt`). Window: [pages](window-pentagon-pages.png), [150 %](window-pentagon-zoom150.png).

## Banks and pages

- **The shim** gets `AddressWindow` (start, size, kind, page, name, role, switchable, writable, the device and offset
  holding it), `CPU::get_windows()`, and `Memory::page_size()` / `page_title()`.
- **In process**, the main Z80's windows come from the core's page map (`MapZ80AddressToPhysicalPage`,
  `IsWindowWritable`).
  - A ROM page is named by its signature (`ROM::CalculateSignature` → `GetROMTitle`), with the layout's role beside it
    (`GetROMPageRole`).
  - RAM 5 and RAM 7 carry "screen 0/1" on the models whose ULA shows them.
- **Over the WebAPI** the same comes from `/debug/snapshot` banks; the ROM at #0000 is named from `/state/paging`.
- **Which windows the ports switch**, read from the core's port decoders:

  | Models | Switchable windows |
  |--------|--------------------|
  | 48K | none |
  | 128K line (128K, +2, Pentagon, Scorpion, KAY, Quorum) | #0000 and #C000 |
  | +2A/+3, TS-Conf, ZX-Evo, ATM, Profi, Sprinter, Next | all four |

- **The GS / NeoGS Z80**: #0000 ROM and #4000 RAM 0 are fixed and #8000 / #C000 are paged by port #00.
  - On the NeoGS, #0000 is flash or RAM (GSCFG0.NOROM) and #8000 / #C000 are paged by MPAG / MPAGEX.
  - The classic card's ROM is no device memory, so its window lists no page.
- **In the window**:
  - A strip above the listing and above each processor's dump: a segment per window with the address, page,
    ⇄ (paged) or "fixed", and what the page holds.
  - A page that changed since the last stop is shown in the changed colour.
  - A click goes to the window's start; the tooltip gives the range, r/w, the layout's role and the device offset.
- **Boundaries in the listing and the dump**: a line where a window starts, with its label at the right; the label is
  part of the row's key, so a page switch repaints only the rows that show it.
- **The "pages" tab** replaces the separate ram / rom / card tabs.
  - A tree on the left: a group per paged memory ("8 pages of 16 KB"), a row per page with where it is mapped now
    ("cpu #C000, gs #8000", bold) and what it holds. A ROM's name is known once it has been mapped.
  - The dump of the chosen page at offsets 0000-3FFF; switching pages keeps the cursor's offset, so two pages compare
    line by line.
- **Fixed on the way**: the remote adapter read `/memory/page/{kind}/{n}` without `length` and got 128 bytes, so the
  ram and rom dumps over the WebAPI showed FF past the first 128 bytes of a page. The new check found it; pages are
  now read whole.

## The window's scale

- **What it is**: a scale of the fonts, not of the pixels.
  - `dbg::font()` (Consolas 10 pt) is multiplied by the scale; the window's own font (tabs, headers, labels) is set
    to its 100 % size times the scale.
  - Every view measures its rows and columns again on `FontChange`: the listing, dump, registers, flags and stack.
  - Item views lay out again, header sections are multiplied by the change, and toolbar and button icons follow.
  - The window keeps its size, and the splitters keep their proportions.
- **Steps**: 50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200, 250 and 300 %.
- **Keys**: Ctrl+= / Ctrl++, Ctrl+-, Ctrl+0, and Ctrl + the wheel (in 120ths, so a touchpad works).
- **Display and setting**: the scale shows in the toolbar and is kept in the setup (`Debugger/zoom`); `--zoom 1.5`
  sets it from the command line.
- **Drawing**: text is drawn at the scale, so it stays sharp; nothing is scaled as a picture.

Cost, Pentagon with NeoGS, `--measure 10`, the window following the run:

| Scale | frames/s | process CPU | GUI thread | debugger paints/s | debugger px/s |
|-------|---------:|------------:|-----------:|------------------:|--------------:|
| 100 % | 48.79 | 3.9 % | 0.9 % | 25.2 | 285 562 |
| 150 % | 48.90 | 3.8 % | 0.8 % | 19.6 | 253 798 |

Paused, nothing is painted at either scale. At 150 % fewer rows fit, so fewer pixels change per tick; the partial
repaint keeps working, because the painted rows are cleared on the change and keyed as before.

A switch from 100 % to 200 % takes 14-20 ms (relayout and one full paint, measured in the self-check).

## Self-check

New checks:

- every window's page holds the bytes the CPU reads there (#0000, #x123 and the last byte of each window);
- strips above the listing and the dump;
- the pages tab marks the page at #C000 and shows it;
- the GS windows: each listed page holds its bytes, and every RAM window is listed;
- at 200 %: fonts, rows and icons are twice the size, and every view has the scaled font;
- "Smaller" steps to 175 %, and 100 % gives every size back.

| Run | Checks passed |
|-----|--------------:|
| Pentagon + NeoGS | 31 / 31 |
| Pentagon + MultiSound | 31 / 31 |
| 48K | 26 / 26 |
| 128K | 26 / 26 |
| TS-Conf | 29 / 29 |
| Sprinter | 25 / 25 |
| ATM3 | 31 / 31 |
| Scorpion | 31 / 31 |
| Profi | 25 / 25 |
| +3 | 26 / 26 |
| ZX Next (eight 8 KB MMU slots) | 25 / 25 |
| unreal-qt over the WebAPI (Pentagon) | 25 / 25 |

The counts differ by model because checks that need a feature the model lacks (a card's CPU, a CALL to step over) are
skipped. "Run to cursor" and the breakpoint at #0038 failed in 6 of 32 repeated runs: two races, both fixed.
- The run-to breakpoint went at "the next stop" seen by the event handler, and the event of the stop it was set at
  could come after it was set, so it was removed before the run began. It now goes only at a stop after the run starts
  (the debug seq moved by the run start and the stop), in process and over the WebAPI.
- A run-to that outlived its stop (a CPU in HALT at its address) stopped the next run at once. Every new command
  (run, step, step over / out, run to) now removes an earlier run-to first.

After the fixes: 0 failures in 30 repeated runs (ATM3, TS-Conf, Pentagon, +3, Next) and the matrix above. The
listing's typed-over check failed once on the Next and passed 12 reruns; it now prints the row, the editor and the
bytes when it fails.

## Zoom classes and the strip

Not everything follows the scale at the same pace (`dbg::Zoom`, `debugstyle.h`): a size is
`clamp(zoom ^ exponent, min, max)` times its 100 % one.

| Class | What | Exponent | Min | Max | At 200 % |
|-------|------|----------|-----|-----|----------|
| Content | listing, dump, registers, their columns | 1.0 | 0.5 | 3.0 | 2.0 |
| Frame | tabs, buttons, labels, column titles: one font in the window | 0.5 | 0.85 | 1.6 | 1.41 |
| Icons | toolbar and button icons | 0.75 | 0.75 | 2.0 | 1.68 |

The frame's font is set on each widget of it, as the platform gives tab bars and buttons fonts of their own. The tabs
are drawn by Fusion: the macOS style keeps them at one height whatever the font.

The bank strip: windows of one size are segments of one width to the pixel (a unit of the smallest window, the
pixels left over go to the PC chip). Its text is at the views' size when it fits and at the largest smaller size it
fits in when not, so it never grows past the segment. Each kind of memory has a hue (ROM warm, RAM cool, flash and
cache violet); the chip at the left shows the PC and goes to it on a click, its window is underlined in the chip's
colour.

## Labels in the listing

When the label manager holds labels, the listing has a column for them between the bytes and the instruction, 12
characters wide; without labels there is no column. A longer label fades out over its last two characters at the
column's edge, and the tooltip over it shows it whole. New or removed labels decode the lines again. The WebAPI
client has no labels yet (its disassembler is the plain one).

The strip never cuts its text with an ellipsis: the address and the page fit by the font's size, what the page holds
gets down to three quarters of it and is left out past that (the tooltip has it).

## Review fixes

An independent read of the diff found, and these are fixed:
- ZX Next: the window's offset ignored the MMU's 8 KB slot (`addressInPage`), so a slot holding an odd 8 KB page
  showed the wrong bytes' owner. The Next now has eight 8 KB windows, each with its own page. Over the WebAPI it still
  has four 16 KB ones: `/debug/snapshot` sends four windows (a core limit, not the POC's).
- The dump's strip and window lines stayed stale while the tab was hidden; they are read again when it is shown.
- The scale was compared against the process-wide font scale, so a second window could keep tabs and icons at 100 %;
  each window now keeps its own.
- Columns drifted a pixel per zoom step there and back; each keeps its 100 % width now.
- A second Ctrl+C ends the process (the first one ends the event loop), and the waits stop at once.
- ROM pages over the WebAPI: the real count (`/state/memory/rom`, 4 on Pentagon instead of the core's 128) and every
  page's name. ROM names follow the page's content (its signature now, not a name cached per page), so a flash write
  or an edit renames the page.
- The 48K's page 7 is no "screen 1".

## The address menu and the listing's header

- One menu for an address wherever one is shown (`addressmenu.{h,cpp}`), after IDA's jump and x64dbg's follow in
  disassembler / dump, run to cursor and set new origin. The title shows the address, its window and page, and what it was
  shown as (`HL = #5C3A  RAM 5 + #1C3A`). The actions are: show in disassembly, show in memory, show its page (the pages
  tab at that byte), run to here, set PC here, set or remove a breakpoint, and copy the address, byte or word.
- It is in the listing (with a `Target #xxxx` submenu for a jump or call), the dump, the registers, the stack, the
  breakpoints, the history and the bank strip (a window's start; the PC chip).
- The views ask the window through an `AddressRouter`, a child of the window: no view knows another one.
- The listing has a header that stays put as the rows scroll: Addr, Bytes, Label, Instruction. Dragging the edge of
  Label sets the column's width in characters (4 to 64, `Debugger/label_chars` in the setup); the other sections
  follow the view's columns.
- Self-check `address menu: listing, memory, page, set PC` drives the menu's actions. Matrix: 11 models, 10 green on
  the first run; PLUS3 failed once at `listing: instruction bytes typed over` (row of #C000 not found, a run beside a
  screenshot run), then passed 5 runs out of 5.

## The port is ours

ecat is no longer an upstream (owner, 2026-10-30). The views are changed as unreal-ng needs; the diff against ecat is
no longer kept small or measured.
