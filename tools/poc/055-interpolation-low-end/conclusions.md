# 055 — conclusions

| Goal | Answer |
|------|--------|
| 1. Where one thread's time goes | A picture is 8.9 ms (a plain scroll) to 29.8 ms (the worst content) on a Deck thread. 12 ms of the worst and 4 ms of the simplest are window counts, block decisions and the pixels' pass, which no bit trick shortens; the field is 7 ms, the per-vector pixel tests 6 ms |
| 2. The reference's steps on bit planes | The field: the same blocks, no faster (the full search costs, not the compare). The pixel tests: tens of times faster |
| 3. What stays | The windows and the decisions: an exact picture cannot go under about 12 ms a thread on the worst content |
| 4. `c-lite` | `lite6`: 1.65 ms a refresh where the reference takes 57 ms (1 / 35), about 0.85 ms on a quiet Deck thread. Suite sum 91 against the reference's 44 and `off`'s 279. With the 2026-11-23 rules (`lite14`): sum 57; measured on a quiet Deck thread 0.45-0.90 ms a refresh by trace, 1 / 37 of the reference on the worst |
| 5. Does a picture fit a 60 Hz refresh on a low-end CPU | With `c-lite`, yes: about 2.5 ms on a core a third of the Deck's, inside the 5 ms budget. With the reference, no: 27-90 ms there |

## Criteria

| Outcome | Met? |
|---------|------|
| go, exact | No: no exact variant in 1.7 ms (the cheapest exact picture stays near 12 ms) |
| go, lite | Yes. The cost on a quiet Deck thread, derived on 2026-11-22 (0.85 ms), was measured on 2026-11-23: `lite6` 0.90-1.00 ms, `lite14` 0.89-0.90 ms on the worst trace, budget 1.7 ms; packing 0.16-0.37 ms a frame on top. Quality: sum 91 against the blend's 797, no scene near the blend's |
| no-go | — |

What the criterion did not ask and matters: `lite6` is worse than `off` on one scene of twelve (`diagonal`) and
about twice the reference's error overall, most of it in three kinds of content: colour changing in place, periodic
patterns, shapes changing as they move.

2026-11-23 ([measurements](measurements-2026-11-23-quality-rules.md)): three rules taken from the reference in mask
form (credible, slowest of equals, tied regions) bring the sum to 57, 1.3 times the reference's. `lite14` beats
`off` on all twelve scenes; colour changing in place and the dither patterns are at the reference's level. What is
left: dots a pitch apart moving by the pitch, shapes changing on the move, motions that stop and start.
