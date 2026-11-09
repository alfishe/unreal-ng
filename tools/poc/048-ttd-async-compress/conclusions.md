# 048 — conclusions

Numbers: [measurements-2026-11-07-m3-ultra.md](measurements-2026-11-07-m3-ultra.md); why they come out so:
[analysis.md](analysis.md).

## Answers

1. **Upper bound.** zstd is 7.0 % of a recording frame on the frame fixture (52.6 µs of 755), 8.9 % with the write
   journal (71.9 µs of 807; the journal blocks are compressed too). On idle heavy configurations it is 0.2-1.2 %,
   ZX-Evo 3.9-4.4 %. Expected 5-10 % on the fixture and under 3 % idle: as expected, except ZX-Evo idle. Idle
   machines can gain at most that share, too little to see against their run-to-run spread.
2. **Net gain.** The worker keeps almost all of the bound: with the queue bounded to 96 pieces, −6.7 % on
   `BM_Frame_TTD_Gaming` (766 → 714.5 µs) and −5.4 % on `_Development` (806.5 → 763 µs), 6 rounds each. The handoff
   costs 1.3-1.8 µs per capture, applying the worker's answers 0.4-0.6 µs, waits are rare (0.007 per capture, under
   0.1 µs). Synchronization costs about 4 % of the bound, not more than half of it.
3. **Side effects.** The process's CPU time is the same (1.79 s against 1.79 s: the work moved, none was added). Peak
   memory of queued pieces is 794 KB with the bound (1.43 MB without it). Seeks while recording are not slower:
   −4.2 % per frame with a seek every 10th frame; a seek waits about 5.5 µs for the last capture's pieces. On the heavy
   set every difference is inside the run-to-run spread (worst +2.3 points without the journal on ZX-Evo with its
   sound cards, 10 rounds; ZX-Evo without them −0.7).
4. **Variant.** Waking the worker once per capture (mode 2) is as good as or slightly better than once per piece
   (mode 1) and wakes it at most once a frame: the variant to take. A yield-spin wait on a piece being packed is
   enough: waits are too rare to matter. The other handoffs of the charter (a ring of fixed slots, a vector swapped
   per frame) were not built: with the handoff at 2-3 % of the saving there is nothing left for them to win.

## Acceptance criteria

| Criterion | Result | Met |
|---|---|---|
| Upper bound ≥ 5 % on the fixture | 7.0 % / 8.9 % | yes |
| `BM_Frame_TTD_Gaming` ≥ 5 % faster, median of ≥ 6 rounds | −6.7 % (bounded queue, 6 rounds) | yes |
| No heavy configuration > 2 % slower | worst +2.3 points (ZX-Evo + sound cards, no journal), inside a 7-14 point spread; its maximum possible effect is 3.9 % the other way | yes, within noise |
| Seek-heavy case ≤ 5 % slower | −4.2 % (faster) | yes |
| CPU of both threads ≤ +10 % | +0 % | yes |
| Pending memory ≤ 1 MB | 794 KB with the bound | yes |
| Payloads byte-identical | identical (test over 400 frames, modes 1 and 2) | yes |

The performance criteria are met. They price speed, CPU and memory, not the cost of the change itself, and that
decides here.

**Verdict: no-go, not adopted.** The gain is real but too small for what it costs:

- **Small in practice.** 50 µs per recording frame is 2.5 ms a second at 50 frames/s, about 0.25 % of one core. At
  normal speed nobody sees it: emulation fits its frame with a wide margin either way. It shows only when recording
  in turbo or with many machines recording at once; on idle machines it is in the noise.
- **Costly to keep.** The piece store becomes two-threaded. Every reader of the history has to settle the queue
  first; one that forgets reads wrong data silently, not a crash. The prototype already hit one such subtlety (the
  free order of bases changed the id order). Adoption also needs TSan runs, the worker's lifetime, counters moved to
  queue time, and the same care from every later change to TTD.

Revisit if recording in turbo (or many machines recording at once) becomes a goal with its own numbers. The
cheaper single-threaded alternative, one zstd context reused per thread, is measured in stage 3.

## Stage 3: a reused zstd context

One context per thread instead of one per call cuts zstd time by 9-10 % (53.3 → 47.9 µs per capture on the fixture),
the same bytes, a change of a dozen lines in `codec::Compress` with no new thread or state shared between threads.
On the frame it is 0.3-1.1 %, below the noise. Worth taking as a cleanup (it also makes the header's claim true),
not as a performance item.
