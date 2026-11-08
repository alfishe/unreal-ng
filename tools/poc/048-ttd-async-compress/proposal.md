# 048 — proposal

Verdict and numbers: [conclusions.md](conclusions.md).

**Not adopted** (no-go, 2026-11-07): the gain is about 0.25 % of a core at normal speed, against a two-threaded
piece store. What follows is the design to start from if turbo recording makes the gain worth it later:

1. **Worker lifetime.** Started with the first queued piece of a recording, stopped when the store is cleared or the
   session ends; no thread while TTD does not record. A shared store (D22) has one worker.
2. **Handoff.** A capture queues its changed pieces (difference and content copied into a job) and wakes the worker
   once at its end (POC mode 2). At most 96 pieces wait; past that a piece is packed on the emulation thread.
3. **Single-threaded store.** The worker only runs zstd; the emulation thread applies the answers with today's
   encoding rule at the next capture or before any read, so the stored bytes and ids are those of today's store.
4. **Reads settle once.** Instead of the POC's settle in every accessor, the readers (decode, export and save,
   ring drop, heap and payload totals, the chain limit's base read) call one `SettleAll` at their entry.
5. **Counters at queue time.** `compressCalls`, compress input bytes and the device blob bytes of a capture are
   counted when a piece is queued, so the CI gate's deterministic counters stay as they are; the queue's memory is in
   `HeapBytes`; `RegionPayloadBytes` resolves without waiting a capture.
6. **Tests.** The POC's byte-identity test (synchronous against worker store over a mixed sequence) becomes a unit
   test; the TTD suites run with the worker on; a sanitizer (TSan) run of the TTD suites before landing.
7. **Benchmarks.** `BM_Frame_TTD_SeekWhileRecording` joins the frame fixture; the A/B of the fixture and the heavy
   set is repeated with the adopted code (≥ 6 rounds, heavy set included).

Rejected within the design: per-piece wake-up (mode 1, no better), other handoff structures (nothing left to win).
