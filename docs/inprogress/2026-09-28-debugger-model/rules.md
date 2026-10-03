# Rules

- **Date:** 2026-09-28
- **Status:** draft for review.
- **Part of:** [the debugger model](README.md). Widgets and fields are in
  [widget-catalog.md](widget-catalog.md); transport is in
  [protocol.md](protocol.md).

These rules hold in every skin and on every automation surface. A skin may
present them differently; it may not behave differently. "U" refers to
[TDD-DBG-01](../2026-09-24-tui-debugger/TDD-DBG-01_unreal-speccy-debugger-tui.md),
"T" to [TDD-DBG-02](../2026-09-24-tui-debugger/TDD-DBG-02_unreal-tsconf-debugger-tui.md),
and requirement IDs (S1, F1, ...) to
[gs-debugger/requirements.md](../2026-09-27-gs-debugger/requirements.md).

## Contents

- [1. The debug session](#1-the-debug-session)
- [2. Run control and stepping](#2-run-control-and-stepping)
- [3. Several CPUs, one clock](#3-several-cpus-one-clock)
- [4. Snapshots and refresh](#4-snapshots-and-refresh)
- [5. Breakpoints](#5-breakpoints)
- [6. Focus and keys](#6-focus-and-keys)
- [7. Editing](#7-editing)
- [8. Change marks and the time mark](#8-change-marks-and-the-time-mark)
- [9. Labels](#9-labels)
- [10. Formats](#10-formats)
- [11. Several front-ends at once](#11-several-front-ends-at-once)
- [12. Messages](#12-messages)
- [13. Classic and improved](#13-classic-and-improved)

## 1. The debug session

- **Attached.** A front-end is *attached* while it shows any debugger widget,
  or holds a debug connection (GDB, DeZog, an event subscription). While at
  least one front-end is attached:
  - the CPUs run their **debug path**: breakpoint checks, change tracking and
    the instruction-start record;
  - with nothing attached, they run the fast path, with no debugger cost
    (P1).
- **Not modal.** Unlike the Unreal monitor (U §9.1), a GUI debugger is not
  modal: the emulator window stays live, and the debugger windows stay open
  while the machine runs. The classic skin may keep its modal behavior: it
  enters on a pause and leaves on Continue.
- **What pauses the machine** (U §9.1, extended). Any of these stops the
  **whole** machine, all CPUs (S1):

| Trigger | Stops | CPU state |
|---|---|---|
| Pause (a front-end, a key, automation) | at the next instruction boundary of the main CPU | main on a boundary |
| Execution breakpoint | before the instruction runs | that CPU on a boundary |
| Memory read / write breakpoint, main CPU | during the access; the machine stops inside the instruction | main inside an instruction |
| Memory / port breakpoint, card CPU | after the instruction that made the access (reported with the address, value and PC) | card on a boundary |
| Port breakpoint, main CPU | during the port access | main inside an instruction |
| Condition breakpoint | when it evaluates to non-zero (§5.3) | on a boundary |
| Event breakpoint (INT, NMI, reset, page switch, DMA, …) | when the event happens | per event, see §5.4 |
| Protocol breakpoint, host side | at the Spectrum's port access | main inside an instruction |
| Protocol breakpoint, card side | at the firmware's next instruction boundary | card on a boundary |
| Step / run-to target reached | at the target | on a boundary |
| TTD seek / reverse step reached | at the target | as recorded |

- **Who stopped it.** Every pause has a *reason*: the CPU, the kind
  (`pause`, `breakpoint`, `step`, `run_to`, `event`, `protocol`, `ttd`), and
  the breakpoint ID when there is one. Every front-end shows it (T5).
- **On a pause** (U §9.1): the stopping CPU's disassembly cursor goes to its
  PC; the stop targets are cleared; the other CPUs are aligned (§3.2).

## 2. Run control and stepping

**Run control is exclusive.** One client at a time may hold run control. GDB
takes it when it attaches. While a client holds it, the other clients'
run-control commands are refused, with the holder named
(`run control held by gdb`). Reading is always allowed.

| Command | CPU | Behavior | Origin |
|---|---|---|---|
| **Continue** | all | Set the time mark and the previous snapshots for every CPU (§8), then resume the machine. | U §9.4 |
| **Pause** | all | Stop at the next boundary of the main CPU; align the others (§3.2). | new |
| **Step** | the chosen CPU | Set that CPU's mark and previous snapshot. Run one instruction of that CPU, including a pending interrupt it accepts at that boundary (U §9.4). The other CPUs keep relative time (§3.3). Stay paused. | U §9.4 |
| **Step over** | the chosen CPU | A taken `CALL` / `CALL cc` or `RST`: run until that CPU returns to `next_pc` with SP back at its value (U §9.4 `dbg_stopsp` safety net). A block instruction: run until `next_pc`. `HALT`: classic stops at the interrupt handler entry (U §9.4, quirk Q6); improved stops after the handler returns, at `PC+1` (T §9). Anything else: as Step. | U §9.4, T §9 |
| **Step out** | the chosen CPU | Run until the current routine returns: PC equals the word at SP when the command was given, and SP is above its value then. | U §9.4 (`mon.exitsub`) |
| **Run to cursor** | the chosen CPU | Run until that CPU's PC equals the address. With a page given, the page must match too. | U §9.4 |
| **Set PC** | the chosen CPU | PC = the address; nothing runs. | U §9.4 |
| **Step the other CPU** | the other CPU | Step, applied to the CPU the current window does not show. | new |
| **Run to a moment** | all | Stop when the chosen condition is reached: card cycle N, main T N, the next frame, the next card interrupt, the next host command. | S7 |
| **Frame step** | all | Run to the start of the next frame. | new |
| **Run to near the frame end** | all | Stop about N T (default 1,000) before the end of the frame (Spectaculator Ctrl+F9). | new |
| **Run until a condition, repeat** | all | Run until an expression holds; "repeat" runs again with the same expression (Spectaculator F6 / Ctrl+F6). | new |
| **Run until a device event** | all | Any plugin event: the tape stops, a disk command completes; on the card: the host writes a command or a data byte, a card interrupt, a sample's playback ends. | new |
| **Auto run** | all | Trace while breakpoints exist, otherwise run at full speed (Spectaculator's Esc). | new |
| **Reverse step / reverse continue / seek** | per the TTD rules | Available while TTD records or holds a session. | D1-D4 |

- **Internal breakpoints.** Step over, step out and run to cursor use hidden
  breakpoints on the chosen CPU. They never appear in lists, never count as
  user hits, and are removed on any pause.
- **Resuming steps off each CPU's own breakpoint** (S10). When the machine
  resumes while one or several CPUs sit on an execution breakpoint, each of
  them first executes the instruction at its breakpoint, without re-hitting
  it.
- **Stop messages say why, in words, with the page and the CPU**
  (Spectaculator's "Break on memory write to #5C3A (RAM 5)"): e.g. `card
  breakpoint #3: write #4100 ← #12 (RAM 1) by #0C30`.
- **Every stop reports both deltas** (S11): main ΔT and card Δcycles since
  the resume, with the frame and the position of each CPU.
- **Step units** (S7): "break in N" and "run to" accept card cycles, card
  instructions, card interrupt periods, main T and main instructions.
  "Frame" always means the main machine's frame.
- **Stepping never skips a breakpoint** of another kind or on another CPU.
  If one fires first, the machine stops there, and the pause reason names
  it.

## 3. Several CPUs, one clock

These rules exist because a sound card's CPU is a second computer with its
own clock (requirements §4.2).

### 3.1 Machine time

- Each CPU counts time in its own unit: the main CPU in T-states, GS in
  12 MHz cycles, NeoGS in cycles of its current clock (and internally in
  120 MHz base ticks).
- **Machine time** is one line. A CPU's position on it is its count divided
  by its clock rate.
- The **clock ratio** converts between them: with GS on a Pentagon it is
  12 / 3.5 = 24/7.
- A clock change (main-CPU turbo; NeoGS switching between 10, 12, 20 and
  24 MHz) applies from the moment it happens. Positions and durations before
  it keep the old ratio (S9).

### 3.2 One pause, one moment

- **R1 (S1).** A pause stops all CPUs.
- **R2 (S2).** While paused, every CPU is shown at **one moment**:
  - The CPU that stopped is on its own instruction boundary, or inside its
    access for a main-CPU memory or port stop (§1).
  - A card CPU that is not the stopper is brought to its last instruction
    boundary at or before the moment. The remainder is the **gap**, less than
    one card instruction, and it is shown ("3 cycles behind").
  - The main CPU, when it is not the stopper, is usually **inside** an
    instruction. It is shown with its **progress**: the instruction's
    address, its start T, the elapsed and total T, the percentage, and the
    bus cycle under way (opcode fetch, operand read, memory read, memory
    write, I/O, wait) (S3).
- **R3 (S4).** The main CPU is never shown later than the moment by more than
  its current instruction. To guarantee this, the card follows the main CPU
  **tightly** (at every main-CPU bus access) whenever any of these is active:
  a card breakpoint, a card step, a card run-to, or a front-end showing a card
  CPU while paused. Otherwise it follows lazily (at host port accesses and
  frame end), as when nothing is debugged. The mode is part of the state
  every front-end shows.
- **R4.** The card never runs ahead of the main CPU. To move the card past
  the main CPU's position, the main CPU moves first.

### 3.3 Stepping one CPU

- **R5 (S5).** Stepping one CPU freezes the others: they run no instruction
  beyond what the elapsed time requires.
  - Stepping the card by one instruction moves the machine time by that
    instruction's duration. The main CPU's progress grows by the matching T.
    When it passes the end of its instruction, that instruction completes and
    the next one starts.
  - Stepping the main CPU by one instruction runs the card for the matching
    cycles (several card instructions), and stops the card on a boundary
    (R2).
- **R6 (S6).** Step into, step over, step out and run to cursor mean the same
  on every CPU, and follow R5 for the others.
- **R7 (S8).** Debugging never changes results. Pausing, stepping, tight sync
  and breakpoints change **when** state is looked at, never **what** any CPU
  computes. Every host-port access is still preceded by an exact catch-up of
  the card.

### 3.4 Worked example

Pentagon with GS; the ratio is 24/7. The card stops at a breakpoint before
`JR NZ` at cycle 119,985, which is main T 34,995.6.

- The main CPU is inside `OUT (#B3),A`, which started at T 34,989: 6.6 of
  11 T (60%). The opcode fetch (T 0-4) and the operand read (T 4-7) are done;
  the port write (T 7-11) has not started. So the card has not received this
  byte yet.
- **Step the card** (`JR NZ`, 12 cycles = 3.5 T): the main CPU is at 10.1 of
  11 T (92%), with the port write under way.
- **Step the card again** (19 cycles = 5.5 T): `OUT` completes, and the main
  CPU is 4.6 T into its next instruction.
- **Step the main CPU** (an 11 T instruction): the card advances about 37.7
  cycles and stops on a boundary.

## 4. Snapshots and refresh

- **A snapshot** is everything a skin needs to draw one CPU's widgets
  ([protocol.md](protocol.md) §3). It is taken:
  - after every pause and every step;
  - after every mutating command;
  - on request, while paused.
- **While paused, nothing changes by itself** (U §9.3). Front-ends **must
  not poll**. They refresh after events and after their own commands.
- **While running:**
  - passive widgets (registers, timeline density, boards, stats, the log)
    may show live values, at most **10 times a second**;
  - card statistics follow the publish-on-read rule
    (neogs-automation-design §5.2);
  - live values are marked `live`, and never used for change marks.
- **Reads are side-effect free.** A debugger read never:
  - consumes a DAC fetch or a ZX-DMA byte;
  - clears a flag;
  - advances a device state machine (U §4.6).

  A value that can only be read with a side effect is shown from the
  device's model, never by performing the access.
- **UI-only state** stays in the front-end (U §13): focus, cursors, scroll
  positions, the jump stack and slots, view modes, and column choices.
  Cursors and scroll positions are **per CPU**.

## 5. Breakpoints

### 5.1 Ownership and identity

- Each CPU has **its own** breakpoint set (T2). A breakpoint is never shared
  between CPUs.
- IDs are unique across all CPUs and never reused.
- A breakpoint on a card CPU that is not fitted now is **kept, inactive**,
  and becomes active again when the card returns (T4).

### 5.2 Address, page, range

- **Any page** (U §10.1): matches at a 16-bit CPU address, whatever is
  mapped there.
- **Page-qualified (new, fixes quirk Q9):** it matches a **physical address**
  (memory kind, 16 KB page, and offset `address & #3FFF`) at every CPU
  address where that page is visible. For example, GS `ROM0:0038` also fires
  at `#8038` while the page register is 0.
- **Ranges** (U §6.4): `start-end`, inclusive. The classic manager shows
  address breakpoints merged into ranges.
- **Ports:** a 16-bit port with an optional mask (`xxFE`), IN and/or OUT.

### 5.3 Conditions: the expression language

The Unreal language (U §10.2), kept as it is and extended:

| Precedence (high → low) | Operators |
|---|---|
| 1 | `!` `~` `M(x)` (a byte of memory) `W(x)` **new** (a word) `a->b` (= `M(a+b)`) |
| 2 | `*` `%` `/` (division or modulo by 0 leaves the left operand) |
| 3 | `+` `-` |
| 4 | `>>` `<<` |
| 5 | `>` `<` `=` `==` `>=` `<=` `!=` |
| 6 | `&` |
| 7 | `^` |
| 8 | `\|` |
| 9 | `&&` |
| 10 | `\|\|` |

| Operand | Meaning | Origin |
|---|---|---|
| `A B C D E F H L`, `AF BC DE HL`, primes, `PC SP IX IY I R` | the registers of **the breakpoint's CPU** | U |
| `FD` | the last `#7FFD` value (main) | U |
| `OUT`, `IN`, `VAL` | the port address of this instruction's OUT / IN (`FFFFFFFF` if none), and its value | U |
| `DOS` | the TR-DOS ports are active | U |
| `RD`, `WR`, `MDT` | this instruction's memory read / write address, and its value | T |
| `PG0`-`PG3` | the page of window 0-3 (any model, from `W.pages`) | T, generalized |
| **new** `T` | the CPU's position in the frame (T for main, cycles for a card) | new |
| **new** `FRAME` | the frame number | new |
| **new** `MPAG`, `GSCFG0` | card registers (on the card CPUs) | new |
| **new** `CMD`, `DATA`, `STATUS` | the card mailbox latches (on any CPU) | new |
| **new** `main.X`, `gs.X`, `neogs.X` | an operand of another CPU, e.g. `main.PC == 8123` on a card breakpoint | new |
| numbers | hex, starting with a digit (`0DFFD`) | U |
| **new** `#DFFD`, `$DFFD`, `0xDFFD`, `123d` | the other number forms | new |
| `'c'` | a character constant | U |

- **Classic parsing is kept:** the text is upper-cased except before `'`,
  and alternate registers must be written in upper case (U §10.2). The
  improved parser is case-insensitive.
- **Evaluation:**
  - Classic: before every instruction of that CPU while any condition exists
    (U).
  - Also on every memory access when `RD` / `WR` / `MDT` are used (T §10).
  - **Improved:** a condition can also be attached to any address, port or
    event breakpoint. It is then evaluated only when that breakpoint matches,
    which costs nothing elsewhere.
- **Limits:** classic allows 16 conditions per CPU (U §6.4); improved has no
  fixed limit.
- **Hit counts (new):** four rules, as in Spectaculator:
  - always;
  - equal to N;
  - a multiple of N;
  - at least N.

  Each has a Reset.
- **A condition that fails to evaluate stops the machine** and puts the
  breakpoint in an error state, with the reason. It is never silently false
  (Spectaculator).
- **Access-variable aliases** (ZXSpin): `RADDR` = `RD`, `WADDR` = `WR`,
  `RPORT` = `IN`, `WPORT` = `OUT`, `VALUE` = `VAL` / `MDT`, and `RWWORD` (the
  16-bit value of a word access). With them, one execution-condition engine
  also covers memory and port watchpoints with value filters.

### 5.4 Event and protocol breakpoints (new)

| Kind | CPU | Fires | Stop point |
|---|---|---|---|
| `int`, `nmi` accepted | any | when the CPU accepts it | the handler's first instruction |
| `reset` | card | card reset | the first instruction after the reset |
| `page_switch` (value filter) | any | a write to a paging register | after that instruction |
| `clock_switch` | neogs | a GSCFG0 clock change | after that instruction |
| `zxdma_start` / `zxdma_stop`, `dma_done` (SD / MP3), `dma_error`, `zxdma_dropped`, `host_wait > N T` | neogs | the DMA event | the card's next boundary; host-side waits stop the main CPU at the access |
| `host_command`, `host_data` (byte N), `host_reply_read` (command filter) | main | the Spectrum's port access | inside the main instruction |
| `card_command_read`, `card_data_read`, `card_reply_write` | card | the firmware's port access | the card's next boundary |

### 5.4a Actions, forbid ranges, timing, masters (new)

- **Action** (X3). A hit does one of:
  - `stop` (the default);
  - `log`, a formatted line with operands, then continue;
  - `mark`, a marker on the timeline and the event viewer, then continue;
  - `count`, then continue.

  Only `stop` pauses the machine. The others are probes: they never change
  timing or results (R7).
- **Forbid range** (X4): a hit whose PC is inside the range (and whose
  condition holds) is ignored.
- **Timing** (X5):
  - A memory or port breakpoint stops **after** the instruction on a card
    CPU, and **inside** the access on the main CPU (§1).
  - `before` is an option on either CPU: the debugger predicts the next
    instruction's accesses and stops before it runs.
- **Masters** (X6): a memory breakpoint fires only for the chosen bus
  masters (`cpu`, `dac`, `sd_dma`, `mp3_dma`, `zx_dma`). The hit names the
  master.
- **Not stoppable** (X21): a CPU can be marked not stoppable. Its
  breakpoints stay stored but are not armed, and cost nothing.

### 5.5 Persistence

- **Classic import and export** (U §10.3): `bpx.ini` lines
  `<type><cpu>=0x<start>[-0x<end>]`, with CPU 0 = main and CPU 1 = the
  card.
- **Native:** a per-machine JSON file with every field of `W.bp`.
- Conditions, events and protocol breakpoints persist in the native format.

## 6. Focus and keys

- **Focus** (U §3). Exactly one widget of a window has focus. Focus cycles
  through the focusable widgets with Tab / Shift+Tab (classic order: regs →
  disasm → memory → pages; improved: the skin's layout order). A click
  focuses the clicked widget and moves its cursor to the clicked field. A
  click on a passive widget does not take focus.
- **Keys map to actions** ([widget-catalog.md](widget-catalog.md) §8). The
  focused widget's actions are matched first, then the window's, then the
  session's.
  - Classic: the multi-key bindings are sorted first, with the Unreal
    shadowing (U §7.2).
  - Modern: every key has one meaning per widget; conflicts are errors in
    the binding configuration.
- **Typing starts editing** (U §3.5):
  - a hex digit on a register;
  - a letter in the disassembly's mnemonic column (assembly);
  - a hex digit or a printable character in memory.

## 7. Editing

- **Only while paused.** While running, editable fields are read-only.
- **Lifecycle** (U §3.5):
  1. Typing or Enter opens an in-place buffer.
  2. Backspace edits the buffer.
  3. Enter validates and commits; Esc cancels and restores.
  4. An invalid value is rejected and changes nothing. Classic re-opens the
     field silently, as for an assembly error; improved shows the reason.
- **What a write does:**
  - Registers: written; `R7` is re-derived from R (U §4.1.5).
  - Memory: written through the CPU's map (`cpu` space) or to the page
    (`page` space). ROM: written only when the skin asks for a forced write,
    which improved skins confirm. Flash: the chip's array is written directly
    (the program command sequence is not used), with a warning.
  - Disk: the physical track is marked modified; a logical write also
    recomputes the sector CRC (U §4.4.5).
  - Ports and board controls: a real OUT, with its side effects.
  - A write equal to the current value is skipped (U §4.4.6).
- **While TTD records**, a state edit is a change the recording cannot
  replay. It is refused with the TTD reason, unless the request asks to end
  the recording first. This is the same rule as media changes.
- After an edit, every attached front-end refreshes (an `edited` event).

## 7a. Undo and patches (new)

- Every memory and register edit made from the debugger is on an undo stack
  per machine: undo and redo (Spectaculator `CDbgUndoManager`).
- **Patch maker:** a selection of edits can be saved as a named patch (a
  list of `{page, offset, old, new}`). A patch can be re-applied, or turned
  into a POKE file.
- Undo never crosses a resume: after Continue, the stack is kept, but an
  undo checks that the bytes still hold the edited value, and refuses if
  they changed.

## 8. Change marks and the time mark

### 8.1 Change marks

- Each CPU keeps a **previous snapshot** of its registers (U §9.5), and
  **new**: of the visible memory rows.
- It is taken:
  - at the start of every step of that CPU, so the marks show what that
    instruction changed;
  - when the machine resumes, so the marks at the next pause show everything
    changed since.
- **Stepping another CPU** also moves this CPU in time (R5). Its changes are
  then marked against its own last snapshot.

### 8.2 Time mark

- Each CPU keeps a **time mark** (U §9.5, §4.9). It is set at the same two
  points as the previous snapshot, and **new**: by the user (`time.set_mark`).
- `W.time.delta` is the time since the mark, in that CPU's unit. After a step
  it is the instruction's cost, contention and waits included.

## 9. Labels

- **Per CPU** (L1). A label on one CPU never appears on another.
- **Page-aware** (U §11, L2). A label belongs to a physical address (kind,
  page, offset), or to any page. Lookup at a CPU address:
  1. the page-tied label at the page mapped there now;
  2. otherwise, the any-page label.

  So `ROM0:0038` shows at `#0038`, and also at `#8038` while the GS page
  register is 0.
- **Sources, in priority order** (a later source may add, never silently
  replace):
  1. firmware symbols (card CPUs, loaded by the ROM's SHA-256, L4);
  2. files the user loads (`.map`, `.sym`, Unreal `user.l` formats `XXXX
     name` and `PP:XXXX name`);
  3. imports (XAS7, ALASM, U §11);
  4. labels added interactively (kept per machine, and for a card per
     firmware SHA-256, L5).
- **Reload.** A loaded file is reloaded when it changes on disk, while labels
  are shown (U §9.2).

## 10. Formats

- **Hex** is upper case. The prefix is a skin setting: none (classic), `#`
  (the Spectrum convention), `$`, or `0x`. Input accepts all of them, and
  decimal with a `d` suffix or, in improved skins, a plain number where the
  field is decimal.
- **Mnemonics.**
  - Classic: lower case, operands at column 5, no prefixes, relative jumps
    shown as absolute targets, `(ix+05)` (U §4.2.2).
  - Improved: the same by default. Upper case and prefixed numbers are skin
    options.
- **Text.** Bytes `#80-#FF` are shown as CP866; `#00` as `.`. Other control
  codes follow the U Appendix D table.
- **Time:** `T` for main-CPU T-states, `c` for card cycles, with thousands
  separators in improved skins (`34,996 T`).
- **Sizes:** `B`, `KB` (1024), and rates per second (`B/s`).

## 11. Several front-ends at once

- **Any number of front-ends** can attach to one emulator at once: the Qt
  windows, a classic skin, a browser, a script, GDB, DeZog.
- **Shared state** (the core owns it):
  - run state and pause reason;
  - breakpoints, labels and watches;
  - the command log, the time marks and the previous snapshots;
  - TTD.
- **Per-front-end state** (§4): focus, cursors, scroll, view modes.
- **Commands are serialized** by the core. They apply in arrival order, and
  every front-end receives the resulting events. There is no locking between
  front-ends, except run control (§2).
- **Events** are delivered to every front-end that subscribed to them
  ([protocol.md](protocol.md) §5). A front-end that missed events asks for a
  full snapshot.

## 12. Messages

Plain words, in these forms (the classic texts of U §12 are kept for the
classic skin):

| Situation | Message |
|---|---|
| Expression error | classic `Error in expression\nPlease do RTFM`; improved `Condition: unexpected ')' at column 12` |
| Invalid range | classic `Invalid breakpoint address / range`; improved `Range end is before its start` |
| Find, no match | `not found` |
| Disk track missing | `track not found` |
| Card CPU not available | `No card CPU: the lightweight player is fitted` / `No sound card is fitted` |
| Run control held | `Run control is held by gdb` |
| Refused while TTD records | `Refused: a TTD recording is running. End the recording first.` |
| Card switch pending | `The card is being switched at the next frame` |
| Unknown firmware | `Unknown firmware (SHA-256 3fa1…): no symbols, no command names` |

## 13. Classic and improved

The model supports both. A skin declares which one it follows. The Unreal
quirks (U §15, T §15) are handled like this:

| Quirk | Classic | Improved |
|---|---|---|
| Q1 window bindings shadow global ones | kept | no shadowing |
| Q2 frames vanish under dialogs | kept | kept dimmed |
| Q3 `reg.a`… only move the cursor | kept (0.39.0) | they open the editor |
| Q4 `switchdump` index independent | kept | follows the current space |
| Q5 hidden load and save menu items | hidden | hidden |
| Q6 step over HALT stops at the handler | kept | stops after the handler (T §9); the classic behavior is an option |
| Q7 `prev_instr` heuristic | kept | kept (it is inherent) |
| Q8 joke context menu | omitted | context menus per widget |
| Q9 breakpoints not bank-aware | kept (any page) | page-qualified available |
| Q10 no ripper indicator | – | a `RIP` badge |
| Q11 unimplemented range actions | ignored | ignored |
| Q12 T counter overflow | kept | clamped |
