# Analysis scoring tools

Measuring tools for the [analysis layers](../../../docs/inprogress/2026-10-24-analysis-layers/README.md) work: they
score a disassembly draft with no agent in the loop, so each phase can prove its gain. Python 3, standard library
only.

## score_draft.py

Compares a draft (SkoolKit `.ctl` or `.skool`, hex or decimal addresses) with a reference skool (skoolkid's
`mm.skool`, `jsw.skool`) over the program range and reports:

| Measure | How |
| --- | --- |
| byte categories | code / data (`b`, `w`, `g`, `s`) / text (`t`) / unused (`u`) / unknown (`i`, no entry), per byte, once by entry type and once by line (skool `DEFB`/`DEFM` lines, ctl sub-blocks); agreement % and a confusion table |
| code entries | reference `c` entries the draft starts exactly; misses and draft-only starts |
| graphics regions | reference sections (entry or mid-block comment) with an image macro (`#UDGARRAY`, `#SPRITE`, `#SCR`, `#FONT`, the game's own `#CAVERN` / `#TILES` / ...): covered by draft data bytes, starting at a draft boundary, and with the right geometry when the draft states one (`--geometry`) |
| record field lists | the draft's fields (`--fields`) against a checked-in expected list (`--expected`); "n/a" when the draft gives none |

```bash
S=tools/verification/analysis/score_draft.py
python3 $S draft.ctl path/to/mm.skool --expected tools/verification/analysis/expected/manic-miner-records.json
python3 $S draft.skool path/to/mm.skool --expected ... --json scratch/score.json   # also write JSON
python3 $S draft.skool path/to/mm.skool --expected ... --json -                    # JSON only, to stdout
python3 $S draft.skool path/to/mm.skool --split-vars --isub --start 0x7FFD          # zxcompare.py convention
```

Options: `--start` / `--end` (default: the expected file's `range`), `-v` lists every graphics region,
`--split-vars` counts `g` as its own category, `--isub` reads `@isub+begin` blocks as SkoolKit's `@isub` mode does
(by default they are skipped, like `skool2html`).

Side files for the future export (P3 / P4), both optional:

- `--fields`: `{"records": [{"name", "base", "layout"?, "fields": [{"offset", "size", "name"?}]}]}`. A record matches
  an expected layout by base address (the live copy or any instance) or by name; a field matches on offset and size.
- `--geometry`: `{"regions": [{"start", "end", "kind", "width", "height", "frames"}]}`, compared with the expected
  file's `graphics` list by start address.

`expected/manic-miner-records.json`: the program range, the five Manic Miner record types (cavern buffer, guardians
horizontal / vertical, items, conveyor, Willy state) with their fields, bases and the 20 cavern-record copies,
written once from the reference comments; and the geometry of the 106 reference graphics regions.

Baselines: [score-p0.md](../../../docs/inprogress/2026-10-24-analysis-layers/baselines/score-p0.md).

## mm_session.py

Records an input-script session on a snapshot through MCP (unreal-qt offscreen on its own ports, a Kempston card, the
mixer muted), builds L1 + L2 and writes every `analysis export` format into `--out` (`draft.ctl`, `draft.skool`,
`draft.fields.json`, `draft.geometry.json`, `draft.layout.mk`, `draft.symbols`, `draft.map.txt`), plus `l1.json`,
`conflicts.txt` and `session.json`. The scripts are zx-reaper's `tests/inputs` format. Example in the docstring.

## reaper_gate.py

The P3 gate: a fresh zx-reaper project from the tape (`zxinit.py`), the input scripts and their expected hashes
copied in, zxinit's skool replaced by the draft, `re/layout.mk` taking the draft's `REGION_START` / `SHIFT_END`, then
`make` (tape identical), `make test`, `make shift-test` at +$100 and +$101 and `score_draft.py` with the draft's
fields. Nothing is edited by hand. `gate.json`, `gate.log`, `score.txt`, `score.json` go into `--work`; exit 0 when
every step passed. Result and commands: [score-p3.md](../../../docs/inprogress/2026-10-24-analysis-layers/baselines/score-p3.md).

## make_rom48_signatures.py

Prints the 48K ROM signature catalog entries (`core/src/debugger/analyzers/analysis/signatures/rom48.cpp`) from a 48K
ROM image: the first bytes of each listed routine, with the absolute operands of JP / CALL / LD (nn) masked out (a
relocated copy matches too). Needs SkoolKit (its disassembler finds the instruction boundaries).

## Tests

```bash
python3 tools/verification/analysis/test_score_draft.py
```

Plain `unittest` on the tiny fixtures in `testdata/` (a decimal reference skool, a hex ctl draft, a hex skool
draft, fields, geometry); under 0.1 s.
