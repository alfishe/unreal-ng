# AI Agent Recipe Library

Battle-tested, copy-pasteable recipes for AI agents (and humans) driving the
Unreal-NG emulator through its two automation surfaces:

- **WebAPI** — REST over HTTP, `http://localhost:8090/api/v1/...`
- **MCP** — Model Context Protocol, `POST http://localhost:8092/mcp`
  (or the stdio bridge `cmake-build-release/bin/unreal-mcp-bridge`, already
  wired in the repo root `.mcp.json`)

Every recipe carries two sections — **MCP (preferred)** and **WebAPI** — plus
a short transport note at the top. **MCP is the preferred transport**: start
with the MCP section. Use the WebAPI section only when a step runs through
host-side Python tools (they speak plain HTTP) or when MCP is unavailable
(server/bridge fault). Recipes show the exact requests, the response fields
worth asserting on, and the pitfalls that actually bite (409s, feature gates,
drive restrictions, pause races).

## Conventions used by every recipe

- `EMU_ID` is the instance UUID returned by `POST /api/v1/emulator/start`.
  MCP recipes pass `target: "auto"` instead and let the server resolve it.
- `BASE` is `http://localhost:8090/api/v1` for WebAPI recipes.
- Shell examples are bash + `jq`; MCP examples are raw JSON-RPC 2.0 bodies for
  `curl -X POST http://localhost:8092/mcp`.
- **MCP notation:** recipes write MCP calls compactly as `tool {"args"}` —
  expand into the standard `tools/call` envelope shown in
  [_common/transports.md](_common/transports.md).
- Every file path passed to the emulator must be **readable by the emulator
  process** (it loads it server-side). The MCP bridge can additionally upload
  a file that exists on the *agent's* machine (embedded content).
- Test artifacts (`.trd`, `.tap`, `.sna`, traces, `.ttd` dumps) belong in
  `scratch/` — never in the project root.

## Picking recipes for a task (read this before the index)

Most tasks need 2-3 recipe files, not the library. Don't read a whole
category folder "just in case" — each recipe states its own scope in its
first paragraph; if that doesn't match your task, go back to the index
below instead of skimming further.

**Step 1 — machine model, only if the task depends on it.** If the task
names or implies a specific model (Pentagon/Scorpion/Profi/ATM/TS-Conf/Sprinter/Spectrum),
read that one `machines/<model>.md` file for its port map and known
gotchas. The other machine files document different hardware and won't help — skip
them entirely. Don't know the model yet? `_common/machines.md` is the
comparison table. Sound-card work (GS/MoonSound/TurboSound/Covox) is the
same idea one level down: read the one matching `peripherals/*.md` file,
skip the other three.

**Step 2 — what are you actually doing?** Pick the one row below that
matches; it names the recipe(s) for that action.

| If the task is... | Read... | Skip |
|:--|:--|:--|
| First call of the session / no instance yet | [_common/setup.md](_common/setup.md) | everything else until the instance exists |
| Loading a disk/tape/snapshot | the matching [media/](media/) file for that format | the other media files |
| Making loaded software actually run (autostart/`RUN`/tape play) | the matching [run/](run/) file for the load path you used | machines/peripherals, unless the model itself matters |
| Recording/replaying/seeking machine state | [analysis/ttd-recording.md](analysis/ttd-recording.md) (+ [ttd-reverse-debugging.md](analysis/ttd-reverse-debugging.md) for `find-last`/`reverse-continue`) | port-trace, memory-counters |
| Pressing joystick buttons / checking what the guest reads at `IN #1F` | [input/joystick.md](input/joystick.md) | everything else |
| Watching port I/O | [analysis/port-trace.md](analysis/port-trace.md) | ttd-*, memory-counters |
| Capturing the VDAC2 card's FT812 bus (an .evr replay stream) | [machines/tsconf-vdac2.md](machines/tsconf-vdac2.md) | port-trace |
| Counting/mapping memory access | [analysis/memory-counters.md](analysis/memory-counters.md) | port-trace, ttd-* |
| Recording a video (with sound) of a run | [media/video-recording.md](media/video-recording.md) | everything else |
| Debugging a visual/screen bug | [analysis/ttd-visual-inspection.md](analysis/ttd-visual-inspection.md) + [media/agent-screenshot-view.md](media/agent-screenshot-view.md) | everything else until you have a reproducible frame |
| Detecting a custom loader / triaging a hang | [analysis/nonstandard-loader.md](analysis/nonstandard-loader.md) | port-trace (it's composed in already) |
| A full multi-step investigation (memory corruption, boot verification, disk protection) | check [articles/](articles/) first — it may already compose the recipes you'd otherwise assemble by hand | the individual recipes it cites; open those only if the article sends you there |

**Step 3 — transport, only if a command isn't working.**
[_common/transports.md](_common/transports.md) explains MCP vs WebAPI —
read it once per session if you're unsure which to use, not before every
call.

## Library index

### `_common/` — read these first

| Recipe | What it covers |
|:--|:--|
| [_common/setup.md](_common/setup.md) | Build, launch, kill stale instances, port discipline, model choice, instance lifecycle |
| [_common/transports.md](_common/transports.md) | WebAPI vs MCP: when to use which, request idioms, `search_api`/`invoke_api` escape hatch, parity rule |
| [_common/machines.md](_common/machines.md) | Model table (RAM options, creatability), create/identity patterns, per-machine introspection endpoints, branch matrix |

### `media/` — getting software into the machine

| Recipe | What it covers |
|:--|:--|
| [media/use-media-slots.md](media/use-media-slots.md) | Every media slot (floppy drives, SD card): list, insert a file or a **host folder**, `auto` slot choice, swap multi-disk software with save/export/discard, export the guest's writes |
| [media/insert-disk.md](media/insert-disk.md) | Insert/eject disk images (`.trd .scl .fdi .udi .dsk .td0 .mgt .img .ima`), drive A/B, blank disks, catalog/sysinfo inspection, Sprinter 1.44 MB floppies and the density latch |
| [media/insert-tape.md](media/insert-tape.md) | Load/eject tapes (`.tap/.tzx`), play/pause/seek/rewind, block catalog, fast-load plan, WAV import |
| [media/load-snapshot.md](media/load-snapshot.md) | Load/save snapshots (`.sna/.z80/.szx`, TS-Conf `.spg` load), verify a state took effect, snapshot round-trips |
| [media/play-rzx.md](media/play-rzx.md) | Play RZX input recordings (`.rzx`): model switch, progress, desync diagnosis, conventions |
| [media/author-udi-images.md](media/author-udi-images.md) | Creating proper UDI images: format capability matrix, host-side conversion/authoring, in-emulator formatting, weak-bit limits |
| [media/video-recording.md](media/video-recording.md) | Record the screen to GIF/H.264/HEVC with an optional sound track (`audio: aac`) from MCP, WebAPI, CLI, Lua, Python; codec/container rules; ffprobe checks |
| [media/agent-screenshot-view.md](media/agent-screenshot-view.md) | Viewing emulator screen as agent: native MCP/WebAPI server-side binary saving without base64 transcript corruption |
| [media/sprinter-hdd.md](media/sprinter-hdd.md) | Sprinter hard disks: IDE slots ide0/ide1, mounting an HDD image, booting DSS from it (DSS 1.71 needs BIOS 3.06), state ide |

### `run/` — making software actually run

| Recipe | What it covers |
|:--|:--|
| [run/autostart-disk.md](run/autostart-disk.md) | One-call disk boot (`autostart: true`), the boot decision table, what to assert |
| [run/manual-trdos-run.md](run/manual-trdos-run.md) | Manual path: insert without autostart, read the catalog, enter TR-DOS, `RUN "NAME.B"` for a chosen file |
| [run/tape-fastload.md](run/tape-fastload.md) | Tape loading: `LOAD ""` + play, block seeking, fast-load, detecting load completion |

### `input/` — driving the machine's input devices

| Recipe | What it covers |
|:--|:--|
| [input/joystick.md](input/joystick.md) | Kempston joystick: press / release / set / tap / status on MCP, WebAPI, CLI, Lua and Python; which models decode `IN #1F`; the paused-machine queueing; reading the byte from a guest loop |

### `analysis/` — instrumenting the machine

| Recipe | What it covers |
|:--|:--|
| [analysis/ttd-recording.md](analysis/ttd-recording.md) | TTD on/off, gaming vs development journal, dump/save `.ttd`, load back, seek/step, bookmarks, coverage heatmap |
| [analysis/ttd-reverse-debugging.md](analysis/ttd-reverse-debugging.md) | Reverse queries: `find-last`, `reverse-step`, `reverse-continue`, coverage probe/scan |
| [analysis/port-trace.md](analysis/port-trace.md) | Port I/O tracing: feature gate, filters/presets, ring buffer, save `json/csv/bin/binz`, re-read server-side, internal port codes (ZX-Evo, Sprinter) |
| [analysis/memory-counters.md](analysis/memory-counters.md) | Memory access counters: profiler start/stop, per-page summaries, per-address counters, YAML export |
| [analysis/nonstandard-loader.md](analysis/nonstandard-loader.md) | Detect custom loaders (port-PC attribution, fastdisk litmus, structural pre-scan), trace hangs and crashes |
| [analysis/ttd-visual-inspection.md](analysis/ttd-visual-inspection.md) | Record once with TTD, then seek/step to any frame for guaranteed-stable inspection (registers, video aspect, per-frame screenshot) |
| [analysis/sprinter-ttd.md](analysis/sprinter-ttd.md) | Sprinter time travel: what a checkpoint holds (PLD, Z84C15, VRAM, fast RAM, keyboard / mouse streams, floppy command), record / seek / dump on MCP, WebAPI, CLI |
| [analysis/sprinter-mame-compare.md](analysis/sprinter-mame-compare.md) | Sprinter boot port trace against MAME's `sprinter` driver, access by access (port, value, PC, internal code); new MAME captures |

### `machines/` — per-model recipes

| Recipe | What it covers |
|:--|:--|
| [machines/pentagon.md](machines/pentagon.md) | Pentagon 128/512/1024 via `ram_size`, Pentagon-1024 `#EFF7` register (GigaScreen, 512x192, a4b), sound-stack defaults |
| [machines/scorpion.md](machines/scorpion.md) | SCORPION/PROFSCORP, Shadow Monitor `#1FFD`, ProfROM `#7EFD`, built-in Beta128, SOS/128K ROM bit |
| [machines/profi.md](machines/profi.md) | Profi 1024: `#7FFD`+`#DFFD` paging, RTC/CMOS, Covox port arbitration, hi-res video, TTD paging, IDE hard disk |
| [machines/tsconf.md](machines/tsconf.md) | TS-Conf (`TSL`): TS-BIOS, the `#xxAF` registers and memory map, TS video modes and TSU, DMA, SD slot `sd.zc`, `.spg` programs, `state tsconf` |
| [machines/tsconf-vdac2.md](machines/tsconf-vdac2.md) | TS-Conf VDAC2 card (FT812): setup (`ENABLE_VDAC2`, `TS_VDAC2=1`, ROM image), games from an SD folder, the FT812 bus capture (`vdac2 capture`, `/vdac2/capture/*`, `capture_media vdac2_capture_*`) |
| [machines/atm.md](machines/atm.md) | ATM710 + ATM3/ZX-Evo: `#FF77` control, `#FFF7` memory manager, CP/M bit, CMOS shaden ports, turbo, video modes, hard disk and CD slots |
| [machines/sprinter-sound.md](machines/sprinter-sound.md) | Sprinter Sp2000 sound: one AY at 1.75 MHz, Covox / Covox-Blaster; play a WAV with DSS's `WAVPLAY.EXE` from a hard disk, `state/sprinter` `sound`, capture and AAC recording, the MAME comparison |
| [machines/sprinter-accelerator.md](machines/sprinter-accelerator.md) | Sprinter Sp2000 block accelerator: run `ACCTEST.EXE` from a DSS floppy, check its picture in the graphics RAM, the accelerator state |
| [machines/spectrum.md](machines/spectrum.md) | 48K/128k/PLUS3: the real-Sinclair boundary, AY/FDC per model, clone-vs-Sinclair differential debugging |
| [machines/zxpoly.md](machines/zxpoly.md) | ZX-Poly: four synchronized instances of one model, `.zxp` / `.prom` / multiloader disk, 16-colour and 512x384 modes, group status and lockstep check |
| [machines/sprinter.md](machines/sprinter.md) | Sprinter Sp2000 (`SPRINTER`): BIOS selection at create / runtime and full / fast start, DSS from a 1.44 MB floppy, typing DSS commands, Spectrum mode + TR-DOS, `state sprinter` (PLD, windows, accelerator, waits, Z84C15), video modes per square, palettes, video RAM (`vram` region), the video change log, digest / raw framebuffer, the port table and its codes, screen text |

### `peripherals/` — sound cards and DACs

| Recipe | What it covers |
|:--|:--|
| [peripherals/generalsound.md](peripherals/generalsound.md) | GS card: BASS HLE vs Z80 LLE (branch), `#B3/#BB/#33` mailbox, firmware ROMs, GS state/port-trace/reset modes, capture proof |
| [peripherals/moonsound.md](peripherals/moonsound.md) | OPL4 card (branch-only): `#C4`-`#C7` FM banks, `#7E/#7F` wave regs, YRW801 ROM, clone-only policy, hiss/HiFi known issues |
| [peripherals/turbosound.md](peripherals/turbosound.md) | TurboSound slot: `AY` pair vs TSFM (YM2203), `/state/audio/ay`+`/fm` endpoints, register decode math, loudness calibration |
| [peripherals/covox-sounddrive.md](peripherals/covox-sounddrive.md) | CovoxFB/CovoxDD/SoundDrive toggles, quad-DAC ports `#F1-#FB`, mono compat mode, capture+trace verification |
| [peripherals/network.md](peripherals/network.md) | Network adapters: ZXNETUSB (W5300) card, the virtual network (DHCP, DNS, gateway, guest servers), NedoOS setup, state on every interface, what hangs without a card |
| [peripherals/cmos-rtc.md](peripherals/cmos-rtc.md) | CMOS clock on ATM3 / Profi / Scorpion+SMUC: report, cell read / write on every interface, setting the time, battery files |

### `articles/` — full workflows that combine recipes

| Article | What it covers |
|:--|:--|
| [articles/bug-hunt-ttd.md](articles/bug-hunt-ttd.md) | "Who corrupted this memory?" — snapshot + TTD + `find-last` + `reverse-continue` |
| [articles/demo-boot-verification.md](articles/demo-boot-verification.md) | Deterministic boot verification: autostart + `run_frames` + screen digest + OCR |
| [articles/disk-protection-triage.md](articles/disk-protection-triage.md) | Disk protection triage: catalog vs raw sectors + FDC state + `fdc-only` port trace |
| [articles/physical-protection-forensics.md](articles/physical-protection-forensics.md) | Physical copy-protection forensics: diskinfo structural scan, clock bitmap, flaky-sector differential test, RE the check |

## Ground truth links

- WebAPI endpoint registry: `core/automation/webapi/src/emulator_api.h`
  (`ADD_METHOD_TO` list), interactive spec at `http://localhost:8090/api/v1/openapi.json`
- Machine model table: `mem_model` in `core/src/emulator/config.h`;
  per-model decoders: `core/src/emulator/ports/models/`; model configs:
  `data/configs/*/unreal.ini`
- Sound stack: `core/src/emulator/sound/` (AY/TurboSound/TSFM/Covox chips);
  branch designs: `docs/inprogress/2026-09-19-general-sound/`,
  `docs/inprogress/2026-09-13-moonsound/`, `docs/inprogress/2026-09-21-profi/`
- MCP tool catalog: [docs/features/mcp/README.md](../docs/features/mcp/README.md),
  implementation `core/automation/mcp/README.md`
- Port-trace design: `docs/inprogress/2026-08-24-diagnostic-observability/`
- TR-DOS autostart policy: `docs/inprogress/2026-09-18-trdos-autostart/design.md`
- Universal track model / UDI / flaky sectors: `docs/inprogress/2026-09-02-universal-track-model/`,
  `docs/WD1793/FlakySectorEmulator.md`; worked protection case study:
  `docs/disasm/black-raven-voron-protection/`
- Host-side disk forensics: `tools/diskinfo/diskinfo.py`, conversion/authoring:
  `tools/diskconverter/diskconvert.py`
- Ready-made Python capture tools: `tools/porttrace/porttrace_capture.py`,
  `tools/verification/debugger/capture_debug_session.py`

## Rules of engagement (the ones agents forget)

1. **One emulator can own port 8090.** Kill stale instances before starting a
   fresh one (see [setup](_common/setup.md)).
2. **Scrubbing during TTD recording is a 409** — always `POST /ttd/stop`
   before `seek`/`step-back`/`find-last`.
3. **Disk autostart is drive A only** — a hard error elsewhere, never silent.
4. **Port trace needs its runtime feature on** —
   `PUT /feature/porttrace {"enabled": true}` (or let the capture tools do it).
5. **`target: "auto"` in MCP** creates a 128K machine when none exists and
   refuses when several exist — pin the id for multi-instance work.
6. **Responses are the contract** — assert on `status`/`state` fields, never
   on prose. The parity rule guarantees MCP and WebAPI return identical data
   from the same handlers.
