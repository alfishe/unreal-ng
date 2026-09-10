# Unreal Speccy (SMT/deathsoft) — Conditional Breakpoint System: Research Report

Sources: actual source of the classic Windows emulator ([mkoloberdin/unrealspeccy](https://github.com/mkoloberdin/unrealspeccy), 0.38.x line by SMT/Alone Coder/deathsoft — parser in `dbgbpx.cpp`, evaluation in `debug.cpp`, hooks in `io.cpp`/`z80_main.inl`), its bundled docs (`doc/unreal_e.txt` English / `doc/unreal_r.txt` Russian, CP1251), and the actively maintained ZX-Evo fork ([tslabs/zx-evo](https://github.com/tslabs/zx-evo), `pentevo/unreal/Unreal/debugger/dbgbpx.cpp`). Everything below about grammar is taken from code, cross-checked against the official docs.

---

## 1. Architecture in one paragraph

The debugger ("monitor", opened with the tilde key; breakpoints manager on **Alt-C**) has three separate breakpoint mechanisms:

1. **Conditional breakpoints (cbp)** — up to `MAX_CBP = 16` expressions per CPU (`unsigned cbp[16][128]` in `z80/defs.h`). Each is an infix C-like expression compiled by `toscript()` (shunting-yard, `dbgbpx.cpp:123`) into an RPN token stream, evaluated by a tiny stack machine `calc()` (`dbgbpx.cpp:24`) **before every instruction** in `debug_events()` (`debug.cpp:269`). Any nonzero result breaks. The same expression engine powers the 4 **on-screen watches**.
2. **Execution breakpoints (bpx)** — a per-address bit `MEMBITS_BPX` in a 64K `membits[]` array; supports ranges (`6000-FFFF`). Checked at PC fetch.
3. **Memory access breakpoints** — `MEMBITS_BPR` / `MEMBITS_BPW` bits in the same array, checked inside the debug-core `rm()`/`wm()` (`z80_main.inl:10-39`).

Port breakpoints have **no dedicated mechanism** — they are done through the cbp language via `OUT`/`IN`/`VAL` pseudo-variables, set by the `in()`/`out()` handlers (`io.cpp:18`, `io.cpp:621`, `io.cpp:965`) and reset to `-1` (`0FFFFFFFF`) after each cbp evaluation pass (`debug.cpp:282`).

If any cbp exists (or any membit is set), `isbrk()` switches the emulator to the slow "debug Z80 loop" — this is the system's main performance cost.

## 2. The cbp expression language (from source)

### Operands (classic 0.37/0.38, `DECL_REGS` table, `dbgbpx.cpp:72-120`)

| Operand | Meaning | Size |
|---|---|---|
| `A F B C D E H L`, `AF BC DE HL` | main registers | 8/16-bit |
| `A' F' B' C' D' E' H' L'`, `AF' BC' DE' HL'` | alternate set | 8/16-bit |
| `PC SP IX IY I R` | control registers (`R` is `r_low`, refreshed before eval) | 16/8-bit |
| `DOS` | 1 if TR-DOS active — reads `comp.flags & CF_DOSPORTS` (`get_dos_flag`, `dbgbpx.cpp:67`) | bool |
| `FD` | last value written to port `#7FFD` (`comp.p7FFD`) — i.e. current 128K paging state | 8-bit |
| `OUT` | port of the OUT executed by the previous instruction, else `0FFFFFFFF` | 32-bit |
| `IN` | port of the IN executed by the previous instruction, else `0FFFFFFFF` | 32-bit |
| `VAL` | byte written/read by that OUT/IN | 8-bit |
| `0A5F3` etc. | **hex only**, must start with a digit (`DFFD` is rejected, write `0DFFD`); no decimal, no `#`/`$`/`0x` prefixes | 16-bit push |
| `'A'` | character literal | 8-bit |

Input is case-insensitive (uppercased in place, apostrophes preserved). Note `DOS` is in the code but **missing from the bundled docs** — it was added after 0.37.

### Operators (priority table `prio[]`, `dbgbpx.cpp:126-155`; lower number = binds tighter)

| Prio | Operators |
|---|---|
| 1 | `!` `~` `M(x)` (unary), `->` |
| 2 | `*` `/` `%` |
| 3 | `+` `-` |
| 4 | `>>` `<<` |
| 5 | `>` `<` `=` `==` `<=` `>=` `!=` (`=` is an alias of `==` "for Pascal programmers") |
| 6 | `&` |
| 7 | `^` |
| 8 | `\|` |
| 9 | `&&` |
| 10 | `\|\|`, `(` `)` |

- **Memory dereference**: `M(x)` = byte at Z80 address `x`; `x->y` = byte at `x+y` (`calc` case `'->'`: `DirectRm(*sp + sp[-1])`), so `pc->1` is the byte after the opcode. Docs: "M(x) is byte from memory address x (same as x->0)".
- All arithmetic is 32-bit unsigned; division/modulo by zero is silently skipped.
- Documented footgun (verbatim from `unreal_e.txt`): *"If you are not familiar with C, use brackets as much as possible … `out & 0FF == 0FE` is treated as `out & (0FF == 0FE)` <- always 0"* (comparisons bind tighter than `&`, like C).
- Expressions are validated on entry by compiling and test-executing once; a stack imbalance sets `calcerr` and the UI shows the famous **"Error in expression — Please do RTFM"** message box. Stored breakpoints are decompiled back to text by `script2text()` for the list box (so what you see after re-editing is the fully parenthesized normal form).

### Canonical examples (from the bundled docs, verified against the grammar)

```
(out+1) | (in+1)                            ; IN or OUT to any port
(in & 8001) == 0                            ; read of keyboard half-row B..SPACE
!(out & 1)                                  ; any OUT to port #FE
(out & 0FF)==0BB && (val==0F3 || val==0F4)  ; General Sound reset
(out & 0FF)==0FD && (val&7)==3              ; page 3 being mapped in
(FD & 7) == 3                               ; ...same, via #7FFD latch state
M(pc)==0CB && pc->1 >= 10 && pc->1 <= 17    ; break on executing RL <reg>
DOS && pc < 4000                            ; executing TR-DOS ROM code
```

## 3. Breakpoint types and UI

| Type | How set | Storage | Granularity |
|---|---|---|---|
| Conditional (cbp) | Alt-C manager, CBP edit box + Add; double-click list entry to edit (it moves back into the edit field) | `cpu.cbp[16][128]`, **not persisted** across sessions | evaluated before every instruction |
| Execution (bpx) | **SPACE** on a line in the CPU/disasm window, or Alt-C manager with address or range `START-END` (hex) | `membits[64K] & MEMBITS_BPX`, persisted to `bpx.ini` as `x0=0x6000-0xFFFF` | Z80 address, ranges OK |
| Memory read/write | Alt-C manager, address/range + `R` / `W` checkboxes (can combine) | `MEMBITS_BPR/BPW` bits, persisted (`r0=`, `w0=` lines) | Z80 address, ranges OK |
| Port in/out | no dedicated type — cbp expressions over `OUT/IN/VAL` | — | fires on the instruction after the I/O |
| Other stop aids | F4 trace-to-cursor (`dbg_stophere`), step-over via `dbg_stopsp`/loop range (`debug.cpp:249-267`) | — | — |

Related debugger keys: `~` enter monitor, Alt-C breakpoint manager, SPACE toggle bpx, F4 run-to-cursor, F7 step, F8 step-over, Ctrl-G goto, Alt-F7/Ctrl-F7 find text/code-with-mask. `bpx.ini` lines are `<type r|w|x><cpu#>=0xSTART[-0xEND]` — note cbp expressions are *not* saved there (a common annoyance).

## 4. Paging/banks handling

- `membits[]` is indexed by **CPU address (0000-FFFF), not physical address**: an execution or R/W breakpoint fires for whatever bank happens to be mapped there. There is **no bank-qualified breakpoint** in the classic emulator.
- The idiomatic workaround is a cbp conjunction using the paging state operands: `DOS` (TR-DOS ROM mapped/active flag) and `FD` (the `#7FFD` latch, e.g. `(FD & 7)==3` for RAM page, `FD & 10` for ROM select bit). There are **no `ROM`/`RAM` operands** in the classic source — the `trace RAM`/`trace ROM` checkboxes in the watch dialog only filter the execution-trace/LED display, not breakpoints.
- `M(x)`/`->` dereference through `DirectRm()` → virtual `DirectMem()`, i.e. they read via the **current** paging of the emulated machine model.
- Multi-CPU aware: breakpoints are per-CPU (`CpuMgr`), covering the General Sound's Z80; `bpx.ini` encodes the CPU index.

## 5. The ZX-Evo/PentEvo fork extensions (tslabs)

The actively developed fork (`tslabs/zx-evo`, `pentevo/unreal/Unreal/debugger/dbgbpx.cpp:77-88`) keeps the same grammar and adds operands that directly answer the classic version's biggest gap:

| New operand | Meaning |
|---|---|
| `RD` | address of the memory **read** performed by the last instruction (else `0FFFFFFFF`) |
| `WR` | address of the memory **write** performed by the last instruction (else `0FFFFFFFF`) |
| `MDT` | the data byte read/written (`brk_mem_val`, set in `z80_main.inl`) |
| `PG0`..`PG3` | TS-Config page registers for each 16K window — bank-aware conditions at last |

So `WR==5C78 && HL==500` or "break when `#34` is written anywhere" (`MDT==34 && WR!=0FFFFFFFF`) work in the fork but not in classic 0.37/0.38.

## 6. Strengths and pain points

**Strengths**
- Genuinely expressive: one small language covers value-conditions, opcode-pattern breaks (`M(pc)==0CB && ...`), port I/O with data matching, and paging-qualified breaks — capabilities many contemporary Spectrum debuggers lacked; the docs proudly note breakpoint-on-range/port/instruction all fall out of it.
- Pre-compiled to RPN, so per-instruction evaluation is cheap for what it does; expressions validated at entry.
- Range breakpoints as first-class UI (`6000-FFFF` in one line), backed by the elegant 64K `membits` bit-array (also reused for the "ripper's tool" R/W/X usage marking).
- bpx/mem breakpoints persist across sessions (`bpx.ini`), per-CPU (GS Z80 debuggable too).

**Pain points**
- **Hex-only, digit-first literals** (`0DFFD` not `DFFD`, no `#FFFD`) and C precedence trip users constantly — acknowledged in the docs themselves ("Please do RTFM" is the actual error dialog).
- **cbp and access breakpoints don't compose** in the classic version: users on the tslabs forum explicitly asked for it — *"Я хочу возможность добавлять это в условия … остановка если И запись в определенную ячейку И например HL=500"* and *"точки останова при выводе чего-нить на аигрек/бипер"*. The fork's `RD/WR/MDT` operands were the answer.
- `OUT`/`IN`/`VAL` fire only on the *next* instruction boundary (values latched in `io.cpp`, checked in `debug_events`, then reset), so you stop after the I/O instruction, not at it.
- Any active cbp forces the slow debug core for the whole session (`isbrk()`), and all 16 expressions are evaluated before every instruction — noticeable slowdown.
- cbp expressions are not saved to `bpx.ini`; only r/w/x address breakpoints persist.
- No bank-qualified address breakpoints (only the `DOS`/`FD` conditional workaround); no symbolic labels in expressions (labels exist elsewhere in the monitor, but the parser accepts only registers/hex); silent 16-entry cap; fixed 128-token script size; div-by-zero silently ignored.
- Fixed-size, unresizable text-mode debugger UI (a recurring forum request: resizable windows, fonts, syntax highlighting).

## Sources

[mkoloberdin/unrealspeccy](https://github.com/mkoloberdin/unrealspeccy) · [doc/unreal_e.txt](https://github.com/mkoloberdin/unrealspeccy/blob/master/doc/unreal_e.txt) · [tslabs/zx-evo](https://github.com/tslabs/zx-evo) · [TS Forum: Unreal Speccy — обсуждение нового функционала](https://forum.tslabs.info/viewtopic.php?f=29&t=321) · [Документация UnrealSpeccy (nedoos.ru)](http://nedoos.ru/index.php/14-all/programmistam/24-dokumentatsiya-unrealspeccy)
