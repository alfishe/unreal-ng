# Model-specific state out of `EmulatorState`: goals and requirements

| | |
|---|---|
| **Date** | 2026-10-07 |
| **Status** | Implemented (results: [tdd.md §6](tdd.md#6-results-2026-10-07)) |
| **Branch** | `refactor/model-state` (worktree `scratch/wt-model-state`) |
| **Design and test plan** | [tdd.md](tdd.md) |

## 1. Problem

`EmulatorState` (`core/src/emulator/platform.h`) is one flat struct of about 300 lines. Next to the
fields every machine uses (counters, clock, `p7FFD`, `pFE`, border, tape) it carries the latches
and RAM of single hardware families, declared in no particular order:

- ATM Turbo 1 / 2+ / 3 and ZX-Evo BaseConf: `aFE`, `aFB`, `pFFF7[8]`, `aFF77`, the 16-cell
  palette, the 2 KB text font RAM, `atmMemSwapped`;
- ATM3 / ZX-Evo BaseConf only: `pBD` (with the `pBDb` byte view), `pBE`, `pBF`, `evoFddMask`,
  `evoTrdemu`, `evoVgSys`, `evoWrProt`, `evoTurboPending`, and two NMI bits packed into a bitfield
  next to a model-neutral one;
- Scorpion ZS-256: `scorpion_turbo`, `p7EFD`, `profrom_bank`, `scorpionDosTrigger`, the SMUC
  latches `pFFBA` / `p7FBA`;
- Profi: `profi_turbo_switch`, `profi_cpm_switch`, `profiPalette[16]`.

It also carries fields that nothing reads or writes any more: `pXXXX`, the GMX group (`p78FD`,
`p7AFD`, `p7CFD`, `gmx_config`, `gmx_magic_shift`), Quorum `p00` / `p80FD`, `pLSY256`, `pVD`,
`vdbase` (only under the never-defined `MOD_VID_VD`), `res1`, `res2`, `p0F` / `p1F` / `p4F` / `p5F`,
and `struct NVRAM nvram` (its `MemoryWrite` has no definition).

Results: a reader of a port decoder cannot tell which fields belong to the machine; names carry
ad-hoc prefixes (`evo*`, `atm*`, `profi_*`, `scorpion_*`) instead of a type; dead fields look live.

## 2. Goals

| ID | Goal |
|----|------|
| G-1 | Every model-specific field lives in a typed struct of its hardware family, declared in its own header under `core/src/emulator/platforms/<family>/`. |
| G-2 | `EmulatorState` keeps only model-neutral fields plus one member per family (`atm`, `evo`, `scorpion`, `profi`). |
| G-3 | Dead fields are removed. |
| G-4 | **No behavior change.** Same emulation, same TTD files, same snapshots, same WebAPI / CLI output. |

## 3. Decision: embedded structs, not `void* modelState`

The first proposal was an opaque `void* modelState` owned by the port decoder. Rejected (owner
decision, 2026-10-07) because of what the code does with these fields:

| Fact in the code | Consequence for `void*` |
|---|---|
| `EmulatorContext` resets with `emulatorState = EmulatorState{}` (`emulatorcontext.cpp`). | The pointer is overwritten: leak, or a dangling pointer if the decoder keeps a copy. |
| The fields are read by memory (`memory.cpp`, `scorpionmemory.cpp`, `scorpionromwindow.cpp`), video (`screen.cpp`, `screenatm.cpp`, Alco, Profi, `videomapservice.cpp`), `core.cpp`, the TTD serializers and about 40 test files, not only by the decoder. | Every reader would need a `static_cast` to a type it must know anyway, or a getter on the decoder. |
| `aFF77`, `pFFF7`, the palette and the font are shared by three decoders (ATM 4.50, ATM 7.10, ATM3 / ZX-Evo) and the Alco video. | There is no single owning decoder. |

Embedded by value, a family struct is a plain member: reset, copy and the TTD capture / restore
work as now, the hot path costs the same (fixed offset), and the type documents ownership.
TS-Conf already keeps its state outside `EmulatorState` (`TsConfState` in `PortDecoder_TSConf`);
that stays as is. Its fields are read only through the decoder, which is not the case here.

## 4. Requirements

### Functional

| ID | Requirement |
|----|-------------|
| FR-1 | New headers: `platforms/atm/atmstate.h` (`AtmState`), `platforms/zxevo/evostate.h` (`EvoState`), `platforms/scorpion/scorpionstate.h` (`ScorpionState`), `platforms/profi/profistate.h` (`ProfiState`). `AtmState::InitFont` is defined in `platforms/atm/atmstate.cpp`. |
| FR-2 | `EmulatorState` gets `AtmState atm; EvoState evo; ScorpionState scorpion; ProfiState profi;` and loses every field listed in §1. |
| FR-3 | Field names drop the family prefix the struct now carries (`atmPalette` → `atm.palette`, `evoTrdemu` → `evo.trdemu`, `scorpion_turbo` → `scorpion.turbo`, `profi_cpm_switch` → `profi.cpmSwitch`, ...). Register-named fields keep their names (`atm.aFF77`, `evo.pBF`, `scorpion.p7EFD`). Full map: [tdd.md §2](tdd.md#2-field-map). |
| FR-4 | `InitAtmPalette()` / `InitAtmFont()` become `AtmState::InitPalette()` / `AtmState::InitFont()`, still called once from the `EmulatorContext` constructor. |
| FR-5 | `nmiAtIntStartPending` (read by `Z80::ProcessInterrupts` on every model) stays in `EmulatorState`, as a plain `bool`. |
| FR-6 | `wd_shadow`, `comp_pal`, `ulaplus_*`, `pDFFD`, `p1FFD`, `pFDFD`, `pFF77`, `pEFF7` stay: they are used by several families or by the generic TTD chipset blob (out of scope, §5). |
| FR-7 | All readers and writers (core, TTD, tests) use the new paths. No compatibility aliases (`#define`, reference members): a forgotten use must be a compile error. |

### Non-functional

| ID | Requirement |
|----|-------------|
| NFR-1 | TTD formats unchanged: no change to `ttd.ksy`, to any `TTD*Blob` / `TTDChipsetState` struct or to their `static_assert`s. Only the lines that copy between a blob and `EmulatorState` change. |
| NFR-2 | `EmulatorState` stays trivially copyable (guarded by a `static_assert`), and so are the four family structs. No pointers in them. |
| NFR-3 | No hot-path code change: the moved fields are reached through `_state->family.field`, a fixed offset like today. The Z80 / memory access loop does not read any moved field per instruction (checked: they are read in port handlers, paging updates, video mode / palette lookups, frame boundaries). No A/B benchmark needed; see [tdd.md §5](tdd.md#5-performance). |
| NFR-4 | Zero warnings: clang (macOS full build including Qt), gcc:16 `-O3` with the CI flags on every changed `.cpp`. |
| NFR-5 | The full `core-tests` suite gives the same result set as on master before the change. |

## 5. Out of scope

- Moving multi-family latches (`pDFFD`, `p1FFD`, `pFDFD`, `pFF77`, `pEFF7`) or device state (ULA+,
  `wd_shadow`, `comp_pal`) out of `EmulatorState`.
- Moving TS-Conf, Sprinter, Next or any state that already lives in a decoder or device.
- Changing ownership (decoder-owned state), getters, or the TTD peripheral blobs.
