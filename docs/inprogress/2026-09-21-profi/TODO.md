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

## Remaining
- [ ] Covox/SoundRive, RTC (DS12885), IDE, Kempston joystick, FE read bit 7
- [ ] Verify the BIOS menu entries boot (CP/M, TR-DOS 48K/128K, Sinclair 48/128); BIOS main menu itself is reached (fixed FDC BUSY visibility, technical-design.md section 14)
- [ ] Hi-res real-hardware timing evidence (design section 12 Q3), real recordings for TTD v2 benchmark
- [ ] Commit (only on explicit request): decide `testdata/machines/profi/` fixtures and `testdata/NOTICE.md` row
