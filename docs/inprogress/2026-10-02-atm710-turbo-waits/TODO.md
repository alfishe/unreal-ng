# TODO: ATM Turbo 2+ v7.10 RAM waits at 7 MHz

**Plan:** PLAN #61 follow-up 4 · [README.md](README.md)

| Step | Status |
|:--|:--|
| Machine-waits requirements: ATM710 row, AC2 narrowed to 3.5 MHz | done 2026-10-02 |
| Research: the rule from the v7.10 schematic (D68, D69.1, D73, D98, D79) and the manual; other emulators | done 2026-10-02: [reference-atm710-turbo-waits.md](reference-atm710-turbo-waits.md) |
| Design | done 2026-10-02: [tdd.md](tdd.md) |
| Overlay, installed by the ATM710 port decoder while turbo is on (`contention` feature); TTD restore sync; `atm710_turbo_waits` in the contention report on every surface | done 2026-10-02 |
| Tests: worked examples, negatives (3.5 MHz, feature off, ROM, I/O), other machines unchanged | done 2026-10-02: `Atm710TurboOverlay_Test` (8); full suite green |
| A/B benchmark (machines without the waits; ATM710 itself) | open: the machine was loaded (load average 95-150) during the work; no code path of the other machines changed (the overlay sits on the bus only while ATM710 turbo is on) |
| Changed baselines (ATM710 golden row, CP/M boot tests, TTD fixtures with ATM710) | none changed: they run at 3.5 MHz |
| NedoOS over the ATM2 COM: wget and the browser at 7 MHz, 38400 | wget done 2026-10-02 (headless, tdd §4); the browser in the Qt window: next |
| Open questions researched (schematics, manual diagrams, 313 zx-pk pages) | done 2026-10-02: reference §7; the rule unchanged |
| WD1793 ports: one wait state at 7 MHz (/VGCS through R1C9) | done 2026-10-02: `AddFdcTurboWait`, test `FdcPortsWaitOneInTurbo` |
| A frame-loop counter run on a real v7.10 (numeric check) | open: no measurement found anywhere |
