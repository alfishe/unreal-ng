# Stream Deck use cases

| | |
|---|---|
| **Date** | 2026-10-21 |
| **Status** | Scenarios and sub-scenarios in three groups, each mapped to the catalogue actions and readouts on master (`data/streamdeck/catalogue.json`, 179 actions); the menus and key layouts follow from this, then the scenario files |

The TTD timeline lives on the strip, never on keys; dials do the continuous things (scrub, step, pace, volume).

Each item names what a key, a dial or the strip binds: `action.id` (params), *readout* `name`. Marks:
*(gap)* not on master yet (the list in §4); *(gui)* a GUI action of unreal-qt (headless: `not_now`).

## 1. Common

Everyone needs these, on every deck and in every scenario, in the same place.

### C1. Control the machine
- C1.1 Run / pause: `run.pause_toggle`; *readout* `run.state`
- C1.2 Reset (hold 1 s): `mach.reset`; cold reset: `mach.cold_reset` (`ram_power_on` random / zero)
- C1.3 NMI (the Magic button): `mach.nmi`
- C1.4 The instance the deck drives: follow the focused window or pinned; *readouts* `instances`, `surfaces.context`
- C1.5 Profi front panel TURBO / CP/M: `mach.switch` (`name`); *readout* `machine.switches`

### C2. Speed
- C2.1 x1 .. x16, next: `set.speed` (`value` 1, 2, 4, 8, 16, next); a dial walks the ladder
- C2.2 Turbo on / off: `mach.turbo_toggle`; while held: `run.turbo_hold`
- C2.3 Turbo with sound: `set.toggle turbo_audio`
- C2.4 Fast / turbo tape, fast disk: `set.toggle fast_tape | turbo_tape | fast_disk`
- C2.5 Back to x1: `set.speed 1`

### C3. Change the machine
- C3.1 A model, a page per family: `mach.start` (`model`, `ram_kb`); the models the build creates: `GET /emulator/models`
  - Sinclair / Amstrad: 48K, 128K, +2, +2A, +3
  - Pentagon 128 / 512 / 1024, Scorpion 256 / 1024, Scorpion Prof
  - ATM 450, ATM 710, ZX-Evo, TS-Conf, TS-Conf + VDAC2
  - Profi 512 / 1024, Profi 3, Profi+
  - Next 1 / 2 MB, Sprinter
  - ZX-Poly 48K / 128K / Pentagon
- C3.2 Replace or a new instance, the stranded media: `mach.start` (`replace`, `stranded`)
- C3.3 Back to the previous model: `mach.previous`; *readout* `machine.previous`

### C4. Load software
- C4.1 Open a file: `media.open` (`path`, `kind`, `drive`, `autostart`, `switch_model`); tape, disk and snapshot formats
  as the loaders take them
- C4.2 Tape deck: `media.tape` (`op` play, pause, toggle, stop, rewind, seek, eject); *readout* `media.tape`
- C4.3 Recent files: `media.open_recent` (`index`, `kind`); *readout* `media.recent`
- C4.4 RZX: `rzx.play`, `rzx.stop`, `rzx.seek`; *readout* `media.rzx`

### C5. Media in the drives
- C5.1 Per drive A .. D: `media.insert`, `media.eject`, `media.protect`; *readout* `media.disk` (name, protect, motor,
  dirty)
- C5.2 Swap two drives' disks: `media.swap` (`drive`, `with`)
- C5.3 New blank disk: `media.create` (`format` trdos, plus3, unformatted; `cylinders`, `sides`)
- C5.4 IDE / SD images: `media.insert`, `media.eject`, `media.create` (`size`)
- C5.5 Discard the changes, rescan: `media.discard`, `media.rescan`

### C6. Save state
- C6.1 Quick save / load, slots 1 .. 9: `snapshot.quick` (`op`, `slot`)
- C6.2 A snapshot file: `snapshot.save` (`path`: szx, z80, sna as the model allows)
- C6.3 Save changed disks: `media.save`; the drive key shows `dirty`
- C6.4 A disk / the tape in another format: `media.export` (`format`)
- C6.5 Unsaved disks on exit / a machine switch: asked by unreal-qt

### C7. Record
- C7.1 TTD history: start / stop `ttd.record_toggle`, `ttd.start`, `ttd.stop`; its length `ttd.history_limit`; clear
  `ttd.invalidate`; save to / open from a file `ttd.dump` (`path`), `ttd.load` (`path`); *readouts* `ttd.position`,
  `ttd.status`
- C7.2 Video: `cap.record_toggle` (`preset` gameplay, gif, hq, multitrack), `cap.record_pause`; *readout* `cap.record`
- C7.2a Audio only: `cap.record_toggle` (`preset` music (FLAC), audio (WAV); `mode` audio)
- C7.3 The last seconds as a clip: `cap.clip_last` (`seconds`, `format`); a TTD range: `ttd.export_clip`
- C7.4 One sound source to a WAV: `cap.audio_capture` (`source`, `seconds`)
- C7.5 Screenshot: `cap.screenshot` (clipboard, *gui*), `cap.screenshot_save` (`format` png / gif, `area`, `source`)

### C8. Sound and mixer
- C8.1 Master mute: `audio.mute_toggle`; *readout* `audio.mute`
- C8.2 Mixer page, a key + a dial per source fitted: `audio.source_mute`, `audio.source_solo`, `audio.volume_by`
  (dial), `audio.source_volume`; *readouts* `audio.mixer`, `audio.source.<key>`
  - beeper, ay1 / ay2 / ay3, fm1 / fm2, covox, gs, gs_mp3, moonsound_fm / moonsound_pcm, cd0 .. cd3, ms_ssg1 / 2,
    ms_fm1 / 2, ms_saa, ms_pcm, ms_midi, next_ay1 .. next_ay3, next_dac
- C8.3 Channels page per chip, the same actions with a channel key (`ay1.a`, `covox.lb`, `gs.3`, `fm1.2`,
  `moonsound_fm.18`, `ms_midi.10` mute only, `next_dac.c`)
- C8.4 Character: `set.value ay_stereo | ay_chip | ay_voicing | ay_room | fm_trim_db | neogs_stereo`, `set.toggle
  ay_punch | beeper_punch | fir_filter | covox_dc_removal`
- C8.5 Sample rate: `set.value audio_rate`

### C9. Picture
- C9.1 Fullscreen: `view.fullscreen` *(gui)*; scale 1x .. 4x: `view.scale`
- C9.2 GPU on / off: `video.gpu_toggle`
- C9.3 Overscan: `video.overscan_toggle`; its crop: `video.viewport` (full, symmetric, standard, screen_only);
  *readout* `video.viewport`
- C9.4 CRT: `video.crt_toggle`, `video.crt_profile` (a name, next, prev); a parameter on a dial: `video.crt_param_by`
  (`name`), `video.crt_params_reset`; *readout* `video.crt_params`
- C9.5 De-flicker: `video.temporal` (`algorithm` or off); frame blending: `video.blending` (`frames`, `weights`),
  `video.blending_decay_by` (dial)
- C9.6 HUD: `hud.toggle`, `hud.theme`, `hud.notice` (`name`)
- C9.7 On-screen keyboard: `osd.keyboard` *(gui)*, its picture: `osd.keyboard_picture`
- C9.8 Toolbar / status bar: `view.toolbar`, `view.status_bar`; *readout* `video.window`

### C10. Monitoring
A read-only page: keys and the strip show values, a press opens the detail page. The keys are value faces
([scenarios.md §4.2](scenarios.md#42-value-faces): `face.value {readout, format, warn, alert}`, deckflow `show`); the
product scenario's `monitor` menu holds one or more per item.
- C10.1 Instances: count, model, state, the followed one; tap to follow: *readouts* `host.clients.instances`,
  `machine.model`, `run.state.state` (no `instances` readout: the count is `host.clients`'; tap to follow not yet)
- C10.2 Host: CPU %, memory, threads: *readout* `host.stats` (`cpu_percent` `{:.0f}%` warn 100 alert 200,
  `rss_bytes` `{:b}`, `threads`)
- C10.3 Emulation: fps, speed %, frame time, CPU MHz, interrupts per frame: *readout* `emulator.stats` (`fps`,
  `speed_percent` warn 95 alert 80, `frame_budget_percent`, `frame_time_us.avg`, `cpu.mhz`, `interrupts_per_frame`)
- C10.4 Peripherals: *readouts* `media.slots` (`{loaded}/{slots:#}`), `controllers.state` (`plugged`), `audio.mixer`
  (`{master.muted?MUTED|ON}`)
- C10.5 Breakpoints: total, active, by kind, groups, last hit: *readout* `debug.breakpoints` (`{active}/{total}`,
  `last_hit.address` `{:04X}`)
- C10.6 Labels and symbol sets: *readout* `debug.labels` (`total`)
- C10.7 TTD: history, memory, bookmarks: *readout* `ttd.status` (`{recording?REC|IDLE}` warn 1, `frames`, `memory_bytes`,
  `bookmarks`)
- C10.8 Profilers: *readout* `debug.analyzers` (`opcode`, `calltrace`, `coverage.instructions`)
- C10.9 Recording: *readouts* `recording.state` (`{recording?REC|IDLE}` warn 1, `duration_s`, `file_bytes`),
  `cap.record` (`{recording?REC|IDLE}`)
- C10.10 Clients per plane: *readout* `host.clients` (`{ws.connections} WS` / `{mcp.connections} MCP`, `companions`)

### C11. The deck itself
- C11.1 Back / home, brightness, pairing: the companion's own keys (scenario navigation, `streamdeck.status`)
- C11.2 The icon pack: every pack, chosen at run time per deck (`PUT /api/v1/decks/{serial}/pack`, Stream Deck
  Studio's toolbar), else the scenario's `settings.pack`, else magicwb; faces name glyphs (`face.glyph`,
  [scenarios.md §4.1](scenarios.md#41-glyph-faces-and-the-icon-pack))

## 2. User / Gamer

Playing and watching: big obvious keys, nothing that needs the debugger.

### U1. Play a game
- U1.1 Load and start it (C4)
- U1.2 Joystick on the deck: `input.joystick` (up, down, left, right, fire; held)
- U1.3 Joystick type: `input.joystick_type` (`kind` kempston, sinclair1 / 2, cursor, fuller, keys, md6); *readout*
  `input.connectors`
- U1.4 Keys a game needs: `input.key` (SPACE, ENTER, BREAK, Y / N, digits, letters), `input.release_all`
- U1.5 Kempston mouse: `feature.toggle kempstonmouse`, `input.mouse_button`, `input.mouse_release`
- U1.6 Pause for a break (C1.1); skip a slow load (C2)

### U2. Undo a mistake
- U2.1 A dial back through the history: `ttd.seek_by` (dial, `unit` seconds); press: continue from there (the TTD
  verb `resume` at the position: record from here)
- U2.2 Jump back N seconds: `ttd.unit_back` (`unit` seconds, `count`)
- U2.3 Back to live: `ttd.resume_live`
- U2.4 Quick load (C6.1)

### U3. Watch a demo
- U3.1 Load it (C4), fullscreen, CRT (C9)
- U3.2 Rewind to see an effect again (U2.1)
- U3.3 Solo a chip or a channel (C8.2, C8.3)
- U3.4 Screenshot or record it (C7)

### U4. BASIC and the ROM
- U4.1 48K / 128K BASIC from the 128K menu: `basic.mode`
- U4.2 RUN, LOAD "", a typed command: `basic.run` (`command`), `input.type`; clear: `basic.clear`
- U4.3 Macros: `input.macro` (e_mode, g_mode, break, cat, format, erase, move)
- U4.4 TR-DOS boot: `media.open` with autostart (drive A); traps: `set.toggle trdos_traps`

### U5. Game pages (later)
- U5.1 A page per game with its own keys, chosen when the game loads (profiles in `data/controllers/games`) *(gap)*

## 3. Developer / Demoscener

Building and tuning code: control of time, state and the beam.

### D1. Run at a chosen pace
- D1.1 The pace ladder on a dial: `run.pace_by`; keys `run.pace_set` (`preset` 25fps, 5fps, 1fps, frame, 100lines,
  10lines, 1line, 1op; `interval_ms`), `run.pace_next`, `run.pace_prev`
- D1.2 Push the dial: `run.pace_toggle`; stop: `run.pace_stop`; one tick: `run.pace_step`; *readout* `run.state`
  (`pace`)
- D1.3 Above x1: C2

### D2. Step by a unit
- D2.1 Step in / over / out: `run.step_in`, `run.step_over`, `run.step_out`
- D2.2 N instructions: `run.steps` (`count`); a dial: `run.step_by` (`unit`, `count`)
- D2.3 A frame, N frames, to the interrupt: `run.frame_step`, `run.frames`, `run.wait_int`
- D2.4 Scanlines: `run.n_scanlines`, `run.to_scanline`; to the first paper pixel: `run.to_pixel`
- D2.5 N T-states: `run.tstates`
- D2.6 Until PC: `run.until_pc` (`pc`); to the cursor: `run.to_cursor` (the disassembly cursor row)
- D2.7 Source level: `run.step_line`, `run.to_line`

### D3. Scrub through time (TTD)
- D3.1 The unit: `ttd.scrub_unit` (frames, seconds, batch, instructions, tstates, scanlines, bookmarks; next / prev)
- D3.2 A dial scrubs in the unit: `ttd.seek_by`; the strip shows unit, position and marks (*readout* `ttd.position`)
- D3.3 One unit back / forward: `ttd.unit_back`, `ttd.unit_forward`; frame / instruction / scanline:
  `ttd.step_back_frame`, `ttd.step_forward_frame`, `ttd.step_back_instruction`, `ttd.step_forward_instruction`,
  `ttd.step_back_scanline`, `ttd.step_forward_scanline`
- D3.4 Start / end of the history: `ttd.seek_start`, `ttd.seek_end`; a frame: `ttd.seek`
- D3.5 Reverse step, reverse continue: `ttd.reverse_step`, `ttd.reverse_continue`
- D3.6 Back to live: `ttd.resume_live`; record from here: the verb `resume` at the position *(gap: no own key action)*
- D3.7 Bookmarks: `ttd.bookmark_add`, `ttd.bookmark_delete`, `ttd.bookmark_prev`, `ttd.bookmark_next`, `ttd.bookmarks`

### D4. Breakpoints
- D4.1 At PC / at the cursor: `dbg.breakpoint_toggle_pc`, `dbg.breakpoint_toggle_cursor`
- D4.2 Add: `dbg.bp_add` (`kind` exec, read, write, rw, in, out, keyboard, nextreg, beam, copper, magic; `address`,
  `end`; `preset` fe, 7ffd, 1ffd, fffd, bffd)
- D4.3 Enable / disable / toggle / remove by id, all, kind, group: `dbg.bp_enable`, `dbg.bp_disable`, `dbg.bp_toggle`,
  `dbg.bp_remove`; groups: `dbg.bp_group_toggle`; clear all: `dbg.breakpoints_clear`; hit counts:
  `dbg.breakpoint_hits_reset`
- D4.4 Debug mode: `dbg.mode_toggle`; *readout* `debug.mode`
- D4.4a Save the breakpoints to a file, load them back *(gap: later)*
- D4.5 Conditions, IRQ breakpoints *(gap: not planned yet)*

### D5. Find the cause
- D5.1 Last write / access to the address under the cursor, then there: `ttd.find_last_write`, `ttd.find_last`
  (`access`)
- D5.2 Port events, memory at a moment / diff, coverage over the history: on the planes (`ttd port-events`,
  `memory-at`, `memory-diff`, `coverage-*`); a key shows no more than a count *(no catalogue action by design)*

### D6. Navigate the debugger
- D6.1 Open, panes: `dbg.open`, `dbg.focus_next_pane`, `dbg.focus_prev_pane` *(gui)*
- D6.2 Cursor and pages: `dbg.cursor_up / down / left / right`, `dbg.page_up / down` *(gui)*
- D6.3 Memory follows the value under the cursor (a register pair, a stack entry): `dbg.memory_follow` *(gui)*
- D6.4 Follow PC, go to, scroll mode, labels: `dbg.follow_pc`, `dbg.goto`, `dbg.scroll_mode_toggle`,
  `dbg.labels_toggle` *(gui)*
- D6.5 Next layers: `view.next_layers`, `view.next_layers_mode`, `view.next_layer` *(gui)*

### D7. Machine state
- D7.1 A register: `dbg.register_set` (`name`, `value`)
- D7.2 Memory: `mem.write` (bytes or a fill)
- D7.3 A port: `port.out` (`port` or a preset, `value`)

### D8. Build loop
- D8.1 Reload the build (snapshot + symbols in one press) *(gap)*
- D8.2 Labels / symbols: load a file `sym.import` (`path`: .map, .sym, ...), save `sym.export` (`path`), unload a set
  `sym.drop` (`id`), a set on / off `sym.set_toggle`, from the source `sym.import_source`, from memory
  `sym.import_live`
- D8.3 Run to the first breakpoint, inspect (D2, D6); save the state to come back to (C6)

### D9. Effect timing
- D9.1 Scanlines, the first paper pixel (D2.4)
- D9.2 The beam on the strip: *readout* `emulator.stats` / the beam *(gap: no beam readout, `GET /video/beam` only)*
- D9.3 Frame cost: *readout* `emulator.stats` (frame time, budget %, halted %)

### D10. Profile and trace
- D10.1 On / off / pause / clear: `prof.toggle`, `prof.pause_toggle`, `prof.clear` (`name` opcode, memory, calltrace,
  coverage, porttrace, gsporttrace, pchist, aylog, audio, all)
- D10.2 Analyzers: `analyzer.toggle`, `analyzer.clear` (`name` trdos, editor-input, ...)
- D10.3 State: *readout* `debug.analyzers`

### D11. Sound work
- D11.1 Mute / solo per chip and channel (C8.2, C8.3)
- D11.2 One source to a WAV (C7.4)

### D12. Capture for the party
- D12.1 Video, screenshot (C7); the last seconds (C7.3); the TTD session `ttd.dump`

## 4. Gaps left

| Item | What is missing |
|:--|:--|
| U5.1 | Game pages chosen by the loaded game |
| D4.5 | Conditional and IRQ breakpoints (the owner decides when) |
| D8.1 | One action that reloads a build: snapshot + symbols |
| D9.2 | A beam readout (line, dot, zone) for the strip |
| C8.3 | Channels of `ms_pcm`, `ms_saa`; meters of the AY channels; MIDI part volume (skipped: mute is enough) |
| C7.2 | Recording mode `split` (a track per AY channel) |
| D4.4a | Breakpoints to a file and back: later; the file format is the owner's choice (not JSON) |
| D3.6, U2.1 | A key action "record from here" (the TTD verb `resume` at the position) |
| - | `osd.quick_menu` is a stub |
