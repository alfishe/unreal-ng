#!/usr/bin/env python3
"""Live check of one port (design §4.10, phase 7): the checks of phases 5 and 6 once more, through a program's core
instead of the plane's sockets. An offscreen unreal-qt on ports of its own with a fresh muted machine, unreal-debugger
in process and connected to it (--connect), the screen viewer connected to it, and an offscreen video wall on ports
of its own:

- through unreal-qt's WebAPI port alone (REST /api/v1/gui): the debugger checks on the three debuggers, the screen
  checks on unreal-qt and the screen viewer (other programs, reached through unreal-qt's port);
- through unreal-qt's MCP (the gui_<interface> tools): the debugger checks on unreal-qt, the screen checks on the
  viewer;
- through the video wall's own WebAPI port: the tile grid checks;
- one call each as a WebSocket `call`, from the CLI `gui` and from Lua / Python on unreal-qt's planes.

    python3 tools/guicontrol/verify_one_port.py [--port-base 28820] [--wall-port-base 28830]

Exit code 0 when every check passed (about 40 s).
"""

import argparse
import json
import os
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
BIN = os.path.join(ROOT, 'cmake-build-agent-release', 'bin')
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools', 'verification', 'mcp'))
import mcplib  # noqa: E402
import verify_debugger  # noqa: E402
import verify_programs  # noqa: E402
from guicontrol import Gui, GuiError, WebGui  # noqa: E402

failures = []


def expect(condition, what, detail=''):
    print(('PASS ' if condition else 'FAIL ') + what + ('' if condition else f'\n     {detail}'))
    if not condition:
        failures.append(what)


def section(title):
    print(f'--- {title}')


def wait_host(pid, seconds=20.0, machine=False):
    """The process's host once it lists (and, with `machine`, once its memory view names a machine)"""
    end = time.time() + seconds
    while time.time() < end:
        try:
            gui = Gui.connect(host=str(pid))
            if not machine or gui.state('memory').get('instance'):
                return gui
            gui.close()
        except (OSError, GuiError):
            pass
        time.sleep(0.25)
    raise SystemExit(f'process {pid}: no GUI control host')


def cli_line(port, line, seconds=20.0):
    """One CLI command on the emulator's CLI port; its answer"""
    with socket.create_connection(('127.0.0.1', port), timeout=seconds) as sock:
        sock.settimeout(1.0)
        banner = b''
        try:
            while True:
                chunk = sock.recv(65536)
                if not chunk:
                    break
                banner += chunk
        except socket.timeout:
            pass
        sock.sendall((line + '\n').encode())
        sock.settimeout(seconds)
        answer = b''
        end = time.time() + seconds
        while time.time() < end:
            try:
                chunk = sock.recv(65536)
            except socket.timeout:
                break
            if not chunk:
                break
            answer += chunk
            if answer.rstrip().endswith(b'>'):
                break
        return answer.decode(errors='replace')


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--unreal-qt', default=os.path.join(BIN, 'unreal-qt.app', 'Contents', 'MacOS', 'unreal-qt'))
    parser.add_argument('--unreal-debugger', default=os.path.join(BIN, 'unreal-debugger'))
    parser.add_argument('--unreal-screen-viewer', default=os.path.join(
        BIN, 'unreal-screen-viewer.app', 'Contents', 'MacOS', 'unreal-screen-viewer'))
    parser.add_argument('--unreal-videowall', default=os.path.join(
        BIN, 'unreal-videowall.app', 'Contents', 'MacOS', 'unreal-videowall'))
    parser.add_argument('--port-base', type=int, default=28820)
    parser.add_argument('--wall-port-base', type=int, default=28830)
    parser.add_argument('--out', default=os.path.join(ROOT, 'scratch', 'guicontrol-verify-one-port'))
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    os.environ['UNREAL_GUI_CONTROL_DIR'] = os.path.join(args.out, 'discovery')
    port = args.port_base
    endpoint = f'http://127.0.0.1:{port}'

    processes = [mcplib.launch(args.unreal_qt, port)]
    try:
        session = mcplib.Session(f'http://127.0.0.1:{port + 200}/mcp', 'PENTAGON')
        session.fresh_machine()
        qt = wait_host(processes[0].pid, machine=True)
        machine = qt.state('memory')['instance']['id']
        processes.append(verify_debugger.launch_debugger(args.unreal_debugger))
        own = wait_host(processes[1].pid, machine=True)
        processes.append(verify_debugger.launch_debugger(args.unreal_debugger, '--connect', f'127.0.0.1:{port}',
                                                         '--emulator', machine))
        remote = wait_host(processes[2].pid, machine=True)
        processes.append(verify_programs.launch(args.unreal_screen_viewer, '--connect', f'127.0.0.1:{port}'))
        viewer = wait_host(processes[3].pid)
        processes.append(mcplib.launch(args.unreal_videowall, args.wall_port_base))
        verify_programs.mute_all(args.wall_port_base)
        wall = wait_host(processes[4].pid)
        pids = {name: str(p.pid) for name, p in zip(
            ('unreal-qt', 'unreal-debugger', 'unreal-debugger --connect', 'unreal-screen-viewer', 'unreal-videowall'),
            processes)}
        for gui in (qt, own, remote, viewer, wall):
            gui.close()

        section('REST on unreal-qt\'s port')
        rest = WebGui(port)
        listed = {entry.get('program') for entry in rest.list().get('hosts', [])}
        expect({'unreal-qt', 'unreal-debugger', 'unreal-screen-viewer', 'unreal-videowall'} <= listed,
               'GET /api/v1/gui lists every GUI program on the machine', listed)
        status, catalogue = verify_programs.http(port, 'GET', '/api/v1/gui/catalogue')
        names = {spec['name'] for spec in catalogue.get('interfaces', [])}
        expect(status == 200 and {'memory', 'disassembly', 'debugger', 'screen', 'tilegrid', 'window',
                                  'unrealqt'} <= names, 'GET /api/v1/gui/catalogue: the interfaces', names)
        status, body = verify_programs.http(port, 'POST', '/api/v1/gui/memory.goto', [1])
        expect(status == 400, 'a body that is not an object is 400', (status, body))
        status, body = verify_programs.http(port, 'POST', '/api/v1/gui/nothing.here', {})
        expect(status == 200 and body.get('ok') is False and body.get('error') == 'not_found',
               'an unknown command is the plane\'s not_found with 200', (status, body))
        for name in ('unreal-qt', 'unreal-debugger', 'unreal-debugger --connect'):
            instance = verify_debugger.conform(WebGui(port, host=pids[name]), f'REST {name}')
            expect(bool(instance.get('id')), f'REST {name}: the views name a machine', instance)
        verify_programs.conform_screen(WebGui(port, host=pids['unreal-qt']), 'REST unreal-qt', machine, endpoint)
        verify_programs.conform_screen(WebGui(port, host=pids['unreal-screen-viewer']), 'REST unreal-screen-viewer',
                                       machine, endpoint)

        section('MCP of unreal-qt')
        tools = session.tools()
        expect({'gui_list', 'gui_state', 'gui_memory', 'gui_disassembly', 'gui_debugger', 'gui_screen',
                'gui_tilegrid', 'gui_window', 'gui_unrealqt'} <= set(tools), 'the MCP lists the gui_ tools',
               sorted(t for t in tools if t.startswith('gui_')))
        actions = tools.get('gui_memory', {}).get('inputSchema', {}).get('properties', {}).get('action', {})
        expect('goto' in actions.get('enum', []), 'gui_memory: action goto', actions)
        mcp = WebGui(port, via='mcp')
        verify_debugger.conform(WebGui(port, host=pids['unreal-qt'], via='mcp'), 'MCP unreal-qt')
        verify_programs.conform_screen(WebGui(port, host=pids['unreal-screen-viewer'], via='mcp'),
                                       'MCP unreal-screen-viewer', machine, endpoint)
        text = mcplib.Result(session.rpc('tools/call', {'name': 'gui_memory', 'arguments': {
            'action': 'goto', 'target': f"{pids['unreal-qt']}/debugger/memory", 'addr': '0x6400'}}))
        expect(not text.is_error and '0x6400' in text.text, 'gui_memory goto: the answer in the text', text.text)
        refused = mcplib.Result(session.rpc('tools/call', {'name': 'gui_memory', 'arguments': {
            'action': 'goto', 'target': 'nowhere/none', 'addr': '0x6400'}}))
        expect(refused.is_error and 'not_found' in refused.text and (refused.structured or {}).get('error') ==
               'not_found', 'a refusal: the error in the text and in structuredContent', refused.text)
        expect(any(e.get('program') == 'unreal-qt' for e in mcp.list().get('hosts', [])), 'gui_list lists hosts')

        section('The video wall\'s own port')
        verify_programs.conform_wall(WebGui(args.wall_port_base, host=pids['unreal-videowall']),
                                     args.wall_port_base,
                                     mcplib.Session(f'http://127.0.0.1:{args.wall_port_base + 200}/mcp', 'PENTAGON'))

        section('WebSocket, CLI, Lua, Python on unreal-qt')
        target = f"{pids['unreal-qt']}/debugger/memory"
        ws = mcplib.WebSocket(port)
        ws.send({'op': 'call', 'rid': 1, 'method': 'POST', 'path': '/api/v1/gui/memory.goto',
                 'body': {'target': target, 'addr': '0x6500'}})
        reply = ws.until(lambda m: m.get('op') == 'result' and m.get('rid') == 1, 'the call\'s result', timeout=10)
        ws.close()
        expect(reply.get('status') == 200 and reply.get('body', {}).get('ok') is True,
               'WebSocket call POST /api/v1/gui/memory.goto', reply)
        expect(rest.state(target)['state']['cursor'] == '0x6500', 'the WebSocket call moved the view')

        answer = cli_line(port + 100, f'gui memory.goto addr=0x6600 target={target}')
        expect('0x6600' in answer and rest.state(target)['state']['cursor'] == '0x6600',
               'CLI gui memory.goto addr=0x6600 target=...', answer)
        answer = cli_line(port + 100, 'gui state ' + target)
        expect('0x6600' in answer, 'CLI gui state <target>', answer)
        answer = cli_line(port + 100, 'gui memory.goto 0x6600')
        expect('name=value' in answer, 'CLI gui: a word without = is refused with the usage', answer)

        for language, code in (
                ('lua', f'gui_call("memory.goto", {{addr = "0x6700"}}, "{target}")'),
                ('python', f'import unreal_emulator\nunreal_emulator.gui_call("memory.goto", "{target}", addr="0x6800")')):
            expected = '0x6700' if language == 'lua' else '0x6800'
            status, body = verify_programs.http(port, 'POST', f'/api/v1/{language}/exec', {'code': code})
            if status == 503:
                print(f'SKIP {language}: not in this build (ENABLE_{language.upper()}_AUTOMATION=OFF)')
                continue
            cursor = rest.state(target)['state']['cursor']
            expect(status == 200 and cursor == expected, f'{language}: gui_call memory.goto {expected}',
                   (status, body, cursor))
    finally:
        for process in reversed(processes):
            process.terminate()
        for process in processes:
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
    failed = failures + verify_debugger.failures + verify_programs.failures
    print(f'{len(failed)} failed' if failed else 'all passed')
    for what in failed:
        print(f'  {what}')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
