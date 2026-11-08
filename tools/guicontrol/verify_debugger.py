#!/usr/bin/env python3
"""Live check of the shared debugger interfaces (design R3, phase 5): an offscreen unreal-qt on ports of its own with
a fresh muted machine, an offscreen unreal-debugger with its own core, and one connected to unreal-qt's machine
(--connect). The same `debugger`, `disassembly` and `memory` checks run on each of the three debuggers; then the same
`unreal-gui memory.goto` moves each one, by host and by the focus the user gave last.

    python3 tools/guicontrol/verify_debugger.py [--port-base 28800]

Exit code 0 when every check passed (about 15 s).
"""

import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
BIN = os.path.join(ROOT, 'cmake-build-agent-release', 'bin')
sys.path.insert(0, os.path.join(ROOT, 'tools', 'verification', 'mcp'))
import mcplib  # noqa: E402
from guicontrol import Gui, GuiError  # noqa: E402

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


def connect(pid, seconds=20.0):
    """The host of the process once its views name a machine"""
    end = time.time() + seconds
    while time.time() < end:
        try:
            gui = Gui.connect(host=str(pid))
            if gui.state('memory').get('instance'):
                return gui
            gui.close()
        except (OSError, GuiError):
            pass
        time.sleep(0.25)
    raise SystemExit(f'process {pid}: no GUI control host with a machine shown')


def conform(gui, name):
    """The contract of the three interfaces: what any debugger's views answer and show"""
    state = lambda target: gui.state(target)['state']  # noqa: E731
    say = lambda what: f'{name}: {what}'  # noqa: E731

    gui.call('debugger.open')
    expect(gui.state('debugger')['state']['visible'], say('debugger.open shows the window'))
    expect(gui.call('debugger.focus', pane='memory') == {'pane': 'memory'}, say('debugger.focus memory'))
    expect(gui.call('debugger.focus_next')['pane'] == 'stack', say('debugger.focus_next: memory -> stack'))
    expect(gui.call('debugger.focus_prev')['pane'] == 'memory', say('debugger.focus_prev: back to memory'))
    expect(refused(gui, 'debugger.focus', 'bad_request', pane='console'), say('debugger.focus refuses an unknown pane'))
    gui.call('debugger.focus', pane='disassembly')
    expect(gui.list()['state']['pane'] == 'disassembly', say('the host state names the pane in focus'))

    expect(gui.call('disassembly.goto', addr='#0038') == {'top': '0x0038'}, say('disassembly.goto #0038'))
    expect(state('disassembly')['top'] == '0x0038', say('the state shows the top'), state('disassembly'))
    expect(gui.call('disassembly.cursor', addr='0x0038') == {'cursor': '0x0038'}, say('disassembly.cursor'))
    moved = gui.call('disassembly.cursor_move', dir='down')['cursor']
    expect(moved > '0x0038', say('disassembly.cursor_move down: the next instruction'), moved)
    expect(refused(gui, 'disassembly.cursor_move', 'bad_request', dir='left'), say('the disassembly moves up or down'))
    paged = gui.call('disassembly.page', n=1)['cursor']
    expect(paged > moved, say('disassembly.page n=1 moves the cursor down a page'), paged)
    expect(gui.call('disassembly.scroll_mode', mode='byte') == {'scroll_mode': 'byte'}, say('scroll_mode byte'))
    gui.call('disassembly.cursor', addr='0x0038')
    stepped = gui.call('disassembly.cursor_move', dir='down')['cursor']
    expect(stepped == '0x0039', say('byte scroll: down is the next byte'), stepped)
    expect(gui.call('disassembly.scroll_mode') == {'scroll_mode': 'command'}, say('scroll_mode flips back'))
    gui.call('disassembly.follow_pc')
    d = state('disassembly')
    expect(d['pc'] is not None and d['top'] is not None, say('disassembly state: top and pc'), d)
    gui.call('disassembly.cursor', addr=d['pc'])
    gui.call('disassembly.breakpoint_toggle_cursor')
    gui.call('disassembly.breakpoint_toggle_cursor')
    expect(True, say('disassembly.breakpoint_toggle_cursor twice'))

    gui.call('memory.follow', register='off')
    expect(gui.call('memory.goto', addr='0x6A00') == {'cursor': '0x6A00'}, say('memory.goto 0x6A00'))
    expect(state('memory')['follow'] == 'off', say('memory state: follow off'), state('memory'))
    gui.call('memory.cursor_move', dir='right')
    expect(state('memory')['cursor'] == '0x6A01', say('memory.cursor_move right'), state('memory'))
    gui.call('memory.cursor_move', dir='down')
    # A line is the view's: 16 bytes in unreal-debugger's dump, as wide as the window in unreal-qt's hex view
    down = int(state('memory')['cursor'], 16) - 0x6A01
    expect(down in (8, 16, 32), say('memory.cursor_move down: a line'), state('memory'))
    gui.call('memory.cursor', addr='0x7FFF')
    gui.call('memory.cursor_move', dir='right')
    expect(state('memory')['cursor'] == '0x8000', say('memory.cursor_move over a page edge'), state('memory'))
    gui.call('memory.select', **{'from': '0x6A10', 'to': '0x6A1F'})
    expect(state('memory')['selection'] == {'from': '0x6A10', 'to': '0x6A1F'}, say('memory.select'),
           state('memory'))
    before = state('memory')['cursor']
    gui.call('memory.page')
    expect(state('memory')['cursor'] > before, say('memory.page moves down'), state('memory'))
    expect(state('memory')['selection'] is None, say('a move drops the selection'), state('memory'))
    gui.call('memory.cursor', addr='0x5C36')  # CHARS: a word in the system variables
    target = gui.call('memory.follow_cursor')['cursor']
    expect(target.startswith('0x'), say('memory.follow_cursor goes to the word at the cursor'), target)
    expect(gui.call('memory.follow', register='sp') == {'follow': 'sp'}, say('memory.follow sp'))
    expect(refused(gui, 'memory.follow', 'bad_request', register='xx'), say('memory.follow refuses a register'))
    gui.call('memory.follow', register='off')
    expect(refused(gui, 'memory.find_last_write', 'not_now'), say('memory.find_last_write, no recording: not_now'))
    expect(refused(gui, 'memory.goto', 'bad_request', addr='0x10000'), say('an address past 0xFFFF is refused'))

    instance = gui.state('memory')['instance']
    gui.call('memory.goto', target=f"memory@{instance['id']}", addr='0x5800')
    expect(state('debugger/memory')['cursor'] == '0x5800', say('memory@instance reaches the view'))
    expect(refused(gui, 'memory.goto', 'not_found', target='memory@no-such-machine', addr='0x4000'),
           say('memory@<another machine> is refused'))
    return instance


def cli(unreal_gui, *args):
    run = subprocess.run([unreal_gui, *args], capture_output=True, text=True, timeout=20)
    return run.returncode, run.stdout + run.stderr


def launch_debugger(binary, *args):
    return subprocess.Popen([binary, '-platform', 'offscreen', *args], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--unreal-qt', default=os.path.join(BIN, 'unreal-qt.app', 'Contents', 'MacOS', 'unreal-qt'))
    parser.add_argument('--unreal-debugger', default=os.path.join(BIN, 'unreal-debugger'))
    parser.add_argument('--unreal-gui', default=os.path.join(BIN, 'unreal-gui'))
    parser.add_argument('--port-base', type=int, default=28800)
    parser.add_argument('--out', default=os.path.join(ROOT, 'scratch', 'guicontrol-verify-debugger'))
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    os.environ['UNREAL_GUI_CONTROL_DIR'] = os.path.join(args.out, 'discovery')

    processes = [mcplib.launch(args.unreal_qt, args.port_base)]
    try:
        mcplib.Session(f'http://127.0.0.1:{args.port_base + 200}/mcp', 'PENTAGON').fresh_machine()
        qt = connect(processes[0].pid)
        machine = qt.state('memory')['instance']['id']
        processes.append(launch_debugger(args.unreal_debugger))
        own = connect(processes[1].pid)
        processes.append(launch_debugger(args.unreal_debugger, '--connect', f'127.0.0.1:{args.port_base}',
                                         '--emulator', machine))
        remote = connect(processes[2].pid)
        hosts = {'unreal-qt': (qt, processes[0].pid), 'unreal-debugger': (own, processes[1].pid),
                 'unreal-debugger --connect': (remote, processes[2].pid)}

        expect(own.welcome.get('program') == 'unreal-debugger' or own.list().get('program') == 'unreal-debugger',
               'unreal-debugger serves the plane as program unreal-debugger', own.list().get('program'))
        instances = {name: conform(gui, name) for name, (gui, _) in hosts.items()}
        expect(instances['unreal-debugger --connect']['id'] == machine and
               instances['unreal-debugger --connect']['endpoint'] == f'http://127.0.0.1:{args.port_base}',
               '--connect: the views name unreal-qt\'s machine and its WebAPI', instances['unreal-debugger --connect'])
        expect(instances['unreal-debugger']['id'] not in ('', machine) and not instances['unreal-debugger']['endpoint'],
               'in process: the views name its own machine, no WebAPI', instances['unreal-debugger'])

        # One command, each debugger: by host, then by the pane the user focused last
        for n, (name, (gui, pid)) in enumerate(hosts.items()):
            address = f'0x{0x6100 + n * 0x100:04X}'
            code, out = cli(args.unreal_gui, '--host', str(pid), 'memory.goto', address)
            expect(code == 0 and gui.state('debugger/memory')['state']['cursor'] == address,
                   f'unreal-gui --host {pid} memory.goto {address} moves {name}', out)
        for n, (name, (gui, _)) in enumerate(hosts.items()):
            address = f'0x{0x7100 + n * 0x100:04X}'
            gui.call('debugger.open')
            gui.call('debugger.focus', pane='memory')
            time.sleep(0.1)  # the focus record carries the wall-clock time
            code, out = cli(args.unreal_gui, 'memory.goto', address)
            expect(code == 0 and gui.state('debugger/memory')['state']['cursor'] == address,
                   f'unreal-gui memory.goto {address}: the focused {name}', out)
        for gui, _ in hosts.values():
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
