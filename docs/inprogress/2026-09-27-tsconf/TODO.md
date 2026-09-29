# TODO — ZX-Evo TSConf machine support

**Status marker:** not started (implementation). Design complete — review
round 1 applied 2026-09-27; ready for phase 0.
Starts after the shared infrastructure and the move of the existing machines onto
it (PLAN rationale 6); the interrupt source and write intercept of phase 0 are
built by PLAN #60 ([implementation-plan.md](implementation-plan.md) phase 0).

## Goal

Add the ZX-Evo **TS-Conf** configuration as a first-class, creatable machine
with full debugging (unreal-qt debugger, memory views, breakpoints,
disassembly, TSConf-specific docks), TTD capture/restore of all TSConf state,
and parity across all automation modules (WebAPI, MCP, CLI, Lua, Python).

Scope confirmed with the user on 2026-09-27, and how the design honors it:

- Storage: SD card (image **and** host folder via virtual FAT) + Beta-128 FDD — yes
- Sound: "AY + GS + Covox/Soundrive (full set)" — AY + beeper/Covox DAC + GS;
  **Soundrive is not part of the TS-Conf hardware** and is not emulated
  (technical-design D4)
- Machine short name `TSCONF` — accepted as an alias; the canonical model key
  stays `TSL`, which the code, ini, API and AGENTS.md already use (D3)

## Progress

- [x] Reference survey ([references.md](references.md)); premise corrected
  (BaseConf and `unreal_fix/0.39.0` are not TS-Conf)
- [x] Hardware contract ([hardware-spec.md](hardware-spec.md)) — v2, fully
  re-verified against the Verilog, TSconf.xls, the ROM images and all
  reference emulators (~40 corrections, listed in its §13)
- [x] Technical design ([technical-design.md](technical-design.md)) — v1.0,
  every code citation re-verified; new infrastructure made explicit
- [x] Implementation plan with test-first work lists
  ([implementation-plan.md](implementation-plan.md))
- [x] ROM page layout resolved from `data/rom/zxevo.rom` (0 SYS, 1 DOS, 2 128, 3 48)
- [x] Prerequisite: TTD RAM page 255 (vdos's page) is an ordinary page —
  `3a6eabc6` on master, 2026-09-27 (PLAN #40 V0 item, tested on ATM3)
- [x] Prerequisite: unique `PeripheralId` table (PLAN #40 V0) — done on master;
  TSConf appends id 16 (13/14 went to the +3, 15 to `EvoSdCard`, which TSConf
  reuses for its SD card)
- [x] Prerequisite: TTD time at the model's top clock (B4, master 2026-09-28):
  TSConf returns `TtdClockUnits() = 4`
- [x] Prerequisite: unified media manager M1/M2/M4 merged to master (#58):
  `HostFolderFat`, slots, `[MEDIA]` config, media verbs on every surface
- [x] Prerequisite: shared SD card - `SdCardSpi` over `IBlockDevice` and
  `ZControllerSpi` on master (#55 E5, `ed703577`); M1 hook (#55 E3) and
  `EvoAvr` (#55 E2a) on master too

## Remaining

- Implementation phases 0-8 per [implementation-plan.md](implementation-plan.md).
- Prerequisites: PLAN #60 (interrupt source + write intercept, linear turbo
  ratio, per-family `Screen` subclass, shared CMOS, wait-state hook) before
  phase 0 per PLAN rationale 6; the unified media manager (#58, M1/M2/M4 on master)
  for the SD part of phase 6: TSConf only registers its `sd.zc` slot
  ([integration-tsconf-sd.md](../2026-09-28-storage-manager/integration-tsconf-sd.md)); control
  from the GUI and every automation surface comes from the media verbs
  ([media-control-design.md](../2026-09-28-storage-manager/media-control-design.md), #58 M4), not
  from a TSConf-specific SD API; the shared IDE (#13a) only if D2 changes; #40 V1 and #42
  (`IVideoMapper`) before phase 7.
- Shared with the ATM3 completion program (PLAN #55, [implementation-plan.md](../2026-09-15-atm-baseconf-highres-ports/implementation-plan.md) §2): M1 hook, write intercept, `ZControllerSpi`, SD on `IBlockDevice`, `EvoAvr`; the SD card, host folders (`HostFolderFat`) and media control through the media manager (PLAN #58, [storage-manager](../2026-09-28-storage-manager/technical-design.md)). ATM3 moves to the official `zxevo_fe.rom` (pages 0-3 empty), so TSConf needs its own ROM file with TS-BIOS in pages 0-3.
- Open user decisions: none blocking. D1-D7 in technical-design §3.2 record the
  defaults taken; revisit D2 (Nemo IDE) together with PLAN #13a.
