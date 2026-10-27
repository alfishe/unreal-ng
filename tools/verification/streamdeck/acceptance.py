#!/usr/bin/env python3
"""Stream Deck integration acceptance (docs/inprogress/2026-10-19-streamdeck-integration/requirements.md §8, ACC-1 ..
ACC-7), live: a rig of its own (an offscreen unreal-qt with its own HOME, ports and daemon lock, the daemon its
supervisor starts, Stream Deck Studio offscreen as an own Stream Deck + XL and as mirrors) and checks that drive it
through the WebAPI, the daemon's and the studio's automation. Nothing of the user's is touched: not their settings,
their pairing, their deck or their running daemon.

    acceptance.py --build cmake-build-agent-release            # the hardware-free checks
    acceptance.py --build ... --only acc2,acc6                  # some of them
    acceptance.py --build ... --keep                            # leave the rig running (prints its ports)

Each check prints what it measured; the exit code is the number of failed checks. Stdlib only."""

import argparse
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'tools', 'verification', 'mcp'))
from mcplib import Session  # noqa: E402

TOKEN = 'unc_acceptance-streamdeck-0001'


class Failure(Exception):
    pass


def expect(condition, what, details=''):
    if not condition:
        raise Failure(f'{what}: {details}' if details != '' else what)


def wait(predicate, what, timeout=10.0, step=0.05):
    """Polls predicate() until it returns a truthy value (returned) or the timeout (Failure with the last value)"""
    deadline = time.monotonic() + timeout
    last = None
    while True:
        try:
            last = predicate()
        except (OSError, urllib.error.URLError, ValueError, KeyError) as error:
            last = error
        if last and not isinstance(last, Exception):
            return last
        if time.monotonic() > deadline:
            raise Failure(f'{what} (after {timeout:.0f} s): {last!r}')
        time.sleep(step)


def http(port, method, path, body=None, token=None, timeout=10, raw=False):
    headers = {'Content-Type': 'application/json'}
    if token:
        headers['Authorization'] = 'Bearer ' + token
    request = urllib.request.Request(f'http://127.0.0.1:{port}{path}', method=method, headers=headers,
                                     data=json.dumps(body).encode() if body is not None else None)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            data = response.read()
            return response.status, (data if raw else json.loads(data.decode() or 'null'))
    except urllib.error.HTTPError as error:
        data = error.read()
        try:
            return error.code, json.loads(data.decode() or 'null')
        except ValueError:
            return error.code, data.decode(errors='replace')


def free(port):
    with socket.socket() as probe:
        return probe.connect_ex(('127.0.0.1', port)) != 0


class Rig:
    """unreal-qt (WebAPI base+0, CLI +100, MCP +200, ...), its daemon (automation base+50, viewers base+51) and the
    studios (automation base+60 ...), all with HOME and the daemon lock in the run folder"""

    def __init__(self, build, base, folder, live=False):
        self.live = live  # a visible unreal-qt and the daemon on the attached deck (owner-assisted ACC-1 / ACC-4)
        self.bin = os.path.join(REPO, build, 'bin')
        self.base = base
        self.folder = folder
        self.home = os.path.join(folder, 'home')
        self.webapi = base
        self.mcp = base + 200
        self.daemon = base + 50
        self.viewers = base + 51
        self.studios = []
        self.processes = []
        self.qt = None
        self.session = None

    # ------------------------------------------------------------------ processes
    def env(self):
        env = dict(os.environ)
        env['HOME'] = self.home
        for name, offset in (('UNREAL_WEBAPI_PORT', 0), ('UNREAL_CLI_PORT', 100), ('UNREAL_MCP_PORT', 200),
                             ('UNREAL_GDB_PORT', 300), ('UNREAL_DEZOG_PORT', 400), ('UNREAL_ZRCP_PORT', 500)):
            env[name] = str(self.base + offset)
        env['UNREAL_COMPANION_TOKENS'] = f'acceptance:{TOKEN}:all'
        env['UNREAL_STREAMDECK_LOCK'] = os.path.join(self.folder, 'unreal-streamdeck.lock')
        env['UNREAL_STREAMDECK_BIN'] = os.path.join(self.bin, 'unreal-streamdeck')
        env['UNREAL_QT_VIEW_SETTINGS'] = 'off'
        env['UNREAL_RECENT_FILES'] = 'off'
        env.pop('UNREAL_STREAMDECK_SUPERVISOR', None)
        if not self.live:
            env['UNREAL_STREAMDECK_USB'] = 'off'  # the daemon never opens the user's deck
        return env

    def log(self, name):
        return open(os.path.join(self.folder, name + '.log'), 'ab')

    def start(self):
        for offset in (0, 50, 51, 60, 61, 62, 100, 200):
            expect(free(self.base + offset), f'port {self.base + offset} is free (pick another --port-base)')
        os.makedirs(os.path.join(self.home, '.config', 'unreal-streamdeck'), exist_ok=True)
        # The daemon's pairing as `unreal-streamdeck pair` leaves it: the token the instance was started with
        with open(os.path.join(self.home, '.config', 'unreal-streamdeck', 'companion.json'), 'w') as f:
            json.dump({'webapi': f'127.0.0.1:{self.webapi}', 'token': TOKEN, 'companion_id': 'acceptance'}, f)
        qt = os.path.join(self.bin, 'unreal-qt.app', 'Contents', 'MacOS', 'unreal-qt')
        if not os.path.exists(qt):
            qt = os.path.join(self.bin, 'unreal-qt')
        self.qt = subprocess.Popen([qt] + ([] if self.live else ['-platform', 'offscreen']), env=self.env(), stdout=self.log('unreal-qt'),
                                   stderr=subprocess.STDOUT)
        self.processes.append(self.qt)
        self.session = Session(f'http://127.0.0.1:{self.mcp}/mcp', 'PENTAGON')
        wait(lambda: self.session.rpc('ping', timeout=2) is not None, 'unreal-qt answers on MCP', 40, 0.3)
        self.machine()
        # The daemon as unreal-qt's supervisor starts it, on the rig's ports
        status, body = http(self.webapi, 'PUT', '/api/v1/streamdeck/settings',
                            {'mode': 'always', 'api_port': self.daemon, 'viewers_port': self.viewers})
        expect(status == 200, 'the supervisor takes the rig settings', body)
        wait(lambda: self.health().get('ok'), 'the supervised daemon answers', 20, 0.2)

    def machine(self, model='PENTAGON'):
        s = self.session
        listing = s.call('emulator_manage', {'action': 'list'}, ok=False)
        for emulator in (listing.structured or {}).get('emulators', []):
            s.call('emulator_manage', {'action': 'destroy', 'target': emulator['id']}, ok=False)
        s.call('emulator_manage', {'action': 'create', 'model': model})
        s.call('emulator_manage', {'action': 'start'}, ok=False)
        self.mute()

    def mute(self):
        # Silent: the owner hears no key clicks from a verification rig
        for emulator in self.emulators():
            http(self.webapi, 'PUT', f'/api/v1/emulator/{emulator["id"]}/audio/mixer/master', {'muted': True})

    def emulators(self):
        status, body = http(self.webapi, 'GET', '/api/v1/emulator')
        expect(status == 200, 'the instance list', body)
        return body.get('emulators', body) if isinstance(body, dict) else body

    def studio(self, args, name):
        """A studio on the next automation port: `--own plusxl` or a mirror (`--mirror`, the default role)"""
        port = self.base + 60 + len(self.studios)
        binary = os.path.join(self.bin, 'unreal-streamdeck-studio.app', 'Contents', 'MacOS', 'unreal-streamdeck-studio')
        if not os.path.exists(binary):
            binary = os.path.join(self.bin, 'unreal-streamdeck-studio')
        process = subprocess.Popen([binary, '-platform', 'offscreen', '--daemon', f'127.0.0.1:{self.viewers}',
                                    '--daemon-api', str(self.daemon), '--api-port', str(port)] + args,
                                   env=self.env(), stdout=self.log(name), stderr=subprocess.STDOUT)
        self.processes.append(process)
        self.studios.append(port)
        wait(lambda: http(port, 'GET', '/api/v1/connection')[1].get('state') == 'joined', f'{name} joined', 20, 0.2)
        return port

    def stop(self):
        for process in reversed(self.processes):
            if process.poll() is None:
                process.terminate()
        for process in self.processes:
            try:
                process.wait(10)
            except subprocess.TimeoutExpired:
                process.kill()
        # The daemon ends with its last lease (unreal-qt); one that does not is ended here
        pid = self.daemon_pid
        if pid:
            try:
                os.kill(pid, signal.SIGTERM)
            except OSError:
                pass

    # ------------------------------------------------------------------ reads
    daemon_pid = None

    def health(self):
        status, body = http(self.daemon, 'GET', '/api/v1/health', timeout=2)
        if status == 200 and isinstance(body, dict):
            self.daemon_pid = body.get('pid', self.daemon_pid)
            return body
        return {}

    def deck_state(self, deck='first'):
        status, body = http(self.daemon, 'GET', f'/api/v1/state?deck={deck}')
        expect(status == 200, 'the deck state', body)
        return body

    def context(self, emulator=None):
        path = '/api/v1/surfaces/context' + (f'?instance={emulator}' if emulator else '')
        status, body = http(self.webapi, 'GET', path)
        expect(status == 200, 'the surface context', body)
        return body


# ---------------------------------------------------------------------- checks
CHECKS = []


def check(name):
    def register(function):
        CHECKS.append((name, function))
        return function
    return register


def run(args):
    folder = os.path.join(REPO, 'scratch', 'streamdeck-acceptance', time.strftime('%Y%m%d-%H%M%S'))
    os.makedirs(folder)
    rig = Rig(args.build, args.port_base, folder, args.live)
    failed = 0
    try:
        rig.start()
        rig.own = None if args.live else rig.studio(['--own', 'plusxl'], 'studio-own')
        print(f'rig: unreal-qt {rig.webapi}, daemon {rig.daemon} (pid {rig.daemon_pid}), studio {rig.own}; {folder}')
        for name, function in CHECKS:
            if args.only and name not in args.only:
                continue
            started = time.monotonic()
            try:
                notes = function(rig) or []
                print(f'  ok    {name} ({time.monotonic() - started:.1f} s)')
                for note in notes:
                    print(f'        {note}')
            except Failure as error:
                failed += 1
                print(f'  FAIL  {name}: {error}')
        if args.keep or args.live:
            print(f'kept: unreal-qt WebAPI {rig.webapi}, MCP {rig.mcp}, daemon {rig.daemon}, studios {rig.studios}')
            stop = os.path.join(folder, 'stop')
            print(f'touch {stop} (or Ctrl-C) stops the rig', flush=True)
            try:
                while not os.path.exists(stop):
                    time.sleep(0.5)
            except KeyboardInterrupt:
                pass
    finally:
        rig.stop()
    print(f'{len(CHECKS) - failed if not args.only else len(args.only) - failed} passed, {failed} failed; logs {folder}')
    return failed


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build', default='cmake-build-agent-release', help='the build folder (bin/ inside)')
    parser.add_argument('--port-base', type=int, default=9400, help='unreal-qt WebAPI port; the rest follow it')
    parser.add_argument('--only', type=lambda v: v.split(','), help='checks to run, comma separated')
    parser.add_argument('--live', action='store_true',
                        help='visible unreal-qt, the daemon opens the attached deck (implies --keep)')
    parser.add_argument('--keep', action='store_true', help='keep the rig running at the end')
    sys.exit(run(parser.parse_args()))


if __name__ == '__main__':
    main()
