# TODO: co-emulation harness follow-ups

**Plan:** follow-up 5 of PLAN #61 · [requirements.md](requirements.md)

| Step | Status |
|:--|:--|
| Requirements | done 2026-09-29 |
| MAME `scorpio`: why it reset | done 2026-09-30: MAME's `scorpio` has Even M1; the probe's old engine ran its delays long, an interrupt cut a delay short and the chain ran into `RST 0`, whose RAM test cleared `DONE`. Reproduced with the old probe (ab1f927c); [mame/README.md](../../../tools/verification/coemu/mame/README.md). ZXMAK2's old Scorpion crash was the same |
| Kozynax runner | done 2026-09-30: Pentagon, Scorpion, ProfScorp, ATM710, ATM3, Profi ok; 48K / 128K / +3 as ZXMAK2 (byte-identical dumps) |
| ZX-M8XXX runner | done 2026-09-30: Pentagon, Scorpion (timed as a Pentagon) ok; the Sinclair machines differ in its instruction-internal timing ([zx-m8xxx/README.md](../../../tools/verification/coemu/zx-m8xxx/README.md)) |
| spec_chum runner | done 2026-09-30: Pentagon ok (from tape); 48K / 128K / +2 internal ticks never wait; +2A / +3 use the 128K pattern; Scorpion does not load (TR-DOS paging) ([spec-chum/README.md](../../../tools/verification/coemu/spec-chum/README.md)) |
| fusetest: assemble, run on unreal-ng | done 2026-09-30: pasmo 0.5.5, FUSE SVN r5736; `FuseTest_Test`. Three unreal-ng defects pinned as known deviations (below); the harness would need a wrapper with `DONE` ([fusetest/README.md](../../../tools/verification/contention/fusetest/README.md)) |
| X-04: contention changes time only | done 2026-09-30: `CtProbeTimeOnly_Test` (48K, 128K, +3): registers, RAM and paging latches equal with contention on and off; floating-bus reads checked by port only |
| Harness README, probe README results | done 2026-09-30 |

## unreal-ng defects found by fusetest (not fixed here)

FUSE 1.6.0 prints `passed` on each; `FuseTest_Test` pins today's output, so a fix shows up as a changed deviation.

| Defect | Machines | Detail |
|:--|:--|:--|
| Floating bus of a port with a contended high byte read too early | 48K, 128K | unreal-ng takes the byte at IORQ; the CPU takes the data at the end of the stretched I/O cycle, after the late waits (FUSE reads at 43069 T and gets the planted attribute; unreal-ng at 43055, idle, #FF) |
| `IN` from the #7FFD decode does not latch the data bus into the paging register | 128K, +2 | FUSE `periph.c` and ZEsarUX latch it; needs the defect above fixed too (#7FFD has a contended high byte) |
| #BFFD reads #FF instead of the selected AY register | +2A, +3 | FUSE `ay_ports_plus3`, ZEsarUX "BFFD R: +2A/+3 mirror of FFFD" |
