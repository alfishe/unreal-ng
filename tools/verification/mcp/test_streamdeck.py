#!/usr/bin/env python3
"""The Stream Deck daemon as unreal-qt supervises it (docs/inprogress/2026-10-19-streamdeck-integration/daemon.md §7):
the `streamdeck` tool's text and its routes. mcplib.launch keeps the launched unreal-qt from starting a daemon
(UNREAL_STREAMDECK_SUPERVISOR=off: the user's setting is not touched): the status says off, the verbs that need the
daemon say it does not run, nothing is changed. The same status over REST, the WebSocket `call`, the topic
`streamdeck.status` and the CLI. The supervision itself (start on a deck, crash, hang, leases, pairing) is in the
core tests and the SD7 live check."""

import json
import socket
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

from mcplib import COMPANION_NARROW_TOKEN, WebSocket, expect, main


def ports(s):
    mcp = urllib.parse.urlparse(s.url).port
    return mcp - 200, mcp - 100  # WebAPI, CLI


def http(s, method, path, body=None):
    request = urllib.request.Request(f'http://127.0.0.1:{ports(s)[0]}{path}', method=method,
                                     data=json.dumps(body).encode() if body is not None else None,
                                     headers={'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return response.status, json.loads(response.read().decode() or '{}')
    except urllib.error.HTTPError as error:
        raw = error.read().decode()
        return error.code, json.loads(raw) if raw else {}


def test_the_status_text_says_what_the_daemon_is(s):
    r = s.call('streamdeck')
    expect('Stream Deck daemon off' in r.text, 'the state in the text (supervision off for this run)', r.text)
    expect('mode off' in r.text and 'automation 127.0.0.1:' in r.text, 'the mode and the port in the text', r.text)
    expect(r.structured['status']['attached'] is True, 'unreal-qt supervises (attached)', r.structured)
    expect(r.structured['status']['state'] == 'off', 'and starts nothing', r.structured)


def test_settings_read_and_the_udev_rule(s):
    r = s.call('streamdeck', {'action': 'settings'})
    expect('mode off' in r.text and 'activity ' in r.text, 'the settings in the text', r.text)
    r = s.call('streamdeck', {'action': 'udev'})
    expect('ATTRS{idVendor}=="0fd9"' in r.text and 'uaccess' in r.text, 'the rule in the text', r.text)


def test_the_daemons_verbs_say_it_does_not_run(s):
    for args in ({'action': 'pair'}, {'action': 'pairing'}, {'action': 'brightness', 'value': 40}):
        r = s.call('streamdeck', args, ok=False)
        expect(r.is_error and '409' in r.text and 'not running' in r.text, f'{args}: not running, said so', r.text)
    r = s.call('streamdeck', {'action': 'revoke'}, ok=False)
    expect(r.is_error and '404' in r.text, 'revoke: no companion of the daemon', r.text)
    r = s.call('streamdeck', {'action': 'settings', 'mode': 'sometimes'}, ok=False)
    expect(r.is_error and '400' in r.text, 'a bad mode is refused (nothing saved)', r.text)


def test_rest_the_socket_the_topic_and_the_cli_agree(s):
    status, rest = http(s, 'GET', '/api/v1/streamdeck')
    expect(status == 200 and rest['status']['state'] == 'off', 'REST status', rest)
    status, body = http(s, 'PUT', '/api/v1/streamdeck/udev')
    expect(status == 405, 'a verb on a method it does not take is 405', body)
    ws = WebSocket(ports(s)[0], token=COMPANION_NARROW_TOKEN)  # test_companion.py revokes "verify"
    try:
        ws.send({'op': 'call', 'rid': 1, 'method': 'GET', 'path': '/api/v1/streamdeck'})
        result = ws.until(lambda m: m.get('op') == 'result' and m.get('rid') == 1, 'the call')
        expect(result['status'] == 200 and result['body']['status']['state'] == 'off', 'the socket call', result)
        ws.send({'op': 'subscribe', 'topics': ['streamdeck.status']})
        state = ws.until(lambda m: m.get('t') == 'streamdeck.status' and m.get('op') == 'state', 'the topic')
        expect(state['value']['state'] == 'off' and 'instance' not in state, 'the topic: one value, no instance',
               state)
    finally:
        ws.close()
    with socket.create_connection(('127.0.0.1', ports(s)[1]), timeout=10) as cli:
        time.sleep(0.2)
        cli.recv(65536)
        cli.sendall(b'streamdeck\n')
        text = b''
        end = time.time() + 5
        while b'Stream Deck daemon' not in text and time.time() < end:
            text += cli.recv(65536)
    expect(b'Stream Deck daemon: off' in text, 'the CLI', text.decode(errors='replace'))


if __name__ == '__main__':
    sys.exit(main(globals()))
