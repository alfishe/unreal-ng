# ATM Turbo 2+ keyboard controller (network step N4)

The i8031 / AT89S52 behind `IN #FE` on ATM Turbo 2+ (v7.xx) boards: PC keyboard,
clock and the RS-232 port, emulated by running the real firmware on an MCS-51 core.

| Document | What |
|---|---|
| [reference-atm2-kbc.md](reference-atm2-kbc.md) | Hardware, firmware (2.2 .. 4.1), protocol, timing, software, other emulators; all sources linked |
| [tdd-atm2-kbc.md](tdd-atm2-kbc.md) | Design and phases K0..K5 |
| [TODO.md](TODO.md) | Status |

Firmware images: [data/rom/atm2kbc](../../../data/rom/atm2kbc/README.md).
Parent: [network TDD](../2026-09-30-nedoos-integration/tdd-network.md) §7.3 (N4).
