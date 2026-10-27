"""Shared plumbing of the MCP verification scripts: a JSON-RPC client for the emulator's MCP endpoint, an emulator
instance of the run's own, and a small test runner (standard library only).

A script is a list of `test_*` functions taking a `Session`; `main(globals())` runs them:

    from mcplib import main, expect

    def test_something(s):
        r = s.call('inspect_state', {'aspects': ['registers']})
        expect('PC=' in r.text, 'registers in the text', r.text)

    if __name__ == '__main__':
        main(globals())

Options every script takes (see `--help`): `--mcp-url` of a running server, or `--launch <unreal-qt binary>` to start
one on ports of its own (`--port-base`, default 8095: WebAPI 8095, MCP 8295, the rest +100/+300/+400/+500), stopped at
the end; `--model` of the machine the tests create (default PENTAGON); `-k` to run the tests whose name contains it.
"""

import argparse
import json
import os
import socket
import subprocess
import sys
import time
import traceback
import urllib.error
import urllib.request


class Failure(Exception):
    pass


# The companion launch() provisions (UNREAL_COMPANION_TOKENS); test_companion.py acts with it
COMPANION_TOKEN = os.environ.get('UNREAL_COMPANION_VERIFY_TOKEN', 'unc_verification-companion-0001')
# ... and one with the run scope only, for the refusals of a scope the user did not grant
COMPANION_NARROW_TOKEN = 'unc_verification-narrow-0001'
# ... and one for the surface topic of /api/v1/ws (test_ws_surface.py; test_companion.py revokes 'verify')
COMPANION_SURFACE_TOKEN = 'unc_verification-surface-0001'


def expect(condition, what, details=''):
    """A check: raises Failure with `what` (and the first 600 characters of `details`) when false"""
    if not condition:
        raise Failure(what + ('\n      ' + str(details)[:600].replace('\n', '\n      ') if details != '' else ''))


class Result:
    """One tools/call answer: text (every text block joined), the blocks, structuredContent, isError"""

    def __init__(self, body):
        self.raw = body
        if 'error' in body:
            raise Failure(f"JSON-RPC error {body['error']}")
        result = body.get('result', {})
        self.content = result.get('content', [])
        self.text = '\n'.join(c.get('text', '') for c in self.content if c.get('type') == 'text')
        self.first_text = self.content[0].get('text', '') if self.content else ''
        self.structured = result.get('structuredContent')
        self.is_error = bool(result.get('isError'))

    def blocks(self, kind):
        return [c for c in self.content if c.get('type') == kind]


class Session:
    """The MCP endpoint and the machine the tests work on (target "auto" resolves to it: the run creates the only one)"""

    def __init__(self, url, model):
        self.url = url
        self.model = model
        self.next_id = 1

    def rpc(self, method, params=None, timeout=120):
        self.next_id += 1
        payload = {'jsonrpc': '2.0', 'id': self.next_id, 'method': method, 'params': params or {}}
        request = urllib.request.Request(self.url, data=json.dumps(payload).encode(),
                                         headers={'Content-Type': 'application/json',
                                                  'Accept': 'application/json, text/event-stream'})
        with urllib.request.urlopen(request, timeout=timeout) as response:
            raw = response.read().decode()
        if raw.startswith('event:') or raw.startswith('data:'):  # SSE framing: the last data line is the answer
            raw = [line[5:].strip() for line in raw.splitlines() if line.startswith('data:')][-1]
        return json.loads(raw)

    def call(self, tool, args=None, ok=True):
        """tools/call; with ok=True a tool error fails the test"""
        args = dict(args or {})
        machineless = tool in ('search_api', 'asm_source', 'streamdeck') or (
            tool == 'signature_info' and args.get('action') != 'media') or (
            tool == 'emulator_manage' and args.get('action') in ('list', 'create', 'list_models', 'server'))
        if not machineless:
            args.setdefault('target', 'auto')
        result = Result(self.rpc('tools/call', {'name': tool, 'arguments': args}))
        if ok:
            expect(not result.is_error, f'{tool} {json.dumps(args)} is not an error', result.text)
        return result

    def tools(self):
        return {t['name']: t for t in self.rpc('tools/list')['result']['tools']}

    def fresh_machine(self, model=None):
        """Destroys every instance and creates one: each script starts from a known machine"""
        listing = self.call('emulator_manage', {'action': 'list'}, ok=False)
        for emulator in (listing.structured or {}).get('emulators', []):
            self.call('emulator_manage', {'action': 'destroy', 'target': emulator['id']}, ok=False)
        self.call('emulator_manage', {'action': 'create', 'model': model or self.model})
        self.call('emulator_manage', {'action': 'start'}, ok=False)  # create starts it on most builds
        # Silent: a verification run makes no sound on the host
        self.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/audio/mixer/master',
                                 'body': {'muted': True}}, ok=False)
        self.call('control_execution', {'action': 'run_frames', 'frames': 100})
        self.call('control_execution', {'action': 'pause'})

    def destroy_all(self):
        listing = self.call('emulator_manage', {'action': 'list'}, ok=False)
        for emulator in (listing.structured or {}).get('emulators', []):
            self.call('emulator_manage', {'action': 'destroy', 'target': emulator['id']}, ok=False)


class WebSocket:
    """A WebSocket client of the WebAPI (stdlib only: text frames, client-masked, pings answered): JSON objects in and
    out. `headers`: extra handshake lines ({'X-Unreal-Source': 'mcp'}). With upgrade=False a refused handshake is no
    failure: `status` is its status line (the HTTP gates refuse a bad token before the upgrade)"""

    def __init__(self, port, path='/api/v1/ws', token=None, headers=None, upgrade=True, rcvbuf=None):
        import base64
        if rcvbuf:  # a small receive window, set before connect (a slow consumer)
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
            self.sock.settimeout(10)
            self.sock.connect(('127.0.0.1', port))
        else:
            self.sock = socket.create_connection(('127.0.0.1', port), timeout=10)
        key = base64.b64encode(os.urandom(16)).decode()
        extra = f'Authorization: Bearer {token}\r\n' if token else ''
        for name, value in (headers or {}).items():
            extra += f'{name}: {value}\r\n'
        self.sock.sendall((f'GET {path} HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\n'
                           f'Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n'
                           f'{extra}\r\n').encode())
        head = b''
        while b'\r\n\r\n' not in head:
            chunk = self.sock.recv(4096)
            expect(chunk, 'the handshake answer', head)
            head += chunk
        head, self.buffer = head.split(b'\r\n\r\n', 1)
        self.status = head.split(b'\r\n', 1)[0].decode()
        self.closed = not head.startswith(b'HTTP/1.1 101')
        if upgrade:
            expect(not self.closed, 'the WebSocket upgrade', head)

    def _frame(self, opcode, data):
        mask = os.urandom(4)
        n = len(data)
        header = bytes([0x80 | opcode]) + (bytes([0x80 | n]) if n < 126 else
                                           bytes([0x80 | 126]) + n.to_bytes(2, 'big') if n < 65536 else
                                           bytes([0x80 | 127]) + n.to_bytes(8, 'big'))
        self.sock.sendall(header + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def send(self, value):
        self._frame(0x1, json.dumps(value).encode())

    def _read(self, n):
        while len(self.buffer) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise EOFError
            self.buffer += chunk
        data, self.buffer = self.buffer[:n], self.buffer[n:]
        return data

    def recv(self, timeout=5):
        """The next JSON text message; None once the server closed (or nothing came in `timeout`: self.closed stays
        False then)"""
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
            except socket.timeout:
                return None
            except (EOFError, ConnectionError, OSError):
                self.closed = True
                return None
            opcode = b0 & 0x0f
            if opcode == 0x8:
                self.closed = True
                return None
            if opcode == 0x9:
                self._frame(0xA, payload)
            elif opcode == 0x1:
                return json.loads(payload)

    def until(self, predicate, what, timeout=5):
        """Reads until a message satisfies `predicate`; returns it (fails with what was seen otherwise)"""
        deadline = time.time() + timeout
        seen = []
        while time.time() < deadline:
            message = self.recv(max(0.05, deadline - time.time()))
            if message is None:
                if self.closed:
                    break
                continue
            seen.append(message)
            if predicate(message):
                return message
        expect(False, what, seen[-12:])

    def drain(self, seconds=0.2):
        """Every message that comes within `seconds`"""
        out = []
        deadline = time.time() + seconds
        while time.time() < deadline:
            message = self.recv(max(0.01, deadline - time.time()))
            if message is None:
                if self.closed:
                    break
                continue
            out.append(message)
        return out

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def launch(binary, port_base):
    """Starts unreal-qt offscreen on ports of its own; returns the process once the MCP endpoint answers"""
    # Another instance on these ports would answer instead of the launched one (its binary, not this build): refuse
    for offset in (0, 200):
        with socket.socket() as probe:
            if probe.connect_ex(('127.0.0.1', port_base + offset)) == 0:
                raise SystemExit(f'port {port_base + offset} is taken by another process: pick other ports with --port-base')
    env = dict(os.environ)
    for name, offset in (('UNREAL_WEBAPI_PORT', 0), ('UNREAL_CLI_PORT', 100), ('UNREAL_MCP_PORT', 200),
                         ('UNREAL_GDB_PORT', 300), ('UNREAL_DEZOG_PORT', 400), ('UNREAL_ZRCP_PORT', 500)):
        env[name] = str(port_base + offset)
    # No Stream Deck daemon of its own, whatever the user's setting says (test_streamdeck.py)
    env.setdefault('UNREAL_STREAMDECK_SUPERVISOR', 'off')
    # The picture as the defaults draw it: the user's View settings (GPU, CRT, scale, HUD) stay out
    env.setdefault('UNREAL_QT_VIEW_SETTINGS', 'off')
    # The files the scripts open stay out of the user's File > Recent Files (kept in memory only)
    env.setdefault('UNREAL_RECENT_FILES', 'off')
    # A companion client for test_companion.py (a paired device acting as the user's hands)
    env.setdefault('UNREAL_COMPANION_TOKENS', f'verify:{COMPANION_TOKEN}:all;narrow:{COMPANION_NARROW_TOKEN}:run;'
                                              f'surface:{COMPANION_SURFACE_TOKEN}:all')
    # /api/v1/ws backpressure in a test's time (test_ws_surface.py reads them too: the scripts inherit them)
    for name, value in (('UNREAL_WS_HIGH_WATER_BYTES', '16384'), ('UNREAL_WS_SLOW_CONSUMER_MS', '1500')):
        os.environ.setdefault(name, value)
        env.setdefault(name, os.environ[name])
    process = subprocess.Popen([binary, '-platform', 'offscreen'], env=env, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
    session = Session(f'http://127.0.0.1:{port_base + 200}/mcp', 'PENTAGON')
    for _ in range(60):
        try:
            session.rpc('ping', timeout=2)
            return process
        except (OSError, urllib.error.URLError, ValueError):
            if process.poll() is not None:
                raise SystemExit(f'{binary} exited with {process.returncode}')
            time.sleep(0.5)
    process.kill()
    raise SystemExit('the launched emulator does not answer on MCP')


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=sys.modules['__main__'].__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--mcp-url', default=os.environ.get('UNREAL_MCP_URL', 'http://127.0.0.1:8295/mcp'))
    parser.add_argument('--launch', metavar='UNREAL_QT', help='start this unreal-qt binary on ports of its own')
    parser.add_argument('--port-base', type=int, default=8095, help='WebAPI port of a launched instance (MCP = +200)')
    parser.add_argument('--model', default='PENTAGON')
    parser.add_argument('-k', dest='only', default='', help='run the tests whose name contains this')
    parser.add_argument('--keep', action='store_true', help='keep the machine the tests created')
    return parser.parse_args(argv)


def run(namespace, args, session):
    """Runs the test_* functions of a script; returns (passed, failed names)"""
    tests = [(name, fn) for name, fn in namespace.items() if name.startswith('test_') and callable(fn)
             and args.only in name]
    setup = namespace.get('setup')
    if setup:
        setup(session)
    else:
        session.fresh_machine()
    passed, failed = 0, []
    for name, fn in tests:
        started = time.time()
        try:
            fn(session)
            passed += 1
            print(f'  ok    {name} ({time.time() - started:.1f} s)')
        except Failure as failure:
            failed.append(name)
            print(f'  FAIL  {name}: {failure}')
        except Exception:  # a crash in the test or the server: report and go on
            failed.append(name)
            print(f'  ERROR {name}:\n' + traceback.format_exc(limit=3))
    if not args.keep:
        session.destroy_all()
    return passed, failed


def main(namespace, argv=None):
    args = parse_args(argv)
    process = None
    url = args.mcp_url
    if args.launch:
        process = launch(args.launch, args.port_base)
        url = f'http://127.0.0.1:{args.port_base + 200}/mcp'
    try:
        script = os.path.basename(namespace.get('__file__', 'script'))
        print(f'{script}:')
        passed, failed = run(namespace, args, Session(url, args.model))
        print(f'{script}: {passed} passed, {len(failed)} failed')
        return 0 if not failed else 1
    finally:
        if process:
            process.terminate()
            process.wait(timeout=20)
