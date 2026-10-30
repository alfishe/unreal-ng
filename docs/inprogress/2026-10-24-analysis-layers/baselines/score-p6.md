# P6 hold-out: Jet Set Willy (2026-10-26)

The first game the layers were not tuned on. Tape: WoS `JetSetWilly(original).tap` (`JETSET.TAP`); session: zx-reaper's
`tests/inputs/play.txt` (the colour code, then 40,000 frames of seeded random play) on `re/orig.z80`; reference:
skoolkid `jsw.skool`; expected records and graphics: `expected/jet-set-willy-records.json` (9 record types, 224
graphics regions, written once from the reference). Baseline: the zx-reaper hand run (`examples/jet-set-willy`,
2.90 M subagent tokens, 2.51 M without the article), its skool scored the same way. Review: a fresh sonnet agent on
the recipe, 126 queue items, 2 experiments, 184,068 tokens, 51 tool calls, 11.5 min.

| Measure | L2 draft | Reviewed | Hand run |
| --- | ---: | ---: | ---: |
| gate: make, make test | pass | pass | pass |
| gate: shift +$100 / +$101 | - | **fail** (write past $FFFF) | pass ($8000-$9EFF only) |
| byte categories by line | 82.49 % | 85.94 % | 91.71 % |
| ... by entry | 89.19 % | 92.64 % | 92.4 % |
| ... code vs non-code | 96.34 % | 99.13 % | 99.11 % |
| code entries | 34 / 40 | 37 / 40 | 40 / 40 |
| graphics covered / right geometry | 224 / 2 | 224 / 0 | 224 / n/a |
| records | 2 / 9 | 4 / 9 | n/a |
| names: exact + normalized | 0 / 314 | 37 / 314 | 95 / 314 |
| names: generated / missing | 112 / 202 | 22 / 200 | 0 / 127 |
| subagent tokens | - | 184,068 | 2,900,000 |

Verdict: **fail** (the shift test, the categories, the code entries and the names are below the hand run). Not a
reviewer's failure: the draft is short of what the review can fix. The causes, all generic:

1. **A pass over the whole image.** The start-up loop at $8400-$8415 reads and rewrites every byte (the fake
   decryption): every byte's facts say "used by $8400", 27 `bug` findings say a table is read past its ends, 15
   `init_table` claims sit on code, and the area at $A000 breaks into 1-byte entries at each byte the loop alone used.
2. **Record arrays stay one entry.** The 61 rooms at $C000-$FFFF are one entry ("256 bytes each"), so the room names
   (1,952 bytes of text by line), the 61 room entries and their 122 points have no draft label or boundary: most of
   the 200 missing names and of the by-line text gap.
3. **The relocation region is the whole range.** The layout says $8000-$FFFF; a game that fills memory overflows
   at +$100. The hand run shifts $8000-$9EFF and keeps the page-arithmetic data fixed.
4. **Geometry.** The draft states geometry for 4 of 224 regions: the 2-bit room maps, the 8x8 tiles in 9-byte steps,
   the item graphics and the guardian pages are not recognised as such.
5. **Reference unused bytes as data** (2,358): areas the game never touches are exported as data blocks.

The recipe notes from the agent (conflict items, experiments without `input`) are in its report; the hold-out is
spent: fixes for these causes are tuning on JSW, so a fresh hold-out game is needed after them.
