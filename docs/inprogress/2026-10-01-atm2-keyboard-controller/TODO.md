# TODO: ATM Turbo 2+ keyboard controller

Status 2026-10-01 (branch `esp-modules`, uncommitted):

- K0 done: reference, TDD, the nine images in `data/rom/atm2kbc/` (origin and links in its
  README). The firmware archives and sources themselves are kept outside the repository; every
  one is a public link in the reference §(c) and §9.
- K1 done: MCS-51 core (`core/src/emulator/cpu/mcs51/`), 8051 / 8052, latch-then-poll
  interrupt latency; `mcs51_test.cpp`.
- K2 done: `Atm2Kbc` on ATM710 (`[ATM] Kbc=`, default V41; `[ROM] ATM2KBC=`), every
  IN #FE through the firmware with its exact WAIT, manual /VWR / /VRD strobes (3.1, 4.1),
  VE1, the board reset line resetting the MCU too (2.2 / 3.1 rely on it), INT_T.
- K3 mostly done: PS/2 keyboard model (set 2 frames, typematic), modes 0..3 work through
  the firmware; host input split into MC_KEY_* / MC_PCKEY_* handlers with a route gate
  (`[INPUT] HostKeyboard=`, `key route`, `POST /keyboard/route`, Qt Machine > Host
  Keyboard); automation presses chord PC keys one frame apart.

Next: K4 (the firmware's UART to the serial peers, NetworkManager serial port
`Atm2Kbc`, Network window), K5 (TTD blob, state report on every surface, A/B benchmark).
Open: tdd §12; seen once: the same CP/M test reaches a key at a slightly different
controller clock depending on which tests ran before it in the process (to explain
before TTD work).
