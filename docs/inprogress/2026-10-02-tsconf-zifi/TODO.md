# ZiFi - TODO

Design: [tdd.md](tdd.md) §6.

| Item | Status |
|:--|:--|
| Research: the AVR path, generations, software, other emulators | done 2026-10-02 ([reference-zifi.md](reference-zifi.md)) |
| Research: the Sprinter Wi-Fi driver and building it for TS-Conf | done 2026-10-02 ([reference-sprinter-wifi-driver.md](reference-sprinter-wifi-driver.md)) |
| Z1 the `#xxEF` device, ZiFi API, line, INT, TTD, surfaces | done 2026-10-02: `ZiFi` (`core/src/emulator/io/network/zifi.*`), the TS-Conf decode, the TS AVR's 16550 on TS-Conf, `[NETWORK] ZiFi=`, TTD blobs 39 / 40, guest 5, status `zifi` on every surface, Qt Network window, recipe; tests `ZiFi_Test` (16), `NetworkPanelModel_Test` |
| Z2 DMA device 7; the ZITOR timeout at its exact time (now: at the next access, received byte or frame boundary, up to a frame late) | open |
| Z3 `ZIFI-NATIVE` | open |
| Z4 ESP-AT 2.2.x dialect | open |
| Z5 end-to-end with the real programs | open |
| Z6 ZiFi32 | on demand |
