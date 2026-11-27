# Quake II for ZX Evolution VDAC4 on TSL-VDAC4

Status 2026-11-27: implemented (§8), uncommitted on branch `quake2`. The demo runs on `TSL-VDAC4`. This document
started as the research of what `TSL-VDAC4` lacked to run the game `Quake-II-for-ZX-Evolution-VDAC4` (release
folder `QUAKE2`: `_Q2.SPG`, `Q2.ELF`, `Q2.BIN`, `PAK0.PAK`..`PAK2.PAK`); §1-§6 are that research.

Sources:

| What | Where |
|------|-------|
| Game, ESP side | `Quake-II-for-ZX-Evolution-VDAC4`: `esp/main/` (q2_app.c, bz_link.c, store_esp.c, sys_esp.c, compat.c, in_esp.c) |
| bomzh-3D core | same repo: `gpu/SPEC.md` (0.4), `gpu/model/bomzh.c` / `bomzh.h` (C model, bit-exact with the RTL), `gpu/rtl/bz_top.sv` |
| Author's notes | same repo: `PROGRESS.md` (includes his own Unreal NG branch `vdac4-q2`, not available to us) |
| Firmware | `VDAC4`: `ESP32 S3 Zero N8R8/Firmware` (vdac4-0.1.174, ABI 1.21): `modules/sdk/include/zm_app.h`, `src/apps/module_app.cpp`, `src/core_store.cpp`, `src/spi_link_core.cpp`, `src/protocol.cpp` |
| Z80 loader | `_Q2.SPG` only (no source; the VDAC4 repo's `Network/ESP App SPG/esp_app.asm` is an old version without core, data files or paths). Disassembled from the SPG (header 0x400, MegaLZ / Hrust blocks, `core/src/loaders/snapshot/loaderspg.cpp`): main code is block 0, page 2 at `#8000` |
| Our side | `core/src/emulator/platforms/tsconf/vdac4/` (paths below are relative to it unless they start with `core/`) |

## 1. The Z80 loader and why it stalls

### 1.1 Step by step

`#8000` main (page 2), with the FAT32 driver in pages 14 / 15 (`#4000` entry table):

1. Pages set (`#21AF`=0E, `#10AF`=0F, `#11AF`=0E), border blue, `call #9275` ZiFi init (the SPI link).
2. `call #8B39`, find the game folder:
   - `#866F` PATH_GET `2A [serial]["Q2",0]` (7 bytes), waits `AA`, checks the returned cluster with the driver
     (`#401E`) and `#8724` (folder holds `Q2.ELF` or a PAK).
   - Not found: `#8750` scans the card, `#86E3` PATH_SET `2B [serial][cluster LE32]["Q2",0]` (11 bytes), waits
     `AB` (`#8701`: 8 x 65535 polls, `EE` = error).
   - `#8B5E`: enter the folder, look up `PAK0.PAK`, `PAK1.PAK`, `PAK2.PAK` (`#8739` -> driver `#40B1`), keep
     cluster / size / FAT time in the 12-byte table at `#91EF`. A missing PAK -> carry -> fail.
3. `call #80EB` with descriptor `#821E`: MOD_QUERY for the core, flags `02`, length `0x000DD89A` (907 418),
   name `Q2.BIN`.
4. `call #80EB` with descriptor `#8210`: MOD_QUERY for the app, flags `00`, length `0x000B10CC` (725 196),
   `Q2.ELF`.
   - `#80EB`: sends `69 [flags][len LE32][FAT time LE32][name,0]`, waits `E9`.
   - Reply 0: cached, done. Reply 1: MOD_PUT `6A` pieces of up to 1016 bytes (page-bounded), then
     MOD_COMMIT `6B`. Any other reply: fail.
   - Commit reply 0 sets carry: fail (`#8163`-`#8168` -> `#808C`).
5. `call #8BAA` / `#8BDB`, the data files. For each PAK it sends
   `69 [04][len][time][name,0]["Q2",0][index]`, with the app name at `#91EC`, and waits `E9` up to 64 times.

   | Reply | Meaning | Loader action |
   |---|---|---|
   | 0 | have | next file |
   | 1 | send | DMA transfer (`#8CD6`...) + COMMIT; border cyan (5) during it |
   | 5 | remote | register the file's FAT chain runs in the table at `#923C` (`#8EA8`) |
   | 2 | no room | border magenta (3) |
   | 4 | no USB drive (firmware 0.1.165) | border yellow (6) |
   | other | | border red (2) |
6. VCONFIG = 04, APP_START `6C "Q2.ELF"`, wait for `EC` in `#80A6`. That wait also serves 0x6E data requests
   (`#8F79`). A nonzero result: fail.
7. Border green; the loop serves 0x6E (`#8F6D`) until a key, then APP_STOP `6D`, VCONFIG 0, border black.
8. `#808C` fail: border red, halt.

0x6E service (`#8F79`): the event `6E [req][file][offset LE32][len LE16]` (8 bytes) is answered by
`6F [req][offset LE32][data]` read through the run table.

- A 5-byte `6F` without data is the error answer: length 0, file >= 3, or a seek failure.
- `#913A` sends by SPI DMA in pieces of up to 8 KB when `#9224` is set, else in frames of `0x3F8` bytes (`#9610`).
- `#9224` comes from `#8E85`, which reads the link state word (marker `D7`, version >= 2). Our link reports
  version 3 (`vdac4esplinkcore.h:80-81`), so the DMA path is taken.

### 1.2 Why it stalls

Earlier run evidence (`scratch/games/q2-state.json`, `q2-run.txt`, git-ignored):

- 946 packets, the tail all MOD_PUT `6A` (1020 / 132 / 206 bytes), the last one MOD_COMMIT `6B` at 19 881 ms.
- `last_step` 107 (0x6B), no app running, Tang personality `ts`, status line `app= run=False`.

The loader passes the folder and PAK steps and sends the core query (step 3). Then:

1. `Vdac4EspFirmware::HandleQuery` (`vdac4espfirmware.cpp:670`) checks only bit 0 (`:678`, resource). Flag
   `02` is taken as an app module; 907 418 bytes is under `kMaxModuleBytes` (2 MB), so it replies 1.
2. The loader sends about 940 MOD_PUTs and then the commit.
3. `HandleCommit` (`:798`) accepts a non-resource delivery only if `vdac4::EspElfImageProblem` passes (`:815`).
   `Q2.BIN` is a Gowin bitstream, not an ELF, so the commit replies 0.
4. The loader sets carry, red border, halt. No packet follows.

So MOD_QUERY is sent; it is answered wrongly. The blockers after this one, in the order the game meets them:

| # | Blocker | Where |
|---|---------|-------|
| B1 | Flag `02` (core) not handled | `vdac4espfirmware.cpp:678`, `:815` |
| B2 | Flag `04` (data file) not handled: PAK0 (184 MB) would be a module over 2 MB -> reply 2 -> magenta border | same |
| B3 | No 0x6E event / 0x6F piece path; the raw-media framer rejects `6F` | `vdac4espframes.cpp:60`, `vdac4esplinkcore.cpp:205-212` |
| B4 | ABI minor 19: `start()` needs >= 20 and `size` covering `data_close` | `esp/espapphost.h:84-89` |
| B5 | Firmware string `unreal-ng-hle-0.1.122` fails `firmware_at_least(166)` | `esp/espapphost.cpp:57` |
| B6 | `core_load` returns not found | `esp/espapphost.cpp:1079` |
| B7 | No bomzh-3D personality in the Tang | `vdac4tang.h:56-61`, `vdac4tang.cpp` LinkRead / LinkWrite |
| B8 | `realloc` moves shrinking blocks: `Hunk_End` calls `Sys_Error("Hunk: heap moved a shrinking block")` | `esp/espapphost.cpp:1234-1253`; game `sys_esp.c:522-540` |

## 2. ABI 1.20 and 1.21

From `modules/sdk/include/zm_app.h` (firmware), appended to `zm_core_t` after `module_open_ex` (minor 19):

| Minor | Entry | Semantics |
|-------|-------|-----------|
| 20 | `int data_open(const char* name, void** file, uint32_t* size)` | Opens a data file of the running app: its folder is the app ELF name without `.ELF` (`Q2`); the name is the 8.3 card name, case-insensitive. Up to 8 handles. `ZM_NOT_FOUND`: the file was neither sent nor registered; `ZM_ERROR`: no free handle. (`zm_app.h:480-501`) |
| 20 | `int data_read(void* file, uint32_t offset, void* data, uint32_t count, uint32_t* got)` | Random-access read; `got < count` only at end of file. `ZM_ERROR` on a USB failure or after three 4 s timeouts with no Z80 answer (remote files). Any task may call it; reads are serialized. |
| 20 | `void data_close(void* file)` | Frees the handle. |
| 21 | `int link_read_checked(uint32_t address, uint32_t* words, uint32_t count)` | LINK read done twice under one lock; on a mismatch a third read and a per-word median. Address multiple of 4, `count <= 16`. (`zm_app.h:503-517`) |

Firmware side: `module_app.cpp` `coreDataOpen` 647-670, `remoteFetch` 672-720 (sends the 0x6E event, waits
`kRemoteWaitMs`, `kRemoteAttempts`), `handleDataPiece` 722-735, `coreLinkReadChecked` 2534-2544.

What Q2.ELF uses:

- The app is ET_DYN, Xtensa, 725 196 bytes.
- It has 59 imports: soft double (`__adddf3`...`__truncdfsf2`), libm, string, setjmp / longjmp, time. All 59
  are already in our HLE ROM tables (checked by name under `vdac4/`).
- Core table calls, from the source (`q2_core->` / `core->`):

  | Calls | Used for |
  |---|---|
  | `log`, `millis`, `delay` | logging and timing |
  | `alloc`, `free`, `realloc` | `realloc` must shrink in place, see B8 |
  | `task_start` | 192 KB, `ZM_TASK_STACK_PSRAM_FIRST`; already supported (`esp/espapphost.cpp:1266`) |
  | `link_read`, `link_write`, `link_burst` | the core's LINK registers |
  | `link_read_checked` | only if `abi_minor >= 21` and the table is long enough; else `link_read` |
  | `core_load("Q2", &version)` | loading the bomzh-3D core |
  | `data_open`, `data_read`, `data_close` | reading the PAKs |
  | `abi_minor`, `size`, `firmware` | checks in `start()` |

  No `code_ram`, no `module_open_ex`, no `command` handling (it returns `ZM_ERROR`; no input yet).
- `start()` (`q2_app.c:125-167`) refuses unless:
  - `abi_minor >= 20` and `size >= offsetof(data_close) + 4`;
  - `firmware` starts with `vdac4-` and parses as `A.B.C` with `B == 1 && C >= 166`.

  The README asks for 0.1.170+.

So the delta is: three `data_*` slots plus `link_read_checked` in `zm_core_t` (`kAbiMinor` 21), and a firmware
string in the `vdac4-0.1.NNN` form. Keep a suffix after the number, e.g. `vdac4-0.1.174-unreal-ng-hle`: the
parser stops at the third number.

## 3. Core delivery (flag 2) and the bomzh-3D core

### 3.1 Delivery

Firmware `module_app.cpp`:

- `handleQuery` 744-800 tests flag 4 before flag 2.
- `handleCoreQuery` 1191-1240:
  - the name must be `*.BIN`, with size `<= kMaxCoreBytes`;
  - if the core is already held (PSRAM, USB cache or BL616 cache) it replies 0;
  - else it allocates and replies 1.
- `handlePut` 964-989 is the same piece path as a module.
- `handleCommit` 991-1060 -> `commitCore` 1240-1285 -> `core_store.cpp checkEvoImage` 597-606:
  - at least 192 bytes, at most a 1 MB slot;
  - `checkImage`: sync word `A5C3`, IDCODE `06 00 00 00 00 00 08 1B` (GW2AR-18), multiboot jump address 0,
    UserCode `VD4Q` (the same checks as the game's `tools/package.py check_core`).
- After that the image goes to USB, the BL616 cache, or stays in PSRAM.

`core_load(name, &version)` (`coreCoreLoad` 2464-2499, `core_store.cpp load` 527-575, `waitCore` 355-395):

1. Programs the Tang through BL616 JTAG.
2. Waits for LINK `VERSION` with personality >= 3, returns that word (`0x51010003` for core 0.03, `0x51` = `Q`).

`core_back` (2501-2520), called on app unload too, returns the Tang to slot 0 (TS-Conf).

Model for us:

- Keep the held core as a named blob owned by the firmware (as resources are), and charge it to PSRAM.
- Validate it with the same header checks: they are cheap, and they catch a wrong file.
- Never interpret the bitstream: the emulator knows cores by name and UserCode. `VD4Q` maps to the bomzh-3D
  personality.
- `core_load("Q2")` then succeeds only if a core named `Q2.BIN` with UserCode `VD4Q` is held. This is what the
  board does: without the loader's flag-2 step there is no core.

### 3.2 bomzh-3D (SPEC 0.4)

- 8 MB SDRAM with an 8-bit indexed frame and a 16-bit z buffer ("float" 5+11).
- Line stride is W rounded up to 16 (424 -> 432). The game's mode 12 is 424 x 240, which HDMI scales x3 to 1272 x 720.
- On-chip: colormap 64 x 256, palette 256 x RGB, wave table 128 x int32; the alphamap (256 x 256) lives in SDRAM.
- Command stream: 32-bit words, opcode in bits 31:24.

  | Opcode | Command | Notes |
  |---|---|---|
  | 01 | TEX | |
  | 02 | LMAP | |
  | 03 | POLY | mode bits PERSP, LIGHT(2), ZTEST, ZWRITE, SKY, WARP, TRANS(2), KEY, ZGT |
  | 04 | POINT | |
  | 05 | PIC | |
  | 06 | FILL | |
  | 07 | FRAME | shows the frame; flash RGBA |
  | 08 | TIME | |
  | 09 | WRITE | |
  | 0A | SETUP | 9 words |
  | 0B | SCISSOR | |
  | 0C | VBUF | |
  | 0D | STBUF | |
  | 0E | TRIS | |
  | 0F | MARK | |

LINK map of the core (`bz_top.sv:138-156`, read mux 504-518). The game uses all of these (`bz_link.c:50-73`).

| Addr | Name | R/W | Model |
|------|------|-----|-------|
| 0x00 | ID | R | `0x56444334` "VDC4" |
| 0x04 | VERSION | R | `0x51010003` |
| 0x08 | STATUS | R | bit 0 fifo overflow (sticky), 1 bad command (sticky), 2 SDRAM ready (1), 3 boot request, 4 armed (M_SEL seen), 5 M_SEL now, 6 read copy busy |
| 0x0C | FIFO_FREE | R | free words, depth 2048, binary |
| 0x10 | FRAMES | R | FRAME commands done |
| 0x14 | EVO_PAGE | RW | bit 0: SDRAM half for Evo-SPI loads |
| 0x18 | EVO_BYTES | R | bytes taken from the Evo |
| 0x1C | FREE_GRAY | R | `{0x47, 12'd0, gray(FIFO_FREE)}` (core 0.03; the game reads this one) |
| 0x3C | SCRATCH | RW | The ESP firmware keeps its `0x5C......` tag here and checks it every 2 s. The game only reads it. Keep it (do not zero it on the switch) or the HLE firmware's tag check (if modeled) sees a lost core. |
| 0x84 | SIZE | RW | `{0x535A, size}`; the game copies TS-Conf CTRL (LINK 0x40, bits 16:15) before `core_load` |
| 0x88 | RD_ADDR | W | SDRAM byte address, multiple of 32 |
| 0x8C | RD_LEN | W | bursts of 32 bytes, 1..128; the write starts the copy into the window |
| 0x90 | MARK | R | last MARK value executed |
| 0xEC | SD_TRAIN | R | constant |
| 0xF0 | BOOT | W | `0x424F4F54` -> back to slot 0 |
| 0x1000-0x1FFC | window | R | 4 KB read window |
| 0x40000-0x7FFFC | queue | W | each word is the next stream word (`link_burst`) |

Also in the RTL, a second input: `bz_evo_in.sv`. The Evo SPI with FT812 write framing (`10` + 22-bit address,
then data) writes straight into SDRAM, and an FT812 `RST_PULSE` (`68 00 00`) or 0.5 s of silence after M_SEL
returns to slot 0. The current Q2.ELF does not use `EVO_PAGE` (no reference in `esp/main/`): textures come
through the LINK queue. Model the reset rule; the SDRAM write path can wait.

Proposed emulation model:

- A new personality `Q2` in `Vdac4Tang`, entered by `core_load` and left by BOOT, `core_back`, the Evo reset
  rule, or power.
- It owns a `bz_t` from the vendored C model, as `core/src/3rdparty/bomzh/` (MIT, Andrew Lazarev):
  - integer only, so the picture is bit-exact on all platforms with no libm;
  - this is the model the RTL is checked against, so it is the reference.
- Queue: words written to 0x40000.. go to a 2048-word buffer.
  - Execute it with `bz_exec` synchronously on the ESP job thread when the buffer is full, at the end of each
    `link_burst`, and before any read of FIFO_FREE / FREE_GRAY / FRAMES / MARK / STATUS / the window.
  - The model then reports FIFO_FREE = 2048 and the game never waits.
  - This is deterministic (the work is fixed by the stream) and needs no thread of its own.
  - A cost model (the RTL's cycles per pixel, so frame pacing is right) can come later, through the FRAMES /
    MARK lag.
- `bz_exec` returns the index of a bad word. Set STATUS bit 1 and log it; the game reports "command stream
  broken" (`bz_link.c:300-345`).
- RD_ADDR / RD_LEN copy `bz_read` into the 4 KB window at once; STATUS bit 6 stays 0.
- Picture: on FRAME, convert `bz_shown_rgb` (W x H) into the Screen's external picture the way `EveHostCard`
  latches FT812 frames (`core/src/emulator/platforms/tsconf/evehostcard.cpp:341-353`, `SetExternalPicture`).
  Use 424 x 240 with the x3 scaling left to the renderer, or a 1272 x 720 latch (open question Q3).
  SIZE (100 / 95 / 90 %) is the same crop the TS and FT personalities apply.
- Where: `Vdac4Tang` today mixes the TS and FT personalities in `LinkRead` / `LinkWrite`. Add a small
  `Vdac4BomzhCore` class (registers, queue, `bz_t`, picture) and dispatch to it from both methods while the
  personality is `Q2`. While in `Q2`, Evo SPI traffic goes to that class (reset rule), not to the FT812.

## 4. Other firmware behavior Q2 relies on

| Behavior | Firmware | Ours today | Need |
|----------|----------|------------|------|
| Flag 4 data file query `[04][len][time][name,0][app,0][index]` | `handleDataQuery` 587-645: since 0.1.167 always reply 5 (remote) when an index is given; 0 if already cached on USB | treated as a module | Reply 5; register `name -> (app, index, size, time)` for `data_open` |
| ESP -> Z80 event 0x6E `[req][file][offset LE32][len LE16]` | `remoteFetch` | `SendEvent` exists (`vdac4espfirmware.cpp:951`) | Send the request; three attempts of 4 s |
| Z80 -> ESP `6F [req][offset LE32][data]`, no reply; empty piece = error | `handleDataPiece`; framer accepts `6F` on raw media (`protocol.cpp:51`, prefix 5, `spi_link_core.cpp:193-199`) | `6F` rejected on raw media (`vdac4espframes.cpp:60`, `vdac4esplinkcore.cpp:205-212`) | Accept `6F` in frames and raw DMA (prefix 5) |
| Blocking `data_read` from the game task | The task sleeps on a semaphore until pieces arrive | App runs as jobs on a worker thread (`research-esp-waits.md`) | `data_read` must wait for Z80 time to pass: the job yields while the Z80 runs, like `delay` |
| `realloc` shrink in place (0.1.166+) | `coreRealloc` 2021-2060 (`heap_caps_realloc`) | always moves (`esp/espapphost.cpp:1234`) | Shrink in place; grow in place if the next block is free |
| Firmware string | `vdac4-0.1.174` | `unreal-ng-hle-0.1.122` | `vdac4-0.1.174-unreal-ng-hle` |
| App unload -> `core_back` | `module_app.cpp` 2501-2520 | `ZmCoreBack` OK, no Tang change | Return the Tang to TS on unload / APP_STOP |
| PSRAM | The board had 4192 KB free before `Qcommon_Init` (PROGRESS.md); Q2 `.bss` is 2.0 MB; `sys_esp.c` sizes its level area from what is free | 7.5 MB budget (`vdac4espfirmware.h:58`) | Match the board's free PSRAM while Q2 runs, otherwise "works in the emulator, fails on the board" goes unseen (Q4) |
| Input, sound | None in the game yet: `in_esp.c` is empty, `snddma_null`; any Evo key makes the loader stop the app | none | none now |

## 5. Xtensa instruction coverage

Method:

- Recursive descent over `.text` (0x27238, 0x6855D bytes), from `e_entry` and every R_XTENSA_RELATIVE target
  that starts with ENTRY (2061 functions).
- The 6739 relocated words inside `.text` are literal pools, so the walk stops there.
- It reaches 386 506 of 427 357 bytes; the rest is literals and padding. Mnemonics come from `unidasm -arch xtensa`.

Result against `core/src/3rdparty/unreal-xtensa/xtensa-isa.cpp` / `xtensa-cpu.cpp`:

- No MAC16 and no 4-byte `EE.*`. A linear sweep shows `mula.dd.*`, `l32e`, `rsr.sNN`, but those are literal
  data. The walk's single MAC16 hit (0x83EF5) lies inside an `l32r` at 0x83EF4: a walk artifact.
- No `DIV0.S`, `RECIP0.S`, `SQRT0.S`, `RSQRT0.S` (float division is the `__divsf3` import). These are the only
  FP ops the interpreter leaves unimplemented (`xtensa-cpu.cpp:1883-1895`).
- Used and implemented: base + density, windowed calls, `loop` / `loopnez` / `loopgtz`, MUL32 / MUL16 / MULSH /
  MULUH, DIV32 (`quos` / `quou` / `rems` / `remu`), `nsau`, `clamps`, `sext`, `min` / `max`, `s32c1i` /
  `l32ai` / `s32ri`, boolean ops (`orb` / `andb` / `any4` / `all4` / `any8` / `all8`, `bt` / `bf`), and the
  single-precision FPU:
  - `lsi` / `ssi` / `lsx` / `ssx` and the post-increment forms, which unidasm names `lsiu` / `lsxu`, ours
    `lsip` / `lsxp`;
  - `add.s` / `sub.s` / `mul.s` / `madd.s` / `msub.s`;
  - conversions `float.s` / `ufloat.s` / `trunc.s` / `utrunc.s` / `round.s` / `floor.s` / `ceil.s`;
  - `wfr` / `rfr`, compares, and `mov*.s`.

  About 6.9k FP ops in total.

No ISA work is needed. Speed is a separate question: Quake II on one Xtensa core is the heaviest app yet.

## 6. Plan

Each step is testable on its own through WebAPI / MCP: `GET /api/v1/emulator/{id}/vdac4/state` (packets,
`last_step`, app, Tang personality) and the ESP log. The order follows the blockers, so every step moves the
loader's border or the game's log further.

| # | Step | Check |
|---|------|-------|
| 1 | Firmware identity: `kFirmware` -> `vdac4-0.1.174-unreal-ng-hle`; SYS_INFO `FW:` in the same form; `kAbiMinor` stays 19 for now | Unit: a fake app's `start()` sees the string; DOOM / R-Type still start |
| 2 | MOD_QUERY flag 2: `*.BIN`, size <= 1 MB, header checks (§3.1), held core blob charged to PSRAM, reply 0 when held with the same length and time | Loader passes step 3; state shows `core: Q2.BIN VD4Q`; unit test with a 192-byte synthetic header and a bad one |
| 3 | MOD_QUERY flag 4: parse `[app,0][index]`, reply 5, register the file per app | Loader reaches APP_START; `#80A6` waits; state lists PAK0-2 with sizes |
| 4 | 0x6E / 0x6F: `SendEvent(0x6E)`, accept `6F` as a frame and as raw media (prefix 5) in `vdac4espframes.cpp` and `vdac4esplinkcore.cpp`, route pieces to the waiting request | Unit: framer round trip; integration: a test app reads 64 KB at a random offset of a card file and compares bytes |
| 5 | ABI 1.20: `data_open` / `data_read` / `data_close` slots (`ZmDataOpen`, ...), `kAbiMinor` 20, 8 handles, three 4 s attempts, `data_read` waits in Z80 time | Q2 `start()` passes, the log shows `data_open pak0.pak` and the first reads; unit tests for not-found / handles exhausted / short read at EOF |
| 6 | ABI 1.21: `link_read_checked` = plain reads (the emulated LINK has no bit errors); `kAbiMinor` 21 | Q2 log: `checked reads` |
| 7 | `CoreRealloc` shrink (and grow) in place | Unit: pointer kept on shrink; Q2 passes `Hunk_End` |
| 8 | `core_load` / `core_back`: look up the held core, switch the Tang to the new `Q2` personality, return `0x51010003`; APP_STOP / unload -> `core_back` -> TS | State: personality `q2` after start, `ts` after a key; Q2 log `LINK check (core open)` clean |
| 9 | `Vdac4BomzhCore`: vendor `gpu/model/bomzh.c` / `.h` into `core/src/3rdparty/bomzh/` (zero warnings on all compilers), LINK map §3.2, queue + synchronous `bz_exec`, RD window, MARK / FRAMES, STATUS | Unit: a short command stream (FILL, POLY, FRAME) against a golden hash from the model; register reads per the table |
| 10 | Picture: FRAME -> external picture (palette, flash, SIZE crop), shown while the personality is `Q2` | Screenshot through WebAPI after the demo starts; frame hash stable across runs |
| 11 | PSRAM budget at board level while an app with a held core runs (Q4); new env variable gets its row in `docs/emulator/environment-variables.md` | Q2 log shows the free PSRAM figure close to the board's 4192 KB |
| 12 | Automation and state: vdac4 state gains `core` (held cores), `bomzh` (frames, mark, queue words, bad command), `data` (registered files, open handles, requests / pieces / bytes / timeouts) on all planes; snapshot / TTD of the new personality (registers + 8 MB SDRAM + on-chip tables) | MCP live script for the new fields; TTD seek check over a demo frame |

Fixtures: the real PAK0 is 184 MB. Tests use a tiny PAK built in the test (a header plus one file), the
synthetic core header and a small test ELF. The full game run (demo `base2` / `waste2` loop) is a manual
acceptance check through WebAPI with the release folder on an SD image.

## 7. Open questions for the owner

1. **Q1 model.** Vendor `bomzh.c` (MIT, bit-exact with the RTL, 881 lines) or write our own? Vendoring keeps
   us equal to the board by construction.
2. **Q2 firmware string.** `vdac4-0.1.174-unreal-ng-hle` claims firmware 0.1.174. Is that acceptable, or must
   the number track the features we actually emulate?
3. **Q3 picture size.** Latch 424 x 240 and let the renderer scale, or 1272 x 720 as the HDMI shows?
4. **Q4 PSRAM.** Reduce the free PSRAM to the board's figure while Q2 runs, or keep the larger emulator budget?
   The author's branch had a 1440 KB reserve (`UNREAL_VDAC4_PSRAM_RESERVE_KB`).
5. **Q5 remote reads.** Keep the Z80 path (0x6E / 0x6F through the loader: faithful, ~100 KB/s, slow level
   loads), or also add a host-side USB cache (reply 0, read from a host folder, ~1 MB/s as on a board with a
   flash drive)?
6. **Q6 TTD scope.** Must the `Q2` personality join time travel from the start? The 8 MB SDRAM per keyframe is
   the cost.
7. **Q7 Evo SPI input** (`bz_evo_in`, texture loads from the Evo). The current game does not use it; implement
   only the reset rule for now?

## 8. Implementation (2026-11-27)

The owner's answers to §7: Q1 vendor the model; Q2 the string `vdac4-0.1.174-unreal-ng-hle`; Q3 latch 424 x 240;
Q4 match the board's free PSRAM with an env knob; Q5 the Z80 path only; Q6 no TTD for `q2`, refuse cleanly; Q7
the reset rule only. Q6 was revised later the same day: `q2` joins time travel (§8.1).

| Plan step | Where |
|-----------|-------|
| 1, 5, 6 firmware string, ABI 1.21 (`data_open` / `data_read` / `data_close`, `link_read_checked` = plain reads) | `esp/espapphost.h/.cpp` (`DataOpen`, `DataReadCall`, `DataReadStep`: 8 handles, 8 KB block cache, whole-sector requests straight to the destination up to 32 KB, three 4 s attempts, journaled pieces, state save tail) |
| 2, 3 MOD_QUERY flags 2 and 4 | `vdac4espfirmware.cpp` (`HandleCoreQuery`, `CommitCore` with `CoreImageProblem` = `checkImage`, `HandleDataQuery`, `AddRemote`, up to 8 files) |
| 4 0x6E / 0x6F | `vdac4espframes.cpp`, `vdac4esplinkcore.cpp` (prefix 5), the firmware routes pieces to the runtime |
| 7 realloc in place | `esp/espmemory.cpp` `EspHeap::Resize` |
| 8 `core_load` / `core_back` | `vdac4espfirmware.cpp` `CoreLoad` -> `IVdac4TangLink::LoadCore` -> `Vdac4Tang::LoadCore` (only UserCode `0x56443451` is emulated); `core_back` clears the app's core only on success |
| 9 bomzh-3D | `core/src/3rdparty/bomzh/` (MIT, `VENDORED.md`; two local changes: `bz_shown_rgb` reads through `rd8`, and the state functions `bz_state_save` / `bz_state_load` / `bz_reset` / `bz_layout` for time travel), `vdac4bomzh.h/.cpp` (`bz_top.sv` LINK map, queue parsed per command and run at once, WRITE streamed, RD window, boot rules) |
| 10 picture | `Vdac4Tang::PresentQ2` (`PresentFrame`, called by `Vdac4Board::OnFrameEnd` after the runtime's flush): a new FRAME -> RGBA -> `Screen::LatchExternalFrame` at the frame end (not in a TTD replay); the Tang owns the external picture while `q2` runs, whatever M_SEL says |
| 11 PSRAM | `UNREAL_VDAC4_PSRAM_RESERVE_KB`, default 1440 KB (the author's board figure): Q2 logs `PSRAM free 4336 KB` before init (board 4192). With 1584 KB it logs exactly 4192 KB but runs out of PSRAM on base2 (`malloc: no memory for 58200 bytes`, level area 2240 KB): Qcommon_Init takes 1248 KB here against 1024 KB in the author's emulator (PROGRESS.md: 4192 -> 3168). Open: find those ~220 KB, then calibrate to 4192 |
| 12 automation | `tang.q2`, `esp.psram_free`, `esp.cores`, `data` on WebAPI / WebSocket, CLI, Lua, Python, MCP; time travel and TTD session files cover `q2` (§8.1) |

Timing model: the core draws a command when its last word arrives, so FIFO_FREE is always 2048 and `rd_busy` is 0;
a word that starts no command sets the sticky `bad_cmd` and is dropped. Back to slot 0 on BOOT, on the Evo's
RST_PULSE (`68 00 00`), or after M_SEL 1 -> 0 and 0.5 s without Evo SPI traffic.

Live run (WebAPI, headless, .20 M3 Ultra): loader -> core and three PAKs registered -> app started -> `core_load`
-> `Map: base2` after about 200 s of PAK reads through the Z80 path (~80 KB/s, about 2000 requests and 20 MB to the
first demo frame, no repeats or failures) -> the demo draws at the game's own 30-35 fps figure while the machine
runs at about 49 fps. The firmware's LINK watchdog is modeled (`Vdac4EspFirmware::PollLinkWatch`): `link_diag` at
core open logs `SCRATCH 00000000 BAD` (the fresh bitstream, as on the board), then the firmware logs `LINK: Tang
reloaded (SCRATCH 00000000), core 51 version 0.3` and writes its mark back. On the switch the Tang latches the black
424x240 picture at once, so the window does not show the last TS-Conf frame in the new geometry.

Tests: `core/tests/emulator/machines/tsconf/vdac4quake2_test.cpp` (core / data queries, bad bitstreams, the 0x6F
frame, bomzh registers, FILL / FRAME picture, WRITE + RD window, bad words, boot rules, heap resize) and two Tang
tests in `vdac4tang_test.cpp`.

### 8.1 Time travel and session files

The `q2` personality records, seeks, steps back, branches and goes into TTD session files (`ttd/dump`,
`ttd/load`) like the rest of the machine. TS-Conf has no machine snapshot format, so a session file is the
snapshot of a `q2` run.

| Part | Where |
|------|-------|
| bomzh model state | `bz_state_save` / `bz_state_load` (`BZ_STATE_BYTES` = 26856: registers, queue / stream state, on-chip tables; no pointers), `bz_layout` = the SDRAM pointer; `Vdac4BomzhCore::Save` / `Load` add the LINK side (window, Msel, boot rules, counters) |
| Tang | `Vdac4Tang::SaveQ2` / `LoadQ2`: running flag, picture size, counters, the core, and the picture source: `Black`, `Core` (rebuilt from the core's shown buffer) or `Raw` (RGBA bytes, only when the shown buffer no longer holds the latched picture). `LeaveQ2` keeps the model, so a restore across `core_load` / `core_back` finds the same SDRAM pointer |
| TTD device | `TTDVdac4Q2` (`ttdvdac4.h/.cpp`, model state type `Vdac4Q2` = 81, restored after `Vdac4EspCpu`): per capture the Live part (`SaveQ2(Live)`: running, picture, the core's Evo side and hash); the 8 MB SDRAM as engine region `vdac4.q2.sdram` (`TTDRegionId::Vdac4Q2Sdram` = 24) |
| Between keyframes | the whole core goes into the ESP CPU's keyframes, the SDRAM is marked only then; between them the restore feeds the core the LINK writes of the ESP journal's replay (`EspAppHost::SetReplayLinkSink` -> `Vdac4Tang::Q2ReplayLinkWrite`) and checks the core's hash ([ttd-design.md](ttd-design.md) §11). TTD off: no marks, no cost |

Live run (WebAPI, headless, muted, .20 M3 Ultra; the second figure is the first version of 2026-11-27, which stored
the SDRAM pieces at every capture, under a load average of 35-80; this one ran under 5-24):

| Measure | Value |
|---------|-------|
| ms/frame, TTD off | 8.7 mean (8.1-9.3), 48.83 fps (was 8.2) |
| ms/frame in the demo, recording | 9.0 mean (7.6-9.9), 48.83 fps (was 10.4) |
| Stream in the demo (the dump, frames 24700-28300, 72 s) | **58 MB/min** (was 278): `vdac4.q2.sdram` 27 (was ~250), ESP PSRAM 17, ESP CPU state 9, Evo RAM 2, the rest 3 |
| Stream while a map loads (frames 18500-24300) | 32 MB/min stored (Evo RAM 6.6, ESP CPU state 6.1, SDRAM 6.0, PSRAM 5.5, ESP firmware state 5.2); 25 MB/min by the history counter |
| The loader run (TS-Conf boot to `core_load` and SETUP, 86 s) | 22 MB in the history (was 205: the firmware's delivery shifted its state, [ttd-design.md](ttd-design.md) §10); session file 32 MB (was 213) |
| One session of 446 s (two demo maps, two loads) | **43 MB/min** stored (was 132): SDRAM 14.9, PSRAM 9.4, ESP CPU state 6.9, Evo RAM 5.0, ESP firmware state 3.6, `vdac4q2` 0.07 |
| SDRAM stores | at every ESP keyframe (25 captures; 50 since, below): median 107 pieces, 218 KB; whole (5.3 MB) at the engine's keyframes |
| Keyframes (one a minute) / delta checkpoints | 8.1-8.9 MB / median 3 KB, largest 0.58 MB |
| Session file | 352 MB (was 1014), dump 1.5 s, load 1.0 s, 4/4 points exact |
| Seeks | 27/27 in the demo and 14/14 across `core_load` exact (screen, `tang.q2` counters, PC); demo 15-151 ms, median 28; loader 11-19 ms |
| Step back / forward | 15 ms, the same screen as a seek, forward exact |
| Branch | made in the demo, ran 10 s, seeks to and from it exact |

What remains of the demo's stream is the ESP's own keyframes (PSRAM, the CPU state, and the SDRAM pieces the
frame and z buffers rewrite between two of them): K trades them for the seek's replay. K swept 10-400
([ttd-design.md](ttd-design.md) §9.5): while the core runs the ESP keys every 50 captures
(`TTDVdac4EspCpu::kKeyframeFramesQ2`), elsewhere every 25:

| K | Demo MB/min | Session MB/min | Session file | Seek median / p90 / max |
|---|---|---|---|---|
| 25 (before) | 40.7 | 33.4 | 228 MB | 19 / 166 / 184 ms |
| **50 (now)** | **27.8** | **28.6** | **199 MB** | 18 / 181 / 326 ms |
| 100 | 21.4 | 25.9 | 183 MB | 19 / 382 / 490 ms |

(The sweep's session: frames 300 to about 18,500, one map, shorter than the 446 s run above.) Seeks across `core_load`
13-19 ms, step back 16-18 ms, recording unchanged.

Tests: `vdac4quake2state_test.cpp` (`Vdac4BomzhState_Test`: fixed size, a state cut mid-command, mid-WRITE, after an RD copy and at the stream's other points of a stream with
WRITE / RD / triangles / FRAME goes on as the uncut run, foreign state refused, reset, the Evo side apart from the LINK side,
shown serial), `Vdac4Tang_Test.Quake2StateRestoresAcrossCoreLoadAndBack`, and
`TTDEngineModels_Fixture.Vdac4Quake2CoreRestoresAtEveryCheckpoint` (20 frames across `core_load`, `core_back` and a
reload: personality, `SaveQ2` bytes, SDRAM and screen equal at every checkpoint),
`Vdac4Quake2CoreOffTheJournalIsReportedNotExact`, `Vdac4Tang_Test.Quake2ReplayFeedEpochAndLivePart`.
