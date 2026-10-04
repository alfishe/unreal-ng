# Sprinter ESP Network Kit Usage

This package provides small Sprinter DSS utilities for the SprinterESP /
Sprinter-WiFi card with ESP8266 ESP-AT firmware.

## Recommended firmware

Use ESP-AT **V2.2.2.0**:
[`firmware/SprinterESP-AT-v2.2.2.0-runtime-flow-fix-full-2MB.bin`](../firmware/SprinterESP-AT-v2.2.2.0-runtime-flow-fix-full-2MB.bin).
The same image is available from the
[GitHub repository](https://github.com/witchcraft2001/sprinter_wifi/blob/main/firmware/SprinterESP-AT-v2.2.2.0-runtime-flow-fix-full-2MB.bin);
see [`firmware/FLASHING.md`](../firmware/FLASHING.md) before flashing.

ESP-AT V2.2.1 remains supported for a transition period, but V2.2.2.0 is more
stable with this package. On V2.2.1, corrupt downloads, missing bytes, and
slow transfers are known firmware limitations; update the ESP module before
diagnosing those symptoms as an application problem.

Normal builds automatically use the ESP-AT 2.2.1/2.2.2 command subset. After
`NETUP`, transfer utilities select their profile-specific UART FIFO setup from
published `NET_ESP_FW`; both profiles use explicit RTS pauses during slow
consumer paths, because automatic AFE alone overran real 2.2.2 `+IPD` bursts.
For a diagnostic build for known firmware, use `ESP_AT_PROFILE=2.2.1 make
build` or `ESP_AT_PROFILE=2.2.2 make build`. A forced executable prints its
profile in the banner. The default transfer path remains active `+IPD`, because
2.2.1 does not provide passive `CIPRECVMODE`/`CIPRECVDATA` receive.

## Utilities

- `NETCFG.EXE` shows current `NET.CFG` values.
- `NETCFG.EXE /W` edits and saves `NET.CFG`.
- `NETUP.EXE` initializes the ESP module and connects to Wi-Fi using `NET.CFG`
  stored beside `NETUP.EXE`, so it also works when invoked through `PATH`.
  It detects the 2.2.1/2.2.2 ESP-AT profile once per run, keeps Wi-Fi settings
  session-only, and publishes the selected profile as `NET_ESP_FW`.
- `TFTP.EXE host[:port] GET remote-file [-o local-name] [-y|-f]` downloads one
  file over TFTP. Existing output files require confirmation unless `-y` (or
  its alias `-f`) is used. TFTP has no resume.
- `TFTP.EXE host[:port] PUT local-file [-o remote-name]` uploads one file over
  TFTP.
- `FTP.EXE host[:port] file [-o output] [-u user] [-p pass] [-y|-f] [-r]`
  downloads one file over passive FTP using `RETR`. Without `-o`, the local name
  is the basename of the remote file. If the local file exists, FTP asks
  `[R]esume / [O]verwrite / [C]ancel`; `-y` (or `-f`) overwrites without
  prompting and `-r` resumes (appends, FTP `REST`) without prompting.
- `FTP.EXE host[:port] PUT local-file [-o remote-name] [-u user] [-p pass]`
  uploads one file over passive FTP using `STOR`. Without `-o`, the remote name
  is the basename of the local file.
- `FTP.EXE host[:port] [path] -l|-n [-u user] [-p pass]` logs in through
  ESP-AT multi-connection mode, enters passive mode and prints a `LIST` or
  `NLST` directory listing.
- `PING.EXE host` checks host reachability using ESP-AT `AT+PING`.
- `WGET.EXE url [-o output] [-y|-f] [-r]` downloads an http:// resource to a
  local DSS file. Without `-o`, the output name is derived from the URL path. If
  the file exists, WGET asks `[R]esume / [O]verwrite / [C]ancel`; `-y` (or `-f`)
  overwrites without prompting and `-r` resumes (appends, HTTP Range) without
  prompting.
- `NTP.EXE` sets DSS time over UDP NTP using the `NET_TZ`/`NET_NTP` values
  published by `NETUP.EXE`.
- `NETPROBE.EXE` checks low-level UART and ESP-AT firmware response. It is a
  diagnostic tool, not a network bring-up command.
- `NETRESET.EXE` resets and reinitializes the ESP module.
- `WTERM.EXE` opens an ESP-AT terminal for manual commands.
- `TELNET.EXE host[:port]` (or `TELNET.EXE host [port]`) opens an ANSI/VT100
  Telnet or raw TCP/PTY session. For a Telnet peer it negotiates BINARY mode;
  raw services receive no IAC control bytes. It automatically receives files
  when the remote side starts `sz`, or prompts for one local file to upload
  when the remote side starts `rz`. For Ymodem with Homebrew lrzsz, run
  `lsb --ymodem file` remotely and press Alt+D to download, or run
  `lrb --ymodem` and press Alt+U to upload. If upload always stops after block
  14, force the remote PTY to binary mode with
  `stty raw -echo; lrb --ymodem; stty sane`; block 15 contains the otherwise
  terminal-significant Ctrl-O byte. Alt+X closes the session; Esc
  aborts only an active transfer. For a BBS explicitly waiting for Ymodem-G,
  press Alt+G instead of Alt+D. Progress shows confirmed transferred KB.

Planned utilities include `CHAT.EXE` and `IRC.EXE`.

Each current utility also has a short standalone TXT reference file:
`NETCFG.TXT`, `NETUP.TXT`, `NETRESET.TXT`, `NETPROBE.TXT`, `TFTP.TXT`,
`FTP.TXT`, `PING.TXT`, `WGET.TXT`, `NTP.TXT`, `WTERM.TXT` and `TELNET.TXT`.

## Installation

The package is distributed as a ZIP archive or may be preinstalled with the OS.
The recommended standard location is:

```text
C:\WIFI
```

Keep all package programs, documentation and the runtime `NET.CFG` together in
that directory unless the OS distribution provides another system location.

For convenient use, add the network kit directory to the DSS `PATH` environment
variable. If it is not in `PATH`, change to the install directory before running
the utilities:

```text
C:
CD \WIFI
```

Future tools should use the same install convention and look for shared network
configuration in the common network kit location.

## First Run

1. Unpack the ZIP package to `C:\WIFI`, or use the OS-preinstalled copy.
2. Add `C:\WIFI` to `PATH`, or change to `C:\WIFI` before running the tools.
3. Run `NETCFG.EXE /W`.
4. Enter `SSID` and `PASS`.
5. Keep `DHCP=1` for normal home/router networks.
6. Save the configuration.
7. Run `NETUP.EXE`.
8. Run `PING.EXE example.com` to verify reachability.

Typical sequence:

```text
NETCFG.EXE /W
NETUP.EXE
PING.EXE example.com
WGET.EXE http://example.com -o INDEX.HTM -y
NTP.EXE
```

## Configuration File

Runtime settings are stored in `NET.CFG`. `NETUP.EXE` loads it from its own
directory, so keep the file in the network kit install directory, normally:

```text
C:\WIFI\NET.CFG
```

Important keys:

- `SSID` - Wi-Fi network name.
- `PASS` - Wi-Fi password, stored as clear text.
- `DHCP` - `1` for DHCP, `0` for static IP.
- `IP`, `GATEWAY`, `NETMASK` - used when `DHCP=0`.
- `DNS1`, `DNS2` - DNS servers.
- `TZ`, `NTP` - used by `NTP.EXE`.
- `BAUD` - UART speed used after `NETUP.EXE` configures ESP with
  `AT+UART_CUR`. Supported values: `230400`, `115200`, `57600`, `38400`,
  `19200`, `9600`. `230400` is valid for the TL16C550C/14.7456 MHz UART
  clock, but should be treated as a fast hardware-test mode until verified on
  your card. Use `57600` or `38400` if `115200` loses bytes on your setup.

Do not distribute a real `NET.CFG` with private Wi-Fi credentials.

## Recommended Workflow

Use this order during normal testing:

1. `NETCFG.EXE` - verify saved settings.
2. `NETUP.EXE` - connect to Wi-Fi.
3. `PING.EXE example.com` - verify ESP-AT ping support and host reachability.
4. `WGET.EXE http://example.com -o INDEX.HTM -y` - verify HTTP download.
5. `NTP.EXE` - set DSS time from ESP SNTP.
6. `TFTP.EXE server GET file -o FILE -y` - verify TFTP download.
7. `TFTP.EXE server PUT FILE -o file` - verify TFTP upload where the
   server permits writes.
8. `FTP.EXE server -l` - verify FTP login, passive mode and directory listing.
9. `FTP.EXE server PUT FILE -o file` - verify FTP upload where the server
   account permits writes.

Bundled batch examples:

- `CONNECT.BAT` runs `NETRESET.EXE`, `NETUP.EXE` and `PING.EXE 8.8.8.8`.
- `TFTPGET.BAT` and `TFTPPUT.BAT` show TFTP download/upload forms for
  `192.168.1.36`.
- `WGETTRD.BAT` downloads `KLAD026.zip` from tr-dos.ru.
- `FTPLIST.BAT` shows a directory listing on an FTP server at `192.168.1.1`.

Use this order when something is stuck:

1. `NETRESET.EXE`
2. `NETPROBE.EXE`
3. `NETUP.EXE`
4. `PING.EXE example.com`

## Diagnostic Notes

Network kit utilities do not clear the screen on startup. They continue
printing at the current DSS console cursor position, so they can be used in
batch logs and command sequences without erasing previous output.

`NETPROBE.EXE` sends `AT`, `ATE0` and `AT+GMR`. It now retries each command once
after an ESP reset. If `NETPROBE.EXE` fails after `NETUP.EXE` and `PING.EXE`
have already succeeded, the network path may still be fine; run `NETRESET.EXE`
and repeat `NETPROBE.EXE` for a clean firmware diagnostic.

`WTERM.EXE` is useful for manual ESP-AT checks. It attaches to the active
NETUP session and uses the published `NET_BAUD`/`NET_ESP_FLOW` values; it never
resets ESP or sends `AT+UART_CUR`. After a manual command changes module state
or leaves the stream confused, run `NETRESET.EXE` and then `NETUP.EXE`.

`PING.EXE` does not reset an ESP that was already brought up by `NETUP.EXE`.
When its initial `AT` check fails, it retries and then uses the ESP-AT `+++`
transparent-mode escape before checking `AT` again. This preserves the current
Wi-Fi association; a remaining failure prints the actual ESP response and
returns status `3`.

`NETRESET.EXE` uses the default 115200 startup speed after an ESP reset.
All other clients, including WTERM, never read `NET.CFG` themselves (the file
belongs to `NETUP.EXE`/`NETCFG.EXE`): they take the UART speed from the
`NET_BAUD` environment variable published by a successful `NETUP.EXE` run.
They also use `NET_ESP_FLOW=3` or `0` to reproduce NETUP's negotiated local
UART mode; clients do not resend `AT+UART_CUR` or toggle AFE during the live
session.

## Exit Codes

Utilities return a DSS process status in the exit code register used by
`DSS_EXIT`.

Common status codes for automation-friendly utilities:

- `0` - success.
- `1` - invalid command line or usage error.
- `2` - Sprinter-WiFi hardware was not found.
- `3` - ESP communication error, timeout, unsupported command, unreachable
  host, or unexpected ESP response.
- `4` - configuration error, for example missing or invalid `NET.CFG`.

Current utility-specific notes:

- `PING.EXE` returns `0` only when `+PING:<time_ms>` was received.
- `NETUP.EXE` returns `4` when `NET.CFG` is missing, unreadable or lacks SSID.
- `NETRESET.EXE` returns `0` on successful reset/reinitialization, `2` when
  hardware is not found and `3` on ESP communication failure.
- `WGET.EXE` returns `0` after a successful body download, `1` for invalid
  command line or URL, `2` when hardware is not found, `3` for ESP/TCP/HTTP
  errors and `5` for local output file errors.
- `NTP.EXE` returns `0` after DSS time is set, `2` when hardware is not found
  and `3` on ESP SNTP, response parse or DSS SETTIME failure.
- `TFTP.EXE` returns `0` after a successful download or upload, `1` for invalid
  command line, `2` when hardware is not found, `3` on ESP/UDP/TFTP protocol
  errors and `5` for local DSS file errors.
- `FTP.EXE` returns `0` after a successful download, upload or listing, `1` for
  invalid command line, `2` when hardware is not found, `3` on ESP/TCP
  communication errors, `4` on FTP server errors and `5` for local DSS file
  errors.
- `UNETTEST.EXE` (diagnostic; ships on the floppy, not the ZIP) returns `0`
  after the full DLL walk, `1` for invalid command line, `2` when hardware is
  not found or the DLL cannot load, `3` on communication/connect/send errors
  and `4` when the network is not configured.

## UNET network DLL

`UNETESP.DLL` exposes the network stack as a libman 1.3 / L1 dynamic library so
programs written in asm, C or Pascal can do TCP, UDP, resolve and ping through
one backend-agnostic interface (the same contract a future `UNETRTL.DLL` will
implement for the RTL8019A card). Bring the network up first (`NETUP`), then a
consumer loads the DLL with libman and calls the numbered functions. The full
contract - function numbers, register ABI, error and capability codes, and the
window/buffer rules - is in `UNETAPI.TXT` (`docs/UNETAPI.md`); the asm include
`src/include/unet.inc` and the C/Pascal bindings under `bindings/` are the
starting points for a consumer. `UNETTEST.EXE` is a ready smoke-test consumer.

Current `WGET.EXE` limitations:

- Supports plain `http://` only, not HTTPS.
- If the URL has no scheme, `WGET.EXE` assumes `http://` and prints a warning.
- Uses ESP-AT active `+IPD` receive. The target ESP-AT V2.2.1 firmware does not
  expose `AT+CIPRECVMODE` / `AT+CIPRECVDATA`.
- Uses a 16 KB receive buffer below the `0xC000` ISA window so several `+IPD`
  frames can be drained from UART before slow DSS file writes.
- If a `Content-Length` response closes early, retries the same URL with
  `Range: bytes=<downloaded>-` and appends the missing tail. Range retries are
  accepted only when the server returns HTTP `206 Partial Content`.
- Downloads HTTP 2xx responses.
- Follows absolute `http://` redirects up to five hops. HTTPS redirects are
  reported but cannot be downloaded.
- Detects chunked transfer encoding and gzip content encoding, then reports them
  as unsupported instead of writing undecodable data.

This allows DSS batch scenarios to run `PING.EXE router-or-host` before starting
another network command and stop when the status is non-zero.
