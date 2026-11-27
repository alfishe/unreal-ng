# Recipe: TS-Conf with the VDAC4 board (Tang FT812 core + ESP32-S3)

The VDAC4 board sits on the ZX-Evo's IDE connector, where the VDAC2 card sits. It has **no FT812 chip**: a Tang
Nano 20K FPGA shows TS-Conf over HDMI and, on demand, loads an FT812 emulation core in the same SPI slot (`#77` bit
2), and an ESP32-S3 on the same bus (`#77` bit 4) runs ELF applications the Z80 uploads to it. DOOM, R-Type VDAC4 and
HMM2-VDAC4 are such applications: the game runs **on the ESP**, the Z80 runs only its launcher (upload, file reads
from the Evo SD, input). The emulator runs the apps on its own Xtensa core (a JIT by default) with the ESP firmware as
HLE, and the Tang's FT812 core as eve-emu with a VDAC4 profile.

Ground truth:
[vdac4control.h](../../core/src/emulator/platforms/tsconf/vdac4/vdac4control.h) (what every surface calls),
[vdac4board.cpp](../../core/src/emulator/platforms/tsconf/vdac4/vdac4board.cpp) (the board, its ini and env),
design in [docs/inprogress/2026-11-15-tsconf-vdac4/](../../docs/inprogress/2026-11-15-tsconf-vdac4/design.md)
(time travel: [ttd-design.md](../../docs/inprogress/2026-11-15-tsconf-vdac4/ttd-design.md)).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use [WebAPI](#webapi) inside host-side pipelines
> (policy: [_common/transports.md](../_common/transports.md)). The TS-Conf basics are in [tsconf.md](tsconf.md); the
> FT812 picture, bus capture and line budget commands are the VDAC2 ones ([tsconf-vdac2.md](tsconf-vdac2.md)).

## VDAC2 or VDAC4: which machine

Both are TS-Conf in the firmware's VDAC2 build (STATUS `VDAC_VER` 7, BLT2, no IDE) with an FT812 picture on the same
chip select; they differ in what answers there and what else sits on the bus.

| | `TSL-VDAC2` (alias `TSCONF-VDAC2`) | `TSL-VDAC4` (alias `TSCONF-VDAC4`) |
|---|---|---|
| The board | VDAC2 card: a real FT812 (Bridgetek EVE2) | VDAC4: Tang Nano 20K (TS-Conf to HDMI, FT812 core on demand) + ESP32-S3 + PCM5102 DAC |
| FT812 | the chip, there from power-on | eve-emu with the VDAC4 profile (§5.2): REG_ID `0x7C`, RAM_G 2 / 3 MB, PALETTED8 as one ARGB8888 entry, 3D commands A0-AF, LOADIMAGE_BOX / IMAGEINFO, PNG / WebP / progressive JPEG; present only after the Tang switches to it (MISO 0 for `LoadDelayMs`, 174 ms) and gone again after 500 ms with `M_SEL` 0 and no FT traffic |
| Who runs the game | the Z80 (the game drives the FT812 itself) | the ESP (the Z80 runs the launcher) |
| Software | R-Type VDAC2, Heroes II, Zuma Deluxe VDAC2, the TS-Labs SDK demos, Weather 2 | DOOM-VDAC4, R-Type VDAC4, HMM2-VDAC4, the ESP App SPG demos; also the VDAC2 programs, through the Tang core |
| Sound | the Evo's (TS-Conf / AY / SD) | the Evo's plus the ESP's DAC (mixer row `vdac4_esp`) |
| Board state surface | `vdac2 capture`, `vdac2 metrics` | the same on the Tang's chip, plus `vdac4 state`, `vdac4 log`, `vdac4 esp-memory` |
| Host cost | the FT812 only | plus the ESP app on a worker thread (DOOM: about 2 ms of host time per frame on an M3 Ultra) |
| Time travel | the FT812's state per frame | plus ESP keyframes every 50 frames and an input journal; a seek re-runs the ESP from its keyframe (`replay_diverged` if it does not reach the recorded state) |

Use **`TSL-VDAC2`** for VDAC2 software, for the FT812 as a chip (its timing, its exact register file from power-on,
the line budget of a real card) and for anything where the extra ESP thread only costs time. Use **`TSL-VDAC4`** for
the ESP games and demos (they need the ESP; on `TSL-VDAC2` their launcher finds no ESP and stops), and to check how a
VDAC2 program behaves on a VDAC4 board (the Tang's late FT812, its profile differences). The ini switch is
`[MISC] TS_VDAC2=1` or `[MISC] TS_VDAC4=1`; the two never match the same configuration.

## Setup

- **The machine:** `TSL-VDAC4`, a machine variant (`variant: true` in the models list). unreal-qt: Machine menu ->
  **TS-Conf + VDAC4**. An instance reports `variant: "TSL-VDAC4"`.
- **The build:** `ENABLE_VDAC4=ON` (default; needs `ENABLE_VDAC2`, the vendored eve-emu, and builds `unreal-xtensa`).
  Without it the variant is not offered and a VDAC4 configuration refuses to load with the reason.
- **The game files** go on the Evo SD (slot `sd.zc`) as a host folder; the launcher reads them from there. DOOM:
  `DOOM/_DOOM.SPG`, `DOOM.ELF`, `DOOM1.WAD` (shareware) in one folder. R-Type VDAC4: `_rtype_vdac4.spg`, `RTYPE.ELF`,
  `RTYPE.DAT`. HMM2-VDAC4 needs the game's own `HMM2.DAT` (not shipped).
- **ini** (`[VDAC4]`, all optional):

  ```ini
  [MISC]
  TS_VDAC4=1            ; the VDAC4 board, no IDE

  [VDAC4]
  RomImage=rom/ft81x.rom ; the FT812 ROM fonts, shared with [VDAC2]
  RamG=2M               ; power-on RAM_G (2M / 3M); the ESP switches it through FT_CFG
  EspCache=             ; the ESP's flash (module cache, path.cfg); default ~/.unreal-ng/vdac4/zm in the desktop app,
                        ; an empty folder per board for headless instances and tests
  EspMhz=240            ; the ESP clock the app sees (millis, CCOUNT)
  EspThread=1           ; the app's jobs on a worker thread (0: on the emulation thread; same results)
  FtThread=0            ; the Tang work of those jobs on a thread of its own
  FtDrawThread=1        ; the FT812 drawing on the eve-emu renderer thread
  EspLatencyMs=4        ; 1-16: part of the emulated behaviour (another value = another, still deterministic, run)
  EspBootMs=400         ; ESP boot time; it arms the Tang then
  EspLog=               ; the app log to a file
  LoadDelayMs=174       ; the Tang's slot switch
  ```

- **Switches** for A/B runs: `UNREAL_XTENSA_JIT=0/1` (or the `esp_jit` feature), `UNREAL_VDAC4_ESP_THREAD`,
  `UNREAL_VDAC4_FT_THREAD`, `UNREAL_VDAC4_FT_DRAW_THREAD`, `UNREAL_VDAC4_ESP_LATENCY`
  ([environment-variables.md](../../docs/emulator/environment-variables.md)).

## MCP (preferred)

```text
emulator_manage  {"action":"create","model":"TSL-VDAC4"}
invoke_api       {"method":"POST","path":"/api/v1/emulator/{id}/media/sd.zc/insert","body":{"path":"/path/to/sd"}}
load_software    {"path":"/path/to/sd/DOOM/_DOOM.SPG"}     # the launcher; it uploads DOOM.ELF and starts it
control_execution {"action":"run_frames","frames":100}    # repeat until vdac4_state shows the app running (DOOM: ~600-800 frames)
vdac4_state      {"log":20}                                # Tang personality, ESP phase, app name / version, link, audio, threads, log
vdac4_esp_memory {"address":"0x6b7ac","size":64,"app":true}  # the running app's memory by its ELF address
capture_media    {"action":"screenshot"}                   # the FT812 picture (1024x768) once the Tang is in FT and M_SEL is 1
analyze_performance {"action":"vdac2_line_budget"}        # the Tang chip's line costs: the VDAC2 commands serve it
```

- `vdac4_state` on a machine without the board is a tool error with the reason and `TSL-VDAC4` (`No VDAC4 Board: the
  machine is not TS-Conf: ...`).
- Mute the machine first (`PUT /api/v1/emulator/{id}/audio/mixer/master {"muted":true}`): DOOM plays sound.
- The state is the board's copy from the last frame end: polling it never parks the emulation thread.
- `vdac4_esp_memory` reads at the caller's moment, not at a frame end: a debugging read. With `app: true` the address
  is the app's ELF address, mapped through `app.sections`; without it a D-bus / I-bus / SRAM address.

## WebAPI

```bash
EMU=$(curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' -d '{"model":"TSL-VDAC4"}' | jq -r .id)
curl -s -X POST "$BASE/emulator/$EMU/media/sd.zc/insert" -H 'Content-Type: application/json' -d '{"path":"/path/to/sd"}'
curl -s -X POST "$BASE/emulator/$EMU/snapshot/load" -H 'Content-Type: application/json' \
     -d '{"path":"/path/to/sd/DOOM/_DOOM.SPG"}'

curl -s "$BASE/emulator/$EMU/vdac4/state?log=20" | jq '{tang, esp: .esp.phase, app: .app.name, link, log}'
#   409 {"error":"No VDAC4 Board", ...} without the board
curl -s "$BASE/emulator/$EMU/vdac4/esp/memory?address=0x6b7ac&size=64&app=1" | jq -r .hex
#   400 {"error":"bad_request","message":"size is 1..4096"}; 409: no board, no app, or the address is not mapped
curl -s "$BASE/emulator/$EMU/vdac2/metrics" | jq .    # the Tang chip's line budget, as on VDAC2
```

WebSocket: every route as a `call`, e.g. `{"op":"call","rid":1,"method":"GET","path":"/api/v1/emulator/<id>/vdac4/state?log=0"}`.
No topic: the state is polled.

## CLI / Lua / Python

```text
vdac4 state             # alias: vdac4 status; text with the last 10 log lines
vdac4 state log 30
vdac4 log 50            # the ESP log only
vdac4 esp-memory 0x6b7ac 64 app
```

```lua
local s = vdac4_state(20)              -- {error = "..."} without the board
print(s.tang.personality, s.esp.phase, s.app.name)
local m = vdac4_esp_memory(0x6b7ac, 64, true)
print(m.hex)
```

```python
s = emu.vdac4_state(log=20)            # RuntimeError without the board
print(s["tang"]["personality"], s["esp"]["phase"], s["app"]["name"])
data = emu.vdac4_esp_memory(0x6b7ac, 64, app=True)   # bytes
```

## Reading the state

| Field | Meaning |
|---|---|
| `tang.personality` | `ts` (the TS-Conf picture), `loading` (switching slots: the FT812 answers 0), `ft` (the FT812 core) |
| `tang.armed`, `tang.esp_host` | the ESP armed the Tang (after `EspBootMs`); the ESP drives the FT812 (`FT_CFG` bit 2: the Z80's FT reads return 0) |
| `esp.phase` | `off`, `booting`, `running`, `flushing`, `restart_wait` |
| `app` | `running`, `module`, `name`, `version`, the command range, `error` (the last runtime error), `sections` |
| `link` | bytes pending each way, `send_blocked`, `bad_checksums`, `resyncs`, `dropped_frames`: nonzero errors = a launcher / link problem |
| `packets` | the Z80's last 64 packets (`command`, `length`, `ms`) |
| `audio` | the ESP DAC: rates, `queued`, `underruns`, `dropped` |
| `threads` | the worker modes and host time per job / FT work (speed work) |
| `log` | the ESP's log lines (the app's `printf`), oldest first |

A healthy DOOM run (checked live): `personality: "ft"`, `armed: true`, `esp_host: true` (DOOM draws from the ESP, so
the Z80's own FT812 reads return 0), `esp.phase: "running"`, `app.running: true`, `app.name: "doom"`, version `1.1`,
link error counters 0, the log ending in `app DOOM doom 1.1 started, commands 70..76`. The CLI text of `vdac4 state`:

```text
VDAC4 board
  Tang:    personality ft, armed, ESP host, RAM_G 2 MB, FT_CFG 0x4
  ESP:     running, 18677 ms, boots 1 (nonce 105), last step 112, resources 0, clock restarts 0, ahead 0 us
  App:     running "doom" 1.1 (DOOM.ELF)
```

## Time travel

Recording works as on any machine ([analysis/ttd-recording.md](../analysis/ttd-recording.md)). The board adds:

- The ESP memory is stored as a keyframe every 25 recorded frames (50 while the Quake II core runs; and on a rescan
  or a journal over 256 KB), the
  traffic into the ESP as an input journal between them; a seek re-runs the app from the keyframe before the target.
  DOOM: about 19 KB per frame and 2.8 ms per frame recording (1.9 ms off), a seek 12-99 ms (median about 37 ms).
- A seek whose re-run does not reach the recorded state answers `not_bit_exact` with an issue `replay_diverged`
  (device `vdac4-esp`) in `check` (`/ttd/position`, `time_travel` seek). Exact otherwise.
- A seek whose FT812 picture shows lines drawn before the session (its first FT812 frames) answers `not_bit_exact`
  with an issue `picture_incomplete`.
- The picture of a position is the FT812 frame in flight drawn up to it over its previous frame; a machine stopped
  there (pause, `run_frames`, `run_tstates`) holds the same: compare a seek with `capture/screen?source=live`. The
  default capture (`presented`, the window's) keeps the last finished frame on a pause, and shows the composed
  picture after a seek.
- To check a seek from outside: hash `vdac4_esp_memory` reads at marks while recording, seek to each mark, compare.

## Pitfalls

- **The ESP game needs time to start:** the ESP boots (`EspBootMs`), the launcher uploads the ELF (DOOM.ELF: 366
  packets) and the game reads its data through the Z80 at about 36 KB/s (DOOM's WAD init): DOOM runs after 600-800
  frames. Poll `app.running` between `run_frames` steps, not a fixed frame count. The recordings and measurements in
  [ttd-design.md](../../docs/inprogress/2026-11-15-tsconf-vdac4/ttd-design.md) start after 6000 frames (the demo
  loop).
- **The ESP cache outlives the desktop app** (`~/.unreal-ng/vdac4/zm`): a second run skips the upload. Headless and
  test instances start with an empty cache each time.
- **The FT812 picture appears only in personality `ft` with `M_SEL` 1**; before that the screen shows TS-Conf.
  VDAC2 software that pokes the FT812 before the ESP armed the Tang (400 ms after power-on) sees MISO 0 and retries,
  as on the board.
- `vdac4_esp_memory` is not frame-exact (players[0] at `0x6b7ac` reads zeros until the demo or a game starts); for per-frame values use a haptic probe `cpu: esp` (the DOOM profile
  `data/software/tsconf/doom-vdac4.yaml` shows the addresses and how they were found).
- Keys: the launchers read the PS/2 keyboard, the Kempston mouse and joystick. Esc, Tab, Ctrl and Backspace exist
  only on PS/2: send them as `pc.escape` etc. with the keyboard route `ps2` or `both`
  ([input/keyboard.md](../input/keyboard.md)).
