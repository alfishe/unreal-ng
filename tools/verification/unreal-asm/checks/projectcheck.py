#!/usr/bin/env python3
"""Z5's check (asm-synchronizer.md §15.3): a real sjasmplus project through a retro target and back. The project is
assembled by sjasmplus (its memory after the last line, 64K), converted with `zxasm convert` to the target (TASM,
ALASM, STORM, XAS, ZX-ASM, MASM, GENS, ZEUS, PROMETHEUS, PASM ...), converted back to sjasmplus through the target's
frontend, assembled again; the two memories must be equal. The target's warnings (what did not convert) are listed.

    projectcheck.py <main.asm> --to tasm [--to-version 4.12] [--sjasmplus PATH] [--zxasm PATH] [--keep DIR]
    projectcheck.py <main.asm>... --to alasm --summary          # one line a project

A source without DEVICE gets DEVICE ZXSPECTRUM48 around it so that its ORG'ed bytes land in memory. Sources whose
INCLUDEs lie beside them are converted file by file into one folder (`zxasm convert <dir>`). Exit 0 when every
project's memory agrees.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..'))


def device_of(folder, main):
    """The DEVICE the source picks (None: none)"""
    with open(os.path.join(folder, main), encoding='latin-1') as f:
        found = re.search(r'^\s*DEVICE\s+(\w+)', f.read(), re.I | re.M)
    return found.group(1) if found else None


def assemble(sjasmplus, folder, main, out, device=None):
    """sjasmplus on a harness around `main` (in `folder`): the 64K memory, or (None, log). Without a DEVICE of its own
    the source gets `device` (NOSLOT64K: a plain 64K, zeros; ZXSPECTRUM48 would set the system variables)"""
    with open(os.path.join(folder, main), encoding='latin-1') as f:
        text = f.read()
    own = re.search(r'^\s*DEVICE\s', text, re.I | re.M)
    # the folders its OUTPUT / SAVEBIN ... write into
    for name in re.findall(r'^\s*(?:OUTPUT|SAVEBIN|SAVESNA|SAVETAP|SAVENEX|EMPTYTAP|SAVETRD|EMPTYTRD)\s+"([^"]+)"', text, re.I | re.M):
        target = os.path.dirname(os.path.join(folder, name))
        if target.startswith(folder):
            os.makedirs(target, exist_ok=True)
    harness = ('' if own else f'        DEVICE {device or "NOSLOT64K"}\n') + f'        INCLUDE "{main}"\n'
    harness += f'        SAVEBIN "{out}",0,65536\n'
    with open(os.path.join(folder, '__harness.asm'), 'w') as f:
        f.write(harness)
    run = subprocess.run([sjasmplus, '--nologo', '--msg=war', '-I' + folder, '__harness.asm'], cwd=folder, capture_output=True, text=True,
                         errors='replace')
    path = os.path.join(folder, out)
    if run.returncode != 0 or not os.path.exists(path):
        return None, run.stdout + run.stderr
    with open(path, 'rb') as f:
        return f.read(), run.stdout + run.stderr


def convert(zxasm, source, args, out, back=False):
    """zxasm convert there or back; its warnings"""
    if back:
        command = [zxasm, 'convert', source, '--codec', args.to, '--to', 'sjasmplus', '-o', out]
        if args.to_version:
            command += ['--version', args.to_version]
    else:
        command = [zxasm, 'convert', source, '--codec', 'sjasmplus', '--to', args.to, '-o', out]
        if args.to_version:
            command += ['--to-version', args.to_version]
    run = subprocess.run(command, capture_output=True, text=True, errors='replace')
    lines = (run.stdout + run.stderr).splitlines()
    return run.returncode, [l for l in lines if l.startswith(('warning', 'error'))]


def check(path, args):
    folder = tempfile.mkdtemp(prefix='projectcheck-', dir=args.keep)
    source = os.path.dirname(os.path.abspath(path))
    main = os.path.basename(path)
    original = os.path.join(folder, 'original')
    try:
        shutil.copytree(source, original, symlinks=True, ignore_dangling_symlinks=True,
                        ignore=shutil.ignore_patterns('*.sna', '*.tap', '*.trd', '*.scl', '.git'))
    except (OSError, shutil.Error) as e:
        return {'project': path, 'state': 'copy failed', 'log': str(e)[:300]}
    # SAVESNA puts the start address on the snapshot's stack (in the memory): left out of the reference
    with open(os.path.join(original, main), 'rb') as f:
        data = f.read()
    with open(os.path.join(original, main), 'wb') as f:
        f.write(re.sub(rb'(?im)^([ \t]*SAVESNA\b)', rb';\1', data))
    device = device_of(original, main)
    reference, log = assemble(args.sjasmplus, original, main, '__memory.bin')
    if reference is None:
        return {'project': path, 'state': 'sjasmplus failed', 'log': log[-600:]}
    if args.project:
        return check_project(path, args, folder, original, main, reference, device)
    there = os.path.join(folder, 'there.' + args.to)
    code, warnings = convert(args.zxasm, os.path.join(original, main), args, there)
    if code != 0 or not os.path.exists(there):
        return {'project': path, 'state': 'conversion failed', 'warnings': warnings}
    back = os.path.join(folder, 'back')
    os.makedirs(back)
    shutil.copytree(original, back, symlinks=True, dirs_exist_ok=True)   # the data files it INCBINs
    # the files it INCLUDEs stay sjasmplus; a target without extensions in INCLUDE comes back as name.asm
    for root, _, files in os.walk(back):
        for name in files:
            if not name.lower().endswith('.asm') and not os.path.exists(os.path.join(root, name + '.asm')):
                shutil.copyfile(os.path.join(root, name), os.path.join(root, name + '.asm'))
    code, backWarnings = convert(args.zxasm, there, args, os.path.join(back, '__back.asm'), back=True)
    if code != 0:
        return {'project': path, 'state': 'back conversion failed', 'warnings': warnings + backWarnings}
    memory, log = assemble(args.sjasmplus, back, '__back.asm', '__memory.bin', device)
    if memory is None:
        return {'project': path, 'state': 'the converted text does not assemble', 'warnings': warnings, 'log': log[-600:]}
    differ = [a for a in range(65536) if reference[a] != memory[a]]
    result = {'project': path, 'state': 'equal' if not differ else f'{len(differ)} bytes differ', 'warnings': warnings,
              'written': sum(1 for b in reference if b)}
    if differ:
        a = differ[0]
        result['first'] = f'#{a:04X}: sjasmplus {reference[a:a + 8].hex()} through {args.to} {memory[a:a + 8].hex()}'
    if not args.keep:
        shutil.rmtree(folder)
    return result


def check_project(path, args, folder, original, main, reference, device):
    """The main file's folder as one project there and back (zxasm convert <dir>): its INCLUDEs converted with it"""
    # the project: the main file, what it INCLUDEs (again and again), and the data files beside them
    sources = os.path.join(folder, 'sources')
    os.makedirs(sources)
    wanted, seen = [main], set()
    while wanted:
        name = wanted.pop()
        if name.lower() in seen or not os.path.exists(os.path.join(original, name)):
            continue
        seen.add(name.lower())
        shutil.copyfile(os.path.join(original, name), os.path.join(sources, os.path.basename(name)))
        with open(os.path.join(original, name), encoding='latin-1') as f:
            wanted += re.findall(r'^[^;]*?\bINCLUDE\s+"?([^"\s;]+)', f.read(), re.I | re.M)
    for name in os.listdir(original):
        if os.path.isfile(os.path.join(original, name)) and not re.search(r'\.(asm|a80|z80|inc|s|bat|cmd|sh|txt|md|diff|lst|sym|map)$', name, re.I):
            shutil.copyfile(os.path.join(original, name), os.path.join(sources, name))
    there = os.path.join(folder, 'there')
    code, warnings = convert(args.zxasm, sources, args, there)
    if code != 0 or not os.path.isdir(there):
        return {'project': path, 'state': 'conversion failed', 'warnings': warnings}
    back = os.path.join(folder, 'back')
    code, backWarnings = convert(args.zxasm, there, args, back, back=True)
    if code != 0 or not os.path.isdir(back):
        return {'project': path, 'state': 'back conversion failed', 'warnings': warnings + backWarnings}
    stem = os.path.splitext(main)[0].lower() + '.asm'
    if not os.path.exists(os.path.join(back, stem)):
        return {'project': path, 'state': 'no ' + stem + ' came back', 'warnings': warnings}
    memory, log = assemble(args.sjasmplus, back, stem, '__memory.bin', device)
    if memory is None:
        return {'project': path, 'state': 'the converted text does not assemble', 'warnings': warnings, 'log': log[-600:]}
    differ = [a for a in range(65536) if reference[a] != memory[a]]
    result = {'project': path, 'state': 'equal' if not differ else f'{len(differ)} bytes differ', 'warnings': warnings}
    if differ:
        a = differ[0]
        result['first'] = f'#{a:04X}: sjasmplus {reference[a:a + 8].hex()} through {args.to} {memory[a:a + 8].hex()}'
    if not args.keep:
        shutil.rmtree(folder)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('main', nargs='+')
    parser.add_argument('--to', required=True)
    parser.add_argument('--to-version')
    parser.add_argument('--sjasmplus', default=os.environ.get('UNREAL_ASM_SJASMPLUS', 'sjasmplus'))
    parser.add_argument('--zxasm', default=os.environ.get('ZXASM', os.path.join(REPO, 'cmake-build-agent-release', 'bin', 'zxasm')))
    parser.add_argument('--keep', help='keep the work folders under this folder')
    parser.add_argument('--summary', action='store_true')
    parser.add_argument('--project', action='store_true', help='convert the main file\'s folder as one project (its INCLUDEs too)')
    args = parser.parse_args()
    if args.keep:
        os.makedirs(args.keep, exist_ok=True)
    ok = True
    for path in args.main:
        r = check(path, args)
        ok = ok and r['state'] == 'equal'
        if args.summary:
            print(f"{r['state']:<40} {len(r.get('warnings', []))} warnings  {path}")
            continue
        print(f"{path}: {r['state']}")
        for key in ('first', 'log'):
            if key in r:
                print(f'  {key}: {r[key]}')
        for w in r.get('warnings', [])[:40]:
            print('  ' + w)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
