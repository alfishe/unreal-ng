# ZiFi: Wi-Fi on TS-Conf (and ZX-Evo with the TS-Labs AVR firmware)

**Date:** 2026-10-02 · **Plan:** network adapters, step N5 (PLAN #78,
[tdd-network.md](../2026-09-30-nedoos-integration/tdd-network.md)) · **Status:** [TODO.md](TODO.md)

## Why

On TS-Conf the Z80 reaches an ESP module through the ZX-Evo's AVR: the TS-Labs
firmware keeps two rings for it (511 bytes in, 255 out), a set of registers at
`#C0EF..#C9EF` and a data window at `#00EF..#BFEF`, and talks to the ESP on its
own UART at 115200. The same AVR also emulates a 16550 COM port at
`#F8EF..#FFEF`. Neither existed in the emulator: `#xxEF` read `#FF`.

Three generations of the ESP side exist in the real world:

1. the original ZiFi: an ESP-01 with Espressif's AT firmware (HackerVBI `zifi.spg`,
   [HackerVBI/ZiFi](https://github.com/HackerVBI/ZiFi));
2. the 2026 "new ZiFi": an ESP-01S or ESP32-S3 with a native binary protocol
   (`5A CMD LEN DATA XOR`), its own `zifi.spg` and Wild Commander plugins;
3. ZiFi32 (TS-Labs): an ESP32-S3 on the VDAC2 card's SPI, not on `#xxEF`.

**Current sources:** the modern `zifi.spg` client (`ZiFi SPG/`, version 0.733, with its built
`build/zifi.spg`), the ESP32-S3 firmware, its Wild Commander plugins (FTP, SMB, NTP, weather, WC Update, online
update) and the protocol description (`docs/PROTOCOL.md`) are in
[andrewinsidelazarev/ZiFi-ESP32-S3-Zero](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero); the
ESP-01S firmware with its FTP / WebDAV / NTP plugins in
[andrewinsidelazarev/ZiFi-ESP-01S-Native-C-Project](https://github.com/andrewinsidelazarev/ZiFi-ESP-01S-Native-C-Project).
The `zifi.spg` on the test SD image is byte-identical to ZiFi-ESP32-S3-Zero's `ZiFi SPG/build/zifi.spg` (commit
`bff1327`, 2026-09-10, SHA-256 `27c03f0756de25ee05b548daf80c797a3ea2908d46a4ffb16740f1113f561c1c`).

The AVR side is the same for 1 and 2. A third party (the Sprinter Wi-Fi driver,
ESP-AT 2.2.x) wants to port to generation 1 too.

## Documents

| File | Content |
|:--|:--|
| [reference-zifi.md](reference-zifi.md) | the AVR firmware path: decoding, registers, commands, rings, interrupt, DMA, the generations, software, other emulators, open questions |
| [reference-sprinter-wifi-driver.md](reference-sprinter-wifi-driver.md) | the Sprinter Wi-Fi driver (`sprinter_wifi`): architecture, its UART layer, the ESP-AT 2.2.x dialect it needs, how to build such a driver for TS-Conf |
| [tdd.md](tdd.md) | the design |
| [TODO.md](TODO.md) | progress |
