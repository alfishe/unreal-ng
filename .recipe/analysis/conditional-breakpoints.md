# Conditional breakpoints: stop only when an expression holds

A breakpoint that stops every time it matches is useless on hot code: the interrupt handler, a write in a loop, the
border port. A **condition** makes it stop only when the state is the one you want. Works on every machine (main
Z80), on memory (execution / read / write) and port breakpoints. The language reference is
[docs/features/conditional-breakpoints.md](../../docs/features/conditional-breakpoints.md).

| You want to stop when... | Breakpoint + condition |
|:--|:--|
| the code at an address runs with a register value | `{"type":"exec","address":"0x8000","condition":"A == 7"}` |
| a byte gets one value | `{"type":"write","address":"0x5C78","condition":"VAL == 0"}` |
| any byte of a range is written with a value | `{"type":"write","address":"0x4000","address_end":"0x57FF","condition":"VAL != 0"}` |
| one address of a range is written, from one routine | `{"type":"write","address":"0x4000","address_end":"0x5AFF","condition":"WR == 5800 && PC >= 9000"}` |
| the border turns red | `{"type":"port_out","address":"0xFE","port_mask":"0xFF","condition":"(VAL & 7) == 2"}` |
| a routine runs while some memory holds a value | `{"type":"exec","address":"0x0038","condition":"M(5C3B) & 20"}` |
| the N-th time the condition holds | the condition + `"hits":"N"` (counts only the hits where it held) |

## Steps (WebAPI)

```bash
BASE=http://localhost:8090/api/v1
EMU=$(curl -s -X POST $BASE/emulator/start -H 'Content-Type: application/json' -d '{"model":"48K"}' | jq -r .id)
curl -s -X PUT $BASE/emulator/$EMU/audio/mixer/master -H 'Content-Type: application/json' -d '{"muted":true}'
curl -s -X PUT $BASE/emulator/$EMU/feature/breakpoints -H 'Content-Type: application/json' -d '{"enabled":true}'

# FRAMES (#5C78) is written on every interrupt; its low byte is 0 once in 256 frames
curl -s -X POST $BASE/emulator/$EMU/breakpoints -H 'Content-Type: application/json' \
     -d '{"type":"write","address":"0x5C78","condition":"VAL == 0"}' | jq '{id, condition, match_count, hit_count}'
#   {"id":1,"condition":"VAL == 0","match_count":0,"hit_count":0}

# ... the machine runs into it within ~5 s and pauses (the wait loop: waiting-and-watching.md)
curl -s $BASE/emulator/$EMU/breakpoints | jq '.breakpoints[0] | {match_count, hit_count}'
#   {"match_count":238,"hit_count":1}   (every write matched, one held; up to 256, from where FRAMES was)

# Change it, or clear it ("" = no condition)
curl -s -X PUT $BASE/emulator/$EMU/breakpoints/1/condition -H 'Content-Type: application/json' -d '{"condition":"VAL == 80"}' | jq -r .condition
#   VAL == #80
curl -s -X PUT $BASE/emulator/$EMU/breakpoints/1/condition -H 'Content-Type: application/json' -d '{"condition":""}'
```

A bad condition is refused with **400** and the reason, before anything is added:

```bash
curl -s -X POST $BASE/emulator/$EMU/breakpoints -H 'Content-Type: application/json' \
     -d '{"type":"exec","address":"0x8000","condition":"RD == 9000"}' | jq -r .message
#   condition: RD is not supplied by this breakpoint's trigger at column 1
```

## CLI, MCP, Lua, Python

| Surface | Example |
|:--|:--|
| CLI | `wp 0x5C78 w if VAL == 0`, `bp 0x8000 --hits 3 if A == 7`, `bport 0xFE o --mask 0xFF if (VAL & 7) == 2`; `bpcond 1 VAL == 80`; `bpcond 1` clears. The CLI reads a plain address as decimal; the condition reads plain numbers as hex |
| MCP | `control_execution` `{"action":"bp_add","type":"write","address":"0x5C78","condition":"VAL == 0"}` answers `Breakpoint added on <id>: #1 if VAL == 0`; `bp_condition` with `bp_id`, `condition` |
| Lua | `local id, why = bp_write(0x5C78, {condition = "VAL == 0"})`; `bp_condition(id, "VAL == 80")` returns `true` or `false, reason` |
| Python | `id = emu.bp_write(0x5C78, condition="VAL == 0")` (`ValueError` with the reason for a bad condition); `emu.bp_condition(id, "VAL == 80")` |

`bplist` / MCP `bp_list` show the condition as it was understood, with brackets where precedence decides and `#`
on numbers from 10 up: `hits 1 matched 238 if VAL == 0`, `to 0x5AFF if (WR == #5800) && (PC >= #9000)`.

## Pitfalls

- **Numbers are hex.** `VAL == 10` is 16. Write decimal as `10d`. `0D` is decimal 0 (the `d` suffix), so 13 is
  `#0D` or `0Dh`. A hex number that starts with a letter needs `#` or `0`: `C000` is read as a name.
- **`&` is looser than `==`** (Unreal order): `VAL & 7 == 2` is `VAL & (7 == 2)` = 0, never true. Write
  `(VAL & 7) == 2`. The shown form tells you how the text was read.
- **The access operands belong to their trigger.** `RD`/`WR`/`VAL` do not exist on an execution breakpoint, and an
  IN has no value (`VAL` on a port-in breakpoint is refused). For "the byte that was read", use a read breakpoint
  and `VAL`.
- **`run_frames` and the other direct runs skip breakpoints**: let the machine run (`resume`) and wait
  ([waiting-and-watching.md](waiting-and-watching.md)).
- **A condition that fails at run time never stops**: the breakpoint is marked (`condition_error`, `bplist`
  `[condition failed: …]`) and skipped until it is enabled again or given a new condition.
- **Labels are read when the condition is set**: set it again after loading other symbols.
- **A condition does not make a breakpoint shared**: two `bp 8000` requests give one breakpoint, but a conditional
  one is always a breakpoint of its own.
- **DeZog and gdb conditions are filtered in the core too.** A DeZog breakpoint condition (DZRP or ZRCP) and a
  gdb `break … if` reach the core as a condition of their own breakpoint, so the machine does not stop where it is
  false; the client still checks it on each stop. A text the core cannot read the same way (a label, `/` in
  DeZog, a loop in a gdb expression) leaves the core breakpoint plain: it stops every time and the client decides
  ([debugger protocols](../../docs/features/conditional-breakpoints.md#debugger-protocols)). The server log names
  the reason.
