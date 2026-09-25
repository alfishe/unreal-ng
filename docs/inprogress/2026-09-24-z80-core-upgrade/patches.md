# History: local patches against upstream unreal-z80 (now upstreamed)

> Moved here from `core/src/3rdparty/unreal-z80/` on 2026-09-24 as part of
> [the Z80 core upgrade write-up](README.md) — this is documentation/history,
> not something the vendored source needs to sit next to. All paths below
> that say "here" or "this directory" refer to the vendored library at
> `core/src/3rdparty/unreal-z80/` (repo-root-relative), not to this docs
> folder. [version.txt](version.txt) lives alongside this file for the same
> reason.

`core/src/3rdparty/unreal-z80/` vendors `z80lib/include` and `z80lib/src` of
unreal-z80 (pinned commit in [version.txt](version.txt)). It is the
opcode-execution engine behind the emulator's `Z80` class
(`core/src/emulator/cpu/z80.cpp`, see "How the host uses this" below).

When this integration was first done (library commit bc98816, version
"0.1.0-poc"), six targeted changes were carried there as a local patch
(`unreal-z80-local.patch`). All six were host-neutral - a general-purpose
hook, accessor or hygiene fix with no unreal-ng specifics in the library -
and were upstreamed verbatim in library version **0.2.0** (commit `faf7b7b`,
"feat(z80lib): upstream the unreal-ng integration patch; library 0.2.0", and
onward). This copy is now pinned to commit `b7451bd` (0.2.0 + the upstream
MIT `LICENSE`) with **no local patch file** - the six changes below are part
of the vendored source as-is. This section is kept as the rationale record
for each of them; see "Re-applying on upgrade" for what changed on top of
0.1.0-poc and what to re-check on the next upgrade.

| # | Change | Files | Why |
|---|---|---|---|
| 1 | Library-private `Z80Lib` namespace for the flag/DAA tables and ALU micro-ops | `z80cpu-internal.h`, `z80tables.cpp`, `z80daa.cpp` | the library did not link into this emulator: duplicate global symbols `log_f`, `inc_f`, `daatab`, ... |
| 2 | Interrupt acknowledge memory cycles go through the wait-state hook and publish T | `z80cpu.cpp`, `z80cpu.h` (docs) | contention and bus tracing were blind to the INT/NMI stack push and IM2 vector read |
| 3 | The halted CPU's M1 cycles are reported to the wait-state hook | `z80step.inc`, `opcodes-callback.cpp`, `opcodes-paged.cpp`, `opcodes-flat.cpp`, `z80cpu.h` (docs) | a halted CPU on a contended machine was never slowed down |
| 4 | `Z80CpuRegHalted` register selector | `z80cpu.h`, `z80cpu.cpp` | the HALT latch was readable but not settable - a host that mirrors CPU state could not restore it |
| 5 | Bulk register-file accessors `Z80CpuGetRegisters` / `Z80CpuSetRegisters` | `z80cpu.h`, `z80cpu.cpp` | per-register accessors cost ~45 selector dispatches per instruction for a host that syncs state every step |
| 6 | RETN notification hook `Z80CpuSetRetnFn` | `z80cpu.h`, `z80cpu-internal.h`, `z80cpu.cpp`, `opcodes-ed.inc` | a host tracking the NMI service session had no way to see it end (only RETI had a hook) |

Deliberately **not** changed (see the end of this file): the `m1State`
semantics of the memory-read callback, and the MEMPTR value of repeating
INIR/INDR/OTIR/OTDR.

---

## 1. `Z80Lib` namespace (link hygiene)

**What.** In `src/z80cpu-internal.h` the flag tables (`inc_f`, `dec_f`,
`rlc_f`, `rrc_f`, `sra_f`, `rl0`, `rl1`, `rr0`, `rr1`, `log_f`, `adc_f`,
`sbc_f`, `cp_f`, `cpf8b`, `rol`, `ror`, `rlca_f`, `rrca_f`), the DAA table
`daatab`, `Z80TablesInit()` and the inline ALU micro-ops (`and8`, `or8`, ...,
`cp8`) are wrapped in `namespace Z80Lib { ... }`, followed by
`using namespace Z80Lib;`. `src/z80tables.cpp` and `src/z80daa.cpp` wrap their
definitions in the same namespace.

**Why.** These arrays have external linkage and C-style names inherited from
the core they were extracted from. A host that still has such tables - the
unreal-ng core itself keeps `log_f`, `inc_f`, `daatab`, ... as globals in
`core/src/emulator/cpu/` - fails at link time with duplicate symbols (and on
toolchains that do not diagnose it, one definition silently wins). A library
should not claim generic global names at all.

**Why this shape.** The using-directive keeps all ~6000 lines of ported
opcode bodies unqualified and diffable against upstream; the header is private
to the library sources (no host includes it), so the directive never leaks. The
opcode handlers themselves already lived in the per-bus namespaces `Z80Flat`,
`Z80Paged`, `Z80Cb`.

**Example.** Before, linking the library into the emulator:

```
duplicate symbol '_log_f' in:  libcore.a(cpulogic.cpp.o)  libcore.a(z80tables.cpp.o)
duplicate symbol '_daatab' in: libcore.a(daa_tabs.cpp.o)  libcore.a(z80daa.cpp.o)
```

After: the library's tables are `Z80Lib::log_f`, `Z80Lib::daatab`, ... and
both sets coexist.

---

## 2. Interrupt acknowledge memory cycles through the wait-state hook

**What.** `Z80CpuInt()` and `Z80CpuNmi()` (`src/z80cpu.cpp`) no longer add a
fixed 13/19/11 T and then perform raw, untimed stack/vector accesses. The
acknowledge is now modeled cycle by cycle, like an instruction:

* NMI: 5 T acknowledge M1, then two 3 T write cycles (push PCH, push PCL).
* IM0/IM1: 7 T acknowledge M1, then the two push writes.
* IM2: 7 T acknowledge M1 (the vector byte is taken from the bus through
  `Z80CpuIntVectorFn`), the two push writes, then two 3 T read cycles of the
  vector table (low, high byte).

Each memory cycle (helpers `Z80CpuAckWrite` / `Z80CpuAckRead`):

1. calls the wait-state hook at the cycle's first T - `Z80CpuAccessWrite` for
   the pushes, `Z80CpuAccessRead` for the vector table (callback and paged
   buses; the flat bus never fires the hook, exactly as for instructions);
2. inserts the returned waits before the cycle;
3. publishes `cpu->t` (= cycle start + waits + 3) before the memory callback,
   so the host observes the exact access T;
4. performs the access.

The functions return the real duration, `13/19/11 + waits`, and leave
`cpu->t` at the end of the acknowledge. With a null hook (or a hook returning
0) the timing is identical to before: 13 / 19 / 11 T, same machine-cycle
order, same final state.

**Why.** On contended machines (ZX Spectrum 48K/128K) the ULA stretches every
memory cycle that addresses contended RAM - including the acknowledge's stack
push when SP points into 4000h-7FFFh, and the IM2 table read when I selects a
contended page. Upstream documented the gap ("Not fired for the raw
stack/vector accesses of Z80CpuInt/Z80CpuNmi"), and its own z80ex
differential listed it as an open item for contended hosts. Also, the memory
callbacks ran with `cpu->t` still at the start of the acknowledge, so a bus
tracer saw all acknowledge accesses at the wrong T.

**Example.** IM2 acknowledge starting at T=100, I=39h, bus vector FEh, SP=8000h,
a hook that returns 2 waits for every Read/Write cycle:

| cycle | kind reported to the hook | hook sees T | access (callback sees T) |
|---|---|---|---|
| M1 ack | - | - | vector FEh from `Z80CpuIntVectorFn` |
| M2 | `Z80CpuAccessWrite` 7FFFh | 107 | write PCH at 112 |
| M3 | `Z80CpuAccessWrite` 7FFEh | 112 | write PCL at 117 |
| M4 | `Z80CpuAccessRead` 39FEh | 117 | read table low at 122 |
| M5 | `Z80CpuAccessRead` 39FFh | 122 | read table high at 127 |

`Z80CpuInt()` returns 27 (19 + 4 x 2) and `Z80CpuTstates()` is 127. With the
hook returning 0 it returns 19, and the writes land at 110/113, the reads at
116/119.

```c
static int Contend(Z80CPU* cpu, uint16_t addr, Z80CpuAccessKind kind, void* ud)
{
    Machine* m = (Machine*)ud;
    if (kind == Z80CpuAccessRead || kind == Z80CpuAccessWrite)
        return m->IsContended(addr) ? m->UlaDelay(Z80CpuTstates(cpu)) : 0;
    return 0;
}
...
int t = Z80CpuInt(cpu);   // now includes the push/table contention
```

**Host impact in unreal-ng.** None today: the emulator keeps INT/NMI
acceptance host-side (`Z80::ProcessInterrupts` / `Z80::HandleINT`, see "How
the host uses this"), so it does not call `Z80CpuInt`/`Z80CpuNmi`. The
change makes those entry points usable for a later move of interrupt
acceptance into the engine without losing contention.

---

## 3. The halted CPU's M1 cycles are reported to the wait-state hook

**What.** The halted branch of `Step()` (`src/z80step.inc`) now runs its 4 T
quantum through a per-bus primitive `Z80HaltT(cpu, tact)`:

* callback and paged buses (`opcodes-callback.cpp`, `opcodes-paged.cpp`):
  fire the hook with `Z80CpuAccessM1` at `cpu->pc` (the HALT opcode's
  address), insert its waits, then add the 4 T;
* flat bus (`opcodes-flat.cpp`): just the 4 T, as before (the flat bus never
  fires the hook).

`cpu->t` is published after the quantum as before; `Z80CpuStep()` returns
`4 + waits`.

**Why.** A halted Z80 keeps executing internal NOPs - real M1 cycles with
refresh, on the bus, every 4 T. On a contended machine those M1 cycles are
contended like any other when PC is in contended memory (a HALT in
4000h-7FFFh on a 48K/128K, or in C000h-FFFFh with an odd RAM page paged in on
a 128K), which shifts the moment the CPU notices INT. Upstream documented this gap too ("the halted NOP's M1 cycle
is not reported"). Implementing it as a per-bus primitive keeps upstream's
design rule: zero bus-mode branches inside the shared step body.

**Example.** HALT at 5000h, callback bus, hook installed:

```
Z80CpuStep(cpu);   // executes HALT: M1 at 5000h (Z80CpuAccessM1), PC stays 5000h
Z80CpuStep(cpu);   // halted quantum: hook(5000h, Z80CpuAccessM1) at T=4, returns 4 + waits
```

**Host impact in unreal-ng.** None today: the emulator re-executes the HALT
opcode every step (its historical HALT model, see below), so the engine's
halted branch is not entered.

---

## 4. `Z80CpuRegHalted` register selector

**What.** New enum value `Z80CpuRegHalted` (appended before `Z80CpuRegCount`,
so existing selector values are unchanged). `Z80CpuGetReg` returns the HALT
latch as 0/1 (same as `Z80CpuHalted`); `Z80CpuSetReg` sets it (any non-zero
value = halted).

**Why.** The register API could read every piece of CPU state a host needs to
mirror or snapshot except that the HALT latch had no setter. A host that keeps
the CPU state in its own structures (snapshot load, time-travel restore,
debugger edits) must be able to put the latch back; the only alternative was
`Z80CpuReset`, which clears everything.

**Example.** Restoring a snapshot taken while the CPU sat in HALT:

```c
Z80CpuSetReg(cpu, Z80CpuRegPc, snap.pc);        // PC at the HALT opcode
Z80CpuSetReg(cpu, Z80CpuRegHalted, snap.halted); // resumes halted, burns 4 T/step
```

**Host impact in unreal-ng.** The wrapper clears the latch before every step
(through the bulk setter below, or this selector when nothing else changed)
so the engine always performs the HALT opcode fetch, as the emulator's
interpreter did.

---

## 5. Bulk register-file accessors

**What.** New public struct and two functions:

```c
typedef struct Z80CpuRegisters
{
    uint16_t af, bc, de, hl;
    uint16_t afAlt, bcAlt, deAlt, hlAlt;
    uint16_t ix, iy, pc, sp;
    uint16_t memptr;
    uint8_t i, r, im;
    uint8_t iff1, iff2;
    uint8_t q;
    uint8_t halted;
} Z80CpuRegisters;

void Z80CpuGetRegisters(const Z80CPU* cpu, Z80CpuRegisters* regs);
void Z80CpuSetRegisters(Z80CPU* cpu, const Z80CpuRegisters* regs);
```

Same value conventions as the per-register selectors: `r` = low 7 bits of the
refresh counter | R7; the setter masks `iff1`/`iff2` to bit 0 and `q` to bits
3/5, clamps `im` to 2 and treats any non-zero `halted` as set.

**Why.** A host that owns the canonical register state (debuggers and
snapshot code write it directly between instructions) must move the whole
file into the engine before each step and back after it. With
`Z80CpuSetReg`/`Z80CpuGetReg` that is about 45 out-of-line calls per
instruction, each ending in the same `switch` jump - one indirect branch
cycling through ~20 targets, which the branch predictor cannot learn. Measured
in unreal-ng: the per-register sync made CPU-bound tests about 3x slower; the
bulk pair brought that down to about 1.4x (the rest is the unavoidable copy -
see "Performance" below). The struct is a plain C aggregate with a fixed
order; it does not expose the internal (packed, union-based) layout.

**Example.**

```c
Z80CpuRegisters regs;
HostToRegs(&host, &regs);        // host's own register structure
Z80CpuSetRegisters(cpu, &regs);
Z80CpuStep(cpu);
Z80CpuGetRegisters(cpu, &regs);
RegsToHost(&regs, &host);
```

---

## 6. RETN notification hook

**What.** New callback type `Z80CpuRetnFn` and setter
`Z80CpuSetRetnFn(cpu, fn, userData)`; `Z80CPU` gets `retnCallback` /
`retnData` (zero-initialized, i.e. off). `ope_45` (RETN, also reached as the
aliases ED 55/65/75) fires the hook after it has restored IFF1 from IFF2,
popped PC and cleared the internal NMI-session flag - with `cpu->t` published
first, exactly the shape of the existing RETI hook in `ope_4D`.

**Why.** Upstream tracks the NMI service session internally
(`nmi_in_progress`, set by `Z80CpuNmi`, cleared by RETN) but exposes neither
the flag nor its end. Hosts need it: NMI-paged ROMs (Scorpion "magic button"
shadow monitor, Multiface-style interfaces) page in on NMI and must know when
the handler returns. RETI already had a device hook; RETN is the NMI-side
twin. A notification hook, rather than exposing the flag, lets the host keep
its own session state and react exactly at the instruction that ends it.

**Example.**

```c
static void OnRetn(Z80CPU* cpu, void* ud) { ((Machine*)ud)->EndNmiSession(); }
Z80CpuSetRetnFn(cpu, OnRetn, machine);
```

**Host impact in unreal-ng.** The wrapper wires it to `Z80::retn()`, which
clears `Z80State::nmi_in_progress` (read by the Scorpion port decoder) - the
same point where the former interpreter called `cpu->retn()`.

---

## Deliberately not changed

**`m1State` of the memory-read callback.** Upstream documents it as "non-zero
when the read is an opcode fetch", but it is non-zero for every
instruction-stream byte (opcode, prefix, operand, displacement, and the DD CB
d operand read). The emulator relies on exactly that: its `MemoryRead(addr,
isExecution)` expected `isExecution = true` for the same set of reads. The
host does not use it to find the real M1 fetch; it derives the M1 fetches from
the instruction structure (first byte; the byte after CB/ED/DD/FD; the byte
after the displacement of DD/FD CB). Changing `m1State` upstream (for
example 1 = M1, 2 = other instruction-stream byte) would be compatible with
this host.

**MEMPTR after a repeating INIR/INDR/OTIR/OTDR.** Upstream sets MEMPTR to the
jump-back address + 1 (PC+1) on the repeat path, like LDIR/CPIR, verified with
z80test's z80memptr. The emulator's former interpreter kept the BC-derived
value, which the old FUSE vectors (`testdata/z80/fuse/tests.expected`) also
expect. Kept as upstream (it is the more accurate behavior);
`FusePhase_Test.AllOpcodes` therefore reports MEMPTR mismatches for exactly
the four cases `edb2_1`, `edb3_1`, `edba_1`, `edbb_1` - flagged for a decision
on the test's expectation, not patched here.

---

## How the host uses this (core/src/emulator/cpu/z80.cpp)

`Z80` keeps its public shape; only the opcode execution inside `Z80Step()`
moved into the engine (`Z80::ExecuteEngineInstruction`, trampolines in
`struct Z80EngineBridge`):

* one `Z80CPU` per `Z80`, callback bus (never the flat/paged fast paths), so
  every access still goes through `MemIf`, the port decoder, the floating bus
  and `busTraceHook`;
* the `Z80Registers` fields stay the source of truth between instructions:
  loaded into the engine before the step (skipped when they still equal what
  the previous step stored) and stored back after it; Q, MEMPTR, the EI
  shadow (`eipos`) and HALT (`halted`, `haltpos`) are derived from the engine;
* ULA contention: the wait-state hook is installed only while the model has
  contention enabled, and returns `UlaContention` delays at the same T the
  interpreter evaluated them;
* HALT: the engine's latch is cleared before each step, so a halted CPU
  re-executes the HALT opcode (with its fetch) every step - the emulator's
  historical model;
* INT/NMI acceptance stays host-side (`ProcessInterrupts` / `HandleINT`),
  RETN is wired to `Z80::retn()`, RETI and the IM2 vector hook are unused.

## Performance

With the full register sync the engine-backed `Z80` runs CPU-bound workloads
about 1.3-1.4x slower than the former in-class interpreter (boot-heavy test
subset: ~6.3 s vs ~4.5 s; profile: register copies and bus trampolines, the
engine's own opcode work is a small share). The remaining cost is structural
- two copies of the register file per instruction - and goes away only when
the engine becomes the single owner of the register file (a follow-up, not a
library patch).

## Verification

* Standalone spike against these sources (callback bus, no emulator):
  FUSE vectors 1352/1356 (the 4 INIR-family MEMPTR cases above), plus checks
  for patches 2-6 (IM1 13 T and IM2 19+waits T timing and access T, NMI
  11+waits T, halted M1 hook, `Z80CpuRegHalted`, bulk accessors vs
  per-register accessors, RETN fires once and RETI does not fire it).
* Warning-clean with clang (`-Wall -Wextra -Werror`, the core's flags) and
  GCC 16 (`-Wall -Wextra -Werror`).
* Emulator suite (`core-tests`): all tests pass except
  `FusePhase_Test.AllOpcodes` (the four MEMPTR cases above).

## What changed between 0.1.0-poc and 0.2.0 (this copy), besides the six patches

* **`m1State` on DJNZ's displacement byte** (`opcodes-base.inc`, `op_10`):
  that read now passes `m1State = true` like every other instruction-stream
  byte, matching the semantics documented in `z80cpu.h` and in "Deliberately
  not changed" below (it was the one instruction-stream read that upstream
  had missed). No host-visible effect here: the wrapper never relied on
  `m1State` for M1 detection (see below), and DJNZ's `address`/`operand`
  fields were already left unfilled.
* **Per-bus HALT quantum placement** (`z80step.inc` `Z80_HALT_OUT_OF_LINE`):
  a pure performance tuning of where the halted-M1 hook call (patch 3) sits
  in each bus's `Step()`, chosen per bus from its effect on the hot path's
  register-saving prologue. No API or behavior change.
* **`Z80CpuSetRetnFn`** is now available (patch 6 was added after the first
  vendoring pass in this repo); the wrapper wires it to `Z80::retn()` (see
  below).
* **Upstream `LICENSE`** (MIT) was added; copied verbatim into this
  directory.

## Re-applying on a future upgrade

1. Copy the new upstream `z80lib/include/z80cpu.h`, all of `z80lib/src/`,
   and `LICENSE` over `core/src/3rdparty/unreal-z80/{include,src,LICENSE}`
   (file list in [version.txt](version.txt)); update the commit/version in
   [version.txt](version.txt).
2. No patch to apply - this copy carries no local modifications. Diff the
   new copy against the previous one to see what changed; if upstream ever
   *reverts* one of the six items above or changes the `m1State`/MEMPTR
   behavior discussed below, that needs a fresh look, not a mechanical patch
   re-apply.
3. Rebuild with zero warnings and run `core-tests` - in particular
   `FusePhase_Test.*`, `Z80TestVerification.*`, `Z80_Test.*`,
   `BusPhase_Test.*`, `IOPhase_Test.*`, `IntAcceptance_Test.*`,
   `IntPendingWrap_Test.*`, `NmiAcceptance_Test.*`, the video
   contention/timing suites (`core/tests/emulator/video/`) and the TTD
   suites (`core/tests/debugger/ttd/`).
