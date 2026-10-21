#!/usr/bin/env python3
"""The canonical list of Stream Deck key glyphs: every icon pack draws exactly these ids.

    python3 tools/icons/streamdeck_glyphs.py      # writes data/streamdeck/icons/glyphs.json

A glyph is one meaning on a key: a catalogue action (GET /api/v1/controllers/actions), one value of a parametrised
action (a tape operation, a joystick direction), a navigation key, a menu folder, a status, or a state variant of one
of them ("<id>.on": the face while the action's readout is true). Packs live in data/streamdeck/icons/packs/<pack>/
(pack.json + src/<glyph id>.svg|png); tools/icons/render_streamdeck_icons.py renders and checks them.

Each entry: id, group, title, "draw" (the brief for the artist: what the picture shows, so that every pack draws the
same thing), and, for a state variant, "variant_of".
"""

import json
import os

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT_DIR, 'data', 'streamdeck', 'icons', 'glyphs.json')

GROUPS = [
    ('nav', 'Navigation and deck'),
    ('run', 'Run control'),
    ('machine', 'Machine'),
    ('ttd', 'Time travel'),
    ('debug', 'Debugger'),
    ('media', 'Media'),
    ('audio', 'Audio'),
    ('video', 'Video and capture'),
    ('input', 'Input'),
    ('basic', 'BASIC'),
    ('settings', 'Settings'),
    ('status', 'Status'),
]

# id, group, title, draw
GLYPHS = [
    # --- navigation and deck
    ('nav.back', 'nav', 'Back', 'arrow pointing left'),
    ('nav.home', 'nav', 'Home', 'house'),
    ('nav.folder', 'nav', 'Menu (generic)', 'folder / drawer'),
    ('nav.prev_page', 'nav', 'Previous page', 'chevron left'),
    ('nav.next_page', 'nav', 'Next page', 'chevron right'),
    ('nav.target', 'nav', 'Target: follows focus', 'crosshair (ring with four ticks, centre dot)'),
    ('nav.target.on', 'nav', 'Target: pinned', 'push pin'),
    ('nav.instances', 'nav', 'Instances / windows', 'two overlapping windows'),
    ('deck.menu', 'nav', 'Deck settings', 'a small key grid (the deck itself)'),
    ('deck.brightness', 'nav', 'Deck brightness', 'sun'),
    # --- run control
    ('run.play', 'run', 'Run / continue (shown while paused)', 'play triangle'),
    ('run.pause', 'run', 'Pause (shown while running)', 'two pause bars'),
    ('run.stop', 'run', 'Stopped', 'stop square'),
    ('run.step_in', 'run', 'Step in', 'arrow down into a dot'),
    ('run.step_over', 'run', 'Step over', 'arc arrow over a dot'),
    ('run.step_out', 'run', 'Step out', 'arrow up out of a dot'),
    ('run.frame_step', 'run', 'Frame step', 'film frame with a small forward triangle'),
    ('run.wait_int', 'run', 'Run to interrupt', 'one square pulse (INT) with a forward triangle'),
    ('run.to_scanline', 'run', 'Run to scanline', 'raster lines, one highlighted, a triangle pointing at it'),
    ('run.n_scanlines', 'run', 'Run N scanlines', 'raster lines with a bracket over three of them'),
    ('run.turbo_hold', 'run', 'Turbo while held', 'fast-forward double triangle over a bar'),
    ('run.steps', 'run', 'Step N instructions', 'step-in arrow and the letter N'),
    ('run.frames', 'run', 'Run N frames', 'film frame and the letter N'),
    ('run.tstates', 'run', 'Run N t-states', 'clock pulse train and the letter T'),
    # --- machine
    ('mach.reset', 'machine', 'Reset (hold)', 'circular arrow'),
    ('mach.cold_reset', 'machine', 'Restart (cold)', 'power symbol'),
    ('mach.nmi', 'machine', 'NMI', 'lightning bolt'),
    ('mach.turbo', 'machine', 'Turbo (off)', 'speed gauge, needle low'),
    ('mach.turbo.on', 'machine', 'Turbo (on)', 'speed gauge, needle high'),
    ('mach.start', 'machine', 'Start a machine / machines menu', 'a ZX Spectrum (wedge with key rows)'),
    ('mach.chip', 'machine', 'Machine of another family', 'a CPU chip with pins'),
    # --- time travel
    ('ttd.menu', 'ttd', 'Time travel', 'clock with an arrow turning back round it'),
    ('ttd.record', 'ttd', 'TTD recording (off)', 'record button, idle'),
    ('ttd.record.on', 'ttd', 'TTD recording (on)', 'record button, lit'),
    ('ttd.stop', 'ttd', 'Stop recording', 'stop square inside the record ring'),
    ('ttd.step_back_frame', 'ttd', 'Back one frame', 'bar and a triangle pointing left (|<)'),
    ('ttd.step_forward_frame', 'ttd', 'Forward one frame', 'triangle pointing right and a bar (>|)'),
    ('ttd.step_back_instruction', 'ttd', 'Back one instruction', 'code lines with a small triangle pointing left'),
    ('ttd.step_forward_instruction', 'ttd', 'Forward one instruction', 'code lines with a small triangle pointing right'),
    ('ttd.step_back_scanline', 'ttd', 'Back one scanline', 'raster lines with a triangle pointing left'),
    ('ttd.reverse_step', 'ttd', 'Reverse step', 'arrow up into a dot (step in, backwards)'),
    ('ttd.reverse_continue', 'ttd', 'Reverse continue', 'rewind double triangle (<<)'),
    ('ttd.go_start', 'ttd', 'Go to the start', 'bar and rewind double triangle (|<<)'),
    ('ttd.resume_live', 'ttd', 'Back to live', 'play triangle with a live dot'),
    ('ttd.bookmark', 'ttd', 'Bookmarks / go to a bookmark', 'bookmark ribbon'),
    ('ttd.bookmark_add', 'ttd', 'Add a bookmark', 'ribbon with a plus'),
    ('ttd.bookmark_delete', 'ttd', 'Delete a bookmark', 'ribbon with a cross'),
    ('ttd.bookmark_prev', 'ttd', 'Previous bookmark', 'ribbon with a chevron left'),
    ('ttd.bookmark_next', 'ttd', 'Next bookmark', 'ribbon with a chevron right'),
    ('ttd.seek', 'ttd', 'Go to a frame', 'arrow pointing right into a bar'),
    ('ttd.find_last_write', 'ttd', 'Find the last write', 'magnifier over a pencil mark'),
    ('ttd.invalidate', 'ttd', 'Drop the history (hold)', 'waste bin'),
    ('ttd.history_limit', 'ttd', 'History limit', 'stacked frames under a limit bar'),
    ('ttd.journal', 'ttd', 'Input journal (off)', 'closed book'),
    ('ttd.journal.on', 'ttd', 'Input journal (on)', 'open book'),
    ('ttd.dump', 'ttd', 'Save the session', 'tray with an arrow down into it'),
    ('ttd.load', 'ttd', 'Open a session', 'tray with an arrow up out of it'),
    ('ttd.timecode', 'ttd', 'Time code', 'clock face'),
    # --- debugger
    ('debug.menu', 'debug', 'Debugger / open the debugger', 'bug'),
    ('dbg.mode', 'debug', 'Debug mode (off)', 'bug, dimmed'),
    ('dbg.mode.on', 'debug', 'Debug mode (on)', 'bug, lit'),
    ('dbg.breakpoint_toggle_pc', 'debug', 'Breakpoint at PC', 'breakpoint mark with the PC arrow pointing at it'),
    ('dbg.breakpoints_clear', 'debug', 'Clear breakpoints', 'breakpoint mark with a cross'),
    ('dbg.breakpoint_hits_reset', 'debug', 'Reset hit counts', 'breakpoint mark with a 0'),
    ('dbg.labels_toggle', 'debug', 'Labels', 'label tag'),
    ('dbg.focus_prev_pane', 'debug', 'Previous pane', 'two panes, the left one lit, arrow left'),
    ('dbg.focus_next_pane', 'debug', 'Next pane', 'two panes, the right one lit, arrow right'),
    ('dbg.cursor_up', 'debug', 'Cursor up', 'arrow up'),
    ('dbg.cursor_down', 'debug', 'Cursor down', 'arrow down'),
    ('dbg.cursor_left', 'debug', 'Cursor left', 'arrow left'),
    ('dbg.cursor_right', 'debug', 'Cursor right', 'arrow right'),
    ('dbg.page_up', 'debug', 'Page up', 'double chevron up under a bar'),
    ('dbg.page_down', 'debug', 'Page down', 'double chevron down over a bar'),
    ('dbg.memory_follow', 'debug', 'Memory follows the cursor', 'memory grid with an arrow pointing into it'),
    ('debug.disasm', 'debug', 'Disassembly', 'code lines with the PC marker'),
    ('debug.memory', 'debug', 'Memory', '3 x 3 grid of cells, one lit'),
    ('debug.registers', 'debug', 'Registers', 'rows of register bars'),
    ('debug.stack', 'debug', 'Stack', 'stacked plates with an arrow down'),
    # --- media
    ('media.open', 'media', 'Open software', 'open folder with an arrow out of it'),
    ('media.software', 'media', 'Software (menu)', 'two stacked floppies'),
    ('media.tape', 'media', 'Tape (menu)', 'compact cassette'),
    ('media.tape.play', 'media', 'Tape play', 'play triangle in the media colour'),
    ('media.tape.pause', 'media', 'Tape pause', 'pause bars in the media colour'),
    ('media.tape.stop', 'media', 'Tape stop', 'stop square in the media colour'),
    ('media.tape.rewind', 'media', 'Tape rewind', 'rewind double triangle in the media colour'),
    ('media.tape.eject', 'media', 'Tape eject', 'cassette with an eject mark'),
    ('media.tape.load', 'media', 'LOAD ""', 'cassette with an arrow down'),
    ('media.disk', 'media', 'Disk (menu)', '3.5" floppy'),
    ('media.eject', 'media', 'Eject (disk)', 'eject mark (triangle over a bar)'),
    ('media.protect', 'media', 'Write protect (off)', 'open padlock'),
    ('media.protect.on', 'media', 'Write protect (on)', 'closed padlock'),
    ('media.save', 'media', 'Save the medium', 'floppy with an arrow down'),
    ('media.create', 'media', 'New blank disk', 'floppy with a plus'),
    ('media.disk.cat', 'media', 'CAT (disk directory)', 'floppy with list lines'),
    ('media.cdrom', 'media', 'CD-ROM', 'compact disc'),
    ('media.hdd', 'media', 'Hard disk', 'hard disk drive'),
    ('snapshot.quick_save', 'media', 'Quick save', 'memory module with an arrow down'),
    ('snapshot.quick_load', 'media', 'Quick load', 'memory module with an arrow up'),
    ('snapshot.save', 'media', 'Save a snapshot', 'memory module over a floppy'),
    # --- audio
    ('audio.sound', 'audio', 'Sound on (menu, unmuted)', 'speaker with waves'),
    ('audio.muted', 'audio', 'Muted', 'speaker with a cross'),
    ('audio.volume_up', 'audio', 'Volume up', 'speaker with a plus'),
    ('audio.volume_down', 'audio', 'Volume down', 'speaker with a minus'),
    ('audio.source', 'audio', 'A sound source (on)', 'sound chip with a wave'),
    ('audio.source.on', 'audio', 'A sound source (muted)', 'sound chip with a wave, crossed out'),
    # --- video and capture
    ('video.menu', 'video', 'Video (menu)', 'monitor'),
    ('video.overscan', 'video', 'Border / overscan (off)', 'screen, small border'),
    ('video.overscan.on', 'video', 'Border / overscan (on)', 'screen with the full border'),
    ('video.temporal', 'video', 'De-flicker (off)', 'two offset frames, outlined'),
    ('video.temporal.on', 'video', 'De-flicker (on)', 'two offset frames, merged / lit'),
    ('cap.screenshot', 'video', 'Screenshot (capture menu)', 'photo camera'),
    ('cap.screenshot_save', 'video', 'Save a screenshot', 'camera with an arrow down'),
    ('cap.record', 'video', 'Record video (off)', 'video camera'),
    ('cap.record.on', 'video', 'Record video (on)', 'video camera with the record light on'),
    ('cap.clip_last', 'video', 'Clip the last seconds', 'scissors'),
    # --- input
    ('input.key', 'input', 'A key (the key name is the label)', 'blank keycap'),
    ('input.keyboard', 'input', 'Keyboard (menu) / type text', 'keyboard'),
    ('input.macro', 'input', 'Key macro', 'keycap with a play triangle'),
    ('input.release_all', 'input', 'Release all keys and buttons', 'keycap with an arrow up'),
    ('input.joystick', 'input', 'Joystick (menu)', 'joystick'),
    ('input.joy.up', 'input', 'Joystick up', 'arrow up from a centre'),
    ('input.joy.down', 'input', 'Joystick down', 'arrow down from a centre'),
    ('input.joy.left', 'input', 'Joystick left', 'arrow left from a centre'),
    ('input.joy.right', 'input', 'Joystick right', 'arrow right from a centre'),
    ('input.joy.up_left', 'input', 'Joystick up + left', 'diagonal arrow'),
    ('input.joy.up_right', 'input', 'Joystick up + right', 'diagonal arrow'),
    ('input.joy.down_left', 'input', 'Joystick down + left', 'diagonal arrow'),
    ('input.joy.down_right', 'input', 'Joystick down + right', 'diagonal arrow'),
    ('input.joy.fire', 'input', 'Joystick fire', 'round fire button'),
    ('osd.keyboard', 'input', 'On-screen keyboard', 'keyboard on a screen'),
    ('osd.quick_menu', 'input', 'Quick menu', 'window with menu lines'),
    # --- BASIC
    ('basic.menu', 'basic', 'BASIC (menu) / LIST', 'program listing with line numbers'),
    ('basic.run', 'basic', 'RUN', 'listing with a play triangle'),
    ('basic.inject', 'basic', 'Write a BASIC program', 'listing with a plus'),
    ('basic.clear', 'basic', 'NEW (clear the program)', 'listing with a cross'),
    ('basic.mode.48', 'basic', '48K BASIC', 'the number 48'),
    ('basic.mode.128', 'basic', '128K BASIC', 'the number 128'),
    # --- settings
    ('settings.menu', 'settings', 'Settings (menu)', 'gear'),
    ('settings.toggle', 'settings', 'A feature or setting (off)', 'switch, off'),
    ('settings.toggle.on', 'settings', 'A feature or setting (on)', 'switch, on'),
    ('settings.value', 'settings', 'Set a value', 'three sliders'),
    ('set.speed', 'settings', 'Speed (menu, next speed)', 'speed gauge, needle in the middle'),
    # --- status
    ('status.ok', 'status', 'OK / done', 'check mark'),
    ('status.warn', 'status', 'Warning', 'warning triangle with !'),
    ('status.error', 'status', 'Error / failed', 'disc with a cross'),
    ('status.busy', 'status', 'Busy', 'hourglass'),
    ('status.offline', 'status', 'No connection to unreal-ng', 'unplugged plug'),
    ('status.hold', 'status', 'Hold to confirm', 'stopwatch'),
]

# Catalogue action -> faces. face / face_on: glyph ids; state: the readout that selects face_on; params: a glyph per
# parameter value; label: the key's text (a pack glyph with a label, or a text-only face where there is no glyph).
ACTIONS = {
    'run.pause_toggle': {'face': 'run.pause', 'face_on': 'run.play', 'state': 'run.state.paused'},
    'run.pause': {'face': 'run.pause'},
    'run.resume': {'face': 'run.play'},
    'run.step_in': {'face': 'run.step_in'},
    'run.step_over': {'face': 'run.step_over'},
    'run.step_out': {'face': 'run.step_out'},
    'run.frame_step': {'face': 'run.frame_step'},
    'run.wait_int': {'face': 'run.wait_int'},
    'run.to_scanline': {'face': 'run.to_scanline'},
    'run.n_scanlines': {'face': 'run.n_scanlines'},
    'run.turbo_hold': {'face': 'run.turbo_hold'},
    'run.steps': {'face': 'run.steps', 'label': 'the count'},
    'run.frames': {'face': 'run.frames', 'label': 'the count'},
    'run.tstates': {'face': 'run.tstates', 'label': 'the count'},
    'ttd.record_toggle': {'face': 'ttd.record', 'face_on': 'ttd.record.on', 'state': 'toggle.ttd.record_toggle'},
    'ttd.start': {'face': 'ttd.record.on'},
    'ttd.stop': {'face': 'ttd.stop'},
    'ttd.step_back_frame': {'face': 'ttd.step_back_frame'},
    'ttd.step_forward_frame': {'face': 'ttd.step_forward_frame'},
    'ttd.step_back_instruction': {'face': 'ttd.step_back_instruction'},
    'ttd.step_forward_instruction': {'face': 'ttd.step_forward_instruction'},
    'ttd.step_back_scanline': {'face': 'ttd.step_back_scanline'},
    'ttd.reverse_step': {'face': 'ttd.reverse_step'},
    'ttd.reverse_continue': {'face': 'ttd.reverse_continue'},
    'ttd.resume_live': {'face': 'ttd.resume_live'},
    'ttd.bookmark_add': {'face': 'ttd.bookmark_add'},
    'ttd.bookmark_delete': {'face': 'ttd.bookmark_delete'},
    'ttd.bookmark_prev': {'face': 'ttd.bookmark_prev'},
    'ttd.bookmark_next': {'face': 'ttd.bookmark_next'},
    'ttd.seek': {'face': 'ttd.seek', 'params': {'frame: 0': 'ttd.go_start', 'bookmark': 'ttd.bookmark'}},
    'ttd.find_last_write': {'face': 'ttd.find_last_write'},
    'ttd.invalidate': {'face': 'ttd.invalidate'},
    'ttd.history_limit': {'face': 'ttd.history_limit', 'label': 'the limit'},
    'ttd.journal': {'face': 'ttd.journal', 'face_on': 'ttd.journal.on', 'state': 'toggle.ttd.journal'},
    'ttd.dump': {'face': 'ttd.dump'},
    'ttd.load': {'face': 'ttd.load'},
    'mach.reset': {'face': 'mach.reset'},
    'mach.nmi': {'face': 'mach.nmi'},
    'mach.turbo_toggle': {'face': 'mach.turbo', 'face_on': 'mach.turbo.on', 'state': 'toggle.mach.turbo_toggle'},
    'mach.start': {'face': 'mach.start', 'params': {'a model of another family': 'mach.chip'},
                   'label': 'the model; a software key shows its thumbnail instead'},
    'mach.cold_reset': {'face': 'mach.cold_reset'},
    'cap.record_toggle': {'face': 'cap.record', 'face_on': 'cap.record.on', 'state': 'toggle.cap.record_toggle'},
    'cap.clip_last': {'face': 'cap.clip_last'},
    'cap.screenshot': {'face': 'cap.screenshot'},
    'cap.screenshot_save': {'face': 'cap.screenshot_save'},
    'osd.keyboard': {'face': 'osd.keyboard'},
    'osd.quick_menu': {'face': 'osd.quick_menu'},
    'input.key': {'face': 'input.key', 'label': 'the key name (needs a label over the picture)'},
    'input.type': {'face': 'input.keyboard', 'label': 'the text'},
    'input.macro': {'face': 'input.macro', 'label': 'the macro name'},
    'input.joystick': {'face': 'input.joystick', 'params': {
        'up': 'input.joy.up', 'down': 'input.joy.down', 'left': 'input.joy.left', 'right': 'input.joy.right',
        'up+left': 'input.joy.up_left', 'up+right': 'input.joy.up_right', 'down+left': 'input.joy.down_left',
        'down+right': 'input.joy.down_right', 'fire': 'input.joy.fire'}},
    'input.release_all': {'face': 'input.release_all'},
    'basic.run': {'face': 'basic.run', 'params': {'LIST': 'basic.menu', 'LOAD ""': 'media.tape.load',
                                                  'CAT': 'media.disk.cat'}, 'label': 'other commands: the command'},
    'basic.inject': {'face': 'basic.inject'},
    'basic.clear': {'face': 'basic.clear'},
    'basic.mode': {'params': {'48k': 'basic.mode.48', '128k': 'basic.mode.128'}},
    'feature.toggle': {'face': 'settings.toggle', 'face_on': 'settings.toggle.on', 'state': 'toggle.feature.<name>',
                       'label': 'the feature'},
    'set.speed': {'face': 'set.speed', 'label': 'x1 x2 x4 x8 x16'},
    'set.toggle': {'face': 'settings.toggle', 'face_on': 'settings.toggle.on', 'state': 'toggle.set.toggle.<name>',
                   'params': {'turbo_mode': 'mach.turbo'}, 'label': 'the setting'},
    'set.value': {'face': 'settings.value', 'label': 'the setting and value'},
    'audio.mute_toggle': {'face': 'audio.sound', 'face_on': 'audio.muted', 'state': 'audio.mute.muted'},
    'audio.source_mute': {'face': 'audio.source', 'face_on': 'audio.source.on',
                          'state': 'toggle.audio.source_mute.<source>', 'label': 'the source (AY 1, BEEPER, ...)'},
    'audio.volume_by': {'params': {'step > 0': 'audio.volume_up', 'step < 0': 'audio.volume_down'},
                        'note': 'a dial action; on a key with a fixed step'},
    'media.open': {'face': 'media.open', 'label': 'a software key shows its thumbnail instead'},
    'media.tape': {'face': 'media.tape', 'params': {'play': 'media.tape.play', 'pause': 'media.tape.pause',
                                                    'toggle': 'media.tape.play', 'stop': 'media.tape.stop',
                                                    'rewind': 'media.tape.rewind', 'eject': 'media.tape.eject'}},
    'media.eject': {'face': 'media.eject', 'params': {'tape': 'media.tape.eject'}, 'label': 'the drive'},
    'media.protect': {'face': 'media.protect', 'face_on': 'media.protect.on',
                      'state': 'media.disk.drives[<drive>].protected', 'label': 'the drive'},
    'media.save': {'face': 'media.save', 'label': 'the drive'},
    'media.create': {'face': 'media.create', 'label': 'the drive'},
    'snapshot.quick': {'params': {'save': 'snapshot.quick_save', 'load': 'snapshot.quick_load'}},
    'snapshot.save': {'face': 'snapshot.save'},
    'video.overscan_toggle': {'face': 'video.overscan', 'face_on': 'video.overscan.on',
                              'state': 'toggle.video.overscan_toggle'},
    'video.temporal': {'face': 'video.temporal', 'face_on': 'video.temporal.on', 'state': 'toggle.video.temporal'},
    'dbg.mode_toggle': {'face': 'dbg.mode', 'face_on': 'dbg.mode.on', 'state': 'debug.mode.enabled'},
    'dbg.open': {'face': 'debug.menu'},
    'dbg.breakpoints_clear': {'face': 'dbg.breakpoints_clear'},
    'dbg.breakpoint_hits_reset': {'face': 'dbg.breakpoint_hits_reset'},
    'dbg.breakpoint_toggle_pc': {'face': 'dbg.breakpoint_toggle_pc'},
    'dbg.labels_toggle': {'face': 'dbg.labels_toggle'},
    'dbg.focus_next_pane': {'face': 'dbg.focus_next_pane'},
    'dbg.focus_prev_pane': {'face': 'dbg.focus_prev_pane'},
    'dbg.cursor_up': {'face': 'dbg.cursor_up'},
    'dbg.cursor_down': {'face': 'dbg.cursor_down'},
    'dbg.cursor_left': {'face': 'dbg.cursor_left'},
    'dbg.cursor_right': {'face': 'dbg.cursor_right'},
    'dbg.page_up': {'face': 'dbg.page_up'},
    'dbg.page_down': {'face': 'dbg.page_down'},
    'dbg.memory_follow': {'face': 'dbg.memory_follow'},
}

# Catalogue ids that get no key glyph, and why.
PRUNED = {
    'ctx.set': 'set by entering a menu that names a context; never a key of its own',
    'run.step_by': 'a dial (delta) action: dials have a strip label, not a key face',
    'ttd.jog_frames': 'a dial (delta) action',
    'ttd.jog_instructions': 'a dial (delta) action',
    'ttd.seek_by': 'a dial (delta) action',
    'ttd.scrub_back': 'a level action of the GUI (held stick / trigger), not a key press',
    'ttd.scrub_forward': 'a level action of the GUI (held stick / trigger), not a key press',
}

NAVIGATION = {
    'back': 'nav.back', 'home': 'nav.home', 'menu (generic)': 'nav.folder', 'previous page': 'nav.prev_page',
    'next page': 'nav.next_page', 'target follows focus / pinned': ['nav.target', 'nav.target.on'],
    'instances': 'nav.instances', 'deck settings': 'deck.menu', 'brightness': 'deck.brightness',
    'menu: machines': 'mach.start', 'menu: software': 'media.software', 'menu: tape': 'media.tape',
    'menu: disk': 'media.disk', 'menu: time travel': 'ttd.menu', 'menu: time code': 'ttd.timecode',
    'menu: bookmarks': 'ttd.bookmark', 'menu: debugger': 'debug.menu', 'menu: disassembly': 'debug.disasm',
    'menu: memory': 'debug.memory', 'menu: registers': 'debug.registers', 'menu: stack': 'debug.stack',
    'menu: speed': 'set.speed', 'menu: audio': 'audio.sound', 'menu: video': 'video.menu',
    'menu: capture': 'cap.screenshot', 'menu: joystick': 'input.joystick', 'menu: keyboard': 'input.keyboard',
    'menu: BASIC': 'basic.menu', 'menu: settings': 'settings.menu',
}


def check():
    ids = [g[0] for g in GLYPHS]
    assert len(ids) == len(set(ids)), 'duplicate glyph ids'
    groups = {g for g, _ in GROUPS}
    assert all(g[1] in groups for g in GLYPHS)
    used = set()
    for a in ACTIONS.values():
        used.update(v for v in (a.get('face'), a.get('face_on')) if v)
        used.update(a.get('params', {}).values())
    for v in NAVIGATION.values():
        used.update(v if isinstance(v, list) else [v])
    missing = sorted(used - set(ids))
    assert not missing, 'faces without a glyph: %s' % missing
    return sorted(set(ids) - used)


def main():
    unused = check()
    doc = {
        'format': 'streamdeck-glyphs', 'version': 1, 'generated_by': 'tools/icons/streamdeck_glyphs.py',
        'groups': [{'id': g, 'title': t} for g, t in GROUPS],
        'glyphs': [dict({'id': i, 'group': g, 'title': t, 'draw': d},
                        **({'variant_of': i[:-3]} if i.endswith('.on') else {})) for i, g, t, d in GLYPHS],
        'actions': ACTIONS, 'pruned': PRUNED, 'navigation': NAVIGATION,
    }
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'w', newline='\n') as f:
        json.dump(doc, f, indent=1, ensure_ascii=False)
        f.write('\n')
    print('%d glyphs, %d actions with faces, %d pruned -> %s' % (len(GLYPHS), len(ACTIONS), len(PRUNED),
                                                                os.path.relpath(OUT, ROOT_DIR)))
    if unused:
        print('glyphs used by no action or menu (states / status): %s' % ', '.join(unused))


if __name__ == '__main__':
    main()
