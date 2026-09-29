# TODO — ZX-Evo TSConf machine support

**Status marker:** not started (implementation). Design complete — review
round 1 applied 2026-09-27; ready for phase 0.
Starts after the shared infrastructure and the move of the existing machines onto
it (PLAN rationale 6); the interrupt source, write intercept and step hook of
phase 0 are built (PLAN #60(a) + INF-5, [implementation-plan.md](implementation-plan.md) phase 0).

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
- [x] Prerequisite: shared CMOS (PLAN #60(c), `04383910`): `EvoAvr` now derives
  from the shared `Ds12887` chip (TTD `PeripheralId::Ds12887` = 18), so the Gluk
  CMOS is reused as is; TSConf adds only the `#EFF7`/DOS/vdos gating
- [x] Shared IDE core on master (PLAN #13a / media manager M6, `f5fc5f05`):
  `IdeAdapter` with the Nemo (A8, Evo) scheme, `[HDD]` config, TTD
  `AtaChannel` = 17, every surface; ATM3 already uses it
- [x] D2 decided 2026-09-29: Nemo IDE **emulated** in phase 6 on that core -
  decode = scheme `NEMO-DIVIDE` (bit-identical to BaseConf), DMA 0x3/0xB in
  16-bit words, the TSConf-only CPU stall emulated but off by default
  (`[HDD] IdeStall=0`); full description in hardware-spec §8.3, design in
  technical-design §3.11, tests IDE-1..5 / DMA-15 / BOOT-4. Already landed
  (`762d813e`): `[HDD] Scheme=NEMO-DIVIDE` in the ts-conf config and
  `IdeAdapter::DmaReadWord` / `DmaWriteWord` with their tests

- [x] Prerequisite: PLAN #60(a) interrupt source (`IInterruptSource` + `OnReti`)
  and write intercept (a write-only `HostBusOverlay`, several overlays chained),
  plus INF-5 (`IMachineStepHook`, ungated per-step engine hook) - branch
  `tsconf-infra` (2026-09-29); technical-design §3.4, §3.5 item 2, §3.8

- [x] Prerequisite: PLAN #60(d) memory wait states (`MemoryWaitOverlay`, for
  phase 8's 14 MHz cache-miss waits; port waits stay in the decoder) and
  #60(e) the `Screen` subclass per model family (`VideoController::CreateScreen`
  builds `ScreenTSConf : ScreenZX` for `MM_TSL`) - branch `tsconf-infra-2`
  (2026-09-29); technical-design §3.9

## Remaining

- Implementation phases 0-8 per [implementation-plan.md](implementation-plan.md).
- Prerequisites: PLAN #60 (built except (f), the raw PC floppy loader, which
  TSConf does not need; the linear turbo ratio (b) is postponed - only the
  Sprinter needs it, TSConf uses `hw_turbo_shift`); the unified media manager (#58, M1/M2/M4 on master)
  for the SD part of phase 6: TSConf only registers its `sd.zc` slot
  ([integration-tsconf-sd.md](../2026-09-28-storage-manager/integration-tsconf-sd.md)); control
  from the GUI and every automation surface comes from the media verbs
  ([media-control-design.md](../2026-09-28-storage-manager/media-control-design.md), #58 M4), not
  from a TSConf-specific SD API; #40 V1 and #42
  (`IVideoMapper`) before phase 7.
- Shared with the ATM3 completion program (PLAN #55, [implementation-plan.md](../2026-09-15-atm-baseconf-highres-ports/implementation-plan.md) §2): M1 hook, write intercept, `ZControllerSpi`, SD on `IBlockDevice`, `EvoAvr`; the SD card, host folders (`HostFolderFat`) and media control through the media manager (PLAN #58, [storage-manager](../2026-09-28-storage-manager/technical-design.md)). ATM3 moves to the official `zxevo_fe.rom` (pages 0-3 empty), so TSConf needs its own ROM file with TS-BIOS in pages 0-3.
- Open user decisions: none. D1-D7 in technical-design §3.2 record the
  decisions taken (D2 changed on 2026-09-29: Nemo IDE emulated).
