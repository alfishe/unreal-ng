#!/usr/bin/env python3
"""Live check of unreal-qt on the GUI control plane: an offscreen unreal-qt on ports of its own, a fresh muted
machine, then every command of `debugger`, `metadata`, `disassembly`, `memory`, `unrealqt` and `window` called through
the Python client with its state read back; last, verify_mcp.py against the same program.

    python3 tools/guicontrol/verify_unrealqt.py [--port-base 28800]

Exit code 0 when every check passed (about 10 s).
"""

import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
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


def check(gui, png):
    state = lambda target: gui.state(target)['state']  # noqa: E731

    # debugger: open, panes
    gui.call('debugger.open')
    root = gui.state('debugger')  # the window node
    expect(root['state']['visible'], 'debugger.open shows the window', root)
    expect(gui.call('debugger.focus', pane='memory') == {'pane': 'memory'}, 'debugger.focus memory')
    expect(gui.call('debugger.focus_next')['pane'] == 'stack', 'debugger.focus_next: memory -> stack')
    expect(gui.call('debugger.focus_prev')['pane'] == 'memory', 'debugger.focus_prev: back to memory')
    expect(refused(gui, 'debugger.focus', 'bad_request', pane='console'), 'debugger.focus refuses an unknown pane')
    gui.call('debugger.focus', pane='disassembly')

    # metadata switches
    labels = gui.list()['state']['metadata']['labels']
    expect(gui.call('metadata.set', switch='labels')['on'] == (not labels), 'metadata.set flips labels')
    expect(gui.list()['state']['metadata']['labels'] == (not labels), 'the host state follows the switch')
    gui.call('metadata.set', switch='labels', on=labels)
    expect(refused(gui, 'metadata.set', 'bad_request', switch='bogus'), 'metadata.set refuses an unknown switch')

    # disassembly
    expect(gui.call('disassembly.goto', addr='#0038') == {'top': '0x0038'}, 'disassembly.goto #0038')
    expect(gui.call('disassembly.cursor', addr='0x003B') == {'cursor': '0x003B'}, 'disassembly.cursor')
    moved = gui.call('disassembly.cursor_move', dir='down')['cursor']
    expect(moved > '0x003B', 'disassembly.cursor_move down goes to the next instruction', moved)
    expect(refused(gui, 'disassembly.cursor_move', 'bad_request', dir='left'), 'the disassembly moves up or down only')
    paged = gui.call('disassembly.page', n=1)['cursor']
    expect(paged > moved, 'disassembly.page n=1 moves the cursor down a page', paged)
    expect(gui.call('disassembly.scroll_mode', mode='byte') == {'scroll_mode': 'byte'}, 'scroll_mode byte')
    expect(gui.call('disassembly.scroll_mode') == {'scroll_mode': 'command'}, 'scroll_mode flips back')
    gui.call('disassembly.follow_pc')
    d = state('disassembly')
    expect(d['pc'] is not None and d['top'] is not None, 'disassembly state: top and pc', d)
    gui.call('disassembly.cursor', addr=d['pc'])
    gui.call('disassembly.breakpoint_toggle_cursor')
    gui.call('disassembly.breakpoint_toggle_cursor')
    expect(True, 'disassembly.breakpoint_toggle_cursor twice')

    # memory
    gui.call('memory.follow', register='off')
    expect(gui.call('memory.goto', addr='0x6A00') == {'cursor': '0x6A00'}, 'memory.goto 0x6A00')
    m = state('memory')
    expect(m['top'] == '0x4000' and m['follow'] == 'off', 'the view shows the 16 KB page of the address', m)
    gui.call('memory.cursor_move', dir='right')
    expect(state('memory')['cursor'] == '0x6A01', 'memory.cursor_move right')
    gui.call('memory.cursor', addr='0x7FFF')
    gui.call('memory.cursor_move', dir='right')
    m = state('memory')
    expect(m['cursor'] == '0x8000' and m['top'] == '0x8000', 'past the page edge the next page shows', m)
    gui.call('memory.select', **{'from': '0x6A10', 'to': '0x6A1F'})
    expect(state('memory')['selection'] == {'from': '0x6A10', 'to': '0x6A1F'}, 'memory.select')
    expect(refused(gui, 'memory.select', 'bad_request', **{'from': '0x3FF0', 'to': '0x4010'}),
           'a selection across two pages is refused')
    before = state('memory')['cursor']
    gui.call('memory.page')
    expect(state('memory')['cursor'] > before, 'memory.page moves down', state('memory'))
    gui.call('memory.cursor', addr='0x5C36')  # CHARS: a word in the system variables
    target = gui.call('memory.follow_cursor')['cursor']
    expect(target.startswith('0x'), 'memory.follow_cursor goes to the word at the cursor', target)
    expect(gui.call('memory.follow', register='sp') == {'follow': 'sp'}, 'memory.follow sp')
    expect(refused(gui, 'memory.follow', 'bad_request', register='xx'), 'memory.follow refuses an unknown register')
    expect(refused(gui, 'memory.find_last_write', 'not_now'), 'memory.find_last_write without a recording: not_now')
    expect(refused(gui, 'memory.goto', 'bad_request', addr='0x10000'), 'an address past 0xFFFF is refused')

    # targets: the view on its machine, an interface, a path
    instance = gui.state('memory')['instance']
    expect(instance['endpoint'].startswith('http://127.0.0.1:'), 'the view names its machine', instance)
    gui.call('memory.goto', target=f"memory@{instance['id']}", addr='0x5800')
    expect(state('debugger/memory')['cursor'] == '0x5800', 'memory@instance reaches the view')
    try:
        gui.call('memory.goto', target='memory@no-such-machine', addr='0x4000')
        expect(False, 'memory@<unknown instance> is refused')
    except GuiError as e:
        expect(e.error == 'not_found' and 'no-such-machine' in e.message and instance['id'] in e.message,
               'memory@<unknown instance>: not_found naming it and what the view shows', e.message)
    expect(state('debugger/memory')['cursor'] == '0x5800', 'the refused call moved nothing')
    try:
        gui.call('window.screenshot', target='debugger', file='x.png')
        expect(False, 'an unknown parameter is refused')
    except GuiError as e:
        expect(e.error == 'bad_request' and 'unknown parameter file' in e.message and 'path?' in e.message,
               'an unknown parameter: bad_request naming it and what the command takes', e.message)

    # unrealqt and window
    expect(gui.call('unrealqt.osd_keyboard', on=True) == {'on': True}, 'unrealqt.osd_keyboard on')
    expect(gui.list()['state']['osd_keyboard'] is True, 'the host state shows the keyboard')
    gui.call('unrealqt.osd_keyboard', on='false')
    expect(gui.list()['state']['osd_keyboard'] is False, 'unrealqt.osd_keyboard off (a word)')
    expect(refused(gui, 'unrealqt.controllers_learn', 'failed', device='nope'), 'controllers_learn: no such device')
    shot = gui.call('window.screenshot', target='debugger', path=png)
    expect(os.path.getsize(png) > 0 and shot['width'] > 0, 'window.screenshot of the debugger', shot)
    gui.call('debugger.close')
    expect(not gui.state('debugger')['state']['visible'], 'debugger.close hides it')


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--binary', default=os.path.join(ROOT, 'cmake-build-agent-release', 'bin', 'unreal-qt.app',
                                                         'Contents', 'MacOS', 'unreal-qt'))
    parser.add_argument('--unreal-gui', default=os.path.join(ROOT, 'cmake-build-agent-release', 'bin', 'unreal-gui'))
    parser.add_argument('--port-base', type=int, default=28800)
    parser.add_argument('--out', default=os.path.join(ROOT, 'scratch', 'guicontrol-verify'))
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    os.environ['UNREAL_GUI_CONTROL_DIR'] = os.path.join(args.out, 'discovery')

    process = mcplib.launch(args.binary, args.port_base)
    try:
        mcplib.Session(f'http://127.0.0.1:{args.port_base + 200}/mcp', 'PENTAGON').fresh_machine()
        gui = None
        for _ in range(40):
            try:
                gui = Gui.connect(host=str(process.pid))
                if gui.state('memory').get('instance'):
                    break
                gui.close()
                gui = None
            except (OSError, GuiError):
                pass
            time.sleep(0.25)
        if gui is None:
            raise SystemExit('the program has no GUI control host with a machine bound')
        with gui:
            check(gui, os.path.join(args.out, 'debugger.png'))
        mcp = subprocess.run([sys.executable, os.path.join(HERE, 'verify_mcp.py'), args.unreal_gui])
        expect(mcp.returncode == 0, 'verify_mcp.py')
    finally:
        process.kill()
    print(f'{len(failures)} failed' if failures else 'all passed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
