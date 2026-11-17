# 01. The Z80 <-> ESP32-S3 interface of the VDAC4 board (what an HLE ESP must do)

Research notes for the VDAC4 HLE design. Scope: everything the Z80 sees of the ESP on the TS-Conf SPI bus, as
used by the real Z80 programs (shared ZiFi driver, ESP App SPG, DOOM, R-Type, HMM2 launchers), plus what the
emulator already has.

Repositories: `VDAC4:` = github.com/andrewinsidelazarev/VDAC4, `DOOM-VDAC4:`, `R-Type-VDAC4:`, `HMM2-VDAC4:` = the same owner's repositories (default branch, 2026-11-15); `core/` paths are unreal-ng.

Path abbreviations:

| Tag | Path |
|-----|------|
| `DOCS` | `VDAC4:ESP32 S3 Zero N8R8/Docs` |
| `FW` | `VDAC4:ESP32 S3 Zero N8R8/Firmware` |
| `Z80` | `VDAC4:ESP32 S3 Zero N8R8/Network/shared/z80` |
| `ESPAPP` | `VDAC4:ESP32 S3 Zero N8R8/Network/ESP App SPG` |
| `DOOM` | `DOOM-VDAC4:` |
| `RT` | `R-Type-VDAC4:Source` |
| `HMM` | `HMM2-VDAC4:` |
| `CORE` | `core/src` (emulator) |

Layering (bottom to top), each section below is one layer:

1. SPI electrical / port level (#77, #57, timing quirks).
2. SPI Slave HD transactions: WRBUF / RDBUF of a 64-byte shared buffer, DMA commands 03/04/07/08.
3. The "link" (SpiLinkCore): request/response mailbox at bytes 0..59, status word 60..63, byte rings both ways,
   bulk/put/media/sector DMA extensions.
4. The frame protocol carried in the rings: `5A CMD LEN DATA CSUM` (same as the UART ZiFi protocol).
5. Commands: core (PING, SYS_INFO, ...), module loader (MOD_* / APP_*), PATH_*, then per-app commands and events.

---

## 1. Port level

| Item | Value | Source |
|------|-------|--------|
| #77 (Z-Controller config) bit 4 | ESP /CS. `#13` = ESP selected, SD deselected (bit 1 = 1); `#03` = nothing selected | `DOCS/VDAC4.md:31-32`, `Z80/zifi_spi.asm:92-94` |
| #77 = `#02` | `SPI_HOST_MASTER`, written first by `ZiFi_Init` | `Z80/zifi_spi.asm:92,148` |
| #77 bit 0 | always 1 except `SPI_HOST_MASTER`; a value with bit 0 = 0 and bit 7 = 1 must never be written (ESP master mode is not supported on VDAC4) | `Z80/zifi_spi.asm:88-91`, `DOCS/VDAC4.md:190-195` |
| #77 = `#07` | FT812 selected (bit 2, active high; same as VDAC2). Used by the Z80 launchers to write FT812 RAM_G directly (HMM2/R-Type E6) and by the ESP App SPG trail diagnostics | `HMM/launcher/graphics.asm:1-80` (FtOn, `out (#57)` RAM_G address), `CORE/emulator/platforms/tsconf/vdac2card.h:106-107` |
| #57 write | send one byte | `DOCS/VDAC4.md:1158-1166` |
| #57 read | published HDL (SCK 14 MHz): returns the byte of the PREVIOUS exchange and starts a new exchange sending #FF. With the "spi28" Tang firmware (SCK 28 MHz) it returns the byte of the CURRENT exchange. The driver is written to tolerate both: it skips one or two junk bytes before RDBUF data (`last=02/3C/8` = one junk byte, `/16` = two) | `DOCS/VDAC4.md:66-82,169-173,1158-1166` |
| SPI mode | mode 0; ESP drives MISO on the rising SCK edge (`rsck_data_out = 1`, "MISO early") | `DOCS/VDAC4.md:1168-1173`, `FW/src/spi_link.cpp` (startDriver, `GPSPI2.slave.rsck_data_out`) |
| Slave config | SPI2 Slave HD, 8-bit command, 8-bit address, 8 dummy bits, queue_size 1, DMA auto, `max_transfer_sz = kMediaMax + 4` | `FW/src/spi_link.cpp:500-530` |

HLE consequence: the emulator's `ZControllerSpi` already implements "previous exchange" reads
(`CORE/emulator/io/spi/zcontrollerspi.h:13-18`), so the driver's 14 MHz path is the one that will run. Only
WRBUF / RDBUF / 03 / 04 / 07 / 08 are legal SPI commands; anything else is ignored by the slave
(`DOCS/VDAC4.md:1224-1228`).

## 2. SPI Slave HD transactions

Every transaction is one CS-low window (#77 = #13 ... #77 = #03).

| Cmd | Name | Bytes on MOSI | Meaning | Source |
|-----|------|---------------|---------|--------|
| `01` | WRBUF | `01 addr dummy data...` | write the 64-byte shared buffer from `addr` | `Z80/zifi_spi.asm:96`, `DOCS/VDAC4.md:1175-1222` |
| `02` | RDBUF | `02 addr dummy` then reads | read the shared buffer from `addr` | `Z80/zifi_spi.asm:97,488-526` |
| `04` | RDDMA | `04 dummy?` then DMA SPI->RAM | master reads the queued 512-byte bulk block (or media window) | `Z80/zifi_spi.asm:539,778` |
| `08` | RD_DONE | `08` (own CS window) | end of a DMA read | `Z80/zifi_spi.asm:541,783` |
| `03` | WRDMA | `03 dummy?` then DMA RAM->SPI | master writes a block (echo or put) | `Z80/zifi_spi.asm:538,550` |
| `07` | WR_DONE | `07` (own CS window) | end of a DMA write | `Z80/zifi_spi.asm:540,554` |

The exact number of address/dummy bytes before DMA data of 03/04: see `Z80/zifi_spi.asm:528-787` (BulkOpen); the
driver sends the command, then three #57 reads before starting DMA on bulk read (`DOCS/VDAC4.md:1256-1275`).

Evo DMA ports used (all #xxAF):

| Port | Function | Source |
|------|----------|--------|
| `#1AAF/#1BAF/#1CAF` | source address L / H / page | `Z80/zifi_spi.asm:528-787`, `CORE/emulator/ports/models/portdecoder_tsconf.cpp:448-453` |
| `#1DAF/#1EAF/#1FAF` | destination address L / H / page | same |
| `#26AF` | DMA_LEN: words per block - 1 (512-byte "line") | same, `RT/Launcher/media.asm:105-121` |
| `#27AF` | DMA_CTRL: `#01` RAM->RAM, `#02` SPI->RAM, `#82` RAM->SPI; read: bit 7 = busy | `CORE/.../portdecoder_tsconf.cpp:737-740`, `CORE/emulator/platforms/tsconf/tsconfdma.h:25,116-121` |
| `#28AF` | DMA_NUM: blocks - 1 | `RT/Launcher/media.asm:114-119` |

The Z80 sweeps (invalidates) its TS-Conf CPU cache after a DMA into RAM (`Z80/zifi_spi.asm` Sweep, inside
528-787). The emulator's DMA already reads SPI with "previous exchange" semantics
(`CORE/emulator/platforms/tsconf/tsconfdma.h:48-52`) through `PortDecoder_TSConf` (`portdecoder_tsconf.cpp:73-86`).

## 3. The link (SpiLinkCore) on the 64-byte buffer

Implemented as portable logic: `FW/include/zifi/spi_link_core.hpp` (309 lines) + `FW/src/spi_link_core.cpp` (478
lines); `SpiLinkCore::handleRequest(request, response, length, BulkState, bulkBuffer, PutInput)`. It has no
ESP-IDF dependencies and is already compiled for PC tests (`FW/tests/spi_link_host.cpp`). Strong candidate for a
direct port into the emulator.

### 3.1 Buffer layout

| Bytes | Content | Source |
|-------|---------|--------|
| 0..59 | exchange area (`kTurnSize = 60`) | `FW/include/zifi/spi_link_core.hpp:87` |
| request | `C3 tid flags len room data[len<=54] sum` | `spi_link_core.hpp:97,89`; `DOCS/VDAC4.md:1175-1222` |
| response | `3C tid status len accepted data[len<=54] sum` | `spi_link_core.hpp:98` |
| checksum | sum of all message bytes including the sum byte = #FF (sum = ~sum(rest)) | `DOCS/VDAC4.md:1175-1222` |
| 60 | `5A` nothing for the Z80 / `A5` data pending | `spi_link_core.hpp:90,99-100` |
| 61 | `D7` magic | `DOCS/VDAC4.md:1175-1222`, `RT/Launcher/media.asm:26-28` |
| 62 | version: docs say 01, then 2 since 0.1.42; code `kVersion = 3` | `spi_link_core.hpp:105`, `DOCS/FAST_MEDIA_DMA.md:3-6` |
| 63 | boot nonce (changes per ESP boot) | `FW/src/spi_link.cpp` publishState, `DOCS/VDAC4.md:1175-1222` |

- The Z80 reads only from a marker address: 0 (C3/3C) or 60 (5A/A5). The marker bytes have inverse nibbles so a
  stuck line cannot fake them (`DOCS/VDAC4.md:1175-1222`).
- `room` (Z80's free space for ESP->Z80 bytes) is clamped to 54 by the ESP.
- The ESP writes the response bytes 4..59 first and the 4-byte head (with the `3C` marker) last
  (`FW/src/spi_link.cpp:776-779`, also 693-695). Until then the Z80 sees its own `C3` at byte 0 and keeps polling.
- A request with a repeated `tid` gets the previous response again (lost-response retry); flag `#80` (new session)
  overrides and resets the link (`DOCS/VDAC4.md:1175-1222`).
- ESP side processing is triggered by the end of a WRBUF transaction to the buffer (`cb_buffer_rx =
  onRequestWritten`, `FW/src/spi_link.cpp:212,525`); the task then reads 60 bytes, runs `handleRequest`, writes the
  response, republishes the state word (`FW/src/spi_link.cpp:626-780`).

### 3.2 Request flags / response status

| Bit | Request flag | Response status | Source |
|-----|--------------|-----------------|--------|
| 01 | DROP (drop pending ESP->Z80 data) | MORE (more data waiting) | `spi_link_core.hpp:109,153` |
| 02 | BULK (verdict of the last bulk echo / ready for bulk) | BULK (a 512-byte block is queued for 04) | `:111,155` |
| 04 | PUT (Z80 wants to write a block; byte 4 = XOR of the block) | BULK_STALE | `:113,157` |
| 08 | PUT_DONE | ECHO_BAD | `:115,160` |
| 10 | MEDIA (control bytes for a raw media DMA up to 8192) | PUT_READY | `:119,162` |
| 20 | SECTOR (VDAC4 VFS sector queue, 0.1.128) | PUT_OK | `:139,164` |
| 40 | RAWOUT (raw window ESP->Z80, 0.1.134) | PUT_BAD | `:148,166` |
| 80 | NEW_SESSION | PUT_CAPABLE | `:124,168` |

Sizes: `kBulkSize = 512`, `kMediaMax = 8192`, `kSectorSlots = 4`, `kBulkAttempts = 3` (bulk disabled until next
session after 3 echo failures) (`spi_link_core.hpp:120,150,170,172`). Rings: Z80->ESP input 16384 bytes, ESP->Z80
4096 bytes (`FW/include/zifi/spi_link.hpp:159-160`).

### 3.3 Normal exchange (Z80 driver)

`Z80/zifi_spi.asm` (951 lines): `ZiFi_Init` 146-184 (master, wait for magic, new session + drop), `ReadBurst`
207-278, `Push` 464-485, `ReadState/OpenRead` 488-526, `Open/Send/Turn` 790-924.

One "turn": WRBUF the request at address 0 -> poll RDBUF at 0 until `3C` with the same tid -> check sum -> copy
`data` into the frame parser. The Z80 also polls byte 60 to see whether the ESP has data (`A5`) without sending
anything.

### 3.4 Bulk read (ESP -> Z80, 512 bytes via DMA)

`DOCS/VDAC4.md:1256-1275`, `Z80/zifi_spi.asm:528-787`.

1. A response carries status BULK: a block is queued in the slave DMA (`FW/src/spi_link.cpp:704-723`).
2. Z80: `04` + three #57 reads + DMA SPI->RAM (512) + separate `08`.
3. Z80 echoes it back: `03` + DMA RAM->SPI + `07`.
4. Next request carries flag 02; the ESP compares echo vs original (`spi_link.cpp:654-674`) and reports
   ECHO_BAD or accepts. After 3 bad echoes bulk is off for the session.

### 3.5 Put (Z80 -> ESP, block via DMA)

`DOCS/VDAC4.md:1302-1326`.

1. Request with flag 04, byte 4 = XOR of the block -> response PUT_READY (receive DMA armed,
   `spi_link.cpp:749-764`).
2. `03` + DMA RAM->SPI + `07`.
3. Request with flag 08 -> PUT_OK / PUT_BAD. Up to `LINK_PUT_TRIES = 3` (`Z80/zifi_spi.asm:130,733`).

### 3.6 Media (raw DMA up to 8192 bytes, protocol version >= 2 and PUT_CAPABLE)

`DOCS/FAST_MEDIA_DMA.md:3-43`, Z80 side `RT/Launcher/media.asm:18-152` (R-Type copy of the player's
`fast_media.asm`).

- Probe: `SpiPutCan` (PUT_CAPABLE seen) and state word `[61]=D7`, `[62]>=2` (`media.asm:18-35`).
- Request flag `10` with control data (no checksum on the payload):
  - `31 LEN16` -> AUDIO_DATA/app data (in R-Type and HMM2 this is the active ELF's file-reply command, not PCM:
    `media.asm:39-54`, `HMM/main/hmm_io.c:332`).
  - `6A LEN16 OFF32` -> MOD_PUT (`media.asm:56-91`).
- Response PUT_READY -> DMA_NUM = floor((LEN-1)/512) lines from page 56 `#C000` -> `07` -> request PUT_DONE ->
  PUT_OK (`media.asm:110-129`). Retries 4096 x ~512-iteration pause (`media.asm:77-78,134-152`).
- Inside the ESP the media block becomes an internal frame `A6 CMD LEN ...` (`kMediaSync = 0xA6`,
  `FW/include/zifi/protocol.hpp:12`; `DOCS/FAST_MEDIA_DMA.md:34-43`). Commands routed to the app by
  `appMediaRange` (`protocol.hpp:33-38`).
- Executable ELF chunks normally go as checked frames; media is for audio and resources
  (`DOCS/FAST_MEDIA_DMA.md:40-43`); R-Type nevertheless uses media MOD_PUT for the ELF when enabled.

### 3.7 Timeouts and retry counts

| Constant | Value | Side | Source |
|----------|-------|------|--------|
| `LINK_POLLS` | 2048 polls per turn | Z80 | `Z80/zifi_spi.asm:125` |
| `LINK_ATTEMPTS` | 16 turns (~0.4 s at 14 MHz) | Z80 | `:126`, `DOCS/VDAC4.md:1230-1234` |
| `LINK_PUSH_TRIES` | 2048 | Z80 | `:128` |
| `LINK_PUT_TRIES` | 3 | Z80 | `:130` |
| `LINK_INIT_TRIES` | 16384 | Z80 | `:133` |
| `WaitLong B` | B x 65535 Proto_WaitCmd polls (poll counts, not ms) | launchers | `RT/Launcher/rtype.asm:1002-1018` |
| frame timeout | 500 ms unfinished frame dropped | ESP | `FW/include/zifi/protocol.hpp:20` |
| ACK re-send | 0.5 s | ESP | `DOCS/PROTOCOL.md:332-338` |
| `kPeerQuietMs` | 5000: Z80 silent -> stop waiting for ring room | ESP | `FW/src/spi_link.cpp:42` |
| `kReserveLimitMs` | 30000 | ESP | `FW/src/spi_link.cpp:44` |
| `kBulkLoadMs` | 20 | ESP | `FW/src/spi_link.cpp:50` |
| app request timeout | 10 s per E4/E5/E6 (app gives up, "SD timeout") | ESP app | `HMM/main/hmm_io.c:153,314`, DOOM `main/app.c` remote_read |
| app idle `kAbandonedMs` | 5000: app with no commands for 5 s may be unloaded when the cache needs room | ESP | `FW/src/apps/module_app.cpp:154,447-452` |

## 4. Frame protocol (inside the rings)

| Item | Value | Source |
|------|-------|--------|
| Frame | `5A CMD LEN_L LEN_H DATA[LEN] CSUM`, CSUM = XOR of CMD, LEN_L, LEN_H and data | `DOCS/PROTOCOL.md:9-16` |
| Max payload | 1024 (`kMaxPayload`), events at most 63 bytes (`kMaxEventPayload`) | `FW/include/zifi/protocol.hpp:23` |
| Padding | SPI link pads odd-length frames with one `00` (so frames start on even bytes for word DMA); Z80 skips bytes outside frames while searching for 5A | `FW/include/zifi/byte_link.hpp:34-38`, `FW/src/z80_transport.cpp:72-83` |
| Error | `EE <text<=48>` before the result; unknown command -> `EE "unsupported:XX"` | `FW/src/z80_transport.cpp:85-93`, `FW/src/main.cpp:1972-1975`, `DOCS/PROTOCOL.md:651` |
| ACK | `FE` | `DOCS/PROTOCOL.md:18-57` |
| While a VFS request waits for the Z80 | only PING and SYS_RESET are served, other commands dropped | `DOCS/PROTOCOL.md:332-338` |

Core dispatch on the ESP: `Application::handle` (`FW/src/main.cpp:1863-1977`): ECHO 00, PING 04 -> F0, GET_STEP
05, SYS_INFO 02, network commands (ACK + network core), SYS_RESET 0F (ACK, 100 ms, restart:
`main.cpp:1771-1776`), SYS_CLOCK, PATH_GET 2A / PATH_SET 2B (`main.cpp:1810-1856`), TANG_READ / TANG_WRITE,
VDISK_*, LINK_PROBE 3E; everything else goes to `dispatchToApps` (`main.cpp:622-640`), which includes
`ModuleApp`. Main loop `pollLink` (`main.cpp:1979-2018`): module app FT-detach poll, VFS bridge, network
responses/events, then one Z80 command.

## 5. Module loader: ELF transfer, cache, start, stop

Commands (`FW/include/zifi/protocol.hpp:139-143`, `DOCS/VDAC4.md:344-350`; handler
`FW/src/apps/module_app.cpp:328-372`):

| Cmd | Name | Request payload | Reply | Handler |
|-----|------|-----------------|-------|---------|
| `69` | MOD_QUERY | `[flags: bit0 resource][size LE32][FAT time LE32][name 8.3, 0]` | `E9 [0 cached / 1 send / 2 no room / 3 bad name][free LE32]` | `module_app.cpp:406-501` |
| `6A` | MOD_PUT | `[offset LE32][data]` (1016 per frame; up to 8192 via media; offset must equal bytes received so far) | `EA [1/0][received LE32]` | `:575-588` |
| `6B` | MOD_COMMIT | - | `EB [1/0]`; validates the ELF, writes it to LittleFS (seconds) | `:591-635` |
| `6C` | APP_START | `[name, 0]` | `EC [result int8][name,0][version,0]`; `0` = OK, `FE` (-2) = ZM_NO_MEMORY | `:866-895`, `startApp :975-1037` |
| `6D` | APP_STOP | - | `ED [1]` (also when no app runs) | `stopApp :1039-1138` |
| `2A` | PATH_GET | `[volume serial LE32][name, 0]` | `AA [1/0][cluster LE32]` | `FW/src/main.cpp:1810-1832` |
| `2B` | PATH_SET | `[serial LE32][cluster LE32][name, 0]` | `AB [1/0]` | `main.cpp:1834-1852` |

- Cache: key = name + size + FAT time stamp; up to 24 files in `/littlefs/zm`; limits module 2 MiB, resource
  7 MiB (`module_app.cpp:148-154`). The stamp the launchers send is the directory entry's write time/date
  (`DOOM/launcher/doom.asm:81-87`, `HMM/launcher/hmm2.asm:140-143`: `#3868+22`), so an unchanged file on the SD is
  never re-sent. ESP App SPG takes it from the ELF mtime at build time (`ESPAPP/prep.py`).
- Delivery order: MOD_QUERY -> (if 1) MOD_PUT chunks from offset 0 -> MOD_COMMIT -> APP_START
  (`DOCS/VDAC4.md:318-342`). WC plugin waits 15 / 60 / 15 s for these.
- Before APP_START every launcher sets `VCONFIG #00AF = 4` (FT812 monitor, "M_SEL") and after APP_STOP back to 0
  (`ESPAPP/esp_app.asm`, `DOOM/launcher/doom.asm`, `RT/Launcher/rtype.asm:992-996`, `HMM/launcher/hmm2.asm:306-308`).
- The app gets Z80 commands `command_first..command_last` from its `zm_app_t` and answers with `core->reply`
  (valid only inside a command) and sends ESP->Z80 events with `core->event` (<= 63 bytes)
  (`module_app.cpp:1463,1472`). App descriptors: DOOM `{..,"doom","1.1",0x70..0x76}` (`DOOM/main/app.c`), HMM2
  `{..,"hmm2","0.1",0x31,0x77,...}` (`HMM/main/hmm_app.c:124`). R-Type ESP source not inspected (`RT/ESP`);
  the launcher uses 31, 70-7A.
- Auto unload: `ModuleApp::pollCore1` reads Tang registers every 100 ms; when the Tang personality returns from the
  FT812 core (2) to the TS-Conf core (1) with M_SEL = 0 after having been selected, the app is stopped
  (`module_app.cpp:375-386`, `FW/include/zifi/ft_detach_guard.hpp:12-25`; Tang core falls back 3 s after the
  last FT exchange with M_SEL = 0: `DOCS/VDAC4.md:1874-1878`).
- App exit protocol: app state in reply F3 becomes 3 (quit) or 255 (error); HMM2 calls `core->app_done()` after
  that F3 reply (`HMM/main/hmm_io.c:419-431`); the launcher then sends APP_STOP.

## 6. SD ownership

| Storage | Owner | How the app reaches it | Source |
|---------|-------|------------------------|--------|
| Evo SD card (on the same Z-Controller SPI, #77 bit 1) | the Z80 only (launcher's FAT32 driver at `#3900`, FILEX) | app-specific events E4 / E5 / E6 / E7 that the launcher serves | `HMM/launcher/hmm2.asm:48-57`, `DOOM/launcher/files.asm` |
| Tang microSD | ESP (`tang_sd`) | ABI `sd_*` calls (`coreSd*`) | `FW/src/apps/module_app.cpp:2004-2160` |
| Tang/USB as WC VFS device 6 | ESP serves sectors to WC (SECTOR flag) | `sd2_*` / VDISK_* | `spi_link_core.hpp:139`, `FW/src/main.cpp:1897-1904` |
| ESP LittleFS | ESP | module cache, path.cfg | `DOCS/VDAC4.md:352-359` |

So for the three games the ESP never touches the Evo SD; the HLE must forward the app's E4/E5/E6 events to the Z80
and take 71/74/76/77 answers.

## 7. Per-program command sets

### 7.1 ESP App SPG (generic launcher, `ESPAPP/esp_app.asm`, 358 lines)

ZiFi_Init -> MOD_QUERY flags 0 (`:46-87`, WaitLong 8) -> MOD_PUT 1016-byte chunks from pages #20+ (`PutChunk
:253-332`, WaitLong 4) -> MOD_COMMIT (64) -> VCONFIG 4 -> APP_START (16, result 0) -> wait any key -> APP_STOP (4)
-> VCONFIG 0 -> halt. No input forwarding, no return to WC (reset).

### 7.2 DOOM (`DOOM/launcher/doom.asm` 549 lines, `files.asm`, `pathcfg.asm`; ESP `DOOM/main/app.c`)

| Dir | Cmd | Payload | Reply / note |
|-----|-----|---------|--------------|
| Z->E | 6D | APP_STOP first (b=32) | ED |
| Z->E | 2A / 2B | PATH_GET / PATH_SET, name "DOOM", serial = BPB+67 | AA / AB |
| Z->E | 69/6A/6B/6C | "DOOM.ELF", 1016-byte chunks via FILEX ReadAt | E9/EA/EB/EC (FE = no memory) |
| Z->E | 70 | WadConfig `[wad size LE32][name, 0]` | F0 [1] |
| E->Z | E4 | `[seq16][off32][len16 <= 1016]` (WAD read) | Z80 answers 71 `[seq16][count16][data]` -> F1 |
| E->Z | E5 | `[seq16][op][name13][other13][off32][count16]` (35 bytes); ops 0 stat, 1 read, 2 create/trunc, 3 write, 4 rename, 5 delete, 6 sync | 74 `[seq16][status][count16][size32][data]` -> F4; write data pulled by 76 `[seq16][off16]` -> F6 `[seq16][ok][off16][count16][data<=248]` |
| Z->E | 75 | every 256 run-loop passes: `[kempston & 31][mouse buttons #FADF][X #FBDF][Y #FFDF][PS/2 scan codes <= 24]` | F5 |
| Z->E | 73 | every 32 SendKeys: status | F3 `[state][1][frames LE32][error str]`; 0/1/2 running, 3 quit, 255 error |

PS/2 source: Gluk `#EFF7 = #80`, `#DFF7` register select, `#BFF7` data; init reg `#0C = 1`, `#F0 = 2`, queue reg
`#F0` (`RT/Launcher/rtype.asm:610-636`, same in DOOM/HMM2). ESP side: keys stale after 500 ms; I_Quit sets state 3.
Stop: APP_STOP (b=64), VCONFIG 0, halt.

### 7.3 R-Type (`RT/Launcher/rtype.asm` 1109 lines, `media.asm`, `prepare.asm`)

Own `zifi_spi.asm` (1072 lines) / `proto.asm` (914 lines) variants with `ZIFI_SPI_DMA` on unless `HSD_NO_DMA`.

| Dir | Cmd | Payload | Reply / note | Source |
|-----|-----|---------|--------------|--------|
| Z->E | 7A | "who are you" | FA, 9 bytes `"RTYP"[state][build LE32]`; match = reuse running app (skip upload), wait DE=5000 polls | `RT/Launcher/prepare.asm:145-185` |
| Z->E | 6D | APP_STOP when not reusing | ED len 1 = `01` else error E1 | `rtype.asm:937-959` |
| Z->E | 69..6C | RTMEM.ELF helper embedded in SPG page #39, 512-byte chunks | E9/EA/EB/EC | `prepare.asm:18-125` |
| Z->E | 7B | helper: free PSRAM | FB [1] | `prepare.asm:113-121` |
| Z->E | 69..6C | "RTYPE.ELF", 1016-byte frames or 8192-byte media MOD_PUT (page 56) | E9/EA/EB/EC | `rtype.asm` (to 470), `media.asm:56-91` |
| Z->E | 70 | `[DAT size LE32][name "RTYPE.DAT", 0]` | F0 [1] | `rtype.asm:883-916` |
| E->Z | E4 | `[seq16][off32][len16 <= 32768]` | Z80 streams 71: short `[seq16][count16][data<=1016]` (len <= 1016) or `[seq16][at16][count16][data<=1014]`; or via media cmd 31 `[seq16][at16][count16][data<=7680]` from page 56 (odd start: 1 byte by frame first) | `rtype.asm:645-854`, `media.asm:39-54` |
| E->Z | E6 | graphics: `[seq16][DAT off32][RAM_G addr32][len16]` | Z80 reads SD and writes FT812 RAM_G itself over SPI (#77=#07), then 77 `[seq16][status]` -> F7 | `rtype.asm:1101` (graphics.asm), see 7.4 |
| Z->E | 75 | 4 bytes + PS/2 when changed or heartbeat (32), at most one pending until F5; joystick bits incl. 5 Force, 7 Start merged with ZX matrix | F5 | `rtype.asm:540-602` |
| Z->E | 73 | every 32 key ticks | F3 (FF fail / 3 stop) | `rtype.asm:582-592` |

Error codes shown on screen: E0 = wait timeout (`rtype.asm:1016`), E1 = file reply/stop failed (`:868,957`).
"Exit to Wild Commander: F12" (`R-Type-VDAC4:README.md:57`): no code path in the launcher returns to WC
(Exit is `out #00AF,0` + `jr $`, `rtype.asm:992-1001`). F12 is most likely the Evo PS/2 hardware reset, after
which the ESP unloads the app via the FT-detach guard (section 5). Inferred, not confirmed in code.

### 7.4 HMM2 (`HMM/launcher/hmm2.asm` 1053 lines; ESP `HMM/main/hmm_io.c`, `hmm_app.c`)

Two ELFs: `HMM2.ELF` (game) and `OGG.ELF` (audio module, loaded after the game is started, without APP_STOP)
(`hmm2.asm:77-112,944-953,159-160`).

| Dir | Cmd | Payload | Reply | Source |
|-----|-----|---------|-------|--------|
| Z->E | 6D | APP_STOP before HMM2.ELF | ED [1] | `hmm2.asm:812-833` |
| Z->E | 2A/2B | path.cfg | AA/AB | `hmm2.asm:64-75` (pathcfg.asm) |
| Z->E | 69..6C | HMM2.ELF, then OGG.ELF; APP_START failure -> one SYS_RESET 0F (ACK FE, delay, ZiFi_Init x64) and retry | EC (FE = no memory) | `hmm2.asm:306-336,864-900` |
| Z->E | 72 | Evo RTC (Gluk regs `0B,0,2,4,7,8,9,0D`, 8 bytes) | F2 [1] | `hmm2.asm:343-349,996-1047`, `hmm_io.c:327-331` |
| Z->E | 70 | `[size LE32]["HMM2.DAT", 0]` | F0 [1] | `hmm2.asm:358-368`, `hmm_io.c:319-326` |
| E->Z | E4 | `[seq16][off32][len16 <= 32768]` | 71 / 31 as R-Type -> F1 | `hmm_io.c:165-180,332-352` |
| E->Z | E5 | file op (as DOOM) | 74 -> F4; 76 -> F6 | `hmm_io.c:296-298,361-391` |
| E->Z | E6 | `[seq16][off32][RAM_G addr32][len16 <= 32768]`, 512-aligned | Z80 DMA SD -> FT812 RAM_G, 77 `[seq16][status]` -> F7 | `hmm_io.c:130-163,353-360`, `HMM/launcher/graphics.asm:1-80` |
| E->Z | E7 | music hint, 8 bytes (prefetch) | none | `HMM/launcher/music_cache.asm:101-115` |
| Z->E | 75 | input, resent after KEYS_WAIT=4 ticks without F5 | F5 | `hmm2.asm:470-540`, `hmm_io.c:392-418` |
| Z->E | 73 | status | F3 `[state][1][frames32][err]`; 3/255 then app_done | `hmm_io.c:419-431` |

The E6 path matters for the emulator: the ESP app also reads FT812 registers over its own Tang link
(`core->link_read(0x5c, ...)` FT_STAT, `hmm_io.c:158-160`), i.e. both Z80 and ESP talk to the FT812.

## 8. Input path summary

Input never goes from the Evo hardware to the ESP directly: the Z80 launcher samples Kempston `#1F`, Kempston mouse
`#FADF/#FBDF/#FFDF`, the PS/2 scan-code queue of the Gluk/AVR (`#EFF7/#DFF7/#BFF7`) and (R-Type) the ZX matrix, and
sends app command 75; the app maps scan codes itself (`HMM/main/hmm_input.c:59-64`). An HLE that runs the
real Z80 launcher needs nothing extra for input beyond the existing TS-Conf PS/2 FIFO and mouse.

## 9. What the emulator already has (`CORE/emulator/io/serial/esp/`)

| Piece | Class / file:line | Matches VDAC4? |
|-------|-------------------|----------------|
| Frame format `5A CMD LEN CSUM`, 1024 max, 500 ms timeout, EE text 48 | `ZiFiNativeModule` constants `zifinativemodule.h:77-80`; `Frame()` `zifinativemodule.cpp:276`; `SendFrame` `:330`; parser in `Process()` `:356` | yes, same frame layer (no odd-length padding: UART link has `padsFrames() == false`) |
| Core command set 00..28 (ECHO, PING->F0, GET_STEP, SYS_INFO, SYS_RESET, WIFI_*, NET_*, FTP_*, WCU_*) | `zifinativemodule.h:83-92`, `Handle()` `zifinativemodule.cpp:444`, `Network()` `:519` | same codes as `protocol.hpp`; MOD_* 69-6D, APP_*, PATH_GET/SET 2A/2B, LINK_PROBE 3E, TANG_*, VDISK_*, media 31 are NOT implemented (not found) |
| VFS ESP->Z80 commands 40..5E, windows, CRC, timeouts | `ZiFiVfsBridge` `zifivfsbridge.h:35-43` (`IsVfsCommand`), `.cpp` 1585 lines | yes (ported from ZiFi-ESP32-S3-Zero s3-native-0.6.94); used by FTP/WebDAV, not by the games |
| "only PING / SYS_RESET while waiting for the Z80" | `zifinativemodule.h` BridgePoll / waitFor comment (header lines 26-30) | same rule as `DOCS/PROTOCOL.md:332-338` |
| Transport | UART: `EspModule : ISerialPeer` (`espmodule.h:28,69-75`, 4 KB RX buffer `:64`), created by `NetworkManager` from `ZIFI-NATIVE[,variant]` COM spec (`CORE/emulator/io/network/networkmanager.cpp:237-240`, `CORE/emulator/io/serial/comportspec.h:24`); on TS-Conf this is the ZiFi port `#xxEF` (`portdecoder_tsconf.cpp:419-420` -> PortArm::ComPort) | NOT the VDAC4 transport: VDAC4 uses SPI Slave HD on #57/#77 bit 4 |
| SPI hub | `ZControllerSpi` (`CORE/emulator/io/spi/zcontrollerspi.h:43-115`): slot 0 SD (D1 active low), slots 1..3 via `AttachDevice(slot, SpiDevice*, mask, activeHigh)` (`:66`); "previous exchange" read (`:13-18`) | ready to host an ESP `SpiDevice` (slot 2?, mask `0x10`, active low since #13 selects / #03 deselects) |
| FT812 on the bus | `Vdac2Card` slot 1, mask `0x04` active high (`CORE/emulator/platforms/tsconf/vdac2card.h:106-107`), attached when `ts_vdac & 7 == 7` (`portdecoder_tsconf.cpp:230-253`) | VDAC4 FT812 uses the same bit; the ESP also needs a back door to the FT812 (Tang link) |
| SPI DMA | `TsConfDma` SPI in/out modes 0x2 / 0xA (`tsconfdma.h:25,116-121`), SPI callback via the ZC bus (`portdecoder_tsconf.cpp:73-86`) | DMA 03/04 data phases will go through the same `SpiDevice::exchange` per byte |
| `SpiDevice` interface | `select(bool)`, `exchange(uint8_t)`, `truncatedByte()` (`CORE/emulator/io/spi/spidevice.h:11-27`) | the VDAC4 ESP HLE implements this: decode 01/02/03/04/07/08 per CS window |

Missing entirely (not found in CORE): SPI Slave HD shared-buffer model, SpiLinkCore (request/response mailbox,
state word, bulk/put/media/sector), module loader (MOD_*/APP_*, cache), app command routing and events, PATH_*,
Tang register/FT link from the ESP side.

## 10. Unknown / not found

- R-Type ESP-side command handlers (`RT/ESP`): not inspected; launcher-side codes 70, 71, 73, 75, 7A, 7B, 31,
  E4, E6 are known from the Z80 code.
- Exact byte count between SPI command and DMA data for 03/04 (address + dummy) beyond "three #57 reads" for 04:
  confirm in `Z80/zifi_spi.asm` BulkOpen before implementing.
- State-word version: docs say 1/2, firmware code says 3; the Z80 checks only `>= 2`.
- F12 "exit to WC" mechanism: not found in launcher code (inferred hardware reset).
- How WC itself (VDAC4 plugins) launches ESP apps and returns: only the plugin wait times are documented
  (`DOCS/VDAC4.md:318-342`); plugin source not read.
- Behaviour of RAWOUT (flag 40) and SECTOR (flag 20) details: used by the WC VDAC4 VFS, not by the three games.
