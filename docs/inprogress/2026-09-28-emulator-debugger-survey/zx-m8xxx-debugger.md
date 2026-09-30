# ZX-M8XXX debugger — capability survey

**Source:** https://github.com/Bedazzle/ZX-M8XXX · local checkout commit `345d70d` (2026-09-27, app version 26.09.03) · vanilla JavaScript ES modules, browser DOM/canvas UI, no build step
**Surveyed:** 2026-09-28 (source reading)
**Scope note:** GUI debugger built into the emulator page (Debugger tab with two configurable panes plus ten sub-panel tabs), a separate batch profiler page (`profile-game.html`), and a headless JavaScript automation API (`window.zxDebug`) meant for driving the emulator from headless Chromium. No console monitor, no GDB/DeZog/socket protocol. The debugger is aimed at reverse engineering a game as much as at stepping code.

All paths below are relative to the emulator root and written as `ZX-M8XXX/...`.

## 1. Capability registry

| Area | Feature | What it does / values it shows | Where |
|---|---|---|---|
| CPU & registers | Register panel | Main, alternate, IX/IY, I/R, flags, IFF/IM, `T-st` (frame T-state, editable) and `ΔT` (T-states since last break) | `ZX-M8XXX/ui/debugger-display.js:116` |
| CPU & registers | Inline register editor | Click to edit any register, toggle single flags, EX AF / EXX swap buttons | `ZX-M8XXX/ui/register-editor.js:4` |
| CPU & registers | Instruction history popup | Last 10 executed instructions, disassembled from the bytes actually fetched (correct under self-modifying code) | `ZX-M8XXX/core/z80.js:78` |
| CPU & registers | Stack view + runtime call stack | Stack words with highlighting; call stack tracked from SP deltas (CALL/RST/INT/RET), max depth 32 | `ZX-M8XXX/ui/stack-view.js:64`, `ZX-M8XXX/core/spectrum.js:4258` |
| CPU & registers | AY register view | R0–R13 in two columns when an AY is present | `ZX-M8XXX/ui/debugger-display.js:158` |
| Disassembly | Two disassembly panes | Left pane and a right pane that can switch between disassembly, memory dump and calculator | `ZX-M8XXX/ui/right-disasm-view.js:7`, `ZX-M8XXX/ui/panel-navigator.js` |
| Disassembly | Label display mode | Show address, label, or both in the address column | `ZX-M8XXX/html/panel-debug.html:361` |
| Disassembly | T-state sum of a selection | Drag across lines and it sums T-states; for a conditional branch on the last line it shows the taken and not-taken totals, and it refuses to sum when flow control makes the total unknown | `ZX-M8XXX/ui/disasm-navigation.js:365`, `ZX-M8XXX/docs/debugger.md` |
| Disassembly | Edit an instruction in place | Double-click the mnemonic, type assembly (sjasmplus encoder, labels resolved), writes bytes; when an instruction has several encodings it picks the one whose length matches the instruction being replaced (e.g. `NOP` over 2 bytes becomes `DD 00`); one undo step | `ZX-M8XXX/ui/disasm-asm-edit.js:32`, `ZX-M8XXX/core/asm-line.js` |
| Disassembly | Code folding | User folds (pick end line, or fold a mouse selection), subroutine folds, a collapsed fold auto-expands when PC enters it | `ZX-M8XXX/debug/fold-manager.js:4` |
| Disassembly | Operand formats | Per-address operand display override (hex/dec/char/...) | `ZX-M8XXX/debug/managers.js:400` |
| Disassembly | Hex/decimal switches | Three independent switches: addresses (incl. ports), byte values, opcode bytes; input accepts `$`/`0x`/`#`/`h` as forced hex | `ZX-M8XXX/core/addr-format.js`, `ZX-M8XXX/docs/debugger.md` |
| Memory views | Two hex dumps | Adaptive 8/16/32 bytes per line, inline byte editor (ROM writes gated by "Edit ROM"), ASCII selection | `ZX-M8XXX/ui/memory-view.js`, `ZX-M8XXX/ui/mem-dump-render.js` |
| Memory views | Memory search | Hex (with wildcards), decimal, text, and "Encoded" text (XOR/complement/offset/position-keyed/nibble-packed schemes) | `ZX-M8XXX/ui/memory-search.js`, `ZX-M8XXX/core/encoded-search.js:63` |
| Memory views | Watches | Up to 10 named watches, 8 bytes each, changed bytes highlighted | `ZX-M8XXX/ui/watches.js:15` |
| Memory views | Memory map / heat map | 512×512 bitmap of 64 KB: region view (code/SMC/db/dw/text/graphics) or heat map (B=exec, G=read, R=write, log scale); 128K 2×4 grid of all banks | `ZX-M8XXX/ui/memory-map.js:513` |
| Memory views | Shadow (second) screen | Shows the inactive 128K screen bank (full or bitmap only), or any 6144 bytes as a linear or Spectrum-interleaved bitmap | `ZX-M8XXX/ui/app-init.js:1398` |
| Memory views | Compare tool | Snapshot vs snapshot, binary, live state; memory vs memory (paged 64K or explicit bank) | `ZX-M8XXX/ui/compare-tool.js:9`, `ZX-M8XXX/core/mem-compare.js` |
| Navigation & bookmarks | Bookmarks per pane, back/forward history, go-to palette (by label or address) | | `ZX-M8XXX/ui/bookmarks.js:5`, `ZX-M8XXX/ui/nav-history.js:3`, `ZX-M8XXX/ui/goto-palette.js:7` |
| Symbols & labels | Labels | Keyed by `page:address`, with name, comment, size and source (user/profiler/ROM); ROM label set for the 48K ROM | `ZX-M8XXX/debug/managers.js:77`, `ZX-M8XXX/data/48k-labels.json` |
| Symbols & labels | Regions, comments | Typed regions (code/db/dw/text/graphics/smc) and inline comments, page-aware | `ZX-M8XXX/debug/managers.js:209`, `ZX-M8XXX/debug/managers.js:318` |
| Symbols & labels | XRefs, subroutines | Scan the visible range or all 64 KB for references; auto or user subroutine marks | `ZX-M8XXX/debug/xref-manager.js:6`, `ZX-M8XXX/debug/subroutine-manager.js:6` |
| Symbols & labels | Signature packs | JSON packs of byte anchors plus labels/regions for known engines and games (AGD, JSW, Manic Miner, Knight Lore...) applied automatically | `ZX-M8XXX/debug/signature-pack-manager.js:4`, `ZX-M8XXX/signatures/index.json` |
| Symbols & labels | Static code-flow analysis | Traces flow from entry points (plus optional ISR at $0038) to mark code | `ZX-M8XXX/core/spectrum.js:9399` |
| Breakpoints (every kind) | Unified trigger list | 12 kinds: exec, read, write, R/W, port IN/OUT/IO, tape block, disk read, disk sector (track:sector), screen bitmap write, screen attribute write | `ZX-M8XXX/core/spectrum.js:181`, `ZX-M8XXX/html/panel-debug.html` |
| Breakpoints (every kind) | Address ranges and page keys | `C000`, `8000-80FF`, `5:C000`, `R0:0038`; port `FE&FF` masks | `ZX-M8XXX/core/spectrum.js:5603` |
| Breakpoints (every kind) | Skip count / hit count, enable toggle, name | Breaks once hitCount > skipCount | `ZX-M8XXX/core/spectrum.js:6206` |
| Breakpoints (every kind) | Comparison breakpoint | Break when `(addrA) op (addrB)` becomes true (==, !=, <, >, <=, >=), checked on writes to either address | `ZX-M8XXX/core/spectrum.js:4658` |
| Breakpoints (every kind) | Code-path trace-break | Break at the first PC not in a recorded baseline set of executed addresses | `ZX-M8XXX/core/spectrum.js:4618` |
| Conditions & expressions | Single-comparison conditions | Registers (incl. shadow), flags, `(HL)`/`(IX+d)`/`(nnnn)`, `T`, `val`, `port`; operators `== != <> < > <= >= & \|` | `ZX-M8XXX/core/spectrum.js:5674` |
| Watchpoints & watches | Memory watchpoints | Read/write/RW over ranges with page and condition; reads only count CPU data reads (not opcode fetches, not debugger reads) | `ZX-M8XXX/core/spectrum.js:542` |
| Watchpoints & watches | Write monitor / read monitor | Log every PC that writes/reads one address, with call stack, old/new value and frame | `ZX-M8XXX/core/spectrum.js:382`, `ZX-M8XXX/ui/poke-search.js:459` |
| Watchpoints & watches | Register tracker | Log a register's value each time PC hits an address (up to 10000 samples) | `ZX-M8XXX/core/spectrum.js:406`, `ZX-M8XXX/ui/poke-search.js:785` |
| Watchpoints & watches | Memory freeze | Rewrite locked addresses at frame end | `ZX-M8XXX/core/spectrum.js:396` |
| Execution control | Step into / over, run to cursor, run to interrupt, run to RET, run N T-states | Right pane has its own copy of the step buttons | `ZX-M8XXX/ui/step-controls.js:16`, `ZX-M8XXX/core/spectrum.js:4987` |
| Execution control | Step-over T-state limit with DJNZ loop-safety analysis | Default 80000 T; a DJNZ loop whose body can be shown not to change B has no limit | `ZX-M8XXX/core/spectrum.js:5045` |
| Tracing & logging | Execution trace | Per instruction: all registers, I/R/IFF/IM/T, up to 8 memory writes (old+new), port ops; 100000-entry ring, optional runtime (full-speed) recording, skip ROM, stop after N | `ZX-M8XXX/debug/trace-manager.js:5` |
| Tracing & logging | Trace navigation with screen revert | Alt+Left/Right walks history; memory writes are undone/redone so the screen shows the state at that step | `ZX-M8XXX/ui/trace-display.js:38` |
| Tracing & logging | Trace export | TSV, register columns print only when changed, block-repeat collapse `(xN)` | `ZX-M8XXX/debug/trace-manager.js:143` |
| Tracing & logging | Port I/O log | Every IN/OUT with port filter list, direction filter, source tag (TRDOS, ROM:n, RAM), frame and T-state; TSV export | `ZX-M8XXX/core/spectrum.js:433`, `ZX-M8XXX/core/spectrum.js:9236` |
| History / rewind / time travel | Rewind ring | SZX snapshot every 100 frames, 30 states (~60 s); Ctrl+Left/Right to scrub | `ZX-M8XXX/core/rewind.js:23`, `ZX-M8XXX/ui/app-init.js:343` |
| History / rewind / time travel | Save slots | Nine quick-save slots (F2/F5) | `ZX-M8XXX/core/save-slots.js` |
| History / rewind / time travel | RZX record and playback | Input recording used for deterministic replays in analysis | `ZX-M8XXX/ui/rzx-recorder.js`, `ZX-M8XXX/core/loaders/rzx.js` |
| Video, raster & beam | Beam overlay | Previous frame greyed out, the current frame in color up to the beam, cyan crosshair at the beam position, 8-pixel grid; "BeamScreen" variant does border only | `ZX-M8XXX/core/spectrum.js:3580` |
| Video, raster & beam | Other overlays | grid, box, screen (border-only), reveal, no-attr (monochrome), no-bitmap (attr cells with crosses); F10 cycles | `ZX-M8XXX/core/spectrum.js:3107`, `ZX-M8XXX/ui/keyboard-shortcuts.js:486` |
| Video, raster & beam | Screen click inspector | When paused, click a pixel: bitmap byte addresses and binary, attribute address and decoded ink/paper/bright/flash | `ZX-M8XXX/ui/screen-info.js:9` |
| Video, raster & beam | Screen-region write breakpoints | Break on writes to a character-cell or pixel rectangle, normal / shadow / both screens | `ZX-M8XXX/core/spectrum.js:6735` |
| Video, raster & beam | Graphics viewer | Browse memory as sprites/tiles for graphics search | `ZX-M8XXX/ui/graphics-viewer.js:7` |
| Video, raster & beam | OCR text ripper, game mapper | Recognize screen text; stitch rooms into a map | `ZX-M8XXX/ui/text-ripper.js`, `ZX-M8XXX/ui/mapper-ui.js` |
| Sound & device views | AY registers, PSG recording, disk activity | | `ZX-M8XXX/ui/debugger-display.js:158`, `ZX-M8XXX/ui/psg-player.js`, `ZX-M8XXX/ui/disk-activity.js` |
| Profiling, heat maps, coverage | Auto-map | Executed/read/written counts per `addr:page`; "fast" bitset mode (~10x cheaper) and paged fast mode | `ZX-M8XXX/core/spectrum.js:299`, `ZX-M8XXX/core/debug-instrument.js:96` |
| Profiling, heat maps, coverage | Behavior profiler + auto-labeler | Per subroutine: calls, ports in/out, screen reads/writes, ISR context, callers/callees, frames; generates labels like `read_keyboard`, `play_music`, `draw_sprite` | `ZX-M8XXX/core/spectrum.js:366`, `ZX-M8XXX/tools/profiler-analysis.js:8` |
| Profiling, heat maps, coverage | Hotspots | T-states per PC, clustered and classified (`delay_djnz`, `frame_sync`, `block_ldir`, `io_poll`...) | `ZX-M8XXX/tools/profiler-analysis.js:210` |
| Profiling, heat maps, coverage | Batch profiler page | Load SNA/Z80/SZX/RZX, run N frames with a scripted key sequence, export `heatmap.tsv` and `profile.json` | `ZX-M8XXX/profile-game.html:224` |
| Profiling, heat maps, coverage | Code-path diff | Record up to 3 executed-address sets (baseline / event A / event B) and diff them | `ZX-M8XXX/ui/codepath.js:7` |
| Profiling, heat maps, coverage | Differential run | Run N frames twice from one snapshot, with a memory change before the second run, and report the first diverging instruction | `ZX-M8XXX/core/divergence.js:26`, `ZX-M8XXX/core/spectrum.js:4591` |
| Profiling, heat maps, coverage | Struct mapper | Accesses at offsets from IX/IY or a base address: per-offset reader/writer PCs | `ZX-M8XXX/core/spectrum.js:413`, `ZX-M8XXX/ui/struct-mapper.js:5` |
| Profiling, heat maps, coverage | Call graph, indirect-jump targets, SMC ranges, provenance | Runtime CALL/RST edges; runtime targets of `JP (HL/IX/IY)`; executed-and-written ranges; which PC writes/reads/runs a range; which PC reads a port | `ZX-M8XXX/core/debug-instrument.js:167` |
| Scripting, automation & remote | `window.zxDebug` headless API | Self-describing (`brief`, `capabilities`, `help`, `require`), peek/poke, search, disassemble, step/run-to, breakpoints, registers, provenance, `callRoutine`, `replayRZX`, `checkpoint`, raw `onAccess` hooks | `ZX-M8XXX/ui/app-init.js:420`, `ZX-M8XXX/core/api-manifest.js:20` |
| Scripting, automation & remote | Lua in the assembler | sjasmplus-style Lua blocks (assembler only, not the debugger) | `ZX-M8XXX/sjasmplus/lua.js` |
| Import / export & persistence | Project file `.zxproj` | Snapshot, media, triggers, labels, regions, comments, xrefs, subroutines, folds, watches, auto-map, code paths, struct mapper, assembler state, settings | `ZX-M8XXX/ui/project-io.js:77` |
| Import / export & persistence | Toolchain exports | SkoolKit `.ctl`, Ghidra CSV (labels, indirect jumps, SMC, call graph), sjasmplus `.sym`, `.asm` export of mapped code | `ZX-M8XXX/core/map-export.js:243` |
| Import / export & persistence | Auto-save per loaded file | Labels/regions/comments go to localStorage under a key derived from the loaded file name | `ZX-M8XXX/debug/managers.js:22` |
| UI conveniences | Undo/redo, splitters, hotkeys, T-state selection popup, POKE search/manager, calculator, opcode table | | `ZX-M8XXX/debug/undo-manager.js:2`, `ZX-M8XXX/ui/keyboard-shortcuts.js:511` |

## 2. CPU, registers and disassembly

- **Register block.** It shows `T-st` (the frame T-state counter, editable) and `ΔT`, the T-states accumulated since the last breakpoint, which works as a stopwatch between two breaks (`ZX-M8XXX/ui/debugger-display.js:116`, `ZX-M8XXX/core/spectrum.js:145`). The counter reset is deferred, so the value stays visible until the next action (`_bpTStatesResetPending`, `ZX-M8XXX/core/spectrum.js:146`).
- **Instruction history.** `cpu.instrHistory` is a preallocated ring of 10 `{pc, bytes: Uint8Array(6), len}` entries filled from `fetchByte()`, so it adds no extra memory reads and no garbage-collection pressure. Chained prefixes such as `DD DD 21` are split into separate entries (`ZX-M8XXX/core/z80.js:78`, `ZX-M8XXX/docs/debugger.md` "Instruction History Popup").
- **Call stack.** Derived, not instrumented. After every instruction the core compares SP before and after. SP−2 with a pushed word 1–4 bytes past the old PC counts as a CALL/RST. SP+2 with new PC equal to the popped word counts as a RET. Any other SP change resets the stack. Depth is capped at 32 (`ZX-M8XXX/core/spectrum.js:4258`, `ZX-M8XXX/core/spectrum.js:147`). The same hook feeds the runtime call graph.
- **Disassembly panes.** The left pane is always disassembly. The right pane switches between disassembly, hex dump and calculator (`ZX-M8XXX/ui/panel-navigator.js`). Both panes share one row renderer (`ZX-M8XXX/ui/disasm-line-render.js`).
- **T-state selection** (`ZX-M8XXX/ui/disasm-navigation.js:365`). Selecting straight-line code gives an exact sum. A conditional branch on the last line gives both totals. It refuses when a branch goes back into the selection, when the selection contains CALL/RST, HALT, block repeats or data lines. The selection is stored by address and re-applied by a `MutationObserver`, because the view re-renders even while paused.
- **Edit in place.** Uses the real assembler's parser and encoder, not a toy assembler. Candidate encodings are verified by disassembling them back. Writes below $4000 require the "Edit ROM" flag. Each edit is one undo step (`ZX-M8XXX/docs/debugger.md` "Editing an instruction in place", `ZX-M8XXX/ui/disasm-asm-edit.js:32`).

## 3. Memory views, search and watches

- The hex dumps compute bytes per line (8/16/32) from the pane width on every render (`ZX-M8XXX/docs/debugger.md` "Layout Splitters").
- **Encoded search** encodes every (scheme, key) pair once and indexes the results by first byte. Seven schemes × 256 keys then cost a single pass over memory (`ZX-M8XXX/docs/tools.md` "Encoded text search", `ZX-M8XXX/core/encoded-search.js:63`). A **table scanner** finds vocabulary tables, keyboard-scan tables and character tables by their shape (`ZX-M8XXX/core/table-scan.js:32`).
- **Watches:** `MAX_WATCHES = 10` and `WATCH_BYTES = 8`, persisted in localStorage `zxm8_watches` (`ZX-M8XXX/ui/watches.js:15`).
- **Memory map** (`ZX-M8XXX/ui/memory-map.js`). The heat map uses log intensity. Heat-map export is TSV `Address Page Exec Read Write` (`:513`). "Export free addresses" lists unused runs of at least 10 bytes, per bank for $C000+ (`:587`).
- **Shadow screen modes:** none / full / bitmap / linear / spectrum at a user address, default $C000 (`ZX-M8XXX/ui/app-init.js:1398`, `ZX-M8XXX/docs/debugger.md` "Shadow Screen").

## 4. Symbols, labels, regions and code analysis

- `LabelManager` keys labels as `"<page|g>:<hex addr>"`, so the same address can carry different labels in different banks (`ZX-M8XXX/debug/managers.js:87`). Labels carry `source`. The Labels panel filters All / User / Profiled / ROM.
- **Persistence.** Each manager auto-saves to localStorage under `zxm8_<kind>_<loaded file name>`. Labels therefore follow the loaded game automatically (`ZX-M8XXX/debug/managers.js:22`). JSON import/export is available per manager (`:59`, `:63`).
- **Signature packs.** JSON files with `anchors` (address, bytes, optional mask), `labels` and regions. They identify known engines and games and apply their disassembly knowledge (`ZX-M8XXX/docs/tools.md` "Signature Packs", `ZX-M8XXX/debug/signature-pack-manager.js:4`).
- **Static flow analysis** from entry points uses `_classifyInstruction` → `{flow, unconditional, target, indirect}` (`ZX-M8XXX/core/spectrum.js:9322`, `:9399`). The runtime-resolved indirect-jump list is the dynamic complement to it.

## 5. Breakpoints, conditions and watchpoints

**Data structure.** There is one `triggers[]` array of objects `{type, start, end, page, mask, condition, enabled, hitCount, skipCount, log, name}` (`ZX-M8XXX/core/spectrum.js:6206`). Screen triggers add `{col,row,w,h,pixelMode,screen}`. The older `breakpoints`/`watchpoints`/`portBreakpoints` arrays are derived views kept for backward compatibility (`_syncLegacyArrays`).

**Types:** `exec, read, write, rw, port_in, port_out, port_io, tape_block, disk_read, disk_sector, screen_bitmap, screen_attr`, as listed in the panel's type dropdown (`ZX-M8XXX/html/panel-debug.html`, type select in the Breakpoints tab).

**Address grammar** (`parseAddressSpec`, `ZX-M8XXX/core/spectrum.js:5603`): `[page:]addr[-end]` in hex. `page` is `0..7` (RAM) or `R0`/`R1` (ROM). Ports use `port[&mask]`. The default mask is `FF` for 8-bit port values and `FFFF` otherwise. A port matches when `(port & mask) == (start & mask)`.

**Bank keying** (`getCurrentPageForAddr`, `ZX-M8XXX/core/spectrum.js:5638`). Page `null` means any bank. Otherwise the current mapping must match: `R0`/`R1` for $0000–$3FFF, a fixed 5 for $4000 and 2 for $8000, and the paged RAM number for $C000. A breakpoint is therefore a logical address filtered by the bank mapped in at that moment. It is not a physical-address breakpoint.

**Condition grammar** (`evaluateCondition`, `ZX-M8XXX/core/spectrum.js:5674`). The condition is either a bare flag (`Z NZ C NC P PE M PO N H S`) or exactly one binary comparison `lhs op rhs` with `op ∈ {==, !=, <>, <, >, <=, >=, &, |}` (regex at `:5793`). Operands:
- 8/16-bit registers, shadow registers (`A'`, `HL'`...), `I`, `R`, `PC`, `SP`
- `T`/`TSTATES` (frame T-state, `:5748`)
- `val` (the byte read or written) and `port` (`:5744`)
- memory `(HL) (DE) (BC) (SP) (IX) (IY) (IX±d) (IY±d) (hex)` (`:5751`)
- literals: all-digit strings are decimal, strings containing A–F or ending in `h` are hex

There is no `&&`/`||`, no arithmetic and no parentheses. `&` means "bitwise AND is nonzero". Parse errors evaluate to false.

**Hit counting:** `hitCount++` on every match (including the condition). The trigger fires when `hitCount > skipCount` (`ZX-M8XXX/core/spectrum.js:6466`).

**Performance approach.**
- `updateMemoryCallbacksFlag()` sets `memory.onRead/onWrite` and `cpu.onFetch` to `null` unless some feature needs them, so the hot path pays nothing when there are no triggers (`ZX-M8XXX/core/spectrum.js:6139`).
- Exec breakpoints go through a `Set` of every address covered by an enabled exec range, rebuilt on change (`:6630`). A PC that is not in the set costs one `Set.has` (`:6466`). The main loop skips even that when the set is empty (`:2195`).
- Screen triggers have their own address sets (`:186`).
- Memory and port triggers scan the whole trigger list on each access (`:6485`, `:6502`). That is linear and uses `Array.includes` on the type list, which makes them the slow path.
- Read watchpoints fire only while `_inCpuExecution && !cpu.isFetching && !_suppressWatchpoints`. Opcode fetches and the debugger's own reads (trace pre-reads, call-stack probes) never trigger them (`:542`).

**Special watchpoints (Pokes tab).** These are aimed at cheat finding:
- write monitor and read monitor, which log `{pc, callStack, oldVal/newVal, frame}` (`ZX-M8XXX/core/spectrum.js:382`, `:4539`)
- comparison breakpoint `(A) op (B)` (`:399`, `:4658`)
- register tracker at a PC (`:406`, `:4682`)
- memory freeze (`:396`)

These are started from `ZX-M8XXX/ui/poke-search.js:459`, `:739`, `:785`. The POKE search itself compares memory across successive snapshots, with modes −1/+1, decreased/increased, changed/unchanged and A-B-A-B (`ZX-M8XXX/docs/tools.md` "POKE Search").

## 6. Execution control

- Buttons: Run, Step Into (F7), Step Over (F8), Run To cursor (F4), Run To Interrupt, Run To RET, Run N T-states. The right pane has a duplicate set (`ZX-M8XXX/ui/step-controls.js:16`–`:31`).
- Hotkeys: F6 pause/resume, Shift+F6 follow PC, F9 toggles a breakpoint at PC (`ZX-M8XXX/ui/keyboard-shortcuts.js:500`–`:539`).
- **Step over** (`stepOver(maxCycles = 80000)`, `ZX-M8XXX/core/spectrum.js:4987`) treats CALL, RST, block repeats and DJNZ as atomic.
  - Block repeats always run to completion (10M T-state cap).
  - DJNZ gets unlimited budget only if `_isDjnzLoopSafe` shows the loop body has no flow control and never writes B (including CB-prefixed ops on B and DD/FD CB ops that store into B) (`:5045`).
  - When the limit is hit, a popup under the PC line shows the remaining B.
- `runToAddress` (`:5152`), `runToInterrupt` (`:5366`) and `runToRet` (`:5437`) cap at 10M T-states and ignore a breakpoint at the starting PC, so you can leave the current breakpoint or a HALT.

## 7. Tracing and logging

**Trace entry** (`ZX-M8XXX/debug/trace-manager.js:16`):
- `pc`, SP, AF..IY, the shadow set, I, R (with bit 7 preserved), IFF1/2, IM, tStates
- `bytes` captured before execution
- `ports: [{dir, port, val}]`
- `mem: [{addr, old, val}]`, capped at `traceMemOpsLimit = 8` per instruction (`ZX-M8XXX/core/spectrum.js:222`)

The history holds 100000 entries (`maxHistory`, trace-manager `:5`) and uses `Array.shift()` to drop the oldest. The README still says 10,000. `skipROM` defaults to true (`:13`). "Stop after N" pauses the emulator when the limit is reached (`:26`). Recording happens during stepping (`traceEnabled`). "Runtime" trace also records during full-speed runs (`ZX-M8XXX/ui/trace-display.js:276`).

**Export format** (`exportToText`, `ZX-M8XXX/debug/trace-manager.js:143`) is tab-separated with header `ADDR BYTES INSTR AF BC DE HL SP IX IY [I R IFF IM T] [AF' BC' DE' HL'] [PORT] [MEM]`. A register cell is printed only when its value changed since the previous row. Port cells look like `out:00FE=07`, memory cells like `5800=38`. Consecutive iterations of the same block instruction collapse into one line with a `(xN)` suffix. Sample (inferred from the code):

```
ADDR	BYTES	INSTR	AF	BC	DE	HL	SP	IX	IY
8000	21 00 40	LD HL,4000h	0044	0000	0000	1234	FF00	0000	5C3A
8003	36 FF	LD (HL),FFh
8005	23	INC HL				4001
```

**Screen revert.** While walking the trace, `applyTraceMemoryDelta(from, to)` writes the stored `old` values backward, or `val` values forward, with the write callbacks disabled, then re-renders. The screen then shows the historical frame content (`ZX-M8XXX/ui/trace-display.js:38`). This is documented as bank-unsafe for $C000+ if paging changed between entries.

**Port I/O log** (`ZX-M8XXX/core/spectrum.js:433`, filter `:9247`). Filters are `{port, mask}`. `src` is `TRDOS`, `ROM:n`, `RAM` or empty, based on what is mapped at the PC (`:9236`). The export is TSV `Dir Port Value PC Src Frame T-states` (`ZX-M8XXX/docs/debugger.md` "Port I/O Log").

## 8. History, rewind and time travel

- `createRewindBuffer({capture, restore, intervalFrames = 100, maxStates = 30})` keeps a ring of SZX snapshots (`ZX-M8XXX/core/rewind.js:23`). It is wired to `spectrum.saveSnapshot('szx')`/`loadSZXSnapshot` via a frame listener (`ZX-M8XXX/ui/app-init.js:343`).
- Scrubbing does not drop the abandoned future until you resume.
- Ctrl+Left/Right steps through it (`ZX-M8XXX/ui/keyboard-shortcuts.js:165`). Only `runFrame` notifies frame listeners, so headless `runFrameHeadless` records no rewind states (`ZX-M8XXX/docs/automation.md` "UI handles").
- The only instruction-granular reverse view is the trace. There is no reverse execution of CPU state.

## 9. Video, raster and beam

- **Beam overlay** (`drawBeamOverlay`, `ZX-M8XXX/core/spectrum.js:3580`).
  - It computes the frame line and position in line from `cpu.tStates`, then an X pixel as `(t − lineStartT) × 2`.
  - It draws the previous complete frame greyed out at 50% luminance, then copies the current frame buffer in color up to the beam, then an 8-pixel magenta grid and a cyan crosshair at the beam.
  - `beamscreen` restricts this to the border, so border effects stand out (`:3122`–`:3128`).
  - It works while single-stepping because the ULA renders progressively. Borders are drawn at the beam; paper is drawn at line end (`ZX-M8XXX/core/ula.js:258`, `ZX-M8XXX/docs/rendering.md`).
- Other overlays: `grid`, `box`, `screen`, `reveal`, `noattr`, `nobitmap` (`ZX-M8XXX/core/spectrum.js:3107`), selected in `ZX-M8XXX/html/panel-settings.html:30` or cycled with F10.
- The screen click inspector decodes bitmap/attribute addresses for a clicked pixel (`ZX-M8XXX/ui/screen-info.js:9`).
- There is no per-T-state event list, contention visualization, or floating-bus view in the UI. Floating-bus and INT timing have only console debug flags (`ZX-M8XXX/core/spectrum.js`, `debugFloatingBus`, `_debugIntTiming` near the top of the constructor).
- Frame export: PNG/GIF/ZIP batches and `.scr`/`.bsc`/`.sca`/gigascreen formats (`ZX-M8XXX/ui/frame-export.js:409`, `:1161`).

## 10. Profiling, coverage and runtime analysis

- **Auto-map** (`ZX-M8XXX/core/spectrum.js:299`, `ZX-M8XXX/core/debug-instrument.js`).
  - Rich mode stores `Map<"addr[:page]", count>` for executed, read and written addresses.
  - Fast mode uses three `Uint8Array(65536)` bitsets. Paged fast mode keeps a bitset triple per page and recomputes slot pointers only when a packed paging signature changes (`ZX-M8XXX/core/debug-instrument.js:27`).
  - Recording happens only inside `runFrame`.
- **Profiler** (`startProfiling`, `ZX-M8XXX/core/spectrum.js:4328`; state `:366`).
  - SubroutineStats per entry: `callCount, portsIn, portsOut, writes/reads ScreenBitmap/Attr, calledFromISR, callees, callers, framesCalled, beeperOuts`. It also detects the IM 2 handler and vector table.
  - `generateProfilerLabels` assigns names by a 13-level priority (`isr_handler`, `main_loop` for >90% of frames, `read_keyboard`, `play_music`, `draw_sprite`, `page_memory`, `disk_read`, `init_XXXX`, `util_XXXX`...) (`ZX-M8XXX/tools/profiler-analysis.js:8`).
  - Hotspots: `tStatesPerPC` is clustered (gap ≤ 4, >1% of total, ≤ 32 bytes) and classified by opcode pattern (`:169`, `:210`).
  - A call-graph view is drawn from profiler data (`ZX-M8XXX/ui/call-graph.js:5`).
- **`profile-game.html`** is a standalone batch profiler.
  - Input: a snapshot or RZX, a frame count (default 500), and a key script such as `SPACE, 200ms, q, a+b`.
  - It runs `runFrameHeadless` and yields to the UI every 50 frames.
  - It exports `heatmap.tsv` (`Address Mnemonic Executed Read Written`), `profile.json` (subroutines plus hotspots) and a `.sna` (`ZX-M8XXX/profile-game.html:224`, `:331`, `:447`).
- **Code path** records up to 3 executed-address sets. Diffs are `A−B`, or `(A∩B)−baseline`, which isolates a handler shared by two events. Blocks are clustered, shown with 5 context lines, and exported as text. Trace-break mode stops at the first PC outside a slot (`ZX-M8XXX/docs/tools.md` "Code Path Tool", `ZX-M8XXX/core/spectrum.js:684`, `:4618`).
- **Differential run.**
  - It restores the snapshot before *both* runs, so both start at the same point in the frame, and runs headless.
  - It applies the `addr=val` changes, records the executed PCs with `startExecTrace` (`ZX-M8XXX/core/spectrum.js:4591`), and reports the first divergence with context, the memory runs that differ, and the registers (`ZX-M8XXX/core/divergence.js:26`, `ZX-M8XXX/docs/tools.md` "Diff run").
  - An empty change doubles as a determinism self-check.
- **Struct mapper:** `fields: Map<offset, {reads: Map<pc,count>, writes: Map<pc,count>}>` relative to IX, IY or a fixed base, up to offset 255 (`ZX-M8XXX/core/spectrum.js:413`).
- **Provenance:**
  - write, read and exec provenance over `[lo,hi]` return `[{pc, count, callers, callSites}]`
  - port-read provenance is keyed by (pc, port) and adds `bank` and `lastValue`
  - also available: indirect-jump targets and the call graph

  (`ZX-M8XXX/core/debug-instrument.js:167`–`:353`)

## 11. Scripting, automation and remote debugging

- **`window.zxDebug`** (`ZX-M8XXX/ui/app-init.js:420`). This is the only external interface. It is in-page JavaScript meant to be driven by headless Edge/Chrome with `--dump-dom`, served by `serve.py`.
- **Self-description:** `brief()` (rules plus the generated API surface as markdown, meant to be handed to an LLM agent), `capabilities()`, `help(name)`, `require([...])`. `require` throws, naming the missing members and the version that has them. `API_VERSION = 1` (`ZX-M8XXX/core/api-manifest.js:20`). A test enforces that every member is declared, still exists, and is documented (`ZX-M8XXX/docs/automation.md` "What this build has").
- **Primitives** (`ZX-M8XXX/docs/automation.md` "Primitives"):
  - memory: `peek/poke/peekWord/pokeBlock/peekBank/snapshotMemory`
  - search: `findBytes` with `??` wildcards and a bank option, `findWord`
  - disassembly: `disassemble`, `disassembleRange`
  - execution: `pause/step/stepOver/runTo/runToInterrupt/runToRet`, which throw if the machine is running instead of silently doing nothing
  - breakpoints and registers: `addBreakpoint/breakpoints/removeBreakpoint`, `captureRegisters/setRegisters`

  All API strings are parsed as hex whatever the UI's decimal setting.
- **Analysis calls:** `watchWrites/Reads/Exec`, `watchPortReads` (`:679`), `watchIndirect`, `watchCalls`, `getSmc`, `enableMap/ranges`, `recordRun/compareRuns`, `searchEncodedText` (`:1128`), and exports `exportCtl/exportCsv/exportSym/export*Csv`.
- **`callRoutine(addr, {regs, sp, maxSteps, interrupts, frames})`** (`:703`) pushes a return marker, runs until the routine returns, restores CPU state, and returns `{returned, halted, timedOut, steps, tStates, regs}`.
- **Robustness for long headless runs:**
  - `emuClock()` gives the emulated time, because wall-clock time is frozen under `--virtual-time-budget`
  - `checkpoint(data)` writes progress into a hidden DOM node that survives a killed `--dump-dom` run (`:869`)
  - `replayRZX` checkpoints automatically (`:897`)
  - `loadUrl` with `cache: 'no-store'`
- **Raw hooks:** `onAccess({onFetch, onRead, onWrite})` returns a disposer (`:827`).

## 12. Import, export and persistence

- **`.zxproj`** is JSON, `version: 2`. It holds a base64 snapshot, media, the debugger view state (addresses, pane types, bookmarks), triggers with hit/skip counts, labels, regions, comments, xrefs, subroutines, folds, operand formats, watches, port trace filters, pokes, auto-map, code-path slots, struct mapper, assembler state and display settings (`ZX-M8XXX/ui/project-io.js:77`, `:179`–`:315`, file name `:335`).
- **Toolchain exports** (`ZX-M8XXX/core/map-export.js`):
  - SkoolKit control file `c/b/t/w` with `@ label=` and `N` comments (`:243`)
  - Ghidra `address,name,comment` CSV (`:285`), plus CSVs for indirect jumps, SMC and the call graph (callee-indexed) (`:310`, `:354`, `:368`)
  - sjasmplus `NAME: EQU` (`:392`)
- An `.asm` export of auto-mapped code is also available, with detection of unrolled loops emitted as `REPT` blocks (`ZX-M8XXX/docs/tools.md` "Memory Map / Heatmap").
- Settings, watches, splitter sizes and per-file labels are stored in localStorage `zxm8_*` keys.

## 13. UI conveniences

- Four drag splitters, persisted and reset by double-click (`ZX-M8XXX/docs/debugger.md` "Layout Splitters").
- Fixed-width number fields, so register blocks do not twitch when a value crosses a digit boundary in decimal mode.
- Undo/redo for edits (`ZX-M8XXX/debug/undo-manager.js:2`).
- Bookmarks per pane with undo.
- Go-to palette.
- The PC auto-expands a collapsed fold it enters.
- A built-in Z80 opcode table, a programmer calculator (also usable in either pane), and a BASIC tab with copy and paste.
- Settings for Pentagon attribute prefetch and late ULA timing affect what the debugger shows for multicolor code (`ZX-M8XXX/docs/rendering.md`).

## 14. Notable and unique ideas

1. **Runtime reverse-engineering instruments aimed at a game.** Provenance ("which PC writes/reads/runs this range, from which call sites"), port-read provenance keyed by (pc, port), runtime indirect-jump targets, runtime call graph and SMC ranges, all exportable to Ghidra/SkoolKit/sjasmplus. These are a direct model for "who touched this" tooling.
2. **Differential run with a first-divergence report**, including a self-check that an empty change reports no divergence. Snapshot restore before both runs fixes the frame-phase trap. It is cheap to build on top of an existing snapshot plus trace.
3. **Behavior profiler that auto-labels subroutines** from observed port, screen and ISR behavior, plus hotspots classified by opcode pattern. It turns coverage data into names a human can navigate.
4. **Screen-region write breakpoints** (character cell or pixel rectangle, normal or shadow screen). This is a Spectrum-specific trigger kind that answers "who draws here" directly.
5. **Beam overlay with the previous frame greyed out** and a crosshair at the beam. It shows where the beam is while stepping, together with the partially rendered current frame, and has a border-only variant for border effects.
6. **Self-describing automation API for LLM/automation drivers** (`brief/capabilities/help/require`, a test that enforces the manifest, DOM checkpointing for killed headless runs, hex-only API independent of UI settings).
7. **Code-path set diffing** (baseline vs event A vs event B, intersection minus baseline) and trace-break at the first unseen PC. This isolates event handlers without reading any code.
8. **Trace with screen revert.** Per-instruction memory write logs (old/new) let the trace scrub the display back in time without snapshots.
9. **Tape-block, disk-sector and disk-read triggers** in the same list as CPU breakpoints. They are useful for loaders and protection code.
10. **Small, exact touches:** a T-state sum over a selection with branch-taken/not-taken totals, DJNZ loop-safety analysis for step-over, instruction history from fetched bytes, and the ΔT stopwatch.

## 15. Gaps and caveats

- **Conditions:** one comparison only (no `&&`/`||`, arithmetic or parentheses). The condition string is re-parsed with regexes on every evaluation. Literals are ambiguous: `10` is decimal but `1A` is hex, and `(1234)` is hex (`ZX-M8XXX/core/spectrum.js:5674`).
- **Bank keying is coarse.** ROM is only `R0`/`R1`, and the parser accepts RAM pages 0–7 only, so the 16 Scorpion and 64 Pentagon-1024 banks, +2A/+3 special paging and RAM-in-ROM cannot be targeted. $4000/$8000 are hard-coded to 5 and 2 (`ZX-M8XXX/core/spectrum.js:5603`, `:5638`).
- **Stored but never used:**
  - The `log` flag ("log without breaking") is normalized in `addTrigger` (`:6221`) but never read, so there are no tracepoints or log-only breakpoints.
  - Tape and disk triggers display `if cond` but never evaluate it (`checkTapeBlockTrigger`, `checkDiskReadTrigger`, `checkDiskSectorTrigger`, `:6534`, `:6548`).
- **Memory and port triggers scan the whole list** on every access (`:6485`, `:6502`). Only exec triggers have an O(1) set.
- **The trace ring uses `Array.shift()`** on a 100000-entry array (O(n) per entry once full) (`ZX-M8XXX/debug/trace-manager.js:65`, *(inferred cost)*). Trace screen revert is not bank-safe for $C000+.
- **No per-T-state views:** no event timeline, contention view, floating-bus or INT view in the UI. The beam view is a static overlay.
- **No remote debug protocol** (GDB/DeZog/socket). Automation is in-page JavaScript only, and the `zxDebug.spectrum` escape hatch is explicitly unstable.
- **Rewind is coarse** (2 s granularity, 30 states) and is not captured by headless frame runs.
- **Bug in `profile-game.html` heat-map export.** Auto-map values are plain numbers, but the export reads `.get(key)?.count`, so every count comes out as 0. It also looks up only unpaged keys, so banked addresses are missed (`ZX-M8XXX/profile-game.html:331`–`:334`; value shape at `ZX-M8XXX/core/spectrum.js:634`, `:9299`).
- **Documentation drift:** the README says "10,000 instructions" of trace, while the code default is 100000 (`ZX-M8XXX/README.md:46`, `ZX-M8XXX/debug/trace-manager.js:5`).
- **Fork `ZX-M8XXX-alfishe`:** an older snapshot (0.14.21). Its only own change is an "include ROM" option for profiling and the call graph (commit `f228b75`), and upstream has absorbed it (`ZX-M8XXX/ui/profiler-ui.js:31`). Nothing else is notable.
