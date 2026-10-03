# Reference: the Sprinter Wi-Fi driver (UNETESP.DLL) and porting it to TS-Conf ZiFi

Research notes, 2026-10-02. Subject: the
[witchcraft2001/sprinter_wifi](https://github.com/witchcraft2001/sprinter_wifi)
repository (the "Sprinter ESP Network Kit", commit `909330f` of 2026-09-20), in particular
[`src/dll/unetesp.asm`](https://github.com/witchcraft2001/sprinter_wifi/blob/main/src/dll/unetesp.asm).
The author wrote about a TS-Conf ZiFi port: for the "old ZiFi" only the physical
access to the ESP needs replacing (ISA UART to ZiFi UART); for the "new" one substantial work
is needed on the host side and inside the ESP32. He also asked how the driver should be built
for TS-Conf, since there it obviously cannot be a Sprinter DLL.

`file:line` references point into the repositories at the commits named in [Sources](#sources).

## Summary

- **What the repository is.** A Z80 assembly (sjasmplus) network kit for the Sprinter under
  Estex DSS. It drives an ESP8266 (ESP-12F) running Espressif's stock **ESP-AT 2.2.2** text
  firmware, through a TL16C550 UART on an ISA card (COM3, `0x3E8`). There is no custom firmware
  and no binary protocol. The firmware image in `firmware/` is an esp-at
  `v2.2.2.0_esp8266` build with a "runtime flow fix". Its source is not in the repository.
- **Two ways to deliver the same code.** Twelve utilities (WGET, FTP, TELNET, ...) INCLUDE the
  library modules (`src/lib/esplib.asm`, `esp_tcp.asm`, ...) and are assembled into
  self-contained `.EXE` files. The same modules, plus a 24-entry jump table, make up
  **UNETESP.DLL**. This is a relocatable *libman 1.3 / L1* library. A consumer embeds the libman
  loader (`l_load` / `l_call` / `l_free`) and calls numbered functions with arguments in
  A/DE/IX/IY. DSS itself knows nothing about DLLs: libman allocates DSS memory pages, relocates
  the image, and maps it into a 16 KB window on every call.
- **Where the hardware access lives.** All UART access is concentrated in `isa.asm` (opens the
  ISA window: the UART's registers appear as memory at `0xC3E8..0xC3EF`) and `esplib.asm`
  (16550 register equates and primitives). However, the hot receive loops in `esp_tcp.asm` and
  a few spots in `unetesp.asm` / `wcommon.asm` read `(REG_LSR)` / `(REG_RBR)` directly for speed.
  So the layer is mostly separated, but not behind a clean interface. The code also depends on
  **RTS/CTS hardware flow control** (16550 auto-flow plus `RXPAUSE`/`RXRESUME`), and ZiFi has no
  such signals.
- **"Old ZiFi" and "new ZiFi".** The old ZiFi is the TS-Labs ZiFi interface (AVR FIFOs at
  `#xxEF`) with an ESP-01 (ESP8266) running stock AT firmware, as used by the original
  [HackerVBI/ZiFi](https://github.com/HackerVBI/ZiFi) client. The new ZiFi is almost certainly
  [andrewinsidelazarev/ZiFi-ESP32-S3-Zero](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero)
  (active, last commit 2026-09-29). It is an ESP32-S3 Zero on an ESP-01S adapter with a
  **native binary protocol** (`5A CMD LEN DATA XOR`) instead of AT. That protocol offers one TCP
  client, an ESP-side HTTP GET, and NTP/ping/IP-config, plus an ESP-to-Z80 file system bridge
  for Wild Commander's FTP/SMB plugins. It has no UDP, no second channel and no listen socket,
  so the UNET contract cannot be implemented on it without firmware changes. That matches the
  author's remark.
- **Building for TS-Conf.** TS-Conf has no OS-level loader comparable to libman/DSS. The
  realistic options are:
  - **(A)** a statically linked library with a back end chosen at build time;
  - **(B)** a Wild Commander plugin (`.WMF`) or a resident module in a fixed RAM page with
    the same jump table, loaded by a small loader;
  - **(C)** a NedoOS driver behind the existing socket API. NedoOS runs on ZX-Evo under
    BaseConf, not under TS-Conf, so (C) only helps if ZiFi is ever reachable from NedoOS.

  (A) is what both ZiFi projects already do.
- **What the emulator needs.** The sprinter_wifi firmware is stock ESP-AT, so it needs **no
  third personality**. It needs:
  - an "ESP8266 ESP-AT 2.2.x" dialect of our `AtModule`, which today answers as NonOS AT 1.7.4
    on ESP8266;
  - the ZiFi port block itself (TS-Conf `#xxEF`, step N5).

  The new ZiFi firmware *is* a third personality ("ZiFi native"). Its protocol is described in
  [What the emulator needs](#what-the-emulator-needs).

## Repository architecture

### Layout and build

| Part | Files | Role |
|---|---|---|
| Hardware | `src/include/sprinter.inc`, `src/lib/isa.asm` | Sprinter page ports, ISA window open/close |
| UART + AT core | `src/lib/esplib.asm` (913 lines) | 16550 equates, find/init, TX/RX primitives, `UART_TX_CMD` (send AT line, collect reply until `OK`/`ERROR`/`FAIL`/`busy`) |
| TCP/UDP over AT | `src/lib/esp_tcp.asm` (3339), `esp_tcp_multi.asm` (1018), `esp_udp.asm` | `CIPSTART`/`CIPSEND`/`+IPD` parsing, single-link and `CIPMUX=1` dialects, defer queues, passive receive (`CIPRECVDATA`) |
| Shared app glue | `src/lib/wcommon.asm`, `netcfg_lib.asm`, `util.asm`, `dss_error.asm` | `NET.CFG`, DSS environment (`NET_*` variables), delays, cancel keys |
| DLL | `src/dll/unetesp.asm` (2840) | UNET ABI wrapper over the library modules |
| DLL loader | `src/lib/libman13.asm` | vendored libman 1.3 (2004) with diagnostics and DSS fixes |
| Contract | `src/include/unet.inc`, `docs/UNETAPI.md` | function numbers, capability bits, error codes |
| Apps | `src/apps/*.asm` | NETUP, NETCFG, NETPROBE, NETRESET, TCPTEST, UDPTEST, PING, WGET, NTP, TFTP, FTP, WTERM, TELNET (statically linked), UNETTEST (DLL consumer), RACETEST/DLSPEED (developer tools) |
| Bindings | `bindings/sdcc`, `bindings/tpascal` | C (SDCC) and Turbo Pascal wrappers for libman + UNET |
| Tests | `tools/test-*.sh`, `tools/*_vectors.asm` | host-side tests: the library is assembled with a scripted byte source (`TEST_READ_BYTE`) instead of the UART |

Build (`Makefile`, [`tools/build.sh`](https://github.com/witchcraft2001/sprinter_wifi/blob/main/tools/build.sh)):

- Each app is a single sjasmplus run: `sjasmplus -I src/include -I src/lib --raw=APP.EXE src/apps/app.asm`
  (`tools/build.sh:41-51`). The library is pulled in by `INCLUDE`. There is no linker. Optional
  `-DESP_AT_FORCE_221/222` selects a firmware dialect at build time (`tools/build.sh:9-20`).
- The DLL is built by `sprinter-mkdll` (the libman builder, a Python tool from a separate
  libman source tree). It assembles the source twice, at two different ORGs, and diffs the
  outputs to produce the relocation bitmap of the L1 container (`tools/build.sh:58-100`,
  header comment `unetesp.asm:17-36`).

### How DSS loads the DLL (libman)

- DSS has no DLL loader. libman is **consumer-side code** that the application embeds
  ([`docs/UNETAPI.md`](https://github.com/witchcraft2001/sprinter_wifi/blob/main/docs/UNETAPI.md),
  section "Loading and the calling convention"; `libman13.asm:1-23`).
- `l_load` (HL = file name, A = window 1/2/3):
  1. allocates a 2-page DSS memory block (`RST 10h`, `C=3Dh`) and maps its first page into
     window 3;
  2. opens the file through the DSS file API, current directory only;
  3. checks the L0/L1 header, relocates the image with the bitmap, and records the block in a
     64-entry `lib_table`;
  4. calls function 0 (`INIT`) (`libman13.asm:60-130`).
- `l_call` (HL = handle, B = function number) maps the DLL's page into its window with the
  explicit DSS `SETWIN1/2/3` calls and jumps to `base + 0x20 + 3*B`. The 32-byte L1 header comes
  first, then the JP table. The dispatcher then restores the caller's page
  (`libman13.asm:693-790`).
- Calling convention: arguments only in **A, DE, IX, IY** (HL and BC belong to the dispatcher).
  Results come back in A/DE/IX/IY. Status is **always in A** (`NERR_*`), and CF is not
  propagated. The library is not reentrant (`docs/UNETAPI.md`, "Register discipline").
- Placement rules: the DLL goes in window 1 or 2, **never window 3**, because the ISA UART is
  mapped into `0xC000` during every call. `INIT` refuses window 3 (`unetesp.asm:194-210`). Caller
  buffers must lie below `0xC000` and outside the DLL's window. At least about 256 bytes of free
  stack are needed.

### Exported entry points

The JP table is at `unetesp.asm:83-106`, and the numbers are in
[`src/include/unet.inc`](https://github.com/witchcraft2001/sprinter_wifi/blob/main/src/include/unet.inc)`:49-125`.

| # | Name | In -> Out |
|---|---|---|
| 0 / 1 | INIT / FINI | libman load / free hooks |
| 2 | GETCAPS | -> DE = caps, IX = ABI 0x0100 |
| 3 / 4 | NETINIT / NETDONE | bring the UART + ESP up / close all channels |
| 5 | CONNECT | A = channel, DE = host, IX = port string |
| 6 | SEND | A = channel, DE = buffer, IX = length -> DE = bytes sent |
| 7 | RECV | A = channel, DE = buffer, IX = max, IY = timeout ms -> DE = bytes received, IX = flags |
| 8 / 9 | CLOSE / STATUS | per channel; STATUS never touches the hardware |
| 10 | UDPOPEN | A = channel, DE = host, IX = remote port, IY = local port |
| 11 / 12 | RESOLVE / PING | via `AT+CIPDOMAIN` / `AT+PING` |
| 13 / 14 | RXPAUSE / RXRESUME | drop / raise RTS |
| 15 / 16 / 17 | GETINFO / LASTERR / SETOPT | `NET_*` environment variables, the tail of the last AT reply, options (cancel keys, RX trigger, send slice) |
| 18 / 19 | LISTEN / UNLISTEN | `AT+CIPSERVER` on one channel |
| 20-23 | reserved | return `NERR_NOTSUP` |

Each `API_x` wrapper calls `F_x` and then `API_RETURN`, which snapshots LASTERR on a non-zero
status (`unetesp.asm:111-190`). The capabilities are TCP, UDP, RESOLVE, PING, RXFLOW, MULTICHAN,
ASYNCSEND and LISTEN (`unetesp.asm:72`). A second backend, UNETRTL.DLL for the RTL8019A Ethernet
card, implements the same numbers in a sibling project (`docs/UNETAPI.md`, RTL appendix).

### Layers in `unetesp.asm`

1. **UNET API layer** (`unetesp.asm:83-1600`): argument and window checks (`CHECK_BUF_RANGE`,
   `CHECK_STRARG`), channel state machine (`CH_*`, `SET_CH_STATE`), and the asynchronous SEND
   transaction (`SETUP_ASYNC_MODE`, `RESOLVE_PENDING`).
2. **AT session layer** (`unetesp.asm:1788-2290`): `ENSURE_MUX` (`AT+CIPMUX=1`),
   `SWEEP_STALE_LINKS` (`AT+CIPCLOSE=5`), `OPEN_RETRY` / `MUX_OPEN` (`CIPSTART`, with a busy
   retry and a recovery ladder), `SEND_AT_BUSY`, and the response parsers (`PARSE_PING_MS`,
   `PARSE_CIPDOMAIN`).
3. **Host OS glue**:
   - `ENV_GET_STAGE` (`DSS_ENVIRON`, `unetesp.asm:2293-2304`), which reads `NET`, `NET_ESP_HW`,
     `NET_ESP_FW`, `NET_ESP_FLOW` and `NET_BAUD` published by NETUP;
   - `CHECK_CANCEL_IN_ISA` (`DSS_SCANKEY`, `unetesp.asm:2755-2803`).
4. **Library modules**, included at the end so that the BSS chain sits inside the image
   (`unetesp.asm:2811-2817`):
   - `util.asm` (cycle-counted delays);
   - `isa.asm`;
   - `esp_tcp.asm` built with `ESP_TCP_RX_DEFER` and `ESP_TCP_MUX` (`unetesp.asm:41-58`);
   - `esplib.asm`.

`NETINIT` shows the bring-up order (`unetesp.asm:243-326`):

1. check `NET=WIFI`;
2. select the RX profile from `NET_ESP_FW`, which must be 2.2.2;
3. `UART_FIND` (probe both ISA slots);
4. apply the flow mode and baud from the environment, then `UART_INIT`;
5. probe the ESP with `AT` and reset it once if it is silent;
6. `CIPCLOSE=5`, `CIPMUX=1`, then the best-effort `AT+CIPTCPOPT=5,-1,0,4000`.

Wi-Fi joining is **not** in the DLL. NETUP does it once (`AT+CWJAP`, DHCP or static) and
publishes the result as DSS environment variables.

### Applications

- **Statically linked** (no DLL): the twelve shipped tools in `tools/artifacts.sh`. Each one
  INCLUDEs `esplib`, `esp_tcp` and the other modules it needs.
- **DLL consumers**: `UNETTEST.EXE`, `RACETEST` (developer tool), the SDCC and Turbo Pascal demos
  ([`bindings/README.md`](https://github.com/witchcraft2001/sprinter_wifi/blob/main/bindings/README.md)),
  and third-party software. `docs/UNETAPI.md` mentions SpecTalkZX and games.

## UART layer (the part to swap)

### What it is on the Sprinter

- The ISA card's I/O space is reached as **memory**. `ISA_OPEN`:
  1. saves the page in window 3;
  2. writes `0x11` to port `0x1FFD`;
  3. maps page `0xD4` (ISA slot 1) or `0xD6` (slot 2) into window 3 via port `0xE2`;
  4. clears `PORT_ISA` (`0x9FBD`).

  `ISA_CLOSE` undoes this (`isa.asm:48-118`). An option (`ISA_RX_GUARD`) also disables
  interrupts while the window is open (`isa.asm:50-70`).
- 16550 registers sit at `0xC000 + 0x3E8` (`esplib.asm:16-33`): `RBR/THR`, `IER`, `IIR/FCR`,
  `LCR`, `MCR`, `LSR`, `MSR`, `SCR`. The crystal is 14.7456 MHz, so the divisor for
  115200 baud is 8 (`esplib.asm:69-71`).
- Access is `LD A,(HL)` / `LD (HL),E` with `HL = REG_x` inside an open window (`UART_READ`
  `esplib.asm:395`, `UART_WRITE` `:407`). **Polling only**: `IER = 0` (`UART_INIT`,
  `esplib.asm:171-235`). No UART interrupt is used.
- `UART_FIND` probes both ISA slots by checking that the high bits of IER are zero and that the
  scratch register reads back `0x55`/`0xAA` (`esplib.asm:129-165`).
- **Flow control**:
  - `UART_INIT` sets `MCR = AFE|RTS`, which is TL16C550 auto-RTS/CTS, with an RX FIFO
    trigger of 4 (`esplib.asm:227`);
  - NETUP configures the ESP with `AT+UART_CUR=<baud>,8,1,0,3`;
  - `RXPAUSE`/`RXRESUME` drop and raise RTS (`UART_RX_PAUSE` `:292`, `UART_RX_RESUME` `:319`);
  - RECV lowers RTS while it idles.

  The DLL's whole lossless-receive design (`docs/UNETAPI.md`, "Avoiding UART overrun") rests on
  the ESP stopping within a few byte times once RTS drops.
- **ESP reset** goes through the 16550's `OUT1` pin (`MCR_RST`), followed by a 2 s wait
  (`ESP_RESET`, `esplib.asm:683`).

### How cleanly it is separated

Direct uses of `REG_*`, `ISA_OPEN/CLOSE` or `PORT_UART_A` per file:

| File | `REG_*` | `ISA_OPEN/CLOSE` | Comment |
|---|---:|---:|---|
| `esplib.asm` | 32 | 19 | the intended home |
| `esp_tcp.asm` | 6 | 16 | hot path: `LD A,(REG_LSR)` / `LD A,(REG_RBR)` in the `+IPD` payload loop (`esp_tcp.asm:2608-2618`), reads with the window held open (`:2717-2725`, `:2800`, `:2923`) |
| `esp_tcp_multi.asm` | 0 | 9 | windows held across passive-receive reads |
| `unetesp.asm` | 1 | 3 | `SETOPT RXTRIG` writes FCR (`unetesp.asm:1439`); cancel check closes and reopens the window (`:2769-2794`) |
| `wcommon.asm` | 2 | 3 | LCR probe, window handling |
| apps | few | few | `telnet`, `wterm`, `ftp`, `dlspeed`, `zmodem` |

The design is "library-owned hardware, open the window once and poll registers inline". A ZiFi
back end needs:

1. **A register-access replacement.** ZiFi has 8-bit `IN`/`OUT` ports instead of memory:
   - `#C0EF`: RX count, 0..255, saturating;
   - `#C1EF`: TX free space;
   - `#BFEF` (any high byte `00..BF`): data;
   - `#C7EF`: command/error register.

   `ISA_OPEN`/`ISA_CLOSE` become no-ops. "LSR.DR" becomes "RX count > 0", "THRE" becomes "TX
   free > 0", and the `+IPD` loop can use `INIR` with `B` = count, capped at `#BF` (pattern from
   [`shared/z80/zifi_uart.asm`](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/shared/z80/zifi_uart.asm)`:123-144`).
   Initialization is `OUT #C7EF,#F1` (API 1), then `OUT #C7EF,#FF` and read the version, then
   clear the FIFOs
   ([zifi.md](https://github.com/tslabs/zx-evo/blob/master/pentevo/docs/ZiFi/zifi.md)).
   About 60 call sites need editing. A few `MACRO`s (`UART_RX_READY`, `UART_RX_BYTE`, ...) in
   place of the inline `LD A,(REG_x)` would make the back end a build-time choice.
2. **A flow-control replacement.** This is more than "physical access".
   - The ZiFi AVR talks to the ESP over USART0 at a **fixed 115200** baud
     ([`rs232.c`](https://github.com/tslabs/zx-evo/blob/master/pentevo/avr/current/rs232.c)`:93-100`
     sets UBRR0 once).
   - There is no RTS/CTS. The buffers are 512 bytes for ESP-to-Z80 and 256 bytes for Z80-to-ESP
     ([`rs232.h`](https://github.com/tslabs/zx-evo/blob/master/pentevo/avr/current/rs232.h)`:160-161`).
   - At 115200 a full 512-byte FIFO takes about 44 ms to fill. So `RXPAUSE`/`RXRESUME` cannot
     work, and `CAP_RXFLOW` has to be dropped. Lossless receive then requires ESP-AT **passive
     receive** (`AT+CIPRECVMODE=1` + `AT+CIPRECVDATA=<n>` with n <= about 400). That is what the
     original ZiFi client does
     ([`ORIGINAL_AT_COMMANDS.md`](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/ZiFi%20SPG/ORIGINAL_AT_COMMANDS.md)).
   - The library already has a passive path (`esp_tcp_multi.asm:235-661`, used by FTP), so this
     is a profile switch, not new code. NETUP must send `AT+UART_CUR=115200,8,1,0,0`, not `,3`,
     and must not change the baud.
3. **ESP reset.** ZiFi has no `OUT1`. Software `AT+RST` is the only reset (the AVR does not
   expose the ESP's reset line in the ZiFi register map).
4. **Timing.** `UTIL.DELAY_1MS` is a `BC=400` decrement loop (`util.asm:43-59`) tuned for the
   Sprinter's CPU clock. On TS-Conf (3.5/7/14 MHz) it needs a per-clock constant, or the TS
   frame counter. All timeouts are expressed in these ticks.
5. **Host OS glue.** These are not hardware, but they are Sprinter-specific:
   - the DSS environment (`NET_*`) and `NET.CFG` through the DSS file API;
   - `DSS_SCANKEY` for cancel;
   - DSS `APPINFO` for the DLL path;
   - libman itself (`DSS GETMEM/SETWIN`).

## Protocol and ESP firmware (old vs new)

### sprinter_wifi: stock ESP-AT, text protocol

- **Firmware.** ESP8266 ESP-AT **2.2.2** (2.2.1 tolerated by the EXEs, not by the DLL),
  [README](https://github.com/witchcraft2001/sprinter_wifi) "ESP-AT Firmware Baseline". The DLL
  pins 2.2.2 (`unetesp.asm:41`). The shipped image is
  `firmware/SprinterESP-AT-v2.2.2.0-runtime-flow-fix-full-2MB.bin`. It is a full 2 MB image built
  from Espressif's [esp-at](https://github.com/espressif/esp-at) `v2.2.2.0_esp8266`; the local fix
  is not published
  ([`firmware/FLASHING.md`](https://github.com/witchcraft2001/sprinter_wifi/blob/main/firmware/FLASHING.md)).
  The card itself is Roman Boykov's [SprinterESP](https://github.com/romychs/SprinterESP)
  (mirror [zxgit.org](https://zxgit.org/romych/SprinterESP)).
- **Commands sent** (counted over `src/`): `AT`, `ATE0`, `AT+GMR`, `AT+SYSSTORE` (`?` probe
  and `=0`), `AT+SYSLOG=1`, `AT+UART_CUR`, `AT+CWMODE(_CUR)`, `AT+CWJAP(_CUR)`, `AT+CWLAP`,
  `AT+CWLAPOPT`, `AT+CWDHCP(_CUR)`, `AT+CIPSTA(_CUR)`, `AT+CIPDNS(_CUR)`, `AT+CIFSR`,
  `AT+CIPMUX`, `AT+CIPSTART`, `AT+CIPSEND`, `AT+CIPCLOSE`, `AT+CIPSTATUS`, `AT+CIPMODE`,
  `AT+CIPRECVMODE`, `AT+CIPRECVDATA`, `AT+CIPDINFO`, `AT+CIPSERVER`, `AT+CIPSERVERMAXCONN`,
  `AT+CIPSTO`, `AT+CIPTCPOPT`, `AT+CIPDOMAIN`, `AT+PING`, `AT+SLEEP`.
- **Framing the DLL relies on**:
  - `+IPD,<link>,<len>:<bytes>` in mux mode;
  - `<link>,CONNECT` / `<link>,CLOSED`;
  - the `>` prompt, then `SEND OK` / `SEND FAIL`;
  - `busy p...`;
  - `ALREADY CONNECTED`;
  - `+CIPDOMAIN:` and `+PING:`.

  The DLL parses `+IPD` that is interleaved with command replies, defers it per channel (2 KB
  each), and treats `<id>,CONNECT` as success without waiting for `OK` (`docs/UNETAPI.md`,
  CONNECT / SEND).
- **Version probe.** NETUP sends `AT+SYSSTORE?`. `ERROR` means 2.2.1 (`_CUR` commands); `OK`
  means 2.2.2, followed by `AT+SYSSTORE=0`. The result is published as `NET_ESP_FW`.

### "Old ZiFi": TS-Labs ZiFi + ESP-01 with stock AT

- Hardware: the ZiFi register block of the ZX-Evo AVR firmware under TS-Conf (map above;
  [zifi.md](https://github.com/tslabs/zx-evo/blob/master/pentevo/docs/ZiFi/zifi.md)). An ESP-01
  hangs off the AVR's second USART.
- Software: the [HackerVBI/ZiFi](https://github.com/HackerVBI/ZiFi) internet client (`zifi.spg`,
  2016-2022). Its driver
  [`_esp/zifi_driver.asm`](https://github.com/HackerVBI/ZiFi/blob/master/_esp/zifi_driver.asm)
  uses ports `#C7EF`/`#BFEF`/`#C0EF`/`#C1EF` (`zifi_driver.asm:551-559`) and AT commands:
  - `ATE0`, `AT+GMR`, `AT+CWMODE_DEF=1`, `AT+CWAUTOCONN=0`, `AT+CIPMUX=0`;
  - `AT+CIPRECVMODE=1` as a probe, falling back to active `+IPD`;
  - `AT+CWJAP_CUR`, `CIPSTART`, `CIPSEND`, `CIPRECVDATA=1024`, `CIPCLOSE`.

  This is the same dialect family as sprinter_wifi, which is why the author calls the port
  "simple".
- Caveat: Espressif's ESP-AT 2.x factory image for ESP8266 is built for **2 MB** flash
  ([esp-at build guide](https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/Compile_and_Develop/How_to_clone_project_and_compile_it.html)),
  and the sprinter image is a full 2 MB. Many ESP-01/ESP-01S modules have 1 MB. A ZiFi user may
  therefore be on NonOS AT 1.x, which lacks `SYSSTORE`, `CIPTCPOPT` and `CIPSERVERMAXCONN` and
  has a different `CIPRECVMODE` story. A port has to either require 2.2.x on a 2 MB module, or
  carry the 2.2.1/1.x profile the EXEs already have. See the open questions.

### "New ZiFi": ESP32-S3 Zero with a native binary protocol

Source: [ZiFi-ESP32-S3-Zero](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero)
(PlatformIO/Arduino, ESP32-S3FH4R2, firmware `s3-native-0.6.94`), protocol in
[`docs/PROTOCOL.md`](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/docs/PROTOCOL.md)
and [`include/zifi/protocol.hpp`](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/include/zifi/protocol.hpp).
It plugs into the same ZiFi connector through an ESP-01S adapter. On the Z80 side the ZiFi
register block is unchanged. Only the bytes on the wire are different.

| Aspect | Old (AT) | New (native) |
|---|---|---|
| Wire format | CR LF text, `+IPD` binary inserts | frame `5A CMD LEN_L LEN_H DATA CSUM`, CSUM = XOR of CMD, LEN and DATA; payload <= 1024 (`protocol.hpp:8-10`) |
| Who drives | the ESP pushes data (active) or the host pulls it (passive) | always the host: a request frame, then a reply frame; `NET_RECV [wanted LE16]` returns `92 [eof][data <= 1023]` |
| Sockets | 5 links, TCP/UDP/SSL, a server | **one** outgoing TCP client (`NetClient`, `net_client.hpp:11-14`); no UDP, no listen, no second channel |
| Name resolution | `CIPDOMAIN`, or a name in `CIPSTART` | inside `NET_OPEN host\0 port` |
| Extras | none | `NET_HTTP_GET` (the ESP parses headers and HTTPS with a CA bundle, and returns `[status][code][Content-Length]`), `NET_NTP`, `NET_PING`, `NET_IP_CONFIG`, proxy, weather, OTA, FTP/SMB servers and a WC updater that call *back* into the Z80 through `VFS_*` requests |
| Wi-Fi config | `CWJAP` from the host | `WIFI_INI` (the whole `zifi.ini`, stored in the ESP's flash) or `WIFI_CONNECT ssid\0pass\0` |

So to implement the UNET contract on the new ZiFi, the ESP32 firmware needs new commands:

- several sockets and a channel id in `NET_*`;
- UDP open/send/recv;
- a listen/accept socket;
- a host-name resolve that returns an address;
- a peer-close notification or status.

The host side needs a new transport (frames instead of AT text) under the same UNET API.
That matches the author's remark: "substantial work on both sides". Alternatively, the ESP32-S3
could be flashed with Espressif's stock **ESP32-S3 ESP-AT**, and then it behaves like the old ZiFi.

## Building for TS-Conf (options)

### What exists in the TS-Conf software world

| Mechanism | Where | Applicable? |
|---|---|---|
| Sprinter-style DLL (libman) | DSS only | no: it needs DSS `GETMEM`/`SETWIN` and the DSS file API |
| **Wild Commander plugins** (`.WMF`) | WC (and [WC Improved](https://github.com/andrewinsidelazarev/Wild-Commander-Improved)): 512-byte header `WildCommanderMDL`, format `#0A`, page list, type byte; code is paged in at `#8000`; WC services through `CALL #6006` with the function number in A ([`wc_header.inc`](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/NTP%20Time%20Sync/src/wc_header.inc), [`wc_api.inc`](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/shared/z80/wc_api.inc)) | yes, for tools living inside WC; a plugin is a loadable *application*, not a shared library that other programs call |
| `.spg` executables | loaded by WC / TS-BIOS into pages | standalone programs; each carries its own code |
| TR-DOS (Evo-DOS virtual drive) | ROM | no loader: static code or a resident block at a fixed address |
| esxDOS (`.dot` commands, drivers) | needs a DivMMC/DivIDE automapper | not available: the TS-Conf FPGA provides Nemo IDE and Z-Controller SD, no DivMMC automap (see `../2026-09-27-tsconf/hardware-spec.md`) |
| NedoOS socket API (`OS_NETSOCKET`, ...; [`api_net.txt`](https://github.com/alfishe/NedoOS/blob/main/src/_sdk/api_net.txt)) with kernel drivers W5300 or ESPNET (`INETDRV`), or the userland [`_sdk/espnet.asm`](https://github.com/alfishe/NedoOS/blob/main/src/_sdk/espnet.asm) linked into each app | NedoOS kernels exist for ATM2, ATM3, Pentagon and ZX-Evo **BaseConf** ([`build_kernel_evo_esp.bat`](https://github.com/alfishe/NedoOS/blob/main/src/kernel/build_kernel_evo_esp.bat): `atm=1`, `INETDRV=0x02`) | only on BaseConf; NedoOS's ESPNET UART types are Kondratyev 16550 / ATM2 COM / ATM2IOESP, none of them the TS ZiFi FIFOs |

### Option A: statically linked library, back end chosen at build time (recommended first step)

- The `src/lib` modules get a UART back-end include: `uart_isa16550.asm` (today's code) or
  `uart_zifi.asm`. Selection is by `DEFINE` (`-DUART_ZIFI`), in the same style as
  `ESP_AT_FORCE_222`. Each TS-Conf program (an `.spg`, a `.WMF` plugin, a TR-DOS `.C`)
  INCLUDEs the library and is one sjasmplus run.
- An OS shim replaces the DSS glue: configuration from a file read through WC or the program's
  own FAT code (a `zifi.ini`-style file), a keyboard poll, and timing.
- Pros:
  - no loader;
  - smallest change to sprinter_wifi, whose apps already work this way;
  - dead code is dropped at assembly;
  - the host-side test harness (`TEST_READ_BYTE`) keeps working.
- Cons:
  - every program carries 8-12 KB of driver;
  - a driver fix means rebuilding every program;
  - no binary compatibility with a future TS-Conf DLL consumer.
- Already done this way:
  - HackerVBI ZiFi (`zifi_driver.asm` INCLUDEd into `zifi.asm`);
  - ZiFi-ESP32-S3-Zero (every plugin INCLUDEs `shared/z80/zifi_uart.asm` + `proto.asm`,
    assembled with `--inc=shared/z80`, as in `NTP Time Sync/build.bat`);
  - sprinter_wifi's own twelve EXEs;
  - NedoOS userland `espnet.asm` (the non-kernel build).

### Option B: resident module with the UNET jump table (a "TS-Conf libman")

- Keep the UNET ABI: function number, arguments in A/DE/IX/IY, status in A. Build the driver as
  one 16 KB image with the JP table at offset 0, assembled at a fixed window base. sjasmplus
  produces the binary, and an optional relocation bitmap can be made exactly as `sprinter-mkdll`
  does.
- A small loader (a `.WMF` of type "run once after WC init", like `NTPTIME.WMF`, or a few
  hundred bytes in each app) reads `UNETZIFI.BIN` into a fixed RAM page. TS-Conf has 4 MB. The
  page number goes into an agreed location, for example a word in a reserved page or a WC
  variable. A caller stub maps that page into window 1 or 2 through the TS page ports, then
  `CALL base+3*fn` and restore. This is libman's `l_call` with TS-Conf paging.
- Pros:
  - one driver binary for all programs;
  - C/Pascal bindings and UNET consumer code port unchanged, apart from the stub;
  - the same API could later get an ESP32-native back end (`UNETZN.BIN`) without rebuilding
    consumers.
- Cons:
  - TS-Conf has no memory manager, so the reserved page has to be agreed by convention and can
    clash with programs that use all of RAM;
  - the stub must save and restore the page registers and the ZiFi API mode;
  - WC plugins already own `#8000`, which leaves window 1 (`#4000`), the screen area in many
    programs.
- Precedent: the libman/DSS model itself. On TS-Conf, WC's paged plugin model plus its
  `#6006` API is the closest thing to a call gate. There is no known shared-library precedent
  on TS-Conf.

### Option C: NedoOS driver behind the existing socket API

- Add a fifth ESPNET/AT UART type, "TS ZiFi", to NedoOS (kernel `INETDRV`), or a separate
  kernel driver that speaks AT over ZiFi and exposes `OS_NETSOCKET`/`CONNECT`/`READ`/`WRITE`.
  UNET then becomes a thin userland adapter, or is dropped.
- Pros: every NedoOS network app (browsers, IRC, wget, `3ws`) works with no app changes.
- Cons:
  - NedoOS does not boot under TS-Conf in the current tree, so this only matters if a TS-Conf
    NedoOS kernel appears, or if ZiFi registers are reachable from BaseConf. Our
    `portdecoder_atm3.cpp` notes that the TS AVR firmware from 2016-04 maps ZiFi registers at
    `#C0..#CF` even under BaseConf (`DescribeNetwork`);
  - NedoOS's socket API is BSD-like, while UNET is channel-based.
- Precedent: NedoOS ESPNET 1.27 (kernel `src/kernel/espnet.asm`, `INETDRV=2`) and the W5300
  driver.

Recommendation for the reply to the author: **A now, B later.** A gives working TS-Conf builds
with no new infrastructure. B is the TS-Conf analog of the DLL and can be layered on the same
sources once the ZiFi back end is stable.

## What the emulator needs

Our ESP peers live in `core/src/emulator/io/serial/esp/`: `EspModule` base, `AtModule`
(Espressif AT), `EspnetModule` (NedoOS ESPNET 1.27), and `EspStack`.

1. **ZiFi itself (step N5).** TS-Conf currently reserves `#xxEF` and reads `#FF`
   (`portdecoder_tsconf.h:40,95-104`). We need the AVR model:
   - API mode register, `#C7EF`;
   - the RX FIFO (512 bytes) and TX FIFO (256 bytes), with `#C0EF`/`#C1EF` counts saturating
     at 255;
   - data at `#00EF..#BFEF`, reading from whichever FIFO was last selected by reading
     `ZIFR`/`RIFR`;
   - threshold/timeout interrupts `#C4EF..#C6EF`;
   - a fixed 115200 serial link to the attached `EspModule`, with no CTS/RTS: the module must
     see CTS as always asserted.

   The FIFO overflow behavior when the Z80 does not drain (bytes dropped at 512) must be
   emulated, because it is exactly what the passive-receive requirement protects against.
2. **sprinter_wifi on ZiFi needs no third personality.** It talks stock ESP-AT. But `AtModule`'s
   ESP8266 dialect is NonOS AT 1.7.4 (`atmodule.cpp:314-322` `AT+GMR` text). A sprinter_wifi
   port expects ESP-AT 2.2.x:
   - `AT+GMR` must report a `2.2.x` version;
   - `AT+SYSSTORE?` must answer `+SYSSTORE:1` / `OK`. Today it is a plain `OK`
     (`atmodule.cpp:370-372`), which happens to select the 2.2.2 profile;
   - `AT+CIPTCPOPT`, `AT+CIPSERVERMAXCONN`, `AT+SYSLOG`, `AT+CIPDNS`/`_CUR` and
     `AT+CWLAPOPT` are missing (they would answer `ERROR`; CIPTCPOPT and CIPSERVERMAXCONN are
     best effort in the DLL, `unetesp.asm:316-322,1488-1491`);
   - `busy p...` already exists (`atmodule.cpp:264`);
   - `_CUR`/`_DEF` suffixes are already folded (`atmodule.cpp:284-290`).

   Proposed change: a chip/dialect setting "ESP8266 ESP-AT 2.2" in `AtModule`, not a new class.
   The Sprinter ISA card can reuse the existing `Uart16550` with auto-flow plus `AtModule`, if
   the Sprinter ever gets an ISA bus.
3. **New ZiFi is a third personality: `ZifiNativeModule`.** Planning data:
   - **Frame**: `5A cmd lenL lenH data[len] csum`, with `csum = cmd ^ lenL ^ lenH ^ data...`.
     The sync byte is not part of the checksum. Payload is at most 1024. The parser resyncs on
     a bad checksum or after **500 ms** of silence inside a frame (`protocol.hpp:8-13`,
     `src/protocol.cpp:29-66`). The same format is used in both directions.
   - **Replies**:
     - `FE` ACK (empty) as soon as a long command is received, then the result frame;
     - `NET_RECV` (`12`) answers `92` directly, with no ACK;
     - `PING` (`04`) answers `F0`;
     - errors come as `EE <ASCII text <= 48>` *before* the result frame;
     - an unknown command gets `EE "unsupported:<CMD>"`.

     Result codes are the command with bit 7 set (`10` gives `90`, ..., `24` gives `A4`), plus
     `81/82/83/85..8E` for the system commands (`protocol.hpp:15-124`,
     `src/main.cpp:1450-1562`).
   - **Network subset worth emulating first** (all `src/main.cpp:1054-1131`):
     - `NET_OPEN host\0 [port LE16]` -> `90 [ok]`;
     - `NET_SEND bytes` -> `91 [ok]`;
     - `NET_RECV [wanted LE16]` -> `92 [eof][data]`, where `eof=1` only on a real close and
       zero data on a live socket means "nothing yet";
     - `NET_CLOSE` -> `93 [1]`;
     - `NET_HTTP_GET host\0 port path\0` -> `94 [ok][code LE16][length LE32]`, with the body
       pulled by `NET_RECV`;
     - `NET_IP_CONFIG` -> `A0`, `NET_NTP` -> `A2` (14 ASCII digits), `NET_PING` -> `A1`;
     - `WIFI_INI` / `WIFI_CONNECT` -> `83`/`81 [status][IPv4 x4]`;
     - `SYS_INFO` -> `82`, `ECHO` -> `00`.
   - **Flow control** is by request size alone. The host asks `NET_RECV` for at most what it
     can drain. Note that a full 1024-byte reply exceeds the AVR's 512-byte RX FIFO, so the
     real host must read while the frame streams in. The emulator should keep that hazard
     (byte loss on overflow) rather than hide it.
   - **Out of scope initially**: the reverse `VFS_*` channel (`40..5E`, the ESP sends requests
     to a WC plugin), FTP/SMB/OTA/WCU and the weather service. These need a Wild Commander
     plugin on the Z80 side and are not network-adapter features.
   - The TCP/HTTP work maps onto our existing `EspStack` / `VirtualNetwork`. HTTPS
     (`NET_HTTP_GET` to an https URL, `NET_OPEN` port 443) needs a TLS decision; see the open
     questions.

## Sources

| What | Link |
|---|---|
| sprinter_wifi repository (commit `909330f`, 2026-09-20) | [github.com/witchcraft2001/sprinter_wifi](https://github.com/witchcraft2001/sprinter_wifi) |
| DLL source | [`src/dll/unetesp.asm`](https://github.com/witchcraft2001/sprinter_wifi/blob/main/src/dll/unetesp.asm) |
| UNET contract | [`docs/UNETAPI.md`](https://github.com/witchcraft2001/sprinter_wifi/blob/main/docs/UNETAPI.md), [`src/include/unet.inc`](https://github.com/witchcraft2001/sprinter_wifi/blob/main/src/include/unet.inc) |
| UART / ISA layer | [`src/lib/esplib.asm`](https://github.com/witchcraft2001/sprinter_wifi/blob/main/src/lib/esplib.asm), [`src/lib/isa.asm`](https://github.com/witchcraft2001/sprinter_wifi/blob/main/src/lib/isa.asm), [`src/lib/esp_tcp.asm`](https://github.com/witchcraft2001/sprinter_wifi/blob/main/src/lib/esp_tcp.asm) |
| libman loader | [`src/lib/libman13.asm`](https://github.com/witchcraft2001/sprinter_wifi/blob/main/src/lib/libman13.asm) |
| Build | [`tools/build.sh`](https://github.com/witchcraft2001/sprinter_wifi/blob/main/tools/build.sh) |
| Firmware | [`firmware/FLASHING.md`](https://github.com/witchcraft2001/sprinter_wifi/blob/main/firmware/FLASHING.md), [espressif/esp-at](https://github.com/espressif/esp-at), [esp-at 2.2 (ESP8266) build guide](https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/Compile_and_Develop/How_to_clone_project_and_compile_it.html) |
| Bindings | [`bindings/README.md`](https://github.com/witchcraft2001/sprinter_wifi/blob/main/bindings/README.md) |
| SprinterESP card | [romychs/SprinterESP](https://github.com/romychs/SprinterESP), [zxgit.org mirror](https://zxgit.org/romych/SprinterESP) |
| ZiFi registers | [`pentevo/docs/ZiFi/zifi.md`](https://github.com/tslabs/zx-evo/blob/master/pentevo/docs/ZiFi/zifi.md), AVR [`rs232.h`](https://github.com/tslabs/zx-evo/blob/master/pentevo/avr/current/rs232.h), [`rs232.c`](https://github.com/tslabs/zx-evo/blob/master/pentevo/avr/current/rs232.c) |
| Old ZiFi client (commit `989a8e1`, 2022-09-07) | [HackerVBI/ZiFi](https://github.com/HackerVBI/ZiFi), [`_esp/zifi_driver.asm`](https://github.com/HackerVBI/ZiFi/blob/master/_esp/zifi_driver.asm), release note [hype.retroscene.org](https://hype.retroscene.org/blog/dev/391.html) |
| New ZiFi (commit `2e5ba83`, 2026-09-29) | [andrewinsidelazarev/ZiFi-ESP32-S3-Zero](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero), [`docs/PROTOCOL.md`](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/docs/PROTOCOL.md), [`include/zifi/protocol.hpp`](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/include/zifi/protocol.hpp), [`src/main.cpp`](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/src/main.cpp), [`shared/z80/zifi_uart.asm`](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/shared/z80/zifi_uart.asm), [`shared/z80/proto.asm`](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/shared/z80/proto.asm), [`ORIGINAL_AT_COMMANDS.md`](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/ZiFi%20SPG/ORIGINAL_AT_COMMANDS.md) |
| Wild Commander plugin format and API | [`wc_header.inc`](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/NTP%20Time%20Sync/src/wc_header.inc), [`wc_api.inc`](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/shared/z80/wc_api.inc), [Wild Commander Improved](https://github.com/andrewinsidelazarev/Wild-Commander-Improved) |
| NedoOS network | [alfishe/NedoOS](https://github.com/alfishe/NedoOS), [`_sdk/api_net.txt`](https://github.com/alfishe/NedoOS/blob/main/src/_sdk/api_net.txt), [`_sdk/espnet.asm`](https://github.com/alfishe/NedoOS/blob/main/src/_sdk/espnet.asm), [`kernel/build_kernel_evo_esp.bat`](https://github.com/alfishe/NedoOS/blob/main/src/kernel/build_kernel_evo_esp.bat) |
| TS forum ZiFi thread (context) | [forum.tslabs.info, ZiFi](https://forum.tslabs.info/viewtopic.php?f=26&t=682&start=200) |
| Our side | `../2026-09-30-nedoos-integration/network-adapters-catalog.md`, `../2026-09-27-tsconf/hardware-spec.md`, `core/src/emulator/io/serial/esp/atmodule.{h,cpp}`, `core/src/emulator/ports/models/portdecoder_tsconf.h`, `portdecoder_atm3.cpp` (`DescribeNetwork`) |

All links above returned HTTP 200 on 2026-10-02. No URL failed to open.

## Open questions

1. **What "new" means.** The identification of the "new ZiFi" with ZiFi-ESP32-S3-Zero is an
   inference: same connector, binary protocol, ESP32, active in 2026-09. Confirm with the
   author. A newer TS-Labs ZiFi hardware revision could also be meant.
2. **Old-ZiFi firmware baseline.** Which AT firmware do ZiFi users actually have on their
   ESP-01? If it is NonOS AT 1.x on 1 MB flash, the port needs the 2.2.1/1.x profile, or a
   reflash to 2.2.x on a 2 MB module. The DLL's hard 2.2.2 pin would then be the wrong default
   for TS-Conf.
3. **ESP reset on ZiFi.** Does any AVR firmware revision expose the ESP's reset or enable line?
   `zifi.md` lists none, so `AT+RST` is the only recovery.
4. **Passive receive and the 2.2.2 mutex stall.** sprinter_wifi relies on
   `AT+CIPTCPOPT ... 4000` against an ESP-AT 2.2.2 send stall. With passive receive on ZiFi,
   is the active-mode defer machinery (2 KB per channel) still needed? This decides how much of
   `esp_tcp.asm` a TS-Conf build must carry.
5. **Resident page convention for option B.** Is there an agreed "system" page on TS-Conf that
   WC, TS-BIOS and common `.spg` programs leave alone?
6. **NedoOS on TS-Conf.** No TS-Conf kernel exists in the current tree. Is one planned? If not,
   option C is moot for TS-Conf.
7. **Emulator TLS.** For `ZifiNativeModule`, HTTPS is terminated on the ESP. Should our virtual
   network do real TLS to the host, or only plain HTTP in v1?
