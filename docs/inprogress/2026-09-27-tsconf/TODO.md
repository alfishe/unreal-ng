# TODO — ZX-Evo TSConf machine support

**Status marker:** in progress - phases 0-7 done (2026-09-30): `TSL` is
creatable, in the Qt Machine menu, boots TS-BIOS, TR-DOS and Wild Commander
from SD, runs SPG programs (opened on any machine: it switches to TS-Conf),
TTD-complete; open: BOOT-4 / IDE-5, DBG-3 + docks (model-first debugger);
next: speed optimizations TS-O1..O3 and phase 8 (timing realism).
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
  `3a6eabc6` on master, 2026-09-27 (PLAN #40 Phase 0, Step 1 item, tested on ATM3)
- [x] Prerequisite: unique `PeripheralId` table (PLAN #40 Phase 0, Step 1) — done on master;
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
  16-bit words, the TSConf-only CPU stall emulated, on by default since
  2026-10-05 (`[HDD] IdeStall=1`, as the RTL); full description in hardware-spec §8.3, design in
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

- [x] Phase 0 remainder (2026-09-29, branch `tsconf-isolation`): INF-6/7 the
  TSConf half-port removed from shared code + `tsconfisolation_test`; INF-8
  `TSCONF` alias; INF-9 `MM_TSL` frame geometry; INF-10 `PeripheralId::TsConfPaging = 16`
  (+ `ttd.ksy`, id-table test). **Phase 0 complete.**

- [x] Phases 1-2 and video v1 (2026-09-30, branch `tsconf-phase1`): decoder,
  memory, cache, FM window, DOS trap, CMOS gating, TTD blob; interrupt
  controller; ZX / 16C / 256C / TXT rendering. TS-BIOS Setup and TR-DOS boot
  on `data/rom/zxevo.rom` (BOOT-1/2). Status and deviations: [implementation-plan.md](implementation-plan.md) phases 1-3

## Remaining

- [x] Phase 3 rest (2026-09-30, branch `tsconf-phase3`): line engine (latches,
  row counter, line table), frame goldens, the classic machines' pictures pinned
  (`ScreenZXFrames_Test`). The per-line DRAM budget moves to phases 4-5.
- [x] Phase 4 TSU (2026-09-30, branch `tsconf-phase4`): tiles, sprites, layer
  order, prefetch ring, mixing; BENCH-1 measured (TSU off 1.9x a ZX frame vs
  the 1.1x target: speed backlog TS-O1…O3 in the implementation plan)
- [x] Phase 5 DMA (2026-09-30, branch `tsconf-phase5`): every task of the
  standard build, the per-line DRAM budget (video / TSU / CPU reads / DMA),
  TSU starvation
- [x] Phase 6 (2026-09-30, branch `tsconf-phase6`): SD card (`sd.zc`), vdos,
  Nemo IDE + stall, SPG v1.0 / v1.1 with the MegaLZ / Hrust depackers
- [x] Phase 7 (2026-09-30, branches `tsconf-phase7`, `tsconf-phase7b`): the
  state report on every surface, the shared sound DAC, port-trace names, screen
  mode names, MCP `unreal://machine/tsconf`, SPG opening on any machine (one
  rule on every surface and in Qt), the Qt menu entry, TTD-5 fixture (found and
  fixed DMA writes missing from TTD dirty pages), BOOT-3 (Wild Commander from SD)
- [x] Review of the plan's "Open" notes (2026-09-30): INT-6, CLK-2, ENG-1
  tested; INT-8, TSU-8, DMA-15 were done; MRG-1 and the paging latches are
  covered by MEM / P7F and `/state/tsconf`
- [x] BOOT-4, IDE-5 (2026-09-30, branch `tsconf-ide`); the PS/2 keyboard
  (the AVR as the PS/2 sink, TTD `EvoPs2`) - Wild Commander reads keys only
  there; notes in [boot-and-storage-notes.md](boot-and-storage-notes.md)
- [x] `TsConfVideoMapper` for #42 (2026-09-30, branch `tsconf-videomap`):
  the graphics layer of every mode on `/video/*` and every automation surface
- [x] The TSU layer in the video mapper (2026-09-30, branch `tsconf-tsumap`)
- [x] TSU objects and CRAM for debug views on every surface (`/state/tsconf/tsu`,
  `state tsconf tsu`, `tsconf_tsu()`, MCP aspect `tsconf_tsu`; DBG-4) - with the
  video mapper this is what a TSU / palette debug view integrates
- [x] TSU-6 (2026-09-30, branch `tsconf-tsu6`): the TSU draws line L at
  `ts_start` of line L - 1 with its latches
- [x] TSU-9 (2026-10-05, RTL audit item 13): the objects of a busy pass after
  `line_start` of L take L's latches; tile count per layer and the prefetch's extra
  DRAM cycle as the Verilog (`rtl-sim` `tsulatch`, test TSU9)
- [x] VDAC builds (2026-09-30, branch `tsconf-vdac`): `[MISC] TS_VDAC` /
  `TS_VDAC2` set STATUS VDAC_VER, the palette curve and BLT2; default NONE
- [x] TIM-5 (2026-09-30, branch `tsconf-tim5`): a DMA CRAM write lands at its dot
- [x] Speed pass 2 (2026-09-30, branch `tsconf-speed`): 1.31x → 1.16x Pentagon
  (frame render 189 → 43.5 µs); 1.1x not met - the rest is per-step render
  calls, backlog: lazy drawing with a watch on the displayed pages
- [ ] Open: DBG-3 and TS docks with
  the model-first debugger
- [x] Speed TS-O1..O3 (2026-09-30, branch `tsconf-perf`): span renderer +
  palette cache, pixel-identical to the old renderer (test TSO2); frame render
  6.9x faster, whole frame 1.89x → 1.26x Pentagon (1.1x target not met; the
  rest is TXT's double pixel count and the per-step hooks)
- [x] Phase 8 timing (2026-09-30, branch `tsconf-phase8`): 14 MHz DRAM
  waits by the zmem.v phase table, 14 MHz external I/O stall, DMA SPI / IDE
  word costs; TIM-4 needs nothing (no mode saturates DRAM); TIM-5 deferred.
  The `cpu_next = 0` arbiter model followed (branch `tsconf-arbiter`), with
  the data-read wait corrected to the RTL (+4..+7 fclk)

- Implementation phases 0-8 per [implementation-plan.md](implementation-plan.md).
- Prerequisites: PLAN #60 (all built, on branch `infra-60` for (b) and (f); the
  linear turbo ratio (b): TSConf sets `hw_turbo_ratio` {1, 2, 4, 4}); the unified media manager (#58, M1/M2/M4 on master)
  for the SD part of phase 6: TSConf only registers its `sd.zc` slot
  ([integration-tsconf-sd.md](../2026-09-28-storage-manager/integration-tsconf-sd.md)); control
  from the GUI and every automation surface comes from the media verbs
  ([media-control-design.md](../2026-09-28-storage-manager/media-control-design.md), #58 M4), not
  from a TSConf-specific SD API; #40 Phase 1 and #42
  (`IVideoMapper`) before phase 7.
- Shared with the ATM3 completion program (PLAN #55, [implementation-plan.md](../2026-09-15-atm-baseconf-highres-ports/implementation-plan.md) §2): M1 hook, write intercept, `ZControllerSpi`, SD on `IBlockDevice`, `EvoAvr`; the SD card, host folders (`HostFolderFat`) and media control through the media manager (PLAN #58, [storage-manager](../2026-09-28-storage-manager/technical-design.md)). ATM3 moves to the official `zxevo_fe.rom` (pages 0-3 empty), so TSConf needs its own ROM file with TS-BIOS in pages 0-3.
- Open user decisions: none. D1-D7 in technical-design §3.2 record the
  decisions taken (D2 changed on 2026-09-29: Nemo IDE emulated).
