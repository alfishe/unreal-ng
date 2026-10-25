#!/usr/bin/env python3
"""type_input / mouse_input / joystick_input: typed text reaches BASIC, key names, device status in the text"""

import sys

from mcplib import expect, main


def setup(s):
    s.fresh_machine('48K')  # straight to the BASIC prompt
    s.call('control_execution', {'action': 'resume'})
    s.call('control_execution', {'action': 'run_frames', 'frames': 150})


def test_typed_line_reaches_basic(s):
    s.call('type_input', {'action': 'type', 'text': '10 REM MCP\n'})
    s.call('control_execution', {'action': 'run_frames', 'frames': 150})
    r = s.call('inspect_state', {'aspects': ['screen_ocr']})
    expect('MCP' in r.text, 'the line on the screen', r.text[:600])


def test_list_keys_and_status(s):
    r = s.call('type_input', {'action': 'list_keys'})
    expect('enter' in r.text.lower(), 'key names in the text', r.text[:300])
    r = s.call('type_input', {'action': 'status'})
    expect('=' in r.text, 'keyboard fields in the text', r.text)


def test_mouse(s):
    r = s.call('mouse_input', {'action': 'move', 'dx': 10, 'dy': -5})
    expect(not r.is_error, 'move', r.text)
    r = s.call('mouse_input', {'action': 'status'})
    expect('=' in r.text, 'mouse fields in the text', r.text)
    r = s.call('mouse_input', {'action': 'move', 'x': 10}, ok=False)
    expect(r.is_error and 'dx' in r.text, 'a wrong argument names the right one', r.text)


def test_joystick(s):
    s.call('joystick_input', {'action': 'press', 'buttons': 'up+fire'})
    r = s.call('joystick_input', {'action': 'status'})
    expect('=' in r.text, 'joystick fields in the text', r.text)
    r = s.call('joystick_input', {'action': 'get'})
    expect(not r.is_error and '=' in r.text, 'get reads the status too', r.text)
    s.call('joystick_input', {'action': 'release', 'buttons': 'up+fire'})
    r = s.call('joystick_input', {'action': 'press'}, ok=False)
    expect(r.is_error and 'buttons' in r.text, 'press needs buttons', r.text)


def test_kempston_card_on_48k(s):
    """Last: the plug restarts the machine. The 48K decodes no #1F; the answer names the card, plugging it wires the
    stick, and BASIC's IN 31 reads it"""
    r = s.call('joystick_input', {'action': 'status'})
    expect('slots_plug slot=edge.1 card=kempston' in r.text, 'the unwired answer names the slot card', r.text)
    s.call('emulator_manage', {'action': 'slots_plug', 'slot': 'edge.1', 'card': 'kempston'})
    s.call('control_execution', {'action': 'resume'})
    s.call('control_execution', {'action': 'run_frames', 'frames': 150})
    r = s.call('joystick_input', {'action': 'press', 'buttons': 'fire'})
    expect('no Kempston joystick interface' not in r.text, 'wired after the plug', r.text)
    s.call('control_execution', {'action': 'resume'})  # tokenized typing needs a running machine
    s.call('type_input', {'action': 'type', 'text': 'PRINT IN 31', 'tokenized': True})
    s.call('type_input', {'action': 'tap', 'key': 'enter'})
    s.call('control_execution', {'action': 'run_frames', 'frames': 150})
    r = s.call('inspect_state', {'aspects': ['screen_ocr']})
    lines = [line.strip() for line in (r.structured or {}).get('screen_ocr', {}).get('lines', []) if line.strip()]
    expect(lines[:1] == ['16'], 'IN 31 reads fire (16)', lines)
    s.call('joystick_input', {'action': 'release', 'buttons': 'fire'})


if __name__ == '__main__':
    sys.exit(main(globals()))
