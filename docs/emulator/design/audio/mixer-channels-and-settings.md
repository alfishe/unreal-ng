# Mixer channels, levels and sound settings on every plane

| | |
|---|---|
| **Date** | 2026-10-21 |
| **Status** | Implemented |
| **Code** | [mixerchannels.h](../../../../core/src/emulator/sound/mixerchannels.h), [mixerchannel.h](../../../../core/src/emulator/sound/mixerchannel.h), [soundmanager_channels.cpp](../../../../core/src/emulator/sound/soundmanager_channels.cpp), [audiomixer.h](../../../../core/src/emulator/sound/audiomixer.h), [soundcharactersettings.h](../../../../core/src/emulator/sound/soundcharactersettings.h) |
| **Tests** | `core/tests/emulator/sound/mixerchannels_test.cpp`, `core/tests/automation/mcp-audio_test.cpp`, `WsHub_Test.AudioMixerPushesTheLevelsAndMuteOfEveryRowOnceThenOnChange`, opl4tests `TestChannelGains` |

The per-device mixer ([sound-device-registry.md](sound-device-registry.md)) gives every fitted source a row with mute,
solo, volume and a meter. This page adds what is under a row: its **channels**, the **levels** pushed to clients, the
rows of the ZX Spectrum Next's sound, and the sound **settings** that were Qt-only or REST-only. Everything here is
host-side: none of it is machine state, so TTD, snapshots and the program never see it, and a row whose channels were
never touched renders exactly as before (unity gains take the hardware path: bit-exact on every compiler).

## 1. Keys

A row is named by its source key; a channel by `<source>.<channel>`. Keys are case-insensitive.

| Row | Source | Channels | Where the channel's mute / volume go |
|:--|:--|:--|:--|
| `ay1`, `ay2` | AY / TurboSound, the TSFM's SSG halves | `a`, `b`, `c` | `SoundChip_AY8910` channel mute / volume |
| `fm1`, `fm2` | TurboSound FM, FM halves | `1`..`3` | `Ym2203Engine::setChannelGain` (Q15, per channel before the sum) |
| `ay3` | Fuller Box AY | `a`, `b`, `c` | the card (`ICard::SetMixerChannel`): the chip |
| `covox` | Covox / SounDrive | `la`, `lb`, `ra`, `rb` | `Covox::setChannelMute` / `setChannelGain` |
| `gs` | General Sound (classic, lightweight), NeoGS | `1`..`4` (NeoGS `1`..`8`) | `GeneralSoundCard::setHostChannelGain`: the channel's level in the card's own output |
| `moonsound_fm` | MoonSound OPL3 | `1`..`18`, metered | the OPL4 tap path (`Opl4::SetChannelMute` / `SetChannelGain`) |
| `moonsound_pcm` | MoonSound wave | `1`..`24`, metered | the same, PCM slots |
| `ms_ssg1`, `ms_ssg2` | ZX-MultiSound SSG | `a`, `b`, `c` | the card: the pair's SSG halves |
| `ms_fm1`, `ms_fm2` | ZX-MultiSound FM | `1`..`3` | the card: `Ym2203Engine` |
| `ms_midi` | ZX-MultiSound SAM2695 | `1`..`16` (mute and solo only) | the card: `sam2695::Synth::SetChannelMute` |
| `next_ay1`..`next_ay3` | the Next's three AY / YM chips | `a`, `b`, `c` | `NextAudio`: the chips |
| `next_dac` | the Next's four-channel DAC | `a`, `b` (left), `c`, `d` (right) | `NextAudio`: per-channel gain |

Rows without channels: `master` (mute only), `beeper`, `gs_mp3`, `cd0`..`cd3`, `ms_saa`, `ms_pcm`, the Sprinter's
Covox-Blaster (`covox` on that machine).

**Semantics.** A channel takes `muted`, `solo` and `volume` (0..1) or `gain_db` (-120..0). Solo is within the row:
with a channel of a row soloed, only the row's soloed channels are heard (a row solo still works across rows). The
state is visible at once and reaches the device at the next frame start (the emulation thread applies it; a busy
store moves it to the next frame, nothing waits). A channel is `audible` when its row is audible and, inside the row,
it is soloed or (no solo) not muted. Meters per channel exist where the device measures them (MoonSound).

**The Next.** Its sound is one device ([nextaudio.h](../../../../core/src/emulator/sound/nextaudio.h)) that mixes the
three AYs, the DAC and the Pi's I2S in one buffer. It now shows four rows (`next_ay1`..`3`, `next_dac`) instead of one
`covox` row: each row's mute, solo and volume become the part's gain in the device's own mix
(`IModelAudioSource::SetAudioPartGain`), and the buffer is mixed once at unity, where the COVOX row was. The meters
are the device's: an AY row shows its chip's swing, the DAC row its largest side. The parts are not capturable on
their own (capture the master mix).

## 2. Levels

`AudioMixer::Levels` is the compact readout of every row and channel: `{master: {muted}, solo, devices: [{source,
muted, solo, audible, volume, peak, active, channels: [{key, muted, solo, audible, volume, peak?}]}]}`. It is the
WebSocket topic `audio.mixer` (latest value, sent when it changed, at the subscriber's `max_hz`), the surface readout
`audio.mixer`, `GET /audio/levels`, Lua / Python `audio_levels()`, MCP `audio_mixer` `levels`. One row or channel:
the readout `audio.source.<key>` (by name: `audio.source.ay1.a`), `GET /audio/mixer/{source}`, Lua / Python
`audio_mixer_get(source)`, MCP `audio_mixer` `source`.

## 3. On every plane

| Plane | Read | Change |
|:--|:--|:--|
| CLI | `mixer` (rows), `mixer <source>` (a row and its channels), `mixer channels` (all) | `mixer ay1.a muted=1`, `mixer gs.3 volume=0.5`, `mixer ay1.b solo=1` |
| Python | `audio_mixer()`, `audio_mixer_get("ay1.a")`, `audio_levels()` | `audio_mixer_set("ay1.a", muted=True)` |
| Lua | `audio_mixer()`, `audio_mixer_get("ay1.a")`, `audio_levels()` | `audio_mixer_set("ay1.a", {muted=true})` |
| MCP | `audio_mixer` `get` / `source` / `levels` | `audio_mixer` `set` (source, muted, solo, volume, gain_db) |
| REST | `GET /audio/mixer`, `GET /audio/mixer/{source}`, `GET /audio/levels` | `PUT /audio/mixer/{source}` `{muted, solo, volume, gain_db}` |
| WebSocket | `call` of the routes; topic `audio.mixer` | `call` `PUT /audio/mixer/{source}` |
| Catalogue | readouts `audio.mixer`, `audio.source.<key>`, `toggle.audio.source_mute.<key>`, `toggle.audio.source_solo.<key>` | `audio.source_mute`, `audio.source_solo` (toggles; `source` a row or channel, or `source` + `channel`), `audio.source_volume` (`volume` or `gain_db`), `audio.volume_by` (a dial: rows, or a channel) |
| Qt | Audio Settings: Sources (rows), Channels (any source with channels: M, S, volume, meter) | the same controls |

## 4. Sound settings

The settings every plane serves from `SoundCharacterSettings` (CLI `setting <name> [value]`, REST `GET / PUT
/settings/{name}`, Lua / Python `get_sound_character` / `set_sound_character`, MCP `audio_mixer` `settings` /
`set_setting`, catalogue `set.value` / `set.toggle`). Requested on any thread, applied at the next frame boundary,
runtime only (never written to an ini).

| Name | Values | What |
|:--|:--|:--|
| `ay_voicing`, `ay_room`, `ay_punch`, `beeper_punch` | as before ([ay-tone-voicing.md](ay-tone-voicing.md)) | tone voicing, crossfeed, punch |
| `ay_stereo` | `abc`, `acb`, `mono` | the AY socket's stereo mixing (initial value `[SOUND] AYStereo`) |
| `ay_chip` | `ay8910`, `ym2149` | the AY socket's DAC curve; reads `ym2203` and refuses a change with a TurboSound FM |
| `fm_trim_db` | -12..+12 in 0.5 dB steps (a number) | the YM2203 FM level over the board's (TSFM, ZX-MultiSound); `unavailable` without YM2203 FM |
| `neogs_stereo` | `separated`, `gs` (alias `legacy`), `mono` | the NeoGS listening choice |
| `covox_dc_removal` | `on`, `off` | the Covox / SounDrive DC blocker; `unavailable` without one |
| `fir_filter` | `on`, `off` | Sound HQ (the `soundhq` feature: the AY FIR decimation and the character chains) |
| `audio_rate` | 44100, 48000, 88200, 96000, 176400, 192000, `auto` | the core rate pin (also on `set.value` now) |

A setting the machine does not have reads `unavailable` (JSON `null` in `GET /settings`).

## 5. Threads and cost

- Setters (any thread) write the store under a mutex held for microseconds; the emulation thread applies the touched
  rows at the frame start with a `try_lock` and never waits.
- Rows nobody touched cost nothing: no channel apply, and every device's gain path is the hardware path at unity
  (`Ym2203Engine` keeps ymfm's `clock_fm()`, the OPL4 a flag test, Covox and GS a branch).
- The Next's part meters add four min / max per chip per generator tick on the non-quiet path only.
