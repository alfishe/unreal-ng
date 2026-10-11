# Audio automation: coverage audit and gap-closing plan (2026-10-11)

**Status:** audit done from source (not measured on a running emulator); implementation not started.
**Hand-off:** each gap below is a self-contained task (A1..A6). Pattern to follow: one `DeviceState`/state function per report,
thin adapters on every plane (WebAPI + OpenAPI, MCP, CLI, Lua, Python), tests + docs, as in
[`../2026-10-07-zx-next/automation-gaps-2026-10-11.md`](../2026-10-07-zx-next/automation-gaps-2026-10-11.md) phase P5
(`nextaudio.h`, `NextAudioReport`).

## 1. What exists today

| Device | WebAPI | MCP aspect | Notes |
|:--|:--|:--|:--|
| AY8910 / TurboSound chips | `/state/audio/ay` | `audio_ay` | registers, periods, volumes per chip |
| TurboSound FM (TSFM) | `/state/audio/fm` | `audio_fm` | chips, SAA1099 voices and envelopes |
| General Sound (Z80 / LLE / LW) and NeoGS personality | `/state/audio/gs`, `/state/audio/gs/porttrace` (POST/GET) | `audio_gs` | device, mailbox, MPAG page, DAC, port trace, actions (incl. NeoGS `stereo_mode`, `sd_insert`, `flash_save`) |
| MoonSound OPL4 | `/state/audio/moonsound` (+`/fm`, `/pcm`) | `audio_moonsound`, `audio_opl4_fm`, `audio_opl4_pcm` | |
| Covox / SounDrive | `/state/audio/covox` | `audio_covox` | 4-channel DAC, port sets by mode |
| MIDI, multi-sound summary | `/state/audio/midi`, `/state/audio/multisound` | `audio_midi`, `audio_multisound` | |
| Beeper, mixer | `/state/audio/beeper`, `/state/audio/channels` | `audio_mixer` | |
| ZX Next (P5) | `/state/next/audio` | `next_audio` | AY/YM mode, ACB, turbosound, per-chip mono, DAC sums |

## 2. Gaps (to verify while implementing; each is a "not found by grep" in `core/automation`, not a proven absence of behaviour)

| ID | Gap | Evidence | Proposal |
|:--|:--|:--|:--|
| A1 | NeoGS internals: ZX DMA (`neogszxdma`), card DMA (`neogsdma`), SPI/SD (`neogsspi`, `neogsmedia`), memory banking (`neogsmemory`), interrupts (`neogsinterrupts`), flash image | only `stereo_mode`/`sd_insert`/`flash_save` actions in `state_audio_api.cpp`; no state fields from these classes | `GET /state/audio/neogs` (+ MCP `audio_neogs`, CLI `state audio neogs`, Lua/Python): DMA registers and busy flags, SPI state and last command, bank map, pending interrupts, flash dirty flag |
| A2 | VS10xx (MP3/MIDI decoder inside NeoGS) | `vs10xx.{h,cpp}` have no automation reference | fold into A1 report: mode, volume, bytes queued, decoder state |
| A3 | Sprinter Covox Blaster | `sound/sprinter/covoxblaster.*` not referenced from automation | add to `/state/audio/covox` as `blaster` block, or a sibling report if registers differ |
| A4 | SpecDrum and other simple DACs | no references anywhere in automation | decide whether the device exists first (grep core/src); if not, device gap D2, otherwise report |
| A5 | Beeper/ULA sound detail: EAR/MIC levels, tape-in, edge log | `/state/audio/beeper` exists; depth unchecked | read the report, compare with what `beeper.cpp` exposes, list the missing fields |
| A6 | Audio journal: timestamped chip register writes (AY/SAA/OPL4/DAC) with T-state/beam stamp, off by default, ring buffer | none for audio (NextReg/SD/DivMMC/Int journals exist as the template) | `AudioJournal` using the NextRegJournal ring pattern; kinds per chip; WebAPI query + control, MCP params, CLI, Lua, Python |

Out of scope here: new devices (UART/ESP, mouse, ULA+, Radastan) — see the Next gaps doc, D1/D2.

## 3. Order and acceptance

1. A1+A2 (one NeoGS report), 2. A3, 3. A5, 4. A6, 5. A4 after the existence check.
For each: report function with unit test (values derived from named constants, no magic numbers), WebAPI route in the OpenAPI table and
route test list, MCP aspect and `tools/verification/mcp` script, CLI, Lua, Python, recipe and interface docs, live check with own
`unreal-qt` (own ports, muted). Checks before landing: `tools/build/build.sh` (zero warnings), targeted `tools/build/test.sh`,
`run_all.py --launch <unreal-qt>`. Verify hardware facts (ROM, schematic, other emulators) before documenting them.
