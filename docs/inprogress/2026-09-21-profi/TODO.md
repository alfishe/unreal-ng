# Status: TODO

ZX Profi 1024 (`MM_PROFI`): design complete, implementation not started.

## Done
- [x] Karabas-Pro FPGA analysis: [karabas-pro-hardware-analysis.md](karabas-pro-hardware-analysis.md)
- [x] Review of UnrealSpeccy / ZXMAK2 / Xpeccy: [existing-emulators-review.md](existing-emulators-review.md)
- [x] Audit of unreal-ng integration points: [unreal-ng-integration-audit.md](unreal-ng-integration-audit.md)
- [x] Technical design: [technical-design.md](technical-design.md)
- [x] Frame-timing investigation from emulator sources: [frame-timing-investigation.md](frame-timing-investigation.md)
- [x] Test material collected in `testdata/machines/profi/`

## Remaining
Everything in the implementation order (technical-design.md §9): config, decoder rewrite, banks, DOS latch and FDC gating, TTD `ProfiPaging`, hi-res renderer and palette, peripherals, automation, tests.
Open decisions to settle first: technical-design.md §12 (Q1 resolved to 69888; Q3 hi-res timing needs real-hardware evidence; Q2 extended ports from SYS ROM, Q3 hi-res clock).

## Notes
- The integration audit wrongly suspected ROM pages 0/2 were swapped; verified 0=SYS, 1=TR-DOS, 2=128K+STS, 3=48K (design §2).
