#!/usr/bin/env python3
"""The hex view part of the Zynaps demo, driven through the GUI control plane instead of synthetic clicks.

The memory view is pointed at the game's variables, its sprites and its font by `memory.goto` / `memory.select`; the
machine is loaded and paused over the emulator's MCP. Before the plane the demo set HL to the block and double-clicked
the register (a mouse script tied to pixel offsets of one window layout).

    python3 tools/guicontrol/demo_zynaps_hexview.py --tape .../Zynaps.tzx [--out scratch/zynaps-plane]

The program runs offscreen on ports of its own (--port-base); a shot of the debugger window per scene goes to --out.
"""

import argparse
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'verification', 'mcp'))
import mcplib  # noqa: E402
from guicontrol import Gui  # noqa: E402

# The scenes: what the memory view shows (Zynaps 48K, the reverse's names)
SCENES = [
    ('variables', 0x8003, (0x8003, 0x801F)),
    ('sprites', 0xE008, (0xE008, 0xE047)),
    ('font', 0xB4AF, (0xB4AF, 0xB4EE)),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--tape', required=True, help='Zynaps.tzx (48K)')
    parser.add_argument('--binary', default=os.path.join(ROOT, 'cmake-build-agent-release', 'bin', 'unreal-qt.app',
                                                         'Contents', 'MacOS', 'unreal-qt'))
    parser.add_argument('--port-base', type=int, default=28700)
    parser.add_argument('--out', default=os.path.join(ROOT, 'scratch', 'zynaps-plane'))
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    # A discovery directory of this run: the launched program is the only host in it
    os.environ['UNREAL_GUI_CONTROL_DIR'] = os.path.join(args.out, 'discovery')

    process = mcplib.launch(args.binary, args.port_base)
    try:
        s = mcplib.Session(f'http://127.0.0.1:{args.port_base + 200}/mcp', '48K')
        s.fresh_machine()
        s.call('load_software', {'path': os.path.abspath(args.tape), 'autostart': True})
        s.call('control_execution', {'action': 'resume'}, ok=False)
        for second in range(120):  # loaded: the game's entry jump at 0x8000
            time.sleep(1)
            m = s.call('memory_access', {'action': 'read', 'address': '0x8000', 'length': 3}, ok=False)
            if re.search(r'C3\s+[0-9A-F]{2}\s+[0-9A-F]{2}', m.text) and second > 5:
                break
        s.call('control_execution', {'action': 'pause'}, ok=False)

        for _ in range(40):  # the discovery file appears once the window is up
            try:
                gui = Gui.connect(host=str(process.pid))
                break
            except Exception:
                time.sleep(0.25)
        else:
            raise SystemExit('the program has no GUI control host')
        with gui:
            gui.call('debugger.open')
            gui.call('window.move', target='debugger', x=0, y=0, w=1280, h=860)
            gui.call('memory.follow', register='off')  # the view stays where the scenes put it
            for name, address, (start, end) in SCENES:
                gui.call('memory.goto', addr=address)
                gui.call('memory.select', **{'from': start, 'to': end})
                state = gui.state('memory')['state']
                assert state['cursor'] == f'0x{end:04X}', state
                assert state['selection'] == {'from': f'0x{start:04X}', 'to': f'0x{end:04X}'}, state
                shot = os.path.join(args.out, f'{name}.png')
                gui.call('window.screenshot', target='debugger', path=shot)
                print(f'{name}: memory view at {state["top"]}, selection {state["selection"]} -> {shot}')
    finally:
        process.kill()
    return 0


if __name__ == '__main__':
    sys.exit(main())
