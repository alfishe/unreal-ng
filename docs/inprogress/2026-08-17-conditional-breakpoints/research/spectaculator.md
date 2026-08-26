# Spectaculator Debugger — Breakpoint System Research Report

Research based on the official Spectaculator 9.0.2 online manual (spectaculator.com/docs), the developer's blog post series on the v9 debugger rewrite, the 9.1 (2026) release notes, and screenshots of the actual dialogs (OCR'd from the official blog/docs images). Spectaculator's debugger was introduced in v6 (2003), "completely overhauled" in v9 (2013), and incrementally updated in 9.1 (May 2026).

## 1. Capabilities Matrix

| Capability | Supported | Notes |
|---|---|---|
| Execute breakpoints | Yes | "Breaks when the Z80's program counter reaches a specific address" |
| Memory read breakpoints | Yes | "On Memory Read" |
| Memory write breakpoints | Yes | "On Memory Write" |
| Memory read-or-write | Yes | "On Memory Read or Write" |
| I/O port read / write / both | Yes | With a **port mask** ("Since most Spectrum hardware does not fully decode the bus, a mask can be used to further restrict the port address") |
| Conditional expressions | Yes | C-like syntax; breakpoint fires only if address hit AND expression true |
| Hit counts | Yes | Break always / count equal / multiple of / equal-or-greater; live counter + Reset |
| Page/bank-restricted breakpoints | Yes | "They can be set anywhere in the Spectrum's memory, including a specific RAM page and ROM"; memory-access breakpoints "can be further restricted by memory page" |
| Enable/disable without deleting | Yes | Checkbox in breakpoints window (v9+) |
| Labels on breakpoints | Yes | "Friendly name" field |
| Breakpoint helpers (canned I/O breakpoints) | Yes | ULA $fe, paging register $7ffd, keyboard half-row, any key, Kempston $31/$df |
| Unlimited breakpoints | Yes | |
| Break on invalid opcode | Yes | Options → Advanced → "Break into debugger on unhandled instruction" (only while tracing) |
| Persist breakpoints to disk | Yes | Debugging Project files (*.dzx), auto-loaded next to snapshot files |
| Immune to self-modifying code | Yes | Explicitly documented: unlike HiSoft MONS, breakpoints aren't destroyed if the program overwrites the instruction |
| Scriptable / CLI debugger | No | GUI only |
| Breakpoint on interrupt/T-state | Partial | Via trace commands (Run until Interrupt, Run to Start/End of Frame, Run Until Condition), not as persistent breakpoints |

## 2. The Breakpoints Dialog (from official screenshots)

One unified "Breakpoints" dialog serves add and edit (New Breakpoint: Ctrl+B). Fields:

**Location group**
- **Break at:** — address field. Accepts decimal and four hex notations everywhere in the debugger: `32678`, `0x8000`, `8000h`, `$8000`, `#8000`. Expressions are also accepted where addresses are ("Expressions can be used in many places such as the address for the Memory Viewer, Breakpoint conditions or Run Until Condition").
- **Type dropdown** — 7 entries: `On Execute Instruction`, `On Memory Read`, `On Memory Write`, `On Memory Read or Write`, `On I/O Port Read`, `On I/O Port Write`, `On I/O Port Read or Write`.
- **Port Mask:** — appears for I/O types (e.g. Break at `$fbfe`, mask `$0401` for a keyboard half-row).
- **Enabled** checkbox.
- **Page restriction checkbox** — e.g. "**Break only when executing from RAM 0**". Note the form: it is a checkbox naming a page (the bank mapped at that address), not a free dropdown of every bank.
- **Label:** — free-text friendly name shown in the breakpoints window.
- **Condition:** — expression field (examples from screenshots: `(f & z_flag) != 0`, `pc > $3fff`, `bc == 0`).

**Hit Count group**
- "**When the breakpoint is hit:**" dropdown: `Break Always` / `Break if hit count equal` / `Break if hit count is a multiple of` / `Break if hit count is equal to or greater than` (+ N field).
- "**Current hit count:** n" readout with a **Reset** button.

**Breakpoints window** columns: enable-checkbox, Address, Type (abbreviated, e.g. `M/X` memory-execute, `IO/R`), Label, Hit Count, Condition. Toolbar: new (with helper dropdown arrow), delete, navigate-to-address. Delete key removes entries (9.1). Six ways to add: gutter click, Ctrl+Space at caret, Edit → New Breakpoint, toolbar button, Ctrl+B, Ctrl+right-click gutter (pre-fills address).

**Breakpoint states** (gutter/list icons): Enabled (solid dark-red circle), Disabled (ignored), **Error** — "There was an error when evaluating the breakpoint's conditional expression" (a distinct third state; bad expressions don't silently pass).

## 3. Conditional Expression Language

"Spectaculator has a C-like syntax for expressions used in the debugger." Case-insensitive; normal precedence with parentheses. Used in breakpoint conditions, watches, memory-viewer addresses, and Run Until Condition (F6).

- **Operators**: `* / + - %`, unary `-`; comparisons `== != < > <= >=`; logical `! && ||`; bitwise `& | ^ ~ << >>`.
- **No dereference operator** — "There is no dereference operator, `*`. Use the `rb()` or `rw()` functions instead":
  - `rb(expr)` — byte at address; `rw(expr)` — little-endian word; `rwb(expr)` — big-endian word.
- **Variables**: `AF BC DE HL AF' BC' DE' HL' IX IY PC SP I R`, all 8-bit halves incl. primed and `IXH IXL IYH IYL`, plus emulator state: `IM`, `IFF1`, `IFF2`, `NMIREQ`, `INTREQ`, `HALTED`.
- **Flag constants**: `S_FLAG` $80, `Z_FLAG` $40, `F5_FLAG` $20, `H_FLAG` $10, `F3_FLAG` $08, `P_FLAG`/`V_FLAG` $04, `N_FLAG` $02, `C_FLAG` $01 — so flag tests are written as masks: `(f & z_flag) != 0`.
- Doc example: "Break when the program counter (PC) is greater than $4000 and the Z80 Zero Flag is reset" → `pc > $4000 && !(f & z_flag)` style.
- **Hit counts are not expressions** — they're the separate structured Hit Count group (a design distinct from e.g. GDB's ignore counts embedded in commands).
- Notably absent: no variables exposing the current paging state (no `RAMPAGE`/`ROMPAGE` variable in the reference) — paging conditions are handled by the page checkbox and the $7ffd helper instead.

## 4. Memory Paging Awareness

What the documentation actually establishes:

- **Execute breakpoints**: "Regular breakpoints… can be set anywhere in the Spectrum's memory, **including a specific RAM page and ROM**." The dialog exposes this as the "Break only when executing from RAM n" checkbox — untick it and the breakpoint fires at that Z80 address regardless of which bank is paged in; tick it and it fires only when that specific bank is mapped.
- **Memory-access breakpoints**: "This can be further restricted by memory page."
- **Paging-register helper**: a one-click canned breakpoint on "Reading or writing to the Spectrum 128/+2's paging register (port **$7ffd**)" — the idiomatic way to catch bank switches.
- **Disassembly view**: shows the CPU's current 64K address space and tracks paging live — the 7.0 changelog fixed "display when memory is paged during single-stepping". There is **no documented bank-selector dropdown ("All RAM/ROM banks" style) in the disassembly/memory viewer** — a correction to the premise in the brief: that explicit bank-picker UI is characteristic of SpecEmu/ZX Spin, not Spectaculator. In Spectaculator, out-of-context banks are reached indirectly (page in via emulation, or via export).
- **Export**: File → Export can dump "from a specific RAM page" (page dropdown in the export dialog); 9.1 added Fill Memory / Export Memory / Paste File Contents.

So Spectaculator's paging model is "current-address-space view + per-breakpoint bank restriction", not "browse any bank at will".

## 5. UI Workflow Strengths and Limitations

**Strengths** (why it's considered the GUI-friendly one):
- "Browser like disassembly window with colour-coding and hyper-linked addresses" — click through JP/CALL targets, hyperlinks even on IX+d/IY+d operands, Ctrl-click opens in Memory Inspector; bookmarks, per-address comments, back/forward navigation.
- Very low-friction breakpoint creation (gutter click, Ctrl+Space) plus the **helper menu** that encodes Spectrum-specific knowledge (keyboard matrix half-rows with correct port masks, ULA, $7ffd, Kempston) so users needn't remember port decoding.
- Structured hit-count UI with a live counter and Reset — friendlier than expression-only debuggers.
- **Debugging Projects (.dzx)**: breakpoints, watches, labels, bookmarks, comments persist and auto-load alongside a snapshot — genuine session workflow, rare among Spectrum emulators of its era.
- Error state on bad conditions, self-modifying-code-proof breakpoints, unlimited breakpoints/watches, inline assembler with undo/redo, Graphics/Screen inspectors, rich trace menu (Step Into/Over/Out, Run to Cursor, Run until Interrupt, Run to Start/End of Frame, Run Until Condition).
- Still maintained: 9.1 (May 2026) added Fill/Export Memory, Memory Inspector undo, debugger-project MRU, cross-inspector change notifications.

**Limitations** (vs. power-user/text-based debuggers such as ZEsarUX, Fuse's debugger, MAME):
- Commercial, closed-source, Windows-only for the full debugger — no scripting, no remote/CLI interface, no automation of any kind.
- Expression language has no memory-paging variables, no T-state/scanline variables, and no dereference sugar; conditions are per-breakpoint only (no global watch-conditions on ranges — breakpoints are single addresses, not address ranges).
- No documented free browsing/disassembly of not-currently-paged banks (contrast: SpecEmu/ZX Spin bank dropdowns, ZEsarUX full bank views) — the biggest gap given its otherwise good paging story.
- Page restriction is a single checkbox bound to one named bank per breakpoint, not multi-bank selection.
- No trace log/instruction history export, no source-level (symbol file) debugging — labels are manual; no CSpect/sjasmplus map import.
- Developer's own historical assessment of the pre-v9 debugger: "it works, but it's pretty basic" — v9 fixed most of this, but community threads on Spectrum Computing ("SpecEmu or another emulator with a good Debugger?", feature requests like "Show previous instruction for breakpoint") indicate hackers wanting deeper facilities often pair it with or move to SpecEmu/ZEsarUX. (Forum thread bodies are login-walled; no verbatim user quotes extracted.)

## Sources

- [About the built-in Debugger / Monitor](https://www.spectaculator.com/docs/spectaculator/9.0/debugger/about.html)
- [About breakpoints](https://www.spectaculator.com/docs/spectaculator/9.0/debugger/breakpoints/about.html) · [Add a breakpoint](https://www.spectaculator.com/docs/spectaculator/9.0/debugger/breakpoints/addbreakpoint.html) · [Edit](https://www.spectaculator.com/docs/spectaculator/9.0/debugger/breakpoints/edit.html) · [Helpers](https://www.spectaculator.com/docs/spectaculator/9.0/debugger/breakpoints/helpers.html)
- [Expressions syntax reference](https://www.spectaculator.com/docs/spectaculator/9.0/debugger/expressions/reference.html) · [About expressions](https://www.spectaculator.com/docs/spectaculator/9.0/debugger/expressions/about.html)
- [Blog: New debugger features in Spectaculator 9 – Part 1: Breakpoints](https://www.spectaculator.com/2013/04/new-debugger-features-in-spectaculator-9-part-1-breakpoints/) (incl. dialog screenshots [1](https://www.spectaculator.com/wp-content/uploads/2013/04/breakpoints.jpg), [2](https://www.spectaculator.com/wp-content/uploads/2013/04/iobreakpoint1.jpg), [3](https://www.spectaculator.com/wp-content/uploads/2013/04/breakpoints_list.jpg))
- [What's new in 9.0.2](https://www.spectaculator.com/docs/spectaculator/9.0/getting_started/new.html) · [Spectaculator 9.1 released](https://www.spectaculator.com/2026/05/spectaculator-9-1-for-windows-released/) · [Spectaculator 7.0 released](https://www.spectaculator.com/2008/06/spectaculator-7-0-released/)
- Forum threads (login-walled, titles only): [SpecEmu or another emulator with a good Debugger?](https://spectrumcomputing.co.uk/forums/viewtopic.php?t=10152) · [Show previous instruction for breakpoint in debugger](https://spectrumcomputing.co.uk/forums/viewtopic.php?t=928)
