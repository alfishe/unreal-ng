# Proposal

Adopt the results into [conditions-and-callbacks.md](../../../docs/inprogress/2026-10-26-breakpoint-taps/conditions-and-callbacks.md)
(breakpoint taps phase 5, conditional breakpoints F1):

1. **Conditions:** build F1 as designed: fast shapes for the common forms and the RPN VM for the rest, with
   short-circuit jumps. No JIT. The worst case (an address-less condition per instruction) is 1.4 % of a core at real
   time, so registerpoints are allowed, with the warning badge.
2. **Lua sync callbacks** run in a hook state owned by the emulation thread, called through the raw API with a
   registry reference (not sol2). They are never sent through the AutomationLua worker per hit.
3. **The per-call budget for Lua is an instruction count** (count hook every 1000 instructions, default 100 000). It
   is deterministic and stops runaways exactly. Wall clock only detects overruns.
4. **Python and remote clients are deferred** by default: a 2-4 ns push into a per-script ring, drained by the
   script's thread. When Python ever calls from the emulation thread, it keeps a thread state per thread instead of
   `PyGILState_Ensure` / `Release` per call (3 µs → 125 ns).
5. **Over-budget policies** `suspend` (default), `unpaced`, `disable` and `ignore`, as the design's §5 describes.

Not measured here, for the implementation: x86-64 numbers (only if a result gets close to a limit), and the hook
state's sandbox (replacing `os.time` / `math.random`).
