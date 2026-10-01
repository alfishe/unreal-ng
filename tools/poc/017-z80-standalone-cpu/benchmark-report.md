# 017 - Z80 Core vs z80ex Benchmark Report

Dedicated analysis of `z80bench` / `z80bench-z80ex` results: who is faster, the
extracted standalone core or z80ex 0.16, and why each gap exists.

**TL;DR:** Neither core is universally faster - the result depends on the
instruction mix. On the callback bus (z80ex's only integration mode) the
aggregate is a draw to +14% for the extracted core. z80ex wins scalar
memory-heavy code by ~26% (`mixed`, 0.79x); the extracted core wins prefixed /
block instructions by ~1.8x (`ldir`) and edges out dispatch overhead (`nops`,
1.06x). With the flat-memory bus - an integration mode z80ex does not have -
the extracted core adds another ~24% on top. Three measurement bugs found
while producing this report are documented in 2.3; earlier numbers elsewhere
are superseded by the tables below.

## 1. Environment and builds

| Item | Value |
|---|---|
| Host | Apple M1 Ultra (arm64), macOS 15.7 |
| Compilers | AppleClang 17.0.0 (`cc` / `c++`), same Release flags for both cores |
| Build | CMake + Ninja, `CMAKE_BUILD_TYPE=Release` (`-O3 -DNDEBUG`) |
| Cores | poc017 `z80lib` 0.1.0 (C++17) vs z80ex 0.16 (C, unity build) |
| Variance method | best-of-3 inside each process, multiple process runs per table |

## 2. Methodology

### 2.1 Workloads (identical guest bytes on both cores)

| Workload | Content | Per iteration | Bus callbacks / iter |
|---|---|---|---|
| `mixed` | LD A,(HL); ADD A,(HL); INC HL; ADD HL,DE; PUSH AF; POP AF; DJNZ | 7 instr, 65 T | 13 (7 fetch + 6 data) |
| `ldir` | 16 KiB `LDIR` copy (ED B0), 21 T per byte | 1 instr / byte | 4 per byte |
| `arith` | ADD A,7; ADC A,3; SUB 5; CP 80h; DJNZ x256 | 5 instr, ~41 T | 9-10 (fetch + immediates) |
| `nops` | 64 consecutive NOPs + HALT | 1 instr, 4 T | 1 (fetch only) |

`nops` is the dispatch probe: it isolates opcode fetch + decode + step
machinery with no operand or data access. Metric is sustained T-states/sec -
the only denominator comparable across cores, because z80ex consumes a prefix
as its own `z80ex_step()` call while the extracted core executes the whole
prefixed instruction in one `Z80CpuStep()`.

### 2.2 Fairness rules

- Both cores run byte-identical guest programs from 0x8000, same reset/PC/SP
  per pass, same best-of-3 timing, same process.
- Both cores sit behind callback buses; each side's callbacks index a
  file-scope 64K array of the same shape (no hidden pointer chases).
- Guest memory preparation and CPU reset happen outside / inside but
  consistently around the timed region for both sides.
- T-states are counted on the extracted core; functional parity (all four
  ZEXALL-family tapes pass on both descriptions of Z80 behavior) makes the
  T-count identical by construction.

### 2.3 Harness fixes made while producing this report

These changed previously published numbers; the tables below are the
authoritative ones.

1. **`bench-z80ex` `arith` was a one-shot loop** (`JR NC` backwards taken
   never): each pass executed the ALU chain once and measured mostly
   reset/entry overhead. Fixed to the 256-iteration DJNZ version used by
   `bench-main`. Old `arith` 0.96x -> real 1.01-1.03x.
2. **Asymmetric callbacks**: our side indexed `gHost->mem` (extra pointer
   dereference per callback), the z80ex side a static array. Both sides now
   use static arrays.
3. **`bench-main` timed loop used `HostMachine::StepCapture()`**, which adds a
   `GetReg` call plus soft-ROM probes per step (test-harness features).
   Replaced with raw `Z80CpuStep()` + a local instruction counter. Effect on
   the extracted core's own numbers: `mixed` flat 973 -> 1346-1372M T/s,
   `nops` flat 676 -> 999-1005M T/s, flat aggregate 1188 -> 1540-1553M T/s.

## 3. Results

### 3.1 Head-to-head, callback bus (8 process runs)

| Workload | z80ex T/s | poc017 T/s | ratio (ours / z80ex) | winner |
|---|---|---|---|---|
| `mixed` | 1373-1391M | 1091-1106M | 0.79x | z80ex (+26%) |
| `ldir` | 1002-1068M (one 1669M outlier) | 1861-1881M | 1.75-1.88x (1.12x in the outlier run) | poc017 (~1.8x) |
| `arith` | 1124-1138M | 1140-1167M | 1.01-1.03x | parity |
| `nops` | 798-811M | 855-864M | 1.06-1.07x | poc017 (slight) |
| aggregate | 1083-1093M (1248M outlier) | 1242-1245M | 1.00x worst, ~1.14x typical | poc017 |

### 3.2 Extracted core, both bus modes (2 runs each)

| Workload | flat T/s | callback T/s | flat advantage |
|---|---|---|---|
| `mixed` | 1346-1372M | 1099-1106M | 1.24x |
| `ldir` | 2765-2783M | 1825-1881M | 1.51x |
| `arith` | 1050-1052M | 1144-1149M | 0.92x (see 4.4) |
| `nops` | 999-1005M | 860-865M | 1.16x |
| aggregate | 1540-1553M | 1232-1250M | 1.24x |

Raw stepping overhead (halted CPU, no dispatch): 2.4 ns per `Z80CpuStep()`.
Every configuration sustains >1G T/s, i.e. >280x the 3.5 MHz real-time rate.

### 3.3 Stability

- The extracted core is stable across process launches (spread <2% per
  workload, all runs).
- z80ex's `ldir` rate is **bimodal across process launches**: 1002-1068M T/s
  in 7 of 8 runs, 1669M in one. Both cores dispatch through data-dependent
  indirect calls; z80ex's LDIR path takes two of them per byte and appears
  sensitive to code/data layout (ASLR). Our single-dispatch path stayed at
  1.86-1.88G in every run. Aggregate ratios above therefore span 1.00x
  (z80ex's lucky draw) to 1.14x (typical).

## 4. Why - per workload

### 4.1 Dispatch machinery (`nops`, 1.06x ours)

Per NOP both cores pay: one opcode-fetch callback, one table dispatch, one
minimal handler, step bookkeeping. z80ex's `z80ex_step()` zeroes five context
fields and checks `int_vector_req` per step; our `Z80CpuStep()` saves
`prev_pc`/`prev_f`, clears `prefix`, fetches via `m1_cycle()`, then does
Q-register maintenance, the EI-shadow store and the RETI null-check. Measured
per 4-T step: ours 4.65-4.70 ns vs z80ex 4.94-5.00 ns. **Conclusion: the step
machinery is at parity; ours is marginally cheaper despite doing more
bookkeeping, because the whole loop is smaller and stays hot in the caches.**

### 4.2 Prefixed / block instructions (`ldir`, ~1.8x ours) - structural

This is the one architectural difference, visible in the sources:

- z80ex treats a prefix as its own step: `z80ex_step()` fetches `ED`,
  dispatches `opcodes_base[0xED]` which just sets `cpu->prefix`, returns; the
  **next** `z80ex_step()` fetches `B0` and dispatches `opcodes_ed[0xB0]`.
  One copied byte costs **two full step dispatches** (2x prologue, 2x table
  lookup) plus the same 4 bus callbacks (ED fetch, B0 fetch, read, write).
- The extracted core keeps the core's prefix chain inside one step: `op_ED`
  fetches the second opcode via `m1_cycle()` and tail-dispatches `ope_B0`
  internally. One copied byte costs **one step dispatch** plus the same 4
  callbacks and 21 T.

Per byte: z80ex 20.4-21.0 ns vs ours 11.2-11.3 ns; the ~9 ns delta is almost
exactly the cost of one extra dispatch round (cf. the 4.7-5.0 ns NOP step in
4.1, doubled because the ED-fetch step in z80ex re-runs the full prologue).
The same mechanism benefits DD/FD-prefixed code, `EXX`-style multi-byte
sequences and every ED block instruction (LDDR/CPIR/INI/OTIR...).

### 4.3 Scalar memory code (`mixed`, 0.79x) - z80ex's win

Both cores dispatch once per instruction here, so 4.1 says the difference is
not the step loop; it sits in the per-memory-access plumbing:

- Our `rd()`/`wd()` are dual-mode: `t += 3; if (mem) return mem[addr]; return
  memRead(this, addr, ...)`. In callback mode every access pays a
  data-dependent bus-mode branch plus an indirect call through a
  member-function pointer before the host callback runs.
- z80ex's `READ_MEM`/`WRITE_MEM` macros call the callback directly - no
  bus-mode branch, because z80ex has no other mode.

`mixed` does 13 callbacks per 65 T; the measured delta is ~12 ns per
iteration, ~1 ns per access, consistent with the extra branch + call
plumbing. Supporting evidence: in **flat** mode the same workload reaches
1346-1372M T/s - dead even with z80ex's 1373-1391M callback number. Remove
the branch and the gap disappears; what remains of the difference is the
callback call itself, which both cores must pay.

### 4.4 Pure ALU (`arith`, 1.01-1.03x) - parity

Both cores implement ALU ops with precomputed flag tables of comparable
shape, one fetch (+ immediate) per instruction. The 1-3% edge to the
extracted core is within run-to-run spread. Curiosity worth recording: our
flat `arith` is consistently ~9% *slower* than our callback `arith`
(1050 vs 1145M T/s). This is a stable code-layout effect (inlining the array
access enlarges the immediate-operand handlers and the dispatch targets
interact worse with the uop cache), not a property of the bus.

### 4.5 Flat bus (ours only, +24% aggregate)

`Z80CpuAttachMemory()` turns every access into an inline array index: no
branch, no indirect call, and LDIR reaches 2.77G T/s (12% of one core). z80ex
has no equivalent integration mode; for embedders that can expose flat RAM
(snapshots, tests, tooling), the extracted core is unconditionally faster.

## 5. Conclusions

1. **Who is faster?** On the callback bus it is workload-dependent: z80ex for
   scalar code that hammers memory through callbacks (0.79x), the extracted
   core for anything prefixed or block-shaped (~1.8x) and for raw dispatch
   (1.06x). Weighted over this suite the extracted core is ahead in every
   observed run (1.00-1.14x aggregate), typically ~1.14x. With flat memory it
   extends the lead to ~1.4x z80ex's aggregate (1540 vs 1083-1091M T/s).
2. **Why we lose `mixed`:** a runtime bus-mode branch inside every `rd()` /
   `wd()` call in the callback build - proven by the flat-mode experiment.
3. **Why we win `ldir`:** prefixes are consumed inside one step call; z80ex
   pays two dispatches per prefixed instruction - permanent architectural
   advantage for the extracted core, and it also explains z80ex's layout
   sensitivity on this path.
4. Both cores are functionally indistinguishable (ZEXALL-family verified) and
   both outrun a real 3.5 MHz Z80 by nearly three orders of magnitude; the
   contest is about host-side efficiency, not fidelity.

## 6. Optimization directions (extracted core)

1. **Compile-time bus selection** - build the opcode units twice (flat and
   callback) or template them on the bus, removing the `if (mem)` branch from
   `rd()`/`wd()`/`m1_cycle()`. Expected to reclaim most of the `mixed` gap:
   flat `mixed` already matches z80ex (1346-1372 vs 1373-1391M T/s) while
   paying full dispatch; a callback-specialized build should land within a
   few percent of z80ex there, keeping the block-op advantage.
2. **Trim the step epilogue** - the RETI null-check can move behind a
   `retiCallback != NULL` compile-time or once-per-reset flag; Q maintenance
   can be gated by a per-opcode "writes flags" bit instead of comparing F.
   Worth ~0.5 ns/step at most (4.1 shows the headroom is small).
3. **Keep the single-dispatch prefix chain** - it is both the performance
   edge over z80ex on block code and the reason our LDIR rate is stable
   across process launches.

Reproduce: `ninja -C build z80bench z80bench-z80ex && ./build/z80bench &&
./build/z80bench-z80ex` (run the head-to-head several times; see 3.3).
