# TODO: three core defects found by fusetest

[requirements.md](requirements.md) · [research.md](research.md) · [tdd.md](tdd.md)

| Step | Status |
|:--|:--|
| Research: RTL, schematics, emulators | done 2026-09-30: all three hold ([research.md](research.md)) |
| Late floating-bus sample | done 2026-09-30 |
| 128K / +2 read-cycle latch | done 2026-09-30 |
| 128K / +2 #BFFD read undecoded | done 2026-09-30 (the +2A / +3 part of the fusetest line was the test runner's empty sound slot) |
| Tests; fusetest with no known deviations | done 2026-09-30 |
| A/B (`BM_HostFrame_*`, `BM_PortIn`) | done 2026-09-30, `d5ef4ae5` against the branch, 10 rounds, load 5-11: every `BM_HostFrame_*` within noise (-2.0 % .. +1.7 %, signs mixed). `BM_PortIn` (1000 x `IN A,(C)`, new): ordinary ports +0.9 .. +2.7 % on a loop of nothing but INs (about 1 ns per IN); #40FF, an undecoded port with a contended high byte, +8.5 % (48K) / +9 % (128K): it now looks up the real fetched byte. A first version cost the 128K +7.5 % on every IN (a virtual call per IN, then a code-layout effect of the reordered calls); fixed by the latch's decode mask in the Z80, one flag for both IN observers, and the rare case in a cold helper (`Z80::FloatingBusAfterLateWaits`) |
| Full build, core tests | done 2026-09-30: no warnings, 5482 passed |
| unreal-z80: the same IN ordering, all suites | done 2026-09-30 (branch `in-late-waits` of the library): the `PortInPost` hook before the read callback, one hook test for both IN waits; units + z80test + all ZEX, T-trace (golden: two lines swap places, no T moves), 27/27 interrupt/contention scenarios, 1356/1356 FUSE internal T-states, z80ex differential 0 mismatches with and without contention (the contended IN is `lateWaits` later than z80ex by design); `z80bench` A/B within noise (a first version cost the callback bus's port loop 17 %: two hook tests in a row) |
| ctprobe: a floating-bus check on a contended-high-byte port (the harness would see this on every emulator) | open, idea |
