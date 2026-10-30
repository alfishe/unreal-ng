# P3 gate: the L2 draft of Manic Miner

The L2 export of one recorded session, put into a fresh zx-reaper project with no hand fixes, then scored with
[score_draft.py](../../../../tools/verification/analysis/score_draft.py) against skoolkid's `mm.skool` (the same
scale as [score-p0.md](score-p0.md): program range $8000-$FFFF, `g` counted as data). No agent touched the draft.
Measured 2026-10-26 on an M1 Ultra, worktree `analysis-layers-proposal`, zx-reaper `unreal-ng-fixes` at f98c784.

## Summary

| Measure | P0 `sna2ctl -m` | Hand runs A / B | **P3 draft** | Target (plan.md / P3 brief) |
| --- | ---: | ---: | ---: | --- |
| Byte categories (by entry) | 79.8 % | 92.4 % / 92.3 % | **99.98 %** (32761 / 32768) | ≥ 95 % - met |
| Byte categories (by line / sub-block) | 81.8 % | 92.4 % / 92.3 % | 97.98 % | - |
| Code entries | 28 / 33 | 33 / 33 | **31 / 33** (71 in the draft) | ≥ 31 of 33 (brief) - met; plan.md's ≥ 32 missed by one |
| Graphics regions covered by data | 44 / 106 | 106 / 106 | 106 / 106 (2 start at a boundary) | geometry is P4 |
| Record field lists | n/a | n/a | **5 / 5** complete | all 5 - met |
| zx-reaper `make` (tape identical) | - | yes | **yes** | yes - met |
| `make test` (4 scripts, per-frame hashes) | - | yes | **yes** (27536 frames) | yes - met |
| shift test +$100 / +$101 | - | yes (after hand fixes) | **yes / yes** | yes - met |

Records, expected fields matched: cavern buffer 23 / 23, guardians 13 / 13, items 4 / 4, conveyor 3 / 3, Willy's
state 6 / 6.

## The session

`mm_session.py` drives unreal-qt offscreen through MCP: a fresh 48K with a Kempston card, the mixer muted, run B's
`re/orig.z80`, `analysis start record=true`, the four zx-reaper input scripts of run B one after another
(demo-kempston 2700 frames, cheat-tour 6815, play 9021, demo 9000: 27536 frames), `analysis build` (replay plus L1 plus
L2: 30 s; 42 routines, 373 blocks, 65 loops, 130.6 M instructions; L2 239 regions, 117 conflicts), then `analysis
export` of every format for `5900-59FF,8000-FFFF`. The draft: 103 entries, 6982 lines; layout `REGION_START $8000`,
`SHIFT_END $9D00` (the fixed tail after the leftover source at $934C-$9CFF, which the moved code grows into).

The shift tests start the moved build at $8500 (+$100) and $8600 (+$101: the `ALIGN 256` before the page-aligned
cavern buffer moves the code by $200); the entry comes from the shifted build's symbols.

## Commands

From the worktree root, with SkoolKit 10.1 in `scratch/skoolkit-venv` and sjasmplus in `scratch/reaper-mm/bin`
(run B's zx-reaper project in `scratch/reaper-mm/run-B`):

```bash
tools/build/build.sh
R=scratch/reaper-mm/run-B
python3 tools/verification/analysis/mm_session.py --snapshot $R/re/orig.z80 \
    --inputs $R/tests/inputs/demo-kempston.txt $R/tests/inputs/cheat-tour.txt $R/tests/inputs/play.txt \
             $R/tests/inputs/demo.txt \
    --out scratch/p3/mm --ranges 5900-59FF,8000-FFFF
python3 tools/verification/analysis/reaper_gate.py --reaper /path/to/zx-reaper --tape $R/original/game.tap \
    --tests $R/tests --draft scratch/p3/mm --work scratch/p3/gate \
    --reference /path/to/skoolkid-manicminer/sources/mm.skool \
    --expected tools/verification/analysis/expected/manic-miner-records.json \
    --venv scratch/skoolkit-venv --bin scratch/reaper-mm/bin
```

`reaper_gate.py` writes `gate.json` (each step's status and output tail), `gate.log` (all output), `score.txt` and
`score.json` into `--work`; exit 0 when every step passed.

## What is left

- **Code entries $8684 and $8944.** Both are jump targets inside running code: $8684 (the reference's STARTGAME) is
  reached by forward conditional jumps and by falling through from the title loop; $8944 follows `JP $8691` and is
  reached only by the `JP Z` three bytes before it. Nothing the replay sees makes them routine starts; the reference
  splits there by reading the game. The entry-after-jump rule takes targets that a tail jump or a backward branch
  reaches, which keeps it from cutting every skip over a `JP`.
- **Draft-only entries (40).** Targets of calls and tail jumps the reference keeps inside one routine (for example
  $8691, a target of many jumps inside the reference's routine at $8684). They cost no bytes and no build: the score counts only
  misses.
- **By-line categories 97.98 %.** 658 reference text bytes are data lines in the draft; 640 of them are the cavern
  names in the 20 cavern records at $B000-$FFFF, which the code prints only from the live copy at $8000 (the
  records are one data block each in the draft, with the buffer's fields listed as instances).

## Zx-reaper fixes the gate needed

Branch `unreal-ng-fixes` of the zx-reaper clone (selftest passed after each):

- 13ea59d `zxq asm`: only ORGs in `REGION_START..SHIFT_END-1` move with the shift (a tape block below the region, MM's
  $5900 attributes, stays); ORGs in `#` hex are read (a skool's `@isub=+ORG #8000`). `zxq shiftpc`: where a PC of the
  plain build is in the shifted one, through the label at or below it.
- 17ff9a7 `zxinit` build.asm: the shifted build saves `REGION_START..REGION_END` (from `re/layout.mk`, passed by the
  shift test), cut at $FFFF when the shift moves it past.
- f98c784 shift test: `REGION_START` / `REGION_END` to the assembler and `zxq asm`, the shifted build's symbols in
  `build/shifted.sym`, the entry looked up there (`ENTRY=` overrides).
