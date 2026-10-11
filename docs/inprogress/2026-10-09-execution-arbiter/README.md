# Execution arbiter: one place decides whether the machine runs

**Started:** 2026-10-09 · branch `exec-arbiter` · status: design agreed; X0 (the arbiter alone, tests, fuzz) done, X1 next

Today about 150 places outside the tests call `Emulator::Pause` / `Resume` directly. `_isPaused` is one bool: it has
no owner and no count. Any of these calls can lift a pause that someone else set, and a silent resume can unpark the
main loop while another thread is stepping the Z80. The bug that led here: a second SPRINTER created
through automation in unreal-qt hangs in the BIOS ([design.md](design.md) §1).

The design: a per-machine **execution arbiter** with one ordered request queue. Every request, from any thread, goes
into the queue and gets a ticket.

- **Intents** (run / pause) are a desired state. Consecutive intents coalesce, and the last one wins. A configurable
  throttle window bounds how long they gather.
- **Commands** are events with a result: media, reset, snapshot load, TTD, stepping. They are barriers. They run in
  order, they hold the CPU while they run, and they never change the intent.
- **A priority lane** carries breakpoint pauses from the emulation thread, abort of a long command, and stop.

Only an applied intent change is broadcast as Paused / Resumed. Every mode is tested, with model-based and machine
fuzzing (design §8).

| Document | What |
|:--|:--|
| [design.md](design.md) | the bug, goals, the queue model, API, thread rules, migration, invariants, testing and fuzzing in every mode |
| [TODO.md](TODO.md) | work list by phase, what X0 found |
| [inventory.md](inventory.md) | every current call site by class, the races found, the existing deferred mechanisms |
