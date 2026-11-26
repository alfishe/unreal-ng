#!/usr/bin/env python3
"""type_input / mouse_input / joystick_input: typed text reaches BASIC, key names, device status in the text"""

import re
import socket
import sys
import time
import urllib.parse

from mcplib import expect, main


def cli(s, command):
    """The CLI's answer to `command` on the machine of the session (the CLI port is the MCP port - 100). The CLI is
    another client: MCP lets run control go first and takes it back after (over the CLI's implicit session)"""
    s.call('control_execution', {'action': 'release_control'}, ok=False)
    emu = s.call('emulator_manage', {'action': 'list'}).structured['emulators'][0]['id']
    port = urllib.parse.urlparse(s.url).port - 100
    with socket.create_connection(('127.0.0.1', port), timeout=30) as sock:
        def read_until_prompt():
            data = b''
            deadline = time.time() + 30
            while not re.search(rb'\n> (\x1b\[K)?$', data) and time.time() < deadline:
                chunk = sock.recv(65536)
                if not chunk:
                    break
                data += chunk
            return data.decode(errors='replace')
        read_until_prompt()
        sock.sendall(f'select {emu}\n'.encode())
        read_until_prompt()
        sock.sendall((command + '\n').encode())
        answer = re.sub(r'\n> (\x1b\[K)?$', '', read_until_prompt()).replace('\r', '')
    s.call('control_execution', {'action': 'take_control'}, ok=False)
    return answer


def setup(s):
    s.fresh_machine('48K')  # straight to the BASIC prompt
    s.call('control_execution', {'action': 'resume'})
    s.call('control_execution', {'action': 'run_frames', 'frames': 150})


def test_typed_line_reaches_basic(s):
    s.call('type_input', {'action': 'type', 'text': '10 REM MCP\n'})
    s.call('control_execution', {'action': 'run_frames', 'frames': 150})
    r = s.call('inspect_state', {'aspects': ['screen_ocr']})
    expect('MCP' in r.text, 'the line on the screen', r.text[:600])


def test_key_chord_on_cli_lua_python(s):
    """`key`: names joined with '+' go down together on the script planes too (caps+letter: a capital in a REM)"""
    def run(code, plane):
        r = s.call('invoke_api', {'method': 'POST', 'path': f'/api/v1/{plane}/exec', 'body': {'code': code}}, ok=False)
        return ((r.structured or {}).get('body') or {}).get('output', '') + r.text

    s.call('type_input', {'action': 'type', 'text': '20 REM 7'})
    s.call('control_execution', {'action': 'run_frames', 'frames': 100})
    answer = cli(s, 'key tap caps+q')
    expect('CAPS' in answer.upper() and '+' in answer and 'Error' not in answer, 'CLI key tap caps+q', answer)
    s.call('control_execution', {'action': 'run_frames', 'frames': 30})
    expected = '7Q'
    output = run('print("lua-chord", key_tap("caps+w"), key_tap("caps+nosuchkey"), key_tap("caps w"))', 'lua')
    expect(re.search(r'lua-chord\s+true\s+false\s+false', output), 'Lua key_tap: a chord, refused names', output)
    s.call('control_execution', {'action': 'run_frames', 'frames': 30})
    expected += 'W'
    output = run('print("lua-combo", key_combo({"caps", "r"}))', 'lua')  # a table of names (it crashed the emulator)
    expect(re.search(r'lua-combo\s+true', output), 'Lua key_combo: a table of names', output)
    s.call('control_execution', {'action': 'run_frames', 'frames': 30})
    expected += 'R'
    output = run('import unreal_emulator as ue\nemu = ue.emu_get_selected()\n'
                 'print("py-chord", emu.key_tap("caps+z"), emu.key_tap("caps+nosuchkey"))\n', 'python')
    if 'Python automation is not available' not in output:
        expect('py-chord True False' in output, 'Python key_tap: a chord, a refused name', output)
        s.call('control_execution', {'action': 'run_frames', 'frames': 30})
        expected += 'Z'
    r = s.call('inspect_state', {'aspects': ['screen_ocr']})
    expect(expected in r.text, f'the capitals on the screen ({expected})', r.text[:600])

    answer = cli(s, 'key press w+nosuchkey')
    expect("Unknown key 'nosuchkey'" in answer and "joined with '+'" in answer, 'CLI: the unknown name of a chord',
           answer)
    for command in ('key press w+o', 'key release w+o'):
        answer = cli(s, command)
        expect('W+O' in answer.upper() and 'Error' not in answer, f'CLI {command}', answer)
    s.call('type_input', {'action': 'tap', 'key': 'enter'})
    s.call('control_execution', {'action': 'run_frames', 'frames': 30})
    s.call('control_execution', {'action': 'release_control'}, ok=False)


def test_list_keys_and_status(s):
    r = s.call('type_input', {'action': 'list_keys'})
    expect('enter' in r.text.lower(), 'key names in the text', r.text[:300])
    r = s.call('type_input', {'action': 'status'})
    expect('=' in r.text, 'keyboard fields in the text', r.text)


def test_key_set_holds_exactly_the_given_keys(s):
    """type_input set / key set / key_set: the whole held-key state in one call. Two letter keys held together type
    nothing (the ROM takes no key), so the planes are checked on their answers; then the machine shows the state:
    Symbol Shift held, set [p] - PRINT appears (Symbol Shift + P would have typed a quote)"""
    def run(code, plane):
        r = s.call('invoke_api', {'method': 'POST', 'path': f'/api/v1/{plane}/exec', 'body': {'code': code}}, ok=False)
        return ((r.structured or {}).get('body') or {}).get('output', '') + r.text

    r = s.call('type_input', {'action': 'set', 'keys': ['q', 'w']})
    expect('Keys held: q, w' in r.text and r.structured.get('keys') == ['q', 'w'], 'set names the held keys', r.text)
    r = s.call('type_input', {'action': 'set', 'keys': ['w', 'nosuchkey']}, ok=False)
    expect(r.is_error and "Unknown key 'nosuchkey'" in r.text and 'Nothing changed' in r.text,
           'an unknown name is refused, nothing changed', r.text)
    r = s.call('type_input', {'action': 'set'}, ok=False)
    expect(r.is_error and 'keys' in r.text, 'set needs keys', r.text)

    answer = cli(s, 'key set q+p')
    expect('Held: q+p' in answer and 'Error' not in answer, 'CLI key set q+p', answer)
    answer = cli(s, 'key set q nosuchkey')
    expect("Unknown key 'nosuchkey'" in answer and 'Nothing changed' in answer, 'CLI key set: an unknown name', answer)
    answer = cli(s, 'key set none')
    expect('Held: none' in answer, 'CLI key set none releases all', answer)
    output = run('print("lua-set", key_set({"w", "o"}), key_set({"w", "nosuchkey"}), key_set({}))', 'lua')
    expect(re.search(r'lua-set\s+true\s+false\s+true', output), 'Lua key_set: a state, a refused name, none', output)
    output = run('import unreal_emulator as ue\nemu = ue.emu_get_selected()\n'
                 'print("py-set", emu.key_set(["w", "o"]), emu.key_set(["w", "nosuchkey"]), emu.key_set([]))\n', 'python')
    if 'Python automation is not available' not in output:
        expect('py-set True False True' in output, 'Python key_set: a state, a refused name, none', output)

    s.call('type_input', {'action': 'press', 'key': 'ss'})
    r = s.call('type_input', {'action': 'set', 'keys': ['p']})
    expect('Keys held: p' in r.text, 'set [p]', r.text)
    s.call('control_execution', {'action': 'run_frames', 'frames': 20})
    r = s.call('type_input', {'action': 'set', 'keys': []})
    expect('released' in r.text and r.structured.get('count') == 0, 'set [] releases all', r.text)
    s.call('control_execution', {'action': 'run_frames', 'frames': 10})
    r = s.call('inspect_state', {'aspects': ['screen_ocr']})
    expect('PRINT' in r.text and '"' not in r.text.split('PRINT')[-1][:4], 'P alone was held: PRINT, not a quote',
           r.text[:600])
    s.call('type_input', {'action': 'tap', 'key': 'enter'})  # the line ends in a report, the editor is clean again
    s.call('control_execution', {'action': 'run_frames', 'frames': 30})
    s.call('control_execution', {'action': 'release_control'}, ok=False)


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
    expect('Kempston joystick port #1F: not decoded on this machine' in r.text and 'port_decoded=false' in r.text,
           'the status says #1F is not decoded on the 48K', r.text)
    expect('Kempston mouse: on' in r.text and 'kempston_mouse_feature=true' in r.text,
           'the status says the Kempston mouse is on', r.text)
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
    r = s.call('joystick_input', {'action': 'status'})
    expect('decoded (the plugged kempston card)' in r.text and 'port_source=card' in r.text,
           'the status names the card as the decoder of #1F', r.text)
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
