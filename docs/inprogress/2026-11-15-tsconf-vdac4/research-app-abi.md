# 02. VDAC4 app <-> firmware ABI (zm_core_t / zm_app_t) for HLE

Research notes for HLE of the VDAC4 ESP32-S3 firmware in unreal-ng. The emulator runs app ELFs on an
Xtensa LX7 interpreter and implements `zm_core_t` plus the exported libc symbols natively.

Repositories: `VDAC4:` = github.com/andrewinsidelazarev/VDAC4, `DOOM-VDAC4:`, `R-Type-VDAC4:`, `HMM2-VDAC4:` = the same owner's repositories (default branch, 2026-11-15); `core/` paths are unreal-ng.

Path prefixes used below:

| Prefix | Path |
|--------|------|
| `FW/` | `VDAC4:ESP32 S3 Zero N8R8/Firmware/` |
| `SDK/` | `FW/modules/sdk/include/` |
| `EL/` | `FW/components/elf_loader/` |
| `DOOM/` | `DOOM-VDAC4:` |
| `RT/` | `R-Type-VDAC4:Source/ESP/main/` |
| `HMM/` | `HMM2-VDAC4:` |

References are `file:line` or `file:first-last`.

---

## 1. Entry protocol and versioning

| Fact | Ref |
|------|-----|
| ABI version `ZM_APP_ABI_MAJOR 1`, `ZM_APP_ABI_MINOR 16` | `SDK/zm_app.h:31` |
| The ELF entry point is `int main(int argc, char **argv)`. The firmware passes `argc = 3` and `argv = {name, (char*)core, (char*)&app_out, NULL}` | `SDK/zm_app.h:9-14`, `FW/src/apps/module_app.cpp:897-949` |
| `argv[1]` is a `const zm_core_t*`; `argv[2]` is a `const zm_app_t**` that the app fills in | `SDK/zm_app.h:9-14` |
| `main` returns `ZM_APP_ABI_MAJOR` (1). The firmware requires `entryResult == 1`, `app->abi_major == 1`, `app->size >= offsetof(zm_app_t, ui_model)` and `start != NULL` | `FW/src/apps/module_app.cpp:975-1037` |
| Fields are only ever appended. `size` is the filler's `sizeof`, so a reader must check `size >= offsetof(field)+sizeof(field)` before it uses a field. A different major is refused | `SDK/zm_app.h:16-19` |
| The firmware fills `core.size = sizeof(zm_core_t)`, `abi_minor = 16` and `firmware = ZIFI_BUILD_VERSION` | `FW/src/apps/module_app.cpp:204-279` |
| Modules (decoders) use the same entry: `module_open` calls `main(3, {name, arg1, arg2})`. For decoders arg1 is a `zm_host_t*` and arg2 a `const zm_source_t**`; the return value must be `ZM_SOURCE_ABI_MAJOR` | `SDK/zm_app.h` (module_open comment), `HMM/main/hmm_audio.cpp:513-517` |

Return codes are `ZM_OK 0`, `ZM_ERROR -1`, `ZM_NO_MEMORY -2`, `ZM_NOT_FOUND -3`, `ZM_BUSY -4` and `ZM_TIMEOUT -5` (`SDK/zm_app.h:47-54`).
Allocation caps are `ZM_MEM_PSRAM 0x01` (the default) and `ZM_MEM_INTERNAL 0x02` (`SDK/zm_app.h:58-59`).
`ZM_TASK_STACK_PSRAM = 0x80000000` is OR-ed into `stack_bytes` (firmware 0.1.112+) (`SDK/zm_app.h:146`).

### Minor history (what each minor appends)

| Minor | Fields added | Ref |
|-------|--------------|-----|
| 0 | header, memory, log/time, tasks/sync, reply/event, pcm_*, set_volume/volume/pcm_faults, module_*, resource* | `SDK/zm_app.h:148-~250` |
| 1 | temperature | `SDK/zm_app.h:32-41` |
| 2 | net_open, net_header, net_read, net_close | same |
| 3 | ft_open, ft_write, ft_read, ft_host_cmd, ft_close | same |
| 4 | core_load, core_back, link_read, link_write, sock_open/send/recv/close, net_resolve, wifi_info | same |
| 5 | ui_available (core); ui_model, ui_cover (app) | same |
| 6 | app_done | same |
| 7 | sd_list, sd_read | `SDK/zm_app.h:300` |
| 8 | sd_write | `SDK/zm_app.h:314` |
| 9 | (semantics only) any file size 1..7 MiB | `SDK/zm_app.h:~320` |
| 10 | sd_open, sd_pread, sd_pwrite, sd_close, sd_create (missing folders are created) | `SDK/zm_app.h:323,334` |
| 11 | wifi_rssi | `SDK/zm_app.h:32-41` |
| 12 | sock_open_tls | same |
| 13 | link_media | same |
| 14 | handoff_put, handoff_take | same |
| 15 | sd2_open, sd2_read, sd2_stream, sd2_close, link_burst | same |
| 16 | pcm_low_latency | same |

---

## 2. `struct zm_core` field by field (`SDK/zm_app.h:148-400`) with firmware behaviour

Every function is a C function pointer and the Xtensa windowed ABI (call8/callx8) applies. In the HLE each
pointer can be a trap address.

### 2.1 Header, memory, log, time

| Field | Signature / semantics | Firmware implementation |
|-------|-----------------------|-------------------------|
| `size, abi_major, abi_minor, firmware` | uint32/uint16/uint16/const char* | `module_app.cpp:204-279` |
| `alloc(size, caps)` | Returns NULL on failure. caps 0 or PSRAM means PSRAM | `coreAlloc` `module_app.cpp:1250-1280`: a 16-byte `AllocHeader{next,prev,caps,magic 0x5A4D414C}` comes first. It tries the requested type, then the other type, then frees the overlay canvas. The block is tracked per app and freed on stop |
| `realloc(p, size)` | | `module_app.cpp:1304-1324`: alloc new, copy, free old. `realloc(NULL)` gives PSRAM |
| `free(p)` | | `module_app.cpp:1282-1302`: ignores blocks without the magic |
| `log(fmt, ...)` | printf-style, truncated to 136 chars | `module_app.cpp:1326-1333` |
| `millis()` | uint32 ms | `esp_timer_get_time()/1000` `module_app.cpp:1335` |
| `delay(ms)` | | `vTaskDelay(max(ms,1))` ticks at 1000 Hz `module_app.cpp:1337`, `FW/sdkconfig.defaults:34` |

### 2.2 Tasks and synchronisation

| Field | Semantics | Implementation |
|-------|-----------|----------------|
| `task_start(fn, arg, stack_bytes, priority, cpu)` | cpu 0/1, or -1 for any | `module_app.cpp:1352-1413`: stack at least 2048 bytes, priority clamped to 20. The stack is internal RAM unless the `ZM_TASK_STACK_PSRAM` bit is set. At most 8 tasks (`module_app.hpp:58-63`), else ZM_BUSY; allocation failure gives ZM_NO_MEMORY. Task name is "zm-app". A trampoline deletes the task when `fn` returns |
| `mutex_create / lock / unlock` | lock waits forever | `module_app.cpp:1415-1461`. Mutexes and signals share a pool of 16 (`kMaxSync`) |
| `signal_create / give / wait(sig, ms)` | binary semaphore. `wait` returns ZM_OK or ZM_TIMEOUT (ZM_ERROR for NULL) | same |

### 2.3 Z80 link

| Field | Semantics | Implementation |
|-------|-----------|----------------|
| `reply(code, data, len)` | Valid only inside `app->command`. Sends the response packet to the Z80 | `module_app.cpp:1463-1470`: ZM_ERROR outside a command, else `transport().send` |
| `event(code, data, len, wait_ms)` | Asynchronous ESP->Z80 event, queued | `module_app.cpp:1472-1476` -> `Application::sendEvent` `FW/src/main.cpp:600-614`. It goes into the intercore `eventQueue_` with a `waitMs` timeout and returns ZM_BUSY when the queue is full. `len <= sizeof(IntercoreEvent::data)` (unknown, see §8) |

### 2.4 PCM

| Field | Semantics | Implementation (`FW/src/audio_player.cpp`) |
|-------|-----------|--------------------------------------|
| `pcm_open(const zm_pcm_format_t*)` | `{rate 8000..96000, channels 1/2}` (`SDK/zm_app.h:61-67`) | `pcmOpen` `:1329-1342` checks the range and runs the open command in the player task. NULL format gives ZM_ERROR (`module_app.cpp:1478-1501`) |
| `pcm_write(frames, count)` | float [-1,1] interleaved. Waits for room and returns the frames accepted | `pcmWrite` `:1344-1382`: clamps, converts to int16 stereo, writes the input ring in chunks of up to 256 frames, polls every 2 ms while the ring is full and gives up after `kPcmWriteWaitMs = 500` (`include/zifi/audio_player.hpp:186`). So it may return fewer than `count` |
| `pcm_queued_ms()` | | `:1384-1394`: ring frames*1000/rate plus the DMA tail not yet played since the last write |
| `pcm_flush / pcm_close / pcm_pause` | | commands to the player task `:1396-1406` |
| `set_volume(0..255)`, `volume()` | 255 means unity (internal 256) | `:101-103, 1408-1413, 1612-1613` |
| `pcm_faults()` | I2S underrun counter | `:665` |
| `pcm_low_latency(on)` (1.16) | returns the DMA depth in frames | `:1324-1327`: 6x240 = 1440 when on, 8x480 = 3840 when off (`:55-64`) |

The header comment says "resampled to 44100, 24-bit DAC" (`SDK/zm_app.h:61-63`). The I2S code actually sends **16-bit data in
32-bit slots, BCK = 64 fs** (`audio_player.cpp:468-475`), the default rate is 44100 (`:50`), the DAC minimum is 16 kHz (`:54`),
and the ring is 256 KB PSRAM or a 16 KB internal fallback (`:43-44, 381-391`). For HLE, treat PCM as int16 at 44100 Hz with a
per-app volume. The exact resampler is not needed.

### 2.5 Modules and resources

| Field | Semantics | Implementation |
|-------|-----------|----------------|
| `module_open(name, arg1, arg2, &handle, &entry_result)` | Loads a cached ELF (e.g. `OGG.ELF`) and calls its `main(3,{name,arg1,arg2})` | `module_app.cpp:2262-2323`. At most 4 modules, 2 MiB each (`module_app.hpp:58-63`, `module_app.cpp:148`) |
| `module_close(handle)` | | same |
| `resource(name, &data, &size)` | Read-only blob from the cache. ZM_NOT_FOUND if absent | same. At most 4 resources, 7 MiB each (`module_app.cpp:149`) |
| `resource_release(name or NULL)` | NULL releases all (0.1.112+) | same |

The module cache is LittleFS `/littlefs/zm` minus a 320 KB reserve (`module_app.cpp:125-136`). It is filled by the Z80
launcher with `MOD_QUERY 0x69 / MOD_PUT 0x6A / MOD_COMMIT 0x6B` (`FW/include/zifi/protocol.hpp:139-143`, `module_app.cpp:398-635`).

### 2.6 Network (minors 2, 4, 11, 12)

`net_open` (HTTP/1.0 GET, ICY 200 counts as 200, at most 2 streams), `net_header` (`":url"` since 0.1.119), and `net_read`
(>0 bytes; 0 = timeout; -1 = end) are at `module_app.cpp:1509-1579`. `sock_*` (at most 5) is at `:1751-1920`. TLS needs a
stack of at least 6 KB (`:1945-1986`). **None of DOOM, R-Type and HMM2 use networking** (§5).

### 2.7 FT812 and LINK (minors 3, 4, 13, 15)

| Field | Semantics | Implementation |
|-------|-----------|----------------|
| `ft_open(timeout_ms)` | Switches Tang to the FT812 personality and takes ESP host mode | `module_app.cpp:1582-1603`. Tang not ready gives NOT_FOUND; `switchToFtCore` failure gives NOT_FOUND; `setHost(true)` failure gives ERROR. It checks LINK `ID == "VDC4"`, `VERSION>>24 == 2` and STAT bit 12, then runs `boot1024x768` (`FW/src/ft_host.cpp:302-336`) |
| `ft_write(addr, words, count)` | addr is a multiple of 4. 32-bit words go to FT812 memory (RAM_G/RAM_DL/REG) | `ft_host.cpp:247-278`: REG 0x302000-0x302FFF is paced word by word; RAM_DL is written without verify; anything else is verified on the last word and retried. Newer cores (VERSION[15:0] >= 0x011D) use the 512-word queue `FT_FREE 0x38` (Gray code) plus `FT_BURST 0x40000` (`ft_host.cpp:76-110`) |
| `ft_read(addr, &word)` | | `module_app.cpp:1605-1641` |
| `ft_host_cmd(cmd, param)` | | word = `cmd | param<<8` to LINK 0xE8 (`ft_host.cpp:20, 224-227`) |
| `ft_close()` | void; drops host mode | `module_app.cpp:1605-1641` |
| `core_load(...)`, `core_back(timeout)` | Tang personality / slot switching; core_back returns to TS-Config (slot 0) | `module_app.cpp:1645-1727` |
| `link_read(addr, words, count)`, `link_write` | Raw Tang LINK registers. 1..16 words, addr%4 == 0 | `module_app.cpp:1729-1749`; register map in `FW/include/zifi/tang_link.hpp:30-79` |
| `link_media(first, last)` (1.13) | Lets Z80 commands first..last carry up to 8 KB raw DMA without checksum, with a prefix of up to 8 bytes | `module_app.cpp:1925-1938` -> `setAppMediaRange` `FW/include/zifi/protocol.hpp:34`; consumed at `FW/src/spi_link_core.cpp:174-179`, `FW/src/protocol.cpp:50-73` |
| `link_burst(addr, words, count)` (1.15) | 16-word transactions to one address (e.g. 0x40000) | `module_app.cpp:2161-2170` -> `tang::writeBurst` `FW/src/tang_link.cpp:753-769` |

The picture is visible only while the TS-Conf `VCONFIG #04` M_SEL is 1. M_SEL = 0 for more than 3 s returns Tang to TS-Config
(`SDK/zm_app.h` ft_open comment). The firmware then unloads the app from `pollCore1` (`module_app.cpp:375-386`).

LINK registers the apps touch directly: `0x00 ID` ("VDC4" = 0x56444334), `0x04 VERSION` (personality in [31:24]: 1 = TS, 2 = FT),
`0x28/0x2C FT frame serial / raster time` (`RT/game.cpp:758`, `HMM/main/hmm_video.c:96`), and `0x5C FT_STAT/FT_CFG`
(cfg bit0 = ?, bit1 = no-dither, bit2 = ESP host for RAM_G, bit3 = ROM write; stat bit27 = RAM_G 3 MB, bit13 = no dither,
bit12 = ESP host) (`FW/include/zifi/tang_link.hpp:30-79`, `FW/src/ft_host.cpp:199-208`). FT_CFG bit0 is set by both R-Type
(`5|dither`) and HMM2 (`1|dither`). Its meaning is not documented in tang_link.hpp (unknown).

### 2.8 SD (minors 7-10, 15)

`sd_list/read/write/open/pread/pwrite/close/create` (`module_app.cpp:2004-2117`) each take a `tang_sd::Session` with a short
timeout (ZM_BUSY if it cannot get one) and mount the card through Tang. sd_create result 3 becomes NO_MEMORY. At most 4
files. `sd2_open(cluster,size,...)` works only from inside a command (WC passes the cluster), and `sd2_stream` reads raw in
multiples of 512 bytes, up to 64 KB (`module_app.cpp:2119-2159`). **None of the four target apps use sd_* or sd2_*.**

### 2.9 Others

`temperature()` returns tenths of a degree C, -32768 if unknown (1 s task). `ui_available()` is at `:2180`. `app_done()` only
sets a flag inside a command; the app is stopped after the reply (`:2176`, `:328-372`). `handoff_put/take` are at `:1162-1217`.

---

## 3. `struct zm_app` (`SDK/zm_app.h:403-431`)

| Field | Semantics | Calling context |
|-------|-----------|-----------------|
| `size, abi_major, abi_minor, name, version` | | |
| `command_first, command_last` | Z80 command range routed to the app | |
| `start(core)` | returns ZM_OK | Called from the APP_START (0x6C) command on core 1, transport loop (`module_app.cpp:975-1037`) |
| `stop()` | Must make its tasks finish | `stopApp` `module_app.cpp:1039-1130`: waits up to 500 ms for tasks, then `vTaskDelete`s the rest |
| `command(cmd, data, len)` | "core 1, reply immediately, at most a few ms" | Called from `handleCommand` on the Arduino loopTask, core 1, priority 1 (`module_app.cpp:328-372`, `FW/src/main.cpp:1980-2031`, `FW/sdkconfig.defaults:44`) |
| `poll(now_ms)` | may be NULL | `pollNetworkCore` on task `zifi-net` (core 0, priority 2, 16 KB stack) under `appLock_` taken with a 0 timeout, so it is skipped when busy (`module_app.cpp:388-396`, `FW/src/main.cpp:88-90, 819-821`) |
| `ui_model(model*)`, `ui_cover(px, side)` (minor 5) | 25-50 calls/s from the screen task, core 1 | `module_app.cpp:2190-2218` |

**Poll cadence.** The `zifi-net` loop blocks up to 2 ms in `xQueueReceive(requestQueue_)` and then runs `pollNetworkCore`
for every app (`FW/src/main.cpp:781-821`). So `poll` runs about every 2 ms (more often under network load, less often when
FTP/SMB/OTA work is slow). Apps rate-limit poll themselves to 100 ms (`RT/game.cpp:975-978`, `HMM/main/hmm_video.c:21-25`).

**Command loop.** `loop()` -> `pollLink()`: `moduleApp_.pollCore1`, VFS, network responses/events, then `transport_.poll`. When
idle it waits up to 1 ms for a bridge request (`FW/src/main.cpp:1980-2016`). Firmware commands (0x69-0x6D, 0x2C/0x2D) are
handled before the app range (`module_app.cpp:328-372`).

### Lifecycle (stopApp order, `module_app.cpp:1039-1130`)

1. clear app pointer and media range;
2. overlay off;
3. `app->stop()`;
4. join tasks (500 ms), then kill the rest;
5. ft_close;
6. close sockets, sd and sd2;
7. core_back if a core was loaded;
8. delete sync objects;
9. close streams;
10. unload modules;
11. `pcm_low_latency(false)`, pcm_close;
12. free all tracked allocations;
13. unload the ELF;
14. clean up handoff.

The HLE must reproduce the forced cleanup: apps rely on it (HMM2 comment `HMM/main/hmm_app.c:65`).

Other stop triggers:
- an app with no commands for 5 s is stopped (`module_app.cpp:451-456`);
- M_SEL = 0 / slot 0 seen in `pollCore1` (`:375-386`);
- `app_done`;
- APP_STOP 0x6D.

---

## 4. ELF loader (espressif elf_loader 1.3.3 + VDAC4 patches)

| Rule | Ref |
|------|-----|
| Section-based loader (`BUS_ADDRESS_MIRROR=y` on S3), not segment-based. Only the sections named exactly `.text`, `.data`, `.rodata`, `.data.rel.ro` and `.bss` are loaded. `.got`, `.hash` and the dynamic sections are ignored | `EL/src/esp_elf.c:174-349`, `EL/Kconfig` |
| `.text` is copied into one PSRAM block (size aligned to 4). `data | rodata | data.rel.ro | bss(zeroed)` go into a second block, laid out in that order | `EL/src/esp_elf.c:174-349`, `EL/src/esp_elf_adapter.c` (`MALLOC_CAP_SPIRAM|8BIT`) |
| Code runs from the I-bus alias: `text_exec = text_data + 0x06000000` (SOC_IROM_LOW-SOC_DROM_LOW, 0x42000000-0x3C000000). `elf_remap_text` adds the offset only for addresses inside the app's own .text | `EL/src/esp_elf_adapter.c` |
| Entry = `e_entry - text.sh_addr + text_load`, remapped to the I-bus alias | `EL/src/esp_elf.c:174-349` |
| Relocations: every `SHT_RELA` section is processed. The symbol-type quirk (relocation type compared against STT_* values) means only RELATIVE, GLOB_DAT, JMP_SLOT and RTLD matter in practice | `EL/src/esp_elf.c:534-719` (quirk at :591) |
| `R_XTENSA_RELATIVE`: `*where = remap_text(map(*where))`. VDAC4 patch: a value exactly one past a section end maps `*where-1`, then adds 1 | `EL/src/arch/esp_elf_xtensa.c:75-127` |
| `R_XTENSA_GLOB_DAT/JMP_SLOT`: `*where = remap_text(symbol address)`; names are looked up in libc table -> espidf table -> registered (`zm_symbols.cpp`) | `EL/src/esp_elf_symbol.c:201-262` |
| `R_XTENSA_RTLD`: ignored. Other types: logged, result ignored (load continues) | `EL/src/arch/esp_elf_xtensa.c`, `EL/src/esp_elf.c:644` |
| A missing symbol gives -ENOSYS (load fails) unless it is weak-undefined, which resolves to 0 (VDAC4, 0.1.122) | `EL/src/esp_elf.c:524-532`, `FW/src/zm_symbols.cpp:7-8` |
| Cache sync: `esp_cache_msync` C2M on text and data, plus an I-cache invalidate of the text at the +0x06000000 alias (`forgetStaleCode`) | `EL/src/esp_elf_adapter.c`, `FW/src/apps/module_app.cpp:897-949` |
| Before caching and loading, `elfImageProblem` validates the header, Xtensa machine, section table, `.text` present, entry inside `.text`, relocation symbol tables, and every relocation target inside a loaded section (4 bytes) | `FW/src/elf_image.cpp:72-187` |
| Size limits: module 2 MiB, resource 7 MiB. App ELF size is limited by the cache / PSRAM (HMM2.ELF is 1.6 MB) | `module_app.cpp:148-149` |

Memory notes: `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`, `RESERVE_INTERNAL=32768` (`FW/sdkconfig.defaults:15-28`). Raw `malloc`
from the libc table is `heap_caps` with default caps, so allocations of 4 KB or less come from internal RAM, and these blocks are
**not** tracked or freed by stopApp (`EL/src/esp_elf_symbol.c:69-161`). Task WDT is 5 s with panic, checked on CPU0 IDLE only
(`FW/sdkconfig.defaults:141-145`): an app that never yields on core 0 for 5 s panics the board.

HLE implications:
1. Map the D-bus 0x3Cxxxxxx and I-bus 0x42xxxxxx PSRAM views onto the same backing memory.
2. Load only the five sections.
3. Implement RELATIVE (with the one-past-end rule), GLOB_DAT and JMP_SLOT.
4. Skip RTLD.
5. Resolve imports to trap stubs.
6. Run `main` on a thread with an Xtensa windowed stack.

## 5. Exported symbols (what an ELF may import)

There are three tables, searched in this order:

1. **libc**, `EL/src/esp_elf_symbol.c:69-161`:
   - string: strerror, memset, memcpy, strlen, strtod, strrchr, strchr, strcmp, strtol, strcspn, strncat;
   - stdio: puts, putchar, fputc, fputs, printf, vfprintf, fprintf, fwrite;
   - process: usleep, sleep, exit, close;
   - heap: malloc, calloc, realloc, free;
   - time: clock_gettime, strftime;
   - pthread: pthread_create, pthread_attr_init, pthread_attr_setstacksize, pthread_detach, pthread_join, pthread_exit;
   - newlib: __errno, __getreent, _ctype_;
   - libgcc: __ltdf2, __fixunsdfsi, __gtdf2, __floatunsidf, __divdf3;
   - getopt: getopt_long, optind, opterr, optarg, optopt;
   - setjmp, longjmp.
2. **espidf**, `EL/src/esp_elf_symbol.c:165-192`: lwip_bind/setsockopt/socket/listen/accept/recv/recvfrom/send/sendto/connect, ipaddr_addr, lwip_htons/htonl, ip4addr_ntoa, ets_printf.
3. **VDAC4 module symbols**, `FW/src/zm_symbols.cpp:49-86`:
   - string: memmove, memcmp, memchr, strncmp, strcpy, strncpy, strcat, strstr, strnlen, strcasecmp, strncasecmp;
   - stdio/stdlib: snprintf, vsnprintf, sprintf, qsort, abs, labs, atoi, strtoul, rand, srand;
   - float math: sinf, cosf, tanf, atanf, atan2f, powf, expf, exp2f, logf, log2f, log10f, sqrtf, floorf, ceilf, roundf, truncf, fmodf, lrintf, lroundf;
   - double math: sin, cos, tan, atan, atan2, pow, exp, log, log10, sqrt, floor, ceil, fmod, fabs, ldexp, frexp, acos, asin, acosh, asinh, atanh, cosh, sinh, tanh, cbrt, expm1, log1p, hypot, lrint, round, trunc, scalbn;
   - time: gettimeofday, gmtime_r, localtime_r, mktime, difftime;
   - libgcc double/int64: __adddf3, __subdf3, __muldf3, __negdf2, __eqdf2, __nedf2, __ledf2, __gedf2, __unorddf2, __fixdfsi, __floatsidf, __extendsfdf2, __truncdfsf2, __fixdfdi, __floatdidf, __divdi3, __udivdi3, __moddi3, __umoddi3, __ashldi3, __ashrdi3, __lshrdi3, __muldi3, __fixsfdi, __fixunssfdi, __floatdisf, __floatundisf, __divsf3, __fixunssfsi, __floatunsisf, __fixunsdfdi, __floatundidf;
   - heap: heap_caps_malloc/calloc/free;
   - ROM TJpgDec: jd_prepare, jd_decomp.

Bit-exactness: the math functions are newlib/ROM implementations. To match the board bit for bit (the project rule is
"no libm transcendentals in tables"), the HLE needs soft implementations with defined results. libgcc helpers are exact IEEE
operations, so host double arithmetic with -ffp-contract=off is acceptable. `zm_level.h` (R-Type/OGG loudness) uses tan/pow/powf/log10f.

---

## 6. Target apps

### 6.1 ELF facts (llvm-readelf)

All are `ET_DYN`, `EM_XTENSA`, flags 0x300, with no `.dynamic` section (imports are found through `.rela.*` plus `.dynsym`).

| ELF | Size | Entry | .text | .rodata | .data | .bss | RELATIVE / JMP_SLOT / RTLD |
|-----|------|-------|-------|---------|-------|------|----------------------------|
| DOOM.ELF | 371812 | 0xf340 | 0x27c2d | 0x18684 | 0xef74 | 0x48214 | 3710 / 28 / 2 |
| RTYPE.ELF | 1516164 | 0x9900 | 0x1267c6 | 0x48090 | 0x3c | 0x9891c | 745 / 23 / 2 |
| HMM2.ELF | 1648464 | 0x15ca70 | 0x13de90 | 0x32d50 (placed after .bss) | NOBITS, size 0 | 0x10a078 | 11265 / 120 / 2 |
| HSD.ELF | 3744 | 0x954 | 0x63d | 0x1e3 | - | 0x4034 | 36 / 2 / 2 |
| OGG.ELF (`WC/ESP/`) | 35420 | 0x2470 | 0x70d2 | - | - | 0x12528 | 99 / 31 / 2 |

JMP_SLOT targets are literal-pool words inside `.text`, so the loader writes into .text through the D-bus.

### 6.2 Imports (`llvm-readelf --dyn-syms`, undefined)

| ELF | Imports |
|-----|---------|
| DOOM | __divdi3 __divsf3 __extendsfdf2 __ltdf2 __truncdfsf2 __udivdi3 atoi longjmp memcmp memcpy memmove memset powf setjmp sinf snprintf strcasecmp strcmp strcpy strlen strncasecmp strncmp strncpy strrchr strstr strtod strtol vsnprintf |
| RTYPE | __adddf3 __ashldi3 __divdf3 __divsf3 __extendsfdf2 __floatunsidf __ledf2 __muldf3 __truncdfsf2 __udivdi3 ceilf log10f memcmp memcpy memset powf strcmp strlen (operator new/delete defined internally) |
| HMM2 | __adddf3 __ashldi3 __divdi3 __divsf3 __extendsfdf2 __fixdfsi __floatdidf __floatsidf __floatunsidf __lshrdi3 __moddi3 __muldf3 __subdf3 __udivdi3 __umoddi3 atan2 cos fabs floor log10 memchr memcmp memcpy memmove memset pow sin snprintf sqrt strchr strcmp strcpy strlen strncmp strncpy strrchr strtoul vsnprintf |
| HSD | __udivdi3 memset |
| OGG | __adddf3 __divdf3 __divsf3 __extendsfdf2 __fixdfsi __floatsidf __floatunsidf __ledf2 __muldf3 __truncdfsf2 __udivdi3 cos exp floor free ldexp log malloc memcmp memcpy memmove memset pow qsort sin snprintf strchr strcmp strlen strncasecmp strnlen |

The union of these is the minimum HLE libc. It includes `setjmp/longjmp` (DOOM), which needs exact Xtensa register-window
semantics, and raw `malloc/free` (OGG).

### 6.3 zm_core usage per app

| Function | DOOM | R-Type | HMM2 | HSD |
|----------|:----:|:------:|:----:|:---:|
| alloc/free | Y (`vd_malloc`) | Y (incl. ZM_MEM_INTERNAL) | Y | - |
| log, millis, delay | Y | Y | Y | Y |
| task_start | 3-4 tasks | 4+ tasks | 3 tasks | - |
| mutex_*, signal_* | Y | Y | Y | mutex |
| reply | Y | Y | Y | Y |
| event | 0xE4, 0xE5 | 0xE4 | 0xE4, 0xE5, 0xE6 | 0xE4 |
| pcm_open/write/queued_ms/flush/close | Y (22050 stereo) | Y (44100 stereo) | Y (44100 stereo) | - |
| pcm_low_latency | - | optional (1.16) | - | - |
| module_open/close | - | - | Y (`OGG.ELF`) | - |
| resource / resource_release | - | release(NULL) | resource (via OGG host) | - |
| ft_open/write/read/close | Y | Y | Y | - |
| ft_host_cmd | - (not seen) | - | - | - |
| link_read/write | - | read 0x00, 0x28, 0x5C; write 0x5C | read 0x00, 0x2C, 0x5C; write 0x5C | - |
| link_media | - | optional 0x31..0x31 | - (uses the 0x31 range) | - |
| core_back | - | - | Y (exit) | - |
| app_done | - | Y | Y (guarded by size) | - |
| sd_*, sd2_*, net_*, sock_*, handoff, ui_* | - | - | - | - |

### 6.4 DOOM (`DOOM/main/`)

- **Descriptor.** `zm_app = {sizeof, 1, 3, "doom", "1.1", 0x70, 0x76, start, stop, command, NULL}`. It is built against its own minor-3 header `DOOM/include/zm_app.h`; `main` returns 1 when argc == 3 (`DOOM/main/app.c:347-350`).
- **Requires minor 3:** checks `core->size >= offsetof(ft_close)` (`app.c:300-333`).
- **Tasks.**
  - `game_worker`: 16384-byte stack, priority 3, CPU 1. Loop: `doomgeneric_Tick(); delay(1)`.
  - `audio_worker`: 4096-byte stack, priority 4, CPU 0.
  - video upload task: 6144-byte stack, priority 3, core 0 (`video.c`).
  - loader splash task: 6144-byte stack, priority 3, core 0 (`loader.c`).
  - `stop` waits up to 17 s (`app.c:300-333`).
- **WAD I/O over the Z80.**
  - Event `0xE4 [id LE16][offset LE32][len LE16]`; the Z80 answers with command `0x71 [id][data...]`, up to 1016 bytes, through a 4 KB block cache (`app.c:62`).
  - File ops: event `0xE5` (35 bytes) with commands 0x74/0x76 (pieces of 248 bytes).
  - Input: 0x75 `[joy, mouse b/x/y, PS/2 bytes]`. Status: 0x73 -> F3. Config: 0x70 `[size LE32][name]` (DOOM1/DOOM/DOOM2.WAD).
  - Replies: F0, F1, F3, F4, F5, F6.
- **Video.** `ft_open(15000)` (`video.c:136`).
  - Triple buffer of 320x200 8-bit paletted frames at RAM_G 0 / 0x10000 / 0x30000; palettes at 0x20000 + 0x200*b.
  - 64-byte delta blocks; each ft_write is at most 1024 words.
  - Display list into RAM_DL: 960x720 bilinear at (32,24). `DLSWAP = 2`, paced every 2 FT frames using REG_FRAMES 0x302004.
  - Optional BL616 GPU ring at CMD_RAM 0x308000 (CMD_READ 0x3020f8, CMD_WRITE 0x3020fc), probed by magic, with software fallback (`gpu.c`).
- **Sound.** 22050 Hz stereo, 256-frame chunks; `delay(2)` while `pcm_queued_ms > 220`; partial pcm_write is handled (`sound.c`).

### 6.5 R-Type (`RT/`)

- **Descriptor.** `app = {sizeof, 1, 6, "R-Type VDAC4", "0.1", 0x31, 0x7a, start, stop, command, poll}` (`RT/game.cpp:1046-1052`). The SDK header it includes (external `sdk/` dir) is not in the repo (unknown).
- **ABI: minimum minor 6** (ft = 3, link = 4, app_done = 6). It uses `link_media` (13) and `pcm_low_latency` (16) only if `core->size` allows (`game.cpp:873-910`, `audio.cpp:367-387`).
- **start.** Creates mutexes and signals, calls `resource_release(NULL)`, `link_media(0x31,0x31)` and `task_start(game_task, 10240, 3, cpu 1)`. It also starts:
  - `logic_task`: 4096-byte stack, priority 2, core 0 (`game.cpp:665`);
  - FLAC music reader: 4096-byte stack, priority 2, core 0;
  - `audio_task`: 8192-byte stack, priority 4, core 0, `delay(1)` after each 256-frame block, 44100 Hz stereo, `queued_limit = dma_ms + 12`. Its engine is allocated with ZM_MEM_INTERNAL (`audio.cpp:367-387`).
- **DAT I/O.** Event `0xE4 [id][offset][size up to 32768]`. Data comes back in 0x71 (up to 1016 bytes) or 0x31 (raw DMA, up to 8 KB, 6-byte header). Config 0x70 `"RTYPE.DAT"`, input 0x75, status 0x73 (calls `app_done` at the end, `game.cpp:968`), ID query 0x7a.
- **poll** (`game.cpp:975-978`): every 100 ms, `link_read(0,2)`. If ID is VDC4 and the personality is 1, it stops itself.
- **Video** (`video_hq.cpp:1803-1810, 2184`).
  - `ft_open(15000)`, then `link_read(0x5C)` and `link_write(0x5C, 5 | dither)`: ESP host plus bit0.
  - ft_write chunks up to 4096 bytes; DLSWAP 0x302054 polled with ft_read; ft_close.
  - `link_read(0x28,2)`: FT frame serial and raster time (`game.cpp:758`).

### 6.6 HMM2 (`HMM/main/`)

- **Descriptor.** `{sizeof, 1, 4, "hmm2", "0.1", 0x31, 0x77, start, stop, hmm_command, hmm_video_poll}` (`HMM/main/hmm_app.c:124`). `main` refuses a core smaller than `offsetof(link_write)+4` (`hmm_app.c:126`). **Minimum minor 4.** `app_done` (6) is used only when `core->size` covers it (`hmm_io.c:427-431`).
- **Self-unpacking runtime.** `main` (`hmm_entry.c:49-52`) first inflates packed `.data` initialisers with `puff` into `__hmm_data_begin` (in .bss). It then applies its own relocation list. The format is a header `HPK1` 0x314b5048 in section `.hmm_pack`, with varint deltas. Words in the linked text range get the runtime `main` address bias; words in the data range get the runtime .bss base (`hmm_entry.c:18-48`).
  - Consequence: the ELF has `.data` NOBITS size 0, and `.rodata` sits after `.bss`. The shipped ELF has only `.text`, `.data` (NOBITS, 0), `.bss` (VA 0x16a350, 0x10a078) and `.rodata` (VA 0x2743d0, 0x32d50): `.hmm_pack` is merged into `.rodata` (`llvm-readelf -S HMM2.ELF`). The loader places `.rodata` *before* `.bss` in its data block (§4), unlike the VA order. Per-section relocation mapping makes that harmless, but the HLE must map per section, not by one linear VA offset.
- **C++ runtime.** It registers EH frames: `__register_frame_info`, with FDE pc fields patched by `bias = anchor - __hmm_eh_begin - hmm_linked_delta` (`hmm_app.c:71-82`). It runs ctors/init arrays. pthread keys and mutexes are emulated on `core->mutex_*`, keyed by `rur.threadptr` (`hmm_app.c:26-64`), so **THREADPTR must be non-zero and unique per task** (main refuses `task_id() == 0`, `hmm_app.c:127`). `__cxa_guard_acquire` spins with `delay(1)` (`hmm_app.c:91-97`).
- **Tasks.**
  - `hmm_session_task`: 16384-byte stack, **priority 1**, CPU 1, the same priority as the Arduino loopTask that owns the Z80 link (`hmm_app.c:100-108`). The comment there records that priority 4 starved the link.
  - `output_task` (audio): 5632-byte stack, priority 4, core 0 (`hmm_audio.cpp:521`).
  - `cursor_task`: 3072-byte stack, priority 4, core 0 (`hmm_gpu.cpp:944`).
- **I/O over the Z80** (`hmm_io.c`).
  - Event `0xE4` reads `HMM2.DAT` (`:177-179`); event `0xE6` asks the Z80 to stream graphics from SD directly to FT812 RAM_G (`:144-146`); event `0xE5` performs FS calls (`:298`). All use `wait_ms = 1000`.
  - Data comes back in commands 0x71 (up to 1020 bytes) or 0x31 (up to 8192 bytes, raw) with `[id LE16]...` (`:332-348`).
  - Replies: F0, F1, F2, F3, F4, F5, F6, F7 (`:325-428`).
- **Audio** (`hmm_audio.cpp`). 44100 Hz stereo, block 256, `pcm_open` at `:424-425`.
  - Music is OGG, decoded by **`OGG.ELF` loaded via `module_open("OGG.ELF", &host, &decoder, ...)`** (`:513-517`). The decoder ABI is `zm_source.h` (MAJOR 1); the host callbacks are in-app.
  - `resource()` is forwarded to the core (`:151`). An OGG with a rate other than 44100 is refused (`:398`).
- **Video** (`hmm_video.c`).
  - `ft_open(15000)`, then waits up to 20 s until personality == 2 and `link_read(0x5C)` works (`:35-43`).
  - Writes `FT_CFG = 1 | dither<<1` with bit2 clear: "Evo SPI delivers RAM_G, the ESP word port serves the DL" (`:47-49`).
  - **Requires FT_STAT bit27 (RAM_G 3 MB)**, otherwise it fails (`:52`). Frames are 640x480 only (`:83`).
  - Reads `0x2C` raster time each frame (`:96`).
  - On exit: fence `ft_read(0)`, `ft_close`, `core_back(10000)`, then checks personality 1 and STAT bit12 clear (`:56-78`).
  - `poll` checks the personality every 100 ms and stops the session on 1 (`:14-25`).
- **HSD.ELF** is an SD-throughput benchmark app (`HMM/bench/sd_bench.c`). It is not a module used by the game: it handles commands 0x70..0x75, issues 0xE4 reads and logs statistics. The README calls it a "former helper" (`HMM/_SD/README.md`). It is low priority for emulation, but it is the smallest end-to-end smoke test of the ABI (2 imports).

---

## 7. Consequences for the emulator HLE

1. **Minimum firmware surface for the four apps:**
   - memory, log/time;
   - task_start (up to about 4 concurrent tasks, pinned to cores, with priorities);
   - mutex/signal;
   - reply/event;
   - pcm_open/write/queued_ms/flush/close (+ low_latency);
   - module_open/close and resource(_release);
   - ft_open/write/read/close;
   - link_read/write on LINK regs 0x00/0x04/0x28/0x2C/0x5C;
   - link_media;
   - core_back;
   - app_done.

   Networking, sockets, sd and sd2 can be stubs that return ZM_NOT_FOUND / ZM_ERROR at first.
2. **Data flow is Z80-centric.** Every game asset comes from the Z80 launcher (SPG) answering events 0xE4/0xE5/0xE6 with
   commands 0x71/0x31. The HLE therefore needs the ESP<->Z80 packet transport (the Z80 side runs real SPG code in the emulated
   TS-Conf). HMM2's 0xE6 additionally needs the Evo SPI -> Tang -> FT812 RAM_G path (FT_CFG bit2 = 0).
3. **Scheduling model.**
   - `command` runs synchronously in the core-1 transport loop and must finish quickly.
   - `poll` runs on core 0 about every 2 ms.
   - App tasks need preemptive or cooperative scheduling with priorities; HMM2 depends on priority 1 sharing with the link loop.
   - A deterministic HLE can time-slice the interpreter threads on emulated time, with `delay`/`signal_wait`/`pcm_write` blocking points.
4. **Thread identity.** THREADPTR is set per task (HMM2), and setjmp/longjmp follow the windowed ABI (DOOM).
5. **ELF loader.** Five sections, the I/D alias, RELATIVE with the one-past-end rule, JMP_SLOT/GLOB_DAT to trap stubs, and
   weak-undefined resolved to 0.
6. **FT812.** All three apps use ft_open(15000), RAM_G/RAM_DL/REG writes, DLSWAP polling and REG_FRAMES. HMM2 needs RAM_G 3 MB
   mode (STAT bit27), DOOM optionally probes the BL616 GPU ring (it can be reported absent).

## 8. Unknowns

| Item | Why it matters |
|------|----------------|
| `IntercoreEvent::data` size (maximum `event` length) | event payload limit; read `FW/src/main.cpp` struct definition |
| FT_CFG bit0 meaning (R-Type writes 5, HMM2 writes 1) | not documented in `tang_link.hpp:30-79`; probably "FT window enable" |
| R-Type's external `sdk/zm_app.h` version | its `sizeof(zm_app_t)` value; only `size >= offsetof(ui_model)` is required anyway |
| `pcm_write` resampling path and DMA tail details (player task in `audio_player.cpp`) | for accurate `pcm_queued_ms` back-pressure |
| `tang_sd` / `sd2` internals and `link_spi3.cpp` timing | only if sd_* apps (players, WC modules) are targeted later |
| Exact FreeRTOS priorities of the firmware tasks competing with apps on core 0 (I2S player, VFS, SMB) | fidelity of starvation behaviour |
