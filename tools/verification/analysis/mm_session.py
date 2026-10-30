#!/usr/bin/env python3
"""Record an input-script session on a snapshot, build L1 + L2 and export the L2 draft (the P3 gate's input).

Drives unreal-qt offscreen through MCP (tools/verification/mcp/mcplib.py): a fresh 48K with a Kempston card (the
scripts' KF needs one; the card is plugged before the snapshot, as plugging restarts the machine), the mixer muted,
the snapshot loaded, `analysis start record=true`, the scripts played one after another in one recording, then
`analysis build` and `analysis export` of every format into the output directory. With --l3, before the exports:
the findings register (findings.md, findings.jsonl), the review queue (review.txt, review.json) and a directed
coverage run (cover.txt, cover.json), so the exported draft carries the coverage notes (P5).

Input scripts are zx-reaper's tests/inputs format (tools/zxrun.py): one line per change, the keys held until the next
line; "FRAME KEYS..." (A-Z 0-9 ENTER SPACE CS SS, Kempston KF KL KR KU KD), "FRAME -" releases all, "FRAME END" ends
the script, "+N" counts from the previous line. Each script's frames count from where the previous one ended.

  mm_session.py --snapshot run-B/re/orig.z80 --inputs run-B/tests/inputs/{demo-kempston,cheat-tour,play,demo}.txt \\
      --out scratch/p3/mm --ranges 5900-59FF,8000-FFFF

Manic Miner order (each script expects the title screen at its start; the cheat tour ends in a game the play script
continues until the lives run out, demo then idles through the title and the demo caverns):
  demo-kempston, cheat-tour, play, demo
"""

import argparse
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'verification', 'mcp'))
import mcplib  # noqa: E402

DEFAULT_BINARY = os.path.join(ROOT, 'cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt')
KEY_NAMES = {'ENTER': 'enter', 'SPACE': 'space', 'CS': 'caps_shift', 'SS': 'symbol_shift'}
KEMPSTON = {'KF': 'fire', 'KL': 'left', 'KR': 'right', 'KU': 'up', 'KD': 'down'}
FORMATS = ('ctl', 'skool', 'fields', 'geometry', 'layout', 'symbols', 'map')


def parse_script(path):
    """[(frame, keys or None for END)] with absolute frames from the script's start"""
    events = []
    last = 0
    with open(path) as f:
        for raw in f:
            line = raw.split('#', 1)[0].strip()
            if not line:
                continue
            words = line.split()
            stamp = words[0]
            frame = last + int(stamp[1:]) if stamp.startswith('+') else int(stamp)
            if len(words) > 1 and words[1] == 'TYPE':
                raise SystemExit(f'{path}: TYPE lines are not supported')
            keys = words[1:]
            if keys == ['END']:
                events.append((frame, None))
                break
            events.append((frame, [] if keys in ([], ['-']) else keys))
            last = frame
    return events


class Player:
    def __init__(self, session):
        self.s = session
        self.keys = []
        self.frames = 0

    def run(self, frames):
        if frames > 0:
            self.s.call('control_execution', {'action': 'run_frames', 'frames': frames})
            self.frames += frames

    def set_keys(self, keys):
        for key in self.keys:
            if key in KEMPSTON:
                self.s.call('joystick_input', {'action': 'release', 'buttons': KEMPSTON[key]})
            else:
                self.s.call('type_input', {'action': 'release', 'key': KEY_NAMES.get(key, key.lower())})
        for key in keys:
            if key in KEMPSTON:
                self.s.call('joystick_input', {'action': 'press', 'buttons': KEMPSTON[key]})
            else:
                self.s.call('type_input', {'action': 'press', 'key': KEY_NAMES.get(key, key.lower())})
        self.keys = list(keys)

    def play(self, events):
        at = 0
        for frame, keys in events:
            self.run(frame - at)
            at = frame
            if keys is None:
                break
            self.set_keys(keys)
        self.set_keys([])


def dump_l3(s, out, args):
    """The P5 outputs: the register, the queue, a coverage run; their counts for session.json"""
    l3 = {}
    began = time.time()
    s.call('analysis', {'action': 'findings', 'format': 'markdown', 'path': os.path.join(out, 'findings.md')})
    s.call('analysis', {'action': 'findings', 'format': 'jsonl', 'path': os.path.join(out, 'findings.jsonl')})
    r = s.call('analysis', {'action': 'findings', 'limit': 1})
    l3['findings'] = {k: r.structured.get(k) for k in ('total', 'by_kind', 'by_status')}
    r = s.call('analysis', {'action': 'review', 'op': 'list', 'ranges': args.ranges, 'limit': 1000})
    with open(os.path.join(out, 'review.txt'), 'w') as f:
        f.write(r.text)
    with open(os.path.join(out, 'review.json'), 'w') as f:
        json.dump(r.structured, f, indent=1)
    l3['review'] = r.structured.get('queue')
    r = s.call('analysis', {'action': 'review', 'ranges': args.ranges})
    with open(os.path.join(out, 'review-next.txt'), 'w') as f:
        f.write(r.text)
    l3['first_sheet_bytes'] = r.structured.get('sheet_bytes')
    l3['findings_seconds'] = round(time.time() - began, 1)
    began = time.time()
    r = s.call('analysis', {'action': 'cover', 'budget': args.cover_budget, 'ranges': args.ranges})
    with open(os.path.join(out, 'cover.txt'), 'w') as f:
        f.write(r.text)
    with open(os.path.join(out, 'cover.json'), 'w') as f:
        json.dump(r.structured, f, indent=1)
    l3['cover'] = {k: r.structured.get(k) for k in ('candidates', 'tried', 'flipped', 'attempts', 'new_code',
                                                     'workers', 'peak_concurrency', 'micros')}
    l3['cover_seconds'] = round(time.time() - began, 1)
    print('L3:', json.dumps(l3))
    return l3


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--snapshot', required=True)
    parser.add_argument('--inputs', nargs='+', required=True)
    parser.add_argument('--out', required=True, help='output directory (L1 JSON, the exports, session.json)')
    parser.add_argument('--ranges', default='4000-FFFF')
    parser.add_argument('--binary', default=DEFAULT_BINARY)
    parser.add_argument('--port-base', type=int, default=8790)
    parser.add_argument('--no-kempston', action='store_true')
    parser.add_argument('--l3', action='store_true', help='also dump findings / review / cover (P5) before the exports')
    parser.add_argument('--cover-budget', type=int, default=64)
    parser.add_argument('--keep', action='store_true',
                        help='leave unreal-qt running with the built analysis (a review agent takes it over); '
                             'stop it with the pid in session.json')
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    out = os.path.abspath(args.out)
    scripts = [(path, parse_script(path)) for path in args.inputs]
    process = mcplib.launch(args.binary, args.port_base)
    report = {'snapshot': os.path.abspath(args.snapshot), 'inputs': [], 'ranges': args.ranges}
    try:
        s = mcplib.Session(f'http://127.0.0.1:{args.port_base + 200}/mcp', '48K')
        s.fresh_machine('48K')
        if not args.no_kempston:
            s.call('emulator_manage', {'action': 'slots_plug', 'slot': 'edge.1', 'card': 'kempston'})
            # The plug restarts the machine: wait for the wired stick before anything else touches the machine
            for _ in range(100):
                if 'slots_plug slot=edge.1' not in s.call('joystick_input', {'action': 'status'}, ok=False).text:
                    break
                time.sleep(0.1)
            else:
                raise SystemExit('the Kempston card did not come up')
        s.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/audio/mixer/master',
                              'body': {'muted': True}}, ok=False)
        r = s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/emulator/{id}/snapshot/load',
                                  'body': {'path': os.path.abspath(args.snapshot)}})
        print(r.text.splitlines()[-1] if r.text else 'snapshot loaded')
        print(s.call('analysis', {'action': 'start', 'record': True}).text.splitlines()[-1])
        player = Player(s)
        for path, events in scripts:
            start = player.frames
            began = time.time()
            player.play(events)
            report['inputs'].append({'script': os.path.basename(path), 'first_frame': start,
                                     'frames': player.frames - start})
            print(f'{os.path.basename(path)}: frames {start}..{player.frames} ({time.time() - began:.0f} s)')

        began = time.time()
        r = s.call('analysis', {'action': 'build', 'path': os.path.join(out, 'l1.json'), 'wait': 30000})
        print(r.text.splitlines()[-1])
        status = s.call('analysis', {'action': 'status'}).text
        while ': running' in status:
            time.sleep(5)
            status = s.call('analysis', {'action': 'status'}).text
        print(status)
        report['build_seconds'] = round(time.time() - began, 1)
        report['status'] = status
        if args.l3:
            report['l3'] = dump_l3(s, out, args)
        for fmt in FORMATS:
            path = os.path.join(out, 'draft.' + fmt + ('.json' if fmt in ('fields', 'geometry') else
                                                         '.txt' if fmt == 'map' else '.mk' if fmt == 'layout' else ''))
            r = s.call('analysis', {'action': 'export', 'format': fmt, 'path': path, 'ranges': args.ranges})
            print(r.text.splitlines()[-1])
        r = s.call('analysis', {'action': 'query', 'kind': 'conflicts', 'limit': 50})
        with open(os.path.join(out, 'conflicts.txt'), 'w') as f:
            f.write(r.text)
        report['frames'] = player.frames
        if args.keep:
            report['kept'] = {'pid': process.pid, 'mcp': f'http://127.0.0.1:{args.port_base + 200}/mcp',
                              'webapi': f'http://127.0.0.1:{args.port_base}'}
        with open(os.path.join(out, 'session.json'), 'w') as f:
            json.dump(report, f, indent=1)
    except BaseException:
        args.keep = False
        raise
    finally:
        if args.keep:
            print(f'kept: pid {process.pid}, MCP http://127.0.0.1:{args.port_base + 200}/mcp')
        else:
            process.terminate()
            process.wait(timeout=20)


if __name__ == '__main__':
    main()
