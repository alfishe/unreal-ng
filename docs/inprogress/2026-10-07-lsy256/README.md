# LSY256 (Orel BK-08): design folder

**Created:** 2026-10-07 · **Status:** design only, nothing built · see [TODO.md](TODO.md)

## What this is

The Orel BK-08 is a 256K Spectrum clone. Its memory extension is the "LSY" logic, credited to BarmaleyM in the
UnrealSpeccy-NedoPC source. unreal-ng already has the enum `MM_LSY256`, the model-table row `LSY256`
("Orel' BK-08 (LSY)", 256K), the `[ROM] LSY=` key, the ROM layout in `rom.cpp` and the ROM image
`data/rom/lsy256.rom`. It is **not creatable**: there is no port decoder case, no `data/configs/lsy256/` folder
and `emulatormanager_test.cpp` asserts `IsModelSupported(MM_LSY256) == false`.

This folder designs the missing part: a port decoder with the `#7B` latch, the window-0 modes, the BK-08 keyboard
matrix, and the integration points (snapshots, TTD, automation, Qt).

## The one thing to know about the sources

Every hardware fact here comes from **one source**: the NedoPC / TS-Labs Unreal Speccy (`zx-evo` repository), added
on 2014-06-02 in a commit titled "added Orel' BK-08 LSY256 model (stub)". Eleven other emulators were searched and
none has the model ([research-reference-consensus.md](research-reference-consensus.md)). No schematic, manual or
article was found in the local collection. So there is no consensus to take: the design follows Unreal, and every
point where Unreal looks incomplete or wrong is an open question in [TODO.md](TODO.md) with a recommendation.

## Documents

| File | Contents |
|:--|:--|
| [requirements.md](requirements.md) | goals, requirements with confidence, acceptance |
| [design.md](design.md) | hardware facts, how it fits unreal-ng, automation parity, phases |
| [tdd-plan.md](tdd-plan.md) | tests per phase, file names, cases |
| [roms.md](roms.md) | the ROM image, its four pages, checksums, provenance |
| [research-reference-consensus.md](research-reference-consensus.md) | what each emulator says (only one says anything), Unreal's code read line by line |
| [research-rom-analysis.md](research-rom-analysis.md) | what the ROM bytes show about `#7B`, the NMI, the RAM test, the keyboard tables |
| [TODO.md](TODO.md) | status, phase checklist, open questions with recommendations |

## Sources (upstream)

- Unreal Speccy NedoPC: <https://github.com/tslabs/zx-evo> (`pentevo/unreal/Unreal/`: `memory.cpp`, `io.cpp`,
  `emul.h`, `z80.cpp`, `config.cpp`, `vars.cpp`, `cfg/rom/lsy256.rom`); commit `05795207` (2014-06-02) added the model.
- Orel BK-08 fan site `http://orel.pp.ua/` is linked from zx-pk.ru threads; it was **not reachable** when this was
  written (DNS failure). It is the first place to look for a schematic.
