#!/usr/bin/env python3
"""Bank corpus check for libsam2695: load every SF2 / SF3 bank under a folder, render a GM test file through
each, and report refusals (with the loader's reason), NaN / denormal output and the render peak.

    ./bankcorpus.py [--root testdata/midi] [--render PATH] [--out DIR] [--timeout 900]

The GM test file (written to the output folder) plays all 128 programs in turn on the 15 melodic channels with
chords, pan and volume changes, pitch bend, modulation wheel and bank-select variations (8, 16, 127), then every
drum note 27..87 on channel 10 across the GS drum kits. One JSON line per bank goes to bankcorpus.jsonl; the
summary is printed.
"""
import argparse
import json
import subprocess
import sys
import time
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
from smfwrite import Bend, Cc, NoteOff, NoteOn, Program, WriteSmf  # noqa: E402


def WriteGmTest(path):
    events = []
    melodic = [c for c in range(16) if c != 9]
    t = 0.0
    for p in range(128):
        ch = melodic[p % len(melodic)]
        variation = {5: 8, 17: 16, 40: 127, 61: 8, 80: 127}.get(p, 0)
        events += [(t, Cc(ch, 0, variation)), (t, Program(ch, p)), (t, Cc(ch, 7, 90 + p % 38)),
                   (t, Cc(ch, 10, (p * 37) % 128)), (t, Cc(ch, 1, 64 if p % 5 == 0 else 0))]
        key = 40 + (p * 7) % 36
        for k in (key, key + 4, key + 7):
            events += [(t + 0.01, NoteOn(ch, k, 50 + (p * 13) % 78)), (t + 0.35, NoteOff(ch, k))]
        if p % 7 == 0:
            events += [(t + 0.15, Bend(ch, 0x3000)), (t + 0.3, Bend(ch, 0x2000))]
        t += 0.12
    t += 0.5
    for kit in (0, 8, 16, 24, 25, 32, 40, 48, 56, 127):
        events.append((t, Program(9, kit)))
        for k in range(27, 88, 3):
            events += [(t + 0.01, NoteOn(9, k, 100)), (t + 0.2, NoteOff(9, k))]
            t += 0.03
        t += 0.1
    WriteSmf(path, events)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=str(REPO_ROOT / 'testdata' / 'midi'))
    ap.add_argument('--render', default=str(REPO_ROOT / 'cmake-build-agent-release' / 'bin' / 'sam2695render'))
    ap.add_argument('--out', default=str(HERE / 'out'))
    ap.add_argument('--timeout', type=int, default=900)
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    midi = out / 'gm-test.mid'
    WriteGmTest(str(midi))
    banks = sorted(p for p in Path(args.root).rglob('*') if p.is_file() and p.suffix.lower() in ('.sf2', '.sf3'))
    results = []
    started = time.time()
    with open(out / 'bankcorpus.jsonl', 'w') as log:
        for i, bank in enumerate(banks):
            rel = str(bank.relative_to(args.root))
            try:
                run = subprocess.run([args.render, '--bank', str(bank), '--midi', str(midi), '--check'],
                                     capture_output=True, text=True, errors='replace',
                                     timeout=args.timeout)
                line = run.stdout.strip().splitlines()[-1] if run.stdout.strip() else ''
                r = json.loads(line) if line.startswith('{') else {'ok': False, 'error': 'Crash',
                                                                    'reason': 'exit %d: %s' % (run.returncode,
                                                                                              run.stderr.strip()[-200:])}
            except subprocess.TimeoutExpired:
                r = {'ok': False, 'error': 'Timeout', 'reason': 'more than %d s' % args.timeout}
            r['bank'] = rel
            r['bytes'] = bank.stat().st_size
            results.append(r)
            log.write(json.dumps(r) + '\n')
            log.flush()
            print('[%3d/%d] %-70s %s' % (i + 1, len(banks), rel[:70],
                                         'ok' if r.get('ok') else '%s: %s' % (r.get('error'), r.get('reason'))),
                  flush=True)
    loaded = [r for r in results if r.get('ok')]
    refused = [r for r in results if not r.get('ok')]
    print('\n%d banks, %.1f GB, %d loaded, %d refused (%.0f s)' %
          (len(results), sum(r['bytes'] for r in results) / 1e9, len(loaded), len(refused), time.time() - started))
    for reason, count in Counter(r.get('error') for r in refused).most_common():
        print('  refused %-14s %d' % (reason, count))
    for r in refused:
        print('    %s: %s' % (r['bank'], r.get('reason')))
    bad = [r for r in loaded if r.get('nan') or r.get('denormal')]
    print('NaN / denormal in the output: %d banks' % len(bad))
    for r in bad:
        print('    %s: nan %s denormal %s' % (r['bank'], r.get('nan'), r.get('denormal')))
    loud = [r for r in loaded if r.get('peak', 0) > 1.0]
    print('peak above full scale (gain 0.25): %d banks' % len(loud))
    print('banks with loader warnings: %d (median %d warnings)' %
          (sum(1 for r in loaded if r.get('warnings')),
           sorted(r.get('warnings', 0) for r in loaded)[len(loaded) // 2] if loaded else 0))
    print('sm24 banks: %d' % sum(1 for r in loaded if r.get('sm24')))
    slow = sorted(loaded, key=lambda r: -(r.get('loadMs', 0)))[:3]
    for r in slow:
        print('  slowest load %s: %.1f s, %.2f GB' % (r['bank'], r['loadMs'] / 1000, r['bytes'] / 1e9))
    return 0 if not bad else 1


if __name__ == '__main__':
    sys.exit(main())
