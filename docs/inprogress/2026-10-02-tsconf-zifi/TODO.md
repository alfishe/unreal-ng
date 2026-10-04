# ZiFi - TODO

Design: [tdd.md](tdd.md) §6.

| Item | Status |
|:--|:--|
| Research: the AVR path, generations, software, other emulators | done 2026-10-02 ([reference-zifi.md](reference-zifi.md)) |
| Research: the Sprinter Wi-Fi driver and building it for TS-Conf | done 2026-10-02 ([reference-sprinter-wifi-driver.md](reference-sprinter-wifi-driver.md)) |
| Z1 the `#xxEF` device, ZiFi API, line, INT, TTD, surfaces | done 2026-10-02: `ZiFi` (`core/src/emulator/io/network/zifi.*`), the TS-Conf decode, the TS AVR's 16550 on TS-Conf, `[NETWORK] ZiFi=`, TTD blobs 39 / 40, guest 5, status `zifi` on every surface, Qt Network window, recipe; tests `ZiFi_Test` (16), `NetworkPanelModel_Test` |
| Z2 DMA device 7; the ZITOR timeout at its exact time (now: at the next access, received byte or frame boundary, up to a frame late) | open |
| Z3 `ZIFI-NATIVE` | done 2026-10-04 ([tdd.md](tdd.md) §7.2): `ZiFiNativeModule` (S3 `s3-native-0.6.94`, ESP-01S `native-0.2.2`), `ZiFi=ZIFI-NATIVE[,S3\|ESP01S]`, `esp` status on every surface, Qt, recipe; tests `ZiFiNativeModule_Test`, `ZiFi_Test` (+3) |
| Z3b the VFS file bridge (FTP / SMB / WebDAV via the WC plugins), OTA, weather, HTTPS | open (they answer "not emulated") |
| Z4 ESP-AT 2.2.x dialect | done 2026-10-04 ([tdd.md](tdd.md) §7.1): `atdialect.*`, per-module firmware `AT,<firmware>`, the ESP-01's ESP8266 1 MB default; tests `AtDialect_Test`, `ComPortSpec_Test` (+2) |
| Z4 open: the kit's "2.2.1" vs Espressif's v2.2.1.0 | ask the kit's author ([tdd.md](tdd.md) §7.4) |
| Z5 end-to-end with the real programs | smoke done 2026-10-04 ([tdd.md](tdd.md) §7.3: the new `zifi.spg` on ZIFI-NATIVE,ESP01S, HackerVBI `zifi.spg` on AT); automated tests and the WC plugins open |
| Z6 ZiFi32 | on demand |
