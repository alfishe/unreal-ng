# fusetest on eight emulators (2026-10-02)

FUSE's own timing test, [fusetest](../../contention/fusetest/README.md), run through this harness with its wrapper
`fusetest-coemu` (it keeps a copy of everything fusetest prints in memory and sets `DONE` when the program
returns). fusetest knows what each machine should print: a test that does not apply to the machine it detects
prints `skipped`. So a run is right when every one of its twelve lines says `passed` or `skipped`.

```
PROGRAM=$PWD/tools/verification/contention/fusetest/fusetest-coemu tools/verification/coemu/run-all.sh
```

## The machines fusetest is written for

| Machine | FUSE | unreal-ng | Kozynax | MAME | spec_chum | SkoolKit | Xpeccy | xpeccy-plus |
|:--|:--|:--|:--|:--|:--|:--|:--|:--|
| 48K | ok | ok | ok | LDIR | LDIR | Floating bus | LDIR, Contended IN, Floating bus | ok |
| 128K | ok | ok | ok | LDIR, `0x3ffd` / `0x7ffd read` | LDIR, `0x3ffd` / `0x7ffd read` | Floating bus, `0x3ffd` / `0x7ffd read` | LDIR, Contended memory, `0xbffd read` | LDIR, Contended IN, Contended memory, `0xbffd read` |
| +2 | ok | ok | no +2 | as the 128K | as the 128K | no +2 | as the 128K | as the 128K |
| +2A | ok | ok | no +2A | `0xbffd read` | LDIR, Contended memory, `0xbffd read` | no +2A | `0xbffd read` | `0xbffd read` |
| +3 | ok | ok | `0xbffd read` | `0xbffd read` | LDIR, Contended memory, `0xbffd read` | no +3 | `0xbffd read` | `0xbffd read` |

A cell names the tests that print `failed`. What they mean (fusetest README, "What it tests"):

| Test | Failing here means |
|:--|:--|
| LDIR | an `LDIR` that copies across the start of contended memory takes the wrong time |
| Contended IN, Contended memory | an `IN` from a port in contended memory, or a fetch at the first contended address, is timed differently |
| Floating bus | the floating bus byte of a port whose high byte is in contended memory is taken at the wrong time (unreal-ng had this until 2026-09-30) |
| `0x3ffd read`, `0x7ffd read` | reading a port the 128K decodes as `#7FFD` does not store the data bus into the paging register |
| `0xbffd read` | port `#BFFD` on the +2A / +3 does not read the selected sound chip register |

unreal-ng passes on all five since the defects fusetest found were fixed
([2026-09-30-fusetest-core-defects](../../../../docs/inprogress/2026-09-30-fusetest-core-defects/TODO.md)).
Its first run here failed `0xbffd read` on the +2A / +3 because the harness's unreal-ng runner booted every
machine without its sound chip (the test runner leaves that slot empty); the runner now fits the AY on every
machine but the 48K.

## The clones

fusetest cannot test them, on any emulator:

- **Pentagon**: fusetest takes it for a TS2068 (a missing jump in its machine detection) and its timing tests
  then mean nothing. Loaded from TR-DOS, as the harness does on the clones, its paging tests also switch the ROM
  under BASIC and the screen fills with garbage: the run never ends (FUSE, unreal-ng, Xpeccy, xpeccy-plus).
  Started from 128 BASIC it finishes (`FuseTest_Test`, MAME, Kozynax, spec_chum: `skipped`).
- **Scorpion, Scorpion ProfROM**: fusetest cannot measure the frame length (`skipped`). Its interrupt
  synchronization needs every instruction to take its documented time; likely the Scorpion's Even M1 stretches
  them. MAME's ATM Turbo 2+ does the same.
- **ATM Turbo 2+ (unreal-ng, Kozynax)**: a 69888-tick frame without contention: fusetest takes it for a 48K
  and the four contention tests fail. **ZX-Evo, Profi**: the run hangs or crashes.
