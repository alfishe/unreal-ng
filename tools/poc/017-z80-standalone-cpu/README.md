# 017 - Standalone Z80 CPU Library Extraction

Extract the emulator core's Z80 CPU ([core/src/emulator/cpu](../../../core/src/emulator/cpu))
into a self-contained, z80ex-style library with **full undocumented-behavior support**, no
integration with the parent build, a separate demo execution loop, a verification suite and
benchmarks.

Structural reference: [z80ex](../../../scratch/z80ex-dl/z80ex.h) (opaque context, host bus
callbacks, step-per-instruction, host-driven INT/NMI, register accessors).

## Results

- **All ZEX exercisers pass.** zexdoc / zexall / zexbit / zexfix (Kevin Horton family,
  `data/testsoft/ZEXALL`) run to `Tests complete` with every test group `OK` - that is the
  definitive verification of the *undocumented* behaviors (zexdoc covers undocumented ops,
  zexbit the MEMPTR-derived X/Y flags of `BIT n,(HL)/(IX+d)`, zexfix flag quirks).
- **~1.0-2.8 billion T-states/s** on flat memory (Apple silicon, clang -O3), i.e. ~300-790x
  real-time for a 3.5 MHz guest; **1.00-1.14x z80ex aggregate** on identical callback buses
  (workload-dependent: ~1.8x on block ops, 0.79x on scalar memory mixes - see
  [benchmark-report.md](benchmark-report.md)).
- 14/14 unit tests pass (reset state, timings, flags, MEMPTR/Q, SLL, IN (C), OUT (C),0,
  IXH/IXL/IYH/IYL, LDIR, EI-delay, IM1/IM2, NMI, HALT, RETI hook).

## Layout

```
017-z80-standalone-cpu/
├── CMakeLists.txt              # fully standalone build (no parent integration)
├── z80lib/                     # the extracted CPU library
│   ├── include/z80cpu.h        # public C API (PascalCase per project convention)
│   └── src/
│       ├── z80cpu-internal.h   # Z80CPU struct (core Z80Registers layout) + micro-ops
│       ├── z80cpu.cpp          # create/reset/step/int/nmi/reg access, bus wiring
│       ├── z80cpu-opcodes.h    # opcode declarations (from op_noprefix.h)
│       ├── opcodes-base.cpp    # from op_noprefix.cpp (main dispatch table)
│       ├── opcodes-cb.cpp      # from op_cb.cpp
│       ├── opcodes-dd.cpp      # from op_dd.cpp (IX)
│       ├── opcodes-fd.cpp      # from op_fd.cpp (IY)
│       ├── opcodes-ed.cpp      # from op_ed.cpp
│       ├── opcodes-xxcb.cpp    # from op_ddcb.cpp (shared DD/FD CB dispatch)
│       ├── z80daa.cpp          # from daa_tabs.cpp
│       ├── z80tables.cpp       # computed flag tables (from cputables.cpp)
│       └── z80tables-data.inc  # static flag tables (from cputables.h)
├── demo/demo-main.cpp          # execution loop demo (flat + callback bus modes)
├── tests/
│   ├── hostmachine.{h,cpp}     # flat 64K host: RST 0x10 soft-ROM, TAP loader, ZEX runner
│   ├── minitest.h              # dependency-free test framework
│   └── test-main.cpp           # unit tests + ZEXALL-family runs
└── benchmarks/
    ├── bench-main.cpp          # flat vs callback bus, 4 workloads + halt microbench
    ├── bench-z80ex.cpp         # head-to-head vs the z80ex reference (optional target)
    └── benchmark-report.md     # dedicated analysis: who is faster, and why
```

Ported opcode units intentionally keep the core's internal naming (`op_XX`/`ope_XX`/`opl_XX`,
`*_f` tables) so they remain diffable against `core/src/emulator/cpu`.

## Build & Run

```bash
cmake -S . -B build -G Ninja
ninja -C build
./build/z80demo            # demo execution loop (both bus modes)
./build/z80tests           # unit tests + full ZEXALL-family verification (~1 min)
./build/z80tests --quick   # unit tests only
./build/z80bench           # flat vs callback benchmarks
./build/z80bench-z80ex     # head-to-head vs z80ex (built when scratch/z80ex-dl exists)
```

The library has no dependencies beyond C++17; the z80ex comparison additionally uses the
reference sources in `scratch/z80ex-dl` (GPL v2, unity-built from z80ex.c).

## API Shape (vs z80ex)

| z80ex                          | this library                        | notes |
|--------------------------------|-------------------------------------|-------|
| `z80ex_create(mrcb, mwcb, ...)`| `Z80CpuCreate` + `Z80CpuSetMemoryBus`/`SetPortBus` | callbacks optional, null-safe |
| `z80ex_step`                   | `Z80CpuStep`                        | returns T-states; HALT burns 4 T |
| `z80ex_int` / `z80ex_nmi`      | `Z80CpuInt` / `Z80CpuNmi`           | 13/19 T and 11 T, host-driven |
| `z80ex_set_reti_callback`      | `Z80CpuSetRetiFn`                   | fired after ED 4D |
| `z80ex_get_reg`/`set_reg`      | `Z80CpuGetReg`/`Z80CpuSetReg`       | + `Memptr` (WZ) and `Q` exposure |
| -                              | `Z80CpuAttachMemory`                | flat-64K fast path, no callbacks |
| per-T-state callback           | -                                   | contention done host-side via `Z80CpuTstates()` |
| (prefixes are separate steps)  | prefixes handled inside one step    | instruction = one `Z80CpuStep` |

## Undocumented Behavior Coverage

| Feature | Mechanism | Verified by |
|---|---|---|
| MEMPTR/WZ (incl. `LD (nn),A` leaves WZ.H=A, `IN/OUT (C)` set WZ=BC+1) | `memptr` in Z80CPU, exposed as `Z80CpuRegMemptr` | zexbit, unit MemptrAndQ |
| X/Y flags (F3/F5) from WZ.H for `BIT n,(HL)`/`BIT n,(IX/IY+d)` | `bitmem()` reads `memh` | zexbit |
| Q register, Zilog SCF/CCF XCF flavor `(A \| (F & ~Q)) & 0x28` | `q` field, updated in `Z80CpuStep` + in-op for SCF/CCF | zexdoc `<daa,cpl,scf,ccf>`, unit MemptrAndQ |
| IXH/IXL/IYH/IYL (incl. `LD`/`INC`/`DEC`/ALU use) | union layout + `directRegisters` | zexdoc groups |
| SLL (`CB 30-37`), implemented via the rl1 table | `opl_30..37` | unit UndocumentedSll, zexdoc |
| `IN (C)` (ED 70) affects flags, no store | `ope_70` | unit UndocumentedInOutC |
| `OUT (C),0` (ED 71) writes configurable value (NMOS 0 / CMOS 0xFF) | `Z80CpuSetOutC0Value` | unit UndocumentedInOutC |
| `DD CB`/`FD CB` with undoc `opN >= 0x30` and the no-destination trash write | `opcodes-xxcb.cpp` | zexdoc `shf/rot (<ix,iy>+1)` |
| EI delay (INT accepted only after the instruction following EI) | `intSuppress`, ported from the core's `t != eipos` check | unit InterruptAcceptance |

## Verification Results

Unit tests: 14/14 (see `tests/test-main.cpp`).

| Tape | Result | T-states | Instructions |
|---|---|---|---|
| zexdoc.tap (undocumented ops) | PASS - all 66 groups OK, `Tests complete` | 3840M | 5767.9M |
| zexall.tap | PASS - all groups OK | 3840M | 5767.9M |
| zexbit.tap (BIT X/Y flags) | PASS - all groups OK | 1332M | 163.7M |
| zexfix.tap (flag quirks) | PASS - all groups OK | 3840M | 5767.9M |

The ZEX programs are hosted without a Spectrum ROM: `HostMachine::StepCapture` soft-ROMs
`RST 0x10` (char capture), `CALL 0x1601` (open channel) and `RST 0x08` (error report).

## Benchmarks

Full analysis with per-workload explanations: [benchmarks/benchmark-report.md](benchmarks/benchmark-report.md).
Numbers below: best-of-3, T-states/s, ranges across repeated runs (Apple M1 Ultra, Release/-O3;
harness fixes documented in the report supersede earlier numbers):

`z80bench` (extracted core, both bus modes):

| Workload | Flat memory | Callback bus |
|---|---|---|
| mixed ALU+(HL)+16bit+stack loop | 1346-1372M | 1099-1106M |
| LDIR 16K copy | 2765-2783M | 1825-1881M |
| ALU chain + DJNZ x256 | 1050-1052M | 1144-1149M |
| NOP sled (dispatch probe) | 999-1005M | 860-865M |
| **aggregate** | **1540-1553M** | **1232-1250M** |
| halt-step microbench | 2.4 ns/step (411M steps/s) | |

`z80bench-z80ex` (identical guest bytes, both cores on callback buses, 8 process runs):

| Workload | z80ex | poc017 | ratio |
|---|---|---|---|
| mixed | 1373-1391M | 1091-1106M | 0.79x (z80ex wins) |
| ldir 16K | 1002-1068M* | 1861-1881M | ~1.8x (poc017 wins) |
| arith | 1124-1138M | 1140-1167M | 1.01-1.03x (parity) |
| nops | 798-811M | 855-864M | 1.06-1.07x (poc017 wins) |
| **aggregate** | **1083-1093M*** | **1242-1245M** | **1.00-1.14x** |

\* z80ex's LDIR rate is bimodal across process launches (one run of 8 hit 1669M T/s);
the extracted core is stable (<2% spread) in every run.

Takeaway: neither core is universally faster. z80ex wins scalar callback-memory code by ~26%
(our `rd`/`wd` pay a runtime bus-mode branch per access; flat-mode mixed matches z80ex, proving
it). The extracted core wins prefixed/block instructions ~1.8x (prefix consumed inside one step;
z80ex pays two dispatches per prefixed instruction) and dispatch overhead slightly. The
flat-memory fast path adds another ~24% aggregate and requires zero host callbacks.

## Porting Notes (deltas vs the core)

- **Timing**: plain T-states only; the core's `rate`/`tt` 256x scaling and contention hooks
  are dropped. `cputact(a)` becomes `cpu->t += a`.
- **EI delay**: the core checks `cpu.t != cpu.eipos` at interrupt time; here `Z80CpuStep`
  sets `intSuppress` when the executed opcode byte was EI (0xFB), which is equivalent for
  instruction-boundary hosts.
- **Interrupts**: INT (13 T IM0/IM1, 19 T IM2) and NMI (11 T) are host-driven calls, ports
  of the core's `ProcessInterrupts` branches, with raw (pre-timed) stack/vector accesses.
- **Tables**: static tables are byte-identical (`z80tables-data.inc`); the big computed
  tables (`adc_f`/`sbc_f` 2x64K, `log_f`, `cp_f`, `cpf8b`, rotations) are built once by
  `Z80TablesInit()` (ports of the `CPUTables::Make*` generators).
- **Bus**: reads/writes go through inline `rd`/`wd` (3 T each) that use the flat pointer
  when attached, else callbacks; `m1_cycle` adds the +1 T opcode-fetch tick and R increment.

## Conclusions

1. The core's CPU is cleanly extractable: opcode semantics moved 1:1 (mechanical copy +
   rename), with only ~350 lines of new glue (context, step, interrupts, register API).
2. Full-fidelity undocumented support survives extraction and is *provable* without the
   emulator: the whole Kevin Horton exerciser family passes on a 64K flat RAM host.
3. Performance is z80ex-class out of the box (1.00-1.14x aggregate on callbacks, 2.78G T/s peak
   flat). For the core itself, the flat-memory fast path is the most interesting import
   candidate (zero-callback execution when the guest page map is flat for a frame span).
