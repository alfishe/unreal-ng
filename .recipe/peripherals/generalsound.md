# Recipe: General Sound (GS) Card

The General Sound expansion: a sample/DAC card with its own Z80
coprocessor. The card is a slot card: `[SLOTS]` names it in the model's
`unreal.ini` (`zxbus.1 = gs`, read at instance creation), automation plugs it
with `slots plug zxbus.next gs` ([slots recipe](../machines/slots.md)), and the
personality is swapped by a slot replace that restarts the machine
([personality switch](#mcp-preferred)):

| Card (`[SLOTS]`) | Legacy `[SOUND] GSType=` | What |
|:--|:--|:--|
| `gs` (`ram`, `rom` options) | `Z80` | full LLE classic card: Z80 coprocessor (unreal-z80) @ 12 MHz, ROM + RAM, DACs, interrupt |
| `gs-lw` | `LW` (alias `LIGHT`; `BASS` deprecated) | lightweight in-tree mod player driven by host commands (no coprocessor) |
| `neogs` (`ram`) | `NGS` | NeoGS FPGA card (`SoundChip_NeoGS`): SD slot, MP3 decoder, DMA |
| no card | `NONE` | card absent |

The legacy `GSType` key still works in an INI without `[SLOTS]`: it is
translated into a slot card at creation (a deprecation line in the log).
The shipped clone configs under `data/configs/` fit the NeoGS (`neogs` in `[SLOTS]`); the 48K / 128K / +2 / +2A /
+3 and Profi (v5, v3) configs fit no GS since 2026-10-04 (owner decision), a user adds one in `[SLOTS]`; the Sprinter
fits its NeoGS behind the ISA ZX-bus adapter (`isa.1`, [docs/features/sprinter-slots.md](../../docs/features/sprinter-slots.md)). Classic-card
firmware `[ROM] GS=` defaults to `rom/gs105a.rom` (`gs104.rom` also ships);
`bootGS.rom` and `rom/neogs/` hold the NeoGS flash image. Other keys:
`GSVol` (0-8192 ini scale, shipped 8000), `GSReset=1` makes a ZX reset
reinitialize the card too.

Ground truth: `core/src/emulator/sound/chips/gs/soundchip_gs.h` (LLE),
`soundchip_gslw.h` (lightweight), `generalsoundcard.h` (common card
interface), `gsmailbox.h` (host mailbox), NeoGS in
`core/src/emulator/sound/chips/neogs/soundchip_neogs.h`; design doc
[docs/inprogress/2026-09-19-general-sound/gs-tdd.md](../../docs/inprogress/2026-09-19-general-sound/gs-tdd.md).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `inspect_state` reads the card, `capture_media audio_capture` proves it
> sounds. Use [WebAPI](#webapi) only inside host-side Python/bash pipelines
> or when MCP is unavailable (policy: [_common/transports.md](../_common/transports.md)).

## The card in one paragraph

Host-side ports (low byte decoded; model decoders cover aliases): `#B3`
GSDAT data, `#BB` GSCOM command/status, `#33` GSCTR control — bit 7 reset,
bit 6 NMI. On-card: Z80 @ 12 MHz, 32 KB ROM (2 x 16 KB pages), 128 KB RAM
stock (256/512 KB were expansions; the classic card is pinned to 128 KB),
4 x 8-bit DAC channels with 6-bit volume, 37.5 kHz periodic interrupt
(every 320 GS cycles). GS-side ports `0x00-0x0B` carry MPAG banking, the
host mailbox and volume latches. Audio mixes channels 1,2 -> L and 3,4 -> R
with 50% cross-feed. The coprocessor lazy-syncs to the ZX clock on every
host port access and at frame boundaries; its 12 MHz clock is fixed — GS
audio does not speed up with host turbo.

**Sprinter Sp2000:** no Spectrum port path - the card sits on the ZX-bus adapter in ISA slot 1 (default) and a
program reaches it through memory window 3 (page `#D4`): `#C0B3` data, `#C0BB` command / status, `#C033` control.
ISA RESET DRV resets it; the NeoGS ZX-DMA cannot reach the Sprinter's memory. The card's report and controls here
work unchanged; what is plugged where: [sprinter-isa.md](../machines/sprinter-isa.md#the-general-sound--neogs-behind-the-zx-bus-adapter-verified-2026-10-04).

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"PENTAGON"}

inspect_state {"aspects":["audio_gs"]}
#   mailbox flags, MPAG page, per-channel DAC sample/volume, coprocessor
#   core; a "neogs" object (windows, clock, SD, MP3, DMA) on NeoGS;
#   reports unavailable when no card is fitted. The card CPU's #4000-#7FFF
#   window: WebAPI GET .../state/audio/gs?ram=1

# Prove the card makes sound (any kind), master mix or the card alone:
capture_media {"action":"audio_capture","seconds":2}
capture_media {"action":"audio_capture","seconds":2,"source":"gs"}      # "gs_mp3": NeoGS MP3 decoder
#   -> analysis: sample_rate, duration_seconds, left/right {peak, rms},
#     dominant_hz (zero-crossing estimate), zero_crossing_rate;
#     "wav":true also exports the WAV (wav_path)
capture_media {"action":"audio_status"}   # {complete: ...} while running
```

Drive the card with `emulator_manage` (all `gs_*` actions go to
`POST /control/audio/gs`; 404 when no card is fitted):

| Action | Fields | Effect |
|:--|:--|:--|
| `gs_reset` | | full power-on reset (mailbox, volumes **and** timing) |
| `gs_reset_card` | | `#33` bit-7 pulse (CPU/banking/timing only; the mailbox survives) |
| `gs_nmi` | | `#33` bit-6 pulse |
| `gs_send_command` / `gs_send_data` | `value` 0-255 (required) | `OUT #BB` / `OUT #B3` semantics |
| `gs_read_status` / `gs_read_data` | | side-effect-free peek: `IN #BB` value (status or `#7E`) / the card-to-ZX byte (bit 7 not cleared) |
| `gs_send` | `value` the command; `pre`, `params` (array), `replies`, `wait_ms` (default 1000) | the debugger's send: each byte when the card may take it, the replies read, named by the firmware profile (`GS send 3: #23 Get number of RAM pages -> #03 (pages)`); a wrong parameter count is refused |
| `gs_conversation` | `send_id` (default the last) | a send's state: queued (a paused machine), running, done, failed with the byte the card did not take |
| `gs_switch_personality` | `personality`: `z80`/`lle`, `lw`/`lightweight`, `ngs`/`neogs`; optional `replace_if_incompatible`, `dry_run` | replace the card in the GS slot: a slot change applied by a machine restart (owner decision Q10) - a new emulator id (the reply's `restart.emulatorId`), the machine state is lost; refused while TTD records |
| `gs_dump_module` | `path` (optional, relative, no `..`; default `gs-module-dump.mod`) | write the last completed COM30..D2 module upload; 404 if none |
| `gs_sd_insert` / `gs_sd_eject` / `gs_flash_save` | `path` (insert: raw image) | NeoGS only; insert/eject while TTD records: an event of the recording at the next frame boundary |
| `gs_stereo_mode` | `mode`: `separated` (as on the board), `gs` (50% cross-feed), `mono` | NeoGS DAC mix, applied next frame |

The writes, resets and NMI are live input: applied at the next instruction
boundary and journaled for TTD (409 while a TTD replay owns input). Pick
`gs_reset` vs `gs_reset_card` deliberately when re-running a firmware boot
test.

What the host program's conversation is at: `inspect_state {"aspects":["audio_gs"]}` shows `protocol:` (the
command, the parameters and replies due, an upload's bytes) while the command log records
(`device_history {"action":"config","device":"zxbus.1.commands","mode":"ring"}`; off: "no protocol
state").

The card's hardware as boards: `inspect_state {"aspects":["boards"]}` lists them (`gs` on the classic card;
`neogs`, `neogs_dma`, `neogs_sd`, `neogs_mp3`, `neogs_flash` on NeoGS), `{"aspects":["board.neogs"]}` reads one
control by control (GSCFG0 and SSTAT bits named, pages, clock, channels, interrupts, mailbox). The SD card through its
board: `emulator_manage {"action":"board_action","board":"neogs_sd","operation":"insert","path":"<image or folder>"}`
(`operation` `eject`; board `neogs_flash`, operation `save`); `media {"action":"list"}` shows it in `sd.ngs`. WebAPI
`GET /debug/boards/{id}?values=1` gives `{values: {control: value}}` for polling.

What the card did, as counters: `inspect_state {"aspects":["audio_gs_stats"]}` (or `audio_gs_stats.mailbox`,
`.dma`, `.sd`, `.errors`, `.rates`...): both mailbox sides (`host_commands` vs `card_commands_read` /
`card_command_acks`, `command_overwrites` = a command written over an unread one), interrupts, on NeoGS every DMA
transaction, SD commands by index and errors, MP3 drops and resyncs, flash programs; `errors.total` is the one number
to watch. For "how many during this action": `emulator_manage {"action":"gs_reset_counters"}` returns a baseline,
then read with `"gs_stats_since": <baseline>`. Rates (`commands`, `zx_dma_bytes`, `cpu_steps` per second) need the
machine running; add `"gs_stats_fresh": true` for a block from after the call.

The firmware's data as named structures: `inspect_state {"aspects":["audio_gs_firmware"]}` (or
`audio_gs_firmware.<instance or variable>`) reads the instances and variables the card's profile declares, from their
physical page even when it is not mapped; `"gs_firmware_profile": "<id>"` reads through another profile,
`audio_gs_firmware_profiles` lists them. A new firmware gets a JSON profile in `data/symbols/gs/profiles/` (schema:
gs-debugger design §8A.9) with its `symbolFile` (relative to `data/symbols/gs`); no code changes.
`audio_gs_firmware.dispatch` shows each command's handler against the ROM (patched entries, e.g. the NedoOS SD driver's
`#1E`), and `unknown_commands` the commands the host sent that no profile names. A profile from the firmware's
sources: `tools/gsfirmware/profile_from_source.py --source DIR --map FILE --id ID`.

The lightweight player (no CPU) is read through `audio_gs` itself: `interpreter` (`state` idle / params / command /
upload / reply / covox, `text` such as "Upload, 2,000 of 3,196 bytes", the queues) and `player` (position, row, tick,
speed, BPM, the four channels with sample, volume, period, note, rate; the module (P) / (S) and its samples);
`audio_gs_firmware` answers the v1.05a variables its interpreter mirrors (`source: mirror`). The ZX-MultiSound's GS
(debugger CPU `ms`): `audio_multisound`'s `gs` has the `audio_gs` fields; its firmware objects and statistics are
`audio_gs_firmware` / `audio_gs_stats` with `"gs_cpu": "ms"`, its command log `device_history` `<slot>.gs.commands`.

NeoGS ZX-DMA bytes (what the Spectrum read from / wrote to card RAM through the window, and where it waited): start the
port trace (`invoke_api POST /api/v1/emulator/{id}/control/audio/gs/porttrace {"action":"start"}`), run, then
`inspect_state {"aspects":["audio_gs_zxdma"]}`: a line a byte with seq, host frame / T, card cycle, direction, card
address and page, value, wait T, host PC, `DROPPED` for a byte lost to mixed directions. `"gs_zxdma_last": N` (0: all
kept), `"gs_zxdma_since": <last_seq>` for only the new rows. WebAPI `GET .../state/audio/gs/zxdma?last=&since=`, CLI
`gs zxdma`, Lua / Python `gs_zxdma(last, since)`. The bytes have a ring of their own (65,536): on a long transfer read sooner or stop the trace after it.

Card CPU budget (is the ISR too long, which command or routine eats the card): `emulator_manage {"action":
"gs_profile_start"}` (`"gs_cpu": "ms"` for the ZX-MultiSound's GS), run some frames, then `inspect_state
{"aspects":["audio_gs_profile"]}`: the ISR's min / avg / max cycles against the period (320 on the classic GS) with
headroom, overruns and latency over the last frame and since the start; each host command's cost (count / avg / max
cycles from the card's read to its next status poll, interrupts apart); the top routines by inclusive cycles with
firmware labels (interrupts never inside a routine's time; the ISR its own root); per-frame ISR / idle / halted %.
`"gs_profile_routines": N` for more rows. Where the time goes inside a frame: also start the port trace (above) and
read `audio_gs_events` (`gs_events_first` / `gs_events_count` pick period rows): per interrupt period the ISR bar
(accept / end) and the host #B3 / #BB accesses, DAC fetches by channel, page and volume writes, in card cycles.
`gs_profile_stop` when done (on, the card runs its slower record loop). WebAPI `GET .../state/audio/gs/profile`,
`.../state/audio/gs/events`, `POST .../control/audio/gs {"action":"profile_start"}`; CLI `gs profile start`,
`gs profile`, `gs events`; Lua / Python `gs_profile`, `gs_profile_control`, `gs_events`.

Card CPU coverage and heat map (classic GS, NeoGS, the MultiSound's GS with `"cpu":"ms"`; not the lightweight GS):
`gs_analysis {"action":"on"}` (off by default and free while off; binds at the card's next run), run the machine, then
`gs_analysis {"action":"coverage"}` - per physical page of ROM / flash and RAM the instruction starts, operands, reads,
writes, jump and CALL targets, DAC fetches, with ranges; the firmware's command handlers that ran; the reads of RAM
never written since the card reset with their PC (`"space":"ram","page":N` for one page and its CALL entries).
`gs_analysis {"action":"heatmap","recent":"dac","frames":50}`: what the DAC played in the last 50 frames as RAM ranges,
and per channel (fetch, read, write, dac, sd_dma, zx_dma) how much of RAM is hot. `reset` clears, `off` keeps the data,
`release` frees it. WebAPI `GET .../state/audio/gs/coverage|heatmap`, `POST .../control/audio/gs/analysis`, CLI
`gs analysis|coverage|heatmap`, Lua / Python `gs_analysis`, `gs_coverage`, `gs_heatmap`. Never-written reads before the
first card reset after `on` are false alarms (the stamps start clear): `gs reset_card` first for a clean list.

Protocol triage: `analyze_performance {"action":"gs_porttrace","frames":10,"limit":32}`
starts a GS trace, runs the frames, stops it and returns the always-on
activity counters (CPU steps, interrupts, DAC pushes) plus the last `limit`
events (sides: host / gs / dac / interrupt / zxdma); no feature flag needed.
Per-device level: `inspect_state {"aspects":["audio_mixer"]}`, WebAPI
`GET /audio/mixer`.

## WebAPI

```bash
# Card state (add ?ram=1 for the card CPU's #4000-#7FFF window)
curl -s "$BASE/emulator/$EMU_ID/state/audio/gs" | jq .

# Control: reset / reset_card / nmi / send_command / send_data / read_* ...
curl -s -X POST "$BASE/emulator/$EMU_ID/control/audio/gs" \
     -H 'Content-Type: application/json' -d '{"action":"reset_card"}' | jq .
curl -s -X POST "$BASE/emulator/$EMU_ID/control/audio/gs" \
     -H 'Content-Type: application/json' -d '{"action":"send_command","value":243}' | jq .
#   -> {"status":"success","action":"send_command","note":"applied at the next instruction boundary"}
# The personality: a slot replace, the machine restarts with a new id
EMU_ID=$(curl -s -X POST "$BASE/emulator/$EMU_ID/control/audio/gs" \
     -H 'Content-Type: application/json' -d '{"action":"switch_personality","personality":"lw"}' \
     | jq -r '.restart.emulatorId')
#   reply: {"ok":true,"status":"applied","personality":"lw","previous":"lle","plan":{...},"restart":{"emulatorId":...}}
curl -s -X POST "$BASE/emulator/$EMU_ID/control/audio/gs" \
     -H 'Content-Type: application/json' -d '{"action":"stereo_mode","mode":"gs"}' | jq .

# Port trace: counters + last N events; control start|stop|pause|resume|clear
curl -s "$BASE/emulator/$EMU_ID/state/audio/gs/porttrace?events=32" | jq .
curl -s -X POST "$BASE/emulator/$EMU_ID/control/audio/gs/porttrace" \
     -H 'Content-Type: application/json' -d '{"action":"start"}' | jq .

# One-shot audio proof with offline analysis ("source":"gs" for the card alone)
curl -s -X POST "$BASE/emulator/$EMU_ID/audio/capture" \
     -H 'Content-Type: application/json' -d '{"action": "start", "seconds": 2}' | jq .
# ... let the machine run ~100 frames ...
curl -s "$BASE/emulator/$EMU_ID/audio/capture/status" | jq '.complete'
curl -s "$BASE/emulator/$EMU_ID/audio/capture/result?wav=true" \
     | jq '{sample_rate, duration_seconds, dominant_hz, left, right, wav_path}'
```

## Pitfalls

- **`reset` / `reset_card` are control actions**, not state: they go through
  `POST /control/audio/gs` (the state endpoint is read-only).
- **The card is read at creation; `switch_personality` is a slot change** —
  the machine restarts with the other card (a new emulator id: take it from
  the reply's `restart.emulatorId`), the machine state is lost; it is refused
  (409) while a TTD recording runs. The `gs_lightweight` feature is the only
  in-place swap left.
- **GS silence on a real Sinclair model is policy, not a bug** — the shipped
  48K / 128K / +2 / +2A / +3 configs fit no GS: plug one (`slots plug edge.1 gs
  --replace`, an `unrealistic` fit there) or use a clone.
- **Host turbo changes the mailbox race, not the card's clock** — if a
  firmware boot fails only under turbo, suspect ZX-side polling timing
  (`#BB` status), not GS-side synthesis.
- **The firmware ROM matters** — behavior differences between `gs104.rom`
  and `gs105a.rom` are real firmware differences. Check the `GS=` path in
  the model config.
- **`dump_module` paths are sandboxed** — relative, no `..`; absolute paths
  get 400.
