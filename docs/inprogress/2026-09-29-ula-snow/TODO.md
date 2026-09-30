# TODO: ULA snow

**Plan:** follow-up 2 of PLAN #61 ([m1-contention TODO](../2026-09-28-m1-contention/TODO.md)) ·
[research.md](research.md) · [requirements.md](requirements.md) · [tdd.md](tdd.md)

| Step | Status |
|:--|:--|
| Research: test programs, hardware model, Snow Hold photos from three machines | done 2026-09-29 |
| Requirements, design | done 2026-09-29 |
| Core: refresh notes, marks, renderer and floating bus | done 2026-09-29 |
| Anchor check against the Snow Hold photos (AC1) | done 2026-09-29: T3 on pixel byte 1's fetch, `R` before the increment ([research.md](research.md) section 6) |
| Invariant and rendering tests | done 2026-09-29 (`UlaSnow_Test`, `UlaSnowRender_Test`) |
| Visual test program | done 2026-09-29: [tools/verification/contention/snowtest](../../../tools/verification/contention/snowtest/README.md), `SnowTest_Test` |
| Analytic floating-bus check | dropped: not observable in this model, no hardware anchor ([tdd.md](tdd.md) section 8) |
| A/B performance measurement | done 2026-09-30: the final version (D) costs nothing measurable (below) |
| Other emulators | open: snowtest is visual; the harness compares memory. Candidates with snow: xpeccy-plus (`snow` option), ZX-M8XXX, SpecEmu |
| Real hardware | open: photos of snowtest on a 48K / 128K / +2 |

## Performance measurements

`BM_HostFrame_*`, A = `5cd83bfa` (master), rounds A B A B A B B A B A with the 1-minute load under 12,
docs/guidelines/performance-guidelines.md section 4. Minimum CPU time per side and the mean of the five paired
differences:

| Version | 48K Fast | Pentagon Fast | Pentagon Debug | Scorpion Fast |
|:--|--:|--:|--:|--:|
| B: the test (`ioContention` and the slot) inline in `m1_cycle` | +0.9 % / +0.1 % | +0.4 % / -0.7 % | -0.1 % / 0.0 % | **+1.8 % / +1.7 %** (4 of 5 pairs positive) |
| C: the pointer test inline, the rest out of line | +1.2 % / +0.6 % | **+1.5 % / +0.9 %** | **+1.2 % / +1.2 %** (5 of 5) | **+2.0 % / +1.3 %** |
| D: the snow check in the contended interfaces only (`MemoryReadM1Snow`), `3ef84696` against its parent | +0.3 % / +0.6 % | -0.1 % / +0.3 % | -0.0 % / +0.2 % | +0.9 % / +0.1 % (pairs +0.3 +0.9 +0.9 +0.4 -2.3) |

B and C cost the machines that never snow about 1-2 %: extra code in the hottest function. D leaves their opcode
fetch unchanged by construction (the plain interfaces' `MemoryReadM1` is their plain read), and the measurement
agrees: every difference within the ±2 % round-to-round noise, signs mixed (2026-09-30, load 6-13, NeoGS and
overlay benchmarks -0.6 % .. +0.3 % too).
