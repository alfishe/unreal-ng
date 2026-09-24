# Recipe: Pentagon (128K / 512K / 1024K)

The Pentagon is the de-facto standard Russian clone — most demos and TR-DOS
software of the 1990s target its timing. It is also the machine whose config
carries the modern sound stack defaults (TSFM, Covox, GS).

One model id covers three machines: `ram_size` picks the decoder:

| `ram_size` | Decoder | Extras |
|:--|:--|:--|
| 128 (default) | Pentagon-128 | `#7FFD` paging only |
| 512 | Pentagon-512 | 5-bit RAM banking |
| 1024 | Pentagon-1024 | + port `#EFF7` feature register, 6-bit banking (64 pages) |

Ground truth: [portdecoder_pentagon1024.h](../../core/src/emulator/ports/models/portdecoder_pentagon1024.h)
(Born Dead #10 `#EFF7` bit map), config
[data/configs/pentagon128k/unreal.ini](../../data/configs/pentagon128k/unreal.ini).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `emulator_manage` creates the variant, `invoke_api` reads the port map.
> Use [WebAPI](#webapi) only inside host-side Python/bash pipelines or when
> MCP is unavailable (policy: [_common/transports.md](../_common/transports.md)).
> Shared create/identity patterns: [_common/machines.md](../_common/machines.md).

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"PENTAGON","ram_size":1024}
#   → model PENTAGON, ram_kb 1024 (config_folder still "pentagon512k" — by design)

invoke_api {"method":"GET","path":"/emulator/{id}/ports"}
#   → port map rows for this instance: #7FFD paging, #EFF7 features (1024K),
#     AY/TurboSound, Covox #FB, Beta128 — the machine's decoded device list

invoke_api {"method":"GET","path":"/emulator/{id}/state/paging"}
#   → latches (#7FFD...) + banks[4]: per 16K window {type: ROM|RAM, page,
#     read_write} and, for ROM, the identified signature/name/role

inspect_state {"aspects":["registers","rom"]}
```

### Pentagon-1024 specifics: port `#EFF7`

Write `#EFF7` to switch on the hardware extensions (decoder header, bits):
bit 0 `a4b` attribute-per-byte multicolor, bit 1 512x192 video mode,
bit 2 memory-above-128K latch (0 = present, 1 = compatibility 128K mode —
also turns bit 5 of `#7FFD` back into the paging lock), bit 4 GigaScreen
(hardware overlay of screens 0/1), bit 7 Gluk CMOS. When bit 2 = 0, the RAM
bank number combines `#7FFD` bits into 6 bits → 64 pages of 16K.

Exercise it from a snapshot of running code or poke the port through a small
loader; then verify the effect with
[capture_media screenshot](../media/agent-screenshot-view.md) (GigaScreen
flicker needs two frames) or `inspect_state {"aspects":["video"]}`.

## WebAPI

```bash
curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
     -d '{"model": "PENTAGON", "ram_size": 1024}' | jq '{id, model, ram_kb}'

# Decoded port families of this instance
curl -s "$BASE/emulator/$EMU_ID/ports" | jq '.entries[] | {port, device}'

# Live paging latch
curl -s "$BASE/emulator/$EMU_ID/state/paging" | jq .
```

### Sound defaults that come with the config

`pentagon128k`/`pentagon512k` `unreal.ini` ships `TurboSound=FM` (TSFM in
the TurboSound slot), `CovoxFB=1` (Pentagon-style Covox at `#FB`),
`SD=1` (SoundDrive), `GSType=BASS` on master (`Z80` LLE on the
`generalsound` branch). Changing any of these requires **editing the config
and creating a new instance** — no runtime switching. Details:
[turbosound.md](../peripherals/turbosound.md),
[covox-sounddrive.md](../peripherals/covox-sounddrive.md),
[generalsound.md](../peripherals/generalsound.md).

## Pitfalls

- **`config_folder` stays `pentagon512k` for the 1024K build** — the folder
  resolver maps everything ≥ 512K there. The Pentagon-1024 decoder is chosen
  by `ram_size` alone. Assert `ram_kb == 1024`, never the folder name.
- **`#EFF7` bit 2 = 1 silently caps the machine at 128K** — extended paging
  stops working and `#7FFD` bit 5 becomes the lock. If a 1024K test suddenly
  behaves like a 128K machine, read `#EFF7` (or `/state/paging`) before
  blaming the loader.
- **GigaScreen halves the visible framerate per screen** — screenshot one
  frame and you may catch an all-screen-0 or all-screen-1 image; compare two
  consecutive frames or a screen digest pair instead.
- **Pentagon timing ≠ Sinclair timing** — no `#1FFD`, different contention.
  When a demo behaves differently on `128k` vs `PENTAGON`, that is data, not
  necessarily an emulator bug.
