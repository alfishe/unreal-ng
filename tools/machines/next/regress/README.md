# regress - ZX Spectrum Next software regression check

Does Next software still start and look the same after an emulation change? `regress.py` runs a fixed set of jobs on one
`unreal-qt` binary and keeps the pictures; `compare.py` lines up two runs title by title and draws A over B for every job that
moved. Use it before landing anything that touches the Next machine (CPU, memory, video, copper, DMA, interrupts, NextREGs, the
card, NextZXOS loading): the unit tests check single mechanisms, this checks that real software still runs.

## Requirements

- Python 3 with Pillow (`pip3 install pillow`).
- A Release build of `unreal-qt` and `core-tests` (`tools/build/build.sh`). Pass `--binary` from the complete `bin/` directory
  (or a full copy of it, `cp -Rc cmake-build-agent-release/bin <dir>`): `core-tests` beside it finds its ROMs and test data in
  `bin/rom`, `bin/testdata`. A copy of the app bundle alone runs the titles but every realboard program then fails; `regress.py`
  refuses such a layout when `--next-tests` is given.
- The Next software collection: a folder with `index.tsv` (columns `title`, `category`, `genre_or_kind`, `main_file`) and the titles
  under it (`--collection`). The owner's copy lives in the ZX Next collection folder (`zx-next/collection`).
- Optional: a clone of [ZXSpectrumNextTests](https://github.com/MrKWatkins/ZXSpectrumNextTests) for the real-board programs
  (`--next-tests`).
- The NextZXOS card: `testdata/machines/zxnext/card` (default `--card`), cloned per job.

Each instance takes six ports from `--port-base + 10 * i`; pick a base no other run on the machine uses.

## Workflow

Frames are counted, never timed, so the same binary and inputs give the same pictures. A change is judged against a baseline of
the code it starts from:

```bash
# 0. two binaries: the base commit and the change, each a full bin/ copy (both in your worktree's scratch/)
A=scratch/next-regress/bin-base;  B=scratch/next-regress/bin-change
RUN="tools/machines/next/regress/regress.py --collection <zx-next/collection> --next-tests <ZXSpectrumNextTests> --jobs 8"

# 1. baseline twice: which jobs are not deterministic (expected: none)
$RUN --binary $A/unreal-qt.app/Contents/MacOS/unreal-qt --out scratch/next-regress/base-1 --port-base 19500
$RUN --binary $A/unreal-qt.app/Contents/MacOS/unreal-qt --out scratch/next-regress/base-2 --port-base 19500
tools/machines/next/regress/compare.py scratch/next-regress/base-1 scratch/next-regress/base-2 --baseline \
    --out scratch/next-regress/b1-vs-b2           # writes nondet.json

# 2. the change
$RUN --binary $B/unreal-qt.app/Contents/MacOS/unreal-qt --out scratch/next-regress/change --port-base 19500
tools/machines/next/regress/compare.py scratch/next-regress/base-1 scratch/next-regress/change \
    --nondet scratch/next-regress/b1-vs-b2/nondet.json --out scratch/next-regress/base-vs-change
```

Then read `base-vs-change/report.md`:

- `newly-failing` (started on A, not on B), `crash`, `hang`: a regression until proven otherwise.
- `changed` (still starts, a picture differs): open `sheets/<job>.png` (A row over B row, `*` on the shots that differ) and explain
  every one. An intended fix shows up here too: name the fix and the reason the new picture is right (the VHDL line, the board
  photograph, the program's own description).
- `realboard differences`: the ZXSpectrumNextTests verdicts (`PASS` / `FAIL` for the self-checking programs) and picture hashes.

To see which part of a combined change moves a title, build each part on its own and rerun only those jobs
(`--only title-019 title-175 --load-only`); `GET /api/v1/emulator/<id>/state/next/regs` at the shot frames shows the layer order,
blend mode, copper mode and so on that the title uses.

A full run (boot + 216 titles, both paths, plus realboard) takes about 8 min with 8 instances on an M3 Ultra.

## What runs

Every job gets a fresh NEXT machine (`ram_power_on` zero) in a headless `unreal-qt` (`--headless`, its own WebAPI / CLI / MCP / GDB /
DeZog / ZRCP ports from `--port-base + 10 * i`, Stream Deck supervisor, view settings and recent files off, master muted) and its own
copy of the card (`--card`; APFS clone, deleted afterwards), so NextZXOS writes never reach the next job. The machine is paused and
only ever advanced by `run_frames`; keys are the frame-driven keyboard queue.

| Job | What |
|:--|:--|
| `os-boot` | power on: frames 50 / 150 / 250 / 350 (boot ROM, firmware, NextZXOS start), the welcome page (first idle at `#0C8F`), SPACE (menu), B (Browser), into `nextzxos/` and back up (EDIT) |
| `title-NNN` | the collection title in row NNN of `index.tsv`, by its primary path. `.nex` / `.snx`: `POST /snapshot/load` (the launcher boots NextZXOS first and loads into it). `.bas` / `.tap` / `.sna` (`--browser-ext`): boot, SPACE, B, the title's folder (copied to `/zt` on the card) and the main file by the Browser search (H + the shortest unique prefix) or by counting down; `.tap` with SYMBOL SHIFT + ENTER, N (Next mode); `.sna` with a second ENTER (the Snapshot Loader's options page); a `.bas` that only loaded (the editor listing) gets `run` + ENTER. Pictures at frames 250, 750, 1500 after the start; SPACE and ENTER are then tapped and a 4th picture is taken at 1800 (`keys`) |
| `title-NNN-br` | a `.nex` / `.snx` title once more through the Browser (`--load-only` skips it): NextZXOS's own loader, the current directory set to the title's folder. A `.sna` is refused by `/snapshot/load` on a NEXT (it names no model and the launcher boots NextZXOS only for `.nex` / `.snx`), so it is Browser-only; an `.snx` loaded directly does not get the title's folder as its current directory (a demo that loads its data files from there shows nothing) |
| `realboard` | `tools/machines/next/realboard/run-all.py` (ZXSpectrumNextTests through `core-tests`, its automatic verdicts), when `--next-tests` is given; `core-tests` is looked for beside the binary's bundle, or `--core-tests` |

Per picture: PNG (`shots/<job>-<label>.png`), SHA-1 of the raw RGBA frame (`/capture/framebuffer`), PC, HALT, NR `#07`, whether
NextZXOS is idle (`HALT` at `#0C8F`) and the screen kind (flat / NextZXOS grey / picture).

Verdict per title (from the frame-1500 picture): `picture` (started: something else than NextZXOS), `stuck-os` (NextZXOS still idle;
for `.bas` / `.tap` the grey NextZXOS screen, since BASIC waits for keys in the same loop), `flat` (one colour at every shot), `no-load`
(the loader refused: HTTP 200 with an error status counts), `no-boot`, `crash` (unreal-qt died: the instance is restarted for the next
job), `hang` (no answer within 10 min), `error` (the script failed).

Output: `results.json`, `report.md`, `sheet-NN.png` (20 titles a page, the 4 shots), `shots/`, `logs/` (one log per unreal-qt start,
`realboard.log`), `realboard/` (run-all's pictures, sheets, report). If no realboard program produced a picture the report says so
at the top of its realboard section.

## regress.py options

| Option | Meaning |
|:--|:--|
| `--binary` | the `unreal-qt` executable (inside the app bundle) |
| `--out` | the run directory |
| `--jobs` | parallel `unreal-qt` instances (default 8) |
| `--port-base` | first port; instance i uses `port-base + 10 * i` .. `+ 5` (default 19500) |
| `--collection` | the software collection (required unless `--no-titles`) |
| `--card` | the NextZXOS card copied for every job (default `testdata/machines/zxnext/card`) |
| `--next-tests`, `--core-tests` | run realboard too; `core-tests` defaults to the one beside the binary's bundle |
| `--only` | job names (`title-019`, `os-boot`) or title substrings; several per flag, the flag may repeat |
| `--limit N` | the first N jobs only |
| `--no-titles` | boot and realboard only |
| `--load-only` | no second Browser run (`title-NNN-br`) for `.nex` / `.snx` |
| `--keep-cards` | keep each job's card copy |
| `--browser-ext` | main-file extensions run through the Browser (default `bas,tap,sna`) |

## Compare

`compare.py A B [--out DIR] [--nondet FILE_OR_RUN ...] [--baseline]` classifies every job: `same` (every shot's hash equal),
`changed`, `newly-failing` (A `picture`, B not), `newly-starting`, `verdict-changed`, `only-in-A` / `only-in-B` (a partial run with
`--only`), and writes `sheets/<job>.png` and `report.md`. realboard programs are compared by verdict and picture hash.

`--baseline`: A and B are two runs of the same binary; `nondet.json` lists the jobs whose pictures differ between them. Pass it as
`--nondet` to later compares - those jobs are listed apart instead of as changes.
