#!/usr/bin/env python3
"""Still frames for the gallery from snapshots: each snapshot loaded, run, the presented frame saved after each wait
into <out>/<name>-<n>.png (352 x 288, the border included). The gallery's own frames are testdata/video/crt-gallery.

usage: capture.py --port PORT --emulator ID [--out DIR] name:snapshot:wait[,wait...] ...
       e.g. capture.py --port 8090 --emulator <id> edge:testdata/loaders/sna/across-the-edge-second.sna:3,0.5"""
import os
import time

from common import ROOT, parser
from webapi import WebAPI


def main():
    p = parser(__doc__)
    p.add_argument('--host', default='127.0.0.1')
    p.add_argument('--port', type=int, required=True, help='WebAPI port of a running unreal-qt')
    p.add_argument('--emulator', required=True, help='emulator id')
    p.add_argument('--out', help='output directory (default: <work>/frames)')
    p.add_argument('shots', nargs='+')
    args = p.parse_args()
    out = os.path.abspath(args.out or os.path.join(args.work, 'frames'))
    os.makedirs(out, exist_ok=True)
    api = WebAPI(args.host, args.port, args.emulator)
    api.take_control()
    for shot in args.shots:
        name, snapshot, waits = shot.split(':')
        status, answer = api.call('POST', '/snapshot/load', {'path': os.path.join(ROOT, snapshot)})
        if status != 200:
            print(name, 'load failed', status, answer)
            continue
        api.call('POST', '/resume')
        for i, wait in enumerate(waits.split(',')):
            time.sleep(float(wait))
            api.screen(f'{out}/{name}-{i}.png')
            print(name, i)
        api.call('POST', '/pause')
    api.release_control()


if __name__ == '__main__':
    main()
