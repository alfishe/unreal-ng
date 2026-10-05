# ZiFi - TODO

Design: [tdd.md](tdd.md) §6.

| Item | Status |
|:--|:--|
| Research: the AVR path, generations, software, other emulators | done 2026-10-02 ([reference-zifi.md](reference-zifi.md)) |
| Research: the Sprinter Wi-Fi driver and building it for TS-Conf | done 2026-10-02 ([reference-sprinter-wifi-driver.md](reference-sprinter-wifi-driver.md)) |
| Z1 the `#xxEF` device, ZiFi API, line, INT, TTD, surfaces | done 2026-10-02: `ZiFi` (`core/src/emulator/io/network/zifi.*`), the TS-Conf decode, the TS AVR's 16550 on TS-Conf, `[NETWORK] ZiFi=`, TTD blobs 39 / 40, guest 5, status `zifi` on every surface, Qt Network window, recipe; tests `ZiFi_Test` (16), `NetworkPanelModel_Test` |
| Z2 DMA device 7; the ZITOR timeout at its exact time (now: at the next access, received byte or frame boundary, up to a frame late) | open |
| Z3 `ZIFI-NATIVE` | done 2026-10-04 ([tdd.md](tdd.md) §7.2): `ZiFiNativeModule` (S3 `s3-native-0.6.94`, ESP-01S `native-0.2.2`), `ZiFi=ZIFI-NATIVE[,S3\|ESP01S]`, `esp` status on every surface, Qt, recipe; tests `ZiFiNativeModule_Test`, `ZiFi_Test` (+3) |
| Z3b the VFS file bridge: the VFS client and the FTP server (both firmwares) | done 2026-10-04 ([tdd.md](tdd.md) §7.5): `ZiFiVfsBridge`, `ZiFiFtpServer`, TTD in the ZiFi blob (variable size), `file_bridge` status on every surface, recipe demo; the TS AVR wait picked up per task (`waitChecksPerLoop`); tests `ZiFiVfsBridge_Test` (10), `ZiFiFtpServer_Test` (12), `Uart16550_Test` (+1); real plugins ZIFIFTP v0.15 (S3) and v0.11 (ESP-01S) byte-exact both ways, passive and active |
| Z3b HTTPS for the S3's HTTP GET and redirects to HTTPS | done 2026-10-04 ([tdd.md](tdd.md) §7.5 "HTTPS"): host-side TLS (`hosttls.*`, OpenSSL, optional `UNREAL_HOST_TLS`), plaintext journaled; tests `HostTls_Test` (3, loopback TLS server), `ZiFiNativeModule_Test` (HTTPS, redirect, downgrade) |
| Z3b weather (S3 `WEATHER_GET`), the ESP-01S WebDAV server, the ESP-01S NTPTIME first boot | done 2026-10-04 (branch zifi-plugins): `zifiweather.*`, `zifiwebdavserver.*`, `native_session.weather` / `.webdav` on every surface, recipe; tests `ZiFiWeather_Test` (7), `ZiFiNativeModuleWeather_Test` (4), `ZiFiWebDavServer_Test` (10); real plugins WEATHER.WMF, NTPTIME.WMF, ZIFIWDAV.WMF (rows below) |
| Z3b WC Update (S3 `WCU_START` / `APPLY` / `STOP` / `SYNC`) | done 2026-10-04 ([tdd.md](tdd.md) §7.5 "WC Update"): `ZiFiWcUpdater` (the firmware's updater as a C++20 coroutine, TTD by a primitive log), `ZiFiHttpFetch` (its HTTPS GET on its own socket and resolver), `zifigit.*` (git SHA-1, the GitHub JSON), `file_bridge.wc_update` on every surface; tests `ZiFiWcUpdater_Test` (9, fake GitHub + fake plugin); real `WCUPDATE.WMF` v1.0 against GitHub byte-exact |
| Z3b SMB (S3), the online update | designed 2026-10-04 ([tdd-smb-online-update.md](tdd-smb-online-update.md)), PLAN #92; phases below (they answer "not emulated" until then) |
| N0 Network-wide: host listeners bind `0.0.0.0` by default + "allow remote access" setting (off = `127.0.0.1`), all surfaces + Qt + recipe (owner decision 2026-10-04) | done 2026-10-04: `[NETWORK] RemoteAccess=on\|off`, key `remote_access` (CLI, WebAPI + OpenAPI, MCP, Lua, Python), `IHostNet::TcpListen` takes the bind address (`VirtualNetworkConfig::ListenAddress`, the one source B0's UDP forwards use too), a change alone re-listens without a refit (`VirtualNetwork::SetRemoteAccess`), status `settings.remote_access`, `virtual_network.remote_access` / `listen_address`, `guest_servers[].host_address`; Qt checkbox; recipe `.recipe/peripherals/network.md` "Remote access"; tests `VirtualNetwork_Test` (6), `HostNetBridge_Test.DISABLED_ListenerBindsTheAddressItIsGiven` (manual: needs a non-loopback address), `NetworkManager_Test` (+3), `Config_Test.NetworkRemoteAccess`, `NetworkPanelModel_Test.RemoteAccessRoundTrip`. TTD: the listener address is host-side only (no host command during replay) |
| U1 online update check: `ZiFiOnlineUpdater` (manifest over HTTPS, version compare, events `65`), status, surfaces, recipe | open (S) |
| U2 online update install: download, header + SHA-256, SAME = real reinstall + restart, else `update:flash not emulated` | open (S; after U1) |
| B0 `Forward=udp:<host>:<guest>` in the virtual network (every surface, TTD) | open (S) |
| B1 SMB core: own SMB 3.0.2 server, NTLMv2, signing, sessions, trees, srvsvc, `SMB_START` / `SMB_STOP`, events 62 / 63 | open (M) |
| B2 SMB files: the file commands, credits, compounds, PENDING, the VFS mapping incl. `44 FAT_WINDOW`, caches, event 64 | open (L; after B1) |
| B3 SMB TTD (bridge section v3), `file_bridge.smb` on every surface + Qt, recipe | open (M; after B2) |
| B4 SMB conformance (smbclient, impacket, libsmb2 scenarios, Finder, Windows 24H2) and the real `ZIFISMB.WMF` under WC with TTD | open (M; after B3) |
| B5 discovery: NBNS, LLMNR, WS-Discovery + HTTP 5357 in the virtual network | open (S; after B1, B0) |
| Z4 ESP-AT 2.2.x dialect | done 2026-10-04 ([tdd.md](tdd.md) §7.1): `atdialect.*`, per-module firmware `AT,<firmware>`, the ESP-01's ESP8266 1 MB default; tests `AtDialect_Test`, `ComPortSpec_Test` (+2) |
| Z4 open: the kit's "2.2.1" vs Espressif's v2.2.1.0 | ask the kit's author ([tdd.md](tdd.md) §7.4) |
| Z5 end-to-end with the real programs | done 2026-10-04 with the programs as a user runs them, real internet ([Z5 results](#z5-results-2026-10-04)); the file-bridge plugins (FTP / SMB / WebDAV / WC Update) fail until Z3b; automated tests open |
| Z6 ZiFi32 | on demand |

## Z5 results (2026-10-04)

TS-Conf, Wild Commander Improved v1.11i from an SD folder (`sd.zc`) with `zifi/zifi.ini` (`SSID: UnrealNG`, `time:
+3`, `city: Rome`), the programs run as a user does (WC panel + Enter, the mouse in the ZiFi browser, F10 for
plugins), the virtual network with host access, TTD recording with a 3000-frame limit before every run. Programs
from their upstream repositories (HEADs checked 2026-10-04, none newer than the local mirrors):
[HackerVBI/ZiFi](https://github.com/HackerVBI/ZiFi) via
[andrewinsidelazarev/ZiFi](https://github.com/andrewinsidelazarev/ZiFi) (`zifi.spg` 0.733),
[ZiFi-ESP32-S3-Zero](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero) `2e5ba83` (S3 `zifi.spg`, the
WMF plugins, `tools/esp_info.sna`),
[ZiFi-ESP-01S-Native-C-Project](https://github.com/andrewinsidelazarev/ZiFi-ESP-01S-Native-C-Project) `90834e4`
(ESP-01S `zifi.spg`, NTPTIME / ZIFIFTP / ZIFIWDAV). Not committed (third-party binaries).

| Program | Firmware | Result | What came from the internet / what failed |
|:--|:--|:--|:--|
| HackerVBI `zifi.spg` 0.733 | `AT` (NonOS 1.7.4) | works | update check `zifi.vtrd.in/zifi_ver.php` 200; Press > Hype: `zifi.vtrd.in/get.php?src=hype` (11.7 KB) - the article list ("Video-spectrumizer финал by nodeus, 12.06.2026", ...); Graphics > zxart.ee Most popular: the list ("0.Baking Soda by Grongy, 2021", ...) and the picture shown and saved to `zifi/downloads/2026_10_04/359852.SCR` (6912 bytes) |
| HackerVBI `zifi.spg` 0.733 | `AT,ESP8266-AT222` | fails (program) | stops at "Try connect to Access point": `AT+CWMODE_DEF` / `AT+CWJAP_CUR` answer `ERROR` as on ESP-AT 2.2.2; the program needs NonOS AT |
| HackerVBI `zifi_rs.spg` | - | not run | the cable build: needs PSB's PC socket server (`ic.exe`) on the COM port |
| S3 `zifi.spg` (new) | `ZIFI-NATIVE,S3` | works | startup: Wi-Fi up, IP 10.0.2.15, HTTP test `zifi.vtrd.in` 200, clock from NTP; Hype list; zxart picture "Baking Soda"; Games: vtrd.in list and "Shadows of Belial" downloaded through `zifi.vtrd.in/unzipremote.php` to `zifi/downloads/2026-10-04/Shad_Bel.scl` (122921 bytes, SCL checksum valid, byte-identical to a host fetch) |
| S3 `zifi.spg` | `ZIFI-NATIVE,S3` | works since Z3b ([tdd.md](tdd.md) §7.5 "HTTPS") | DOWNLOAD > "Demos: bbb.retroscene.org": `http://` answers 301, the redirect goes over TLS (`connect-tls` to :443), the list shows ("1.ZX Tribute 40 by .koval, 2022", as a host fetch); item 1 saved as `zifi/downloads/2026-10-04/ZXTRIB40.TAP` (53 944 bytes, byte-identical to the host's `unzipremote.php` answer without its 4-byte ".tap" prefix); TTD on. prods.tslabs.info's HTTPS did not answer from this host either (curl: no connection) |
| S3 `zifi.spg` | `ZIFI-NATIVE,ESP01S` | works | startup (PROXY_STATUS unknown to the ESP-01S: "Proxy disabled"), Hype list |
| ESP-01S `zifi.spg` (new) | `ZIFI-NATIVE,ESP01S` | works | startup with SYS_INFO "... FW:native-0.2.2"; Hype list; zxart picture "Phantis by MAC"; "Cauldron 1, 2" downloaded to `CAUL12TG.SCL` (98615 bytes, byte-identical to a host fetch; the file's own SCL checksum is wrong at the source) |
| ESP-01S `zifi.spg` | `ZIFI-NATIVE,S3` | works | startup, Hype list |
| `NTPTIME.WMF` (S3) | `ZIFI-NATIVE,S3` | works | runs after WC starts: PING, WIFI_INI, NET_NTP `A2` 14 digits; the WC clock goes from host time to UTC+3 (20:44 at 17:44 UTC) |
| `NTPTIME.WMF` (ESP-01S) | `ZIFI-NATIVE,ESP01S` | works (first boot too since 2026-10-04, branch zifi-plugins) | the first boot after the module is fitted: PING, `03 WIFI_INI` -> `83` at once (the module is on the virtual AP from the box and takes the first password as its own), `22` -> `A2`; the WC clock UTC+3 (21:51 at 18:51 UTC). Before: the 1.5 s rejoin outlasted the plugin's ~12000 polls (follow-up 4) |
| `WEATHER.WMF` (S3) | `ZIFI-NATIVE,S3` | works (2026-10-04, branch zifi-plugins) | F10, "ZiFi Weather Saver": `03 WIFI_INI` (`city: Rome`, `country: IT`), `24 WEATHER_GET` -> `A4` 90 bytes over the real internet (geocoding-api.open-meteo.com, then api.open-meteo.com); the saver shows "Rome", the clock, 22 C clear, sunset 18:46, wind, pressure, five days and the month; `native_session.weather` place Rome 41.8919 / 12.5113 |
| `ZIFIFTP.WMF` v0.15 (S3) | `ZIFI-NATIVE,S3` | works since Z3b ([tdd.md](tdd.md) §7.5) | Python ftplib from the host: LIST / MLSD / SIZE, RETR 100 000 bytes and STOR 70 000 bytes byte-exact (checked on the exported card too), passive and active, MKD / CWD, errors; TTD on |
| `ZIFISMB.WMF` v0.5.10 (S3) | `ZIFI-NATIVE,S3` | fails (Z3b file bridge) | `SMB_START` -> `EE "smb:not emulated"` |
| `WCUPDATE.WMF` v1.0 (S3) | `ZIFI-NATIVE,S3` | works since Z3b ([tdd.md](tdd.md) §7.5 "WC Update") | F10, "ZiFi WC Update v1.0": the check against GitHub `andrewinsidelazarev/Wild-Commander-Improved` `main` `exe` (commit f06f9c5) lists 45 entries ("5 to update": `boot.$C` DIFFERS, `WC/FDI2FDD.WMF`, `Help.txt`, `WC_History.txt`, `WC_todo.txt` NEW; the SD-only plugins "SD only"; `WC/wc.ini` "kept"); applied `boot.$C`, `FDI2FDD.WMF`, `WC_History.txt` (104 339 bytes), `WC_todo.txt`: UPDATED, "- restart WC"; on the exported card each file's MD5 equals the raw.githubusercontent.com file, no `WCUPD.TMP` / `.OLD` left; TTD on. On the ESP-01S `unknown cmd 25`, as the real firmware |
| `ZIFIUPD.WMF` (S3) | `ZIFI-NATIVE,S3` | fails (Z3b: online update) | `ONLINE_UPDATE_CHECK` -> `EE "update-check:not emulated"` |
| `ZIFIFTP.WMF` v0.11 (ESP-01S) | `ZIFI-NATIVE,ESP01S` | works since Z3b ([tdd.md](tdd.md) §7.5) | the same checks: one session (a second gets 421), LIST "Jan 01 00:00", RETR / STOR byte-exact, passive and active; TTD on |
| `ZIFIWDAV.WMF` (ESP-01S) | `ZIFI-NATIVE,ESP01S` | works (2026-10-04, branch zifi-plugins on the zifi-z3b bridge) | F10, "ZiFi WebDAV Server": `06 FTP_START` -> `86`, "Status: Listening"; host curl through `Forward=tcp:8080:80`: PROPFIND Depth 1 of `/` and `/zifi`, GET `zifi/e01.spg` byte-identical (44032 bytes), PUT 20000 bytes -> 201 and back identical, MKCOL 201, HEAD Content-Length, DELETE 204 then 404; Esc -> `07` -> `87`, both servers stop |
| `esp_info.sna` (S3 tools) | both native | works | the SYS_INFO fields: S3 `s3-native-0.6.94`, PSRAM, cores, proxy OFF; ESP-01S `native-0.2.2`, the S3-only fields `n/a` |
| Karabas Pro net-tools, NedoOS | - | not applicable | Karabas: Profi CP/M programs, not TS-Conf; NedoOS has no ZiFi driver (reference-zifi.md §4) |

The file bridge, for checking Z3b with these plugins (the ESP calls the Z80; the plugin answers while it waits):

| Plugin | Starts with | Server | Bridge frames the plugin must answer |
|:--|:--|:--|:--|
| `ZIFIFTP.WMF` v0.15 (S3) | `06 FTP_START [port LE16][user\0][password\0]`, stop `07` | FTP, 3 control sessions, active `PORT/EPRT`, passive `PASV/EPSV` on 2122-2124 | VFS `40 STAT`, `41 OPENDIR`, `42 READDIR`, `43 FSINFO`, `50 OPEN` (modes 0 / 1 / 3), `51 READ`, `53 CLOSE`, `54 DELETE`, `55 MKDIR`, `56 BLOCK`, `57 WRITE_WINDOW`, `58 READ_WINDOW`, `59 RENAME`, `5E SET_METADATA` (MFMT); events `60` client, `61` command, `66` RSSI |
| `ZIFISMB.WMF` v0.5.10 (S3) | `0B SMB_START [port LE16][share\0][host\0][workgroup\0][user\0][password\0]`, stop `0C` | SMB2/3 on TCP 445 (NTLMSSP), NBNS UDP 137, WS-Discovery UDP 3702 | VFS as FTP plus batched `42 READDIR [count]`, `44 FAT_WINDOW`, `5A EXTEND`, `5B SEEK`, `5C SET_EOF`, `5D MOVE_RENAME`; events `62/63/64`, `66` |
| `WCUPDATE.WMF` (S3) | `25 WCU_START repo\0branch\0dir\0...`, `26 WCU_APPLY`, `27`, `28 WCU_SYNC` | HTTPS to GitHub (tree + raw files) | VFS read / write of the WC files (the FTP plugin's `vfs.asm` / `fs.asm`); events `67` state, `68` list line |
| `ZIFIFTP.WMF` v0.11 + `ZIFIWDAV.WMF` (ESP-01S) | `06 FTP_START` (opens FTP on the port and WebDAV on 80), stop `07` | FTP and WebDAV (PROPFIND GET HEAD PUT DELETE MKCOL) | VFS `40`, `41`, `42`, `50`, `51`, `53`, `54`, `55`, `56`; events `60`, `61` |

Evidence (not committed): `scratch/z5/` in the worktree - `ttd/*.ttd` per run, `shots/`, `logs/` (text pages,
`state/network` with the ESP exchanges and the virtual network's activity).

### Z5 follow-ups

1. Z3b file bridge (FTP / SMB / WebDAV / WC Update), weather, online update, TLS for HTTPS and redirects to HTTPS
   (bbb.retroscene.org, prods.tslabs.info): FTP, WebDAV, WC Update, weather and HTTPS done; SMB and the online update
   open (rows above).
2. Screenshots of the ZiFi browser's main screen are black (both builds): the program switches TS-Conf modes per
   line (256-color top bar, text console, list) from line interrupts; the frame and live captures show only the
   sprites. The picture viewer (ZX mode) and Wild Commander capture fine. Text was read from the text page (RAM
   page `#D8`) and the graphics from VRAM instead. To re-check after the black-screenshot fix; if it stays black,
   check the renderer's per-line mode latching.
   **Done 2026-10-04:** the black screenshots were the renderer clearing the frame at every mode switch (fixed in
   `f40928ee0` / `6d992bb0e`, [screenshotter TODO](../2026-10-03-screenshotter/TODO.md) item 6); the colored noise over
   the list was the TSU drawing the junk behind the third sprite LEAP (this commit). Screenshots and recordings of the
   browser now show the header, the list and the status bar.
3. `zifi.spg` sends empty lines (`\r\n`) between commands; the AT module answers `ERROR` to each (NonOS does the
   same as far as known; harmless, the program ignores it).
4. **Done 2026-10-04 (zifi-plugins):** the module, on the virtual AP from the box, takes the first password for it as
   its own (`_passwordKnown`), so `83` comes at once. Was: the ESP-01S `NTPTIME.WMF` skips the clock on the first WC
   boot after the module is fitted: a fresh module joins
   in 1.5 s and the plugin waits only ~12000 port polls for `83`. A real module rejoins from its saved `zifi.ini`
   at power-on before WC starts. Candidate: keep the module's saved `zifi.ini` (its flash) across emulator starts
   and join at power-on.
5. Execution breakpoints at the NTPTIME plugin's code (`#8000..`) did not fire during the WC autostart (not
   investigated; the plugin ran, its frames are in the journal).
6. A mouse `glide` sent while an earlier one was still queued appeared to replace it; the pointer of an absolute-position
   program then ends somewhere else. Wait for a glide to finish (the demo steps do).
7. **Not an emulator bug (2026-10-04):** in `zifi.spg` 0.733 the mouse pointer disappears under the grey strip below
   the status bar. The program limits the pointer to `0..239` (`MOUSE45`: `ld hl,240-1`) and puts that value into the
   16 x 16 pointer sprite's Y (`mouse_proc`: `ld (mouse_spr),a`; `ZiFi SPG/zifi.asm` in
   [ZiFi-ESP32-S3-Zero](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero), the same in HackerVBI's original). With `T_CONFIG = #80` the sprite window is the
   320 x 240 graphics window, so at the bottom only the sprite's first line is inside it and the other 15 lines fall
   into the border. RTL (`video_sync.v` `v_ts` / `hvtspix`, `ts_rres_ext` = T_CONFIG bit 0), the TS-Labs Unreal fork
   (`draw_ts` only between `u_brd` and `d_brd`, `tsconf.cpp` sprite row from `vid.line - u_brd + 1 - spr.y`) and the
   original program running in that fork ([reference-emulator-wine.md](../2026-09-27-tsconf/reference-emulator-wine.md))
   all show the same; the client would have to clamp the pointer at `240-16`. The vertical stripes on photos of the
   real screen in the 256-color bands are the 2-bit DAC's PWM (border CRAM `#0C85`: every channel below level 8) as
   an LCD monitor samples it; the digital picture is one flat color there.
