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
      on one sheet (W/screens.png), and keeps the recording (W/session.ttd). The instance stays up:
      W/session.json holds its process and ports. --record-load records from power-on, the load included: the
      stages before the game (a loader in a REM, a depacker) are in the recording (no explorer: the --inputs
      scripts, else seeded random play); W/start.bin is then the memory at the recording's first keyframe.
  re_session.py open --work W --ttd session.ttd [--model 48K] [--port-base 9720]
      A new instance with a saved recording loaded: one recording serves every later build.
  re_session.py build --work W [--stage N | --from-frame F [--from-tinframe T]] [--to-frame G] [--into W2]
      Builds L1 / L2 again from the loaded recording, a window of it for one stage; writes l1.json, start.bin (the
      memory at the window's start, with no window at the recording's first keyframe), review.txt and findings.txt
      (into W2: the stage's own folder, which shares the instance but does not own it).
  re_session.py stages --work W
      The code arrivals of the last build: the stages and the window to build each (--to-frame: the next
      arrival's frame + 1).
  re_session.py interrupts --work W
      The interrupt handlers of the last build (l1.json): each routine entered by an interrupt, the single tail jumps
      it runs on through, its entries and entries a frame, T-states an entry, its share of the CPU and its callees.
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
      Ends the instance the folder launched, once its pid is checked to still be that unreal-qt.

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
        json.dump({'pid': process.pid, 'binary': args.binary, 'port_base': args.port_base, 'model': args.model,
                   'ranges': args.ranges}, f)
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
    if args.record_load:
        # The recording starts at power-on: the load and every stage before the game (a loader in a REM, a depacker,
        # a title overlay) are in it; build --from / --to cuts one stage out. The explorer loads saved states, which
        # a recording cannot cross: random play only
        m.post('/snapshot/save', {'path': os.path.join(work, 'power-on.szx'), 'force': True})
        s.call('analysis', {'action': 'start', 'record': True})
    if args.snapshot:
        m.post('/snapshot/load', {'path': os.path.abspath(args.snapshot)})
    else:
        media = os.path.abspath(args.media)
        if media.endswith('.zip'):
            media = corpus.unpack(media, work, corpus.MEDIA_SUFFIXES['disk' if args.disk else 'tape'])
        game = {'kind': 'disk' if args.disk else 'tape', 'model': args.model}
        report['load_frames'] = corpus.load(m, game, media, report, args)
    if not args.inputs and not args.record_load:
        explorer = corpus.Explorer(m, player, work)
        steps, trace, reached = explorer.run()
        report['explore'] = {'keys': steps, 'reached_control': reached, 'trace': trace}
        explorer.replay()
    explorer_start = explorer.start if explorer else None
    m.post('/snapshot/save', {'path': os.path.join(work, 'start.szx'), 'force': True})
    with open(os.path.join(work, 'start.bin'), 'wb') as f:
        f.write(m.read(0, 0x10000))
    if not args.record_load:
        s.call('analysis', {'action': 'start', 'record': True})
    screens = os.path.join(work, 'screens')
    os.makedirs(screens, exist_ok=True)
    frames = 0
    if args.inputs:
        for path in args.inputs:
            frames += play_script(s, m, path, screens, frames)
    else:
        if explorer:
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
    built = s.call('analysis', {'action': 'build', 'path': os.path.join(work, 'l1.json'), 'wait': 30000})
    state = s.call('analysis', {'action': 'status'}).text
    while ': running' in state:
        time.sleep(5)
        state = s.call('analysis', {'action': 'status'}).text
    report['status'] = state.splitlines()
    if args.record_load:
        # The recording starts at power-on, the build replays from there: start.bin is the memory at its first
        # keyframe, not after the load
        frame, tinframe = write_start_memory(s, os.path.join(work, 'start.bin'), bundle_first_frame(built))
        report['start'] = {'frame': frame, 'tinframe': tinframe}
    # The recording, kept: `open` loads it into a new instance, `build` replays any window of it again
    dumped = s.call('time_travel', {'action': 'dump', 'path': os.path.join(work, 'session.ttd')}, ok=False)
    report['ttd'] = (dumped.text or '').splitlines()[:1]
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


def read_memory(s, frame, tinframe):
    """The 64 K Z80 view at a moment of the recording (TTD memory_at): (bytes, None), or (None, the refusal)"""
    data = bytearray()
    for offset in range(0, 0x10000, 0x4000):
        r = s.call('time_travel', {'action': 'memory_at', 'space': 'cpu', 'frame': frame, 'tinframe': tinframe,
                                   'offset': offset, 'length': 0x4000}, ok=False)
        body = r.structured or {}
        if r.is_error or not body.get('exact', False) or len(body.get('hex', '')) != 0x8000:
            return None, f'memory_at frame {frame} t {tinframe} offset {offset:#x}: {r.text}'
        data += bytes.fromhex(body['hex'])
    return bytes(data), None


def write_memory(s, path, frame, tinframe):
    """The 64 K Z80 view at a moment of the recording, as start.bin is for the capture's start"""
    data, error = read_memory(s, frame, tinframe)
    if data is None:
        raise SystemExit(error)
    with open(path, 'wb') as f:
        f.write(data)


def bundle_first_frame(reply):
    """The recording's first frame from a build's answer (its bundle: the keyframes the build replays from)"""
    return int(((reply.structured or {}).get('bundle') or {}).get('first_frame', 0))


def write_start_memory(s, path, frame):
    """start.bin of a build with no window: the memory at the recording's first keyframe, which may lie inside its
    first frame (a recording started mid-frame). The frame's start first; refused (before the keyframe), a seek there
    names the earliest moment kept. Returns (frame, tinframe) read"""
    data, error = read_memory(s, frame, 0)
    if data is None:
        r = s.call('time_travel', {'action': 'seek', 'frame': frame, 'tinframe': 0}, ok=False)
        earliest = (r.structured or {}).get('earliest') or {}
        if 'frame' not in earliest:
            raise SystemExit(error)
        frame, tinframe = int(earliest['frame']), int(earliest.get('tinframe', 0))
        write_memory(s, path, frame, tinframe)
        return frame, tinframe
    with open(path, 'wb') as f:
        f.write(data)
    return frame, 0


def open_session(args):
    """A new instance with a saved recording loaded (the capture's session.ttd or any .ttd): build, review and export
    work on it as on the capture's own"""
    work = os.path.abspath(args.work)
    os.makedirs(work, exist_ok=True)
    process = mcplib.launch(args.binary, args.port_base)
    with open(os.path.join(work, 'session.json'), 'w') as f:
        json.dump({'pid': process.pid, 'binary': args.binary, 'port_base': args.port_base, 'model': args.model,
                   'ranges': args.ranges}, f)
    s = mcplib.Session(f'http://127.0.0.1:{args.port_base + 200}/mcp', args.model)
    s.fresh_machine(args.model, ram_power_on='zero')
    s.call('control_execution', {'action': 'take_control'}, ok=False)
    s.call('emulator_manage', {'action': 'slots_plug', 'slot': 'edge.1', 'card': 'kempston'}, ok=False)
    for _ in range(100):  # the plug restarts the machine: wait for the wired stick
        if 'slots_plug slot=edge.1' not in s.call('joystick_input', {'action': 'status'}, ok=False).text:
            break
        time.sleep(0.1)
    s.call('control_execution', {'action': 'pause'}, ok=False)
    s.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/audio/mixer/master',
                          'body': {'muted': True}}, ok=False)
    r = s.call('time_travel', {'action': 'load', 'path': os.path.abspath(args.ttd)}, ok=False)
    print((r.text or '').splitlines()[0])
    if r.is_error:
        raise SystemExit(1)
    print(f"MCP http://127.0.0.1:{args.port_base + 200}/mcp  WebAPI http://127.0.0.1:{args.port_base}")


def build(args):
    """One build of the loaded recording, a window of it with --from / --to (a stage: `stages` lists them); writes
    l1.json, start.bin (the memory at the window's start: the image the draft decodes from and the rebuild gate
    compares with), review.txt and findings.txt"""
    s, _ = session_of(args.work)
    work = os.path.abspath(args.work)
    out = os.path.abspath(args.into) if args.into else work
    if out != work:
        # The stage's folder works on the same instance: call / export / check --work <into>
        os.makedirs(out, exist_ok=True)
        # Without the pid: only the instance's own folder stops it (a second stop could hit a reused pid)
        with open(os.path.join(work, 'session.json')) as f:
            shared = json.load(f)
        shared.pop('pid', None)
        shared['instance_of'] = shared.get('instance_of', work)
        with open(os.path.join(out, 'session.json'), 'w') as f:
            json.dump(shared, f)
    body = {'action': 'build', 'path': os.path.join(out, 'l1.json'), 'wait': 30000}
    frame, tinframe = 0, 0
    if args.stage is not None:
        with open(os.path.join(work, 'l1.json')) as f:
            arrivals = json.load(f).get('arrivals', [])
        if not 0 <= args.stage < len(arrivals):
            raise SystemExit(f'stage {args.stage}: the last build lists {len(arrivals)} arrivals')
        frame, tinframe = arrivals[args.stage]['frame'], arrivals[args.stage]['tinframe']
    if args.from_frame is not None:
        frame, tinframe = args.from_frame, args.from_tinframe
    if frame or tinframe:
        body.update(from_frame=frame, from_tinframe=tinframe)
    if args.to_frame:
        body['to_frame'] = args.to_frame
    started = time.monotonic()
    r = s.call('analysis', body, ok=False)
    if r.is_error:
        raise SystemExit(r.text)
    state = s.call('analysis', {'action': 'status'}).text
    while ': running' in state:
        time.sleep(2)
        state = s.call('analysis', {'action': 'status'}).text
    if frame or tinframe:
        write_memory(s, os.path.join(out, 'start.bin'), frame, tinframe)
    else:
        # No window: the replay starts at the recording's first keyframe, frame 0 may not be recorded
        frame, tinframe = write_start_memory(s, os.path.join(out, 'start.bin'), bundle_first_frame(r))
    with open(os.path.join(out, 'review.txt'), 'w') as f:
        f.write(s.call('analysis', {'action': 'review', 'op': 'list'}).text + '\n')
    with open(os.path.join(out, 'findings.txt'), 'w') as f:
        f.write(s.call('analysis', {'action': 'findings', 'limit': 0}, ok=False).text + '\n')
    print(state.splitlines()[-1] if state else '')
    print(f'window from frame {frame} t {tinframe}' + (f' to {args.to_frame}' if args.to_frame else '') +
          f', {time.monotonic() - started:.1f} s')


def stages(args):
    """The arrivals of the last build (l1.json): where execution first entered code the recording wrote, one line
    each with the window to build that stage from"""
    with open(os.path.join(args.work, 'l1.json')) as f:
        arrivals = json.load(f).get('arrivals', [])
    for i, a in enumerate(arrivals):
        # The window ends before --to-frame: up to the next arrival's frame included, so a next arrival in the same
        # frame leaves a window of one frame, not an empty one the build refuses
        to = arrivals[i + 1]['frame'] + 1 if i + 1 < len(arrivals) else None
        if to is not None and to <= a['frame']:
            to = None
        print(f"{i:3}  {a['at']}  frame {a['frame']} t {a['tinframe']}  written {a['written_from']}-{a['written_to']}"
              f" by {a['writer'] or '?'}, entered from {a['from'] or '-'}"
              f"  (build --stage {i}{f' --to-frame {to}' if to else ''})")
    if not arrivals:
        print('no arrivals: the recording ran no code it wrote')


def interrupts(args):
    """The interrupt handlers of the last build (l1.json), found by L1 with no breakpoint: each routine entered by an
    interrupt (or an NMI), the jumps it runs on through (an IM 2 vector slot's JP, a ROM's JP to a hook), how often it
    ran, what one entry costs and what it calls"""
    with open(os.path.join(args.work, 'l1.json')) as f:
        doc = json.load(f)
    routines, calls, replay = doc['routines'], doc['calls'], doc['replay']
    out = {}
    for c in calls:
        out.setdefault(c['caller'], []).append(c)
    found = False
    for i, r in enumerate(routines):
        kinds = r['kinds'].split(',') if isinstance(r['kinds'], str) else r['kinds']
        if not any(k.strip() in ('interrupt', 'nmi') for k in kinds):
            continue
        found = True
        chain, at, seen = [i], i, {i}
        while True:  # a handler of one jump runs on into the next routine (a tail call)
            tails = [c['callee'] for c in out.get(at, []) if c['kind'] == 'tail' and c['callee'] not in seen]
            if len(tails) != 1:
                break
            at = tails[0]
            chain.append(at)
            seen.add(at)
        p = r['profile']
        entries = max(1, p['entries'])
        inclusive = p['tstates_inclusive']  # the entry's: the chain and everything it calls
        frames = max(1, replay['frames'])
        print(f"{r['kinds']} {' -> '.join(routines[k]['entry'] for k in chain)}: {p['entries']} entries in {frames} "
              f"frames ({p['entries'] / frames:.2f} a frame), {inclusive // entries} T an entry, "
              f"{100.0 * inclusive / max(1, replay['tstates']):.1f} % of the CPU")
        for k in chain:
            for c in sorted(out.get(k, []), key=lambda c: -c['count']):
                if c['kind'] != 'tail' or c['callee'] not in seen:
                    print(f"    {routines[k]['entry']} {c['kind']} {routines[c['callee']]['entry']} x{c['count']}")
    if not found:
        print('no routine was entered by an interrupt: interrupts off (DI), or IM 1 into the ROM outside --ranges')


def stop(args):
    with open(os.path.join(args.work, 'session.json')) as f:
        state = json.load(f)
    if 'pid' not in state:
        print(f"the instance is {state.get('instance_of', 'another folder')}'s: stop it with --work there")
        return
    pid = state['pid']
    # The pid may be another process's by now (the instance ended, the pid reused): only the launched binary,
    # offscreen, gets the signal
    p = subprocess.run(['ps', '-p', str(pid), '-o', 'command='], capture_output=True, text=True)
    command = p.stdout.strip()
    if p.returncode != 0 or not command:
        return  # gone already
    if '-platform offscreen' not in command or state.get('binary', 'unreal-qt') not in command:
        print(f'pid {pid} is no longer the instance ({command[:120]}): not stopped')
        return
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
    p.add_argument('--record-load', action='store_true',
                   help='record from power-on, the load included (the stages before the game: build --from / --to)')
    p.add_argument('--disk-boot-frames', type=int, default=1500)
    p = sub.add_parser('open')
    p.add_argument('--work', required=True)
    p.add_argument('--ttd', required=True, help="a saved recording (a capture's session.ttd)")
    p.add_argument('--model', default='48K')
    p.add_argument('--ranges', default='5B00-FFFF')
    p.add_argument('--port-base', type=int, default=9720)
    p.add_argument('--binary', default=corpus.DEFAULT_BINARY)
    p = sub.add_parser('build')
    p.add_argument('--work', required=True)
    p.add_argument('--stage', type=int, help="build from the last build's arrival N (stages lists them)")
    p.add_argument('--from-frame', type=int)
    p.add_argument('--from-tinframe', type=int, default=0)
    p.add_argument('--to-frame', type=int)
    p.add_argument('--into', help="the stage's own folder (default: --work); later calls take it as --work")
    p = sub.add_parser('stages')
    p.add_argument('--work', required=True)
    p = sub.add_parser('interrupts')
    p.add_argument('--work', required=True)
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
    {'capture': capture, 'open': open_session, 'build': build, 'stages': stages, 'interrupts': interrupts, 'call': call, 'export': export,
     'check': check, 'stop': stop}[args.command](args)


if __name__ == '__main__':
    main()
