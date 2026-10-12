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
        machineless = tool in ('search_api', 'asm_source') or (
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
        self.call('control_execution', {'action': 'run_frames', 'frames': 100})
        self.call('control_execution', {'action': 'pause'})

    def destroy_all(self):
        listing = self.call('emulator_manage', {'action': 'list'}, ok=False)
        for emulator in (listing.structured or {}).get('emulators', []):
            self.call('emulator_manage', {'action': 'destroy', 'target': emulator['id']}, ok=False)


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
