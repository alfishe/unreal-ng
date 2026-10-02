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
  CaptureFile=              ; optional: capture the bus from the machine's creation
  ```

- **The ROM image** (optional, for `CMD_TEXT` and the ROM fonts): extract it from a
  Bridgetek EVE emulator DLL with
  [tools/machines/tsconf/vdac2/extract-ft81x-rom.py](../../tools/machines/tsconf/vdac2/README.md)
  and put it next to the other ROMs as `rom/ft81x.rom`. It is never committed; the games
  run without it (R-Type, Heroes II and Zuma draw their own text).

Games read their data files from the SD card: insert the game's folder into slot
`sd.zc`, then open its `.spg`. Zuma expects to be started from Wild Commander (it
takes its path from WC's panel page): put it on a WC SD image instead.

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"TSL-VDAC2"}
invoke_api      {"method":"POST","path":"/api/v1/emulator/{id}/media/sd.zc/insert",
                 "body":{"path":"/path/to/R-Type VDAC2"}}
load_software   {"path":"/path/to/R-Type VDAC2/rtype_vdac2.spg"}
capture_media   {"action":"screenshot","format":"png"}      # the FT812 picture while V_CONFIG bit 2 is set

# Bus capture: everything on the FT812's bus, to an .evr replay stream
capture_media   {"action":"vdac2_capture_start","filename":"scratch/rtype.evr"}
capture_media   {"action":"vdac2_capture_status"}
capture_media   {"action":"vdac2_capture_stop"}
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
```

## CLI / Lua / Python

```text
vdac2 capture start /tmp/rtype.evr
vdac2 capture status
vdac2 capture stop
```

```lua
local ok, err = vdac2_capture_start("/tmp/rtype.evr")
print(vdac2_capture_status().frames)
vdac2_capture_stop()
```

```python
emu.vdac2_capture_start("/tmp/rtype.evr")   # RuntimeError with the reason on failure
print(emu.vdac2_capture_status()["frames"])
emu.vdac2_capture_stop()
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

## Pitfalls

- **One capture at a time:** a new start finishes the running capture.
- **The file is complete only after stop** (or when the machine goes away): the
  end record is written then.
- The monitor switches per machine frame, as the last line latched `V_CONFIG` bit 2.
- The Qt window, screenshots and recordings take the FT812 picture's size
  (`HSIZE × VSIZE`, 1024×768 in the games) while it is shown.
