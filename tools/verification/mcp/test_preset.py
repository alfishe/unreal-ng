#!/usr/bin/env python3
"""emulator_manage preset: a preset machine in one call (RE API efficiency V3). A 48K with a Kempston card and the Kempston
mouse off: the answer's text lists the effective settings, the stick works at once (no polling after the plug), the
mixer is muted, the machine is paused, run control is held; refusals name the valid values and leave no instance; the
same verb over REST (`POST /api/v1/emulator/preset`)."""

import json
import re
import socket
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

from mcplib import expect, main


def webapi_port(s):
    return urllib.parse.urlparse(s.url).port - 200


def http(s, method, path, body=None, headers=None):
    request = urllib.request.Request(f'http://127.0.0.1:{webapi_port(s)}{path}', method=method,
                                     data=json.dumps(body).encode() if body is not None else None,
                                     headers={'Content-Type': 'application/json', **(headers or {})})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return response.status, json.loads(response.read().decode() or '{}')
    except urllib.error.HTTPError as error:
        raw = error.read().decode()
        return error.code, json.loads(raw) if raw else {}


def instances(s):
    return [e['id'] for e in s.call('emulator_manage', {'action': 'list'}).structured['emulators']]


def setup(s):
    s.destroy_all()  # the preset verb makes the machine


def test_a_machine_preset_is_ready_in_one_call(s):
    r = s.call('emulator_manage', {'action': 'preset', 'model': '48K', 'cards': ['edge.1=kempston'],
                                   'features': {'kempstonmouse': False}, 'replace': True})
    preset = r.structured
    emu = preset['id']
    # The text alone tells the machine (an agent may see only the text)
    expect(re.search(r'Preset machine \S+ ready: 48K \(48 KB\), RAM zero, paused at frame \d+ t \d+\.', r.text),
           'the machine, its RAM and where it is paused', r.text)
    expect(emu in r.text and 'edge.1 = kempston' in r.text.split('Cards: ')[1].splitlines()[0],
           'the card in its slot', r.text)
    expect('kempstonmouse off (asked)' in r.text and 'fasttape off' in r.text and 'turbotape off' in r.text,
           'the features not at their default', r.text)
    expect('master muted' in r.text and 'Fast tape: off' in r.text, 'sound and tape', r.text)
    expect('Run control: held by you (mcp, session ' in r.text and 'release_control' in r.text, 'the session', r.text)
    expect(preset['paused'] is True and preset['muted'] is True and preset['fast_tape'] is False and preset['ram'] == 'zero',
           'the structured settings', preset)
    expect({'slot': 'edge.1', 'card': 'kempston', 'asked': True}.items() <= preset['cards'][0].items()
           if len(preset['cards']) == 1 else any(c['slot'] == 'edge.1' and c['card'] == 'kempston' for c in preset['cards']),
           'cards[]', preset['cards'])
    expect(preset['features']['asked'] == {'kempstonmouse': False}, 'features.asked', preset['features'])
    expect(preset['position'] == {'frame': 0, 't': 3, 'pc': 0}, 'paused at power-on: the reset only', preset['position'])
    expect(instances(s) == [emu], 'the only instance', instances(s))

    # The stick works at once: no restart to wait for, no "fit the card" hint
    r = s.call('joystick_input', {'action': 'press', 'buttons': 'up+fire'})
    r = s.call('joystick_input', {'action': 'status'})
    expect('slots_plug' not in r.text and 'up' in r.text.lower() and 'fire' in r.text.lower(),
           'the Kempston stick is wired and holds up+fire', r.text)
    s.call('joystick_input', {'action': 'release', 'buttons': 'up+fire'})

    # Muted, paused, the mouse off, control held by this client (no "opened implicitly" advice on a changing call)
    r = s.call('audio_mixer')
    expect('master: muted' in r.text, 'the mixer master is muted', r.text)
    status, body = http(s, 'GET', f'/api/v1/emulator/{emu}')
    expect(status == 200 and body['is_paused'] is True and body['is_running'] is True, 'started and paused', body)
    status, body = http(s, 'GET', f'/api/v1/emulator/{emu}/feature/kempstonmouse')
    expect(status == 200 and body.get('enabled') is False, 'kempstonmouse is off', body)
    status, body = http(s, 'GET', f'/api/v1/emulator/{emu}/control/session')
    expect(body['held'] is True and body['surface'] == 'mcp' and body['implicit'] is False,
           'an explicit MCP session holds the machine', body)
    r = s.call('control_execution', {'action': 'run_frames', 'frames': 2})
    expect('implicitly' not in r.text, 'the run goes on in the session the preset call took', r.text)
    status, body = http(s, 'POST', f'/api/v1/emulator/{emu}/resume')
    expect(status == 409, 'another client is refused: the machine is held', body)


def test_refusals_name_the_valid_values_and_leave_no_instance(s):
    before = instances(s)
    r = s.call('emulator_manage', {'action': 'preset', 'model': '48K', 'features': {'kempstonmice': False}}, ok=False)
    expect(r.is_error and "unknown feature 'kempstonmice'" in r.text and 'kempstonmouse' in r.text,
           'an unknown feature names the features', r.text)
    r = s.call('emulator_manage', {'action': 'preset', 'model': '48K', 'cards': ['edge.1=kempstone']}, ok=False)
    expect(r.is_error and "unknown card 'kempstone'" in r.text and 'kempston,' in r.text,
           'an unknown card names the cards', r.text)
    r = s.call('emulator_manage', {'action': 'preset', 'model': '48Q'}, ok=False)
    expect(r.is_error and "unknown model '48Q'" in r.text and 'PENTAGON' in r.text, 'an unknown model', r.text)
    r = s.call('emulator_manage', {'action': 'preset', 'model': '48K', 'ram': 'ones'}, ok=False)
    expect(r.is_error and 'zero or random' in r.text, 'a bad ram value', r.text)
    r = s.call('emulator_manage', {'action': 'preset', 'model': '48K', 'cards': ['nobus.1=kempston']}, ok=False)
    expect(r.is_error and 'HTTP 409' in r.text and 'The buses of 48K' in r.text and 'Nothing was created' in r.text,
           'a slot the machine does not have: the plan refuses it', r.text)
    expect(instances(s) == before, 'no instance was left behind', (before, instances(s)))


def test_beside_the_others_by_default_and_replace(s):
    first = s.call('emulator_manage', {'action': 'preset', 'model': '48K', 'replace': True}).structured['id']
    r = s.call('emulator_manage', {'action': 'preset', 'model': '48K', 'paused': False, 'muted': True})
    second = r.structured['id']
    expect(sorted(instances(s)) == sorted([first, second]), 'without replace the machine is created beside', r.text)
    expect('2 instances exist' in r.text and ', running.' in r.text, 'the text says so', r.text)
    r = s.call('emulator_manage', {'action': 'preset', 'model': '48K', 'replace': True})
    expect(instances(s) == [r.structured['id']] and 'Replaced 2 instance(s)' in r.text, 'replace', r.text)


def cli_line(s, command):
    """The CLI's answer to one command on a fresh connection (the CLI port is the MCP port - 100)"""
    with socket.create_connection(('127.0.0.1', urllib.parse.urlparse(s.url).port - 100), timeout=30) as sock:
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
        sock.sendall((command + '\n').encode())
        return read_until_prompt().replace('\r', '')


def test_selected_only_when_asked(s):
    first = s.call('emulator_manage', {'action': 'preset', 'model': '48K', 'replace': True, 'select': True})
    expect(first.structured['selected'] is True, 'select: the new machine is the selected one', first.text)
    r = s.call('emulator_manage', {'action': 'preset', 'model': '48K'})
    expect(r.structured['selected'] is False and 'not the selected one' in r.text,
           'by default the selection is left alone', r.text)
    s.call('control_execution', {'action': 'release_control'}, ok=False)


def test_the_same_verb_on_cli_lua_python(s):
    s.call('emulator_manage', {'action': 'preset', 'model': '48K', 'replace': True, 'take_control': False})
    before = len(instances(s))
    answer = cli_line(s, 'preset 48K card=edge.1=kempston feature=kempstonmouse=off take_control=off')
    expect('Preset machine' in answer and 'edge.1 = kempston' in answer and 'kempstonmouse off (asked)' in answer
           and len(instances(s)) == before + 1, 'CLI preset', answer)
    answer = cli_line(s, 'preset 48K ram=sometimes')
    expect('Error' in answer and 'zero' in answer and len(instances(s)) == before + 1,
           'CLI preset: a bad value names the valid ones, no instance', answer)

    def run(code, plane):
        r = s.call('invoke_api', {'method': 'POST', 'path': f'/api/v1/{plane}/exec', 'body': {'code': code}}, ok=False)
        return ((r.structured or {}).get('body') or {}).get('output', '') + r.text

    output = run('local p, why = machine_preset{model = "48K", cards = {"edge.1=kempston"}, '
                 'features = {kempstonmouse = false}, take_control = false} '
                 'print("lua-preset", p and p.model, p and p.paused, p and p.selected, why) '
                 'local bad, reason = machine_preset{model = "48K", ram = "sometimes"} '
                 'print("lua-refused", bad == nil, reason)', 'lua')
    expect(re.search(r'lua-preset\s+48K\s+true\s+false', output) and re.search(r'lua-refused\s+true\s+\S', output)
           and len(instances(s)) == before + 2, 'Lua machine_preset: a table, or nil and the reason', output)
    output = run('import unreal_emulator as ue\n'
                 'p = ue.machine_preset("48K", cards=["edge.1=kempston"], features={"kempstonmouse": False}, '
                 'take_control=False)\n'
                 'print("py-preset", p["model"], p["paused"], p["selected"])\n'
                 'try:\n    ue.machine_preset("48K", ram="sometimes")\nexcept ValueError as e:\n'
                 '    print("py-refused", "zero" in str(e))\n', 'python')
    if 'Python automation is not available' not in output:
        expect('py-preset 48K True False' in output and 'py-refused True' in output
               and len(instances(s)) == before + 3, 'Python machine_preset: a dict, ValueError', output)
    s.call('control_execution', {'action': 'release_control'}, ok=False)
    s.destroy_all()


def test_the_same_verb_over_rest(s):
    s.call('control_execution', {'action': 'release_control'}, ok=False)
    status, preset = http(s, 'POST', '/api/v1/emulator/preset',
                       {'model': '48K', 'cards': [{'slot': 'edge.1', 'card': 'kempston'}], 'fast_tape': True,
                        'replace': True, 'client': 'test_preset'})
    expect(status == 201 and preset['ok'] is True and preset['status'] == 'created', 'created', preset)
    expect(preset['text'].startswith('Preset machine ') and 'edge.1 = kempston' in preset['text'], 'the text', preset)
    expect(preset['fast_tape'] is True and 'fasttape' not in preset['features']['not_default'], 'fast_tape: true', preset)
    control = preset['control']
    expect(control['held'] and control['header'] == 'X-Unreal-Session' and control['client'] == 'test_preset',
           'the session of the REST client', control)
    emu = preset['id']
    status, body = http(s, 'POST', f'/api/v1/emulator/{emu}/steps', {'count': 1},
                        headers={'X-Unreal-Session': control['session']})
    expect(status == 200, 'the session drives the machine', body)
    status, body = http(s, 'POST', '/api/v1/emulator/preset', {'model': '48K', 'mute': True})
    expect(status == 400 and "unknown argument 'mute'" in body['message'] and 'muted' in body['valid'],
           'an unknown argument names the arguments', body)
    status, body = http(s, 'DELETE', f'/api/v1/emulator/{emu}/control/session',
                        headers={'X-Unreal-Session': control['session']})
    expect(status == 200, 'released', body)


if __name__ == '__main__':
    sys.exit(main(globals()))
