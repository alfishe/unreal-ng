#!/usr/bin/env python3
"""Idle templates for part B (asm-synchronizer.md §16.3, Z1): an assembler started from its disk in a fresh instance
and saved as a snapshot while it waits at its command prompt with no text, so `zxasm inject` (or sync-inject with
`snapshot`) can put a host source into it without an emulator.

    make-template.py alasm509 <ALASM509.trd> <out.szx> [--also sna,z80] [--port P] [--keep]

The disk is ejected before the save, so the snapshot names no host file. The snapshot is checked before the tool
ends: the probe finds the assembler idle. --also saves the same moment in the
other formats next to it (out.sna, out.z80). The assembler disk is the collection's; the tool creates its own
instance and deletes it.
"""
import argparse
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib'))
from emulator import DEFAULT_PORT, Emulator  # noqa: E402

ASSEMBLERS = {
    # name: (loader, seconds to the prompt, descriptor)
    'alasm509': ('alasm64', 2, 'alasm-5.09'),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('assembler', choices=sorted(ASSEMBLERS))
    parser.add_argument('disk')
    parser.add_argument('output', help='the template snapshot (.szx, .sna or .z80)')
    parser.add_argument('--also', default='', help='other formats of the same moment, comma-separated (sna,z80,szx)')
    parser.add_argument('--zxasm', default=os.environ.get('ZXASM', 'zxasm'), help='zxasm to check the template with')
    parser.add_argument('--port', type=int, default=DEFAULT_PORT)
    parser.add_argument('--keep', action='store_true', help='keep the instance (default: delete it)')
    args = parser.parse_args()
    loader, wait, descriptor = ASSEMBLERS[args.assembler]
    outputs = [os.path.abspath(args.output)]
    stem = os.path.splitext(outputs[0])[0]
    outputs += [f'{stem}.{f}' for f in args.also.split(',') if f]
    emu = Emulator(port=args.port)
    try:
        emu.insert_disk(os.path.abspath(args.disk))
        emu.run_trdos(loader)
        time.sleep(wait)
        emu.stop_recording()
        ejected = emu.post('/media/A/eject', {'discard': True})   # the snapshot names no host disk (ALASM is in memory)
        if not ejected.get('ok'):
            raise SystemExit(f'eject: {ejected}')
        for path in outputs:
            reply = emu.post('/snapshot/save', {'path': path, 'force': True})
            print('saved:', path, reply.get('format') or reply.get('message') or reply)
        emu.post('/resume')
    finally:
        if not args.keep:
            emu._request('DELETE', emu.base)   # the instance this run created
    failed = 0
    for path in outputs:
        probe = subprocess.run([args.zxasm, 'probe', path], capture_output=True, text=True)
        idle = any(line.startswith(descriptor + '\t') and line.endswith('\tidle') for line in probe.stdout.splitlines())
        print(probe.stdout.strip())
        if not idle:
            print(f'NOT IDLE: {path}: {probe.stderr.strip()}')
            failed += 1
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
