#!/usr/bin/env python3
"""The part B oracle (asm-synchronizer.md §18): a host source written into a running assembler through the WebAPI
(POST /asm/sync/inject), assembled there by the assembler itself, and its bytes compared with what sjasmplus builds
from the same source. The assembler disk is the collection's; the tool creates its own instance and deletes it.

    inject-oracle.py alasm509 <ALASM509.trd> <source.asm> <address> [--sjasmplus PATH] [--port P] [--keep]
    inject-oracle.py alasm <disk> <source.asm> <address> --boot NAME --assembler alasm-4.44 [--help-key]   # any build
    inject-oracle.py alasm509 --template <alasm509.szx|.sna|.z80> <source.asm> <address> [--zxasm PATH] ...
    inject-oracle.py alasm <disk> <main.asm> <address> --boot NAME --assembler alasm-X --project   # with its INCLUDEs
    inject-oracle.py xas <XAS disk> <source.asm> <address> --boot XAS7.447 --assembler xas-7.447 [--combo-frames 12]

XAS (Z3): started from its disk, the first source of its list loaded (or a new text), its command line opened (EXT;
9.x need the keys held 12 frames), the source written there and assembled with A.

With --project the source goes as a path, and each file it INCLUDEs into an ALASM text page of its own (the INCLUDE
names the page); --template always takes the INCLUDEs (zxasm inject reads them beside the source).

With --template (Z1) the source goes into the template snapshot with `zxasm inject` instead, without an emulator; the
snapshot written is then loaded into a fresh instance and assembled there (make-template.py makes templates).

The source is sjasmplus (any dialect with a frontend works with --from). ALASM 5.09 assembles #8000-#BFFF into its
system page: give sources that ORG below #8000. Exit 0 when the bytes agree; a screenshot of the assembler's report
is saved next to the source as <source>.<assembler>.png.
"""
import argparse
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib'))
from emulator import DEFAULT_PORT, Emulator  # noqa: E402


def sjasmplus_bytes(sjasmplus, source, address):
    """What sjasmplus builds from `source`, from `address` to the end"""
    with tempfile.TemporaryDirectory() as tmp:
        harness = os.path.join(tmp, 'harness.asm')
        out = os.path.join(tmp, 'out.bin')
        with open(harness, 'w') as f:
            f.write(f'        DEVICE ZXSPECTRUM48\n        INCLUDE "{os.path.abspath(source)}"\n'
                    f'        SAVEBIN "{out}",{address},$-{address}\n')
        run = subprocess.run([sjasmplus, '--nologo', harness], capture_output=True, text=True)
        if run.returncode != 0:
            raise SystemExit('sjasmplus: ' + run.stdout + run.stderr)
        with open(out, 'rb') as f:
            return f.read()


def alasm509(emu, args, expected):
    if args.assembler == 'alasm':
        args.boot = args.boot or 'alasm64'
    else:
        args.boot, args.descriptor = 'alasm64', 'alasm-5.09'

    if args.template:
        # zxasm writes the source into the template; the snapshot it writes is loaded as it is
        injected = os.path.abspath(args.source) + '.injected' + os.path.splitext(args.disk)[1]
        run = subprocess.run([args.zxasm, 'inject', args.source, args.disk, '-o', injected, '--from', args.from_,
                              '--title', 'INJECTED', '--assembler', args.descriptor], capture_output=True, text=True)
        print('zxasm inject:', run.stderr.strip().splitlines()[-1] if run.stderr.strip() else run.returncode)
        if run.returncode != 0:
            raise SystemExit(run.stderr)
        emu.stop_recording()
        reply = emu.post('/snapshot/load', {'path': injected})
        if not reply.get('ok', True) or reply.get('error'):
            raise SystemExit(f'snapshot load: {reply}')
        emu.ensure_running()
        time.sleep(1)
    else:
        emu.insert_disk(os.path.abspath(args.disk))
        emu.run_trdos(args.boot)
        time.sleep(3)                                  # the memory test, then the A> prompt
        if args.help_key:
            emu.tap('enter')                           # 3.8c shows its help screen first
            time.sleep(1)
        body = {'from': args.from_, 'assembler': args.descriptor, 'title': 'INJECTED'}
        if args.project:
            body['path'] = os.path.abspath(args.source)    # the emulator reads the sources it INCLUDEs beside it
        else:
            with open(args.source, encoding='utf-8') as f:
                body['text'] = f.read()
        reply = emu.post('/asm/sync/inject', body)
        print('inject:', {k: reply.get(k) for k in ('assembler', 'name', 'page', 'bytes', 'lines', 'previous', 'includes', 'message')})
        for d in reply.get('diagnostics', []):
            if d.get('severity') != 'info':
                print('  ', d)
    emu.type('a')                                  # ASSEMBLE
    emu.tap('enter')
    time.sleep(args.wait)
    emu.screenshot(os.path.abspath(args.source) + '.' + args.descriptor + '.png')
    return bytes(emu.read(args.address, len(expected)))


def xas(emu, args, expected):
    emu.insert_disk(os.path.abspath(args.disk))
    emu.run_trdos(args.boot)
    time.sleep(4)                                      # the file list
    for _ in range(args.right):
        emu.tap('right')                               # past XMACROS (4.x / 5.05SE list the macro file first)
    emu.tap('enter')                                   # the first source (or a new text) into the editor
    time.sleep(3)
    emu.post('/keyboard/combo', {'keys': ['cs', 'ss'], 'frames': args.combo_frames})   # EXT: the command line
    time.sleep(1)
    body = {'from': args.from_, 'assembler': args.descriptor, 'title': 'INJECTED'}
    with open(args.source, encoding='utf-8') as f:
        body['text'] = f.read()
    reply = emu.post('/asm/sync/inject', body)
    print('inject:', {k: reply.get(k) for k in ('assembler', 'name', 'page', 'bytes', 'lines', 'previous', 'message')})
    for d in reply.get('diagnostics', []):
        if d.get('severity') != 'info':
            print('  ', d)
    emu.tap('a')                                       # ASSEMBLE
    time.sleep(args.wait)
    emu.screenshot(os.path.abspath(args.source) + '.' + args.descriptor + '.png')
    return bytes(emu.read(args.address, len(expected)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('assembler', choices=['alasm509', 'alasm', 'xas'])
    parser.add_argument('disk', help='the assembler disk, or with --template an idle template snapshot')
    parser.add_argument('--template', action='store_true', help='inject with zxasm into the template snapshot instead (Z1)')
    parser.add_argument('--zxasm', default=os.environ.get('ZXASM', 'zxasm'))
    parser.add_argument('--boot', help='alasm: the BASIC program that starts it (default alasm64)')
    parser.add_argument('--assembler', dest='descriptor', default='alasm-5.09', help='alasm: the descriptor (alasm-4.44 ...)')
    parser.add_argument('--project', action='store_true',
                        help='give the source as a path: the files it INCLUDEs go into texts of their own (ALASM pages)')
    parser.add_argument('--combo-frames', type=int, default=4, help='xas: frames EXT is held (9.x: 12)')
    parser.add_argument('--right', type=int, default=0, help='xas: cursor moves right in the file list before loading (past XMACROS)')
    parser.add_argument('--help-key', action='store_true', help='alasm: a key closes a help screen at the start (3.8c)')
    parser.add_argument('source')
    parser.add_argument('address', type=lambda v: int(v, 0))
    parser.add_argument('--from', dest='from_', default='sjasmplus', help='the source\'s dialect (default sjasmplus)')
    parser.add_argument('--sjasmplus', default=os.environ.get('UNREAL_ASM_SJASMPLUS', 'sjasmplus'))
    parser.add_argument('--wait', type=float, default=3, help='seconds the assembler gets to assemble')
    parser.add_argument('--port', type=int, default=DEFAULT_PORT)
    parser.add_argument('--save', help='write the bytes the assembler built to this file')
    parser.add_argument('--keep', action='store_true', help='keep the instance (default: delete it)')
    args = parser.parse_args()
    expected = sjasmplus_bytes(args.sjasmplus, args.source, args.address)
    emu = Emulator(port=args.port)
    try:
        built = xas(emu, args, expected) if args.assembler == 'xas' else alasm509(emu, args, expected)   # every ALASM build: the same steps
    finally:
        if not args.keep:
            emu._request('DELETE', emu.base)   # the instance this run created
    if args.save:
        with open(args.save, 'wb') as f:
            f.write(built)
    if built == expected:
        print(f'OK: {len(expected)} bytes from #{args.address:04X} agree with sjasmplus')
        return 0
    first = next(i for i in range(len(expected)) if built[i] != expected[i])
    print(f'DIFFER at #{args.address + first:04X}: assembler {built[first:first + 8].hex()} sjasmplus {expected[first:first + 8].hex()}')
    return 1


if __name__ == '__main__':
    sys.exit(main())
