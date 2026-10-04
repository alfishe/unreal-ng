# Status: TODO

ZX Profi 1024 (`MM_PROFI`): design complete; implementation in progress on branch `profi` (UnrealSpeccy feature parity).

## Done
- [x] Karabas-Pro FPGA analysis: [karabas-pro-hardware-analysis.md](karabas-pro-hardware-analysis.md) (clone board, not authoritative)
- [x] Review of UnrealSpeccy / ZXMAK2 / Xpeccy: [existing-emulators-review.md](existing-emulators-review.md)
- [x] Audit of unreal-ng integration points: [unreal-ng-integration-audit.md](unreal-ng-integration-audit.md)
- [x] Frame-timing investigation from emulator sources: [frame-timing-investigation.md](frame-timing-investigation.md)
- [x] Technical design: [technical-design.md](technical-design.md) (status in section 14)
- [x] Automation and docs pass: port map / port-trace rules, `/state/paging` pDFFD fields, ROM page names, `PROFI`/`PROFIHR` mode reporting (WebAPI, CLI, Lua, Python), MCP resource `unreal://machine/profi`, permanent docs updated
- [x] Config, decoder, banks, DOS latch and FDC ports, palette, TTD, standard + hi-res video, tests, real-ROM boot to the BIOS splash
- [x] Covox/SoundRive DAC wired at `#5F`/`#3F` (NORMAL mode only; `#3F/#5F` are Beta128 FDC registers while `CF_DOSPORTS` is set) - forwards into the shared `Covox` device via its canonical Left/Right ports, isolated to `PortDecoder_Profi::DecodePortOut`
- [x] RTC/CMOS (DS12885-style) wired at `#BF/#FF` (address), `#9F/#DF` (data), EXT mode only (`cpm && rom14`, UnrealSpeccy's own definition - not Karabas's wider variant). Since 2026-09-28 (PLAN #60(c)) the shared `Ds12887` chip (`core/src/emulator/io/rtc/ds12887.h`, 256 cells) with `[PROFI] NvramFile=`; the former Profi-owned `ProfiCMOS` is gone
- [x] Hi-res renderer extracted into its own class `ScreenProfi` (`video/profi/`, commit `f773998d`) - the `ScreenAtm` pattern; never a method on `ScreenZX`
- [x] Reconciliation against the reference emulator sources: [2026-09-25-profi-reconciliation.md](2026-09-25-profi-reconciliation.md) (parity matrix, TTD bit-to-bit verdict, gap list)
- [x] Palette gaps from the reconciliation (section 1.4): full 9-bit `GGGRRRBBB` storage (extra blue LSB from `#FE.D7`) and `#FE` read bit 7 ("GX0"/UniCopy, DS80-gated)
- [x] NMI -> DOS latch ("magic button", reconciliation G5): `Emulator::RequestMNI()` raises `CF_TRDOS` when DS80=0, not gated on `pDFFD.4` (Q7 consensus); 8 tests in `profimni_test.cpp`
- [x] TTD gap T4 (reconciliation section 4.2): fold DOS-latch flags (`CF_TRDOS`/`CF_DOSPORTS`) into `TTDProfiPaging::TTDHashState`, not the persisted blob; 2 tests in `ttdprofipaging_test.cpp`
- [x] Covox CP/M-extended-mode aliases (reconciliation G4): `#C7` Left, `#A7` Right - real hardware behavior (DAC moves off the FDC-claimed `#1F..#7F`), not a UnrealSpeccy-only quirk; fixed the mono-leak silencing to not fire on entering ExtMode (the DAC just moves alias, it never loses the bus); 8 tests in `profi_covox_test.cpp`
- [x] Documentation drift cleanup (reconciliation section 7): reworded the stale `ProfiHiresRaster=pico` claim in technical-design.md section 7.4, and `.agents/AGENTS.md`'s "Creatable on master" wording (still needs a final pass on the actual merge)
- [x] IDE/HDD design: [2026-09-25-ide-hdd-design.md](2026-09-25-ide-hdd-design.md) - Profi controller, comparison with SMUC/ATM/Nemo/ZX-Evo/DivIDE, shared core vs per-board adapters, image files + host folders + ATAPI CD-ROM, test methodology, two-rollout plan

## Remaining
- [x] IDE, rollout 1 (reconciliation G1) - done 2026-09-28 as the IDE + ATAPI scope, on master (`f5fc5f05`): [implementation-plan.md](../2026-09-28-ide-atapi/implementation-plan.md) §5. **Closes G1**: the Profi board (`#xxCB/#xxEB/#06AB`, EXT-mode gate, mirrored latches), `Scheme=PROFI`; the SYS ROM HDD loader boots from a disk at `#28CE` (`profi_hdd_test`). Automation is the `media` verbs and `state ide`; TTD is the `AtaChannel` blob + write barriers (the rollout-2 serializer, earlier than planned). Profi geometry is auto-detected from the `ProfiHiDD` header (Q4; LBA 256: 16 x 16, LBA 1008: 16 x 63, else 16 x 16)
- [ ] IDE, rollout 2 (reconciliation T3) - **requires further investigation first** (design §7.3, §10, Q10): R2-0 investigation, then COW change layer + write journal, `PeripheralId::AtaChannel` TTD serializer, snapshot media references, folder commit-back
- [x] RTC CMOS in TTD (reconciliation T1/T2) - `PeripheralId::Ds12887` (id 18) since PLAN #60(c), 2026-09-28
- [ ] IDE open questions (design §13): Q1 `#06AB` read value, Q2 Karabas EXT-mode variant, Q5 SYS menu path to `#28CE`, Q6 real Profi CP/M HDD image, Q9 CD-reading guest software
- [x] Verify the BIOS menu entries boot - done in [2026-10-01-profi-v3-v5](../2026-10-01-profi-v3-v5/TODO.md): v5
  `ProfiBoot_Test.MenuEntriesStartWhatTheyName` (TR-DOS 48K, Sinclair 48, Sinclair 128), `CpmBootsFromTheKondorSystemDisk`
  and `SpDosBootsToItsShell` (CP/M); v3 `Profi3Boot_Test.KramisMenuSinclairStartsThe128Menu`,
  `KramisMenuTrDosStartsTrDos503`, `SpDosBootsToItsShell` (Profi-DOS). Not covered by a test: the v5 "TR-DOS 128K" entry
- [x] Hi-res real-hardware timing evidence - done in [2026-10-01-profi-v3-v5](../2026-10-01-profi-v3-v5/design-hires.md):
  BIOS 2.0's hi-res speed test reads what a real 5.06 reads (1.50 / 2.45, `BiosSpeedTestReadsWhatARealBoardReads`).
  Real recordings for a TTD v2 benchmark: none made
- [x] Commit: the `testdata/machines/profi/` fixtures are decided (one disk per boot check, `cpm/README.md`; the
  firmware in `data/rom/profi/`); no `testdata/NOTICE.md` row: it lists only fixtures with a known license, the
  general third-party notice there covers these; continued in [2026-10-01-profi-v3-v5](../2026-10-01-profi-v3-v5/TODO.md)
