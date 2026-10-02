# TODO: ATM Turbo 2+ keyboard controller

Status 2026-10-01 (K0-K3 and the controller's TTD blob on master da579f4d9; K4 on branch `esp-modules`):

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

- K4 in progress: the firmware's UART to the serial peers (`ComPort=` on ATM710,
  RTS-gated receive, modem lines), NetworkManager serial port `Atm2Kbc` beside a ZX-WiFi
  card, `kbc_firmware` at runtime, `machine_serial` in the network state on every
  surface, the peer's TTD blob (`MachineSerialPeer` = 27). Left: Qt Network window,
  NedoOS ATM2 ESP kernel end-to-end check.

Next: K5 (state report `atm2kbc` on every surface, A/B benchmark - skipped for now).
Open: tdd §12; seen once: the same CP/M test reaches a key at a slightly different
controller clock depending on which tests ran before it in the process (to explain
before TTD work).
