# Can the TS-Conf TUI debugger run on the WebAPI?

- **Date:** 2026-10-03
- **Checkout:** master at `a42be6408` (main checkout)
- **Scope:** read-only research. Nothing was built or run; no live emulator was queried.
  Timing figures are estimates from the code, marked as such.
- **Status (2026-10-03):** order 1 (a step no longer parks on a breakpoint; `stop` in the step
  replies; `/steps` ends at a hit) and order 3 (WebSocket events `paused` / `resumed` /
  `step_done` / `breakpoints_changed` with `seq`; no long-poll fallback) are built - see
  [.recipe/analysis/breakpoints-and-events.md](../../../.recipe/analysis/breakpoints-and-events.md).
  D1 (`/state/tsconf`: `regs`, programmed DMA addresses, `dma.ctrl`, `sys_config` / `cache_en`,
  `memory.fm_maps`, `video.line.t0_gpage` / `t1_gpage`) and D2 (`/registers`: `memptr`, `q`, `t`,
  `halted`, `boundary` - the debugger protocol's Registers names; `im`, `iff1`, `iff2`, `memptr` / `wz`
  writable; R bit 7 round-trips - `/registers`, Lua and Python reported R with bit 7 at 0x4000) are built
  on every surface, and so is D4's page part (`"page":{"kind":"ram","page":32}` on `POST /breakpoints`,
  CLI `bp` / `wp --page ram32`, Lua / Python `bp*`, MCP `bp_add`; listed and in the status). PC history
  (E2) is left to the TTD v2 work. The rest of D4 (ranges, physical keys, hit counts, port masks) follows
  the hot-path design of
  [conditional-breakpoints/design.md §5.2](../2026-08-17-conditional-breakpoints/design.md); the other
  additions use the debugger protocol's fields and routes
  ([protocol.md](../2026-09-28-debugger-model/protocol.md)).

**Short answer:** yes, most of it is possible, and part of it already works today. The POC
already has a WebAPI client. About 60% of the screen can be fed from existing endpoints. To
reach the rest you need roughly 8 server additions. Three of them are needed to make the
debugger behave correctly, not just look complete:
1. A step must not freeze the HTTP server when it lands on a breakpoint.
2. The server must tell the client when the machine stops (push events, or at least a
   long-poll call).
3. The TS-Conf state report must include the raw register bank.

---

## 1. What the TUI debugger is

| Item | Finding | Source |
|---|---|---|
| Location | `tools/poc/018-tui-debuggers/` holds two independent apps: `classic/` (80x30) and `tsconf/` (157x30 fork, binary `dbgtsconf`) | `tools/poc/018-tui-debuggers/README.md:1-14` |
| Spec | TDD-DBG-01 (classic Unreal Speccy monitor) and TDD-DBG-02 (TS-Conf / ZX-Evo fork) | `docs/inprogress/2026-09-24-tui-debugger/TDD-DBG-02_unreal-tsconf-debugger-tui.md:15-51` |
| Tech | C++20, FTXUI 7.1.0 terminal UI, vendored z80ex for the mock machine | `tools/poc/018-tui-debuggers/README.md:84-86` |
| Committed | `815d1797c`, 2026-09-25 | `git log` |
| Role going forward | The POC's widget and field set is the content source for the "model-first debugger". The TUI is meant to become one skin over that model's protocol | `docs/inprogress/2026-09-24-tui-debugger/TODO.md:1-5`, `docs/inprogress/2026-09-28-debugger-model/README.md:85-95` |
| How it gets data | Everything goes through one interface, `IDebuggerBackend`. Implemented: `MockBackend` (an in-process z80ex machine with canned data) and `RestBackend` (an HTTP client for the unreal-ng WebAPI, selected with `--backend rest --endpoint http://localhost:8090`). WebSocket and IPC backends are stubs | `tsconf/src/backend/debugger-backend.h:1-18, 193-199`; `tsconf/src/main.cpp:9, 97-119` |
| What `RestBackend` does today | On every stop it fetches the registers and the whole 64K as 16 requests of 4096 bytes. It disassembles, assembles and searches locally over that copy. Every request opens a new TCP connection (`Connection: close`) | `tsconf/src/backend/rest-backend.cpp:150-224`; `rest-backend.h:19-24`; `http-client.cpp:371` |
| TS-Conf parts over REST | Not wired. `GetTsConf()` and `GetPcHistory()` keep their default "no data" implementations, so the PC-history column and the register board stay blank under `--backend rest` | `debugger-backend.h:181-190`; `tsconf/README.md:21-24, 97-99` |
| Known REST gaps (the POC author's list) | No T-state counter, no port write, no latched port / beta128 / AY values, no conditional breakpoints. `PUT /debugmode` together with breakpoints "deadlocks POST /steps". Several of these are partly outdated, see §3 | `rest-backend.h:26-29`; `rest-backend.cpp:128-131` |

---

## 2. Widget / operation -> WebAPI

Legend: **OK** = an endpoint serves it today. **Partial** = usable, but data is missing, the
shape is wrong, or it is slow. **Missing** = no endpoint.

All paths are relative to `/api/v1/emulator/{id}`. Route table: `core/automation/webapi/src/emulator_api.h`
(line numbers below as `h:NNN`).

### 2.1 Panels (what is painted)

| Widget (spec) | WebAPI call(s) + params | Status | Notes |
|---|---|---|---|
| **Registers** (TDD-01 §4.1) | `GET /registers` (h:370) | Partial | Returns AF..IY, alternates, PC, SP, I, R (8-bit, merged), IFF1/2, IM and decoded flags (`debug_api.cpp:1264-1314`). Missing: **halted** flag (needed for the `DiHALT` cell), T-state within the frame, `memptr`. Change highlighting has to be done on the client by comparing with the previous response. That is fine, and it is what the POC does (`rest-backend.cpp:155`). |
| **Time delta** (TDD-01 §4.9) | `GET /video/beam` (h:243) gives `tstate_in_frame`, `frame` and `frame_timing.frame_tstates` | Partial | `DeviceState::VideoBeam` (`core/src/emulator/state/devicestatevideo.cpp:161-224`). Absolute T = frame x frame_tstates + tstate_in_frame. The client keeps the "mark" itself. Caveat: the TS-Conf CPU clock changes (3.5/7/14 MHz) affect how T converts to frames, so the formula may drift on clock switches (unverified). The POC says "no T-state counter" (`rest-backend.cpp:277-281`). That claim is outdated: this endpoint exists. |
| **Trace / disassembly** (TDD-01 §4.2) | `GET /disasm?address=&count=` (h:384), max 100 lines (`debug_api.cpp:2427`). Or client-side over a memory copy (what the POC does) | OK / Partial | Each line has address, bytes, mnemonic, size, label, target and targetLabel (`debug_api.cpp:2455-2510`). Not served: (a) a **backward step** ("previous instruction", TDD-01 §4.2.5); (b) **branch taken / not taken at PC** for the arrow. The client can compute both from memory and registers, as the POC does with `UnrealBranchInfo`. The loop stops at the 64K wrap (`currentAddr >= address`, `debug_api.cpp:2437`). |
| **Breakpoint marks in trace** | `GET /breakpoints` (h:360) | OK | Returns id, type, address, execute/read/write flags, active state, note, group (`debug_api.cpp:808-898`). The client builds the per-address bitmap. |
| **Memory dump** (TDD-01 §4.4, `ED_MEM`) | `GET /memory/{addr}?len=&format=full` (h:376, max 4096 bytes, `debug_api.cpp:1465`). Or `GET /memory/read/{addr}?length=N` (h:211, `length` is uint16, so up to 65535 bytes in one call) | OK / Partial | Reads have no side effects (`DirectReadFromZ80Memory`, `debug_api.cpp:1490-1495`; `state_memory_api.cpp:584-586`). The only formats are hexdump, full and sparse, all JSON (`state_memory_api.cpp:43-88`). "full" sends every byte twice, as a number array and as a hex string, so about 7 bytes of JSON per memory byte. There is **no binary format**. `length=65536` wraps to 0 (`state_memory_api.cpp:557-562`). |
| **Watches** (8 bytes at PC, SP, BC, ...) (TDD-01 §4.3.1) | Registers + memory reads | OK | Done on the client. No server-side "watch" object exists (the model protocol plans `/debug/watches`, `debugger-model/protocol.md:518`). |
| **Stack** (TDD-01 §4.7) | `GET /memory/read/{SP-2}?length=20` | OK | Done on the client. |
| **Pages** (4 windows, names, read-only) (TDD-01 §4.8, TDD-02 §4.8) | `GET /state/paging` (h:208): `banks[]{type, page, read_write, name, role, contended}`. Or `GET /memory/info` (h:372): `z80_banks.bankN.mapping` = bank name | OK / Partial | `state_memory_api.cpp:1546-1636`; `debug_api.cpp:2120`. Gives everything needed to build `RAM%2X` / `ROM%2X` names on the client. For TS-Conf, `read_write` is hard-coded "read/write" for windows 1-3 (`state_memory_api.cpp:287-302`) and does not reflect W0_WE for window 0 the way the TS-Conf mapper sees it (unverified for TS-Conf). The POC still paints static 128K names (`rest-backend.cpp:255-261`). |
| **Ports** (FE, 7FFD, ext/cmos, EFF7) (TDD-01 §4.5) | `GET /state/paging` -> `latches[]{port, latch, value, decoded}` plus `paging_locked` | Partial | Latches tagged "memory" only (`state_memory_api.cpp:1519-1543`, `1643`). So 7FFD and the model's paging ports are there. **#FE and #EFF7** are not paging latches. FE's border bits appear as `border_color` in `/state/screen` (`devicestate.cpp:1387`), but the full FE byte (MIC/EAR) does not. `GET /ports` (h:239) returns the decoder's port map, not values (`ports_api.cpp:51-103`). The POC says "no latched ports" (`rest-backend.cpp:340-344`), which is partly outdated. |
| **DOS indicator** | `GET /state/paging` -> `trdos_active` | OK | `state_memory_api.cpp:1644`. |
| **Beta 128** (CD, STAT, SECT, T, S) (TDD-01 §4.6; TDD-02 §4.6 raw STAT) | `GET /state/fdc` (h:284) | OK | Command, track, sector, data and raw status registers, the Beta128 system register, INTRQ/DRQ, selected drive, and per drive the head track (`core/src/emulator/state/devicestate.cpp:1286-1334`). The fork wants the raw status, and that is what is served. The POC does not use it yet (`rest-backend.cpp:346-349`). |
| **AY registers** (TDD-01 §4.10) | `GET /state/audio/ay` (h:257), `GET /state/audio/ay/{chip}` (h:258) | Partial | 16 registers, decoded channels, TurboSound flag, chip count (`devicestate.cpp:52-118, 575-620`). Missing: the **latched register** (last write to #FFFD, highlighted in the panel) and which TurboSound chip is selected. No such field was found in the report (grep for `latched` / `selected` came up empty). |
| **Screen preview** (TDD-02 §4.3: ZX screen from page 5 or 7, border = TS border) | `GET /memory/page/ram/{5|7}?length=6912` (h:216) + `GET /state/tsconf` -> `video.border` | OK | The fork draws the ZX screen itself, so raw page bytes are enough. For the classic "real frame" mode: `GET /capture/framebuffer?format=index|rgba&encoding=binary` (h:134, `capture_api.cpp:108-160`) or `GET /capture/screen` (PNG). Whether `format=index` works on TS-Conf (15-bit CRAM colors) is unverified: it may answer 409 (`capture_api.cpp:131-134`). |
| **PC history** (TDD-02 §4.11: 32-entry ring of `page:addr` at every M1 fetch) | none. Closest: `GET /profiler/opcode/trace?count=32` (h:397) after `POST /profiler/opcode/start` | Missing (stand-in: Partial) | The opcode-profiler trace returns recent `pc, prefix, opcode, flags, a, frame, tstate` (`profiler_api.cpp:410-480`). That is one entry per instruction (not per M1 fetch), has **no page**, and must be started first. `GET /calltrace` returns an always-empty list (`debug_api.cpp:2360` `// TODO`). The model protocol plans `GET /debug/pchist` (`debugger-model/protocol.md:520`). |
| **TS-Conf board** (16 controls, TDD-02 §5.2) | `GET /state/tsconf` (h:300) + `GET /state/tsconf/tsu` (h:302) | Partial | See §2.3: about 75% of the board fields are present. The ones that are missing (DMA control bits, programmed vs live DMA addresses, SysConfig, raw FMAddr) all exist in the core state but are not in the JSON. |
| **Memory editors: CMOS** (TDD-02 §4.4) | `GET /rtc/cells?start=0&count=256` (h:325), `POST /rtc/cells` (h:326) | OK | `state_device_api.cpp:676-757`. TS-Conf binds its RTC (the ZX-Evo AVR) through `GetRtcBinding` (`core/src/emulator/ports/models/portdecoder_tsconf.h:127-133`). The POC returns zeros (`rest-backend.cpp:362-365`), which is outdated. |
| **Memory editors: NVRAM (2048)** | none | Missing | No endpoint found. |
| **Memory editors: disk physical / logical track** | `GET /disk/{drive}/track/{cyl}/{side}[/raw]` (h:107-108), `GET /disk/{drive}/sector/{cyl}/{side}/{sec}[/raw]` (h:101-103) | Read OK, write Missing | No sector or track **write** route exists in the route table. The editor is therefore read-only. |
| **TSU sprites / tiles / CRAM** (not in the original board, but part of the TS-Conf debug views) | `GET /state/tsconf/tsu` | OK (read) | All 85 sprite descriptors decoded, both tile layers, and the 256 CRAM cells with RGB (`core/src/emulator/platforms/tsconf/tsconfdevicestate.cpp:202-293`). CRAM and SFILE cannot be **written**: TS-Conf registers no device-memory region. Only the Sprinter does (`core/src/emulator/ports/models/portdecoder_sprinter.h:163`; the base hook is `portdecoder.h:751`). |

### 2.2 Operations (what the user does)

| Operation (spec) | WebAPI call(s) + params | Status | Notes |
|---|---|---|---|
| Attach / list / create instance | `GET /api/v1/emulator`, `POST /api/v1/emulator/start {"model":"TSL"}` | OK | The POC always attaches to the first instance and creates "128k" when none exists (`rest-backend.cpp:93-137`). That is a client choice, not an API gap. |
| Pause / continue | `POST /pause` (h:53), `POST /resume` (h:54) | OK | |
| **Know that the machine stopped** (breakpoint hit, step-over done) | Poll `GET /{id}` (`state`, `is_paused`) or `GET /breakpoints/status` (`last_triggered_*`, `is_paused`) | Partial (polling only) | `lifecycle_api.cpp:573-577`; `debug_api.cpp:1172-1240`. No push: the WebSocket `/api/v1/websocket` echoes "ACK: ..." (`emulator_websocket.cpp:27`). Its `broadcastEmulatorData` publishes into a private static PubSub that nobody subscribes to (`emulator_websocket.cpp:104-114`), and nothing in the tree calls it. The `last_triggered_*` fields persist across stops, so the client must remember the previous value to notice a new hit. |
| Step (F7) | `POST /step` (h:342) | Partial, **with a server-freeze hazard** | Runs on the HTTP thread (`debug_api.cpp:92-108` -> `Emulator::RunSingleCPUCycle(false)`, `emulator.cpp:2407-2424`). The reply holds only `pc`, `sp` and `state`: no stop reason, no "breakpoint hit". **Hazard:** with debug mode on, a breakpoint hit during the step calls `emulator.Pause()` and then `WaitWhilePaused()` on the calling thread (`core/src/emulator/cpu/z80.cpp:269-323`; memory and port breakpoints do the same at `memory.cpp:386-389`, `portdecoder.cpp:287-290`). That thread is the HTTP worker, so the request never returns until another client calls `/resume`. The server has only **2 worker threads** (`automation-webapi.cpp:335`), so two such hangs make the whole WebAPI unresponsive. This is the "deadlock" the POC observed (`rest-backend.cpp:128-131`). |
| Step N / run in batches | `POST /steps {"count":N}` (h:343), max 100000 (`debug_api.cpp:157`) | Partial | Same hazard. In addition, `RunNCPUCycles` ignores the step result and keeps going after a breakpoint (`emulator.cpp:2437-2459`), so the count does not stop at a hit either. |
| Step over (F8) | `POST /stepover` (h:344) | Partial | For CALL / RST / block instructions, `Emulator::StepOver` sets a temporary breakpoint, calls `Resume()` and returns right away (`emulator.cpp:3029-3033`). The reply's `pc` is therefore the PC *before* the call returns (`debug_api.cpp:218-230`). The client must poll for the stop. The fork's HALT rule ("stop at PC+1 after the handler", TDD-02 §9) has not been checked against `StepOver` (unverified). |
| Run until return (F11) | `POST /stepout` (h:345) | Partial | Synchronous. It **skips breakpoints** inside the callee (`emulator.cpp:3095-3110`), while the original stops on them. It also has no run-control check, unlike `/step` (`debug_api.cpp:252-262` vs `79`). |
| Run to cursor (F4) | `POST /skip_until {"pc":addr,"max_tstates":N}` (h:346) | Partial | Synchronous, default budget 100 frames, cap 700M T (`debug_api.cpp:345-363`). It **skips breakpoints** (`emulator.cpp:2862` `ExecuteStep(true, ...)`), so a breakpoint before the cursor is missed. A long run blocks one of the 2 HTTP threads. |
| Continue until breakpoint (`` ` `` / Esc) | `PUT /debugmode {"enabled":true}` (h:357) + `PUT /feature/breakpoints` (h:158, for read/write breakpoints, `memory.cpp:103`) + `POST /resume`, then poll | OK (with polling) | This path is safe: the emulation thread parks in `WaitWhilePaused`, not the HTTP thread. The POC avoided debug mode because of the step hazard and replaced Continue with 128-instruction `/steps` batches, checking breakpoints on the client (`rest-backend.cpp:399-483`). That approach can miss a breakpoint inside a batch and costs about 18 requests per stop. |
| Set PC (Z) | `PUT /registers/PC {"value":N}` (h:371) | OK | |
| Edit register / flag | `PUT /registers/{A|F|BC|...|AF'|IXH...}` | Partial | No entries exist for **IM, IFF1, IFF2** in the register table (`core/src/emulator/cpu/z80.cpp:2007-2048`). Writing `R` sets only `r_low` (`z80.cpp:2017`), while `GET /registers` builds R from `r_hi` and `r_low` (`debug_api.cpp:1293`), so bit 7 may not round-trip (unverified). |
| Edit memory / ASCII | `PUT /memory/{addr} {"data":[...]}` (h:377) or `POST /memory/write` (h:212) | OK | |
| Assemble at cursor (A) | `POST /assemble {"code","address","write":true}` (h:552) | OK | `debug_api.cpp:3853-3975`. The POC uses its own subset assembler instead. |
| Find bytes / text | `POST /memory/find {"pattern_hex","start","end","max"}` (h:213) | Partial | No **mask** (`ParseHexPattern` rejects anything but hex digits, `state_memory_api.cpp:727-745`). Z80 64K only: no CMOS, disk or page search. |
| Fill block, load / save block from binary file, disasm to file | Memory read/write + `/disasm` on the client | OK | Done on the client. Files live on the client side. |
| Load / save TR-DOS sectors | `GET /disk/.../sector/...` | Read OK, write Missing | No sector write endpoint. |
| Toggle exec breakpoint (F2/Space) | `POST /breakpoints {"type":"exec","address":N}`, `DELETE /breakpoints/{id}` (h:361, 363) | OK | `debug_api.cpp:901-1016`. Types: exec, read, write, port_in, port_out. |
| Breakpoint manager: ranges, R/W, enable/disable | `POST /breakpoints` per address; `PUT /breakpoints/{id}/enable|disable` (h:364-365) | Partial | No **ranges**: a `4000-57FF` write watch means 6144 requests and 6144 breakpoints. No **page** field over HTTP, although the core supports page-specific breakpoints (`AddExecutionBreakpointInPage`, `core/src/debugger/breakpoints/breakpointmanager.h:230-246`). |
| Conditional breakpoints (TDD-01 §10.2; fork adds RD, WR, MDT, PG0-PG3, TDD-02 §10) | none | Missing | No condition engine in the core: only `breakpointmanager.{h,cpp}` exist in `core/src/debugger/breakpoints/`. `docs/inprogress/2026-08-17-conditional-breakpoints/TODO.md:3-5` says "Phase 1 not implemented". This is a core feature gap, not only a WebAPI one. |
| Labels: show, jump-to-label, load `user.l` | `GET /labels` (h:531, includes `bank`/`bankType`, `debug_api.cpp:2758-2772`), `POST /labels`, `POST /symbols/load` (h:540, a file on the server side), `GET /labels/resolve` (h:536) | OK | |
| Labels import (XAS / ALASM scan) | none | Missing | The POC shows the disabled menu items (`rest-backend.cpp:722-739`). |
| Ripper (R/W/X coverage over 64K) | `POST /profiler/memory/start`, `GET /profiler/memory/counters?mode=z80` (h:400, 407); or `/coverage/*` (h:178-182) | OK | `profiler_api.cpp:881-890`. The POC still fakes "every byte unreferenced" (`rest-backend.cpp:743-755`), which is outdated. |
| Alt+B (write 7FFD), Alt+M (write ext port), board edit `OUT (#nnAF)`, set TS page (TDD-02 §13 `set_ts_page`, `out`) | none | Missing | No port-write route in `emulator_api.h`. The POC only latches the value locally (`rest-backend.cpp:524-543`). |
| Switch CPU (Ctrl+`) to the GS / NeoGS Z80 | `GET /state/audio/gs` (h:262): card CPU PC, SP, AF, halted only | Missing for debugging | No register file, step or memory view per card CPU. The model protocol adds `?cpu=` (`debugger-model/protocol.md:505-520`). |
| Snapshot / quick save / load, reset, NMI | `/snapshot/save|load` (h:117-118), `/reset` (h:55), `/nmi` (h:57) | OK / Partial | The reset variants (to 128 / 48 / DOS / cache / service ROM) were not checked (unverified). |
| Max speed toggle (fork `mon.maxspeed`) | `PUT /feature/{name}` (h:158), speed settings | Unverified | Which feature name controls max speed was not checked. |

### 2.3 TS-Conf register board, field by field (TDD-02 §5.2 vs `GET /state/tsconf`)

The report is built in `core/src/emulator/platforms/tsconf/tsconfdevicestate.cpp:63-200`.
Every TS-Conf register write is stored in `ts.regs[]` (`core/src/emulator/ports/models/portdecoder_tsconf.cpp:679`,
`tsconfstate.h:114`), so all the missing raw values exist in the core. They are just not in the JSON.

| Board control (TDD-02) | Field in `/state/tsconf` | Status |
|---|---|---|
| VConfig raw + RRES, NOGFX, NOTSU, GFXOVR, VMODE | `video.v_config`, `geometry`, `nogfx`, `notsu`, `gfxovr`, `mode` (:111-118) | OK (FT_EN is derivable from the raw `v_config`) |
| TSConfig raw + S_EN, T1_EN, T0_EN, T1Z_EN, T0Z_EN, bit 0 | `video.tsu.t_config` (:126) | OK (decode on the client) |
| SysConfig raw: CACHE_EN (bit 2), ZCLK | only `cpu_clock` as text (:193) | **Partial**: no raw byte, no CACHE_EN |
| CacheConfig raw | `memory.cache_config` (:102) | OK |
| MemConfig raw + LCK128, W0_RAM, W0_MAP, W0_WE, ROM128 | `memory.mem_config`, `lck128`, `window0_*`, `rom128` (:88-97) | OK |
| Bitmap: VPage, X/Y offsets (shown as hi/lo) | `video.v_page`, `gx_offset`, `gy_offset` (:119-123) | OK (split hi/lo on the client) |
| Tiles0/1: GPage, X/Y offsets, Z_EN/EN LEDs | `video.tsu.tile0_page`, `tile1_page`; `/state/tsconf/tsu` `tile_layers[]` (:226-242) | Partial: the board shows the **line-delayed copy** `t0gpage[2]` (TDD-02 §5.2 Tiles0). The API returns the register value (the core has `latT0GPage`, `tsconfstate.h:143`) |
| PalSel raw + T1PAL, T0PAL, GPAL | `video.pal_sel` (:120) | OK |
| Misc: TMPage, Border, FDDVirt + FDA-FDD LEDs | `video.tsu.tilemap_page`, `video.border`, `memory.fdd_virt` | OK |
| FMAddr raw: FM_EN, FM_MAPS | `memory.fm_window` (-1 or the base address) (:103) | Partial: derivable, but no raw byte |
| MemPages Page0-3 | `memory.pages[4]` (:93-96) | OK |
| Sprites: SGPage | `video.tsu.sprite_page` (:134) | OK |
| DMA: programmed SRC / DST | none. `dma.source` / `destination` are the **live** addresses (`ts.dmaSrc` "live source word address", `tsconfstate.h:155-156`) | **Missing**: the raw regs 1A-1F are not exposed |
| DMA: CURR SRC / DST, CURR NUM / LEN, ACTIVE | `dma.source`, `destination`, `blocks_left`, `words_left_in_block`, `busy` (:174-187) | OK (the counters appear only while busy) |
| DMA: NUM / LEN programmed | `dma.blocks`, `words_per_block` (+1 form) | OK |
| DMA: CTRL raw + OPT, S_ALIGN, D_ALIGN, A_SZ, DDEV | `dma.task` (a name from `dmaDevice` = CTRL bits 7 and 2:0, `tsconfstate.h:165`) | **Partial**: bits 3-6 are missing, and so is the raw byte |
| Interrupt: HSINT, VSINT (H/L), IntMask | `interrupts.frame_tact`, `frame_line`, `int_mask` (:158-169) | OK (also `pending[]`, which the board does not show) |

**Freshness (unverified):** the report reads `ts.dmaSrc` and the other live fields directly
(`tsconfdevicestate.cpp:175-186`). Reading DMA_STATUS from the guest first calls
`CatchUpEngine()` (`portdecoder_tsconf.cpp:661-663`). If the TS-Conf engine runs lazily, the
report could lag behind the CPU by a few T-states. This needs checking before relying on
per-step DMA updates (TDD-02 §5.4).

---

## 3. Missing pieces, grouped

### 3.1 Endpoints to add

| # | Endpoint | Why | Example |
|---|---|---|---|
| E1 | `POST /out {"port":"0x7FFD","value":"0x10"}`, with the full side effects of the decoder, recorded as a tool edit for TTD | Alt+B, Alt+M, TS-Conf board editing, setting a TS page | `{"port":"0x13AF","value":"0x20"}` maps page 0x20 into window 3 |
| E2 | `GET /debug/pchist?depth=32` -> `[{page, address}]`, newest first | PC history panel (TDD-02 §4.11). Needs a per-instruction ring that is armed only while a debugger is attached (performance rule: zero cost otherwise, `AGENTS.md` Performance) | `[{"page":5,"address":"0x8123"}, ...]` |
| E3 | `GET /debug/snapshot?disasm=21&memory=0x8000:96&stack=1` | One round trip per repaint, read under one pause, see §3.4 | as in `debugger-model/protocol.md:355-375` |
| E4 | Disk sector write: `PUT /disk/{drive}/sector/{cyl}/{side}/{sec}` | Disk editor writes, "save to TR-DOS sectors" | |
| E5 | NVRAM read / write | Editor `ED_NVRAM` | |
| E6 | TS-Conf CRAM and SFILE as device memory regions (`/memory/region/cram`, `/memory/region/sfile`) | Palette and sprite editing. The pattern exists: Sprinter `CollectMemoryRegions` | |
| E7 | Label import scans (XAS / ALASM) | Ctrl+A import menu | low priority |
| E8 | Long-poll fallback: `GET /debug/wait?since=<seq>&timeout_ms=1000` | Stop notification without WebSocket, for simple clients. Must not hold a worker thread; drogon supports async callbacks | answers `{"seq":42,"reason":"breakpoint","id":3}` or `{"timeout":true}` |

### 3.2 Data to add to existing endpoints

| # | Endpoint | Add | Size |
|---|---|---|---|
| D1 (done) | `GET /state/tsconf` | `regs` = all raw TS-Conf registers (hex). That alone fixes the programmed DMA SRC/DST, DMA CTRL bits, SysConfig/CACHE_EN and raw FMAddr. Also `t0gpage_line` / `t1gpage_line` (the delayed copies) | small |
| D2 (done) | `GET /registers` | `halted`, `t` (T in frame), `frame`, `memptr`; register-table entries for `IM`, `IFF1`, `IFF2` so `PUT` works; R bit 7 round-trip | small |
| D3 (done for `/step`, `/steps`) | `POST /step`, `/steps`, `/stepover`, `/stepout`, `/skip_until` | `stop_reason` (`step`, `breakpoint`, `target`, `budget`) + `breakpoint_id`; `/steps` should stop at a hit | small, but tied to F1 below |
| D4 (page done) | `POST /breakpoints` | `page` + `page_type` (the core already supports it), `address_end` (ranges), later `condition` | small (page, range); large (condition, needs the core engine) |
| D5 | `GET /state/audio/ay/{chip}` | `latched_register`, and at board level `active_chip` | small |
| D6 | Ports | #FE full byte, #EFF7, and on other models the extended port, e.g. in `/state/paging` or a new `GET /debug/ports` | small |
| D7 | `GET /memory/{addr}` and `/memory/read/{addr}` | `format=binary` (application/octet-stream). Allow `length=65536` | small |
| D8 | `GET /memory/find` | `mask` / wildcard bytes; other spaces (page, CMOS) | small |
| D9 | `GET /state/paging` (TS-Conf) | Correct `read_write` per window from the TS-Conf mapper | small, verify first |

### 3.3 Transport issues

| # | Issue | Evidence | Effect on the TUI |
|---|---|---|---|
| F1 | **A step that hits a breakpoint freezes the HTTP worker.** The breakpoint code pauses and then waits on the calling thread, and for HTTP stepping that thread is the drogon worker | `z80.cpp:269-323`, `memory.cpp:386-389`, `portdecoder.cpp:287-290`; worker count 2 at `automation-webapi.cpp:335` | Any step with debug mode on can hang the server. The POC must keep debug mode off and check breakpoints on the client, so read/write/port breakpoints never fire during REST stepping. The right fix is in the core: decide how a direct step reports a hit instead of parking (`feedback: no quick fixes in core`). |
| F2 | **No push events.** The WebSocket is a stub that echoes text. `broadcastEmulatorData` publishes into a PubSub nobody subscribes to, and nothing calls it | `emulator_websocket.cpp:11-53, 97-114`; `debugger-model/protocol.md:56-60` confirms | The TUI must poll while running to notice a breakpoint or a finished step-over. At 20 Hz polling, the stop shows up within about 50 ms, which is acceptable. Push becomes necessary for several clients at once, for "edited by another client", and for the timeline. The core already posts `NC_EXECUTION_BREAKPOINT` (with emulator id, breakpoint id and address, `z80.cpp:316-320`), `NC_EXECUTION_CPU_STEP` (no payload, `emulator.cpp:2423`) and state changes. A bridge to the WebSocket is what is missing. |
| F3 | **Too many round trips per repaint.** The POC's per-stop refresh is 1 step + 1 registers + 16 memory requests, each on a new TCP connection | `rest-backend.cpp:385-397, 204-224`; `http-client.cpp:371` | Estimate, not measured: 16 x ~28 KB of JSON ("full" format) is about 450 KB per step. Holding F7 down at keyboard repeat (~30/s) means ~13 MB/s of JSON parsing. A TS-Conf repaint adds `/state/tsconf` + `/state/fdc` + `/state/audio/ay` + `/state/paging` + `/video/beam` + pchist, about 6 more requests. Fixes: E3 (one snapshot call) or at least `/memory/read?length=65535` (1 request instead of 16), D7 (binary), and keep-alive on the client. |
| F4 | Synchronous long-running calls (`/skip_until` up to 700M T, `/stepout`, `/steps` 100000) occupy one of 2 workers | `debug_api.cpp:345-363`, `automation-webapi.cpp:335` | Run-to-cursor on a slow path can starve other requests, including the TUI's own polling. These calls should resume and report through events (F2) like Continue does. |
| F5 | Run control is claimed per surface: `/step`, `/steps`, `/stepover` answer 409 when another surface (for example GDB) holds it, but `/stepout`, `/skip_until` and the run_* calls do not check | `debug_api.cpp:79, 138, 202` vs `252-262, 297-300` | Inconsistent behavior when the TUI and another debugger are attached at the same time. |

### 3.4 Consistency (one coherent picture)

- **While paused:** separate GETs give a consistent picture, provided no other client writes
  in between. Nothing tells the TUI that someone else changed something: there is no
  `edited` event (F2).
- **While running:** registers, memory, `/state/tsconf` and `/video/beam` are read at
  different moments, possibly several frames apart. Example: the board can show DMA
  "ACTIVE" next to a PC from a later frame. There is no API that reads all of them at one
  instruction boundary. The model protocol's `/debug/snapshot` is meant for this
  (`debugger-model/protocol.md:355-375`). The TUI spec only paints while stopped
  (TDD-01 §9.3), so this matters only if the TUI adds a live view.
- **Memory reads while running** use `DirectReadFromZ80Memory` from the HTTP thread with no
  pause. Torn multi-byte views are possible (unverified, but there is no lock in
  `debug_api.cpp:1490-1495`).

### 3.5 TS-Conf specific gaps (summary)

1. Raw register bank missing from `/state/tsconf` (D1): blocks the DMA CTRL, programmed SRC/DST, SysConfig and FMAddr rows.
2. Line-delayed T0/T1 GPage copies not exposed (D1).
3. No PC history with page (E2).
4. No port write, so no board editing, no `set_ts_page`, no Alt+B on TS-Conf (E1).
5. CRAM / SFILE read-only (E6).
6. Bank-aware breakpoints (`PG3==20 && PC==0C000`, TDD-02 §10) need either the condition engine or at least the `page` field on `POST /breakpoints` (D4).
7. DMA live-field freshness at step granularity: unverified (§2.3).

---

## 4. Verdict

**Feasible with additions.** The POC already proves that live registers, memory,
disassembly, stepping and breakpoints work over the WebAPI
(`tools/poc/018-tui-debuggers/README.md:44-56`). Several gaps it lists are already solved
by existing endpoints that the POC does not use: `/video/beam` (T-states), `/state/fdc`
(beta128), `/state/paging` (7FFD latch, DOS flag, page names), `/rtc/cells` (CMOS),
`/state/tsconf` (most of the board), `/profiler/memory/counters` (ripper), `/assemble`.

**Can be wired today, client-side only, with no server change:** registers, trace, memory,
watches, stack, pages, 7FFD, DOS indicator, beta128, AY (without the latched-register
highlight), time delta, ZX screen preview, CMOS, disk view (read), labels, exec/R/W/port
breakpoints, assemble, find (no mask), and about 75% of the TS-Conf board. Continue works
with debug mode on plus polling.

**Additions, in recommended order:**

| Order | Addition | Rough size | Why this order |
|---|---|---|---|
| 1 | **F1 + D3**: direct steps report a breakpoint hit (stop reason in the reply) instead of parking the HTTP thread; `/steps` stops at a hit | M (core: decide the rule for a direct step vs a running machine, tests for every breakpoint kind) | Without it, debug mode cannot be turned on from a step client, so R/W/port breakpoints are unusable during stepping |
| 2 | **D1 + D2**: raw TS-Conf regs + delayed GPage; registers gain halted / T / IM / IFF writes | S (a few fields each, all data already in the core) | Completes the board and the register panel cheaply |
| 3 | **F2 / E8**: WebSocket events (`paused`, `resumed`, `step_done`, `edited`, `breakpoints_changed`, with `seq`) bridged from the existing `NC_*` notifications; long-poll as a fallback | M | Removes polling; needed for step-over, run-to and Continue to feel instant; also the base for the browser skin |
| 4 | **E3 + D7**: `/debug/snapshot` composite + binary memory | M | One request per repaint instead of 20+; fixes the consistency issue |
| 5 | **E1**: port write with full side effects | S-M (must go through the decoder and mark TTD) | Alt+B / Alt+M / board edit / TS page set |
| 6 | **E2**: PC-history ring with page, gated on "debugger attached" | M (hot path: needs an A/B benchmark per `docs/guidelines/performance-guidelines.md`) | The only TS-Conf panel with no data source at all |
| 7 | **D4 (page, range)**, D5, D6, D8, D9 | S each | Polish: bank-aware breakpoints, AY highlight, FE/EFF7, find mask |
| 8 | **E4, E5, E6, E7** | S-M each | Disk / NVRAM / CRAM editing, label import |
| 9 | **Conditional breakpoints** (+ RD/WR/MDT/PG0-3) | L (the core condition engine does not exist; `2026-08-17-conditional-breakpoints/TODO.md:3-5`) | Largest item; independent of the TUI |

Sizes: S = under a day, M = a few days, L = a week or more. These are rough estimates.

**Fit with the model-first direction:** items 1, 3, 4 and 6 are exactly the `DebugService`,
event bridge, snapshot and PC-history pieces in `docs/inprogress/2026-09-28-debugger-model/protocol.md`
(§1, §3.8, §3.17, §5). Building them for the TUI is not throw-away work. Items 2, 5 and 7
are small enough to add to today's routes first and to carry into the model later.

### Not verified

- Live latency and payload sizes (estimated from code only; no emulator was run).
- `capture/framebuffer?format=index` on TS-Conf.
- `StepOver` HALT behavior vs the fork's "stop at PC+1 after the handler".
- R register bit-7 round trip on `PUT /registers/R`.
- DMA live-field freshness in `/state/tsconf` (lazy engine catch-up).
- TS-Conf window read-only flags in `/state/paging`.
- Reset variants and the max-speed feature name.
