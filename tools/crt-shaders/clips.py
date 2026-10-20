#!/usr/bin/env python3
"""Motion clips for videos.py: consecutive frames into <work>/clips/<name>/NNNN.png, frame by frame (run_frame), with
the tape's fast and turbo loading off: `edge` (Across the Edge, scrolling), `aleste` (Aleste, a shooter), `tape` (a tape
loading: border stripes, the loading screen drawn).

usage: clips.py --port PORT --emulator ID [--tape FILE] [--tape-wait SECONDS] [name ...]"""
import os
import time

from common import ROOT, parser
from webapi import WebAPI

SNAPSHOTS = {  # name: (snapshot, seconds to run first, frames)
    'edge': ('testdata/loaders/sna/across-the-edge-second.sna', 5.0, 150),
    'aleste': ('testdata/loaders/sna/aleste1.sna', 2.5, 150),
}
TAPE_FRAMES = 250


def grab(api, work, name, count):
    os.makedirs(f'{work}/clips/{name}', exist_ok=True)
    for i in range(count):
        api.call('POST', '/run_frame')
        api.screen(f'{work}/clips/{name}/{i:04d}.png')
    print(name, count, 'frames')


def main():
    p = parser(__doc__)
    p.add_argument('--host', default='127.0.0.1')
    p.add_argument('--port', type=int, required=True, help='WebAPI port of a running unreal-qt')
    p.add_argument('--emulator', required=True, help='emulator id')
    p.add_argument('--tape', default='testdata/loaders/tap/greenberet.tap', help='the tape of the `tape` clip')
    p.add_argument('--tape-wait', type=float, default=4.0, help='seconds of loading before the clip starts')
    p.add_argument('names', nargs='*', default=list(SNAPSHOTS) + ['tape'])
    args = p.parse_args()
    work = os.path.abspath(args.work)
    api = WebAPI(args.host, args.port, args.emulator)
    api.take_control()
    for setting in ('fast_tape', 'turbo_tape'):
        assert api.call('PUT', f'/settings/{setting}', {'value': False})[0] == 200
    for name in args.names:
        if name in SNAPSHOTS:
            snapshot, warm, count = SNAPSHOTS[name]
            status, answer = api.call('POST', '/snapshot/load', {'path': os.path.join(ROOT, snapshot)})
            assert status == 200, (status, answer)
            api.call('POST', '/resume')
            time.sleep(warm)
            api.call('POST', '/pause')
            grab(api, work, name, count)
        elif name == 'tape':
            assert api.call('POST', '/reset')[0] == 200
            api.call('POST', '/resume')
            time.sleep(2)
            assert api.call('POST', '/tape/load', {'path': os.path.join(ROOT, args.tape)})[0] == 200
            assert api.call('POST', '/basic/run', {'command': 'LOAD ""'})[0] == 200
            api.call('POST', '/tape/play')
            time.sleep(args.tape_wait)
            api.call('POST', '/pause')
            grab(api, work, name, TAPE_FRAMES)
        else:
            print(name, 'unknown clip')
    api.release_control()


if __name__ == '__main__':
    main()
