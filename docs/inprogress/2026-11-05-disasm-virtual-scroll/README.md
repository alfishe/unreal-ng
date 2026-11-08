# Disassembly: a virtual 64K window, decoded on demand

Status: design, 2026-11-05. Owner decisions:

- the window logic lives in core (shared by Qt, TUI, eCat, WebAPI / MCP); core first, then the Qt view;
- no listing of the whole 64K: the scrollbar is a virtual window over the address space and only what is shown is
  disassembled, on demand (much of the memory is not code at all).

## 1. The problem

The Qt disassembler (`unreal-qt/src/debugger/disassemblerwidget.cpp`) is a `QTextEdit` that gets a new HTML page
of N rows on every refresh (`setHtml`). Scrolling (`navigateUp` / `navigateDown`, the wheel, the keys) moves the
top address and renders the page again.

| Symptom | Cause |
|:--|:--|
| No scrollbar, no idea where in 64K the view is | the view knows only its top address and N rows |
| Up then down does not come back to the same rows; the view jumps on data | `getPreviousCommandAddress` guesses the previous instruction (tries 4..1 bytes back, first fit wins) |
| No pixel scroll, no smooth scroll | the rows are text lines of an HTML page |
| Hit tests through a side map | `m_addressMap` (text line -> address), patched for long-label lines |
| Every refresh parses HTML | `setHtml` of the whole page, then extra selections for PC, breakpoints, cursor |
| TUI, eCat and agents disassemble on their own | `GET /disasm` gives N lines from an address, forward only |

Forward is easy: an instruction start gives the next one. Backward is ambiguous: Z80 instructions are 1-4 bytes,
and `3E 01` is `ld a,1` from its first byte and part of `ld bc,...` from a `01` two bytes earlier.

## 2. Requirements

| ID | Pri | Requirement |
|:--|:--|:--|
| W1 | P0 | **On demand.** Only the rows shown (and a small margin) are decoded; nothing is built for memory nobody looks at. |
| W2 | P0 | **Virtual 64K.** The scrollbar is the address of the top row over `0000-FFFF`; a drag lands on an address and snaps to an instruction start near it. |
| W3 | P0 | **Reversible.** Scrolling up N rows then down N rows shows the same rows, on code and on data alike. |
| W4 | P0 | **Right where it matters.** Rows at PC, labels, execution breakpoints and (with tracking on) executed instruction starts are instruction starts. |
| W5 | P0 | **Fast.** A step redraws in under 16 ms (G30 of the debugger model); scrolling never stutters. |
| W6 | P0 | **Zero cost with no view.** No hook on the memory write path, no background work. |
| W7 | P0 | **Every plane.** Rows by address over CLI, Python, Lua, MCP, REST and WebSocket (`docs/guidelines/automation-planes-guidelines.md`). |
| W8 | P1 | **Label rows.** A label wider than its column gets a row of its own (as the Qt view since db552283b). |
| W9 | P1 | **Card CPUs**, through `CpuView`. |
| W10 | P1 | **Data rows.** Data regions of the memory markup (user, profile, analysis, runtime) render as `db` rows. |
| W11 | P2 | **Jump arrows** for the visible rows. |

## 3. Core: `DisassemblyWindow`

`core/src/debugger/disassembler/disassemblywindow.h`. A function of (memory as mapped now, anchors, top address,
row count); no state of its own beyond a small cache.

### 3.1 Forward

From the top address, decode row after row until the window is full: an instruction row, a label row before it
when the label is long (W8). This is what `GET /disasm` does now. When the next instruction would run across an
anchor (§3.3), it is cut: the bytes up to the anchor become a `db` row and decoding resyncs at the anchor.

### 3.2 Backward: a local sync, never a global one

To show K rows above address A (scroll up, centre PC):

1. Take a short stretch below A: from the nearest anchor at most 64 bytes below, else A - 32.
2. Decode forward from each start offset in that stretch; keep the chains that land exactly on A.
3. Pick one: the chain from an anchor wins; else the chain most offsets agree on (Z80 chains converge within a few
   instructions, so most starts give the same last rows); else A - 1 as a `db` row.
4. Its last K rows are the rows above A. Need more: repeat from the first of them.

The cost is about 32 short decodes per screen scrolled up, microseconds.

### 3.3 Anchors

The addresses known to start an instruction, looked up only for the stretch being decoded:

- PC and the recent PCs (the CPU's PC history, as `pchist` of the debugger snapshot);
- labels; execution breakpoints;
- the memory markup ([../2026-11-05-software-metadata-profile/README.md](../2026-11-05-software-metadata-profile/README.md)):
  `MemoryMarkup::IsInstructionStart` (code region starts, entries, runtime opcode fetches with tracking on), and its
  data regions, which become `db` rows (W10) instead of instructions.

### 3.4 Reversible scrolling (W3)

A client keeps the strip of rows it has shown (top address + rows, a few screens). Scrolling inside the strip
reuses its rows, so up then down gives the same rows; past its ends the strip grows by §3.1 / §3.2. The strip is
dropped when its bytes change: a 64-bit hash of the strip's bytes, checked on refresh (a write, a page switch, a
TTD seek), or when an anchor appears inside it.

### 3.5 Queries

| Call | Gives |
|:--|:--|
| `Rows(address, count)` | rows from an address forward |
| `RowsBefore(address, count)` | rows that end at an address (§3.2) |
| `Around(address, before, after)` | both, the "follow PC" view |
| `SnapToInstruction(address)` | the instruction start for a scrollbar drag: the address itself when an anchor or a converged chain says so, else the chain start of §3.2 |

Each row: address, bytes, length, kind (instruction / label / db), label, mnemonic, runtime hints, jump target,
breakpoint, PC flag.

## 4. Planes

| Plane | Form |
|:--|:--|
| REST | `GET /disasm` gains `before=` (rows above the address, §3.2) and `snap=1`; `cpu=` as today |
| WebSocket | the same as a `call` |
| MCP | `debug_code disassemble` gains `before` |
| CLI | `disasm <address> [count] [before]` |
| Python / Lua | `disasm(address, count, before=0)` |

Docs chain: `command-interface.md` -> the disassembler module doc -> plane docs.

## 5. The Qt view

`QAbstractScrollArea` with a painter, replacing the `QTextEdit`:

- vertical scrollbar `0000-FFFF` = the top row's address; wheel and keys move rows through the strip (§3.4); a drag
  sets the address and snaps (`SnapToInstruction`);
- `paintEvent` draws the visible rows only (fixed character columns); a pixel offset inside the top row for smooth
  scroll;
- hit test is `y / lineHeight` into the strip, no side map;
- PC, breakpoints, cursor, marks, hover and the label column are painted, not extra selections;
- follow PC keeps PC inside the margins (`Around`);
- every current feature stays: cursor and marks, history back / forward, follow operand, go to address, context
  menu, breakpoint clicks, byte scroll mode (the top address moves by one byte, decoding starts there).

The unused REDasm port (`disassemblertextview.*`, `disassemblerlistingview.*`) is a source of ideas (arrows), not
a base.

## 6. Open points

- How a `db` row cut before an anchor looks next to an overlapping instruction.
</content>
