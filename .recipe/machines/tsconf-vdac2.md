# Recipe: TS-Conf with the VDAC2 card (FT812)

The VDAC2 card sits on the ZX-Evo's IDE connector. It has an FT812 graphics
controller (Bridgetek EVE2) on the Z-Controller SPI bus, next to the SD card. A
program selects it with `#77` bit 2, sends display lists and coprocessor commands
over `#57`, and switches the monitor to the FT812 picture with `V_CONFIG` bit 2.
The FT812's interrupt then replaces TS-Conf's line interrupt. Games for it: R-Type,
Heroes II, Zuma. The TS-Labs SDK programs are in
[testdata/machines/tsconf/vdac2-sdk/](../../testdata/machines/tsconf/vdac2-sdk/README.md).

Ground truth:
[vdac2card.h](../../core/src/emulator/platforms/tsconf/vdac2card.h)/[.cpp](../../core/src/emulator/platforms/tsconf/vdac2card.cpp),
[vdac2control.h](../../core/src/emulator/platforms/tsconf/vdac2control.h) (what every
surface calls), design in
[docs/inprogress/2026-10-01-tsconf-vdac2/](../../docs/inprogress/2026-10-01-tsconf-vdac2/vdac2-tdd.md)
(the capture format: `vdac2-test-corpus.md` §4).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use
> [WebAPI](#webapi) inside host-side pipelines (policy:
> [_common/transports.md](../_common/transports.md)). The TS-Conf basics are in
> [tsconf.md](tsconf.md).

## Setup

- **The machine:** `TSL-VDAC2` (alias `TSCONF-VDAC2`), a machine variant: TS-Conf in the
  firmware's VDAC2 build (STATUS VDAC_VER = 7, BLT2) with the card fitted and no IDE (the
  card takes the connector). unreal-qt: Machine menu -> **TS-Conf + VDAC2 (FT812)**. Every
  automation surface creates it by name and lists it with the models (`variant: true`);
  an instance reports `variant: "TSL-VDAC2"`.
- **The build:** the FT812 library (`eve-emu`) is vendored at `core/src/3rdparty/eve-emu`
  and built by default (`ENABLE_VDAC2=ON`). `-DEVE_EMU_DIR=` points at another `eve-emu`
  checkout; with `ENABLE_VDAC2=OFF` the variant is not offered and a VDAC2 configuration
  refuses to load ("this build has no VDAC2 support").
- **The same board from an ini** (any TS-Conf config, `configs/ts-conf/unreal.ini`):

  ```ini
  [MISC]
  TS_VDAC2=1            ; the VDAC2 build: the card fitted, no IDE

  [VDAC2]
  RomImage=rom/ft81x.rom    ; the FT812's ROM fonts (optional: without it ROM text is blank)
  LineBudgetMargin=10       ; line metrics: the soft budget, percent below the line period (0..50)
  CaptureFile=              ; optional: capture the bus from the machine's creation
  ```

- **The ROM image** (optional, for `CMD_TEXT` and the ROM fonts): extract it from a
  Bridgetek EVE emulator DLL with
  [tools/machines/tsconf/vdac2/extract-ft81x-rom.py](../../tools/machines/tsconf/vdac2/README.md)
  and put it next to the other ROMs as `rom/ft81x.rom`. It is never committed; the games
  run without it (R-Type, Heroes II and Zuma draw their own text).

Games read their data files from the SD card: insert the game's folder into slot
`sd.zc`, then open its `.spg`. Zuma expects to be started from Wild Commander (it
takes its path from WC's panel page): put WC (`boot.$C` and `WC/`) and the game's
folder into one host folder, insert it into `sd.zc` and reset. The BIOS boots WC
from the card ([tsconf.md](tsconf.md#wild-commander-the-ts-conf-shell-from-the-sd-card)),
and you open the game's `.spg` from WC.

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"TSL-VDAC2"}
invoke_api      {"method":"POST","path":"/api/v1/emulator/{id}/media/sd.zc/insert",
                 "body":{"path":"/path/to/R-Type VDAC2"}}
load_software   {"path":"/path/to/R-Type VDAC2/rtype_vdac2.spg"}
capture_media   {"action":"screenshot"}                      # the FT812 picture (1024x768 in the games) while V_CONFIG bit 2 is set; PNG, area full

# Bus capture: everything on the FT812's bus, to an .evr replay stream
capture_media   {"action":"vdac2_capture_start","filename":"scratch/rtype.evr"}
capture_media   {"action":"vdac2_capture_status"}
capture_media   {"action":"vdac2_capture_stop"}

# Line budget: what each FT812 line cost against the clocks it has (last finished frame)
analyze_performance {"action":"vdac2_line_budget"}
analyze_performance {"action":"vdac2_line_budget","lines":true,"in_flight":true}   # in_flight: pause first
analyze_performance {"action":"vdac2_line_budget_set","margin":10,"measure_always":true}
```

## WebAPI

```bash
EMU=$(curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' -d '{"model":"TSL-VDAC2"}' | jq -r .id)
curl -s -X POST "$BASE/emulator/$EMU/media/sd.zc/insert" -H 'Content-Type: application/json' \
     -d '{"path":"/path/to/R-Type VDAC2"}'
curl -s -X POST "$BASE/emulator/$EMU/snapshot/load" -H 'Content-Type: application/json' \
     -d '{"path":"/path/to/R-Type VDAC2/rtype_vdac2.spg"}'

# Bus capture (409 = no VDAC2 card: not TS-Conf, not the VDAC2 build, or no library)
curl -s -X POST "$BASE/emulator/$EMU/vdac2/capture/start" -H 'Content-Type: application/json' \
     -d '{"path":"/tmp/rtype.evr"}' | jq .
curl -s "$BASE/emulator/$EMU/vdac2/capture/status" | jq .
#   → {"capturing":true,"path":"/tmp/rtype.evr","bytes":...,"selects":...,"exchanges":...,
#      "frames":...,"start_clock":...,"last_clock":...,"format":"evr"}
curl -s -X POST "$BASE/emulator/$EMU/vdac2/capture/stop" | jq .

# Line budget metrics of the last finished FT812 frame
curl -s "$BASE/emulator/$EMU/vdac2/metrics" | jq .
#   → {"valid":true,"frame":1234,"lines":768,"hard_budget":1344,"soft_budget":1209,
#      "worst_line":402,"worst_clocks":1247,"total_clocks":...,"lines_over_soft":12,
#      "lines_over_hard":0,"margin":10,"measure_always":false}
curl -s "$BASE/emulator/$EMU/vdac2/metrics?lines=1&in_flight=1" | jq '.line_clocks | max, .in_flight'
curl -s -X PUT "$BASE/emulator/$EMU/vdac2/metrics" -H 'Content-Type: application/json' \
     -d '{"margin":10,"measure_always":true}' | jq .
```

## CLI / Lua / Python

```text
vdac2 capture start /tmp/rtype.evr
vdac2 capture status
vdac2 capture stop
vdac2 metrics                 # worst line, lines over soft / hard
vdac2 metrics lines inflight  # every line's cost; the frame in flight (paused machine)
vdac2 metrics margin 10
vdac2 metrics always on
```

```lua
local ok, err = vdac2_capture_start("/tmp/rtype.evr")
print(vdac2_capture_status().frames)
vdac2_capture_stop()
local m = vdac2_metrics()            -- vdac2_metrics(true, true): line_clocks, in_flight
print(m.worst_line, m.worst_clocks, m.lines_over_hard)
vdac2_metrics_set(10, true)          -- margin, measure_always (each may be nil)
```

```python
emu.vdac2_capture_start("/tmp/rtype.evr")   # RuntimeError with the reason on failure
print(emu.vdac2_capture_status()["frames"])
emu.vdac2_capture_stop()
m = emu.vdac2_metrics(lines=True)    # in_flight=True adds the frame in flight (paused machine)
print(m["worst_clocks"], m["lines_over_hard"], max(m["line_clocks"]))
emu.vdac2_metrics_set(margin=10, measure_always=True)
```

## What a capture holds

- Every chip select change and every byte, with the FT812's answer, stamped with
  FT812 system clocks; a record at every FT812 frame end with the frame count and a
  hash of the picture. While capturing, the chip draws every frame (also while the
  monitor shows the Evo), so every frame record has a hash.
- Started on a running chip, the stream begins with the chip's whole state (its
  state blob and memory regions), so it replays from that point. With
  `[VDAC2] CaptureFile` it starts at the chip's power-on.
- About 0.5 MB per second of game. Replaying it drives the `eve-emu` library alone
  (byte answers and frame hashes must match): the reference workload for the
  library's tests and performance work.

## Line budget metrics

The FT812 draws every screen line from the display list within the line period:
1344 clocks in the 1024 x 768 mode. Each display list command costs a clock on
**every** line, and filled pixels cost by format and filter; a line that needs more
than it has comes out broken on a real card. The model, the worked examples and what
an overflow looks like: [line-budget-model.md](../../docs/inprogress/2026-10-01-tsconf-vdac2/line-budget-model.md).

- **The block is the last finished FT812 frame**, replaced at each FT812 frame end. It
  is part of the chip state: after a TTD seek (`time_travel` seek) it reads as at that
  moment, without drawing the frame again.
- **`valid: false`** = the frame was not drawn: the monitor showed the Evo (`V_CONFIG`
  bit 2 clear) or turbo skipped the frame. `measure_always` draws and measures every
  frame (costs host time, changes nothing the program sees); a bus capture or a TTD
  replay draws every frame anyway.
- **`lines_over_hard` > 0** = lines broken on a real card. **`lines_over_soft`** =
  lines inside the last `margin` percent: risky (developers keep about 10 % free).
- **`in_flight`** (the frame being scanned now: the lines passed so far, each with its
  cost, -1 for a line passed without drawing) is read only on a paused machine; on a
  running one it answers `known: false`.
- The cost model's absolute numbers are TO VERIFY on a card (line overhead, bilinear
  and primitive fill rates): right for comparing frames and finding heavy lines.

## Pitfalls

- **One capture at a time:** a new start finishes the running capture.
- **The file is complete only after stop** (or when the machine goes away): the
  end record is written then.
- The monitor switches per machine frame, as the last line latched `V_CONFIG` bit 2.
- The Qt window, screenshots and recordings take the FT812 picture's size
  (`HSIZE × VSIZE`, 1024×768 in the games) while it is shown. A screenshot
  needs no special parameter for it: the default `area=full` returns the
  whole FT812 picture, and `area=screen` is the same image (the picture has
  no border). Before the screenshot rewrite it returned a 256x192 piece cut
  from the middle.
