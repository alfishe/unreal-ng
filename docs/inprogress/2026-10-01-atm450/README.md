# ATM Turbo 2 v4.50 ("ATM450") — support program

**Created:** 2026-10-01 · **Status:** not started (design + research complete) · see [TODO.md](TODO.md)

## What this is

ATM450 (`MM_ATM450`, "ATM-Turbo v4.50") is the last ATM-family machine that the
reference emulators implement and we do not. It is **already ~70 % pre-wired** in
the codebase: the enum, the model-table row, timing, ROM plumbing, the `aFE/aFB`
latches, the video-mode detector, IDE fitment, TTD paging fields and several
automation cases all exist — only the port decoder, its factory case, a config
folder and tests are missing. The model is **not creatable** today solely because
`GetPortDecoderForModel` has no `MM_ATM450` case
([core/src/emulator/ports/portdecoder.cpp:111](../../../core/src/emulator/ports/portdecoder.cpp)).

Every other emulator that has ATM450 (UnrealSpeccy, ZXMAK2, kozynax's ZXMAK2 port)
shares one implementation shape: a `pFDFD` ROM/RAM-extension latch written through
the `#FDFD` port group, two address-bus latches (`aFE` on `#xxFE` writes, `aFB` on
`#xxFB`/`#xx7B` reads) that replace the 710/ATM3 `#FF77`/`#xxFF7` register file,
and video modes selected by **address bits A13-A14** of the `#FE` write instead of
a port latch. "PentEvo" in ZXMAK2/Xpeccy is the same hardware family position as
our ATM3 (baseconf) — no extra machine hides behind it. Full survey and the
per-variant comparison: [cross-mapping.md](cross-mapping.md).

## Deliverables

1. `core/src/emulator/ports/models/portdecoder_atm450.{h,cpp}` — inherits
   `PortDecoder_ATM710` (same pattern as `PortDecoder_ATM3`), overriding the
   ATM450 paging/decode arms. What to implement: [requirements.md](requirements.md)
   R2-R5; behavior sources mapped line-by-line in [cross-mapping.md](cross-mapping.md).
2. Factory + `IsModelSupported` + port-trace/port-map rows in
   `core/src/emulator/ports/portdecoder.cpp`; flip the "no factory case" expectation
   in `core/tests/emulator/emulatormanager_test.cpp:575`.
3. `data/configs/atm450/unreal.ini` (from `data/configs/atm710/unreal.ini`;
   the ROM image `data/rom/atm1.rom` is already shipped, 64 KiB = 4 × 16 KiB).
4. Tests, written first where practical: [tdd-plan.md](tdd-plan.md).
5. UI (`unreal-qt/src/menumanager.cpp` supportedModels) + docs
   (`.recipe/_common/machines.md`, `AGENTS.md` model list).

## Effort

One focused sprint, clearly below the ATM3 program: no new video renderer (the ATM
family `ScreenAtm`/`AtmVideoMapper` are shared and `DetectModeATM1` is already
tested green), no memory overlays (no 14 MHz DRAM waits), no CMOS, no SD. The
novelty is one decoder (~600-900 lines against 889 in `portdecoder_atm710.cpp`,
part inherited) plus its tests. Turbo is **out of scope** (R8): the 4.50 board has
no software turbo port — references ship it as 3.5 MHz (UnrealSpeccy) or as a
separate "[turbo]" machine variant (ZXMAK2).

## How to start

Work in a dedicated branch (suggested: `atm450`, off `master`). The TDD plan is
ordered so that each phase leaves the tree green: registry → decoder unit tests
(CUT pattern) → boot tests → TTD/automation → UI/docs.

Reference trees live outside the repo under `emulators/github/` next to the project
(same convention as [2026-09-27-tsconf/references.md](../2026-09-27-tsconf/references.md));
all external line numbers below were verified 2026-10-01 and may drift.
