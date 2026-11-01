#!/usr/bin/env python3
"""The mechanics of the reverse-engineering recipe (.recipe/analysis/reverse-engineering.md): one live unreal-qt
instance per game, kept between calls, so an agent works the review queue on it and exports when done.

  re_session.py capture --work W (--media game.tzx | --snapshot game.z80) [--model 48K] [--inputs a.txt ...]
                        [--frames 15000] [--ranges 5B00-FFFF] [--port-base 9720]
      Launches unreal-qt offscreen (muted, RAM zeroed at power-on, a Kempston card), loads the game (a tape as a
      user does: LOAD "" typed, the tape played; a disk booted), finds the keys to play (corpus.py's explorer) unless
      input scripts are given, saves the start state (W/start.szx, the Z80 view as W/start.bin) and starts the
      recording there, plays (the scripts, or the found keys then seeded random play), builds L1 / L2 and writes the
      queue's first page (W/review.txt), the findings counts (W/findings.txt) and eight screens through the run
      on one sheet (W/screens.png). The instance stays up:
      W/session.json holds its process and ports.
  re_session.py call --work W ACTION ['{"op":"next"}']
      One `analysis` call on the live instance (the control session is taken first); prints the answer's text,
      --json OUT writes its structuredContent too.
  re_session.py export --work W
      Every draft format into W/draft (draft.skool, .ctl, .map.txt, .fields.json, .geometry.json, .symbols,
      .layout.mk) and the findings register (findings.md, findings.jsonl), over the session's --ranges.
  re_session.py check --work W --venv SKOOLKIT_VENV [--sjasmplus BIN] [--reference REF.skool [--expected FILE]]
      The gates, written to W/check.json: `rebuild` (skool2bin.py of draft.skool gives the start image byte for byte
      over the ranges), `assemble` (skool2asm.py -c through sjasmplus gives the same bytes,
      the @isub operands resolved;  the skool2asm warnings are the operands still left as numbers), and with --reference the score of the draft
      (score_draft.py with the draft's fields and geometry).
  re_session.py stop --work W
      Ends the instance.

Input scripts are mm_session.py's (zx-reaper's tests/inputs format). Everything the game leaves (screens, RAM, the
recording's start image, the draft) stays in W: keep W under scratch/.
"""

import argparse
import json
import os
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import corpus  # noqa: E402
import mm_session  # noqa: E402
from corpus import mcplib  # noqa: E402

FORMATS = {'skool': 'draft.skool', 'ctl': 'draft.ctl', 'map': 'draft.map.txt', 'fields': 'draft.fields.json',
           'geometry': 'draft.geometry.json', 'symbols': 'draft.symbols', 'layout': 'draft.layout.mk'}


def session_of(work):
    with open(os.path.join(work, 'session.json')) as f:
        state = json.load(f)
    s = mcplib.Session(f"http://127.0.0.1:{state['port_base'] + 200}/mcp", state['model'])
    # Every POST action changes the emulator: without the control session each one answers "opened implicitly"
    s.call('control_execution', {'action': 'take_control'}, ok=False)
    return s, state


def ranges_of(text):
    """[(start, end)] with end exclusive, from "5B00-FFFF,..." (hex, inclusive)"""
    out = []
    for part in text.split(','):
        a, b = part.split('-')
        out.append((int(a, 16), int(b, 16) + 1))
    return out


def capture(args):
    work = os.path.abspath(args.work)
    os.makedirs(work, exist_ok=True)
    process = mcplib.launch(args.binary, args.port_base)
    with open(os.path.join(work, 'session.json'), 'w') as f:
        json.dump({'pid': process.pid, 'port_base': args.port_base, 'model': args.model, 'ranges': args.ranges}, f)
    s = mcplib.Session(f'http://127.0.0.1:{args.port_base + 200}/mcp', args.model)
    s.fresh_machine(args.model, ram_power_on='zero')
    s.call('emulator_manage', {'action': 'slots_plug', 'slot': 'edge.1', 'card': 'kempston'}, ok=False)
    for _ in range(100):  # the plug restarts the machine: wait for the wired stick
        if 'slots_plug slot=edge.1' not in s.call('joystick_input', {'action': 'status'}, ok=False).text:
            break
        time.sleep(0.1)
    s.call('control_execution', {'action': 'pause'}, ok=False)
    s.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/audio/mixer/master',
                          'body': {'muted': True}}, ok=False)
    m = corpus.Machine(s, args.port_base)
    report = {'model': args.model, 'ranges': args.ranges}
    player = corpus.Input(s)
    steps, reached = [], True
    explorer = None
    if args.snapshot:
        m.post('/snapshot/load', {'path': os.path.abspath(args.snapshot)})
    else:
        media = os.path.abspath(args.media)
        if media.endswith('.zip'):
            media = corpus.unpack(media, work, corpus.MEDIA_SUFFIXES['disk' if args.disk else 'tape'])
        game = {'kind': 'disk' if args.disk else 'tape', 'model': args.model}
        report['load_frames'] = corpus.load(m, game, media, report, args)
    if not args.inputs:
        explorer = corpus.Explorer(m, player, work)
        steps, trace, reached = explorer.run()
        report['explore'] = {'keys': steps, 'reached_control': reached, 'trace': trace}
        explorer.replay()
    explorer_start = explorer.start if explorer else None
    m.post('/snapshot/save', {'path': os.path.join(work, 'start.szx'), 'force': True})
    with open(os.path.join(work, 'start.bin'), 'wb') as f:
        f.write(m.read(0, 0x10000))
    s.call('analysis', {'action': 'start', 'record': True})
    screens = os.path.join(work, 'screens')
    os.makedirs(screens, exist_ok=True)
    frames = 0
    if args.inputs:
        for path in args.inputs:
            frames += play_script(s, m, path, screens, frames)
    else:
        frames = explorer.play(steps)
        explorer.clean()
        for count, keys in corpus.generic_input(os.path.basename(work), args.frames, start=not reached):
            player.set(keys)
            frames = corpus.run_frames(m, min(count, args.frames - frames), screens, frames)
            if frames >= args.frames:
                break
        player.set([])
    report['frames'] = frames
    # Eight screens through the run on one sheet: whether the play reached the game, not a menu, is seen at once
    shots = sorted(os.path.join(screens, f) for f in os.listdir(screens) if f.endswith('.scr'))
    if shots:
        picked = [shots[i * (len(shots) - 1) // 7] for i in range(8)] if len(shots) > 8 else shots
        subprocess.run([sys.executable, os.path.join(HERE, 'scr2png.py'), os.path.join(work, 'screens.png'), *picked])
    if explorer_start and os.path.exists(explorer_start):
        os.remove(explorer_start)
    s.call('analysis', {'action': 'build', 'path': os.path.join(work, 'l1.json'), 'wait': 30000})
    state = s.call('analysis', {'action': 'status'}).text
    while ': running' in state:
        time.sleep(5)
        state = s.call('analysis', {'action': 'status'}).text
    report['status'] = state.splitlines()
    with open(os.path.join(work, 'review.txt'), 'w') as f:
        f.write(s.call('analysis', {'action': 'review', 'op': 'list'}).text + '\n')
    with open(os.path.join(work, 'findings.txt'), 'w') as f:
        f.write(s.call('analysis', {'action': 'findings', 'limit': 0}, ok=False).text + '\n')
    with open(os.path.join(work, 'capture.json'), 'w') as f:
        json.dump(report, f, indent=1)
    print(f"captured {frames} frames; {report['status'][-1] if report['status'] else ''}")
    print(f"MCP http://127.0.0.1:{args.port_base + 200}/mcp  WebAPI http://127.0.0.1:{args.port_base}")


def play_script(s, m, path, screens, at):
    """A zx-reaper input script from where the previous one ended; returns the frames it took"""
    held, last = [], 0
    for frame, keys in mm_session.parse_script(path):
        if frame > last:
            corpus.run_frames(m, frame - last, screens, at + last)
            last = frame
        if keys is None:
            break
        for key in held:
            set_key(s, key, False)
        for key in keys:
            set_key(s, key, True)
        held = keys
    for key in held:
        set_key(s, key, False)
    return last


def set_key(s, key, down):
    action = 'press' if down else 'release'
    if key in mm_session.KEMPSTON:
        s.call('joystick_input', {'action': action, 'buttons': mm_session.KEMPSTON[key]}, ok=False)
    else:
        s.call('type_input', {'action': action, 'key': mm_session.KEY_NAMES.get(key, key.lower())}, ok=False)


def call(args):
    s, _ = session_of(args.work)
    body = json.loads(args.args) if args.args else {}
    r = s.call('analysis', dict(body, action=args.action), ok=False)
    print(r.text)
    if args.json:
        with open(args.json, 'w') as f:
            json.dump(r.structured or {}, f, indent=1)
    if r.is_error:
        raise SystemExit(1)


def export(args):
    s, state = session_of(args.work)
    draft = os.path.join(os.path.abspath(args.work), 'draft')
    os.makedirs(draft, exist_ok=True)
    for fmt, name in FORMATS.items():
        r = s.call('analysis', {'action': 'export', 'format': fmt, 'path': os.path.join(draft, name),
                                'ranges': state['ranges']}, ok=False)
        lines = [l for l in (r.text or '').splitlines() if not l.startswith('Analysis ')]
        print(f"{fmt:9} {lines[0] if lines else ''}")
    for fmt, name in (('markdown', 'findings.md'), ('jsonl', 'findings.jsonl')):
        s.call('analysis', {'action': 'findings', 'format': fmt, 'path': os.path.join(draft, name)}, ok=False)


def run(command, cwd=None):
    p = subprocess.run(command, cwd=cwd, capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


def first_difference(built, image, base, ranges):
    """(address, built byte, image byte) of the first byte inside the ranges where they differ, or None"""
    for start, end in ranges:
        for a in range(max(start, base), min(end, base + len(built))):
            if built[a - base] != image[a]:
                return a, built[a - base], image[a]
    return None


def check(args):
    work = os.path.abspath(args.work)
    with open(os.path.join(work, 'session.json')) as f:
        ranges = ranges_of(json.load(f)['ranges'])
    with open(os.path.join(work, 'start.bin'), 'rb') as f:
        image = f.read()
    draft = os.path.join(work, 'draft')
    skool = os.path.join(draft, 'draft.skool')
    venv = os.path.abspath(args.venv)
    py = os.path.join(venv, 'bin', 'python')
    tool = lambda name: os.path.join(venv, 'bin', name)  # noqa: E731
    result = {}
    # 1. rebuild: the skool's own bytes as written (@isub operands name labels skool2bin cannot resolve: the
    # assemble gate checks them)
    for mode, flags in (('plain', []),):
        out = os.path.join(draft, f'rebuild-{mode}.bin')
        code, _, err = run([py, tool('skool2bin.py'), '-w', *flags, skool, out])
        line = next((l for l in err.splitlines() if l.startswith('Wrote')), '')
        base = int(line.split('start=')[1].split(',')[0]) if 'start=' in line else 0
        built = open(out, 'rb').read() if code == 0 and os.path.exists(out) else b''
        diff = first_difference(built, image, base, ranges) if built else None
        covered = sum(max(0, min(e, base + len(built)) - max(s, base)) for s, e in ranges)
        wanted = sum(e - s for s, e in ranges)
        result[f'rebuild-{mode}'] = {'ok': code == 0 and diff is None and covered == wanted, 'covered': covered,
                                     'of': wanted, 'first_difference': diff and '$%04X: %02X, image %02X' % diff,
                                     'error': err.strip().splitlines()[-1:] if code else []}
    # 2. assemble: skool2asm (labels made for every instruction) through sjasmplus
    if args.sjasmplus:
        asm = os.path.join(draft, 'draft.asm')
        code, text, err = run([py, tool('skool2asm.py'), '-H', '-c', skool])
        warnings = [l for l in err.splitlines() if l.startswith('WARNING')]
        # sjasmplus spells the undocumented halves IXL / IXH
        for half in ('IXl', 'IXh', 'IYl', 'IYh'):
            text = text.replace(half, half.upper())
        with open(asm, 'w') as f:
            f.write(text)
        wrapper = os.path.join(draft, 'build.asm')
        low, high = min(s for s, _ in ranges), max(e for _, e in ranges)
        with open(wrapper, 'w') as f:
            f.write(f'  DEVICE ZXSPECTRUM48\n  INCLUDE "draft.asm"\n  SAVEBIN "assembled.bin",${low:04X},{high - low}\n')
        acode, aout, aerr = run([os.path.abspath(args.sjasmplus), '--nologo', '--msg=war', 'build.asm'], cwd=draft)
        built = open(os.path.join(draft, 'assembled.bin'), 'rb').read() if acode == 0 else b''
        diff = first_difference(built, image, low, ranges) if built else None
        result['assemble'] = {'ok': code == 0 and acode == 0 and bool(built) and diff is None,
                              'skool2asm_warnings': len(warnings), 'first_warnings': warnings[:5],
                              'first_difference': diff and '$%04X: %02X, image %02X' % diff,
                              'error': (aerr or aout).strip().splitlines()[:5] if acode else []}
    # 3. score against the reference disassembly
    if args.reference:
        command = [sys.executable, os.path.join(HERE, 'score_draft.py'), skool, args.reference, '--json', '-',
                   '--fields', os.path.join(draft, 'draft.fields.json'),
                   '--geometry', os.path.join(draft, 'draft.geometry.json')]
        if args.expected:
            command += ['--expected', args.expected]
        else:
            command += ['--start', hex(min(s for s, _ in ranges)), '--end', hex(max(e for _, e in ranges))]
        code, text, err = run(command)
        result['score'] = json.loads(text) if code == 0 else {'error': err.strip().splitlines()[-3:]}
    with open(os.path.join(work, 'check.json'), 'w') as f:
        json.dump(result, f, indent=1)
    for name, value in result.items():
        if name == 'score':
            print('score   ', 'written to check.json' if 'error' not in value else value['error'])
        else:
            print(f"{name:15} {'ok' if value['ok'] else 'FAIL'} "
                  + ' '.join(f'{k}={v}' for k, v in value.items() if k != 'ok' and v not in (None, [], '')))
    if not all(v.get('ok', True) for v in result.values()):
        raise SystemExit(1)


def stop(args):
    with open(os.path.join(args.work, 'session.json')) as f:
        pid = json.load(f)['pid']
    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('capture')
    p.add_argument('--work', required=True)
    source = p.add_mutually_exclusive_group(required=True)
    source.add_argument('--media', help='a tape (tzx / tap) or, with --disk, a disk; a zip is unpacked')
    source.add_argument('--snapshot')
    p.add_argument('--disk', action='store_true')
    p.add_argument('--model', default='48K')
    p.add_argument('--inputs', nargs='*', help='input scripts played one after another (no exploration then)')
    p.add_argument('--frames', type=int, default=15000)
    p.add_argument('--ranges', default='5B00-FFFF', help='the program range of the exports and the checks (hex)')
    p.add_argument('--port-base', type=int, default=9720)
    p.add_argument('--binary', default=corpus.DEFAULT_BINARY)
    p.add_argument('--load-frames', type=int, default=30000)
    p.add_argument('--disk-boot-frames', type=int, default=1500)
    p = sub.add_parser('call')
    p.add_argument('--work', required=True)
    p.add_argument('--json')
    p.add_argument('action')
    p.add_argument('args', nargs='?')
    p = sub.add_parser('export')
    p.add_argument('--work', required=True)
    p = sub.add_parser('check')
    p.add_argument('--work', required=True)
    p.add_argument('--venv', required=True, help='a virtualenv with SkoolKit 9+ (skool2bin.py, skool2asm.py)')
    p.add_argument('--sjasmplus', help='the sjasmplus binary (the assemble gate)')
    p.add_argument('--reference', help='the reference skool to score against')
    p.add_argument('--expected', help="score_draft.py's expected file (range, records)")
    p = sub.add_parser('stop')
    p.add_argument('--work', required=True)
    args = parser.parse_args()
    {'capture': capture, 'call': call, 'export': export, 'check': check, 'stop': stop}[args.command](args)


if __name__ == '__main__':
    main()
