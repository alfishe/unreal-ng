# Analysis

Numbers from [measurements-2026-10-26-conditions-callbacks.md](measurements-2026-10-26-conditions-callbacks.md).

## Conditions

- **Fast shapes: 1.1-1.8 ns**, of which 0.5 ns is the loop. A condition on a tap adds about 1 ns per hit, below
  anything a frame benchmark can see. The budget of 5 ns (R02 P-2, performance.md tier 3) holds with room to spare.
- **RPN VM: 8.6-16 ns for 4-22 ops.** The 4-op programs cost 8.6-8.9 ns, so the fixed cost (entering the loop, the
  first dispatch, the stack) is about 6 ns, and each further op costs 0.4-1 ns. Short-circuit jumps keep the long
  condition at 15-16 ns. The VM is 5-9× a fast shape, so the shapes are worth having for the common forms. The VM
  stays the general evaluator: the 30 ns budget is met at 22 ops, and a JIT has nothing left to save (performance
  notes said 5-25 ns).
- **Mostly false vs random** makes no difference beyond the short circuit, so branch prediction is not hiding
  anything: the states are cycled from a 4096-entry table.
- **Per-hit cost at real rates.** The worst case of performance.md is a full-range exec condition at 0.9 M
  instructions per second. At 16 ns that is 14 ms per second, **1.4 % of a core at real time**, and 6 % under ×4
  turbo. That is below the 3-8 % the design estimated. A hot false watchpoint at 1 M writes per second costs the same.
  Conditions on taps (event rates, ≤ 1000 hits per frame) cost under 16 µs per frame: 0.08 % of a 20 ms frame.

## Lua

- **A Lua call from a state the caller owns costs 19 ns (empty body) and 31 ns (a realistic body).** That is about
  20× cheaper than the 0.5 µs expected, and 3× cheaper than performance.md S6 assumed (~100 ns). sol2 adds 12 ns, so
  the hit path should use the raw API (`lua_rawgeti` + `lua_pcall`) with a registry reference.
- **Lua's cost is not the problem; where its state lives is.** A round trip to the thread that owns the state
  (today's `AutomationLua` worker) costs 3-4 µs p50 and 16-18 µs p99 with a condvar. That is 100-500× the call itself.
  A spinning worker gets 125-583 ns, but burns a core per emulator. A synchronous callback has to run in a state
  owned by the emulation thread.
- **GC.** An allocating callback pays p50 84 ns and stalls of 21-43 µs (max). These are rare stalls on a
  1-20 ms budget, so a per-frame `lua_gc(LUA_GCSTEP)` is not needed. The budget measurement must count them
  anyway: the stall lands inside one callback.
- **The count hook is the deterministic budget.** With the hook on every 1000 VM instructions, the realistic body
  costs +8-10 ns, which is +26-34 % relative to the bare call. That misses the "≤ 10 %" expectation in relative terms
  but is noise in absolute ones (a fraction of the 34 ns a clock-based check would cost). A runaway `while true` is
  stopped exactly at the budget: at 2.3 ns per VM instruction, 100 000 instructions take 0.23 ms and 430 000 take
  ~1 ms. An instruction count does not depend on host load, so the same hit stops at the same point on every host,
  under TTD re-run and in CI. Wall clock cannot give that.
- **Hook granularity.** A count of 1000 bounds the overrun to 999 instructions (~2 µs). A smaller count costs more per
  call; 1000 is the right order.

## Python

- **Python is fast when the GIL is free and the thread state is kept: 125 ns**, only 4× Lua. The per-call
  `PyGILState_Ensure` / `Release` pair is the trap: it creates and deletes a thread state on every call (3 µs).
- **Python is unbounded when any other Python thread runs.** With a script busy in Python, the emulation thread waits
  for the GIL hand-off: p99 6.3 ms at the default switch interval of 5 ms, and 0.7 ms at 0.5 ms. The wait does not
  depend on the callback, so the callback cannot control or budget it. An automation script is exactly that kind of
  busy thread (a loop polling the emulator). Python callbacks therefore cannot be synchronous on the emulation thread
  by default. They are deferred: the hit is queued (2-4 ns), and the script's own thread drains it with the GIL it
  already holds (83 ns per call).
- **Stopping a runaway Python callback** cannot work like Lua's count hook. The options are
  `PyThreadState_SetAsyncExc` or `Py_AddPendingCall` (raise in the running thread at the next bytecode boundary),
  or a `sys.settrace` / `sys.monitoring` budget, which is expensive. For deferred callbacks this matters less: they run
  on the script's thread, and a runaway hangs the script, not the emulator.

## Measuring a callback

- **Two clock reads cost 34 ns**, the same as the Lua call itself. Always on, at 1000 synchronous hits per frame,
  that is 34 µs per frame (0.17 %). That is acceptable, but it doubles the per-hit cost of a cheap callback.
- **Cheaper alternative:** count the hook's instructions per callback (free with the count hook on), and time the
  whole callback batch per frame (two reads per frame). Time single calls only for asynchronous languages (Python
  drains), where 34 ns is nothing next to the call.

## Budgets in numbers (this host, 50 Hz frame = 20 ms)

| Item | Cost | Hits per 10 % of a frame (2 ms) |
|:--|--:|--:|
| fast condition | 1-2 ns | > 1 M (never the limit) |
| RPN condition | 9-16 ns | 125 000 |
| Lua synchronous, realistic, count hook on | ~40 ns | 50 000 |
| … plus a per-call clock pair | ~75 ns | 27 000 |
| async push (any language, deferred) | 2-4 ns | > 500 000 |
| Python synchronous, GIL free, kept thread state | 125 ns | 16 000 |
| Python synchronous with a busy script | up to 6.3 ms | 0 (one hit can exceed the frame) |
