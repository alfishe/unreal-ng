# Open questions for IDA Pro: Spectaculator 9.1 and ZXSpin 0.7

- **Date:** 2026-09-28
- **For:** the reverse-engineering agent working in IDA Pro on the project
  share (`IDA/Spectaculator91`, `IDA/ZXSpin`).
- **Why:** the surveys
  [spectaculator-debugger.md](spectaculator-debugger.md) and
  [zxspin-debugger.md](zxspin-debugger.md) were written from help files,
  resources, strings and earlier decompiler output. These questions need the
  code itself. The answers feed the unreal-ng debugger design
  ([debugger family](../2026-09-28-debugger-family/comparative-analysis.md)).

## How to answer

- Answer each question under its ID (`SPC-Qn`, `SPN-Qn`) with: the function
  addresses and names you confirmed or corrected, pseudo-code where asked, and
  the "expected answer" form each question gives.
- Mark each answer **confirmed** (read in code), **likely** (partial
  evidence) or **refuted**.
- Correct wrong names in the IDA databases as you go and list the renames
  (the surveys found several mislabels, e.g. ZXSpin `Dbg_RunSingleFrame`,
  `FrameTiming_ptr`, `Emu_RunFrame_Fast`).
- `Spectaculator/drivers/Debugger.dll` has no IDA database yet; load it first
  (MSVC RTTI is present).
- Return the answers as a markdown file named
  `ida-re-answers.md`; we fold them into the two surveys.

## Priority

Answer these first; they decide designs we are about to make:

| Priority | Questions | Topic |
|---|---|---|
| 1 | SPC-Q1, SPC-Q2, SPN breakpoint-record and check-loop questions | cost and granularity of breakpoint checks |
| 1 | SPC-Q3, SPN bytecode and precedence questions | condition language representation and semantics |
| 2 | SPC trace-log and cycle-breakpoint questions, SPN profiler questions | tracing and profiling data |
| 2 | SPC contention/beam-data questions | whether the core holds data it never shows |
| 3 | persistence formats (`.dzx`, symbol files), assembler debug info | import/export compatibility |

---

## Spectaculator 9.1 — open questions for IDA Pro

Companion to `docs/inprogress/2026-09-28-emulator-debugger-survey/spectaculator-debugger.md`.

Binaries:
- `exe` = `Spectaculator91/unpacked/Spectaculator.exe` (image base 0x400000; IDA db `Spectaculator.exe.i64` exists, 2,223 functions named by an earlier session — names may be wrong).
- `dbg.dll` = `Spectaculator91/Spectaculator/drivers/Debugger.dll` (image base 0x10000000; NOT yet in IDA; MSVC RTTI present, so class vtables are easy to recover via `.?AV...@@` strings; single export `CreateInstanceEx` @ string 0x10042fcf).
- Addresses below are from the prior RE docs (function VAs in exe) or computed string VAs (xref them).

---

#### SPC-Q1 — Breakpoint fast-reject structure and per-access cost
- **Question:** What exactly is the "fast lookup bitmap" (granularity, size, where it lives in the core object), and is it consulted separately for execute, memory-read, memory-write and I/O? How many instructions does the no-breakpoint path cost per memory access / per opcode fetch?
- **Why it matters:** We need a cheap breakpoint gate on every memory access; the prior doc contradicts itself ("64-bit per 1KB" vs "128 bits, 1 bit per 512 bytes"), which would be a very coarse filter.
- **Where to look:** `breakpoint_rebuild_bitmap` 0x41eb20, `z80_memory_watch_check` 0x4569d0, `z80_write_memory_with_breakpoint` 0x403780, `z80_fetch_opcode_contended` 0x45bd50, `z80_mem_read_byte` 0x457ba0; core-object offsets "+30..+37" cited in `Debugger_Breakpoints.md`.
- **Expected answer:** struct layout (bits per address block, separate bitmaps per access kind or not), pseudo-code of the gate, instruction count of the miss path, and whether the gate is skipped entirely when debugging is stopped (Shift+F5).

#### SPC-Q2 — Where the execute breakpoint is checked
- **Question:** Is the execute check done per M1 opcode fetch (including prefix bytes DD/FD/CB/ED and each LDIR repetition), once per instruction boundary, or in the main loop? Does a prefixed instruction at a breakpointed address break once?
- **Why it matters:** Determines break granularity semantics for prefixes, block repeats, HALT and interrupt acknowledge — cases we must specify for our own debugger.
- **Where to look:** `z80_execute_opcodes` 0x47aa60 (174 KB interpreter), `z80_execute_main_loop` 0x45c5e0, `z80_fetch_opcode_contended` 0x45bd50, xrefs to the hash lookup `hashtable_equal_range` 0x41f860.
- **Expected answer:** call sites list + pseudo-code; table of instruction classes vs. number of checks.

#### SPC-Q3 — Expression evaluator: representation, width, cost
- **Question:** Is the condition parsed once into an RPN node list (`CRpnNodeList`) and re-evaluated, or re-parsed per hit? What is the value type (16-bit `short` per prior doc, 32-bit int?), signedness of `<`/`>`, `>>` arithmetic vs logical, division-by-zero behavior (error state vs 0), and how `rb/rw/rwb` read memory (side-effect-free peek? current paging? contention/floating bus?)
- **Why it matters:** Our condition language needs defined width/signedness/error semantics; side-effect-free reads are mandatory.
- **Where to look:** `CRpnCalc_evaluate` 0x447e30, `debugger_eval_expression` 0x456220, RTTI `.?AVCRpnExpression@@` 0x5098e4 (vtable), `CRpnFunctionDef`, error strings "Function not found" 0x4d7810, "Division by zero" 0x4d79fc; register callback "vtable +4" with token ids 1005-1044.
- **Expected answer:** node struct, operator table (token → op), value type, per-evaluation cost estimate, peek function used by rb/rw.

#### SPC-Q4 — Order of condition, page filter and hit count; error state
- **Question:** Confirm the order page-filter → condition → hit-count increment. Does the hit counter increment when the condition is false? On an evaluation error, does it set state 2 and break once, then stay disabled? Is `hit_current` reset on edit/reload?
- **Why it matters:** Hit-count semantics differ across debuggers; we want to document ours against a respected reference.
- **Where to look:** `cheat_check_trigger` 0x40bf60 (likely really `CBreakpoint::ShouldBreak`), `CBreakpoint_set` 0x41e0e0, dzx loader in dbg.dll (xref `hitcounttype` 0x10039778).
- **Expected answer:** pseudo-code with the exact branch order and state transitions.

#### SPC-Q5 — Memory page identity for breakpoints and annotations
- **Question:** How is `mempageid` / `bank_filter` encoded (ROM n, RAM n, TR-DOS ROM, Multiface RAM/ROM, +3 special all-RAM modes)? For an execute breakpoint with the page checkbox, is the page taken from the 16K slot of the address at creation time? What happens for +3 special paging where RAM appears at 0000?
- **Why it matters:** Our bank-aware breakpoints and label/comment keying need a physical page id model; Spectaculator's is a proven UI.
- **Where to look:** `get_memory_page` 0x41e940, `get_hardware_slot` 0x41e9e0, `get_memory_page_flags` 0x41ea20, page-name strings "Service ROM" 0x4d9474 / "RAM %d" 0x4d94a4; dbg.dll string "Break only when executing from %s" (STRINGTABLE 1032) and `%s @$%04x (%s:$%04x)` 0x10039b1c.
- **Expected answer:** enum/encoding table for page ids, and the comparison performed at hit time.

#### SPC-Q6 — Break timing for memory and I/O breakpoints
- **Question:** When a memory-write breakpoint fires, has the write already happened? Does the CPU stop mid-instruction (and resume correctly) or finish the instruction first? Same for I/O read (is the port value visible?) and I/O write. How is the port mask applied (`(port & mask) == (addr & mask)`?).
- **Why it matters:** Mid-instruction stops complicate state capture; we need to choose and document the same contract.
- **Where to look:** `z80_memory_watch_check` 0x4569d0 → `z80_helper_1` 0x47aa20 (pause), `debugger_handle_break` 0x457320 (break type @ core+23308, address @ +23316), `z80_out_port` 0x402a90, `ula_port_write` 0x402b90, `zx_cheat_check_port` 0x456900.
- **Expected answer:** sequence diagram (access → check → pause → resume), whether the access is completed/deferred, and the mask expression.

#### SPC-Q7 — `.dzx` schema
- **Question:** Exact XML shape: which names are elements vs attributes, value encodings (hex/decimal, bptype and bpstate enums, hitcounttype enum), version attribute, how patches (`patchgroup`/`patch`, `start`, `bytes`) are stored.
- **Why it matters:** Possible import path for users migrating debugging sessions; also a reference design for our own project file.
- **Where to look:** dbg.dll TinyXML writers/readers; xref `zxdebuginfo` 0x100396a8 and neighboring keys (`hitcounttype` 0x10039778, `mempageid`, `errormsg`).
- **Expected answer:** annotated sample `.dzx` file plus enum tables.

#### SPC-Q8 — CPU instruction log
- **Question:** Confirm that menu id 40031 toggles the core trace (`debug_trace_set_state`) and that the file is fixed at `C:\temp\z80cpu.log`. What is "Cycle" (`%05d`: T-state in frame? since previous line?), is the line written per instruction via fprintf (cost), is it flushed, and are interrupts/HALT repeats logged?
- **Why it matters:** Trace format and cost inform our tracing design; the prior doc's sample line contradicts the format string.
- **Where to look:** exe `debug_trace_set_state` 0x4a6c00, `debug_trace_open_file` 0x4a6c30, `debug_trace_close_file` 0x4a6cf0, header string 0x4dab1c, line format 0x4dab64; dbg.dll path string 0x1003953c; core offsets +8336/+8340/+8344.
- **Expected answer:** real sample lines, meaning of each field, write path pseudo-code.

#### SPC-Q9 — Hidden instruction-fetch counting / coverage
- **Question:** What do `@LIMITINSFETCH`, `@GETINSFETCHES`, `@RESETREPOCH` do, who queries them, and what does `zx_mark_insn_bytes` (called from opcode fetch "if tracking enabled") record? Is there a code/data map or fetch counter in the core that no UI exposes?
- **Why it matters:** If the core already tracks executed bytes, that is a code/data-logger design worth copying (and a sign of what is cheap to do in the fetch path).
- **Where to look:** strings 0x4d95bc / 0x4d95dc / 0x4d95fc and their xrefs; `z80_fetch_opcode_contended` 0x45bd50 (the mark call); plugin query dispatcher `plugin_call_method` 0x458720.
- **Expected answer:** purpose of each key, data structure of the mark array (bits per address, per page?), consumers.

#### SPC-Q10 — "Break on cycle" mechanism (Run to Start/End of Frame)
- **Question:** How is a T-state breakpoint implemented — scheduler event (`z80_schedule_event` 0x4a6bd0) or a per-instruction compare? Why the fixed ~1000 T offsets? Is there a general cycle breakpoint the UI does not expose?
- **Why it matters:** We plan raster/T-state breakpoints; the scheduler-based approach would be zero-cost.
- **Where to look:** `z80_schedule_event` 0x4a6bd0, `z80_frame_start` 0x45a030 / `z80_frame_end` 0x459f00, dbg.dll handlers for commands 40019/40020 (find via accelerator/command dispatch), break-reason string id 2106.
- **Expected answer:** pseudo-code and the event id used; list of break sources that go through the same path.

#### SPC-Q11 — Run Until Condition / Run Until Event evaluation frequency
- **Question:** Is the Run Until expression evaluated after every instruction (cost!) or only on some events? How is "event" generalized (break reason 2108 "Break on event at %s - %s") — is there an event registry that other plugins (zxtape.dll → "tape stopped") post into?
- **Why it matters:** Design of our event-driven breakpoints (tape, FDC, AY) and their cost.
- **Where to look:** dbg.dll handlers for 40082/40083/40147, setting `RunUntilExpression` 0x1003aeac; exe side of `IZXDebugContainer` (RTTI 0x50aad4) methods.
- **Expected answer:** the IZXDebugContainer / IZXDebugger interface vtables with method semantics, and the event registration mechanism.

#### SPC-Q12 — Step Over / Step Out implementation
- **Question:** How does Step Over recognize CALL/CALL cc/RST/block repeats/HALT; is RST treated as a call? Does Step Out watch SP (break when SP > entry SP after RET) or just the next RET? How are temporary breakpoints stored (core+23300 per prior doc) and cleaned up if another breakpoint hits first?
- **Why it matters:** Reference semantics for our step commands, especially RST-based ROM calls and stack-manipulating code.
- **Where to look:** `debugger_handle_break` 0x457320 (removes temp BP at +23300), dbg.dll command handlers 40002/40003/40004/40005; `CZ80Disassembler` in exe for instruction-length queries.
- **Expected answer:** decision table per opcode class, and the termination condition for Step Out.

#### SPC-Q13 — Emulation/UI threading and pause protocol
- **Question:** Confirm the emulator thread blocks on a condition variable while the debugger UI is open, how the UI reads memory/registers safely, and how "T: n / m" in the status bar is obtained (core getter?). Does the watch window really update while running (polling timer? per frame callback?).
- **Why it matters:** We have the same split (core thread vs UI); live watches during run are a feature we lack.
- **Where to look:** `debugger_handle_break` 0x457320 (mutex +23208, condvar +23256, UI vtable +36), `z80_get_tstates` 0x456300, dbg.dll string "T: %d / %d" 0x10039684 xrefs, `ZxDbg.Watch.Changed` / `ZxDbg.Breakpoint.Hit` 0x10038f84 notification posts.
- **Expected answer:** thread diagram, the update mechanism and rate for watches, lock usage.

#### SPC-Q14 — Break reason codes
- **Question:** Map the core break types stored at +23308 (prior doc lists 0-7: normal, step into, mem read, mem write, step over, step out, run to cursor, "memory access with bank info") to the UI messages 2100-2108 (execute, mem read, mem write, I/O read, I/O write, interrupt, cycle, run-until expression, event). Where are I/O, interrupt, cycle and event reasons encoded?
- **Why it matters:** Shows the full set of break sources the core supports, including ones with no UI.
- **Where to look:** `debugger_handle_break` 0x457320 and its callers; dbg.dll code loading string ids 2100-2108 (LoadStringW xrefs).
- **Expected answer:** complete enum with the producer function for each value.

#### SPC-Q15 — What counts as a memory read
- **Question:** Do "On Memory Read" breakpoints fire on opcode fetches, operand fetches, stack pops, and block-instruction reads? Do non-CPU reads (ULA screen fetch, floating bus, Multiface/DivIDE-style traps, debugger peeks, `rb()`) ever trigger them?
- **Why it matters:** We must define the access classes our watchpoints see; a reference implementation's choice is useful evidence.
- **Where to look:** `z80_mem_read_byte` 0x457ba0, `z80_memory_read_floating` 0x4571d0, `z80_read_floating_bus` 0x402420, xrefs into `z80_memory_watch_check` 0x4569d0.
- **Expected answer:** table of read paths with check yes/no.

#### SPC-Q16 — Contention and beam data available in the core
- **Question:** How is contention computed (per-T-state table vs formula; the prior doc's `delay = tstates & 1` is clearly incomplete), where the per-line ULA fetch table (+5317, 224 entries) and border-change log (+12292/+12296, ≤6999 entries) are populated, and whether any of it is exposed to plugins.
- **Why it matters:** Spectaculator is timing-accurate but shows no beam/contention views; knowing its data model tells us what a zero-cost beam view could reuse.
- **Where to look:** `z80_add_memory_contention` 0x4023c0, `z80_fetch_opcode_contended` 0x45bd50, `zx_frame_border_change` 0x4598d0, `z80_read_floating_bus` 0x402420, timing offsets +43864..+43884.
- **Expected answer:** contention algorithm pseudo-code per model, table layouts, list of plugin-visible accessors.

#### SPC-Q17 — Unhandled-instruction break
- **Question:** Which opcodes count as "unhandled" (undefined ED xx? DD/FD before non-HL opcodes?), and how the "only while tracing" gate is implemented.
- **Why it matters:** The Z80 has no invalid opcodes; we need to know what Spectaculator treats as a trap-worthy instruction.
- **Where to look:** setting string "Break On Unhandled" 0x4d9594 → setting load → check inside `z80_execute_opcodes` 0x47aa60 (ED-table default handler).
- **Expected answer:** list of opcode patterns and the gating condition.

#### SPC-Q18 — Patch Maker and plist export
- **Question:** How are patches formed (from the current selection vs from grouped undo entries), what does a `.plist` patch contain (keys, data encoding), and what consumes it (iOS Spectaculator?).
- **Why it matters:** An undo-stack-to-patch workflow is a nice idea for our memory editor; format details decide whether it is worth copying.
- **Where to look:** dbg.dll RTTI `.?AVCPatchManager@@`, `.?AVCPatchMakerWnd@@`, `.?AVCDbgUndoManager@@`; plist writer (xref `!DOCTYPE plist` 0x1003a9a0); commands 40123-40131.
- **Expected answer:** data model, sample plist, relation between undo groups and patches.

#### SPC-Q19 — Watches: sysvar sizes and update policy
- **Question:** For system variables with size > 2 (KSTATE 8, STRMS 38, MEMBOT 30, FRAMES 3), what expression is generated (templates `rb(%d)` 0x1003b2e4 / `rw(%d)`)? How are watch changes detected and highlighted?
- **Why it matters:** Minor; informs our watch presets.
- **Where to look:** `CSysVarsDlg`, `CWatchExpression` in dbg.dll; XML resource 141 loader.
- **Expected answer:** size → expression mapping; change-detection code.

#### SPC-Q20 — Hidden disassembly export
- **Question:** Is command 40016 "Saves the disassembly to a file." (CZ80SrcExporter, DEFB/DEFW/DEFM formats) reachable from any UI (toolbar, context menu, Copy as DEFBs path), and what is its output format (labels? comments included?).
- **Why it matters:** Tells whether Spectaculator has a source exporter we did not see in the help.
- **Where to look:** dbg.dll RTTI `.?AVCZ80SrcExporter@@`, string `DEFB %s` 0x10038b4c, command dispatch for 40016 and 40148.
- **Expected answer:** reachable yes/no, sample output.

#### SPC-Q21 — Cheat engine sharing CBreakpoint
- **Question:** Does the cheat/POKE engine (pokeman.dll, `.pok` files) really use `CBreakpoint` records with conditions and actions, as the prior doc claims? If so, are there action types (write value on hit) that the debugger UI does not expose?
- **Why it matters:** "Breakpoint with action" (poke on hit, log on hit) is a feature we are considering; this would be prior art inside the same engine.
- **Where to look:** `cheat_apply_settings` 0x40c030, `cheat_load_and_apply` 0x41e270, `cheat_find_by_address` 0x41e7f0, `zx_cheat_check_port` 0x456900, pokeman.dll imports/exports.
- **Expected answer:** whether cheats are breakpoints, their action encoding, and the trigger path.

---

## ZXSpin 0.7 — open questions for IDA Pro

Target: `ZXSpin/ZXSpin.exe` (md5 b1086a60c692b98ace67dac2f5af136f), IDB `ZXSpin/ZXSpin.exe.i64`. Companion survey:
`docs/inprogress/2026-09-28-emulator-debugger-survey/zxspin-debugger.md`. Addresses are VAs. "MT:" names are
Delphi published methods recovered from the method table of `TDebuggerForm` (VMT method table at 0x549423) and
other forms. Some prior IDB names are misleading: `Dbg_RunSingleFrame` (0x55CDDC) = `TDebuggerForm.SingleStep`
(one instruction); `FrameTiming_ptr` = the Z80 register file; `Emu_RunFrame_Fast` (0x584360) = the debug-aware
frame loop. Please rename these in the IDB as you go.

---

**SPN-Q1 — Breakpoint record layout (32 bytes) and the unexplained fields**
- Question: What are fields +8 (string cleared at creation) and +16 (compiled condition buffer) of the breakpoint
  record at `TDebuggerForm+3572` (count at +3456)? Which bits of the key besides `0x10000000/0x20000000/0x40000000`
  and `0x80000000` exist (for example a "disabled" bit)? What do page ids `0x18/0x19` mean (they print no label)?
- Why it matters: shows how one record carries address, page, kind, condition and profiling links. This is the
  model we are weighing for our breakpoint manager.
- Where: `SetBreakPoint` 0x55C75C (creation and deletion, page-name switch); `Dbg_BuildBreakpointList` 0x56C57C;
  `Emu_PerOpcodeHook_Fast` 0x5847D0; `Edit4Click` 0x56C210; `Unset1Click` 0x56C3B4; `PopupMenu2Popup` 0x56AADC.
- Expected answer: a struct definition with every field typed, plus a table of key bits and page ids.

**SPN-Q2 — Is "Disable Breakpoint" implemented?**
- Question: `Disable1` in `PopupMenu2` has no `OnClick` in the DFM. Is it wired in code (`PopupMenu2Popup`
  0x56AADC or FormCreate 0x551464), and how is a disabled breakpoint represented and skipped?
- Why it matters: tells us whether ZXSpin has enable/disable semantics or only set/unset.
- Where: 0x56AADC, 0x551464, references to the `Disable1` component field.
- Expected answer: yes/no, and the code path plus the bit or field used.

**SPN-Q3 — Compiled check-list entry format and the cost of address-less conditions**
- Question: What are the four dwords of each 16-byte entry built by `Dbg_BuildBreakpointList` (packed through
  `sub_567868` into `dword_8D8688`)? For key `0x80000000` entries, what mask and key are emitted? Is the condition
  evaluated on every instruction?
- Why it matters: this is the per-instruction breakpoint cost model: a linear scan with mask compare, then the VM
  on a match.
- Where: 0x56C57C (two loops), `sub_567868` 0x567868, checker `sub_56C83C` 0x56C83C, `dword_8D8694` (index of
  the hit).
- Expected answer: entry struct `{?, mask, key, cond_ptr}` with the first dword explained, the values for
  address-less entries, and a note on ordering or early exit.

**SPN-Q4 — Full opcode map of the condition bytecode**
- Question: Complete the opcode table of the interpreter at 0x568F2C (jump table at 0x568F52): meaning of 0 vs 10
  (end or return), 32/33 (sub-expression and memory peek), 34 (negate), 35 (literal), 40/41/42 (byte, word, long
  reads at a literal address). How do the compiler's `.B/.W/.L` suffix opcodes 42/43/44 line up with handlers
  40..42? Is 43/44 an off-by-two bug?
- Why it matters: gives the exact expression semantics (memory-read width, sign handling) and whether size
  suffixes work at all.
- Where: compiler 0x56797C (`sub_567868(42|43|44)` near the `.B/.W/.L` literals at 0x568F10..0x568F28), handlers
  0x5692A7..0x5696D3, `sub_57716C` (byte read), `sub_5771BC` (word read), `sub_577158`.
- Expected answer: an opcode table (number, mnemonic, stack effect, source variable) and a yes/no on the suffix
  mismatch.

**SPN-Q5 — Condition grammar: precedence, parentheses, unary operators, numbers**
- Question: Confirm the compiler has no operator precedence (operator emitted after the right operand,
  accumulator VM). What does `(` compile to in a condition: a group (32) or a memory read? In `GetValue` 0x561484
  `(x)` is a byte peek. Which number formats are accepted (`$FF`, `#FF`, `0FFh`, `%1010`, decimal, `'A'`)? Are
  `NOT`, `!`, `~` supported? Is `&&`/`||` accepted?
- Why it matters: needed to describe the language accurately and to compare it with our condition language.
- Where: 0x56797C (tokenizer character sets `"_A-Za-z"`, `"'!_0-9A-Za-z"`, `"<>=!&A-Za-z"`), number parsing
  helper called from it, `sub_5696E0` (bracket matcher).
- Expected answer: an EBNF grammar with an evaluation-order statement and 3-4 worked examples.

**SPN-Q6 — Access-capture variables: exact semantics**
- Question: For `RADDR`, `WADDR`, `RPORT`, `WPORT`, `VALUE` and `RWWORD`: which accesses set `word_B9D8AC`,
  `word_B9D8AE`, `word_B9D8B0` and the flag bits in `byte_B9D8AB`? `VALUE` reads `word_B9D8AE >> 8` for byte
  accesses. Where is the data byte stored for reads and for writes? For multi-access instructions (`LDIR`, `EX
  (SP),HL`, `PUSH`, `CALL`, block I/O), which access wins? Are instruction fetches and M1 reads excluded? Are port
  reads (`RPORT`, flag bit 0) set anywhere? No `or byte_B9D8AB,1` was found.
- Why it matters: shows whether ZXSpin's memory and port breakpoints are exact or sampled once per instruction. We
  want per-access semantics and need to know the trade-off they chose.
- Where: writers `sub_57730C` (write, bit 3), `sub_577608` (read, bit 2), `sub_577920` (RMW, `0x0C`),
  `sub_578740` (16-bit read, `0x14`), `Z80_OutPort` 0x586D48 (bit 1); `Z80_InPort` 0x584CD0; the clear in
  `Emu_RunFrame_Fast` 0x584360; gate `byte_B9D8B2`.
- Expected answer: a table of access kind -> flag bit -> address/value slot, a list of all writers, and a
  statement on multi-access behavior and on the missing port-read writer.

**SPN-Q7 — When does a breakpoint fire relative to the instruction?**
- Question: The debug loop calls the hook before `Z80_ExecuteOpcode` and clears the access flags after it. So an
  execute breakpoint stops before the instruction, and an access condition becomes true one instruction later.
  Confirm this. On an access hit, is PC left after the accessing instruction? Does the UI show which access
  triggered?
- Why it matters: the break-before vs break-after contract for our watchpoints.
- Where: 0x584360 loop body, 0x5847D0, `byte_B9D8C1` (stop flag), `sub_43C758`, `sub_60FFD0(0,0x406,0)`
  (window message that opens the debugger).
- Expected answer: a timeline per case (exec, mem read, mem write, port write) with the PC shown on stop.

**SPN-Q8 — Run-mode selection and interaction with RZX playback**
- Question: `Emu_SelectRunMode` 0x608510 switches to the debug loop whenever breakpoints exist, replacing the RZX
  loops `sub_58426C` / `sub_583F68`. Does RZX playback or recording still frame correctly (instruction-count
  frames) while breakpoints are set? Does the debugger refuse, or does it desync?
- Why it matters: RZX-aware debugging is a feature we plan (TTD plus input journal). Knowing how ZXSpin handles
  (or breaks) it is useful.
- Where: 0x608510, 0x58426C, 0x583F68, `RZX_PlaybackNextFrame` 0x5752B4, `off_621DC8` (RZX mode).
- Expected answer: a description of the combined behavior, with any guard code.

**SPN-Q9 — Step Over, Exit Function, Run to Selected: exact algorithms**
- Question: Step Over plants "PC = next instruction" (`dword_8D86A4 = next - 0x1000000`). Does it treat
  CALL/RST/DJNZ/LDIR/HALT specially? What stops Exit Function (`0xFE……` target, `off_6221DC = 1`,
  `dword_B9D924` tested in the hook)? Is it a call-depth counter updated by CALL/RET in the core, or an SP
  compare? What does "Previous PC" (Ctrl+R, 0x572580) use: a ring of past PCs, or only the last one?
- Why it matters: step semantics to match or improve on.
- Where: `StepOver1Click` 0x56E864, `StepOverButtonClick` 0x56E9D4, `ExitFunction1Click` 0x56E96C,
  `RunToSelected1Click` 0x56E90C, writers of `dword_B9D924` and `off_6221DC` inside `Z80_ExecuteOpcode`
  0x5788A4.
- Expected answer: pseudo-code for each command.

**SPN-Q10 — Profiler arithmetic and % CPU**
- Question: How is "% CPU" computed (sum over which denominator: frame T-states, elapsed total since Clear
  stats)? Is the "Last" value the last duration? What happens when an entry breakpoint is hit twice before the
  exit (recursion, interrupts)? Is the T-state stamp `Emu_TotalTstates + frame_tstate` monotonic across frames?
  What do "Clear stats" and "Clear all" reset?
- Why it matters: profile blocks are the stand-out feature. We want exact semantics before copying the idea.
- Where: `Addprofileblock1Click` 0x571E58; hook 0x5847D0 (profile branch); `TProfilerForm` methods
  `Clearstats1Click` 0x520684, `PopupMenu1Popup` 0x520724; list refresh code (search for writes to
  `ProfilerListView`); 304-byte record at `TDebuggerForm+3580` (count +3576).
- Expected answer: the 304-byte record fully typed, the refresh formula for each column, and the re-entrancy
  behavior.

**SPN-Q11 — "Log executed" coverage bitmap: indexing bug?**
- Question: `Emu_RunFrame_Normal` sets `bittestandset(base + ((PC & 0x1FF0) >> 2), PC & 0x1F)` and the debug loop
  sets `bittestandset16(base + ((PC & 0x1FF0) >> 3), PC & 0xF)`. Do both index the same bit for a given PC? Which
  page ids map to the 32 x 1 KB slices? Is `Z80_DebugFlags` bit 0 the same flag as `off_622244` toggled by
  `LogPC1Click` 0x571DBC? Is anything besides the Exec column fed by it (for example Save Disassembly)?
- Why it matters: code/data logging design, and whether ZXSpin's Exec column is reliable.
- Where: 0x583CA0 (Normal loop), 0x584360, `ExecCoverageBitmap` 0x9C0C90, reader in `BuildDisassembly`
  0x552E18 region (`DEC chunk_07200_07400.c:5193`), `sub_571E08` (clear).
- Expected answer: the bit formula for both loops (same or different), the page-id map and the list of readers.

**SPN-Q12 — T-States column source**
- Question: Where do the per-instruction T-state figures in the disassembly come from: a static table (with
  taken/not-taken pairs like "13/8"), or measured? Do they include contention for the current PC?
- Why it matters: a static vs measured timing column is a design choice for our disassembly view.
- Where: `GetDisassemblyParts` 0x555288, `GetDisassembly` 0x55375C, `BuildDisassembly` 0x552E18; look for a
  byte table indexed by opcode.
- Expected answer: the table location and format, and whether contention is applied.

**SPN-Q13 — Assembler: "Generate debug info" and source-level debugging**
- Question: What does `GenDebugInfo` produce (address<->line map?) and how is it used? Do the assembler editor's
  Step/Step Over/Run to Selected work on source lines? Does the editor highlight the current PC line? How does
  "Underlay Source" (mode 4 of `TAsmFileForm`) overlay a source on the disassembly: labels only, or comments and
  lines too?
- Why it matters: source-level debugging of assembled code is a capability we lack.
- Where: `TAsmEditor` methods (method table 0x53454D: `ToggleBreakpoint1Click` 0x53DBF0, `Assemble1Click`
  0x538A34, `Execute1Click`), `TZ80Assembler` class methods (`TAsmSrcFile`), `UnderlaySource1Click` 0x56C8B0,
  `AssembleFile1Click` 0x56CA4C, form field +812 mode switch.
- Expected answer: the data structure of the debug info, the stepping behavior in the editor, and the underlay
  behavior.

**SPN-Q14 — Assembler feature set confirmation**
- Question: Confirm the directive set and semantics: `ASMREAD/ASMWRITE`, `DEFSW`, `LOAD`, `OFFSET`, `BASE`,
  `CODE/DATA` (sections?), `ENABLE/DISABLE`, `NOOPT`, the relocatable mode (which directive?), and the
  `PAGE`/`ORG` interplay with 128K banks. Are expressions evaluated with precedence here? Is there
  multi-pass forward-reference resolution ("Phasing error" suggests yes)?
- Why it matters: calibrating "full editor/assembler" claims in our comparison.
- Where: the directive table near the `TZ80Assembler` RTTI name (strings "ALIGN".."TSTI"), the parser dispatching
  on it, the error-string table ("Jump out of range" .. "Phasing error").
- Expected answer: a directive list with one-line semantics, and the expression grammar.

**SPN-Q15 — Symbol file format**
- Question: Full grammar of the symbol import (`sub_56CB04`): the meaning of type letters `d` and `e` (widths 4 and
  3 in the parser), the `t` variants (`td`/`tt` + hex delimiter), the length field, and the flag fields at record
  offsets +4..+20. Does Export Labels exist (no `OnClick`)? Is `Symbols.txt` auto-loaded or auto-saved (referenced
  from FormCreate 0x551464)?
- Why it matters: symbol interchange format for our import support.
- Where: 0x56CB04, 0x5724DC (Import), 0x551464, 0x551CE0 (FormDestroy: possible save).
- Expected answer: a line grammar with an example file, and the load/save behavior.

**SPN-Q16 — Command-line verbs `ASM`, `ASMR` and `RUN TO`**
- Question: In `ExecuteString` 0x560874, the `ASM`/`ASMR` branches look empty in Hex-Rays. Is that a decompiler
  artifact? What does `RUN TO` rewrite to (the string "UNTIL" is substituted)? Are there more verbs in a jump
  table Hex-Rays folded?
- Why it matters: completeness of the command-line inventory.
- Where: 0x560874 (literals at 0x561258..0x561480), disassembly around the `ASM` compare (literal at 0x561464).
- Expected answer: the verb list with syntax and effect.

**SPN-Q17 — Stop and Restore snapshot mechanics**
- Question: When the assembler runs code (Run/Debug), what state is saved: the 64-byte register block at
  `form+1560` plus PC, and a full snapshot via `sub_602870`? When is each restored? Does "Enable interrupts before
  running" set IFF only, or also IM? How is the return detected (the `0x40000000` breakpoint at which address, a
  RET to a sentinel)?
- Why it matters: "run as subroutine and roll back" is a useful workflow for our TTD and automation surfaces.
- Where: `sub_53B564` 0x53B564, the assembler's `Execute1Click` / `StopExecuting1Click` (TAsmEditor method table
  0x53454D), `sub_602870`, `sub_608240`.
- Expected answer: a sequence diagram of run -> hit -> restore, with the saved-state contents.

**SPN-Q18 — Breakpoint-hit handling and the log line**
- Question: What exactly is logged on a hit (the full register format string list at 0x551DEC), and is anything
  else recorded (T-state, frame, page)? Is there a hit counter or pass count per breakpoint?
- Why it matters: hit counts and log actions are standard in modern debuggers; we need to know whether ZXSpin has
  them.
- Where: `FormShow` 0x551DEC, `sub_56C83C` (`dword_8D8694` = hit index), `StatusBar1`.
- Expected answer: the log line format with a sample, and yes/no on hit counters.

**SPN-Q19 — Hardware Info data sources and any hidden ULA/beam display**
- Question: Does anything in the debugger read the ULA state (current line/T-state within the frame, floating bus,
  border) beyond the Hardware Info text and the "TStates" register label? Is there any overlay on the main screen
  showing the beam position when paused?
- Why it matters: confirms or refutes a ULA/beam view (the survey says none exists).
- Where: `BuildHardwarePage` 0x55F7E4, `UpdateRegisters` 0x55D42C, the renderer entry points
  `Display_RenderFrame` 0x59D3A8 / `Display_EndOfFrame` 0x4E2090 (look for a debugger-state check), `Timer1Timer`
  0x55C2EC.
- Expected answer: yes/no with the code path if yes.

**SPN-Q20 — Graphics ripper internals**
- Question: The ripper's "Line/Char/Screen" layouts and mask modes: exact byte orders, and what "Line Mod" does
  (stride?). Can it follow a register (combo "Program Counter / Stack Pointer / HL Register")? Any search or
  auto-detect code despite the missing UI?
- Why it matters: calibrates the "sprite/graphics finder" claim.
- Where: `TGfxRipper` methods (method table 0x52F85F: `PaintBox1Paint` 0x5308BC, `RGChange` 0x531070,
  `GridClick` 0x531108), `FormCreate` 0x52FBD8.
- Expected answer: layout formulas and yes/no on any search capability.

**SPN-Q21 — RZX rollback and RZX Studio**
- Question: Can the user roll back to an earlier embedded snapshot during recording (the "first snapshot block"
  message suggests yes)? What key or menu triggers it? What do the nine buttons of `TRZXSnapWindow` do (method
  table 0x57313A)?
- Why it matters: RZX rollback is the only "history" feature in ZXSpin; it is relevant to our TTD comparison.
- Where: `sub_575DB4` 0x575DB4, `RZX_StartRecording` 0x575028, `TRZXSnapWindow` methods
  (`FastIMG1MouseDown`), main-window `MenuItemClick` 0x60EDD0 (RZX items), `Emu_CheckHotkeys`.
- Expected answer: the rollback workflow and the button map.

**SPN-Q22 — Hotkey to open the debugger and pause semantics**
- Question: Which key opens the debugger from the running emulator (toolbar "Open the Debugger" exists, no menu
  shortcut)? Does opening it pause mid-frame at the current instruction or at the end of the frame?
- Why it matters: UI parity and pause-granularity behavior.
- Where: `Emu_CheckHotkeys`, `ToolButtonClick` 0x60E0D8, `sub_60FFD0(0,0x406,0)` handler (`WndProc` 0x60BA1C),
  `Dbg_*` open path in `FormShow` 0x551DEC.
- Expected answer: the key, and "mid-frame at instruction boundary" or "frame end".

**SPN-Q23 — Action script semantics**
- Question: Units of `WAIT` (frames or ms), key-name syntax for `KEYPRESS/KEYDOWN/KEYUP` (`sub_4E29E0`), what
  `DIR` does, whether `SNAPSHOT` loads or saves, and whether the queue (`dword_6304A8`) is deterministic with RZX
  recording.
- Why it matters: comparison with our automation and input-injection surfaces.
- Where: `str_openActionScript` 0x4E2364, `sub_4E29E0`, the consumer of `dword_6304A8` queue entries.
- Expected answer: the command reference with a sample script.
