# Control surfaces: actions, the seek, marks and readouts

| | |
|---|---|
| **Date** | 2026-10-18 |
| **Status** | Design, for review |
| **Proposals** | P2, P3, P7, P8 in [README.md](README.md#proposals-for-the-owner-2026-10-18) |
| **Extends** | [debug-and-ttd.md §3 action catalogue](../2026-10-17-controller-manager/debug-and-ttd.md#3-action-catalogue), `unreal-qt/src/controllers/defaultactions.cpp` |

## Contents

- [1. v1 catalogue](#1-v1-catalogue)
  - [1.1 Scenario actions (SD6)](#11-scenario-actions-sd6)
- [2. ttd.seek_by](#2-ttdseek_by)
- [3. Timeline marks](#3-timeline-marks)
- [4. run.step_by](#4-runstep_by)
- [5. Readouts](#5-readouts)
- [6. Planes](#6-planes)

## 1. v1 catalogue

The ids of [debug-and-ttd §3](../2026-10-17-controller-manager/debug-and-ttd.md#3-action-catalogue) stay. v1 moves
every working action of `unreal-qt/src/controllers/defaultactions.cpp` into the core registry (the stubs stay stubs)
and adds three. Kind: P press, D delta, L level, T text (L added in S1 for the analog scrub). Scope rules as the companion gate already decides the routes
(`automationgate.cpp`); "pause" = needs a paused machine (`CompanionNeedsPause`).

| Id | Kind | Scope rule | Executor | |
|:--|:--|:--|:--|:--|
| `run.pause_toggle` | P | pausing: free; resuming: run | `RunControl` | moved |
| `run.step_in`, `run.step_over`, `run.step_out`, `run.frame_step` | P | debug, pause | `RunControl` | moved |
| `run.wait_int`, `run.to_scanline`, `run.n_scanlines` | P | debug, pause | `RunControl` | moved |
| `run.turbo_hold` | P, held | run | `RunControl` | moved |
| `run.step_by` | D | debug, pause | `RunControl` / `TTDControl` ([§4](#4-runstep_by)) | new |
| `ttd.record_toggle` | P | free (TTD start / stop) | `TTDControl` | moved |
| `ttd.seek_by` | D | rewind | `TTDControl::Seek` ([§2](#2-ttdseek_by)) | new |
| `ttd.step_back_frame`, `ttd.step_forward_frame`, `ttd.resume_live` | P | rewind | `TTDControl` | moved |
| `ttd.step_back_instruction`, `ttd.step_forward_instruction` | P | debug, pause | `TTDControl` | moved |
| `ttd.reverse_step`, `ttd.reverse_continue`, `ttd.bookmark_add` | P | debug | `TTDControl` | moved |
| `cap.screenshot` | P | free (captures) | the capture route's logic | stays a GUI action in S1 (the clipboard copy of the window's picture), `gui` |
| `mach.reset` (held 1 s), `mach.nmi`, `mach.turbo_toggle` | P | run | `RunControl` | moved |
| `ctx.set` | T | free | `ContextState` | new |
| `dbg.open` | P | GUI | Qt | moved, `gui` |
| `ttd.jog_frames`, `ttd.jog_instructions` | D | rewind; debug, pause | `TTDControl` (landed on master after this design, `720fbb53e`) | moved |
| `ttd.scrub_back`, `ttd.scrub_forward` | L (level 0..1) | rewind | Qt timer → `ttd.jog_frames` on the worker | `gui` |

As built in S1 (2026-10-20, `core/src/emulator/actions/builtinactions.cpp`): `run.to_scanline` and `run.n_scanlines`
take `params.line` (default 0) and `params.count` (default 1) instead of the Qt dialog's prompt; the stubs answer
`Not Implemented` ("not available yet"); the debugger window's pause no longer switches the debug mode on (the
action pauses as `POST /pause` does).

The stubs (`ttd.find_last_write`, `ttd.bookmark_prev/next`, `dbg.focus_*`, ...) get executors in C7
of the [controller manager TODO](../2026-10-17-controller-manager/TODO.md), not here (2026-10-21: `ttd.find_last_write`,
`ttd.bookmark_prev/next`, `ttd.step_back_scanline`, `dbg.labels_toggle` have them, §1.3).

### 1.1 Scenario actions (SD6)

The actions a Stream Deck scenario runs instead of its REST steps ([Stream Deck migration.md
§4](../2026-10-19-streamdeck-integration/migration.md#4-rest-paths-to-action-ids)), built 2026-10-20 in
`core/src/emulator/actions/hostactions.cpp`. Each calls the core object its route calls, never HTTP, and takes the
**companion scope of that route** (`automation_gate::CompanionScopeOf` when `ChangesMachine`, else 0): a deck's action
and the REST step it replaces are gated alike. Params come in `ActionValue::params` (REST / MCP `params`, CLI
`name=value`, Lua table, Python keywords); the catalogue lists them in its `params` field (`?` = optional). A number
may come as a string (`count=10`), a bool as `on` / `off` / `true` / `1`, a list as an array or `"caps+1"`. "flips" =
without `on` the action toggles the current state.

| Id | Kind | Params | Scope (route) | Executor |
|:--|:--|:--|:--|:--|
| `run.pause` | P | | free (`/pause`) | `RunControl::Pause` |
| `run.resume` | P | | run (`/resume`) | `RunControl::Resume` |
| `run.steps` | P | `count?` 1..100000 | debug, pause (`/steps`) | `RunControl::Steps` (new) |
| `run.frames` | P | `count?` 1..10000 | debug, pause (`/run_frames`) | `RunControl::RunFrames` (new) |
| `run.tstates` | P | `count?` 1..10000000 | debug, pause (`/run_tstates`) | `RunControl::RunTStates` (new) |
| `mach.start` | P | `model`, `ram_kb?`, `replace?` (true), `stranded?` (keep), `ram_power_on?`, `select?` | machine (`/start`) | `replace`: `ModelSwitch` in place (media and cards carried, symbolic id and selection kept, the old instance removed), then started; else a new instance, started and selected |
| `mach.cold_reset` | P | `ram_power_on?` | machine (`/model`) | `ModelSwitch` to the same model, the instance's override kept |
| `cap.screenshot_save` | P | `path`, `format?`, `area?`, `source?` | free (`/capture`) | `Screenshotter::Take` to a file (the GUI's `cap.screenshot` copies to the clipboard) |
| `input.key` | P, held | `key` or `keys` (`"caps+1"`), `hold?` (false: a tap), `frames?` (2) | input (`/keyboard/*`) | `DebugKeyboardManager` tap / press on down, release on up |
| `input.type` | T | `tokenized?`, `delay_frames?` | input | `DebugKeyboardManager::TypeText`; tokenized: `CommandTyper` (needs a running machine) |
| `input.macro` | P | `name` | input | `ExecuteNamedSequence` |
| `input.joystick` | P, held | `buttons` (`"up+fire"`) or `dir` + `fire?`, `hold?`, `frames?` | input (`/joystick/*`) | `DebugJoystickManager` tap / press / release |
| `input.release_all` | P | | input | every key, the joystick, a running key sequence |
| `basic.run` | P | `command?` (RUN), `press_enter?`, `wait_report?`, `timeout_ms?` | debug (`/basic/run`), needs a running machine | `CommandTyper::TypeAndWait` |
| `basic.inject` | T | | debug | `BasicEncoder::injectProgram` |
| `basic.clear`, `basic.mode` | P | `mode` 48k / 128k | debug | `BasicEncoder` |
| `feature.toggle` | P | `name`, `on?` (flips) | the feature's (`/feature/{name}`: turbo run, fast loading media, debug ones debug, else machine) | `FeatureManager::setFeature` (TTD's holds apply) |
| `set.speed` | P | `value?` 1/2/4/8/16 (none: the next) | run (`/settings/speed`) | `Emulator::SetSpeedMultiplier` |
| `set.toggle` | P | `name` (turbo_mode, fast_tape, turbo_tape, fast_disk, turbo_audio, trdos_traps, ay_punch, beeper_punch), `on?` | the setting's (`/settings/{name}`) | the route's rules: FeatureManager, config, `SoundCharacterSettings` |
| `set.value` | P | `name`, `value` (speed, the bools, ay_voicing, ay_room) | the setting's | as `set.toggle` |
| `audio.mute_toggle` | P | `on?` | free (`/audio/mixer`) | `AudioMixer::Apply` master |
| `audio.source_mute` | P | `source` (beeper, ay1, ..., cd3, ms_*, next_*; a channel `ay1.a`), `channel?`, `on?` (toggle; readout `toggle.audio.source_mute.<key>`) | free | `AudioMixer::Apply` |
| `audio.source_solo` | P | `source` (a row: only soloed rows are heard; a channel: only the soloed channels of its row), `channel?`, `on?` (toggle) | free | `AudioMixer::Apply` |
| `audio.source_volume` | P | `source` (a row or channel), `channel?`, `volume` (0..1) or `gain_db` (-120..0) | free | `AudioMixer::Apply` |
| `audio.volume_by` | D | `source?` (all rows; a row or a channel `ay1.a`), `channel?`, `step?` (0.05 per detent) | free | `SoundManager::setDeviceVolume` / `setMixerChannel`, clamped 0..1 (master has no volume in the core: it is the host's) |
| `media.open` | P | `path`, `kind?` (auto by extension), `drive?` (A), `autostart?` (true), `switch_model?` | media (`/tape/load`, `/disk/X/insert`, `/snapshot/load`) | snapshot: `SnapshotLauncher::Load`; tape: `LoadTape`, autostart types `LOAD ""` (the 128K menu left for 48K BASIC); disk: `AutostartDisk` (drive A) or `LoadDisk` |
| `media.tape` | P | `op` (play, pause, toggle, stop, rewind, eject, seek), `block?` | media (`/tape/{op}`) | `Tape` under a `CpuHold`, as the routes |
| `media.eject`, `media.protect`, `media.save`, `media.create` | P | `drive` (A..D, tape, a slot id), the verb's options (`discard`, `on?` flips, `path`, `cylinders`, ...) | media (`/media/X/*`) | `MediaControl` verbs |
| `snapshot.quick` | P | `op` (save / load), `slot?` 1..9, `dir?` | save free, load media | `quick-<slot>.szx` in `~/.unreal-ng/snapshots`; a load follows the file's model |
| `snapshot.save` | P | `path`, `force?` | free (`/snapshot/save`) | `Emulator::SaveSnapshot` (an existing file needs `force`) |
| `video.overscan_toggle` | P | `on?` | machine (`/state/screen/overscan`) | `VideoViewport::SetOverscan` (the GUI's viewport with it: symmetric on, full off) |
| `video.temporal` | P | `algorithm?` (a name, off; none flips off / mod-tpgwafsd) | free (`/video/temporal`) | `Screen::SetTemporalAlgorithm` |
| `dbg.mode_toggle` | P | `on?` | debug (`/debugmode`) | FeatureManager `debugmode` |
| `dbg.breakpoints_clear` | P | | debug | `BreakpointManager::ClearUserBreakpoints` |
| `dbg.breakpoint_hits_reset` | P | `id?` | debug | `ResetHitCount` / `ResetAllHitCounts` |
| `dbg.breakpoint_toggle_pc` (toggle) | P | `address?` (else PC; a number or a label) | debug (`/breakpoints`) | the user's plain exec breakpoint there removed, else added (`AddBreakpoint(spec)`); the Qt client adds `dbg.breakpoint_toggle_cursor` (the disassembly cursor) |
| `dbg.bp_add` | P | `kind` (exec, read, write, rw, in, out, io, nextreg*, beam, copper, magic, keyboard), `address` / `end` (labels for memory), `preset?` (fe, 7ffd, 1ffd, fffd, bffd: port + mask), `mask?`, `page?`, `slot_only?`, `hc?`, `doors?`, `value?`, `value_mask?`, `opcode?`, `key?`, `on?`, `hits?`, `note?`, `group?` | debug | `AddBreakpoint(spec)` |
| `dbg.bp_remove`, `dbg.bp_enable`, `dbg.bp_disable`, `dbg.bp_toggle` (toggle `toggle.bp.<select>`) | P | `select?` (all, an id, a kind, group:<name>; or `id` / `kind` / `group`), `on?` (toggle) | debug (`/breakpoints/active`, `/breakpoints/remove`) | `BreakpointSelection` (the user's breakpoints, one repaint) |
| `dbg.bp_group_toggle` (toggle `toggle.bp_group.<group>`) | P | `group`, `on?` | debug | `BreakpointSelection::Group` |
| `dbg.register_set` | P | `name`, `value` | debug (`/registers/{name}`) | `Emulator::SetRegisterFromTool` (refused in the recorded past) |
| `mem.write` | P | `address`, `bytes` (hex) or `value` + `count?` | debug (`/memory/write`) | `EditMemoryFromTool` + `DirectWriteToZ80Memory` |
| `port.out` | P | `port` (or a preset), `value` | debug (`/ports/out`) | `PortWrite::Write` |
| `sym.import`, `sym.export`, `sym.import_source`, `sym.import_live`, `sym.drop` | P | the `SymbolControl` verb's options (`path`, `format`, `set`, `id`, ...) | free (`/symbols/*`) | `SymbolControl` verbs |
| `sym.set_toggle` (toggle `toggle.sym_set.<id>`) | P | `id`, `on?` | free | `SymbolControl` `set` |
| `prof.toggle` (toggle `toggle.profiler.<name>`), `prof.pause_toggle` (toggle `toggle.profiler_paused.<name>`), `prof.clear` | P | `name` (opcode, memory, calltrace, coverage, porttrace, gsporttrace, pchist, aylog, audio, all), `on?`, `keep?`, `capacity?`, `seconds?`, `source?` | free (`/profiler/*`) | `tracecontrol::Execute` |
| `analyzer.toggle` (toggle `toggle.analyzer.<name>`), `analyzer.clear` | P | `name` (trdos, editor-input, coverage, aylog, audiocapture), `on?` | free (`/analyzer/*`) | `AnalyzerManager` |
| `ttd.start`, `ttd.stop`, `ttd.invalidate` | P | | free (`/ttd/start`, ...) | `TTDControl` verbs |
| `ttd.seek` | P | `frame` + `tinframe?`, or `bookmark` | rewind (`/ttd/seek`) | `TTDControl` `seek` |
| `ttd.bookmark_delete`, `ttd.history_limit`, `ttd.journal`, `ttd.dump`, `ttd.load` | P | `label`; `frames?`, `bytes?`; `enabled`; `path` | debug | `TTDControl` verbs |

The core TTD actions of §1 now pass their params on as the verb's options too: `ttd.bookmark_add` `{label}`,
`ttd.reverse_step` `{count}`.

Not added (REST stays the way, the `webapi` step of a scenario): `/skip_until` (1 step), `/coverage/*`,
`/profiler/*`, `/debug/pchist` (analysis sessions, 2 steps each), `/memory/rom/protect` (a WebAPI-only switch of its
memory writes), `/control/audio/gs` reset (1 step), `/start` / `/stop` of an instance.
`ttd.seek_by`, `run.step_by` and `ctx.set` come with S2 (§2, §4). `/video/record` is
`cap.record_toggle` since 2026-10-21 (below).

**The machine area** (2026-10-21, `core/src/emulator/actions/hostactions_machine.cpp`; Stream Deck use cases C1.5,
C3.3, C4.3, C4.5, C5, C6.4, C6.5, C7, U1.3, U1.5). Readouts of their own in `machinereadouts.h` (§5).

| Id | Kind | Params | Scope (route) | Executor |
|:--|:--|:--|:--|:--|
| `mach.start` | P | `model`: a short name, a variant (`TSL-VDAC2`, `PROFI-PLUS`, their aliases) or a ZX-Poly configuration (`ZXPOLY-128K`): `machines::Resolve`, what `EmulatorManager` creates by name (before: short names only) | machine | as above |
| `mach.switch` | P, toggle | `name` (turbo, cpm), `on?` (flips) | machine (`/switches`) | `Emulator::SetFrontPanelSwitch` (the TTD input journal records it); state `toggle.mach.switch.<name>` |
| `mach.previous` | P | `replace?`, `stranded?`, `ram_power_on?` (as `mach.start`) | machine | `mach.start` with the newest entry of `machines::MachineHistory` that is not the current model (EmulatorManager notes every machine it creates by name); `not_now` without one |
| `media.insert` | P | `drive` (a slot selector or `auto`), `path`, `wp?`, `access?`, a disposition | media (`/media/X/insert`) | `MediaControl` `insert` |
| `media.swap` | P | `drive`, `with` (the two drives exchange their disks) or `path` | media | `eject` + `insert` of the two files, write protect carried (unsaved writes: `save` or `discard`; a blank disk: refused); with `path` the `swap` verb |
| `media.export` | P | `drive`, `format` (trd, scl, udi, fdi, dsk, td0, mgt, img, hfe, scp; tap, tzx) or `path`, `force?` | media | `MediaControl` `export`; without a path next to the medium's file with the new extension |
| `media.discard`, `media.rescan` | P | `drive`; `save?` / `discard?` | media | `MediaControl` verbs |
| `media.create` | P | `drive`, `format?` (auto, trdos, plus3, unformatted / raw), `cylinders?`, `sides?`, `size?` (bytes, `64M`, `2G`) | media | `MediaControl` `create` (`trdos`: formatted, an empty catalogue) |
| `media.open_recent` | P | `index?` (0), `kind?` (all, snapshot, tape, disk, rzx, image, cd), `autostart?`, `drive?` | media | `RecentFiles`, then `media.open` (an `rzx`: `RzxLauncher::Play`; an `image` / `cd`: `insert auto`) |
| `rzx.play` | P | `path`, `tolerant?`, `switch_model?` (true) | media (`/rzx/play`) | `RzxLauncher::Play` |
| `rzx.stop` | P | | media | `Emulator::StopRzx` |
| `rzx.seek` | P | `frame` or `by` (frames, ±) | media; `not_now` without a playback | `Emulator::SeekRzx` |
| `input.joystick_type` | P | `kind` (kempston, sinclair1, sinclair2, cursor, fuller, keys, md6, none), `connector?` (auto; none needs one), `keys?` | machine (`/controllers/{connector}/plug`: a plug is hardware) | `ControllerControl` `plug` / `unplug` |
| `input.mouse_button` | P, held | `button` (left, right, middle), `hold?` (false: a click), `frames?` (2) | input (`/mouse/press`) | `DebugMouseManager` `Click` / `PressButton` / `ReleaseButton` |
| `input.mouse_release` | P | | input | `DebugMouseManager::ReleaseAllButtons` |
| `cap.record_toggle` | P, toggle | `preset?` (gameplay, music, audio, gif, hq, multitrack), `format?`, `container?`, `audio?`, `mode?` (single, multi, audio), `profile?`, `path?`, `fps?`, `scale?`, `region?` | free (`/video/record`) | `capture::RecordingControl` `toggle`: start (default folder `<user data>/recordings`) or stop |
| `cap.record_pause` | P, toggle | | free | `RecordingControl` `pause` / `resume`; `not_now` without a recording |
| `cap.clip_last` | P | `seconds?` (10), `format?` (gif, mp4, mkv, webm, mov, avi), `path?`, `scale?` | free (`/ttd/export-clip`); `not_now` without a TTD recording | `capture::ClipLast`: the recording pauses for browsing at its end (D8), the last seconds go to a video file, the recording goes on from the paused point, the run state as it was |
| `ttd.export_clip` | P | `from`, `to`, `path?` (a video file; a folder: the lossless clip), `codec?`, `scale?`, `chunk?` | free | `TTDControl` `export-clip` (a video path: `capture::ExportTtdClip`) |
| `cap.audio_capture` | P | `source?` (master, beeper, ay1, ...), `seconds?` (5, 0.01..30), `path?` (.wav) | free (`/audio/capture`); `not_now` on a paused machine | the audio capture analyzer for N emulated seconds (waits at most 4x that), then a WAV |

### 1.2 Picture and window actions (C9)

Added 2026-10-21 in `core/src/emulator/actions/pictureactions.cpp`: the window's display settings (WindowDisplay,
the same validation as `video window` on every plane) and the overscan viewport (VideoViewport). Scope 0 (their
routes change nothing); headless the window ones answer `not_now` "no window"; their toggles read the settings the
window published (`WindowView::Publish`). The table with params:
[command-interface.md §16](../../emulator/design/control-interfaces/command-interface.md#16-host-actions).

| Id | Kind | Params | Core |
|:--|:--|:--|:--|
| `video.gpu_toggle`, `video.crt_toggle`, `video.blending` | P | `on?` (`video.blending` also `frames?`, `weights?`, `decay?`) | WindowDisplay `gpu`, `crt`, `blending*` |
| `video.crt_profile` | P | `profile?` (name, next, prev) | `crt_profile` |
| `video.temporal_toggle`, `video.temporal_effect` | P | `on?` / `effect?` (blend, dlss, next) | the Temporal Effects dialog's switch and effect: `blending` or `Screen::SetTemporalAlgorithm`, one at a time |
| `video.crt_param_by`, `video.blending_decay_by` | D | `name` / - | `SetCrtParameters` steps / `blending_decay` |
| `video.crt_params_reset` | P | `name?` | `SetCrtParameters` reset |
| `video.viewport` | P | `viewport?` (name, next, prev) | `VideoViewport::SetViewport` |
| `view.fullscreen`, `view.toolbar`, `view.status_bar`, `view.next_layers` | P | `on?` | `fullscreen`, `toolbar`, `status_bar`, `next_layers` |
| `view.scale`, `view.next_layers_mode` | P | `scale?` 1..4 / `mode?` (name, next, prev) | `scale`, `next_layers_mode` |
| `view.next_layer`, `hud.notice` | P | `name`, `on?` | `next_layer.<name>`, `hud_notice.<id>` (toggles `toggle.view.next_layer.<name>`, `toggle.hud.notice.<id>`) |
| `hud.toggle`, `hud.theme`, `osd.keyboard_picture`, `osd.keyboard_style` | P | `on?` / `theme?` / `picture?` / `style?` | `hud`, `hud_theme`, `osd_keyboard_picture`, `osd_keyboard_style` |

### 1.3 Run, step and time-travel units (2026-10-21)

Stream Deck use cases D1, D2, D3, D5, D6 ([use-cases](../2026-10-19-streamdeck-integration/use-cases.md)), in
`core/src/emulator/actions/steppingactions.cpp` (registered after the host actions) and, for the disassembly,
`unreal-qt/src/controllers/guiactions.cpp`. The stubs `ttd.find_last_write`, `ttd.bookmark_prev`, `ttd.bookmark_next`,
`ttd.step_back_scanline` and `dbg.labels_toggle` got their executors.

| Id | Kind | Params | Scope | Executor |
|:--|:--|:--|:--|:--|
| `run.pace_set`, `run.pace_next`, `run.pace_prev` | P | `preset?`, `interval_ms?` | run | `RunPacer::Set` / `Shift` |
| `run.pace_by` | D | | run | `RunPacer::Shift(delta)` |
| `run.pace_toggle` (toggle) | P | | run | `RunPacer::Toggle`: a running pace stops, a machine at full speed pauses, else the pace starts |
| `run.pace_stop`, `run.pace_step` | P | | free; run (as `POST /run_pace`) | `RunPacer::Stop`, `Step` |
| `run.to_pixel` | P | | debug, pause (`/run_to_pixel`) | `RunControl::RunToPixel` |
| `run.until_pc` | P | `pc`, `max_tstates?`, `breakpoints?` | debug, pause (`/skip_until`) | `RunControl::RunUntilPc` |
| `run.to_cursor` | P | `addr?` (else `DebuggerCursors` "disasm"), `max_tstates?` (500 frames) | debug, pause | `RunControl::RunUntilPc`, breakpoints honoured |
| `run.step_line`, `run.to_line` | P | `max_tstates?`; `line` | debug, pause (`/listing/*`) | `RunControl::StepLine` / `RunToLine` |
| `ttd.step_back_scanline`, `ttd.step_forward_scanline` | P | `count?` | debug; history | `reverse-step` `scanlines`; a seek to the T-state |
| `ttd.bookmark_prev`, `ttd.bookmark_next` | P | | rewind; history | a seek to the bookmark before / after the position |
| `ttd.bookmarks` | P | | free | `bookmarks` |
| `ttd.find_last_write`, `ttd.find_last` | P | `addr?` (else the "memory" cursor, then "disasm"), `access?`, `seek?` (true), the filters | debug; history | `find-last`, then `seek` there |
| `ttd.seek_start`, `ttd.seek_end` | P | | rewind; history | `seek` to the published cursor's first / end |
| `ttd.unit_back`, `ttd.unit_forward` | P | `unit?`, `count?` | rewind; history | `ttd.seek_by` with delta ∓1 |
| `ttd.scrub_unit` | T | `count?` | free | the instance's unit of `ttd.seek_by`; the `ttd.position` readout shows `unit`, `unit_count` |
| `ttd.journal_build`, `ttd.journal_build_cancel`, `ttd.export_clip` | P | `from_frame?`, `to_frame?`; —; `path`, `from?`, `to?`, `chunk?` | the routes' | the verbs |
| `dbg.follow_pc`, `dbg.labels_toggle` (toggle), `dbg.scroll_mode_toggle`, `dbg.goto` | P | `addr` (goto) | GUI | the Qt disassembly |

**The debugger's cursors** (`actions::DebuggerCursors`): the Qt client publishes the address of the disassembly's
cursor row as `disasm` and the memory view's cursor as `memory`, per instance; `run.to_cursor`, `ttd.find_last*` (and
the breakpoint actions at the cursor) take them when the call names no `addr`. The cursor row moves with a click, with
Up / Down (an instruction; a byte in byte scroll) and PageUp / PageDown (16) in the disassembly, and so with a deck's
`dbg.cursor_up` / `dbg.cursor_down` / `dbg.page_up` / `dbg.page_down` on that pane: it starts at PC when there is no
row yet, the view scrolls to keep it visible, the row is kept and marked across refreshes. The mouse wheel scrolls the
view without moving it. In the memory pane the same keys move the hex cursor (the view follows it). Headless there is
no cursor: the call needs `addr`.

**`ttd.seek_by` units** (§2): `params.unit`, else the instance's (`ttd.scrub_unit`): `frames`, `seconds` (the
curve), `batch` (`count` frames a step, default 10), `instructions` (back: `reverse-step count`; forward: up to 64
instruction steps), `tstates` (`count`, default 100; back `reverse-step tstates`, forward a seek), `scanlines` (as
tstates by `t_line`), `bookmarks` (the N-th bookmark on). The result names `unit`.

**`run.step_by` units** (§4): `params.unit` `frames`, `scanlines` or `tstates` with `count`: forward runs them
(`RunFrames`, `RunNScanlines`, `RunTStates`), back steps back through the history (`MoveFrames`, `reverse-step`).

## 2. ttd.seek_by

Value: `delta` (steps, signed), `rate` (steps/s), `params {unit, min, max, rate_lo, rate_hi}`. Defaults
`unit: frames`, `rate_lo: 4`, `rate_hi: 40` (the owner's tuning on the Stream Deck, 2026-10-17).

```
t        = clamp((rate - rate_lo) / (rate_hi - rate_lo), 0, 1)
per      = min * (max / min) ^ t                       // per step, in the unit
perFrame = unit == seconds ? max(1, llround(per * fps)) : max(1, llround(per))
frames   = llround(delta * perFrame)
target   = clamp(cursor.frame + frames, cursor.first, cursor.end)
Seek {frame: target, tinframe: 0}                      // tinframe 0: the frame's start
```

- `fps` from the machine's frame period ([architecture §7](architecture.md#7-the-ttd-published-cursor-and-readouts)).
  The conversion to frames happens before rounding, so 0.02 s on a Pentagon is one frame, not zero.
- **Clamping** to the history: a target past the end would come back as a success with `halt_reason:
  out_of_range` and no move; the executor clamps first, so it never sends one. A clamped target is an `edge` mark.
- **Availability**: without a history ("record first"). While recording, the default backend pauses the
  recording for browsing (TTD D8) and the seek goes on; `end` is then the paused end.
- **Result**: the route's body (`reached`, `arrived_at`, `halt_reason`, `earliest`, `blocking_marker`, `state`) plus
  `marks` ([§3](#3-timeline-marks)).
- A seek that halts at an external-event marker (`halt_reason: external_event`, a replay barrier,
  `/ttd/markers`) gives a `barrier` mark; the next turn in the same direction stops there again (the barrier is the
  TTD's rule, not ours).
- The curve is applied here only. A router binding passes its parameters in `params`; a companion passes them in its
  call (the Stream Deck's three dials: 1 → 100, 10 → 1000, 100 → 10 000).

As built (S2, 2026-10-20, `builtinactions.cpp` `SeekBy`): the base is the published cursor's frame; `rate` comes
from the value, else from `params.rate` (the CLI and Python pass it as a parameter); `min` / `max` default to 1 / 100
frames or 0.02 / 2 s; a parameter out of range is `Bad Request`. A move that rounds to 0 frames, or a clamped target
equal to the position, sends no seek: success with the place it stands (and `edge` when clamped). The body adds
`from`, `frame` (the target), `frames`, `fps`, `marks` [{`kind`, `frame`}] strongest first. The v1 backend refuses a
seek while it records (its rule); the engine pauses the recording for browsing (D8).

## 3. Timeline marks

`TimelineMarks::Crossed(from, to, first, fps, bookmarks)` (`core/src/debugger/ttd/timelinemarks.*`): pure, cheap
(arithmetic and the bookmark list), no history access.

| Kind | Rule | Pattern (default profile) |
|:--|:--|:--|
| `second` | the whole second `floor((f - first) / fps)` differs between `from` and `to` (fps is not an integer: 48.83 on Pentagon, so seconds start at frames 0, 49, 98, 147, …, never drifting) | `tick` |
| `ten_seconds` | same with `floor((f - first) / (10 × fps))`; replaces `second` there | `double` |
| `edge` | the target was clamped to `first` or `end` | `buzz` |
| `barrier` | the seek halted at an external-event marker | `buzz` |
| `bookmark` | a bookmark lies in the interval | `double` |

As built: at most one `second` / `ten_seconds` mark per seek, at the boundary nearest the target (forward the start of
the target's second, back the start of the second after it), and one `bookmark`, the nearest the target in
`(from, to]` forward, `[to, from)` back (`ttd::timeline_marks::Crossed`, `timelinemarks.h`).

Several marks in one seek: the strongest plays (`edge`/`barrier` > `bookmark` = `ten_seconds` > `second`); the
40 ms rate limit of [architecture §9](architecture.md#9-haptic-feedback) merges the rest.

## 4. run.step_by

One dial both ways (owner, 2026-10-17: "step in forward, step back backward"):

- `delta > 0`: `RunControl` step in, `min(delta, 16)` times; needs a pause.
- `delta < 0`: `TTDControl` instruction step back, `min(-delta, 16)` times; needs a history (else `not_now`, "record
  first").
- A breakpoint hit while stepping stops the loop; the result names it.

As built (S2): the result is the last step's body plus `steps` (instructions executed: the step that met an execution
breakpoint executed none), `direction` (`forward` / `back`) and `stopped_by` (`breakpoint`, or the error that ended
the loop after the first step). Availability: back without a history is `not_now` "record first"; both ways need a
pause.

## 5. Readouts

| Id | Value | When |
|:--|:--|:--|
| `ttd.position` | `{frame, tinframe, first, end, state, fps}` | the cursor changed, ≤ 50 Hz |
| `run.state` | `{state: running/paused, speed}` | on change |
| `video.window` | the window's published display settings (`available`) | on change |
| `video.crt_params` | `{profile, kind, values}` of the active CRT profile | on change |
| `video.viewport` | `{overscan, viewport, display, ...}` | on change |
| `machine.previous` | `{model, ram_kb, current}` (null fields: no previous model yet) | on change |
| `machine.switches` | `{switches: [{name, on}]}` | on change |
| `media.recent` | `{revision, kinds: {snapshot: [...], tape: [...], ...}, all: [{index, kind, name, path, time}]}` | on change |
| `media.rzx` | `{loaded, active, name, state, frame, total_frames, progress, desyncs}` | on change |
| `input.connectors` | `{connectors: [{id, kind, device, fixed, port?}]}` | on change |
| `cap.record` | `{available, recording, paused, frames, seconds, bytes, output}` | on change |
| `actions.unavailable` | `{id: reason}` of the catalogue's actions the user's hands could not run now (the dry run of the gates, as a host device acting for the user: published state only, no session opened); a deck greys their keys out and says the reason on a press (`set.speed`: `TTD holds a history recorded at 1x: discard it to change the speed`) | on change |

`media.disk` also carries each drive's `dirty` (unsaved writes). The context is not a readout: it is the `ctx` message ([protocol.md §3](protocol.md#3-messages)).

## 6. Planes

Every plane (automation planes guidelines; built in S1, the `surface` topic in S4). Reference:
[command-interface.md §16](../../emulator/design/control-interfaces/command-interface.md#16-host-actions).

| Plane | List | Run |
|:--|:--|:--|
| CLI | `action list [--json]` | `action run <id> [value] [param=val ..] [--json]` |
| Python | `emu.actions()` | `emu.action(id, value=None, **params)` |
| Lua | `actions()` | `action(id, [value], [params])` |
| MCP | `actions` `list` | `actions` `run` (`id`, `value`, `params`, `target`) |
| REST | `GET /api/v1/controllers/actions` (`?instance=`: id, label, group, kind, held, gui, scope, `needs_pause`, `available`, `error`, `reason`) | `POST /api/v1/controllers/actions/{id}` `{instance?, value?, params?}` |
| WebSocket | `{"op":"call","method":"GET","path":"/api/v1/controllers/actions"}` on `/api/v1/ws` | `{"op":"call","method":"POST","path":"/api/v1/controllers/actions/{id}","body":…}`; push: the `surface` topic (`join`, `action`, `result`, `ctx`, readouts; [websocket-interface.md §3.1](../../emulator/design/control-interfaces/websocket-interface.md#31-the-surface-topic)) |
| Qt | menus and toolbars call the registry (C7); a device's action bindings run through it (`unreal-qt/src/controllers/guiactions.cpp`) | |

Rows for the [controller manager parity table](../2026-10-17-controller-manager/surfaces.md#5-parity-table).
