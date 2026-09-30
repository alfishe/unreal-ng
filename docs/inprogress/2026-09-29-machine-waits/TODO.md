# TODO: machine waits not modeled yet

**Plan:** follow-up 4 of PLAN #61 · [requirements.md](requirements.md)

| Step | Status |
|:--|:--|
| Requirements | done 2026-09-29 |
| Research: ZX-Evo BaseConf waits (14 MHz, 48K / 128K rasters) from the RTL | done 2026-09-29: [research-zxevo.md](research-zxevo.md) (RTL + Verilator run of the released modules) |
| Research: Scorpion turbo waits from the SC15.1 equations | done 2026-09-29: [research-scorpion-turbo.md](research-scorpion-turbo.md) (JED decoded, schematic traced, simulated) |
| Design | done 2026-09-29: [tdd.md](tdd.md) |
| Overlay opcode-fetch entry, interrupt acknowledge | open |
| ZX-Evo 14 MHz waits (`EvoTurboOverlay`), TTD cache state | open |
| Scorpion turbo waits (`ScorpionTurboOverlay`) | open |
| Tests, A/B | open |
| ZX-Evo 48K / 128K rasters and their contention | deferred: the rasters are not modeled (PLAN #55); rule ready in the research |
