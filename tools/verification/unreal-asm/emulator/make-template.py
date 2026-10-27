#!/usr/bin/env python3
"""Idle templates for part B (asm-synchronizer.md §16.3, Z1): an assembler started in a fresh instance and saved as
a snapshot while it waits at its prompt, so `zxasm inject` (or sync-inject with `snapshot`) can put a host source
into it without an emulator. Every profile of inject-oracle.py (../lib/assemblers.py): the same start.

    make-template.py alasm509 <ALASM509.trd> <out.szx> [--also sna,z80] [--port P] [--keep]
    make-template.py tasm412 <TASM_412.TRD> <out.szx>
    make-template.py xas <XAS7_447.trd> <out.szx> --boot XAS7.447 --assembler xas-7.447
    make-template.py gens4 <DEVPAC_4.TAP> <out.szx>          # 48K: --also z80 (an .sna of a 48K machine works too)

The disk is ejected before the save, so the snapshot names no host file (an assembler that saves its code to its
disk gets it back in inject-oracle.py --template --media). The snapshot is checked before the tool ends: the probe
finds the assembler idle. --also saves the same moment in the other formats next to it (out.sna, out.z80). The tool
creates its own instance and deletes it.
"""
import argparse
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib'))
from assemblers import PROFILES  # noqa: E402
from emulator import DEFAULT_PORT, Emulator  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('assembler', choices=sorted(PROFILES))
    parser.add_argument('disk', help='the assembler disk or tape')
    parser.add_argument('output', help='the template snapshot (.szx, .sna or .z80)')
    parser.add_argument('--also', default='', help='other formats of the same moment, comma-separated (sna,z80,szx)')
    parser.add_argument('--boot', help='the BASIC program that starts it (as inject-oracle.py)')
    parser.add_argument('--assembler', dest='descriptor', help='the descriptor the probe must find idle (default the profile\'s)')
    parser.add_argument('--combo-frames', type=int, default=4, help='xas: frames EXT is held (9.x: 12)')
    parser.add_argument('--right', type=int, default=0, help='xas: cursor moves right in the file list before loading')
    parser.add_argument('--help-key', action='store_true', help='alasm: a key closes a help screen at the start (3.8c)')
    parser.add_argument('--install', type=int, help='prometheus: the address its installer moves the code to (as inject-oracle.py)')
    parser.add_argument('--zxasm', default=os.environ.get('ZXASM', 'zxasm'), help='zxasm to check the template with')
    parser.add_argument('--port', type=int, default=DEFAULT_PORT)
    parser.add_argument('--settle', type=int, default=60, help='seconds it may take to reach its prompt after the start')
    parser.add_argument('--keep', action='store_true', help='keep the instance (default: delete it)')
    args = parser.parse_args()
    profile = PROFILES[args.assembler]
    descriptor = args.descriptor = args.descriptor or profile.descriptor   # the boot of some profiles reads it
    outputs = [os.path.abspath(args.output)]
    stem = os.path.splitext(outputs[0])[0]
    outputs += [f'{stem}.{f}' for f in args.also.split(',') if f]
    emu = Emulator(port=args.port, model=profile.model, ram_size=profile.ram_size)
    try:
        profile.boot(emu, args)
        emu.stop_recording()
        # until it waits at its prompt (Primus loads its manual after it starts): a snapshot of the moment probed
        # (paused for the probe and kept paused through the eject and the save: the template is the moment probed)
        probe_path = stem + '.settle.szx'
        for second in range(args.settle):
            emu.post('/pause')
            emu.post('/snapshot/save', {'path': probe_path, 'force': True})
            probe = subprocess.run([args.zxasm, 'probe', probe_path], capture_output=True, text=True)
            if any(line.startswith(descriptor + '\t') and line.endswith('\tidle') for line in probe.stdout.splitlines()):
                break
            emu.post('/resume')
            time.sleep(0.5 + (second % 3) * 0.13)   # another moment of its loop next time
        if os.path.exists(probe_path):
            os.remove(probe_path)
        if not profile.tape:
            ejected = emu.post('/media/A/eject', {'discard': True})   # the snapshot names no host disk (the assembler is in memory)
            if not ejected.get('ok'):
                raise SystemExit(f'eject: {ejected}')
            for drive in 'BCD':                    # a disk some put in another drive too (ZAsm 4.0x8: D)
                if emu.get('/media').get('slots', [{}] * 4)[' ABCD'.index(drive) - 1].get('state') == 'present':
                    emu.post(f'/media/{drive}/eject', {'discard': True})
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
