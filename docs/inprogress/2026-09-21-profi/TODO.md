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
- [x] RTC/CMOS (DS12885-style) wired at `#BF/#FF` (address), `#9F/#DF` (data), EXT mode only (`cpm && rom14`, UnrealSpeccy's own definition - not Karabas's wider variant). New Profi-owned register file (`core/src/emulator/memory/profi/proficmos.{h,cpp}`), sharing only the DS12885 register map with ATM3's CMOS, not its I2C NVRAM
- [x] Hi-res renderer extracted into its own class `ScreenProfi` (`video/profi/`, commit `f773998d`) - the `ScreenAtm` pattern; never a method on `ScreenZX`
- [x] Reconciliation against the reference emulator sources: [2026-09-25-profi-reconciliation.md](2026-09-25-profi-reconciliation.md) (parity matrix, TTD bit-to-bit verdict, gap list)
- [x] Palette gaps from the reconciliation (section 1.4): full 9-bit `GGGRRRBBB` storage (extra blue LSB from `#FE.D7`) and `#FE` read bit 7 ("GX0"/UniCopy, DS80-gated)
- [x] NMI -> DOS latch ("magic button", reconciliation G5): `Emulator::RequestMNI()` raises `CF_TRDOS` when DS80=0, not gated on `pDFFD.4` (Q7 consensus); 8 tests in `profimni_test.cpp`
- [x] TTD gap T4 (reconciliation section 4.2): fold DOS-latch flags (`CF_TRDOS`/`CF_DOSPORTS`) into `TTDProfiPaging::TTDHashState`, not the persisted blob; 2 tests in `ttdprofipaging_test.cpp`
- [x] Covox CP/M-extended-mode aliases (reconciliation G4): `#C7` Left, `#A7` Right - real hardware behavior (DAC moves off the FDC-claimed `#1F..#7F`), not a UnrealSpeccy-only quirk; fixed the mono-leak silencing to not fire on entering ExtMode (the DAC just moves alias, it never loses the bus); 8 tests in `profi_covox_test.cpp`
- [x] Documentation drift cleanup (reconciliation section 7): reworded the stale `ProfiHiresRaster=pico` claim in technical-design.md section 7.4, and `.agents/AGENTS.md`'s "Creatable on master" wording (still needs a final pass on the actual merge)
- [x] IDE/HDD design: [2026-09-25-ide-hdd-design.md](2026-09-25-ide-hdd-design.md) - Profi controller, comparison with SMUC/ATM/Nemo/ZX-Evo/DivIDE, shared core vs per-board adapters, image files + host folders + ATAPI CD-ROM, test methodology, two-rollout plan

## Remaining
- [ ] IDE, rollout 1 (reconciliation G1) - parity with other emulators; design: [2026-09-25-ide-hdd-design.md](2026-09-25-ide-hdd-design.md) §14.1, reconciliation §9
  - [ ] R1-1 Shared ATA disk core (`AtaDevice`/`AtaDisk`/`AtaChannel`), `IBlockDevice`, `RawImage` (write-through) + `MemoryDisk`; delete the `io/hdd` skeleton
  - [ ] R1-2 `IdeAdapterProfi` (`#xxCB/#xxEB/#06AB`, EXT-mode gate), `[HDD]` ini parsing, `Scheme=PROFI` in `data/configs/profi/unreal.ini` (CRLF), TTD interim rule (first IDE command invalidates a recording); SYS ROM HDD loader test at `#28CE` - **closes G1**
  - [ ] R1-3 `hdd`/`cd` automation verbs (CLI, WebAPI, Lua, Python, MCP), `NC_HDD_STATE_CHANGED`
  - [ ] R1-4 Other boards: Nemo (+A8, DivIDE/Evo toggle), SMUC (replaces the Scorpion stub), ATM
  - [ ] R1-5 Image formats: HDF, HDI, fixed VHD; Profi geometry auto-detect
  - [ ] R1-6 Host folder as FAT16/32 volume (session write map, export to `.img`, read-only option)
  - [ ] R1-7 ATAPI CD-ROM (`.iso`, pico-spec command set)
  - [ ] R1-8 Differential harness, fuzz, perf check, Qt HDD/CD menu + activity LED
- [ ] IDE, rollout 2 (reconciliation T3) - **requires further investigation first** (design §7.3, §10, Q10): R2-0 investigation, then COW change layer + write journal, `PeripheralId::AtaChannel` TTD serializer, snapshot media references, folder commit-back
- [ ] RTC CMOS in TTD (reconciliation T1/T2) - deferred until IDE lands
- [ ] IDE open questions (design §13): Q1 `#06AB` read value, Q2 Karabas EXT-mode variant, Q4 Profi disk geometry / `ProfiHiDD` header, Q5 SYS menu path to `#28CE`, Q6 real Profi CP/M HDD image, Q9 CD-reading guest software
- [ ] Verify the BIOS menu entries boot (CP/M, TR-DOS 48K/128K, Sinclair 48/128); BIOS main menu itself is reached (fixed FDC BUSY visibility, technical-design.md section 14)
- [ ] Hi-res real-hardware timing evidence (design section 12 Q3), real recordings for TTD v2 benchmark
- [ ] Commit (only on explicit request): decide `testdata/machines/profi/` fixtures and `testdata/NOTICE.md` row
