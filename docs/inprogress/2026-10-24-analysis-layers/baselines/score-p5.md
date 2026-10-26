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

## Run C: the review agent

One agent run by the recipe on the P5 draft (review only, no disassembly of its own), judged by `run_ab.py` with its
ledger:

| Measure | Run C | Run B |
| --- | ---: | ---: |
| subagent tokens | **60,627** (18 tool calls) | 2,034,851 (806) |
| gate (7 steps) | pass | pass |
| byte categories (by entry) | 99.98 % | 92.26 % |
| code entries | 31 / 33 | 33 / 33 |
| graphics with the right geometry | 106 / 106 | 0 / 106 |
| records found | 5 / 5 | n/a |
| names the same (exact + normalized, below) | 2 / 142 (1.41 %): 2 exact, 4 for a judge, 131 generated, 5 missing | 50 / 142 (35.21 %) |

Verdict: **fail**. The tokens are 3 % of run B's, but the queue gave the agent no way to reach the two missing entries
($8684, $8944) and no naming task: of the draft's 449 labels the agent made 7 meaningful, the rest stayed generated
(`var_8068`, `L8029`). P5b adds both to the review and measures the names.

## P5b: names, entry candidates, naming batches

**Names metric.** `score_draft.py` now scores labels: each reference entry and inner label of mm.skool in $8000-$FFFF
(142) against the draft's label at the same address: `exact` (same text), `normalized` (the same words after case,
`_`, digits and the synonyms of `name_vocabulary.json`), `judge` (a meaningful name that differs, left to a reader),
`auto` (a generated `var_` / `L` / `sub_` name), `missing` (no label). "The same" = exact + normalized; `run_ab.py`
prints the rows for the draft and for run B's final skool and writes `names-pairs.tsv`; `quality_ok` now also needs
names the same >= run B's.

**Entries.** One rule and one review reason in the code map:

- A conditional tail call is an entry: a target reached only by one forward conditional `JP`, right after an
  unconditional `JP` to a known entry, whose code never returns into the routine before it. On Manic Miner it adds
  **$8944** and nothing else (the ctl differs from P5 by one line, `c $8944`); the other 31 entries and the 303
  entries of P5 stay.
- The weaker shapes become `entry` review items (`rule` prologue or tail, confidence 400) instead of entries: a
  prologue of straight code jumped to from 2+ sites that runs into an entry, a closed tail reached by several jumps.
  Manic Miner has **5**: $8684 (prologue, a real entry in mm.skool), $88A8 and $88FF (tail), $8C7B and $8FDF
  (prologue), the last four are inner points in mm.skool (`*` lines). As an automatic rule the prologue shape would
  have added those 4 false entries, so $8684 stays a review decision: `verdict=ok` makes it an entry, `wrong` drops
  it.

**Naming batches.** After the five frozen reasons and `entry`, the queue offers `name` items: batches `N1..` of at most
10 subjects with an address name (`var_`, `L`, `sub_`, ...) and one class (code, data), each sheet at most 2 KB, at
most 40 batches, deterministic (address order). Manic Miner over the session's ranges: **22 batches, 198 subjects**, not
capped (24 / 198 without a range filter: 2 more batches in the screen); the first sheet 1515 bytes. The subjects hold
**118 of the 142** reference names (94 of 99 entries, 24 of 43 inner points); the 24 left: 12 loop points reached only by
`JR` / `DJNZ` (the subject rules skip those), 5 addresses with no label in the draft ($805F $80C5 $8260 $8854 $A800)
and 7 data entries the rules leave out (untouched / unused $841B $845E $934C, $8408 $841F, graphics $B2E0 $C2E0 inside
a copied block).

The queue of the fresh session (no agent): 135 open = bug 0, conflict 0, unnamed-hot 7, low-confidence 40, unknown
61, entry 5, name 22 (the first five as P5, so the first 108 items are unchanged); findings 624 (P5 619 + 5 entry).

| Measure | P5 draft | **P5b draft** (no review) | Run C | Run B |
| --- | ---: | ---: | ---: | ---: |
| byte categories (by entry) | 99.98 % | **99.98 %** | 99.98 % | 92.26 % |
| code entries | 31 / 33 | **32 / 33** | 31 / 33 | 33 / 33 |
| graphics with the right geometry | 106 / 106 | **106 / 106** | 106 / 106 | 0 / 106 |
| records found | 5 / 5 | **5 / 5** | 5 / 5 | n/a |
| names the same | 0 / 142 | **0 / 142** | 2 / 142 | 50 / 142 |
| ... exact / normalized | 0 / 0 | 0 / 0 | 2 / 0 | 7 / 43 |
| ... for a judge | 0 | 0 | 4 | 61 |
| ... generated / missing | 137 / 5 | 137 / 5 | 131 / 5 | 0 / 31 |
| gate | pass | **pass** | pass | pass |

The draft alone names nothing, as before: names come from the review. With the batches the next agent run (run D)
can close the entry gap with one decision ($8684) and has to name about 50 of the 118 reference addresses it is shown
to match run B.

## Commands

```bash
R=scratch/reaper-mm/run-B
python3 tools/verification/analysis/mm_session.py --snapshot $R/re/orig.z80 \
    --inputs $R/tests/inputs/{demo-kempston,cheat-tour,play,demo}.txt \
    --out scratch/p5/mm --ranges 5900-59FF,8000-FFFF --port-base 9600 --l3    # P5b: --out scratch/p5b/mm
python3 tools/verification/analysis/run_ab.py --draft scratch/p5/mm --work scratch/p5/ab \
    --ledger scratch/p5/empty-ledger.json --reaper <zx-reaper> --tape $R/original/game.tap --tests $R/tests \
    --reference <skoolkid-manicminer>/sources/mm.skool \
    --expected tools/verification/analysis/expected/manic-miner-records.json \
    --venv scratch/skoolkit-venv --bin scratch/reaper-mm/bin --run-b-skool scratch/reaper-mm/compare/B.skool
```

`run_ab.py` runs `reaper_gate.py` into `<work>/gate`; the gate alone is as in [score-p3.md](score-p3.md#commands).

## Run C2: the review agent on the P5b draft (2026-10-26)

A fresh sonnet agent, the recipe only, a built instance (`mm_session.py --l3 --keep`), the reference closed to it:
140 queue items decided (7 unnamed-hot, 42 low-confidence, 62 unknown, 5 entry, 24 naming batches), no experiment,
118,745 tokens, 32 tool calls, 6.5 min.

Its first export failed `make`: a label the review gave an address inside an entry (`GuardianSprites` at $8100)
renamed the point label after Relocate had written `@isub=LD D,sprite_8100/256`. The export now renames the @isub
references with the label (`ReviewLabelInsideAnEntryRenamesItsIsubReferences`). The same decisions with the fix
applied to the exported skool (the 4 @isub lines it changes; the instance was gone):

| Measure | Run C2 | Run B |
| --- | ---: | ---: |
| gate: make, make test, shift +$100 / +$101 | pass | pass |
| byte categories (by entry) | 98.03 % | 92.3 % |
| code entries | 32 / 33 | 33 / 33 |
| graphics / records | 106 / 106, 5 / 5 | 106 / 106, 5 / 5 |
| names: exact + normalized | 30 / 142 | 50 / 142 |
| names: + a blind judge (sonnet, both runs mixed, 112 pairs) | **73 / 142 (51.4 %)** | **109 / 142 (76.8 %)** |
| names left generated / missing | 56 / 5 | 0 / 31 |
| subagent tokens | **118,745** | 2,034,851 |

The categories fall from 99.98 % because the review made the 20 cavern names (640 bytes) text entries of their own;
the reference keeps them inside each cavern's data entry. The names gap is mostly the 56 subjects the agent left out
of its batches ("skip rather than guess" in the recipe): the recipe now asks for a name for every subject from its
facts, a skip only for empty facts, one MCP connection for `take_control`, and the harness's file names.

## Run C3: the review agent on the revised recipe (2026-10-26)

A fresh sonnet agent on the revised recipe: 137,491 tokens, 31 tool calls, 8.3 min. Its first export failed the shift
test (+$101) and found 3 of 5 records: the review retyped the tile table inside the cavern buffer ($8020-$8067,
`level_map`), and the export moved the buffer's `ALIGN 256` to the next plan entry and dropped the field lists of the
claims the range overlapped. The export now takes the alignment from the outermost page-aligned buffer and a kind
replaces only the claims inside it (`ReviewKindInsideABufferKeepsItsAlignAndFields`). The decisions replayed on a
fresh session of the fixed build (the finding ids are the same, 624 of 624):

| Measure | Run C3 | Run B |
| --- | ---: | ---: |
| gate: make, make test, shift +$100 / +$101 | pass | pass |
| byte categories (by entry) | 98.03 % | 92.3 % |
| code entries | 33 / 33 | 33 / 33 |
| graphics / records | 106 / 106, 4 / 5 | 106 / 106, 5 / 5 |
| names: exact + normalized | 50 / 142 | 50 / 142 |
| names: + a blind judge (sonnet, both runs mixed, 129 pairs) | **108 / 142 (76.1 %)** | **107 / 142 (75.4 %)** |
| names left generated / missing | 19 / 5 | 0 / 31 |
| subagent tokens | **137,491** | 2,034,851 |

The judge agreed with the C2 judge on 57 of run B's 61 pairs. The missing record is the conveyor ($806F, 4 bytes):
the review answered `ok` with `kind: variable` ("three adjacent game variables"), which confirmed the claim and
replaced it at once, so its fields went. A claim now refuses `ok` with another kind (`wrong` with the kind, or `ok`
without it). Other fixes from the agent's notes: the recipe gives the 32-character label limit, a naming batch's reply
lists the skipped addresses, and `take_control` by the session's holder (a new MCP connection of the same server, a
restarted script) keeps the session instead of being refused as held by another.

## Run C4: a fresh review after the C3 fixes (2026-10-26)

All C3 fixes in (loop labels and used plain data named, a kind alone decides, the export ALIGN fix), a fresh session
and a fresh sonnet agent on the recipe: 156 queue items (40 naming batches, 347 subjects named), no experiment,
153,140 tokens, 25 tool calls, 8.2 min.

| Measure | Run C4 | Run C3 | Run B |
| --- | ---: | ---: | ---: |
| gate: make, make test, shift +$100 / +$101 | pass | pass | pass |
| byte categories (by entry) | 98.03 % | 98.03 % | 92.3 % |
| code entries | 33 / 33 | 33 / 33 | 33 / 33 |
| graphics covered / right geometry | 106 / 104 | 106 / 106 | 106 / n/a |
| records | 4 / 5 | 4 / 5 | 5 / 5 |
| names: exact + normalized | 37 / 142 | 50 / 142 | 50 / 142 |
| names: + a blind judge (sonnet, mixed with B, 150 pairs) | **99 / 142 (69.7 %)** | 108 / 142 | **108 / 142 (76.1 %)** |
| names left generated / missing | 11 / 5 | 19 / 5 | 0 / 31 |
| subagent tokens | **153,140** | 137,491 | 2,034,851 |

The judge agreed with the first one on 58 of run B's 61 pairs. What C4 found:

- The 40-batch cap: with the loop labels the subjects grew to 357 and the batches past the cap were the high
  addresses, two routines (`sub_8D88`, `sub_8E58`) among them; 123 labels stayed generated. Over the cap the
  batches naming routine entries, then data entries, then points inside code are now kept first.
- The conveyor record again: the claim read "Variables used together", the agent answered `wrong` with
  `kind: variable`. The claim now says "One record: N fields the same routines use together" and the recipe says
  `ok` keeps the record and its fields.
- Recipe: a `findings` jsonl line, and that a resolve may be repeated (the last word counts).

## Run C5: the batch priority over the cap (2026-10-26)

The C4 fixes in (the naming-batch priority over the cap, the record claim's title, the recipe additions), a fresh
session and a fresh sonnet agent: 156 queue items resolved, one experiment, 154,780 tokens, 25 tool calls, 9.1 min.

| Measure | Run C5 | Run C4 | Run B |
| --- | ---: | ---: | ---: |
| gate: make, make test, shift +$100 / +$101 | pass | pass | pass |
| byte categories (by entry) | 98.03 % | 98.03 % | 92.3 % |
| code entries | 32 / 33 | 33 / 33 | 33 / 33 |
| graphics covered / right geometry | 106 / 106 | 106 / 104 | 106 / n/a |
| records | **5 / 5** | 4 / 5 | 5 / 5 |
| names: exact + normalized | 26 / 142 | 37 / 142 | 50 / 142 |
| names left generated / missing | 26 / 5 | 11 / 5 | 0 / 31 |
| subagent tokens | 154,780 | 153,140 | 2,034,851 |

The record claim's new title worked (the conveyor record kept, 5 / 5). The names got worse, from the priority
itself: the game's variables are fields inside the cavern buffer at $8000, points inside data, scored 0, so their
three batches ($8056-$80CB) fell past the cap while the screen and buffer positions at $4000-$7FFF, batches with an
entry each, stayed; 21 variables kept their generated names. Not judged (the cause is known). The lost code entry is
the reviewer's: `wrong` on the prologue candidate $8684 (C4 said `ok`).

Fixed after C5: an L2 variable inside data scores as a data entry, the cap is 48 batches (the Manic Miner draft has
43 with the loop labels in, so none is dropped), and a batch's head line gives its first and last subject (a batch
that starts inside an entry no longer seems to overlap the one before). Replaying C5's queue decisions on the fixed
build: 43 batches, the variables in N7-N9, `name_capped` 0. Recipe: give an experiment `finding` only when it tests
that claim (a failed check refutes it); do not rerun `cover` when it already ran.

## Run C6: variables over the cap (2026-10-26)

The C5 fixes in (variables score as data entries, the cap 48, the batch head line), a fresh session and a fresh
sonnet agent: 159 queue items resolved (43 naming batches), no experiment, 164,845 tokens, 36 tool calls, 12.1 min.

| Measure | Run C6 | Run C5 | Run C4 | Run B |
| --- | ---: | ---: | ---: | ---: |
| gate: make, make test, shift +$100 / +$101 | pass | pass | pass | pass |
| byte categories (by entry) | 98.03 % | 98.03 % | 98.03 % | 92.3 % |
| code entries | **33 / 33** | 32 / 33 | 33 / 33 | 33 / 33 |
| graphics covered / right geometry | 106 / 106 | 106 / 106 | 106 / 104 | 106 / n/a |
| records | **5 / 5** | 5 / 5 | 4 / 5 | 5 / 5 |
| names: exact + normalized | 37 / 142 | 26 / 142 | 37 / 142 | 50 / 142 |
| names: + a blind judge (sonnet, mixed with B, 156 pairs) | **120 / 142 (84.5 %)** | n/a | 99 / 142 | 108 / 142 (76.1 %) |
| names left generated / missing | 5 / 5 | 26 / 5 | 11 / 5 | 0 / 31 |
| subagent tokens | 164,845 | 154,780 | 153,140 | 2,034,851 |

The judge agreed with the first one on 58 of run B's 61 pairs (B stays 108). It was lenient on a few C6 pairs
(WILLYATTRS / DrawWilly, SBMSB / DrawTileRow, MOVEWILLY2 / CheckFatalFallAndReadKeys and about three more) and strict
on one ($DC00, the right cavern layout): read strictly, C6 is about 114 / 142, above B either way. Of the 5
generated names, 3 have no reference name ($841B, $845E untouched; the reference names $934C only). `run_ab`'s
verdict stays "fail" on its deterministic names alone (37 < 50); every other measure is at or above run B at 8 % of
its tokens.

Fixed after C6 (the agent's notes): a naming batch reports a subject labelled through its finding as kept, not
skipped; the review sheet shows 32 bytes and a `text` line when most of them print (the cavern names are 32
characters); recipe: an `ok` + `kind` + `label` example for `unknown`, one label per address (a pattern repeated
per level gets one name each), a skip carries a comment.
