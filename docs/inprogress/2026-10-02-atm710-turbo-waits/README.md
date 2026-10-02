# ATM Turbo 2+ v7.10: RAM waits at 7 MHz

**Date:** 2026-10-02 · **Plan:** PLAN #61 follow-up 4 ([machine waits](../2026-09-29-machine-waits/requirements.md)) ·
**Status:** [TODO.md](TODO.md)

## Why

In turbo the ATM Turbo 2+ v7.10 runs its Z80 at 7 MHz, but the RAM is
shared with the video fetcher: every RAM access waits for the CPU's slot
(the assembly manual: "7 MHz, 140-160% performance, not 200% due to memory
WAIT states"). ROM and I/O run at full speed. unreal-ng ran ATM710's turbo
as a plain 2x clock.

Found through NedoOS's ESP driver on the ATM2 COM (the keyboard
controller's RS-232): its receive loop polls `IN #FE` from RAM, and only
with the RAM waits do the gaps between reads leave the controller's 8051
time for its serial interrupt. Without them every reply from the ESP
module lost bytes ([tdd-atm2-kbc.md §7.1](../2026-10-01-atm2-keyboard-controller/tdd-atm2-kbc.md)).

## Documents

| File | Content |
|:--|:--|
| [reference-atm710-turbo-waits.md](reference-atm710-turbo-waits.md) | the hardware rule from the schematic and the manual, other emulators |
| [tdd.md](tdd.md) | the design (an overlay like the Scorpion's, installed while turbo is on) |
| [TODO.md](TODO.md) | progress |
