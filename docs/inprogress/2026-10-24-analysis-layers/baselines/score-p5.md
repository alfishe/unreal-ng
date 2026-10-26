# P5: L3 review on Manic Miner

L3 ([analysis-layers.md](../../../emulator/analysis/analysis-layers.md#l3-review)) on the session of P3 / P4: the
findings register, the review queue, directed coverage and the candidate bugs, then the P4 gate again and the A/B
harness with no ledger. No agent touched the draft. Measured 2026-10-26 on an M1 Ultra, worktree
`analysis-layers-proposal`, zx-reaper `unreal-ng-fixes` at f98c784 (no new zx-reaper fixes).

## Summary

| Measure | P4 | **P5** | Target (P5 brief) |
| --- | ---: | ---: | --- |
| Findings in the register | - | **619** (372 claims, 160 conflicts, 7 routines, 80 unknown; 342 observed, 277 inferred) | - |
| Review queue (open) | - | **108**: 0 bugs, 0 conflicts, 7 unnamed hot routines, 40 low-confidence claims, 61 unknown areas | a cap plus a summary |
| First review sheet | - | 1024 bytes (the cap) | <= 1 KB |
| One-way branches (coverage candidates) | - | 51: 19 tried, 32 skipped | - |
| Coverage attempts / flips | - | **38 / 17** (1 not flipped after 5, 1 not reached) | - |
| Instructions the session never ran, reached | - | **77** | - |
| Coverage wall time (4 workers, peak 4) | - | 5.4 s | - |
| Candidate bugs | - | **0** (4 false positives before the tuning, below) | real or not, stated |
| Byte categories (by entry) | 99.98 % | **99.98 %** | keep P4 - met |
| Code entries | 31 / 33 | **31 / 33** (misses $8684 $8944, as P3 / P4) | keep P4 - met |
| Record field lists | 5 / 5 | **5 / 5** | keep P4 - met |
| Graphics with the right geometry | 106 / 106 | **106 / 106** | keep P4 - met |
| zx-reaper `make` / `make test` / shift +$100 / +$101 | pass | **pass** | keep P4 - met |

The draft keeps the 303 entries and the 71 code entries of P4; the coverage notes (17 "needs ..." lines at the
branches, "reached only in an experiment" at the first new instruction) are comments, so the build and the score do not
move.

## The session

`mm_session.py --l3` with the four run B scripts (27536 frames, the mixer muted), L1 build 30.9 s: 42 routines, 373
blocks, 65 loops (as P2-P4). With `--l3` the script writes `findings.md`, `findings.jsonl`, `review.txt`,
`review.json`, `review-next.txt`, `cover.txt` and `cover.json` before the exports, so the exported draft carries the
coverage notes. Two sessions (before and after the last text fix in the findings) gave the same numbers and a
byte-identical skool.

## Review queue

The rules of [the review queue](../../../emulator/analysis/analysis-layers.md#review-queue) stay as written; on Manic
Miner they select 108 of 619 findings:

| Reason | Items | What they are |
| --- | ---: | --- |
| `bug` | 0 | - |
| `conflict` | 0 | each of the 160 conflicts has a code map fallback on one side or confidences more than 300 apart |
| `unnamed-hot` | 7 | $8ABB, $8DAA, $8FF4, $92BA, $92CB, $92D5, $92DC (the main loop at $92DC has 6 entries and 18.9 % of the time) |
| `low-confidence` | 40 | records and variable groups below 700 |
| `unknown` | 61 | data areas over 16 bytes with only a fallback claim |

With the default cap of 40 an agent sees the 7 routines and the first 33 low-confidence claims, and one summary line of
the 68 others; it resolves those and asks again. The thresholds are frozen with these values; JSW and a 128K game are
the hold-outs (P6).

## Directed coverage

`cover` with the defaults (budget 64, 8 attempts a branch, 50 frames each), from the last frame each branch ran in:

| Tested | Branches | Flipped | Not flipped | Skipped |
| --- | ---: | ---: | ---: | ---: |
| a memory byte | 38 | 14 | 1 ($8FCC after 5), 1 not reached | 22 (flags from more than one producer) |
| port $xxFE (keys) | 1 | 1 (key ENTER, $8672) | - | - |
| port $xx1F (Kempston) | 2 | 2 (fire, $8C79 $9341) | - | - |
| a register / a computed value | 10 | - | - | 10 |

Seven flips reach code the session never ran (77 instructions): $87B3-$87C6 (poke $8458=$01), $8E04-$8E0D ($80DB=$01),
$8E58-$8E5D and $8FBF-$8FC4 ($8074=$00), $90B4-$90F0 and $90FE-$9104 ($845A=$00), $9149-$9154, $916C-$917C and
$919D-$91A1 ($7506=$11). The other ten flip a test without reaching an instruction the session had not run.

Two defects found on the way, fixed before these numbers:

- Every attempt started at frame 0: the start frame came from the branch byte's data-use frames, which opcodes do not
  have. L1's branch conditions now keep the first and the last frame they were decided in, and coverage starts from
  the last one. Before: 1 flip of 19, no new code.
- A start at a frame boundary may sit between the read and the branch, so the first arrival tested the flags from
  before the poke or the key and ended the attempt as "went the old way". The first arrival now decides only when it
  goes the new way (`AnalysisL3_Test.ExperimentWithAKeyReachesTheKeyHandler` checks a key flip from every recorded
  frame).

## Candidate bugs

The first run gave 4 `read-unwritten` hits: $BEDB-$BEDC, $BEDE-$BEDF, $CADB-$CADC and $CADE-$CADF, all read by $86AB.
They are zero bytes inside the cavern data ($B000-$FFFF), which the instruction at $86AB copies with the non-zero bytes
around them: values of static data, not an uninitialised area, so all 4 were false positives. The rule now skips a run
whose reader also reads a never-written non-zero byte within 256 bytes (`AnalysisL3_Test.ReadUnwrittenFlagsABufferNotAStaticTable`:
a buffer read and never written is still a candidate, a table of values with zeros is not). After it: 0 candidates.
The other five rules give nothing on Manic Miner; they look for memory and stack faults, not game logic.

## A/B harness

`run_ab.py` on the P5 draft, with no ledger and with a dummy one (150,000 tokens in 2 agents), run B's final skool
scored the same way:

| Measure | P5 draft | Run B |
| --- | ---: | ---: |
| byte categories (by entry) | 99.98 % | 92.26 % |
| code entries | 31 / 33 | 33 / 33 |
| graphics covered | 106 / 106 | 106 / 106 |
| graphics with the right geometry | 106 | 0 |
| records found | 5 / 5 | n/a |
| subagent tokens | no ledger (dummy: 150,000) | 2,034,851 |

Gate: pass (all 7 steps). Verdict: fail, as expected for the draft alone: 2 code entries short of run B, and no ledger.
The review-only agent run by the recipe ([analysis-layers.md](../../../../.recipe/analysis/analysis-layers.md)) is the
run that has to close both; `run_ab.py` judges it with its ledger.

## Commands

```bash
R=scratch/reaper-mm/run-B
python3 tools/verification/analysis/mm_session.py --snapshot $R/re/orig.z80 \
    --inputs $R/tests/inputs/{demo-kempston,cheat-tour,play,demo}.txt \
    --out scratch/p5/mm --ranges 5900-59FF,8000-FFFF --port-base 9600 --l3
python3 tools/verification/analysis/run_ab.py --draft scratch/p5/mm --work scratch/p5/ab \
    --ledger scratch/p5/empty-ledger.json --reaper <zx-reaper> --tape $R/original/game.tap --tests $R/tests \
    --reference <skoolkid-manicminer>/sources/mm.skool \
    --expected tools/verification/analysis/expected/manic-miner-records.json \
    --venv scratch/skoolkit-venv --bin scratch/reaper-mm/bin --run-b-skool scratch/reaper-mm/compare/B.skool
```

`run_ab.py` runs `reaper_gate.py` into `<work>/gate`; the gate alone is as in [score-p3.md](score-p3.md#commands).
