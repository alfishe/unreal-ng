#!/usr/bin/env python3
"""sjasmplus sources through the rasm backend (research-sjasmplus-to-rasm.md): each main source is assembled by sjasmplus
(its 64K memory after the last line), converted with `zxasm convert --project --codec sjasmplus --to rasm` (the main source with every file it INCLUDEs and
INCBINs, into a folder of its own), assembled by rasm (its 64K memory, SAVE "...",0,65536 after the text) and the two
memories compared.

    rasmcheck.py <main.asm>... [--list FILE] [--zxasm P] [--rasm P] [--sjasmplus P] [--keep DIR] [--summary]

Only the bytes the program writes are compared: a source with a DEVICE of its own (ZXSPECTRUM48 sets up the system
variables and attributes) is compared where its memory differs from that DEVICE's empty memory, rasm's memory starts
with zeros. Tools: --zxasm / UNREAL_ASM_ZXASM (default <repo>/cmake-build-agent-release/bin/zxasm), --rasm /
UNREAL_ASM_RASM, --sjasmplus / UNREAL_ASM_SJASMPLUS. Work files: scratch/rasmcheck (kept with --keep). Exit 0 when
every source sjasmplus builds is equal through rasm.
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
TIMEOUT = 120


def run(argv, cwd):
    try:
        return subprocess.run(argv, cwd=cwd, capture_output=True, text=True, errors='replace', timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return subprocess.CompletedProcess(argv, -1, '', 'timeout')


def device_of(text):
    found = re.search(r'^\s*DEVICE\s+([^;\n]+)', text, re.I | re.M)
    return found.group(1).strip() if found else None


def sjasmplus_memory(sjasmplus, folder, main, device):
    """The 64K memory sjasmplus builds from `main` (a harness INCLUDEs it), and that DEVICE's memory with no program
    (None, log when it does not build)"""
    with open(os.path.join(folder, main), encoding='latin-1') as f:
        text = f.read()
    for name in re.findall(r'^\s*(?:OUTPUT|SAVEBIN|SAVESNA|SAVETAP|SAVENEX|EMPTYTAP|SAVETRD|EMPTYTRD)\s+"([^"]+)"', text, re.I | re.M):
        target = os.path.dirname(os.path.join(folder, name.replace('\\', '/')))
        if target.startswith(folder):
            os.makedirs(target, exist_ok=True)
    own = device is not None
    images = []
    for body in (f'        INCLUDE "{main}"\n', ''):
        if own and not body:
            harness = f'        DEVICE {device}\n'
        elif own:
            harness = body
        else:
            harness = '        DEVICE NOSLOT64K\n' + body
        harness += '        SAVEBIN "__memory.bin",0,65536\n'
        with open(os.path.join(folder, '__harness.asm'), 'w') as f:
            f.write(harness)
        path = os.path.join(folder, '__memory.bin')
        if os.path.exists(path):
            os.remove(path)
        r = run([sjasmplus, '--nologo', '--msg=war', '-I' + folder, '__harness.asm'], folder)
        if r.returncode != 0 or not os.path.exists(path):
            return None, None, r.stdout + r.stderr
        with open(path, 'rb') as f:
            images.append(f.read())
    return images[0], images[1], ''


def rasm_memory(rasm, folder, text_name):
    """The 64K memory rasm builds from the converted text (None, log when it does not build)"""
    with open(os.path.join(folder, '__rasm.asm'), 'w') as f:
        f.write(f'        INCLUDE "{text_name}"\n        SAVE "__rasm.bin",0,65536\n')
    # rasm makes no folders for its SAVEs (sjasmplus does)
    for name in os.listdir(folder):
        if name.endswith('.asm'):
            with open(os.path.join(folder, name), 'rb') as f:
                for saved in re.findall(rb'(?im)^\s+SAVE\s+"([^"]+)"', f.read()):
                    os.makedirs(os.path.join(folder, os.path.dirname(saved.decode('latin-1'))), exist_ok=True)
    path = os.path.join(folder, '__rasm.bin')
    if os.path.exists(path):
        os.remove(path)
    r = run([rasm, '__rasm.asm', '-o', '__rasm_out'], folder)
    log = re.sub(r'\x1b\[[0-9;]*m', '', r.stdout + r.stderr)
    if r.returncode != 0 or not os.path.exists(path):
        return None, log
    with open(path, 'rb') as f:
        data = f.read()
    return data + bytes(65536 - len(data)), log


def check(path, args, work):
    folder = tempfile.mkdtemp(prefix='rasmcheck-', dir=work)
    source = os.path.dirname(os.path.abspath(path))
    main = os.path.basename(path)
    original = os.path.join(folder, 'original')
    try:
        shutil.copytree(source, original, symlinks=True, ignore_dangling_symlinks=True,
                        ignore=shutil.ignore_patterns('*.sna', '*.tap', '*.trd', '*.scl', '.git'))
    except (OSError, shutil.Error) as e:
        return {'source': path, 'state': 'copy failed', 'log': str(e)[:300]}
    with open(os.path.join(original, main), 'rb') as f:
        data = f.read()
    # SAVESNA puts the start address on the snapshot's stack (in the memory): left out of the reference
    with open(os.path.join(original, main), 'wb') as f:
        f.write(re.sub(rb'(?im)^([ \t]*SAVESNA\b)', rb';\1', data))
    device = device_of(data.decode('latin-1'))
    reference, empty, log = sjasmplus_memory(args.sjasmplus, original, main, device)
    if reference is None:
        return {'source': path, 'state': 'sjasmplus failed', 'log': log[-400:]}
    there = os.path.join(folder, 'rasm')
    c = run([args.zxasm, 'convert', main, '--project', '--codec', 'sjasmplus', '--to', 'rasm', '-o', there], original)
    warnings = [l for l in (c.stdout + c.stderr).splitlines() if l.startswith(('warning', 'error'))]
    # the main source's name in the project: the "<stem> -> <name>" line of the conversion
    stem = os.path.splitext(main)[0]
    names = dict(re.findall(r'(?m)^\s+(\S+) -> (\S+)$', c.stdout + c.stderr))
    converted = names.get(stem, stem) + '.asm'
    if c.returncode != 0 or not os.path.exists(os.path.join(there, converted)):
        return {'source': path, 'state': 'conversion failed', 'warnings': warnings, 'log': (c.stdout + c.stderr)[-400:]}
    memory, log = rasm_memory(args.rasm, there, converted)
    if memory is None:
        errors = [l for l in log.splitlines() if l.startswith('[') or 'rror' in l]
        return {'source': path, 'state': 'rasm does not build it', 'warnings': warnings, 'log': '\n'.join(errors[:6]) or log[-400:]}
    # a byte the program wrote: where its memory differs from the DEVICE's empty one; elsewhere rasm leaves 0 or the same
    differ = [a for a in range(65536)
              if (reference[a] != empty[a] and memory[a] != reference[a]) or (reference[a] == empty[a] and memory[a] not in (0, empty[a]))]
    written = sum(1 for a in range(65536) if reference[a] != empty[a])
    result = {'source': path, 'state': 'equal' if not differ else f'{len(differ)} bytes differ', 'warnings': warnings, 'written': written}
    if differ:
        a = differ[0]
        result['first'] = f'#{a:04X}: sjasmplus {reference[a:a + 8].hex()} rasm {memory[a:a + 8].hex()}'
    if not args.keep:
        shutil.rmtree(folder, ignore_errors=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('main', nargs='*')
    parser.add_argument('--list', help='a file of main sources, one a line')
    parser.add_argument('--zxasm', default=os.environ.get('UNREAL_ASM_ZXASM', os.path.join(REPO, 'cmake-build-agent-release', 'bin', 'zxasm')))
    parser.add_argument('--rasm', default=os.environ.get('UNREAL_ASM_RASM', 'rasm'))
    parser.add_argument('--sjasmplus', default=os.environ.get('UNREAL_ASM_SJASMPLUS', 'sjasmplus'))
    parser.add_argument('--keep', help='keep the work folders under this folder')
    parser.add_argument('--summary', action='store_true', help='one line a source')
    args = parser.parse_args()
    sources = list(args.main)
    if args.list:
        with open(args.list) as f:
            sources += [l.strip() for l in f if l.strip() and not l.startswith('#')]
    if not sources:
        parser.error('no sources')
    work = os.path.abspath(args.keep or os.path.join(REPO, 'scratch', 'rasmcheck'))
    os.makedirs(work, exist_ok=True)
    counts = {}
    failed = False
    for path in sources:
        r = check(path, args, work)
        state = r['state'] if not r['state'].endswith('differ') else 'bytes differ'
        counts[state] = counts.get(state, 0) + 1
        failed = failed or r['state'] not in ('equal', 'sjasmplus failed')
        if args.summary:
            print(f"{r['state']:<28} {len(r.get('warnings', [])):>3} warnings  {r.get('written', 0):>6} bytes  {path}")
            continue
        print(f"{path}: {r['state']}")
        for key in ('first', 'log'):
            if key in r:
                print(f'  {key}: ' + r[key].replace('\n', '\n    '))
        for w in r.get('warnings', [])[:20]:
            print('  ' + w)
    print('; '.join(f'{k}: {v}' for k, v in sorted(counts.items())))
    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
