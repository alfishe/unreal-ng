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

Automatic verdicts (the programs that colour their own result): `!NextReg` (no red cell but #0A: core 3.02.03 reads NR #0A bit 2
as 0, zxnext.vhd 5912, where the program, written on 3.1.5, expects it back; `NEXTREG_EXPECTED_RED`), `NReg0x69` (green border), `!Z80N` (no
`ERR` row), `z80bltst` (red flag bytes expected: the T80 sets no PC-based F5 / F3 on an interrupted block repeat, as on the board; the rest green). Everything else is `look` - the photograph is the reference, not another emulator (the
rule of [TODO.md](../../../../docs/inprogress/2026-10-07-zx-next/TODO.md), "What counts as a reference"). The same four verdicts run as
unit tests in `core/tests/emulator/machines/next/nextrealboard_test.cpp`.

The rules sample the frame relative to the paper: the 720 x 576 HDMI frame has it at (96, 96) (`PAPER_ORIGIN`, the 48K / 128K
family of `NextPictureWindow::Origin`); a frame of another size fails with "unknown frame size".

To add a verdict: write the rule in `rule_result` and name it in `PROGRAMS` (frames, keys `"key@frame:count"`, rule).
