# TODO: machine waits not modeled yet

**Plan:** follow-up 4 of PLAN #61 · [requirements.md](requirements.md)

| Step | Status |
|:--|:--|
| Requirements | done 2026-09-29 |
| Research: ZX-Evo BaseConf waits (14 MHz, 48K / 128K rasters) from the RTL | done 2026-09-29: [research-zxevo.md](research-zxevo.md) (RTL + Verilator run of the released modules) |
| Research: Scorpion turbo waits from the SC15.1 equations | done 2026-09-29: [research-scorpion-turbo.md](research-scorpion-turbo.md) (JED decoded, schematic traced, simulated) |
| Design | done 2026-09-29: [tdd.md](tdd.md) |
| Overlay opcode-fetch entry, interrupt acknowledge | done 2026-09-29: `HostBusOverlay::onReadM1` / `onInterruptAcknowledge`, `Memory::MemoryReadOverlayM1` |
| ZX-Evo 14 MHz waits (`EvoTurboOverlay`), TTD cache state | done 2026-09-29: TTD peripheral `EvoTurboCache` = 21 |
| Scorpion turbo waits (`ScorpionTurboOverlay`) | done 2026-09-29: SC15.1 |
| Tests | done 2026-09-29: `EvoTurboOverlay_Test` (11), `ScorpionTurboOverlay_Test` (6); full suite green |
| Changed baselines | ATM3 only (its BIOS runs at 14 MHz): the `CoreGolden` row, the TTD CI gate's four exact `ATM3/idle` rows; `ScorpionTurbo_Test.TurboStrobeAppliesMidFrame` (the turbo-off `IN` pays its 2 T); `Core_Test`'s plain-interface list without ATM3 |
| A/B performance | done 2026-09-30 (`790e6268` against `2e106688`, `BM_HostFrame_*`, rounds A B A B A B B A B A, load 11-17): 48K, Pentagon, NeoGS and the overlay benchmarks within noise (-1.5 % .. +1.3 %, signs mixed). **Scorpion +3.2 % debug / +3.9 % fast, 5 of 5 pairs**: its ROM leaves turbo on (checked: after the benchmark's boot `hw_turbo_ratio` 2, one overlay installed), so the benchmark runs the Turbo+ waits. About 2 % of it is the overlay interface itself (the pass-through overlay costs the Pentagon that much), the rest the slot rule (`InPicture` divides per edge inside the wait loops). Idea for later: the slot wait by arithmetic, one division per access |
| Docs | done 2026-09-29: [contention-by-machine.md](../2026-09-28-m1-contention/contention-by-machine.md) §3, §6.5, §9.2, §12 corrected by the research; [memory-contention.md](../../emulator/design/core/memory-contention.md) |
| ZX-Evo 48K / 128K rasters and their contention | deferred: the rasters are not modeled (PLAN #55); rule ready in the research |
| NedoOS shell test with the 14 MHz waits | done 2026-09-30: `ZXEvoErs_Test.NedoOsShellRunsATypedCommand` failed with the waits on. Traced: the same scan codes reach the kernel, the command runs and prints, the screen is identical; the test searched RAM for the output text, which survives only when the pipe hands it to the terminal in one piece (with the waits it arrives in two and the receive buffer is overwritten). The test now reads the ATM text screen |
| ATM3 clock select applied at the next frame | **done 2026-10-01** ([tdd-e8b-board-fidelity.md](../2026-09-15-atm-baseconf-highres-ports/tdd-e8b-board-fidelity.md)): taken over at the next M1 refresh, as `zclock.v` `int_turbo`; the waits follow the applied clock |
| Scorpion: 3.5 MHz while /INT is active; SC15.3 as an option | open (tdd.md section 4) |
