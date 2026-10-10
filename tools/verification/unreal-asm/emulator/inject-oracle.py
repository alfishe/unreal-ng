#!/usr/bin/env python3
"""The part B oracle (asm-synchronizer.md §18): a host source written into a running assembler through the WebAPI
(POST /asm/sync/inject), assembled there by the assembler itself, and its bytes compared with what sjasmplus builds
from the same source. The assembler disk is the collection's; the tool creates its own instance and deletes it.

    inject-oracle.py alasm509 <ALASM509.trd> <source.asm> <address> [--sjasmplus PATH] [--port P] [--keep]

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
    emu.insert_disk(os.path.abspath(args.disk))
    emu.run_trdos('alasm64')
    time.sleep(2)                                  # the memory test, then the A> prompt
    with open(args.source, encoding='utf-8') as f:
        text = f.read()
    body = {'text': text, 'from': args.from_, 'assembler': 'alasm-5.09', 'title': 'INJECTED'}
    reply = emu.post('/asm/sync/inject', body)
    print('inject:', {k: reply.get(k) for k in ('assembler', 'name', 'page', 'bytes', 'lines', 'previous')})
    for d in reply.get('diagnostics', []):
        if d.get('severity') != 'info':
            print('  ', d)
    emu.type('a')                                  # ASSEMBLE
    emu.tap('enter')
    time.sleep(args.wait)
    emu.screenshot(os.path.abspath(args.source) + '.alasm509.png')
    return bytes(emu.read(args.address, len(expected)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('assembler', choices=['alasm509'])
    parser.add_argument('disk')
    parser.add_argument('source')
    parser.add_argument('address', type=lambda v: int(v, 0))
    parser.add_argument('--from', dest='from_', default='sjasmplus', help='the source\'s dialect (default sjasmplus)')
    parser.add_argument('--sjasmplus', default=os.environ.get('UNREAL_ASM_SJASMPLUS', 'sjasmplus'))
    parser.add_argument('--wait', type=float, default=3, help='seconds the assembler gets to assemble')
    parser.add_argument('--port', type=int, default=DEFAULT_PORT)
    parser.add_argument('--keep', action='store_true', help='keep the instance (default: delete it)')
    args = parser.parse_args()
    expected = sjasmplus_bytes(args.sjasmplus, args.source, args.address)
    emu = Emulator(port=args.port)
    try:
        built = alasm509(emu, args, expected)
    finally:
        if not args.keep:
            emu._request('DELETE', emu.base)   # the instance this run created
    if built == expected:
        print(f'OK: {len(expected)} bytes from #{args.address:04X} agree with sjasmplus')
        return 0
    first = next(i for i in range(len(expected)) if built[i] != expected[i])
    print(f'DIFFER at #{args.address + first:04X}: assembler {built[first:first + 8].hex()} sjasmplus {expected[first:first + 8].hex()}')
    return 1


if __name__ == '__main__':
    sys.exit(main())
