# Conditional breakpoints

A memory or port breakpoint can carry a **condition**: an expression checked every time the breakpoint matches. It
stops only when the expression holds. Example: `bp 0x8000 if A == 7 && M(#5C3A) != 0` stops at `#8000` only when A is
7 and the byte at `#5C3A` is not 0.

The condition is checked only after the address matched. A machine without conditional breakpoints pays nothing,
and a condition on one address costs nothing at other addresses.

Design and measurements: [conditions-and-callbacks.md](../inprogress/2026-10-26-breakpoint-taps/conditions-and-callbacks.md).
Task recipe: [.recipe/analysis/conditional-breakpoints.md](../../.recipe/analysis/conditional-breakpoints.md).

## Contents

- [The language](#the-language)
- [Operands](#operands)
- [Counting and hit policies](#counting-and-hit-policies)
- [Checks when a condition is set](#checks-when-a-condition-is-set)
- [A condition that fails at run time](#a-condition-that-fails-at-run-time)
- [Every plane](#every-plane)
- [Debugger protocols](#debugger-protocols)
- [The sound card's CPU](#the-sound-cards-cpu)
- [Game events and haptics](#game-events-and-haptics)
- [Limits of this version](#limits-of-this-version)

## The language

The language is Unreal Speccy's (the canonical one). A second dialect, `c`, reads the same names with C's operator
precedence and decimal numbers. The emulator always prints a condition back in the Unreal form, with the brackets
that show how it was read and numbers from 10 up with `#`: `a==7&&b>3` is shown as `(A == 7) && (B > 3)`,
`val&7==2` as `VAL & (7 == 2)`, `wr==5800` as `WR == #5800`.

**Precedence** (highest first):

| Level | Operators |
|:--|:--|
| 1 | unary `!` `~` `-` |
| 2 | `M(x)` `W(x)` `a->b` |
| 3 | `*` `/` `%` |
| 4 | `+` `-` |
| 5 | `<<` `>>` |
| 6 | `==` `!=` `<` `>` `<=` `>=` (`=` and `<>` are accepted too) |
| 7 | `&` |
| 8 | `^` |
| 9 | `\|` |
| 10 | `&&` |
| 11 | `\|\|` |

In the Unreal order, comparisons bind **tighter** than `&`, `^` and `|`. So `A & 7 == 2` is `A & (7 == 2)`, which is
always 0. To test bits, write the brackets: `(A & 7) == 2`. In the `c` dialect `A & 7 == 2` reads the same way (C's
own trap), so the brackets are the safe habit in both.

`&&` and `||` stop as soon as the answer is known: in `HL == 4000 && M(HL) == 0` the memory is not read when HL is
something else. Arithmetic is 64-bit. Division and modulo by 0 give the left operand (as in Unreal). `M(x)` and
`W(x)` addresses wrap at 64K.

**Numbers** are hex unless written as decimal:

| Written | Value |
|:--|:--|
| `FF`, `0FF`, `#FF`, `$FF`, `0xFF`, `FFh` | 255 |
| `255d` | 255 (decimal) |
| `'A'` | 65 (a character) |

A hex number that starts with a letter must start with `0` or `#`: `C000` is a name, `#C000` and `0C000` are numbers.
The `d` suffix means decimal, so `0D` is decimal 0 and `13d` is 13. Write the hex value 13 as `#0D` or `0Dh`. In the
`c` dialect plain digits are decimal and hex needs `0x`, `#`, `$` or `h`.

Names are case-insensitive. A name that is not a register or a variable is looked up as a label. A leading `.` forces
the label: `.PC` is the label `PC`, not the register.

## Operands

| Operand | Value |
|:--|:--|
| `A F B C D E H L`, `A' F' B' C' D' E' H' L'` | 8-bit registers (`C` is the register, the carry flag is `CY`) |
| `IXH IXL IYH IYL` (also `XH XL YH YL HX LX HY LY`) | halves of IX and IY |
| `AF BC DE HL AF' BC' DE' HL' IX IY SP PC` | 16-bit registers |
| `WZ` / `MEMPTR` | the internal WZ register |
| `I R IM IFF1 IFF2 HALTED` | interrupt state and the HALT flag (0 / 1) |
| `Z NZ CY NC PE PO FM FP FH FN` | flags: 1 when the condition holds |
| `M(x)`, `a->b` | the byte at `x`, at `a + b` (read without side effects) |
| `W(x)` | the little-endian word at `x` |
| `RD`, `WR` | read / write address of the access (aliases `MRA`, `MWA`, `RADDR`, `WADDR`) |
| `MDT` | the byte read or written (aliases `MRV`, `MWV`) |
| `IN`, `OUT` | the port of an IN / OUT (aliases `PRA`, `PWA`, `RPORT`, `WPORT`) |
| `PWV` | the byte an OUT writes |
| `VAL` (`VALUE`) | the byte of the access: memory read / write or port out |
| `ADDR` | the address or port of the access |
| `PORT` | the port of an IN or OUT |
| `T` | T-state of the frame |
| `FRAME` | the frame counter |
| `HITS` | the hits so far (those whose condition held) |
| `FD` | the last value written to `#7FFD` |
| `DOS` | 1 while TR-DOS is paged in |
| `PG0 PG1 PG2 PG3` | the RAM page in each 16K slot (`#FF` for ROM) |
| `ROMPG` (`ROMPG0`), `ROMPG1 ROMPG2 ROMPG3` | the ROM page in each 16K slot (`#FF` for RAM) |
| `MMU0`..`MMU7` | the 8K RAM page in each 8K slot: the Next's MMU value as the CPU sees it, elsewhere twice the 16K page plus the half (`#FF` for ROM) |
| `DFFD`, `P1FFD`, `EFF7`, `FF77` | the last value written to `#DFFD`, `#1FFD`, `#EFF7`, `#FF77` (aliases `PDFFD`, `PEFF7`, `PFF77`; `P7FFD` is `FD`) |
| `CPUSPEED` | the CPU clock in units of 3.5 MHz: 1, 2 (7 MHz), 4 (14 MHz), 8 (the Next's 28 MHz), 6 (the Sprinter's 21 MHz) |
| `NR(x)` | ZX Next: the NEXTREG register `x` as a read of `#253B` returns it, without the read's side effects |
| `VC`, `HC` | ZX Next: the beam's line counter (the copper's, NR #64 applied) and the pixel clock in the row |
| `CPC` | ZX Next: the copper's instruction index |
| `x@prev` | the value of `x` at the previous check of this condition (the first check: `x` itself), at most 16 in a condition: `M(#5C3A) < M(#5C3A)@prev` holds when the byte went down. A branch `&&` / `\|\|` skipped does not update its value |

Each access operand exists only on the breakpoint kinds that supply it:

| Breakpoint | Operands it supplies |
|:--|:--|
| execution | none (registers and memory only) |
| memory read | `RD`, `MDT`/`VAL`, `ADDR` |
| memory write | `WR`, `MDT`/`VAL`, `ADDR` |
| port in | `IN`, `PORT`, `ADDR` |
| port out | `OUT`, `PWV`/`VAL`, `PORT`, `ADDR` |
| NEXTREG write | `OUT`, `PWV`/`VAL` the byte written, `PORT`/`ADDR` the register; `NR(x)` is still the value before the write |
| NEXTREG read | `IN`, `PORT`, `ADDR` the register |
| sound chip register (`bchip`) | `OUT`, `PWV`/`VAL` the byte written, `PORT`/`ADDR` the register |
| copper, beam, magic opcode | none |

`NR(x)`, `VC`, `HC` and `CPC` exist on the Next only; on another machine the condition is refused when it is set ("no NR
here"). A sound card CPU's breakpoint has none of the machine's operands (`NR`, the latches, the beam, `CPUSPEED`).

A breakpoint on reads and writes (`wp … rw`) supplies both sets. On a given access, the operand of the other kind is
`FFFFFFFF` (as in Unreal). A port-in breakpoint has no value: the value is not known when the check runs.

## Counting and hit policies

A conditional breakpoint has two counters:

- `match_count` counts every access the breakpoint matched.
- `hit_count` counts the matches whose condition held.

A hit policy (`--hits 5`, `>=5`, `%5`) counts `hit_count`. So `bp 0x8000 --hits 3 if A == 7` stops the third time
#8000 runs with A = 7. `HITS` inside the condition is the count before this match. Resetting the hit counters resets
both counters.

## Checks when a condition is set

A condition is checked when it is set, whether with the breakpoint or later with `bpcond` / `bp_condition`. A bad
condition is refused with the reason and its column:

| Text | Refused because |
|:--|:--|
| `A ==` | `condition: operand expected at column 5` |
| `RD == 9000` on an execution breakpoint | `RD is not supplied by this breakpoint's trigger` |
| `VAL == 1` on a port-in breakpoint | the same: the value of an IN is not known when the check runs |
| `NOSUCH == 1` | `unknown name 'NOSUCH'` (not a register, a variable or a loaded label) |
| a condition on a keyboard or an event breakpoint | `a condition applies to memory, port, NextREG, copper, beam, magic and chip register breakpoints` |
| `NR(7) == 0` on a machine without NEXTREG | `the condition does not evaluate here: no NR here` |

A refused text leaves the old condition in place. An empty text removes the condition. With a machine present the
condition is also run once on the current state: an expression that cannot be computed there is refused too.

Labels are looked up when the condition is set: a label moved later does not move the condition (set it again).

## A condition that fails at run time

A condition that cannot be computed when it runs **never stops the machine**. The breakpoint is marked instead:

- `condition_error` holds the reason, with the frame and PC where it happened;
- `bplist` shows `[condition failed: …]`;
- every surface gets one `breakpoints_changed`;
- the breakpoint is skipped (not counted) until it is enabled again or given a new condition.

With the checks above, this happens only when the state a condition needs is missing.

## Every plane

| Plane | Set with the breakpoint | Change later | Shown |
|:--|:--|:--|:--|
| CLI | `bp 0x8000 if A == 7`, `wp 0x4000-0x57FF w if VAL != 0`, `bport 0xFE o --mask 0xFF if (VAL & 7) == 2`; `--dialect c` before `if` (the CLI reads a plain address as decimal, the condition reads plain numbers as hex) | `bpcond <id> [--dialect c] [--cpu gs] [condition]` (none: clear) | `bplist`: `if …`, `(matched Nx)`, `[condition failed: …]` |
| Lua | `bp(0x8000, {condition = "A == 7", dialect = "c"})`, also `bp_read`, `bp_write`, `bp_port_in`, `bp_port_out`; `-1, reason` when refused | `bp_condition(id, text [, dialect [, cpu]])`: `true` or `false, reason` | `bp_status()`: `condition`, `match_count`, `condition_error` |
| Python | `emu.bp(0x8000, condition="A == 7", dialect="c")`, the same kinds; `ValueError` with the reason when the condition does not compile | `emu.bp_condition(id, text, dialect="", cpu="")`: `True`, `False` for an unknown id, `ValueError` for a bad text | `emu.bp_status()`: the same keys |
| MCP | `control_execution` `bp_add` with `condition`, `condition_dialect`; the text answers `#id if <canonical>` | `bp_condition` with `bp_id`, `condition` (`''` clears), `cpu` | `bp_list`: `hits N matched M if …` |
| REST | `POST /breakpoints {…, "condition": "A == 7", "condition_dialect": "c"}` (400 with the reason) | `PUT /breakpoints/{bp_id}/condition {"condition", "dialect"}` (`?cpu=gs`) | `condition`, `match_count`, `condition_error` in every breakpoint object |
| WebSocket | the REST routes as `call`s | the same | changes pushed as `breakpoints_changed` |
| Catalogue | `dbg.bp_add` with `condition`, `dialect` | - | - |

## Debugger protocols

The three debugger protocols pass the condition of a client's breakpoint to the core, written in this language. The
client still evaluates its condition on every stop (DeZog, gdb and the ZEsarUX evaluator all do), so the core form is
only a filter: it spares the stops where the client's condition is false. A text the translator cannot write so that
the core answers exactly as the client is not passed on; the breakpoint is then set without a condition, it stops on
every hit and the client decides, as before. The server logs the reason.

| Protocol | Client condition | Taken | Kept by the client |
|:--|:--|:--|:--|
| ZRCP (ZEsarUX, DeZog `zrcp`) | `set-breakpoint N PC=8000h AND A=7` | registers, flags (`F`, `FS FZ FH FP FV FN FC`), `PEEK`/`PEEKB`/`PEEKW`, `+ - *`, `/` by a nonzero number, `& \| ^` (bound like `*`, as ZEsarUX's calc), `AND OR XOR NOT`, `= <> != < <= > >=` | `ROM`/`RAM` (ZEsarUX's paging: the ROM in slot 0, the bank at `C000h`), a division by a register, unknown names |
| DZRP (DeZog `remoteType: "cspect"`) | the JavaScript condition of a DeZog breakpoint, `A == 7 && HL > 0x4000` | the registers in any case (`a'`, `IXH`, `IR`), numbers in DeZog's forms (`0x`, `$`, `h`, `b`, decimal, masked to 16 bits), `+ - * %`, the bitwise operators, shifts, comparisons, `&& \|\| !` | labels and letter-led hex (`FFh` could be a label), `/` (a fraction in JavaScript), `**`, a `%` that can divide by 0, values JavaScript truncates to 32 bits, strict equality between a truth and a number, `\|\|`/`&&` used as a number |
| GDB RSP | `break *0x8000 if $a == 7` (sent as agent expressions: `Z0,8000,1;X…`) | the bytecode of the expression: constants, registers (pairs, `ir`, `iff`, `im`, `p7ffd`), memory reads of 8, 16 and 32 bits, arithmetic, comparisons, sign and zero extension, forward jumps (each `if_goto` a path, at most 64) | loops, `trace`, `printf`, state variables, floats, `p1ffd`/`pfe`, a division by anything but a nonzero constant, a shift by a register |

The server advertises `ConditionalBreakpoints+` in its `qSupported` reply, so gdb sends the condition with the
breakpoint by itself (`set breakpoint condition-evaluation auto`).

## The sound card's CPU

A breakpoint of the General Sound / NeoGS card's Z80 (`cpu` `gs`,
[breakpoints-and-traces.md §8](../emulator/design/debugger/breakpoints-and-traces.md#8-the-sound-card-cpu)) takes a
condition in the same language, on every plane, at creation or later (`--cpu gs`, `cpu = "gs"`, `?cpu=gs`). The
operands are the card's:

- the registers and flags are the card CPU's at the access;
- `M(x)` / `W(x)` read the card's memory as its CPU sees it now, without side effects;
- `PG0`..`PG3` are the card's RAM page in each 16K window (`#FF` for ROM or flash), `ROMPG` its ROM / flash page at
  `#0000`;
- `FRAME` and `HITS` are as on the machine (`FRAME` is the machine's frame counter);
- `T`, `FD` and `DOS` are the machine's state and are refused ("FD is the machine's, not this CPU's");
- labels name the main CPU's addresses and do not resolve.

```
bp 0x0270 --cpu gs if (A & 1) == 0 && M(#026E) == #DB
wp 0x4000-0x7FFF w --cpu gs if PG1 == 0 && VAL == #FF
```

Card events (`bevent`) take no condition. A condition never reaches across CPUs: a main CPU breakpoint cannot test
the card's registers (`gs.A`), nor the reverse.

## Game events and haptics

A game profile's sources (`events:`, `haptics.sources`) take `if:` in this language: a breakpoint probe checks it at
the access, any other source at the frame end. See
[haptics.md §5](../inprogress/2026-10-17-controller-manager/haptics.md).

## Limits of this version

- Conditions apply to memory, port, NextREG, copper, beam, magic and chip register breakpoints. Keyboard and event
  breakpoints, and memory-space pages (`vram1`), come later.
- Not yet supported: `SHADOW`, `DT` (T-states since the last stop), the beam position off the Next, another CPU's operands
  (`gs.A` on a main CPU breakpoint), and a check on every memory access of an instruction.
- Labels are not followed when they change.
- Script callbacks on a hit (Lua and Python code run on the breakpoint or a tap): [script-callbacks.md](script-callbacks.md).
- The Qt breakpoint editor does not have a condition field yet.
