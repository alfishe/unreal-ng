#!/usr/bin/env python3
"""Live check of the plane on every GUI program (design phase 6): an offscreen unreal-qt on ports of its own with a
fresh muted machine, unreal-debugger and unreal-screen-viewer connected to it, and an offscreen unreal-videowall on
ports of its own. `unreal-gui list` shows the four programs; the `screen` interface runs on unreal-qt's main screen
and on the screen viewer, `tilegrid` and `window` on the video wall.

    python3 tools/guicontrol/verify_programs.py [--port-base 28800] [--wall-port-base 28810]

Exit code 0 when every check passed (about 20 s).
"""

import argparse
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
BIN = os.path.join(ROOT, 'cmake-build-agent-release', 'bin')
sys.path.insert(0, os.path.join(ROOT, 'tools', 'verification', 'mcp'))
import mcplib  # noqa: E402
from guicontrol import Gui, GuiError  # noqa: E402

PROGRAMS = ('unreal-qt', 'unreal-debugger', 'unreal-screen-viewer', 'unreal-videowall')
failures = []


def expect(condition, what, detail=''):
    print(('PASS ' if condition else 'FAIL ') + what + ('' if condition else f'\n     {detail}'))
    if not condition:
        failures.append(what)


def refused(gui, method, error, target=None, **params):
    try:
        gui.call(method, target=target, **params)
    except GuiError as e:
        return e.error == error
    return False


def until(fn, seconds=10.0):
    end = time.time() + seconds
    while True:
        value = fn()
        if value or time.time() > end:
            return value
        time.sleep(0.2)


def connect(pid, seconds=20.0):
    """The host of the process once it lists"""
    end = time.time() + seconds
    while time.time() < end:
        try:
            return Gui.connect(host=str(pid))
        except (OSError, GuiError):
            time.sleep(0.25)
    raise SystemExit(f'process {pid}: no GUI control host')


def http(port, method, path, body=None):
    request = urllib.request.Request(f'http://127.0.0.1:{port}{path}', method=method,
                                     data=json.dumps(body).encode() if body is not None else None,
                                     headers={'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(request, timeout=10) as reply:
            return reply.status, json.loads(reply.read() or b'{}')
    except urllib.error.HTTPError as e:
        return e.code, {}


def mute_all(port):
    _, listing = http(port, 'GET', '/api/v1/emulator')
    for emulator in listing.get('emulators', []):
        http(port, 'PUT', f"/api/v1/emulator/{emulator['id']}/audio/mixer/master", {'muted': True})
    return [e['id'] for e in listing.get('emulators', [])]


def conform_screen(gui, name, machine, endpoint):
    """The contract of `screen`: zoom, full screen and the machine shown"""
    say = lambda what: f'{name}: {what}'  # noqa: E731
    state = lambda: gui.state('main/screen')  # noqa: E731

    shown = until(lambda: state() if state().get('instance') else None)
    expect(bool(shown) and shown['instance'] == {'endpoint': endpoint, 'id': machine},
           say('main/screen names the machine and its WebAPI'), state())
    expect('screen' in gui.state('main/screen')['interfaces'], say('the view implements screen'))
    expect(gui.call('screen.zoom', target='main/screen', factor=2) == {'zoom': 2}, say('screen.zoom 2'))
    zoom = until(lambda: state()['state']['zoom'])
    expect(isinstance(zoom, (int, float)) and zoom >= 1, say('the state shows the zoom'), state())
    gui.call('screen.zoom', target='main/screen', factor=1)
    expect(until(lambda: state()['state']['zoom'] is not None and state()['state']['zoom'] < zoom) or zoom <= 1.05,
           say('screen.zoom 1: the picture smaller'), (zoom, state()))
    expect(refused(gui, 'screen.zoom', 'bad_request', target='main/screen', factor=5), say('factor 5 is refused'))
    expect(refused(gui, 'screen.zoom', 'bad_request', target='main/screen', factor=1.5),
           say('a factor not whole is refused'))
    expect(gui.call('screen.fullscreen', target='main/screen', on=True) == {'fullscreen': True},
           say('screen.fullscreen on'))
    expect(until(lambda: state()['state']['fullscreen'] is True), say('state fullscreen true'), state())
    expect(gui.call('screen.fullscreen', target='main/screen') == {'fullscreen': False},
           say('screen.fullscreen without on flips it off'))
    expect(until(lambda: state()['state']['fullscreen'] is False), say('state fullscreen false'), state())
    expect(gui.call('screen.zoom', target=f'screen@{machine}', factor=1) == {'zoom': 1},
           say('screen@<instance> reaches the view'))


def conform_wall(gui, wall_port, mcp):
    """`tilegrid` (focus, layout) and `window` on the video wall"""
    tilegrid = lambda: gui.state('main/tilegrid')['state']  # noqa: E731
    # The wall starts empty; a machine created through its MCP becomes a tile, the first one takes the focus
    for _ in range(2 - len(tilegrid().get('tiles') or [])):
        mcp.call('emulator_manage', {'action': 'create', 'model': 'PENTAGON'})
        mute_all(wall_port)
    until(lambda: len(tilegrid().get('tiles') or []) >= 2)
    mute_all(wall_port)
    tiles = tilegrid()['tiles']
    expect(len(tiles) >= 2, f'video wall: tiles with their machines ({len(tiles)})', tilegrid())
    expect(tilegrid().get('layout') == 'windowed' and tilegrid().get('focused') == 0,
           'video wall: windowed, the first tile focused', tilegrid())

    expect(gui.call('tilegrid.focus', tile=1) == {'focused': 1}, 'tilegrid.focus 1')
    mute_all(wall_port)  # the focused tile plays the sound
    expect(until(lambda: tilegrid().get('focused') == 1), 'state focused 1', tilegrid())
    expect(gui.call('tilegrid.focus', tile=0) == {'focused': 0}, 'tilegrid.focus 0: the focus moves')
    mute_all(wall_port)
    expect(gui.call('tilegrid.focus', tile=0) == {'focused': 0}, 'tilegrid.focus 0 again: stays (no toggle)')
    expect(until(lambda: tilegrid().get('focused') == 0), 'state focused 0', tilegrid())
    expect(refused(gui, 'tilegrid.focus', 'bad_request', tile=len(tiles) + 5), 'a tile past the wall is refused')

    expect(gui.call('tilegrid.layout', preset='frameless') == {'layout': 'frameless'}, 'tilegrid.layout frameless')
    expect(until(lambda: tilegrid().get('layout') == 'frameless'), 'state layout frameless', tilegrid())
    time.sleep(0.3)  # the wall lays its tiles out 100 ms after the switch
    mute_all(wall_port)
    expect(gui.call('tilegrid.layout', preset='fullscreen') == {'layout': 'fullscreen'},
           'tilegrid.layout fullscreen (from frameless)')
    expect(until(lambda: gui.state('main')['state'].get('visible') and tilegrid().get('layout') == 'fullscreen'),
           'state layout fullscreen', tilegrid())
    time.sleep(0.3)
    mute_all(wall_port)
    expect(gui.call('tilegrid.layout', preset='windowed') == {'layout': 'windowed'}, 'tilegrid.layout windowed')
    expect(until(lambda: tilegrid().get('layout') == 'windowed'), 'state layout windowed', tilegrid())
    time.sleep(0.3)
    mute_all(wall_port)
    expect(refused(gui, 'tilegrid.layout', 'bad_request', preset='mosaic'), 'an unknown preset is refused')

    gui.call('window.move', target='main', x=40, y=60, w=700, h=500)
    geometry = until(lambda: gui.state('main')['state']['geometry'])
    expect(geometry.get('w') == 700 and geometry.get('h') == 500, 'video wall: window.move resizes the window',
           geometry)


def cli(unreal_gui, *args):
    run = subprocess.run([unreal_gui, *args], capture_output=True, text=True, timeout=20)
    return run.returncode, run.stdout + run.stderr


def launch(binary, *args):
    return subprocess.Popen([binary, '-platform', 'offscreen', *args], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--unreal-qt', default=os.path.join(BIN, 'unreal-qt.app', 'Contents', 'MacOS', 'unreal-qt'))
    parser.add_argument('--unreal-debugger', default=os.path.join(BIN, 'unreal-debugger'))
    parser.add_argument('--unreal-screen-viewer', default=os.path.join(
        BIN, 'unreal-screen-viewer.app', 'Contents', 'MacOS', 'unreal-screen-viewer'))
    parser.add_argument('--unreal-videowall', default=os.path.join(
        BIN, 'unreal-videowall.app', 'Contents', 'MacOS', 'unreal-videowall'))
    parser.add_argument('--unreal-gui', default=os.path.join(BIN, 'unreal-gui'))
    parser.add_argument('--port-base', type=int, default=28800)
    parser.add_argument('--wall-port-base', type=int, default=28810)
    parser.add_argument('--out', default=os.path.join(ROOT, 'scratch', 'guicontrol-verify-programs'))
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    os.environ['UNREAL_GUI_CONTROL_DIR'] = os.path.join(args.out, 'discovery')
    endpoint = f'http://127.0.0.1:{args.port_base}'

    processes = [mcplib.launch(args.unreal_qt, args.port_base)]
    try:
        mcplib.Session(f'http://127.0.0.1:{args.port_base + 200}/mcp', 'PENTAGON').fresh_machine()
        qt = connect(processes[0].pid)
        machine = until(lambda: (qt.state('memory').get('instance') or {}).get('id'))
        processes.append(launch(args.unreal_debugger, '--connect', f'127.0.0.1:{args.port_base}',
                                '--emulator', machine))
        processes.append(launch(args.unreal_screen_viewer, '--connect', f'127.0.0.1:{args.port_base}'))
        viewer = connect(processes[2].pid)
        processes.append(mcplib.launch(args.unreal_videowall, args.wall_port_base))
        mute_all(args.wall_port_base)
        wall = connect(processes[3].pid)
        connect(processes[1].pid).close()

        code, out = cli(args.unreal_gui, 'list')
        expect(code == 0 and all(program in out for program in PROGRAMS), 'unreal-gui list shows the four programs',
               out[:2000])
        expect(viewer.list().get('endpoints') == [endpoint], 'the screen viewer names the WebAPI it watches',
               viewer.list().get('endpoints'))

        conform_screen(qt, 'unreal-qt', machine, endpoint)
        conform_screen(viewer, 'unreal-screen-viewer', machine, endpoint)
        conform_wall(wall, args.wall_port_base,
                     mcplib.Session(f'http://127.0.0.1:{args.wall_port_base + 200}/mcp', 'PENTAGON'))

        # One command across programs: the host given, then the only screen of that machine in the plane
        code, out = cli(args.unreal_gui, '--host', str(processes[2].pid), 'screen.zoom', '--target', 'main/screen',
                        'factor=2')
        expect(code == 0, 'unreal-gui --host <viewer> screen.zoom factor=2', out)
        for gui in (qt, viewer, wall):
            gui.close()
    finally:
        for process in reversed(processes):
            process.terminate()
        for process in processes:
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
    print(f'{len(failures)} failed' if failures else 'all passed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
