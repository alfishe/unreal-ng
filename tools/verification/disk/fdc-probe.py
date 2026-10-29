#!/usr/bin/env python3
"""Watch a disk program frame by frame: the CPU registers and the floppy controller (command, phase, main status,
result bytes, track, ST0-ST2) per sample, then the stack and a disassembly of the return addresses on it. Made for a
load that hangs: a command repeated forever with the same result (a protection check waiting for an ID it never sees),
a controller stuck in its execution phase, a loop in the DOS ROM.

    python3 tools/verification/disk/fdc-probe.py --launch --frames 2000 --samples 30 game.dsk
    python3 tools/verification/disk/fdc-probe.py --launch game.dsk --disasm 0x5e80 --stack-depth 12

Uses GET /state/fdc (uPD765 on the +3, WD1793 elsewhere: the fields the controller reports are printed as they are).
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import disklib  # noqa: E402


def fdc_line(fdc):
    if not fdc:
        return 'fdc -'
    command = fdc.get('command', {})
    status = fdc.get('status', {})
    drive = (fdc.get('drives') or [{}])[0]
    result = fdc.get('result', {}).get('bytes', [])
    return (f"{command.get('name', '?')} {command.get('bytes', [])} {fdc.get('phase', '?')} "
            f"msr {fdc.get('main_status', {}).get('value', '?'):>3} result {result} track {drive.get('track', '?')} "
            f"st {status.get('st0', '?')}/{status.get('st1', '?')}/{status.get('st2', '?')}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('image')
    disklib.add_instance_args(parser)
    parser.add_argument('--model', default='auto', help='machine model, or auto (DSK: PLUS3, else PENTAGON)')
    parser.add_argument('--settle', type=int, default=150, help='frames before the key (default 150)')
    parser.add_argument('--key', default='enter', help="key tapped after --settle frames, or 'none' (default enter)")
    parser.add_argument('--frames', type=int, default=2000, help='frames to run before sampling (default 2000)')
    parser.add_argument('--samples', type=int, default=20, help='samples to print (default 20)')
    parser.add_argument('--step', type=int, default=1, help='frames between samples (default 1)')
    parser.add_argument('--stack-depth', type=int, default=8, help='stack words to show and disassemble at (default 8)')
    parser.add_argument('--disasm', action='append', default=[], metavar='ADDR',
                        help='also disassemble here (hex or decimal; repeatable)')
    parser.add_argument('--count', type=int, default=16, help='instructions per disassembly (default 16)')
    parser.add_argument('--json', action='store_true', help='print the full /state/fdc of the last sample')
    args = parser.parse_args()

    api, process = disklib.open_instance(args)
    try:
        machine = disklib.Machine(api, disklib.model_for(args.image, args.model))
        try:
            ok, message = machine.insert(args.image)
            print(f'insert: {message}')
            if not ok:
                return 1
            machine.resume()
            machine.run_frames(args.settle)
            if args.key != 'none':
                machine.tap(args.key)
            machine.run_frames(args.frames)
            machine.pause()

            fdc = {}
            for sample in range(args.samples):
                regs = machine.registers()
                main, special = regs.get('main', {}), regs.get('special', {})
                fdc = machine.fdc()
                print(f"{sample:3} pc {special.get('pc', 0):04X} sp {special.get('sp', 0):04X} "
                      f"af {main.get('af', 0):04X} bc {main.get('bc', 0):04X} de {main.get('de', 0):04X} "
                      f"hl {main.get('hl', 0):04X}  {fdc_line(fdc)}")
                if sample + 1 < args.samples:
                    machine.run_frames(args.step)
            if args.json:
                print(json.dumps(fdc, indent=1))

            # Return addresses: the words on the stack, each disassembled a few bytes before (the CALL) and on
            sp = machine.registers().get('special', {}).get('sp', 0)
            dump = machine.memory(sp, args.stack_depth * 2)
            print(dump.get('hexdump', dump) if isinstance(dump, dict) else dump)
            words = []
            for line in (dump.get('hexdump', '') if isinstance(dump, dict) else '').splitlines():
                words += [int(b, 16) for b in line.split(':', 1)[1].split('|')[0].split()]
            stack = [words[i] | (words[i + 1] << 8) for i in range(0, len(words) - 1, 2)]
            print('stack:', ' '.join(f'{w:04X}' for w in stack))

            targets = [(int(a, 0), 'requested') for a in args.disasm] + [(max(w - 3, 0), 'returns to') for w in stack]
            for address, why in targets:
                print(f'\n{why} {address:04X}:')
                for ins in machine.disasm(address, args.count):
                    print(f"  {ins.get('address', 0):04X}  {ins.get('bytes', ''):<10} {ins.get('mnemonic', '')}")
        finally:
            machine.close()
    finally:
        disklib.stop(process)
    return 0


if __name__ == '__main__':
    sys.exit(main())
