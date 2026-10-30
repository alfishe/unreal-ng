# 038 — proposal: a replay query engine in core

> **Deferred (owner, 2026-10-28).** Phase 2 makes the in-place replay fast first
> ([phase 2 plan](../../../docs/inprogress/2026-10-28-ttd-analysis-feedback/phase-2-plan.md), POC 040). The pool
> comes back only if real agent questions need it: one background clone first, then this pool for batch work.

The POC code is not linked into core. Adoption re-implements it in `core/src/debugger/ttd/` with unit tests,
exactness tests per model and A/B benchmarks against today's verbs.

## What the engine does

A **window query** names a frame window and the observers it needs, and gets all their answers from one replay:

| Observer | Answer | Today |
|----------|--------|-------|
| exec probe (address, page) | frames and times where it ran | `reverse_continue` all, paged, 20 s / 50k frames |
| write probe (range, page) | each write: time, PC, address, page, value, old value | `find_accesses`, 53 s / 50k frames |
| read probe (range, page) | each read with its PC (fonts, tables, sprites) | none |
| port probe (port, mask, direction) | each access, e.g. the AY timeline (#FFFD register, #BFFD value) | none |
| probe fired | the frames in which a probe (exec / read / write / port, HapticProbe-shaped) fired | none |
| PC profile | instructions per PC (and per page) over the window | the frame cache, 2.1 ms / frame |

The probe-fired query is an answer only. Replays never drive haptics, sound or the Stream Deck.

## How

1. **A clone pool.** Headless instances of the session's model (no sound out, no video out) load the session from
   memory.
   - The pool stays warm while the session does not change: a new checkpoint, a barrier or a stop rebuilds it.
   - An end that only moved re-sends the new frames' journal.
   - Size: performance cores / 2 by default (8 on a 16 + 4 Mac: the live machine stays within +5 %), set per call.
   - Memory: 35-60 MB per clone. The pool is released after an idle minute and on memory pressure.
2. **Chunks, not stripes.** The window is cut at checkpoints into chunks of about 500 frames, and the clones take
   them from a queue. A chunk ends early at a replay barrier, as the verbs do.
3. **An exactness check on every chunk.** At the chunk end the clone's machine is compared with the next
   checkpoint: CPU, chipset, decoded devices and RAM page hashes. This costs one checkpoint restore per chunk, under
   1 % at 500 frames.
   - On a mismatch the query fails with the frame and the device.
   - It never returns a silently wrong answer.
4. **Internal seeks.** A clone seeks without composing the picture, so no frame is replayed for nothing (F3).
5. **The live machine is not touched.** No replay mode, no saved and restored live state, no lock beyond reading the
   session for the copy. The user keeps running, recording or scrubbing.
6. **Jobs on every automation plane.** `ttd query start` returns a job id. Then:
   - progress (frames done, answers so far);
   - cancel;
   - paged results with a cursor;
   - on WebSocket, a progress topic and a done topic.
   Small windows (under one chunk) answer at once, without a job. The planes are CLI, Python, Lua, MCP, REST and
   WebSocket. The docs chain is `command-interface.md` -> TTD module doc -> plane docs.

## Prerequisites (engine fixes found by the POC)

1. **F1: the baseline interval.** Fixed 2026-10-28: the baseline checkpoint stores that the FM chips still adopt the
   CPU's clock (TSFM board byte bit 5).
2. **F2: host settings and turbo.** Fixed 2026-10-28: the TSFM render mode (quality, synthesis suppressed) is pinned
   to the recording's while the machine is bound to a timeline, so a clone replays exactly with the sound off, in LQ
   or in turbo. A recording changes the mode only at a frame start, and the change is a `ConfigChange` cut. A
   chunk therefore ends at a cut, as it ends at a barrier.
   - Both fixes: [TTD analysis feedback](../../../docs/inprogress/2026-10-28-ttd-analysis-feedback/README.md#engine-fix-turbosound-fm-replays-on-any-host-poc-038-findings-f1-f2).
3. **F3: a seek that does not present.** An internal `SeekTo` variant for clones. The public one keeps composing the
   picture.

## Steps

| Step | Content | Proof |
|------|---------|-------|
| 1 | first-interval corpus test (F1 and F2 are fixed) | `TimeTravelControllerCorpus_Test` with the baseline interval, every model |
| 2 | `TtdClonePool`: create, load from the in-memory session, invalidate, idle release | unit tests; RSS and setup benchmark |
| 3 | `TtdWindowQuery`: chunks, queue, exactness check, observers exec / read / write / port / profile | answers = today's verbs on the POC workloads (unit tests at 300 frames, long tier at 20k) |
| 4 | probe-fired and AY timeline answers | tests against hand-computed frames |
| 5 | jobs + all planes + docs | plane tests, `tools/verification/mcp/run_all.py` |
| 6 | A/B benchmark | Google Benchmark, query vs verbs at 3k frames, live-machine frame time with the pool running |

Gates are the same as the POC's: answers exact, the 50k W1 question in 5 s or less at 8 clones, live machine within
+5 %, zero warnings on clang, gcc, MSVC and MinGW.

## Not in this proposal

- Changing today's verbs. They stay for single answers near the cursor, where one replayed frame is faster than a
  pool.
- Running clones on other hosts.
- Writing into the session from a clone. Clones only read.
