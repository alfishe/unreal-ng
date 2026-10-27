#!/usr/bin/env python3
"""The sound mixer and the sound settings (docs/emulator/design/audio/mixer-channels-and-settings.md): the
`audio_mixer` tool's text, its rows and channels (<source>.<channel>), the levels, the settings; the same row over
REST, the WebSocket `call` and the `audio.mixer` topic. The machine is the run's PENTAGON (the shipped config: a
TurboSound FM in the AY socket), its master muted by the session."""

import json
import sys
import urllib.error
import urllib.parse
import urllib.request

from mcplib import COMPANION_NARROW_TOKEN, WebSocket, expect, main


def webapi_port(s):
    return urllib.parse.urlparse(s.url).port - 200


def http(s, method, path, body=None):
    request = urllib.request.Request(f'http://127.0.0.1:{webapi_port(s)}{path}', method=method,
                                     data=json.dumps(body).encode() if body is not None else None,
                                     headers={'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return response.status, json.loads(response.read().decode() or '{}')
    except urllib.error.HTTPError as error:
        raw = error.read().decode()
        return error.code, json.loads(raw) if raw else {}


def emulator_id(s):
    listing = s.call('emulator_manage', {'action': 'list'})
    return listing.structured['emulators'][0]['id']


def test_the_rows_are_in_the_text_and_the_master_is_muted(s):
    r = s.call('audio_mixer')
    expect('master: muted' in r.text, 'the session muted the master; the text says so', r.text)
    expect('beeper (' in r.text and 'ay1 (' in r.text, 'the rows by key and name', r.text)
    expect('[3 channels]' in r.text, 'a row with channels says how many', r.text)
    expect('ay1.b' not in r.text, 'the rows alone by default (no channel listed)', r.text)


def test_a_row_shows_its_channels_and_a_channel_takes_solo(s):
    r = s.call('audio_mixer', {'action': 'get', 'source': 'ay1'})
    for key in ('ay1.a', 'ay1.b', 'ay1.c'):
        expect(key in r.text, f'{key} listed', r.text)
    r = s.call('audio_mixer', {'action': 'set', 'source': 'ay1.b', 'solo': True})
    expect('ay1.b' in r.text and 'solo' in r.text, 'the change in the text', r.text)
    r = s.call('audio_mixer', {'action': 'source', 'source': 'ay1.a'})
    expect('silent' in r.text, 'B soloed: A of the same chip is silent', r.text)
    s.call('audio_mixer', {'action': 'set', 'source': 'ay1.b', 'solo': False})
    r = s.call('audio_mixer', {'action': 'set', 'source': 'ay1.z', 'muted': True}, ok=False)
    expect(r.is_error and 'a, b, c' in r.text, 'an unknown channel names the ones there are', r.text)


def test_mute_and_unmute_shortcuts(s):
    r = s.call('audio_mixer', {'action': 'unmute'})
    expect(not r.is_error, 'unmute the master', r.text)
    r = s.call('audio_mixer')
    expect('master: muted' not in r.text, 'unmute reached the master', r.text)
    r = s.call('audio_mixer', {'action': 'mute'})
    r = s.call('audio_mixer')
    expect('master: muted' in r.text, 'mute without a source mutes the master', r.text)


def test_levels_and_settings(s):
    r = s.call('audio_mixer', {'action': 'levels'})
    expect('fm1.1' in r.text or 'ay1.c' in r.text, 'every channel in the levels', r.text)
    r = s.call('audio_mixer', {'action': 'settings'})
    for name in ('ay_stereo', 'ay_chip', 'fm_trim_db', 'neogs_stereo', 'fir_filter'):
        expect(f'{name} =' in r.text, f'{name} in the settings', r.text)
    r = s.call('audio_mixer', {'action': 'set_setting', 'name': 'ay_stereo', 'value': 'acb'})
    expect('ay_stereo = acb' in r.text, 'the setting changed', r.text)
    s.call('audio_mixer', {'action': 'set_setting', 'name': 'ay_stereo', 'value': 'abc'})
    r = s.call('audio_mixer', {'action': 'set_setting', 'name': 'ay_stereo', 'value': 'cab'}, ok=False)
    expect(r.is_error and '400' in r.text, 'a bad value is refused', r.text)


def test_rest_the_socket_call_and_the_topic_agree(s):
    eid = emulator_id(s)
    status, body = http(s, 'PUT', f'/api/v1/emulator/{eid}/audio/mixer/ay2.c', {'muted': True})
    expect(status == 200, 'PUT a channel', body)
    status, row = http(s, 'GET', f'/api/v1/emulator/{eid}/audio/mixer/ay2.c')
    expect(status == 200 and row['muted'] is True and row['source'] == 'ay2.c', 'GET the channel', row)
    status, levels = http(s, 'GET', f'/api/v1/emulator/{eid}/audio/levels')
    expect(status == 200 and any(d['source'] == 'ay2' for d in levels['devices']), 'the levels', levels)
    ws = WebSocket(webapi_port(s), token=COMPANION_NARROW_TOKEN)
    try:
        ws.send({'op': 'call', 'rid': 1, 'method': 'GET', 'path': f'/api/v1/emulator/{eid}/audio/mixer/ay2.c'})
        result = ws.until(lambda m: m.get('op') == 'result' and m.get('rid') == 1, 'the call')
        expect(result['status'] == 200 and result['body']['muted'] is True, 'the socket call answers as REST',
               result)
        ws.send({'op': 'subscribe', 'topics': ['audio.mixer'], 'max_hz': 20})
        state = ws.until(lambda m: m.get('t') == 'audio.mixer' and m.get('op') == 'state', 'the topic')
        ay2 = [d for d in state['value']['devices'] if d['source'] == 'ay2'][0]
        expect(ay2['channels'][2]['muted'] is True, 'the topic carries the channel state', ay2)
        http(s, 'PUT', f'/api/v1/emulator/{eid}/audio/mixer/ay2.c', {'muted': False})
        state = ws.until(lambda m: m.get('t') == 'audio.mixer' and
                         [d for d in m['value']['devices'] if d['source'] == 'ay2'][0]['channels'][2]['muted'] is False,
                         'the change pushed')
    finally:
        ws.close()


if __name__ == '__main__':
    sys.exit(main(globals()))
