# realboard - the ZXSpectrumNextTests programs on our NEXT machine

[ZXSpectrumNextTests](https://github.com/MrKWatkins/ZXSpectrumNextTests) is a set of self-contained 48K snapshots (`.snx` / `.sna`)
whose results were checked on real boards; the repository has the photographs. `run-all.py` runs every program from its sorted
`Tests/<area>/<test>/` tree through the bring-up test (`LoaderNexRun_Test`, the snapshot loader plus the post-NextZXOS state), judges
the ones that paint their own verdict, and lays the rest next to the board photograph.

```bash
tools/build/build.sh core-tests
UNREAL_NEXT_TESTS=/path/to/ZXSpectrumNextTests tools/machines/next/realboard/run-all.py --out scratch/realboard
```

| Output | What |
|:--|:--|
| `<out>/<name>.png` | our picture of each program after its frames |
| `<out>/sheet-<area>.png` | ours \| the board photograph, one row a program |
| `<out>/report.md` | the table: area, test, program, verdict (`PASS` / `FAIL` / `look`), detail |
| exit status | non-zero when a program with an automatic verdict fails |

Automatic verdicts (the programs that colour their own result): `!NextReg` (no red cell), `NReg0x69` (green border), `!Z80N` (no
`ERR` row), `z80bltst` (no red flag byte). Everything else is `look` - the photograph is the reference, not another emulator (the
rule of [TODO.md](../../../../docs/inprogress/2026-10-07-zx-next/TODO.md), "What counts as a reference"). The same four verdicts run as
unit tests in `core/tests/emulator/machines/next/nextrealboard_test.cpp`.

To add a verdict: write the rule in `rule_result` and name it in `PROGRAMS` (frames, keys `"key@frame:count"`, rule).
