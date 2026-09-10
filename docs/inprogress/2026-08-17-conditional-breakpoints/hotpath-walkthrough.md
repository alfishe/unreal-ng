# How Fast Conditional Breakpoints Work — A Plain-Language Walkthrough

Date: 2026-08-17
Status: explanatory companion to [`performance.md`](performance.md) (strategy) and [`implementation-hotpath.md`](implementation-hotpath.md) (code-level spec). This document explains the same design in generic terms, with one worked example followed from end to end. A short glossary at the bottom translates the terminology used in the sibling documents.

---

## 1. The single idea everything rests on

The speed of evaluating a condition almost does not matter. **What matters is how rarely we evaluate it.**

The design is a funnel of checks, ordered from cheapest to most expensive. Each stage runs 10–100× less often than the one before it, so expensive work is only permitted at stages that almost no events ever reach:

```
every memory access / instruction   (millions per second)
      │  1 byte flag check — ~1 CPU cycle
      ▼
accesses of a kind that has breakpoints
      │  2 array lookups — ~3–4 ns
      ▼
accesses that touch a breakpoint address     (thousands per second, at most)
      │  find which breakpoint — ~20–40 ns
      ▼
accesses where the breakpoint matches
      │  evaluate the condition — ~2–30 ns
      ▼
condition is true                            (human-scale: a few per session)
      │  pause the emulator, notify the UI — microseconds, who cares
      ▼
debugger opens
```

Crucially, **no per-condition logic of any kind sits in the "every opcode" path**. The check that runs on every access is a fixed, condition-independent test: "could anything possibly be interested in this address?" Everything condition-specific lives behind an address match — and an address match is, by definition, as rare as the user made it when choosing where to put the breakpoint.

## 2. Worked example, end to end

The user types:

```
wp $5C78 w if A==0        ; stop on writes to $5C78, but only when register A is zero
```

### 2.1 What happens at set-time (once, outside the emulation thread)

1. **The condition text is translated once.** `A==0` is parsed, and because it has a simple shape ("register compared to a number"), it is stored inside the breakpoint record as a small fixed form — four fields:

   ```
   shape:       register-compared-to-number
   register:    A
   comparison:  ==
   number:      0
   ```

   (Complex conditions that don't fit any simple shape are instead stored as a short list of precomputed steps — see §3. The user never sees the difference.)

2. **A lookup table is marked.** There is a 64 KB byte array, one byte per Z80 address, where each byte says "a breakpoint of kind read/write/execute exists at this address". We set the *write* bit at index `$5C78`.

3. **A global flag is raised**: `hasWrite = 1` ("at least one write breakpoint exists anywhere").

That's it. Nothing was installed into the emulation loop; the loop just starts seeing different values in data it already reads.

### 2.2 What every unrelated instruction now pays

The emulator executes `LD B, 5` at address `$8000` — an instruction with no connection to our breakpoint:

- **Opcode fetch** (a memory read): check `hasRead` — it is 0, branch not taken. Total cost: **one byte load and one perfectly predicted branch, ~1 CPU cycle.** We have no read breakpoints, so the read path pays essentially nothing.
- **PC check** (execute): `hasExec` is 0 — same ~1 cycle.
- The instruction performs no writes, so the write path is never entered.

Now `LD ($4000), A` — a write, but to a different address:

```cpp
if (hot->hasWrite) {                                   // load + branch: taken (flag is 1)
    uint8_t f = hot->z80Flags[0x4000]                  // load from the 64 KB table → 0
              | hot->slotSlice[1][0x0000];             // load from the per-page table → 0
    if (f & WRITE_BIT) { ... }                         // 0 → not taken, done
}
```

Total for a "foreign" write: **3 loads from L1 cache + 2 branches ≈ 3–4 ns.** No switch statement, no condition logic, no hashing, no function call — the compiler inlines this into a straight-line sequence of about 8 machine instructions. (The second array — `slotSlice` — exists so that breakpoints placed on a *physical memory page* keep working when that page is remapped to a different Z80 address; remapping only updates 4 pointers. See `implementation-hotpath.md` §2.)

### 2.3 What the matching write pays

Eventually the program executes `LD ($5C78), A`:

1. The 64 KB table gives a non-zero write bit → proceed deeper (first time in millions of events).
2. A hash lookup finds the breakpoint record; its enabled/type fields are checked (~20–40 ns).
3. **Only here** does condition-specific code run. For our simple stored form it is one branch on the shape plus one comparison:

   ```cpp
   case RegisterComparedToNumber:
       return z80->a == record.number;      // read one byte, compare — ~2–5 ns
   ```

4. If A is 7 → false → **immediate return with zero side effects**: no allocation, no notification, no counters, nothing written anywhere. The emulation continues as if nothing happened.
5. If A is 0 → true → now, and only now, the expensive machinery runs: pause, timestamp, message to the UI, wait. This happens at human frequency (you hit the breakpoint), so microseconds are fine here.

## 3. Simple conditions vs. complex conditions

**The general mechanism** works for any expression: at set-time the text is translated once into a short list of primitive steps — "load register HL", "load the byte at that address", "compare", "AND the results" — and at hit-time a small loop walks that list. Every name is resolved during translation (register names become fixed byte offsets into the CPU state structure), so executing a step is just a memory load or an arithmetic operation: **~1–2 ns per step, ~30 ns for a genuinely complex condition** with 15 steps. No text is ever re-parsed at hit-time, nothing is allocated, and errors are returned as values rather than thrown.

**The shortcut** exists because real-world conditions are overwhelmingly primitive. We collected every example published in the documentation of four reference emulators: 19 of 23 are a single comparison — register vs. number, register vs. register, one memory byte vs. number, a flag test. For those shapes the translator skips the step-list entirely and stores the four-field form shown above; checking it is ~3 machine operations. Two-clause `X && Y` where both clauses are simple is stored as a pair of forms and naturally stops after the first false clause.

**Why two evaluation paths don't create bugs**: the step-list path can evaluate everything, including the simple shapes. Tests run every simple condition through *both* paths over thousands of randomized CPU/memory states and require identical answers. The shortcut is invisible; behavior is always defined by the general path.

**What we deliberately don't do**: translate conditions to native machine code at set-time. No emulator in our research does it, it drags in per-CPU-architecture code generation and OS security ceremony (writable-then-executable memory pages, extra rules on Apple Silicon), and it would save single-digit nanoseconds on a path that only runs after an address match. The design keeps a clean seam to add it later if profiling ever demands it — it never has for anyone.

## 4. Performance estimates

Pentagon at real-time speed ≈ 0.9 M instructions/s, 2–3 M memory accesses/s. A modern host runs ~4 cycles/ns.

| Scenario | Added cost per event | Total impact |
|---|---|---|
| Debugger off | 0 — the fast memory path contains no breakpoint code at all (unchanged from today) | **0%** |
| Debugger on, no breakpoints | ~1 cycle flag check × ~3 M/s | **≪ 1%** |
| Our `wp $5C78 w if A==0` armed | write path ~4 ns × ~0.7 M writes/s; read/execute paths ~1 cycle each | **~0.3–0.5% of one core** |
| Program hammers exactly $5C78 in a loop, condition false (pathological) | ~40–60 ns × hit rate; even at 500 K hits/s | **~2–3%** |
| `bp 0000-FFFF if A==0` — condition on *every instruction* (the only way to get condition code into the per-opcode path, and it requires explicitly asking for it) | ~50–90 ns × 0.9 M/s | **~5–8%** real-time; larger when running unthrottled. UI shows a warning badge on such breakpoints |

**Reference point:** the *current* implementation, with a single ordinary breakpoint armed, pays a page-mapping call plus 1–2 hash lookups on **every** access — ~20–50 ns × 3 M/s ≈ **6–15% of a core**. The new scheme is therefore roughly 10× faster than today's unconditional breakpoints while adding conditions, ranges, and physical-page support.

And for contrast with conventional debuggers: a trap-based debugger (GDB host-side, Visual Studio) pays 0.26–1 **milli**second per false condition hit, because every hit stops the whole process and crosses the kernel twice. Evaluating in-line inside the emulator's own dispatch loop is what puts us 4–5 orders of magnitude below that — details in [`research/performance-prior-art.md`](research/performance-prior-art.md).

## 5. Glossary (terms used in the sibling documents)

| Term in `performance.md` / `implementation-hotpath.md` | Plain meaning |
|---|---|
| Hot path | The code that runs on every emulated memory access or instruction |
| Tier 0 / prefilter (Tier 1) | The "could anything care?" checks: the global per-kind flag, then the two array lookups |
| Descriptor | The breakpoint record (address, kind, condition, counters, label…) |
| RPN / bytecode / `BpProgram` | The precomputed step list a complex condition is translated into |
| VM / interpreter / stack machine | The small loop that executes that step list |
| Fast predicate / tagged POD | The four-field stored form for simple conditions |
| Shunting-yard | The standard algorithm that turns `A==0 && HL>4` into the step list, honoring operator precedence |
| Constant folding | Pre-computing pure-number math at set-time (`$0F+1` becomes `$10`) |
| JIT | Translating a condition into real machine code at set-time (rejected — see §3) |
| Slot / bank | One of the four 16 KB windows of the Z80's 64 KB address space |
| Physical page | An actual 16 KB block of emulated RAM/ROM, independent of where (or whether) it is currently visible in the Z80 address space |
| Interval list | The sorted list of address ranges used to match range breakpoints after the 64 KB table says "something here" |
| Over-approximation | The lookup tables are allowed to say "maybe" when the answer is "no" (the next stage corrects it); they must never say "no" when the answer is "yes" |
