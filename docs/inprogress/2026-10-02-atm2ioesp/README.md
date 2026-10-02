# ATM2IOESP: Wi-Fi on the ATM Turbo 2+ INTERNAL I/O connector

**Date:** 2026-10-02 · **Plan:** network adapters (PLAN, NedoOS integration) ·
**Status:** [TODO.md](TODO.md)

## Why

NedoOS reaches an ESP module on the ATM Turbo 2+ in two ways: the board's own
COM port (the keyboard controller's RS-232 at 38400, comType 1) and the
ATM2IOESP card (a TL16C550C at 115200, comType 3). The COM port works on the
real board too only "as it can": its 8051 answers every `IN #FE` of the
driver's polling loop and now and then misses a received byte
([tdd-atm2-kbc.md §7.1](../2026-10-01-atm2-keyboard-controller/tdd-atm2-kbc.md)).
The card has a real 16550 with a 16-byte FIFO and no such limit. Both exist in
the real world, so both are emulated.

## Documents

| File | Content |
|:--|:--|
| [reference-atm2ioesp.md](reference-atm2ioesp.md) | the card, the INTERNAL I/O bus (#FB latch, #FA strobes), NedoOS's use, sources |
| [tdd.md](tdd.md) | the design |
| [TODO.md](TODO.md) | progress |
