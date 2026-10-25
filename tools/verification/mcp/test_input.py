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


if __name__ == '__main__':
    sys.exit(main(globals()))
