# Measurements 2026-10-26: conditions and script callbacks

## Setup

- Host: this Mac (M-series, arm64), shared and loaded (load average 8-16 during the runs).
- Build: `cpp/` standalone, Release (`-O3`), Apple clang; Lua 5.4.7 and sol2 3.5.0 vendored from
  `core/automation/lua/lib`; CPython 3.14.8 (Homebrew, the one `core/automation/python` finds here).
- Runs: `poc039 7` five times (`scratch/2026-10-26-poc-039/run1.log` … `run5.log`). Each batched measurement is 7
  rounds; the lower quartile (q1) and the median of the rounds are printed. µs-scale work is timed per call and
  reported as p50 / p99 / max / mean. The clock resolution is ~42 ns, so per-call numbers below ~100 ns are quantised.

## Method

- **Conditions:** 4096 pre-generated CPU states (registers, pages, the accessed value) are cycled, with 64 KiB of
  random memory. There are two data sets: "mostly false" (99 % of the states make every condition false, the real
  case for a tap) and random. The empty loop is measured on its own (0.52 ns) and is included in the numbers below.
  A differential check runs each fast shape against the VM on every state: no mismatches.
- **Lua:** `luaL_newstate`, functions called through a registry ref and `lua_pcall` (raw C API), or through
  `sol::protected_function`. The bodies:
  - `trivial`: empty;
  - `real`: a counter and a conditional store;
  - `alloc`: one table per call;
  - `runaway`: `while true do end`.
  The budget is `lua_sethook(LUA_MASKCOUNT, 1000)` with the budget reset per call; the hook raises `luaL_error`
  once the budget is spent.
- **Worker round trip:** the call goes to a thread that owns the state, waits for the result, then returns
  (today's AutomationLua `dispatchSync` model). There are two variants, condvar and a spinning worker.
- **Async:** a single-producer ring of 4096 `{frame, addr, value, kind}` events, drained by a consumer thread that
  calls Lua. The producer drops on a full ring, which happened for most pushes because it outruns the consumer.
  The number is the push cost.
- **Python:** `PyObject_Vectorcall` on a two-argument function, in four setups:
  - with the GIL held by the caller;
  - from a foreign thread through `PyGILState_Ensure` / `Release` per call;
  - from a foreign thread that keeps its thread state (`PyEval_RestoreThread` / `SaveThread` per call);
  - from a foreign thread while a Python thread spins (`while not stop: x += 1`), with switch intervals of 5 ms (the
    default) and 0.5 ms.

## Results

### G1 conditions (ns per evaluation, median; spread over 5 runs)

| Condition | Fast shape | RPN VM | VM ops |
|:--|--:|--:|--:|
| `A==0` | 1.65 | 8.9 | 4 |
| `HL>=$4000` | 1.83 | 8.6 | 4 |
| `Z` = `(F&$40)!=0` | 1.43 | 12.7 | 6 |
| `MWV==0` | 1.07 | 8.9 | 4 |
| `($5C78)==13` | 1.32 | 10.7 | 5 |
| `A==0 && (HL)!=$FF` | — | 11.3 | 10 |
| `(score).w >= 1000` | — | 10.4 | 5 |
| `PG3==7 && MWV>=$80` | — | 11.3-12.7 | 9 |
| `(A&$0F)==3 && (B>5 \|\| (IX+5)==$C9) && SP<$8000` | — | 14.7-16.2 | 22 |

The two data sets differ by under 1 ns, except the longest condition: about 1.5 ns more on random states, because
the short circuit is taken less often there.

### G2 Lua

| Measurement | Result |
|:--|:--|
| own state, raw API, trivial body | 18.7 ns (batched) |
| own state, raw API, realistic body | 30.3-32.2 ns (batched); per call p50 42, p99 42, max 17 µs |
| sol2 `protected_function`, realistic | 43.1-45.2 ns |
| allocating body, incremental GC | p50 84 ns, p99 209 ns, max 43 µs |
| allocating body, generational GC | p50 84 ns, p99 1.2 µs, max 21.5 µs |
| count hook on (every 1000), realistic | 38.3-41.6 ns (+26-34 %, +8-10 ns) |
| runaway stopped at 10 000 instructions | 25.7 µs (p99 32 µs) |
| runaway stopped at 100 000 instructions | 229-235 µs |
| runaway stopped at 1 000 000 instructions | 2.33 ms (p99 2.67 ms) |
| round trip to a worker, condvar | p50 3.0-4.2 µs, p99 15.7-18.5 µs, max 55-297 µs |
| round trip to a worker, spinning worker | p50 125-583 ns, p99 167-750 ns, max 1.5-19.5 µs (burns a core) |
| async push to the ring | q1 1.6-4.1 ns, median 2.6-13.6 ns (consumer contention on the indices) |

A runaway loop costs about 2.3 ns per VM instruction, so 1 ms is about 430 000 Lua instructions on this host.

### G3 Python 3.14.8

| Measurement | p50 | p99 | max |
|:--|--:|--:|--:|
| GIL held by the caller | 83 ns | 84-125 ns | 13-30 µs |
| foreign thread, `PyGILState_Ensure` + call + `Release` | 2.9-3.2 µs | 6.5-7.0 µs | 0.2-0.6 ms |
| foreign thread, thread state kept | 125 ns | 125 ns | 11-40 µs |
| a busy Python thread, switch interval 5 ms | 3.3-6.1 µs | **6.3 ms** | 6.5 ms |
| a busy Python thread, switch interval 0.5 ms | 3.5-5.0 µs | 32-712 µs | 0.69-0.71 ms |

`PyGILState_Release` destroys the thread state when its counter reaches zero, so the per-call Ensure / Release pays
for creating and deleting a thread state every time (about 3 µs). Keeping one thread state per emulation thread
brings the GIL-free call down to 125 ns.

### G4 measuring a callback

Two `steady_clock::now()` reads cost 33.8 ns (batched; all five runs within 0.1 ns).
