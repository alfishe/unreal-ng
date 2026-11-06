# Requirements audit 2026-10-26: conditions and hit callbacks

This audit collects every requirement on the condition engine and on script callbacks found in `docs/inprogress/`,
`docs/guidelines/`, `.recipe/` and the code. The coverage verdict per block is in
[conditions-and-callbacks.md §2](../../../docs/inprogress/2026-10-26-breakpoint-taps/conditions-and-callbacks.md).

Abbreviations:

| Short | Document |
|:--|:--|
| CB | `docs/inprogress/2026-08-17-conditional-breakpoints/` |
| TAPS | `2026-10-26-breakpoint-taps/README.md` |
| RULES | `2026-09-28-debugger-model/rules.md` |
| R02 | `2026-09-21-roadmap/02-capability-registry-and-triggers.md` |
| GS | `2026-09-27-gs-debugger/` |
| EE | `2026-08-26-expression-evaluator/design.md` |
| BE | `2026-08-26-breakpoint-enhancements/design.md` |
| META | `2026-09-27-metadata-manager/trigger-integration.md` |

## Requirements

| id | Requirement | Source |
|:--|:--|:--|
| L1 | Boolean expression per breakpoint, compiled once to RPN, evaluated after the trigger matched | CB design §3 F1, §4.2 |
| L2 | All registers incl. primed, I, R, IM; flag mnemonics; `F & mask` | CB design §4.2 |
| L3 | Memory `(e)` byte and `(e).w`, side-effect free | CB design §4.2 |
| L4 | Access bus `MRA MWA MRV MWV PRA PWA PRV PWV`, aliases `ADDR VAL PORT` | CB design §4.2 |
| L5 | `PG0-3` from the memory slot table, `ROMPG DOS SHADOW` | CB design §4.1-4.2, §7 |
| L6 | `T FRAME DT IFF1 IFF2 HALTED` | CB design §4.2 |
| L7 | Labels, recompiled on label change | CB design §4.2, §5.2 |
| L8-L10 | Literals (decimal default), precedence (bitwise tighter), strict `&&`/`\|\|`, int64, a stack of 32 | CB design §4.2; implementation-hotpath.md §3 |
| L11 | Fast-predicate tier with the VM as reference; a differential test | CB performance.md S4 |
| L12 | Every documented Unreal / SpecEmu / Spectaculator / ZX-M8XXX example parses | CB implementation-plan.md §6.2 |
| L13 | WZ, beam (`RAY`), `HITS`, read/write flag, device fields | debugger-family use-cases §4.4, roadmap §4; EE |
| L14 | Cross-CPU `main.X gs.X neogs.X`; card registers; `W(x)`, `FD`; ZXSpin aliases; case-insensitive | RULES §5.3 |
| L15 | Conditions and hit counts on GS card breakpoints, same syntax | GS requirements B2, B4, X1 |
| L16 | `memhash` / `physhash` (deferred); frame-sampled conditions with edges and `X@prev` | META TR-7, TR-8, TR-13 |
| L17 | Registry-named observables; an unavailable facet is refused at apply | R02 §3 |
| L18 | Struct and field access; `@log "fmt"` with expression placeholders | toolchain design §7.2, §9.2 |
| L19 | Edge trigger, skip-first-N, fire count | EE; BE §2 |
| L20 | One language for breakpoints, watches, trace filters, logpoints and triggers | use-cases §4.4; protocol §3.8 |
| T1 | Exec, memory, port triggers; ranges; slots; physical pages (built) | CB design F2-F4, F7 |
| T2 | Device and event triggers reuse descriptor + condition + policy | CB design F12 |
| T3 | Address-less conditions (registerpoints) per instruction | BE §5; comparative analysis §3 |
| T4 | Conditions on taps, no extra work on the tap path | TAPS §2, §10 |
| T5 | A re-check on each memory op of an instruction; write-triggered value conditions | comparative analysis §4, §12 |
| T6 | Play-mode arming without debug mode | META TR-9 |
| A1 | Actions stop / log / mark / count; forbid range; `before`; masters filter | RULES §5.4a; protocol §6.7 |
| A2 | Snapshot, script, poke, register edit, input, event, bookmark; ordered `actions[]`; once / Nth / window / rate | R02 §3.1, §4; BE §2 |
| A3 | Count condition-true hits only; the false path store-free | CB performance.md S9 |
| A4 | DeZog logpoints, assertions, `WPMEM` | dezog workflows §5; GS X14 |
| A5 | Lua / Python `on_breakpoint(callback)`, "needs a callback protection strategy" | automation-gaps action-plan 0.3.5-0.3.6 |
| A6 | Callback on the emulation thread, 1 ms budget, over budget → logged and disabled; read-value hooks | GS design §8A.7 |
| A7 | Inline / deferred (safe point, per-frame budget, overrun → ERROR) / async tiers; sync Lua by flag | R02 §6; toolchain §9.3 |
| A8 | `on_game_event` callbacks (not built; the poll covers it) | TAPS §7 |
| E1 | Validate at entry with a position | CB design F6, §5.5 |
| E2 | Run-time error → ERROR, notify, do not pause; evaluator `noexcept` | CB design §5.2; performance.md §3 |
| E3 | Run-time error stops and sets ERROR, "never silently false" | RULES §5.3 |
| E4 | ZRCP: a parse failure stops | zrcp-server.md §5 |
| E5 | A broken condition is true | BE §2 |
| P1 | Compiled off the emulation thread, swapped atomically | CB design §5.2 |
| P2 | No Lua on the hot path; on-hit actions at human-scale rates | CB performance.md S6 |
| P3 | Tap sinks run inside the access and "must only note the hit" | breakpointmanager.h:183 |
| D1 | TTD replay mutes taps and breakpoints in one place | TAPS §2 |
| D2 | Debugging never changes what a CPU computes; action classes observe / annotate / mutate (journaled) / control / delegate | RULES R7; R02 §5 |
| D3 | Lua `logic` scripts: no host time or RNG, state in checkpoints; `view` scripts skippable on replay | R02 §8 |
| D4 | Same trigger live and retroactive over a recording | R02 §7; GS D4 |
| D5 | The language is prunable for the TTD reverse-search index | 2026-08-20-ttd-reverse-search-index §3 |
| S1-S2 | Persistence per machine JSON, `bpx.ini`, per firmware SHA, owner-scoped sets | CB F9; RULES §5.5; GS X15; R02 R1 |
| AP1-AP6 | Same names on every plane; `cpu` target; pushed `breakpoint_hit` / `trigger_fired` with sequence; MCP `condition`; run until expression | CB design §5.4, §6; R02 §11; GS A1; RULES §2 |
| DZ1-DZ2 | DZRP condition strings; the shipped ZRCP dialect | CB dezog-integration.md; zrcp-server.md |
| UI1-UI2 | Qt editor validation, columns, error icon, live value, Global Conditions panel | CB design §5.4; ui-mockups.md |

## Performance numbers stated

- R02 §12:
  - P-1: the miss path is unchanged.
  - P-2: fast ≤ 5 ns, RPN ≤ 30 ns.
  - P-3: an inline log ≤ 50 ns.
  - P-4: 10 000 armed non-matching triggers with no regression.
  - P-5: deferred ≤ 10 % of a frame.
- CB performance.md tiers:
  - gate ~1 ns;
  - prefilter 2-4 ns;
  - resolve 20-60 ns;
  - fast shape 2-5 ns;
  - VM 5-30 ns;
  - policy ~2 ns.
- Callbacks: 1 ms per call (GS).
- Taps: one each-write tap ≤ 232 µs per frame.

## Existing code

- **No hit or event callbacks on Lua or Python.** `lua-interface.md` documents `set_breakpoint_callback`, which does
  not exist.
- **`run_until_condition(predicate, max_tstates)`** (lua_emulator.h:2547, python_emulator.h:1537) runs on the script
  thread. The predicate is called per instruction with `(pc, af, bc, de, hl)`. Its budget is in T-states only; it has
  no time limit and no error handling.
- **C++ tap sinks:** `HapticProbes::OnTap` and `AnalyzerManager::OnTap`, synchronous on the emulation thread.
  `BreakpointTapHit` has no `pc`, `tState` or `before` yet.
- **Condition parser:** `core/automation/zesarux/src/zesaruxcondition.cpp` (ZRCP dialect, re-parsed on every stop).
  The TUI prototype `tools/poc/018-tui-debuggers/classic/src/backend/expr-lang.*` implements the Unreal language.
