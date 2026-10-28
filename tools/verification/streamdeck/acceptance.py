#!/usr/bin/env python3
"""Stream Deck integration acceptance (docs/inprogress/2026-10-19-streamdeck-integration/requirements.md §8, ACC-1 ..
ACC-7), live: a rig of its own (an offscreen unreal-qt with its own HOME, ports and daemon lock, the daemon its
supervisor starts, Stream Deck Studio offscreen as an own Stream Deck + XL and as mirrors) and checks that drive it
through the WebAPI, the daemon's and the studio's automation. Nothing of the user's is touched: not their settings,
their pairing, their deck or their running daemon.

The deck the checks drive is the studio's own Stream Deck + XL (input through the studio's automation, as a finger
on it); with `--live` it is the attached deck, driven through a mirror studio of it (its input goes to the daemon as
the deck's). What needs hardware the rig does not have is a `skip` with the reason (the Surface Dial of ACC-1, the
mirrors of ACC-4 without a real deck, the platform packages of ACC-5 without --package, CI's other OSes of ACC-7).

    acceptance.py --build cmake-build-agent-release            # the hardware-free checks
    acceptance.py --build ... --only acc2,acc6                  # some of them
    acceptance.py --build ... --scrub-seconds 10                # a shorter ACC-1 (the criterion is 60 s)
    acceptance.py --build ... --keep                            # leave the rig running (prints its ports)
    acceptance.py --only none --live                            # the manual review rig: visible unreal-qt, real deck
    acceptance.py --package <installed package> --only acc5     # ACC-5 on a fresh install (see --package)

Each check prints what it measured; the exit code is the number of failed checks. Stdlib only."""

import argparse
import json
import os
import random
import re
import signal
import socket
import subprocess
import sys
import threading
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


class Skip(Exception):
    """A check (or a part of it) this rig cannot run: the reason is printed, it counts as neither pass nor fail"""


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


def process_path(pid):
    """The executable a process runs"""
    if os.name == 'nt':
        command = ['powershell', '-NoProfile', '-Command', f'(Get-Process -Id {pid}).Path']
    elif os.path.exists(f'/proc/{pid}/exe'):
        return os.readlink(f'/proc/{pid}/exe')
    else:
        command = ['ps', '-p', str(pid), '-o', 'comm=']
    return subprocess.run(command, capture_output=True, text=True).stdout.strip()


class Rig:
    """unreal-qt (WebAPI base+0, CLI +100, MCP +200, ...), its daemon (automation base+50, viewers base+51) and the
    studios (automation base+60 ...), all with HOME and the daemon lock in the run folder"""

    def __init__(self, build, base, folder, live=False, package=None):
        self.live = live  # a visible unreal-qt and the daemon on the attached deck (owner-assisted ACC-1 / ACC-4)
        self.package = package  # an installed package's root: its own binaries and data, its own daemon lookup
        self.bin = os.path.join(REPO, build, 'bin')
        self.data = self.bin  # the directory holding streamdeck/
        self.qt = os.path.join(self.bin, 'unreal-qt.app', 'Contents', 'MacOS', 'unreal-qt')
        self.studio_binary = os.path.join(self.bin, 'unreal-streamdeck-studio.app', 'Contents', 'MacOS',
                                          'unreal-streamdeck-studio')
        app = os.path.join(package or '', 'UnrealNG.app')
        if package and os.path.isdir(app):  # macOS: the helper and the data inside the suite's bundle
            self.bin = os.path.join(app, 'Contents', 'MacOS')
            self.data = os.path.join(app, 'Contents', 'Resources')
            self.qt = os.path.join(self.bin, 'unreal-qt')
            self.studio_binary = os.path.join(app, 'Contents', 'Applications', 'Stream Deck Studio.app', 'Contents',
                                              'MacOS', 'unreal-streamdeck-studio')
        elif package:  # Windows ZIP, Linux tarball / extracted AppImage usr/bin: everything side by side
            self.bin = self.data = package
            exe = '.exe' if os.name == 'nt' else ''
            self.qt = os.path.join(package, 'unreal-qt' + exe)
            self.studio_binary = os.path.join(package, 'unreal-streamdeck-studio' + exe)
        if not os.path.exists(self.qt):
            self.qt = os.path.join(self.bin, 'unreal-qt')
        if not os.path.exists(self.studio_binary):
            self.studio_binary = os.path.join(self.bin, 'unreal-streamdeck-studio')
        self.base = base
        self.folder = folder
        self.home = os.path.join(folder, 'home')
        self.webapi = base
        self.mcp = base + 200
        self.daemon = base + 50
        self.viewers = base + 51
        self.studios = []
        self.processes = []
        self.qt_process = None
        self.session = None
        self.own = None    # the own studio's automation port (hardware-free)
        self.input = None  # the studio whose input drives the deck (the own one, or a mirror of the real deck)
        self.deck = None   # the serial of the deck the checks drive
        self.host_requests = 0  # REST requests this script sent to unreal-qt (ACC-1 subtracts them)

    # ------------------------------------------------------------------ processes
    def env(self):
        env = dict(os.environ)
        env['HOME'] = self.home
        for name, offset in (('UNREAL_WEBAPI_PORT', 0), ('UNREAL_CLI_PORT', 100), ('UNREAL_MCP_PORT', 200),
                             ('UNREAL_GDB_PORT', 300), ('UNREAL_DEZOG_PORT', 400), ('UNREAL_ZRCP_PORT', 500)):
            env[name] = str(self.base + offset)
        env['UNREAL_COMPANION_TOKENS'] = f'acceptance:{TOKEN}:all'
        env['UNREAL_STREAMDECK_LOCK'] = os.path.join(self.folder, 'unreal-streamdeck.lock')
        if not self.package:  # a package's unreal-qt finds its own daemon: part of what ACC-5 checks
            env['UNREAL_STREAMDECK_BIN'] = os.path.join(self.bin, 'unreal-streamdeck')
        env['UNREAL_QT_VIEW_SETTINGS'] = 'off'
        env['UNREAL_RECENT_FILES'] = 'off'
        env.pop('UNREAL_STREAMDECK_SUPERVISOR', None)
        if not self.live:
            env['UNREAL_STREAMDECK_USB'] = 'off'  # the daemon never opens the user's deck
        return env

    def platform(self):
        """offscreen, but visible with --live, and with --package: a package deploys the native platform plugin only"""
        return [] if self.live or self.package else ['-platform', 'offscreen']

    def log(self, name):
        return open(os.path.join(self.folder, name + '.log'), 'ab')

    def start(self):
        # every port the rig hands out: unreal-qt's servers (env()), the daemon and viewers, the studios
        for offset in (0, 50, 51, 60, 61, 62, 100, 200, 300, 400, 500):
            expect(free(self.base + offset), f'port {self.base + offset} is free (pick another --port-base)')
        os.makedirs(os.path.join(self.home, '.config', 'unreal-streamdeck'), exist_ok=True)
        # The daemon's pairing as `unreal-streamdeck pair` leaves it: the token the instance was started with
        with open(os.path.join(self.home, '.config', 'unreal-streamdeck', 'companion.json'), 'w') as f:
            json.dump({'webapi': f'127.0.0.1:{self.webapi}', 'token': TOKEN, 'companion_id': 'acceptance'}, f)
        self.qt_process = subprocess.Popen([self.qt] + self.platform(), env=self.env(), stdout=self.log('unreal-qt'),
                                   stderr=subprocess.STDOUT)
        self.processes.append(self.qt_process)
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
        self.release_mcp()
        self.mute()

    def release_mcp(self):
        """The MCP calls leave an implicit run-control session that refuses the deck's and the checks' run actions
        for a while: an explicit take replaces it, the release ends it"""
        self.session.call('control_execution', {'action': 'take_control'}, ok=False)
        self.session.call('control_execution', {'action': 'release_control'}, ok=False)

    def mute(self):
        # Silent: the owner hears no key clicks from a verification rig
        for emulator in self.emulators():
            self.host('PUT', f'/api/v1/emulator/{emulator["id"]}/audio/mixer/master', {'muted': True})

    def host(self, method, path, body=None, timeout=10):
        """A WebAPI request to unreal-qt, counted (host.clients.rest.requests minus these = the daemon's)"""
        self.host_requests += 1
        return http(self.webapi, method, path, body, timeout=timeout)

    def action(self, action_id, value=None, params=None, emulator=None):
        body = {}
        if value is not None:
            body['value'] = value
        if params:
            body['params'] = params
        path = f'/api/v1/controllers/actions/{action_id}' + (f'?instance={emulator}' if emulator else '')
        status, answer = self.host('POST', path, body)
        expect(status == 200 and isinstance(answer, dict) and answer.get('ok'), f'{action_id} runs', answer)
        if answer.get('automation_session', {}).get('implicit'):
            # a run action opens an implicit session for this script, which refuses the deck's run actions (its jogs)
            # until the lease ends: the script ends it at once
            target = emulator or answer.get('instance')
            self.host('DELETE', f'/api/v1/emulator/{target}/control/session')
        return answer

    def readouts(self, names, emulator=None):
        path = '/api/v1/surfaces/readouts?names=' + ','.join(names) + (f'&instance={emulator}' if emulator else '')
        status, body = self.host('GET', path)
        expect(status == 200, 'the readouts', body)
        return body['values']

    def emulators(self):
        status, body = self.host('GET', '/api/v1/emulator')
        expect(status == 200, 'the instance list', body)
        return body.get('emulators', body) if isinstance(body, dict) else body

    def studio(self, args, name):
        """A studio on the next automation port: `--own plusxl` or a mirror (`--mirror`, the default role)"""
        port = self.base + 60 + len(self.studios)
        process = subprocess.Popen([self.studio_binary] + self.platform() + ['--daemon', f'127.0.0.1:{self.viewers}',
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

    def deck_state(self, deck=None):
        status, body = http(self.daemon, 'GET', f'/api/v1/state?deck={deck or self.deck or "first"}')
        expect(status == 200, 'the deck state', body)
        return body

    def context(self, emulator=None):
        path = '/api/v1/surfaces/context' + (f'?instance={emulator}' if emulator else '')
        status, body = self.host('GET', path)
        expect(status == 200, 'the surface context', body)
        return body

    def position(self, emulator):
        """The host's TTD cursor (GET .../ttd/position): (frame, history)"""
        status, body = self.host('GET', f'/api/v1/emulator/{emulator}/ttd/position')
        expect(status == 200, 'the TTD position', body)
        return body['current']['frame'], body.get('history', False)

    def strip_frame(self):
        """The frame the deck's strip shows: the daemon's ttd.position readout"""
        value = self.deck_state()['surface'].get('strip', {}).get('ttd.position') or {}
        return value.get('frame')

    # ------------------------------------------------------------------ the deck, as a finger on it
    def key(self, row, col, studio=None, action='click'):
        status, body = http(studio or self.input, 'POST', '/api/v1/input/key',
                            {'row': row, 'col': col, 'action': action})
        expect(status == 200, f'key [{row}, {col}] pressed', body)

    def dial(self, index, ticks, studio=None):
        status, body = http(studio or self.input, 'POST', '/api/v1/input/dial',
                            {'index': index, 'action': 'turn', 'ticks': ticks})
        expect(status == 200, f'dial {index} turned', body)

    def navigate(self, node=None):
        body = {'deck': self.deck, 'to': 'node', 'node': node} if node else {'deck': self.deck, 'to': 'home'}
        status, answer = http(self.daemon, 'POST', '/api/v1/navigate', body)
        expect(status == 200, f'navigate to {node or "home"}', answer)
        return answer

    def own_deck_serial(self):
        """The serial of the own studio's Stream Deck + XL (a viewer's own deck)"""
        status, body = http(self.daemon, 'GET', '/api/v1/decks')
        expect(status == 200, 'the decks', body)
        return next((d['serial'] for d in body.get('decks', []) if not d.get('real')
                     and d.get('model') == 'Stream Deck + XL'), None)

    def path(self):
        return self.deck_state()['path']

    def live_again(self, emulator):
        """Out of the history and running: resume_live cuts the history at the jog's point, the pause the jog took
        stays, run.resume ends it"""
        self.action('ttd.resume_live', emulator=emulator)
        # run.state is the published state: the pause may show a moment after the answer
        deadline = time.monotonic() + 1.0
        while time.monotonic() < deadline:
            if self.readouts(['run.state'], emulator)['run.state'].get('state') == 'paused':
                self.action('run.resume', emulator=emulator)
                return
            time.sleep(0.05)

    def history(self, emulator, frames=150):
        """A recorded TTD history of at least `frames` frames, live again, the recording stopped"""
        if not self.position(emulator)[1]:
            self.action('ttd.start', emulator=emulator)
            wait(lambda: self.readouts(['ttd.status'], emulator)['ttd.status'].get('frames', 0) >= frames,
                 f'{frames} frames recorded', 30, 0.2)
            self.action('ttd.stop', emulator=emulator)
        expect(self.position(emulator)[1], 'a TTD history')


# ---------------------------------------------------------------------- checks
CHECKS = []

# The product scenario's home (data/streamdeck/scenarios/src/unreal.spec.yaml): the keys and dials the checks use
HOME_PAUSE = (0, 0)    # run.pause_toggle: always available (set.speed is not while TTD holds a history), twice = back
HOME_DEBUG = (3, 0)    # the Debugger menu (context debug)
DIAL_FRAMES = 0        # ttd.jog_frames, on home and on Time
SCRUB_SECONDS = 60     # ACC-1's criterion; --scrub-seconds shortens it
HUMAN_DIAL = False     # --surface-dial: ACC-1's turns come from a person on the Surface Dial, not the deck's dial


def check(name):
    def register(function):
        CHECKS.append((name, function))
        return function
    return register


def followed(rig):
    status, body = rig.host('GET', '/api/v1/surfaces/target')
    expect(status == 200, 'the followed instance', body)
    return body['target']['instance']


def activity_since(rig, since, emulator=None):
    path = f'/api/v1/surfaces/activity?since={since}' + (f'&instance={emulator}' if emulator else '')
    status, body = rig.host('GET', path)
    expect(status == 200, 'the activity feed', body)
    return body.get('entries', []), body.get('last', since)


def rest_requests(rig):
    return rig.readouts(['host.clients'])['host.clients']['rest']['requests']


@check('acc1')
def acc1_scrub(rig):
    """UC-1: scrub the TTD with a dial; at every rest the strip shows the frame the host is at; the daemon sends no
    HTTP request to the host meanwhile (its actions and readouts go over its one WebSocket)"""
    emulator = followed(rig)
    rig.navigate()
    rig.history(emulator, 300)
    if HUMAN_DIAL:
        # the Dial's profile acts in `ttd`; the deck's home sets `play` (the knob does nothing there), so the run sets
        # `ttd` as a person would from the host and gives home's `play` back after
        rig.action('ctx.set', 'ttd', emulator=emulator)
        wait(lambda: rig.context(emulator)['context'] == 'ttd', 'the instance in ttd', 2)
        try:
            return acc1_surface_dial(rig, emulator)
        finally:
            rig.action('ctx.set', 'play', emulator=emulator)
    rng = random.Random(1)
    # host.clients counts every REST request; of the two reads that bracket the window exactly one falls inside it
    before = rest_requests(rig)
    rig.host_requests = 0
    rests, worst, deadline = 0, 0.0, time.monotonic() + SCRUB_SECONDS
    while time.monotonic() < deadline:
        for _ in range(rng.randint(1, 6)):
            rig.dial(DIAL_FRAMES, rng.choice([-4, -3, -2, -1, 1, 2, 3, 4]))
            time.sleep(rng.uniform(0.01, 0.06))
        rested = time.monotonic()

        def agree():
            host = rig.position(emulator)[0]
            strip = rig.strip_frame()
            return host == strip and rig.position(emulator)[0] == host and (host, strip)
        wait(agree, f'rest {rests + 1}: the strip shows the host\'s frame', 2.0, 0.02)
        worst = max(worst, time.monotonic() - rested)
        rests += 1
    mine = rig.host_requests
    after = rest_requests(rig)
    daemon_requests = after - before - mine - 1
    rig.live_again(emulator)
    expect(rests >= 10, 'at least 10 rests', rests)
    expect(daemon_requests <= 0, 'the daemon sent no HTTP request to the host while scrubbing', daemon_requests)
    notes = [f'{rests} rests in {SCRUB_SECONDS} s, the strip agreed within {worst * 1000:.0f} ms at worst; '
             f'host REST requests {after - before - 1}, all {mine} the script\'s own']
    if SCRUB_SECONDS < 60:
        notes.append(f'shortened: the criterion is 60 s (--scrub-seconds {SCRUB_SECONDS})')
    notes.append('the Surface Dial half needs the hardware: the dial here is the deck\'s, the host\'s cursor stands '
                 'in for the Dial\'s result')
    return notes


def acc1_surface_dial(rig, emulator):
    """ACC-1's Surface Dial half: a person turns the Dial (unreal-qt's host input, its debug profile's ttd.seek_by);
    a rest is the host's cursor standing still for 250 ms after a move; at each the deck's strip shows that frame"""
    def names():
        status, body = rig.host('GET', '/api/v1/controllers/devices')
        expect(status == 200, 'the host devices', body)
        return [d.get('name') for d in body.get('devices') or []
                if 'surface-dial' in json.dumps(d).lower()], [d.get('name') for d in body.get('devices') or []]

    if not names()[0]:  # the Dial sleeps when idle and is gone from the host until touched
        print('        TOUCH THE DIAL: it sleeps; waiting for it (120 s)', flush=True)
    wait(lambda: names()[0], 'unreal-qt sees the Surface Dial', 120, 0.5)
    _, since = activity_since(rig, 0, emulator)
    start = rig.position(emulator)[0]
    print(f'        TURN THE DIAL NOW: {SCRUB_SECONDS} s from the first step (frame {start})', flush=True)

    def jogging():  # the first step takes the machine into its history and holds it there
        status, body = rig.host('GET', f'/api/v1/emulator/{emulator}/ttd/position')
        return status == 200 and body.get('paused') and body['current']['frame'] != start
    wait(jogging, 'the Dial takes the machine into its history', 180, 0.02)
    before = rest_requests(rig)
    rig.host_requests = 0
    deadline = time.monotonic() + SCRUB_SECONDS
    rests, worst, last, still = 0, 0.0, rig.position(emulator)[0], time.monotonic()
    moved = True
    while time.monotonic() < deadline:
        time.sleep(0.02)
        frame = rig.position(emulator)[0]
        if frame != last:
            last, still, moved = frame, time.monotonic(), True
            continue
        if not moved or time.monotonic() - still < 0.25:
            continue
        rested = still

        def agree():
            host = rig.position(emulator)[0]
            strip = rig.strip_frame()
            return host == strip and rig.position(emulator)[0] == host and (host, strip)
        host, _ = wait(agree, f'rest {rests + 1}: the strip shows the host\'s frame', 2.0, 0.02)
        worst = max(worst, time.monotonic() - rested - 0.25)
        rests += 1
        moved, last = False, host
    mine = rig.host_requests
    after = rest_requests(rig)
    daemon_requests = after - before - mine - 1
    seeks = []
    while True:  # the feed pages 64 entries at a time; the ring may have dropped the oldest
        entries, last_seq = activity_since(rig, since, emulator)
        seeks += [e for e in entries if e.get('id') == 'ttd.seek_by']
        if not entries or last_seq == since:
            break
        since = last_seq
    sources = sorted({e.get('surface') or '?' for e in seeks})
    rig.live_again(emulator)
    expect(rests >= 10, 'at least 10 rests', rests)
    expect(daemon_requests <= 0, 'the daemon sent no HTTP request to the host while scrubbing', daemon_requests)
    return [f'Surface Dial ({dials[0].get("name")}): {rests} rests in {SCRUB_SECONDS} s, the strip agreed within '
            f'{worst * 1000:.0f} ms of the rest at worst; {sum(e.get('merged', 1) for e in seeks)} ttd.seek_by in the feed from {", ".join(sources)}; '
            f'host REST requests {after - before - 1}, all {mine} the script\'s own']


@check('acc2')
def acc2_context(rig):
    """UC-2 / UC-4: a context change from either side switches the other before its next input"""
    emulator = followed(rig)
    rig.navigate()
    wait(lambda: rig.context(emulator)['context'] == 'play', 'the home page sets play', 2)
    # the deck to the host: entering the Debugger menu sets debug. Timed by the host's own clock (the activity feed's
    # t_ms of the ctx.set) from the key event it followed: a menu key acts on down or on up, and the studio's input
    # call returns only after its window took the event
    _, since = activity_since(rig, 0, emulator)
    pressed = time.time()
    rig.key(*HOME_DEBUG, action='down')
    time.sleep(0.08)
    released = time.time()
    rig.key(*HOME_DEBUG, action='up')
    wait(lambda: rig.context(emulator)['context'] == 'debug', 'the deck\'s Debugger menu sets debug on the host', 2,
         0.01)
    entry = wait(lambda: next((e for e in activity_since(rig, since, emulator)[0]
                               if e['id'] == 'ctx.set' and e.get('value', {}).get('text') == 'debug'), None),
                 'the ctx.set in the activity feed', 2, 0.01)
    changed = entry['t_ms'] / 1000.0
    deck_to_host = changed - (released if changed >= released else pressed)
    rig.navigate()
    wait(lambda: rig.context(emulator)['context'] == 'play', 'back home sets play again', 2, 0.01)
    # the host to the deck: another surface sets the context, the deck goes to the menu declaring it
    started = time.monotonic()
    rig.action('ctx.set', 'debug', emulator=emulator)
    wait(lambda: rig.path()[-1] == 'Debugger', 'ctx debug from the WebAPI takes the deck to the Debugger', 2, 0.01)
    host_to_deck = time.monotonic() - started
    rig.action('ctx.set', 'play', emulator=emulator)
    wait(lambda: rig.path() == ['Home'], 'ctx play takes it home', 2, 0.01)
    # "before its next input": a hand's next press is >= 150 ms away
    expect(deck_to_host < 0.15 and host_to_deck < 0.15, 'each side follows within 150 ms',
           f'deck->host {deck_to_host * 1000:.0f} ms, host->deck {host_to_deck * 1000:.0f} ms')
    return [f'deck -> host {deck_to_host * 1000:.0f} ms, host -> deck {host_to_deck * 1000:.0f} ms '
            '(deck -> host: the key event to the host\'s ctx.set by its clock; host -> deck: the request to the '
            'deck\'s page seen, polling included)']


@check('acc3')
def acc3_targets(rig):
    """UC-5 / UC-6 with two instances: the deck's target switches with the focused view; a pin holds; the deck shows
    its target"""
    first = followed(rig)
    created = rig.session.call('emulator_manage', {'action': 'create', 'model': '48K'}).structured['id']
    rig.release_mcp()
    rig.mute()
    view = None
    try:
        def deck_target():
            return rig.deck_state()['surface'].get('target') or {}

        def shows(instance):
            t = deck_target()
            return t.get('instance') == instance and t.get('model') and t.get('index') is not None and t
        # unreal-qt's main window took the new instance; a second window (an automation-plane view) shows the first
        status, body = rig.host('POST', '/api/v1/surfaces/views', {'name': 'acceptance wall', 'active': first})
        expect(status == 200, 'a second view', body)
        view = body['view']['id']
        status, body = rig.host('PUT', f'/api/v1/surfaces/views/{view}', {'active': first, 'activate': True})
        expect(status == 200, 'the view to the front', body)
        target = wait(lambda: shows(first), 'the deck targets the front view\'s instance', 2, 0.02)
        status, body = rig.host('PUT', '/api/v1/surfaces/views/v1', {'activate': True})
        expect(status == 200, 'the main window to the front', body)
        main_active = followed(rig)
        expect(main_active != first, 'the main window shows the other instance', main_active)
        other = wait(lambda: shows(main_active), 'focus moves: the deck follows', 2, 0.02)
        notes = [f'follow: #{target["index"]} {target["model"]} -> #{other["index"]} {other["model"]} with the focus']
        # the pin: the deck's surface session in TargetState (targeting.md §4), pinned to the first instance
        status, body = rig.host('GET', '/api/v1/surfaces/sessions')
        expect(status == 200, 'the surface sessions', body)
        sessions = body.get('sessions', [])
        session = next((x for x in sessions if x.get('serial') == rig.deck), None)
        expect(session is not None, 'the deck is a surface session the host can pin (GET /api/v1/surfaces/sessions)',
               sessions)
        status, body = rig.host('PUT', f'/api/v1/surfaces/sessions/{session["id"]}/pin',
                                {'target': f'instance:{first}'})
        expect(status == 200, 'the deck pinned to the first instance', body)
        try:
            wait(lambda: shows(first), 'the pinned deck targets the first instance', 2, 0.02)
            rig.host('PUT', f'/api/v1/surfaces/views/{view}', {'activate': True})
            rig.host('PUT', '/api/v1/surfaces/views/v1', {'activate': True})
            time.sleep(0.3)
            expect(shows(first), 'the pin holds while the focus moves', deck_target())
        finally:
            rig.host('PUT', f'/api/v1/surfaces/sessions/{session["id"]}/pin', {'target': 'follow'})
        notes.append('pin: held while the focus moved')
        return notes
    finally:
        if view:
            rig.host('DELETE', f'/api/v1/surfaces/views/{view}')
        rig.session.call('emulator_manage', {'action': 'destroy', 'target': created}, ok=False)
        rig.release_mcp()


def scenario_file(directory, name, model, key_action, value):
    """A one-key scenario (format v2) for a deck of another model"""
    path = os.path.join(directory, f'{name}.json')
    with open(path, 'w') as f:
        json.dump({'format': 'streamdeck-scenario', 'model': model, 'name': name,
                   'root': {'id': 'home', 'type': 'menu', 'title': 'Home', 'items': [
                       {'id': 'k', 'type': 'action', 'title': 'K', 'at': [0, 1],
                        'do': dict({'kind': 'action', 'id': key_action},
                                   **({'value': value} if value is not None else {}))}]}}, f)
    return path


def presses_counted(rig, press, emulator, what, action='run.pause_toggle'):
    """`press` once: the host's activity feed gets exactly one `action` for it; a second press puts the state back"""
    def pressed():
        return [e for e in activity_since(rig, since, emulator)[0] if e['id'] == action]
    _, since = activity_since(rig, 0, emulator)
    press()
    wait(pressed, f'{what}: the press reaches the host', 3, 0.05)
    time.sleep(0.3)  # a second copy would come within this
    entries = pressed()
    expect(len(entries) == 1 and entries[0].get('merged', 1) == 1, f'{what}: the press reaches the runtime once',
           entries)
    _, since = activity_since(rig, 0, emulator)
    press()
    wait(pressed, f'{what}: the second press puts the state back', 3, 0.05)


@check('acc4')
def acc4_viewers(rig):
    """A deck and two studio windows (a mirror, an own deck of another model): the mirror shows the deck's last
    images; a press from any of them reaches the runtime once"""
    emulator = followed(rig)
    rig.navigate()
    notes = []
    presses_counted(rig, lambda: rig.key(*HOME_PAUSE), emulator, 'the deck')
    neo = rig.studio(['--own', 'neo'], 'studio-neo')
    serial = http(neo, 'GET', '/api/v1/connection')[1].get('deck')
    expect(serial, 'the Neo has a deck of its own', serial)
    path = scenario_file(rig.folder, 'acc4-neo', 'neo', 'run.pause_toggle', None)
    status, body = http(rig.daemon, 'PUT', f'/api/v1/decks/{serial}/scenario', {'file': path, 'watch': False})
    expect(status == 200, 'the Neo plays a one-key scenario', body)
    wait(lambda: http(rig.daemon, 'GET', f'/api/v1/state?deck={serial}')[1].get('scenario') == 'acc4-neo',
         'the Neo\'s runtime has the scenario loaded', 5, 0.05)
    presses_counted(rig, lambda: rig.key(0, 1, studio=neo), emulator, 'the Neo (its own runtime)')
    notes.append('presses: the deck and an own Neo, one action each')
    if not rig.live:
        raise Skip('; '.join(notes) + '; the mirror half needs a real deck (the daemon mirrors USB decks only): '
                   '--live')
    # the real deck: rig.input is a mirror of it already; a second mirror joins late and gets the deck's last images
    late = rig.studio([], 'studio-mirror-late')
    # the machine paused, so the strip's live counters stand; the LCD as the deck drew it (area=deck adds the
    # studio's own knob angles, which follow the turns sent through that window)
    rig.action('run.pause', emulator=emulator)
    try:
        time.sleep(1.0)
        shots = []
        for port in (rig.input, late):
            status, png = http(port, 'GET', '/api/v1/screenshot?area=lcd', raw=True)
            expect(status == 200, 'a mirror screenshot', status)
            shots.append(png)
    finally:
        rig.action('run.resume', emulator=emulator)
    expect(shots[0] == shots[1], 'two mirrors (one live from the start, one fed the deck\'s last images at join) '
           'show the same pixels', f'{len(shots[0])} vs {len(shots[1])} bytes')
    notes.append('mirrors: the late one equals the live one byte for byte')
    return notes


ABSOLUTE = re.compile(r'(^|[\s:"\'\[,])(/(Users|home|Volumes|tmp|var|opt|private)/|~/|[A-Za-z]:[\\/]|file://)')
FORBIDDEN = re.compile(r'kind:\s*(shell|open|keys|webapi)\b|"kind":\s*"(shell|open|keys|webapi)"')


@check('acc5')
def acc5_default_scenario(rig):
    """The default scenario: what a deck plays without a file, no absolute path, no shell / open / keys / webapi
    step, every action id known to the catalogue"""
    status, decks = http(rig.daemon, 'GET', '/api/v1/decks')
    expect(status == 200, 'the decks', decks)
    deck = next(d for d in decks['decks'] if d['serial'] == rig.deck)
    product = os.path.join(rig.data, 'streamdeck', 'scenarios', 'unreal.deck.yaml')
    expect(os.path.realpath(deck['scenario']) == os.path.realpath(product) and deck.get('start_scenario'),
           'the Stream Deck + XL plays the product scenario as its start scenario', deck.get('scenario'))
    files = [product, os.path.join(rig.data, 'streamdeck', 'scenarios', 'shared', 'speed.deck.yaml')]
    for name in files:
        with open(name) as f:
            for number, line in enumerate(f, 1):
                expect(not ABSOLUTE.search(line), f'no absolute path ({os.path.basename(name)}:{number})',
                       line.strip())
                expect(not FORBIDDEN.search(line),
                       f'no shell / open / keys / webapi step ({os.path.basename(name)}:{number})', line.strip())
    catalogue = os.path.join(rig.data, 'streamdeck', 'catalogue.json')
    checked = subprocess.run([os.path.join(rig.bin, 'unreal-streamdeck'), 'scenario', 'check', product,
                              '--catalogue', catalogue], capture_output=True, text=True, timeout=60)
    output = (checked.stdout + checked.stderr).strip()
    expect(checked.returncode == 0, 'unreal-streamdeck scenario check passes', output)
    expect('warning' not in output.lower(), 'the check has no warning (an unknown action id is one)', output)
    # the actions the deck runs: each known to the running host
    status, body = rig.host('GET', '/api/v1/controllers/actions')
    expect(status == 200, 'the host\'s catalogue', body)
    known = {a['id'] for a in (body.get('actions') or [])}
    expect(known, 'the host lists its actions', str(body)[:200])
    with open(product) as f:
        used = set(re.findall(r'^\s*id:\s*([a-z][a-z0-9_]*\.[a-z0-9_.]+)\s*$', f.read(), re.M))
    unknown = sorted(used - known)
    expect(not unknown, 'every action id is in the host\'s catalogue', unknown)
    if rig.package:
        # the daemon the package's unreal-qt started: its own helper, not one from the build tree or PATH
        pid = rig.health().get('pid')
        expect(pid, 'the daemon reports its pid')
        started = process_path(pid)
        exe = '.exe' if os.name == 'nt' else ''
        expect(os.path.normcase(os.path.realpath(started)) ==
               os.path.normcase(os.path.realpath(os.path.join(rig.bin, 'unreal-streamdeck' + exe))),
               'the package\'s unreal-qt started the package\'s daemon', started)
        return [f'{len(used)} action ids, all known; the check clean; the daemon {started}']
    raise Skip(f'passed on the build tree\'s data ({len(used)} action ids, all known; the check clean); a fresh '
               'install of each platform package is the packaging run\'s')


@check('acc6')
def acc6_daemon_killed(rig):
    """Kill -9 the daemon during a scrub: the emulation keeps its frame time; unreal-qt restarts the daemon; the deck
    comes back on the same page"""
    emulator = followed(rig)
    rig.history(emulator, 150)
    rig.navigate('time')
    page = rig.path()
    expect(page[-1] == 'Time', 'the deck on the Time menu', page)
    stop = threading.Event()
    rng = random.Random(6)

    def scrub():
        while not stop.is_set():
            try:
                http(rig.input, 'POST', '/api/v1/input/dial',
                     {'index': DIAL_FRAMES, 'action': 'turn', 'ticks': rng.choice([-2, -1, 1, 2])}, timeout=2)
            except OSError:
                pass
            time.sleep(0.03)
    scrubber = threading.Thread(target=scrub, daemon=True)
    scrubber.start()
    time.sleep(1.0)
    old = rig.health().get('pid')
    expect(old, 'the daemon\'s pid')
    os.kill(old, signal.SIGKILL)
    killed = time.monotonic()
    time.sleep(0.2)
    stop.set()
    scrubber.join(3)
    # the emulation, live again, at its frame rate while the daemon is gone and comes back
    rig.live_again(emulator)
    # the window opens once the machine is up to speed again: a resume takes about half a second to reach it (the
    # same with the daemon alive), the daemon's restart falls in the window
    def up_to_speed():
        stats = rig.readouts(['emulator.stats'], emulator)['emulator.stats']
        return stats['state'] == 'running' and stats['fps'] >= 0.95 * stats['nominal_fps']
    wait(up_to_speed, 'the machine runs at speed again', 5, 0.02)
    # the count from the live position: emulator.stats publishes its frame twice a second, 25 frames a step
    frame0 = rig.position(emulator)[0]
    samples, timeline, slowest, t0 = [], [], 0.0, time.monotonic()
    while time.monotonic() - t0 < 3.0:
        started = time.monotonic()
        stats = rig.readouts(['emulator.stats'], emulator)['emulator.stats']
        slowest = max(slowest, time.monotonic() - started)
        samples.append(stats['frame_time_us']['max'])
        timeline.append(f'{started - t0:.1f}:{rig.position(emulator)[0] - frame0}')
        time.sleep(0.1)
    frames = rig.position(emulator)[0] - frame0
    seconds = time.monotonic() - t0
    nominal = stats['nominal_fps']
    expect(frames >= 0.95 * nominal * seconds, 'the emulation keeps its frame rate',
           f'{frames} frames in {seconds:.1f} s (nominal {nominal:.2f}/s); s:frames {" ".join(timeline)}; '
           f'health pid {rig.health().get("pid")}')
    expect(max(samples) < 1e6 / nominal, 'no frame takes longer than its budget', f'{max(samples):.0f} us')
    expect(slowest < 0.2, 'the WebAPI answers meanwhile', f'{slowest * 1000:.0f} ms')
    new = wait(lambda: rig.health().get('pid') not in (None, old) and rig.health()['pid'],
               'unreal-qt restarts the daemon', 15, 0.1)
    restarted = time.monotonic() - killed
    wait(lambda: http(rig.input, 'GET', '/api/v1/connection')[1].get('state') == 'joined', 'the studio rejoins', 15,
         0.1)
    if not rig.live:
        rig.deck = wait(rig.own_deck_serial, 'the own deck is back', 10, 0.1)
    back = wait(lambda: rig.path(), 'the deck\'s page', 5, 0.1)
    expect(back == page, 'the deck comes back on the same page',
           f'{back}, was {page}; the daemon keeps each deck\'s menu path in deck-pages.json')
    return [f'{frames / seconds:.2f} fps over {seconds:.1f} s after the kill (nominal {nominal:.2f}), worst frame '
            f'{max(samples):.0f} us; restarted in {restarted:.1f} s (pid {old} -> {new})']


@check('acc7')
def acc7_conformance(rig):
    """The conformance suite (a virtual deck and a virtual companion) passes headless"""
    notes = []
    for name in ('streamdeck-tests', 'streamdeck-studio-tests'):
        binary = os.path.join(rig.bin, name)
        expect(os.path.exists(binary), f'{name} is built', binary)
        env = dict(os.environ, QT_QPA_PLATFORM='offscreen')
        started = time.monotonic()
        result = subprocess.run([binary], capture_output=True, text=True, timeout=600, env=env)
        # the summary is stdout's last line; stderr carries Qt's notes
        tail = result.stdout.strip().splitlines()[-1:] or (result.stderr.strip().splitlines()[-1:] or [''])
        expect(result.returncode == 0, f'{name} passes headless', tail[0])
        notes.append(f'{name}: {tail[0]} ({time.monotonic() - started:.0f} s)')
    with open(os.path.join(REPO, '.github', 'workflows', 'cmake-ci.yml')) as f:
        ci = f.read()
    jobs = [j for j in ('build-streamdeck-linux', 'build-streamdeck-macos', 'build-streamdeck-windows-msvc',
                        'build-streamdeck-windows-mingw') if j in ci]
    expect(len(jobs) == 4, 'CI runs both suites on Linux, macOS and Windows', jobs)
    notes.append(f'CI jobs: {", ".join(jobs)} (their results are CI\'s)')
    return notes


def run(args):
    global SCRUB_SECONDS, HUMAN_DIAL
    SCRUB_SECONDS = args.scrub_seconds
    HUMAN_DIAL = args.surface_dial
    folder = os.path.join(REPO, 'scratch', 'streamdeck-acceptance', time.strftime('%Y%m%d-%H%M%S'))
    os.makedirs(folder)
    rig = Rig(args.build, args.port_base, folder, args.live, args.package)
    failed = passed = skipped = 0
    try:
        rig.start()
        if not args.live:
            rig.own = rig.input = rig.studio(['--own', 'plusxl'], 'studio-own')
            rig.deck = rig.own_deck_serial()
        elif args.only != ['none']:
            rig.input = rig.studio([], 'studio-mirror')  # the real deck's input goes through it
            rig.deck = http(rig.input, 'GET', '/api/v1/connection')[1].get('deck')
        print(f'rig: unreal-qt {rig.webapi}, daemon {rig.daemon} (pid {rig.daemon_pid}), studio {rig.input}, '
              f'deck {rig.deck}; {folder}', flush=True)
        for name, function in CHECKS:
            if args.only and name not in args.only:
                continue
            started = time.monotonic()
            notes = []
            try:
                notes = function(rig) or []
                passed += 1
                print(f'  ok    {name} ({time.monotonic() - started:.1f} s)')
            except Skip as reason:
                skipped += 1
                print(f'  skip  {name} ({time.monotonic() - started:.1f} s): {reason}')
            except Failure as error:
                failed += 1
                print(f'  FAIL  {name}: {error}')
            for note in notes:
                print(f'        {note}')
            sys.stdout.flush()
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
    print(f'{passed} passed, {failed} failed, {skipped} skipped; logs {folder}')
    return failed


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build', default='cmake-build-agent-release', help='the build folder (bin/ inside)')
    parser.add_argument('--port-base', type=int, default=9400, help='unreal-qt WebAPI port; the rest follow it')
    parser.add_argument('--only', type=lambda v: v.split(','), help='checks to run, comma separated')
    parser.add_argument('--live', action='store_true',
                        help='visible unreal-qt, the daemon opens the attached deck (implies --keep)')
    parser.add_argument('--keep', action='store_true', help='keep the rig running at the end')
    parser.add_argument('--package', help='an installed package to run instead of the build tree (ACC-5): the folder '
                        'holding UnrealNG.app, the Windows ZIP\'s or Linux tarball\'s top, an AppImage\'s usr/bin')
    parser.add_argument('--surface-dial', action='store_true',
                        help="ACC-1 with a person turning the Surface Dial (with --live, --only acc1)")
    parser.add_argument('--scrub-seconds', type=int, default=SCRUB_SECONDS, help="ACC-1's scrub (the criterion: 60)")
    sys.exit(run(parser.parse_args()))


if __name__ == '__main__':
    main()
