# Recipe: NeoGS Card (SD Slot, Flash, MP3, Stereo, Personality Switch)

Scope: the NeoGS sound card, the modern GS clone with an FPGA around its own
Z80: 8 DAC channels, 2 or 4 MB RAM, a 512 KB flash, an SD card slot and a
VS1001/VS1011 MP3 decoder chip. This recipe covers fitting it, reading its
state, putting an SD image in, saving the flash, choosing the stereo layout
and switching between the NeoGS and the classic card. The classic General
Sound card (mailbox protocol, `send_command`/`send_data`, module playback)
is in [generalsound.md](generalsound.md); the mixer and capture of its
output are in [audio-mixer-and-capture.md](audio-mixer-and-capture.md).

Ground truth: config parse in
[slotmanager.cpp](../../core/src/emulator/slots/slotmanager.cpp) (`[SLOTS]`, the legacy `[SOUND] GSType` translated), card in
[soundchip_neogs.h](../../core/src/emulator/sound/chips/neogs/soundchip_neogs.h),
SD/flash request rules in
[neogsmedia.cpp](../../core/src/emulator/sound/chips/neogs/neogsmedia.cpp),
MP3 decoder [vs10xx.h](../../core/src/emulator/sound/chips/neogs/vs10xx.h),
control handler `postControlAudioGS` in
[state_audio_api.cpp](../../core/automation/webapi/src/api/state_audio_api.cpp),
MCP actions in [mcp-tools.cpp](../../core/automation/mcp/src/mcp-tools.cpp)
(`emulator_manage`). Story of the card:
[neogs-bringup.md](../../docs/inprogress/2026-09-29-neogs-bringup/neogs-bringup.md).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `emulator_manage` carries the `gs_*` actions, `inspect_state` reads the
> card. Use [WebAPI](#webapi) only inside host-side Python/bash pipelines or
> when MCP is unavailable (policy: [_common/transports.md](../_common/transports.md)).

## Is the card fitted?

The card is a slot card: a `neogs` (`gs`, `gs-lw`) card in `[SLOTS]` at instance
creation (`zxbus.1 = neogs`, `zxbus.1.ram = 4m`; the legacy `[SOUND] GSType=NGS`
still works and is translated; the table is in [generalsound.md](generalsound.md)),
or plugged later with `slots plug zxbus.next neogs` ([slots recipe](../machines/slots.md)).
`inspect_state {"aspects":["slots"]}` shows where it sits. The shipped clone configs
fit the NeoGS, the Sprinter behind its ISA ZX-bus adapter (`isa.1`,
[docs/features/sprinter-slots.md](../../docs/features/sprinter-slots.md)); the 48K / 128K / +2 / +2A / +3
and Profi (v5, v3) configs fit none since 2026-10-04 (owner decision: none of their
buses takes a ZX-bus card without an adapter). The card's own settings sit in the `[NGS]` block of
the same `unreal.ini`:

| Key | Values (shipped first) | Meaning |
|:--|:--|:--|
| `Flash` | `rom/neogs/full_ngs.rom` | 512 KB flash: loader, main ROM v1.11, FPGA configuration |
| `RamSize` | `2048` \| `4096` | card RAM, KB |
| `Boot` | `loader` \| `direct` | `loader` boots `NEOGS.ROM` from the SD card; `direct` starts the main ROM at once |
| `SDCardImage` | empty | SD image or host folder (slot `sd.ngs`); `[MEDIA] sd.ngs` wins |
| `SDType` / `SDWrite` | `auto` / `session` | `sdsc`/`sdhc`; `session` keeps guest writes until exit, `persist` writes the image, `off` refuses |
| `MP3Support` | `software` \| `stub` \| `none` | `software` decodes and plays, `stub` takes data silently |
| `Mp3Chip` / `Mp3Gain` | `vs1001` \| `vs1011` / `1.0` | decoder model; output gain 0.0-8.0 |
| `FlashWrite` | `session` \| `persist` \| `off` | what happens to flash programming |
| `StereoMode` | `separated` \| `gs` \| `mono` | start-up DAC layout (see below) |

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"PENTAGON"}     # zxbus.1 = neogs from the shipped config

inspect_state   {"aspects":["audio_gs"]}                   # the card report (see fields below)

# SD card: a raw image or a host folder, then eject
emulator_manage {"action":"gs_sd_insert","path":"scratch/neogs-sd.img"}
emulator_manage {"action":"gs_sd_eject"}
media           {"action":"list"}                          # the same card as slot sd.ngs
media           {"action":"insert","slot":"sd.ngs","path":"scratch/neogs-sd.img"}

# Flash: write what the guest programmed (NedoPC flasher etc.) back
emulator_manage {"action":"gs_flash_save"}

# Listening layout of the 8 DAC channels
emulator_manage {"action":"gs_stereo_mode","mode":"gs"}    # separated | gs | mono

# Replace the card in the GS slot: a slot change, the machine restarts (new emulator id in the reply)
emulator_manage {"action":"gs_switch_personality","personality":"z80"}   # z80|lle, lw|lightweight, ngs|neogs
emulator_manage {"action":"gs_switch_personality","personality":"ngs"}
emulator_manage {"action":"slots_plug","slot":"zxbus.1","card":"neogs","options":"ram=4m","replace_if_incompatible":true}

# Diagnostics: the last COM30..D2 module upload as a file (classic-card style upload)
emulator_manage {"action":"gs_dump_module","path":"scratch/module.mod"}

# Functional proof that the card (and its MP3 decoder) sounds: see audio-mixer-and-capture.md
capture_media   {"action":"audio_capture","seconds":2,"source":"gs"}
capture_media   {"action":"audio_capture","seconds":2,"source":"gs_mp3"}
```

Notes on the MCP names: the `gs_` prefix maps one to one onto the WebAPI
`action` names (`gs_sd_insert` is `sd_insert`). `gs_sd_insert` needs `path`,
`gs_stereo_mode` needs `mode`, `gs_switch_personality` needs `personality`;
a missing one is a tool error before any HTTP call. The classic mailbox
actions (`gs_reset`, `gs_reset_card`, `gs_nmi`, `gs_send_command`,
`gs_send_data`, `gs_read_status`, `gs_read_data`) work on the NeoGS too; they
are described in [generalsound.md](generalsound.md).

## WebAPI

```bash
BASE=http://localhost:8090/api/v1

# State (the report behind inspect_state audio_gs); ?ram=1 adds the card CPU's #4000-#7FFF window
curl -s "$BASE/emulator/$EMU_ID/state/audio/gs" | jq '{device, implementation, ngs}'

# SD card in and out
curl -s -X POST "$BASE/emulator/$EMU_ID/control/audio/gs" -H 'Content-Type: application/json' \
     -d '{"action":"sd_insert","path":"scratch/neogs-sd.img"}' | jq .
curl -s -X POST "$BASE/emulator/$EMU_ID/control/audio/gs" -H 'Content-Type: application/json' \
     -d '{"action":"sd_eject"}' | jq .

# Flash, stereo layout, personality
curl -s -X POST "$BASE/emulator/$EMU_ID/control/audio/gs" -H 'Content-Type: application/json' \
     -d '{"action":"flash_save"}' | jq .
curl -s -X POST "$BASE/emulator/$EMU_ID/control/audio/gs" -H 'Content-Type: application/json' \
     -d '{"action":"stereo_mode","mode":"mono"}' | jq '{mode, neogs_fitted}'
curl -s -X POST "$BASE/emulator/$EMU_ID/control/audio/gs" -H 'Content-Type: application/json' \
     -d '{"action":"switch_personality","personality":"neogs"}' | jq '{status, personality, previous, restart}'
```

## The state report (assert on these)

`GET /state/audio/gs` reports the card; the NeoGS fields sit under `ngs`
(absent for the classic card), and the common ones at the top level:

- `implementation` / `device`: which card answers (`ngs` for NeoGS); `ram_kb`,
  `rom_loaded`, `firmware`.
- `ngs.stereo_mode`, `ngs.clock_hz`, `ngs.flash`, `ngs.flash_modified`
  (true once the guest programmed the flash and `flash_save` has not run),
  `ngs.gscfg0_flags` (`ram_mode`/`rom_mode`, `8_channels`, `pan4ch`, ...),
  `ngs.windows[]` (the four memory windows: `page`, `flash`), `ngs.ready`,
  `ngs.led_on`, `ngs.int_enable` / `ngs.int_request`.
- `ngs.sd`: `present`; with a card also `path`, `sdhc`, `size_bytes`,
  `blocks_read`, `blocks_written`. Rising `blocks_read` proves the guest is
  reading the card.
- `ngs.mp3`: `fitted`; with a decoder `chip`, `dreq`, `rate`, `channels`,
  `frames`, `decode_time_s`, `input_fill`. `frames` growing means the VS10xx
  path is decoding.
- `ngs.dma`: `select`, per module `running` / `address`, and `zx` (the ZX-DMA
  engine: `mode`, `bytes_read`, `bytes_written`, `bytes_dropped`).
- Per-channel DAC values are in `channels[]` (`sample`, `volume`).

## Worked example: does Neo Player play an MP3 from the card?

1. Create a Pentagon (the card is fitted by the shipped config).
2. `gs_sd_insert` with a FAT16/FAT32 image that holds the player and an
   `.mp3`.
3. Run the player (autostart it as the image's README says), then run frames.
4. `inspect_state audio_gs`: `ngs.sd.blocks_read` is rising,
   `ngs.mp3.frames` is rising, `ngs.mp3.dreq` toggles.
5. `capture_media audio_capture` with `source: "gs_mp3"`: `dominant_hz` and
   RMS above zero on both channels.

## Pitfalls

- **Only the NeoGS has the SD slot and the flash.** `sd_insert` / `sd_eject`
  / `flash_save` on the classic or lightweight card answer `409` ("only the
  NeoGS card (a neogs slot card) has an SD slot and a flash chip"). `stereo_mode`
  is accepted on any card but only reaches a NeoGS (`neogs_fitted` says
  whether it did; a NeoGS fitted later by `switch_personality` starts with
  the stored choice).
- **No card at all is `404`.** The handler answers "General Sound card not
  fitted" when no GS card is in the slots (`slots plug zxbus.next neogs` fits one).
- **Media actions are queued while the machine runs.** `sd_insert`,
  `sd_eject`, `flash_save` reply `status: "queued"` and happen at the next
  instruction boundary (while paused: when execution continues); `done`
  means it already ran. Poll `ngs.sd.present` to confirm.
- **`409` while TTD records or replays.** SD insert and eject (and the
  personality switch) are refused during a TTD recording: the machine
  configuration is fixed then. `422` means the path is empty or no file or
  folder is there (`no SD image or folder at that path`).
- **`path` is read by the emulator process**, like every media path. For
  `dump_module` it is the opposite: a relative path with no `..`, written
  relative to the server's working directory (default `gs-module-dump.mod`);
  an absolute path is a `400`. Use `scratch/...`.
- **`dump_module` answers `404`** until a COM30..D2 module upload finished;
  it is a classic-card diagnostic, a NeoGS state that never saw one has
  nothing to dump (unconfirmed for NeoGS firmware that streams modules).
- **`switch_personality` restarts the machine.** It is a slot change (owner
  decision Q10): the reply carries the plan and `restart.emulatorId`, the new
  machine; the machine state (and the card's mailbox) starts fresh, the SD card
  `sd.ngs` follows when the new card has the slot (a NeoGS -> classic GS switch
  with unsaved SD writes needs `mediaDisposition`). Only the `gs_lightweight`
  feature still swaps the card in place.
- **`stereo_mode` values:** `separated` (as on the board), `gs` (50%
  cross-feed like the classic card), `mono`. Applied at the next frame.
  Other words are a `400`.
- **Guest writes to the SD card are `session` by default** — gone at exit.
  Keep a master image pristine and work on a copy under `scratch/`; export
  with `media {"action":"export","slot":"sd.ngs","path":"scratch/after.img"}`
  (see [use-media-slots.md](../media/use-media-slots.md)).
- **Unconfirmed:** whether capture `source: "gs"` records the NeoGS DAC mix
  and `gs_mp3` only the decoder path (the source keys are listed by the
  mixer for the fitted devices; read `GET /audio/mixer` first and use what
  it lists).
