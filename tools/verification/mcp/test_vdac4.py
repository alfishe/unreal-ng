#!/usr/bin/env python3
"""vdac4_state: the TS-Conf VDAC4 board (Tang FT812 core + ESP32-S3) as an agent reads it. A machine without the board
answers a tool error saying why; the TSL-VDAC4 model has it: the Tang's personality, the ESP's phase, the app and the
audio in the text, the same values as REST GET /vdac4/state and its WebSocket call, the log line count obeyed. The
board's behaviour is asserted by the core tests (vdac4*_test.cpp); this checks the live surface."""

import json
import sys
import urllib.error
import urllib.parse
import urllib.request

from mcplib import WebSocket, expect, main


def webapi_port(s):
    return urllib.parse.urlparse(s.url).port - 200


def http_get(s, path):
    try:
        with urllib.request.urlopen(f'http://127.0.0.1:{webapi_port(s)}{path}', timeout=30) as response:
            return response.status, json.loads(response.read().decode() or '{}')
    except urllib.error.HTTPError as error:
        raw = error.read().decode()
        return error.code, json.loads(raw) if raw else {}


def emulator_id(s):
    return s.call('emulator_manage', {'action': 'list'}).structured['emulators'][0]['id']


def test_no_board_is_a_clear_error(s):
    s.fresh_machine('PENTAGON')
    r = s.call('vdac4_state', ok=False)
    expect(r.is_error, 'a machine without the board is a tool error', r.text)
    expect('No VDAC4 Board' in r.text and 'TSL-VDAC4' in r.text, 'the error says why and how to get one', r.text)
    status, body = http_get(s, f'/api/v1/emulator/{emulator_id(s)}/vdac4/state')
    expect(status == 409 and body.get('error') == 'No VDAC4 Board', 'REST answers 409 the same', (status, body))


def test_board_state_in_the_text_and_same_as_rest(s):
    s.fresh_machine('TSL-VDAC4')
    r = s.call('vdac4_state', {'log': 5})
    expect(r.text.startswith('VDAC4 board: Tang personality '), 'the personality heads the text', r.text)
    st = r.structured
    for key in ('tang', 'esp', 'app', 'link', 'packets', 'audio', 'log'):
        expect(key in st, f'structuredContent has {key}', st)
    expect('ESP: ' + st['esp']['phase'] in r.text, 'the ESP phase in the text', r.text)
    expect('\nApp: ' in r.text and '\nAudio: ' in r.text and '\nLink: ' in r.text, 'app, audio and link lines', r.text)
    if st['app']['running']:
        expect(st['app']['name'] in r.text, 'the running app by name', r.text)
    expect(len(st['log']) <= 5, 'log=5 gives at most 5 lines', st['log'])

    eid = emulator_id(s)
    status, body = http_get(s, f'/api/v1/emulator/{eid}/vdac4/state?log=5')
    expect(status == 200 and body['tang']['personality'] == st['tang']['personality'] and
           body['esp']['phase'] == st['esp']['phase'], 'REST answers the same (paused machine)', body)

    ws = WebSocket(webapi_port(s))
    try:
        ws.send({'op': 'call', 'rid': 1, 'method': 'GET', 'path': f'/api/v1/emulator/{eid}/vdac4/state?log=0'})
        reply = ws.until(lambda m: m.get('op') == 'result' and m.get('rid') == 1, 'the call')
    finally:
        ws.close()
    expect(reply['status'] == 200 and reply['body']['tang'] == body['tang'] and reply['body']['log'] == [],
           'the socket call answers as REST, log=0 none', reply)

    r = s.call('vdac4_state', {'log': -1}, ok=False)
    expect(r.is_error and 'log' in r.text, 'a negative log count is refused', r.text)


if __name__ == '__main__':
    sys.exit(main(globals()))
