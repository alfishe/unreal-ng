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

## Remaining
- [ ] IDE (blocks TTD gaps T1/T3 - RTC-CMOS-in-TTD and an IDE serializer, both deferred until IDE itself lands)
- [ ] Verify the BIOS menu entries boot (CP/M, TR-DOS 48K/128K, Sinclair 48/128); BIOS main menu itself is reached (fixed FDC BUSY visibility, technical-design.md section 14)
- [ ] Hi-res real-hardware timing evidence (design section 12 Q3), real recordings for TTD v2 benchmark
- [ ] Commit (only on explicit request): decide `testdata/machines/profi/` fixtures and `testdata/NOTICE.md` row
