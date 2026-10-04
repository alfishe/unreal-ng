# Recorder: access traces of real programs

The experiments replay what a real program does on the CPU bus, not random addresses: code runs in loops,
data is read in tables, the stack and the screen are written in bursts, and a 128K program switches pages.
A matcher's speed depends on that mix (how often its filter says "maybe", how well it stays in the cache),
so every experiment runs on the same recorded traces.

## What it records

`recorder` is a 128K Spectrum on the vendored `unreal-z80` core (`core/src/3rdparty/unreal-z80`): ROM 0/1,
eight RAM pages, #7FFD paging (A15 = 0, A1 = 0), the frame interrupt every 70908 T. It starts from a `.sna`
snapshot (48K or 128K) and writes one event per:

| Event | When | What the emulator checks for it |
|:--|:--|:--|
| `exec` | each instruction start, at its PC | `BreakpointManager::HandlePCChange` |
| `fetch` | each opcode / operand byte | the debug read path (`HandleMemoryRead`), like a data read |
| `read`, `write` | each data access | `HandleMemoryRead`, `HandleMemoryWrite` |
| `in`, `out` | each port access | `HandlePortIn`, `HandlePortOut` |
| `remap` | a slot shows another page after an `OUT` to #7FFD | (the slot table `MapZ80AddressToPhysicalPage` reads) |

No key is ever pressed (port #FE reads #FF): a game stays in its menu or attract loop, a demo plays.

## The traces

Recorded with 2 000 000 instructions each:

```bash
B=scratch/poc-022/build; T=scratch/poc-022/traces; R=data/rom/128.rom
$B/recorder $R "testdata/loaders/sna/Dizzy Y 2.sna"          $T/game-dizzy.trace     2000000
$B/recorder $R testdata/loaders/sna/aleste1.sna              $T/game-aleste.trace    2000000
$B/recorder $R testdata/loaders/sna/action.sna               $T/demo-action.trace    2000000
$B/recorder $R testdata/loaders/sna/z80full.sna              $T/cputest-z80full.trace 2000000
```

| Trace | Program | Events | exec | fetch | read | write | in | out | remap |
|:--|:--|--:|--:|--:|--:|--:|--:|--:|--:|
| game-dizzy | Dizzy Y 2 (128K game, menu) | 8 206 640 | 2 000 000 | 4 425 702 | 895 016 | 870 628 | 5 136 | 9 516 | 642 |
| game-aleste | Aleste (128K game) | 7 228 910 | 2 000 000 | 3 262 452 | 1 001 414 | 948 483 | 5 423 | 11 138 | 0 |
| demo-action | Action (128K demo) | 5 988 193 | 2 000 000 | 2 873 478 | 698 906 | 365 565 | 0 | 49 883 | 361 |
| cputest-z80full | z80full (48K instruction tester) | 5 138 187 | 2 000 000 | 2 413 903 | 583 265 | 141 001 | 16 | 2 | 0 |

The Scroller demo was tried and dropped: without its sound card it sits in `HALT` (2M instructions, 111K
fetches), which tests nothing. The traces (6 bytes per event, 13-49 MB) stay out of the repository; the
commands above recreate them bit for bit.

Experiments 04 and 06 add a synthetic `remapheavy-dizzy`: the Dizzy trace with slot 3 switched between six
RAM pages every 64 events - far more often than any real 128K program, the worst case for work done on a
remap.
