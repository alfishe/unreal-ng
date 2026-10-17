#!/usr/bin/env python3
"""Companion clients (docs/inprogress/2026-10-16-companion-client): a paired device acts as the user's hands. With
its token its changes open no session (the GUI shows no remote control and does not lock); debugger actions are
refused; while an agent holds control it is refused and may take control back; every plane lists and revokes.

Needs the companion that mcplib.launch provisions (UNREAL_COMPANION_TOKENS 'verify:<token>:all'): run it with
--launch, or start the server with that variable. The WebAPI and the CLI are found from the MCP port (WebAPI = MCP - 200,
CLI = WebAPI + 100)."""

import base64
import json
import os
import re
import socket
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

from mcplib import COMPANION_NARROW_TOKEN, COMPANION_TOKEN, expect, main

_ports = {}


def webapi(s):
    if 'webapi' not in _ports:
        port = urllib.parse.urlparse(s.url).port
        _ports['webapi'] = f'http://127.0.0.1:{port - 200}'
        _ports['cli'] = port - 100
    return _ports['webapi']


def http(s, method, path, body=None, token=True, headers=None):
    """(status, json body, headers: case-insensitive) of a WebAPI request; token=True: as the verification companion"""
    request = urllib.request.Request(webapi(s) + path, method=method,
                                     data=json.dumps(body).encode() if body is not None else None)
    request.add_header('Content-Type', 'application/json')
    if token:
        request.add_header('Authorization', 'Bearer ' + (token if isinstance(token, str) else COMPANION_TOKEN))
    for name, value in (headers or {}).items():
        request.add_header(name, value)
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            raw = response.read().decode()
            return response.status, (json.loads(raw) if raw else {}), response.headers
    except urllib.error.HTTPError as error:
        raw = error.read().decode()
        return error.code, (json.loads(raw) if raw else {}), error.headers


def emulator_id(s):
    status, body, _ = http(s, 'GET', '/api/v1/emulator', token=False)
    expect(status == 200 and body.get('emulators'), 'an instance to work on', body)
    return body['emulators'][0]['id']


def held(s):
    status, body, _ = http(s, 'GET', f'/api/v1/emulator/{emulator_id(s)}/control/session', token=False)
    expect(status == 200, 'the session state', body)
    return body['held']


def free_control(s):
    """The MCP calls of the setup leave an implicit session: an explicit take replaces it, the release ends it"""
    s.call('control_execution', {'action': 'take_control'}, ok=False)
    s.call('control_execution', {'action': 'release_control'}, ok=False)
    expect(not held(s), 'run control is free', '')


def setup(s):
    s.fresh_machine()
    # Silent: the tests press keys (the mixer changes no machine, it opens no session)
    http(s, 'POST', f'/api/v1/emulator/{emulator_id(s)}/audio/mixer/master', {'muted': True}, token=False)
    free_control(s)


def cli(s, *commands):
    """The answers of a CLI connection to `commands`, one string each"""
    webapi(s)
    answers = []
    with socket.create_connection(('127.0.0.1', _ports['cli']), timeout=10) as sock:
        def read_until_prompt():
            data = b''
            deadline = time.time() + 10
            # the prompt ends every answer: "\r\n> ", the greeting's followed by an erase-line sequence
            while not re.search(rb'\n> (\x1b\[K)?$', data) and time.time() < deadline:
                chunk = sock.recv(65536)
                if not chunk:
                    break
                data += chunk
            return data.decode(errors='replace')
        read_until_prompt()  # the greeting
        for command in commands:
            sock.sendall((command + '\n').encode())
            answers.append(read_until_prompt())
    return answers


class Events:
    """A WebSocket client of /api/v1/companions/events (stdlib only: text frames, client-masked)"""

    def __init__(self, s, token=None):
        port = int(webapi(s).rsplit(':', 1)[1])
        self.sock = socket.create_connection(('127.0.0.1', port), timeout=10)
        key = base64.b64encode(os.urandom(16)).decode()
        auth = f'Authorization: Bearer {token}\r\n' if token else ''
        self.sock.sendall((f'GET /api/v1/companions/events HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n'
                           f'Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n'
                           f'Sec-WebSocket-Version: 13\r\n{auth}\r\n').encode())
        head = b''
        while b'\r\n\r\n' not in head:
            chunk = self.sock.recv(4096)
            expect(chunk, 'the handshake answer', head)
            head += chunk
        head, self.buffer = head.split(b'\r\n\r\n', 1)
        expect(head.startswith(b'HTTP/1.1 101'), 'the WebSocket upgrade', head)
        self.closed = False

    def send(self, value):
        data = json.dumps(value).encode()
        mask = os.urandom(4)
        header = bytes([0x81, 0x80 | len(data)]) if len(data) < 126 else \
            bytes([0x81, 0x80 | 126]) + len(data).to_bytes(2, 'big')
        self.sock.sendall(header + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def _read(self, n):
        while len(self.buffer) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise EOFError
            self.buffer += chunk
        data, self.buffer = self.buffer[:n], self.buffer[n:]
        return data

    def recv(self, timeout=5):
        """The next JSON text message; None once the server closed"""
        self.sock.settimeout(timeout)
        while True:
            try:
                b0, b1 = self._read(2)
                n = b1 & 0x7f
                if n == 126:
                    n = int.from_bytes(self._read(2), 'big')
                elif n == 127:
                    n = int.from_bytes(self._read(8), 'big')
                payload = self._read(n)
            except (EOFError, ConnectionError):
                self.closed = True
                return None
            if b0 & 0x0f == 0x8:
                self.closed = True
                return None
            if b0 & 0x0f == 0x1:
                return json.loads(payload)

    def until(self, predicate, what, timeout=5):
        deadline = time.time() + timeout
        seen = []
        while time.time() < deadline:
            message = self.recv(max(0.1, deadline - time.time()))
            if message is None:
                break
            seen.append(message)
            if predicate(message):
                return message
        expect(False, what, seen)

    def close(self):
        self.sock.close()


def test_mcp_lists_the_companion(s):
    r = s.call('companion_manage', {'action': 'list'})
    expect("'verify'" in r.text and 'environment' in r.text,
           'the provisioned companion in the text (launch with --launch, or set UNREAL_COMPANION_TOKENS)', r.text)
    names = [c['name'] for c in (r.structured or {}).get('companions', [])]
    expect('verify' in names, 'and in structuredContent', r.structured)


def test_user_actions_open_no_session(s):
    eid = emulator_id(s)
    for path, body in (('/resume', None), ('/keyboard/tap', {'key': 'space'}), ('/reset', None),
                       ('/settings/speed', {'value': 1}), ('/pause', None)):
        status, answer, headers = http(s, 'POST', f'/api/v1/emulator/{eid}{path}', body)
        expect(status == 200, f'{path} as the companion', answer)
        expect('X-Unreal-Session' not in headers and 'automation_session' not in answer,
               f'{path}: no session announced', headers)
        expect(not held(s), f'{path}: run control stays free (no remote control at the GUI)', '')


def test_debugger_actions_work_and_stepping_needs_a_pause(s):
    eid = emulator_id(s)
    http(s, 'POST', f'/api/v1/emulator/{eid}/resume')
    status, answer, _ = http(s, 'POST', f'/api/v1/emulator/{eid}/memory/write', {'address': 32768, 'data': [0]})
    expect(status == 200, 'a memory write: the companion may do what the user may', answer)
    status, answer, _ = http(s, 'POST', f'/api/v1/emulator/{eid}/step')
    expect(status == 409 and answer.get('error') == 'not_now', 'a step while the machine runs: pause first', answer)
    status, me, _ = http(s, 'GET', '/api/v1/companions/me')
    step = me['emulators'][0]['actions']['step']
    expect(not step['available'] and 'pause first' in step.get('reason', ''), 'me says why not', step)
    http(s, 'POST', f'/api/v1/emulator/{eid}/pause')
    status, me, _ = http(s, 'GET', '/api/v1/companions/me')
    expect(me['emulators'][0]['actions']['step']['available'], 'paused: step is available', me['emulators'][0]['actions'])
    status, answer, _ = http(s, 'POST', f'/api/v1/emulator/{eid}/step')
    expect(status == 200, 'paused: it steps', answer)
    expect(not held(s), 'nothing opened a session', '')


def test_a_scope_not_granted_is_refused(s):
    eid = emulator_id(s)
    status, answer, _ = http(s, 'POST', f'/api/v1/emulator/{eid}/disk/A/eject', token=COMPANION_NARROW_TOKEN)
    expect(status == 403 and answer.get('error') == 'companion_forbidden' and answer.get('required_scope') == 'media',
           'the narrow companion has no media', answer)
    status, me, _ = http(s, 'GET', '/api/v1/companions/me', token=COMPANION_NARROW_TOKEN)
    disk = me['emulators'][0]['actions']['disk']
    expect(not disk['available'] and "'media'" in disk.get('reason', ''), 'me says why not', disk)


def test_without_a_token_the_gate_is_unchanged(s):
    eid = emulator_id(s)
    status, answer, headers = http(s, 'POST', f'/api/v1/emulator/{eid}/resume', token=False)
    expect(status == 200 and headers.get('X-Unreal-Session-Implicit') == 'true', 'an implicit session opens', headers)
    expect(held(s), 'the session holds the machine', '')
    http(s, 'DELETE', f'/api/v1/emulator/{eid}/control/session', token=False,
         headers={'X-Unreal-Session': headers['X-Unreal-Session']})
    expect(not held(s), 'and ends when released', '')


def test_agents_never_act_as_companions(s):
    eid = emulator_id(s)
    status, answer, _ = http(s, 'POST', f'/api/v1/emulator/{eid}/resume', headers={'X-Unreal-Source': 'mcp'})
    expect(status == 403 and answer.get('error') == 'companion_not_for_agents', 'the MCP marker refuses a token', answer)
    status, answer, _ = http(s, 'POST', f'/api/v1/emulator/{eid}/resume', token='unc_not-a-companion-token-0000')
    expect(status == 401 and answer.get('error') == 'companion_unknown', 'an unknown token is refused', answer)


def test_a_held_session_refuses_and_take_back_ends_it(s):
    eid = emulator_id(s)
    s.call('control_execution', {'action': 'take_control', 'client': 'verification agent'})
    status, answer, _ = http(s, 'POST', f'/api/v1/emulator/{eid}/resume')
    expect(status == 409 and answer.get('error') == 'held_by_session', 'refused while the agent holds it', answer)
    expect(answer.get('holder', {}).get('surface') == 'mcp' and answer.get('take_back') is True,
           'names the holder and offers take-back', answer)
    status, me, _ = http(s, 'GET', '/api/v1/companions/me')
    control = me['emulators'][0]['control']
    expect(not control['can_act'] and control['can_take_back'], 'me: cannot act, may take back', me)

    status, answer, _ = http(s, 'POST', f'/api/v1/emulator/{eid}/control/take-back')
    expect(status == 200 and answer.get('taken_back'), 'taken back', answer)
    expect(not held(s), 'the session is over', '')
    r = s.call('inspect_state', {'aspects': ['registers']}, ok=False)
    expect(r.is_error and 'revoked_by_user' in r.text, "the agent's next call is the stop error", r.text)

    status, answer, _ = http(s, 'POST', f'/api/v1/emulator/{eid}/control/allow-again')
    expect(status == 200 and answer.get('allowed'), 'the agent may take control again', answer)
    http(s, 'POST', f'/api/v1/emulator/{eid}/pause')  # resume answers an error on a running machine
    status, answer, _ = http(s, 'POST', f'/api/v1/emulator/{eid}/resume')
    expect(status == 200, 'the companion acts again', answer)
    status, answer, _ = http(s, 'POST', f'/api/v1/emulator/{eid}/control/take-back')
    expect(status == 409 and answer.get('error') == 'nothing_held', 'nothing left to take back', answer)


def test_the_cli_acts_as_the_companion(s):
    auth, resume, write, step, status, session, listing = cli(
        s, f'companion auth {COMPANION_TOKEN}', 'resume', 'memory write 8000 00', 'step', 'companion', 'session take',
        'companion list')
    expect("Companion 'verify'" in auth, 'authenticated', auth)
    expect('Error' not in resume and 'Note:' not in resume, 'resume opens no session', resume)
    expect('Error' not in write and 'Note:' not in write, 'a memory write works and opens no session', write)
    expect('not_now' in step, 'a step while running: pause first', step)
    expect('Run control: free' in status and 'Available now:' in status and 'step: the machine runs' in status,
           'the status says control is free and what is possible now', status)
    expect('companion_session' in session, 'a companion takes no session', session)
    expect(not held(s), 'nothing held after the CLI', '')
    expect('verify' in listing and 'environment' in listing, 'the connection lists the companions', listing)


def test_me_and_pairing_routes(s):
    status, me, _ = http(s, 'GET', '/api/v1/companions/me')
    expect(status == 200 and me['companion']['name'] == 'verify', 'me', me)
    expect(me['emulators'] and me['emulators'][0]['control']['can_act'], 'me: may act', me)
    status, answer, _ = http(s, 'GET', '/api/v1/companions/me', token=False)
    expect(status == 401 and answer.get('error') == 'companion_required', 'me needs a token', answer)

    status, listing, _ = http(s, 'GET', '/api/v1/companions', token=False)
    expect(status == 200 and 'token' not in json.dumps(listing).replace('token_', ''), 'the list has no tokens', listing)
    if not listing.get('approval_available'):
        status, answer, _ = http(s, 'POST', '/api/v1/companions/pairings', {'name': 'probe'}, token=False)
        expect(status == 503 and answer.get('error') == 'pairing_unavailable', 'no GUI: no pairing', answer)
        return
    status, pairing, _ = http(s, 'POST', '/api/v1/companions/pairings', {'name': 'probe', 'scopes': ['run']},
                              token=False)
    expect(status == 202 and len(pairing.get('code', '')) == 6, 'a pairing is requested', pairing)
    pid = pairing['pairing_id']
    status, poll, _ = http(s, 'GET', f'/api/v1/companions/pairings/{pid}', token=False)
    expect(status == 200 and poll.get('state') == 'pending', 'it waits for the user', poll)
    status, answer, _ = http(s, 'DELETE', f'/api/v1/companions/pairings/{pid}', token=False)
    expect(status == 200 and answer.get('cancelled'), 'the device cancels it', answer)
    status, poll, _ = http(s, 'GET', f'/api/v1/companions/pairings/{pid}', token=False)
    expect(status == 404, 'gone', poll)


def test_events_follow_the_machine(s):
    eid = emulator_id(s)
    http(s, 'POST', f'/api/v1/emulator/{eid}/pause')
    events = Events(s, COMPANION_TOKEN)
    try:
        hello = events.recv()
        expect(hello and hello.get('op') == 'hello' and hello['companion']['name'] == 'verify', 'hello: me', hello)
        entry = next(e for e in hello['emulators'] if e['id'] == eid)
        expect(entry['state'] == 'paused' and entry['actions']['step']['available'], 'paused: step lit', entry)
        start = time.time()
        http(s, 'POST', f'/api/v1/emulator/{eid}/resume')
        running = events.until(lambda m: m.get('op') == 'emulator' and m['emulator']['id'] == eid and
                               not m['emulator']['actions']['step']['available'], 'resumed: step goes dark')
        expect('pause first' in running['emulator']['actions']['step'].get('reason', ''), 'with the reason', running)
        expect(time.time() - start < 0.9, 'at once, not at the next second', time.time() - start)

        s.call('control_execution', {'action': 'take_control', 'client': 'verification agent'})
        held_event = events.until(lambda m: m.get('op') == 'emulator' and m['emulator']['control']['held'],
                                  'an agent takes control: the entry says so')
        expect(held_event['emulator']['control']['can_take_back'], 'and offers take-back', held_event)
        http(s, 'POST', f'/api/v1/emulator/{eid}/control/take-back')
        events.until(lambda m: m.get('op') == 'emulator' and not m['emulator']['control']['held'], 'taken back')
        r = s.call('inspect_state', {'aspects': ['registers']}, ok=False)  # the agent's one stop answer
        expect(r.is_error and 'revoked_by_user' in r.text, "the agent's next call is the stop error", r.text)
        http(s, 'POST', f'/api/v1/emulator/{eid}/control/allow-again')
        http(s, 'POST', f'/api/v1/emulator/{eid}/pause')
        events.send({'op': 'snapshot'})
        events.until(lambda m: m.get('op') == 'snapshot', 'a snapshot on request')
    finally:
        events.close()


def test_events_authenticate_by_message(s):
    events = Events(s)
    try:
        events.send({'op': 'snapshot'})
        refused = events.recv()
        expect(refused and refused.get('error') == 'companion_required', 'nothing before authentication', refused)
        events.send({'op': 'auth', 'token': COMPANION_TOKEN})
        events.until(lambda m: m.get('op') == 'hello', 'authenticated by message')
    finally:
        events.close()
    events = Events(s)
    try:
        events.send({'op': 'auth', 'token': 'unc_not-a-companion-token-0000'})
        refused = events.recv()
        expect(refused and refused.get('error') == 'companion_unknown', 'an unknown token', refused)
        expect(events.recv() is None, 'and the connection closes', '')
    finally:
        events.close()


def test_zz_revoked_over_mcp_the_token_stops(s):
    events = Events(s, COMPANION_TOKEN)
    events.until(lambda m: m.get('op') == 'hello', 'hello')
    r = s.call('companion_manage', {'action': 'revoke', 'id': 'verify'})
    expect('revoked' in r.text, 'revoked', r.text)
    events.until(lambda m: m.get('op') == 'revoked', 'the events channel is told')
    expect(events.recv() is None, 'and closed', '')
    events.close()
    status, answer, _ = http(s, 'POST', f'/api/v1/emulator/{emulator_id(s)}/resume')
    expect(status == 401 and answer.get('error') == 'companion_unknown', 'the token no longer works', answer)


if __name__ == '__main__':
    sys.exit(main(globals()))
