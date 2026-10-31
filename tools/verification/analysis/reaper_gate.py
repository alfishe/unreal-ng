#!/usr/bin/env python3
"""The P3 gate: an L2 draft (tools/verification/analysis/mm_session.py's output) through a fresh zx-reaper project
with no hand fixes, then scored against the reference skool.

  reaper_gate.py --reaper zx-reaper --tape game.tap --tests run-B/tests --draft scratch/p3/mm --work scratch/p3/gate \\
      --reference mm.skool --expected tools/verification/analysis/expected/manic-miner-records.json \\
      --venv scratch/skoolkit-venv --bin scratch/reaper-mm/bin

Steps (each one's exit status and the tail of its output in <work>/gate.json, the whole output in gate.log):
  1. zxinit.py <tape> <work>/project (the --reaper checkout's; its make must say IDENTICAL)
  2. the input scripts and their expected hashes from --tests (inputs/*.txt, expected/*.hash: the original's
     per-frame hashes, nothing the annotation wrote)
  3. zxq.py split <draft>/draft.skool re/skool (the draft replaces zxinit's skool; zxinit's build/ goes)
  4. re/layout.mk: zxinit's lines with REGION_START / SHIFT_END from <draft>/draft.layout.mk
  5. make (the tape byte-identical), make test (per-frame RAM hashes of the scripts), make shift-test SHIFT=0x100
     and SHIFT=0x101
  6. score_draft.py <draft>/draft.skool against the reference with --fields / --geometry / --expected

The PATH gets sjasmplus (--bin) and the SkoolKit venv (--venv).
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def run(cmd, cwd, env, log):
    print('$', ' '.join(cmd), flush=True)
    p = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    log.write('$ ' + ' '.join(cmd) + '\n' + p.stdout + '\n')
    tail = p.stdout.strip().splitlines()[-12:]
    print('\n'.join(tail))
    return {'cmd': ' '.join(cmd), 'status': p.returncode, 'tail': tail}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--reaper', required=True, help='a zx-reaper checkout (its tools/zxinit.py makes the project)')
    ap.add_argument('--tape', required=True, help='the original tape')
    ap.add_argument('--tests', required=True, help='a directory with inputs/*.txt and expected/*.hash')
    ap.add_argument('--draft', required=True, help="mm_session.py's output directory")
    ap.add_argument('--work', required=True)
    ap.add_argument('--reference', required=True, help='reference skool (mm.skool)')
    ap.add_argument('--expected', required=True)
    ap.add_argument('--venv', help='SkoolKit virtualenv (its bin/ goes first on PATH)')
    ap.add_argument('--bin', help='a directory with sjasmplus')
    ap.add_argument('--shifts', default='0x100,0x101')
    ap.add_argument('--frames', type=int, default=200, help="zxinit's coverage frames (its skool is replaced)")
    args = ap.parse_args()

    draft = os.path.abspath(args.draft)
    work = os.path.abspath(args.work)
    project = os.path.join(work, 'project')
    if os.path.exists(project):
        shutil.rmtree(project)
    os.makedirs(work, exist_ok=True)
    env = dict(os.environ)
    paths = []
    if args.venv:
        paths.append(os.path.join(os.path.abspath(args.venv), 'bin'))
    if args.bin:
        paths.append(os.path.abspath(args.bin))
    env['PATH'] = os.pathsep.join(paths + [env.get('PATH', '')])
    python = os.path.join(os.path.abspath(args.venv), 'bin', 'python3') if args.venv else sys.executable

    report = {'draft': draft, 'tape': os.path.abspath(args.tape), 'steps': []}
    with open(os.path.join(work, 'gate.log'), 'w') as log:
        steps = report['steps']
        steps.append(run([python, os.path.join(os.path.abspath(args.reaper), 'tools', 'zxinit.py'),
                          os.path.abspath(args.tape), project, '--frames', str(args.frames)], work, env, log))
        if steps[-1]['status'] != 0:
            return finish(report, work)
        for sub, ext in (('inputs', '.txt'), ('expected', '.hash')):
            os.makedirs(os.path.join(project, 'tests', sub), exist_ok=True)
            for name in sorted(os.listdir(os.path.join(args.tests, sub))):
                if name.endswith(ext):
                    shutil.copy(os.path.join(args.tests, sub, name), os.path.join(project, 'tests', sub, name))
        shutil.rmtree(os.path.join(project, 're', 'skool'))
        # zxinit's own build: make compares mtimes, and on a volume with whole-second times the draft's files can
        # look no newer than what zxinit built from its skool a moment ago
        shutil.rmtree(os.path.join(project, 'build'), ignore_errors=True)
        steps.append(run([python, 'tools/zxq.py', 'split', os.path.join(draft, 'draft.skool'), 're/skool'],
                         project, env, log))
        # The layout: zxinit's lines with the draft's REGION_START / SHIFT_END
        layout = {}
        with open(os.path.join(draft, 'draft.layout.mk')) as f:
            for line in f:
                m = re.match(r'(\w+)\s*:=\s*(\S+)', line)
                if m:
                    layout[m.group(1)] = m.group(2)
        lines = []
        with open(os.path.join(project, 're', 'layout.mk')) as f:
            for line in f:
                m = re.match(r'(\w+)\s*:=', line)
                if not (m and m.group(1) in layout):
                    lines.append(line)
        lines += ['%s := %s\n' % (k, v) for k, v in layout.items()]
        with open(os.path.join(project, 're', 'layout.mk'), 'w') as f:
            f.writelines(lines)
        report['layout'] = ''.join(lines)
        make = ['make', 'PY=' + python]
        steps.append(run(make, project, env, log))
        steps.append(run(make + ['test'], project, env, log))
        for shift in args.shifts.split(','):
            steps.append(run(make + ['shift-test', 'SHIFT=' + shift], project, env, log))
        score = [sys.executable, os.path.join(HERE, 'score_draft.py'), os.path.join(draft, 'draft.skool'),
                 os.path.abspath(args.reference), '--expected', os.path.abspath(args.expected),
                 '--fields', os.path.join(draft, 'draft.fields.json'),
                 '--geometry', os.path.join(draft, 'draft.geometry.json')]
        steps.append(run(score + ['--json', os.path.join(work, 'score.json')], work, env, log))
        with open(os.path.join(work, 'score.txt'), 'w') as f:
            f.write(subprocess.run(score, stdout=subprocess.PIPE, text=True).stdout)
    return finish(report, work)


def finish(report, work):
    report['passed'] = all(s['status'] == 0 for s in report['steps'])
    with open(os.path.join(work, 'gate.json'), 'w') as f:
        json.dump(report, f, indent=1)
    print('gate:', 'all steps passed' if report['passed'] else
          'failed: ' + ', '.join(s['cmd'].split(' ', 2)[-1] for s in report['steps'] if s['status'] != 0))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
