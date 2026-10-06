#!/usr/bin/env python3
"""Assemble a source with the original assembler running in unreal-ng and save the bytes it built: the oracle the
converted source is compared with (sjasmplus output must be byte-equal).

    assemble-in-emulator.py tasm412  <assembler.trd> <source.$A> <address> <length> <out.bin> [--url U] [--id ID]
    assemble-in-emulator.py alasm509 <assembler.trd> <source.$H> <address> <length> <out.bin> --list-position C,R
                            [--url U] [--id ID]
    assemble-in-emulator.py storm13  <assembler.trd> <source.$C> <address> <length> <out.bin> [--extra FILE.$T ...]
                            [--url U] [--id ID]

The source is added to a copy of the assembler's disk; the memory range is filled with #AA first, so bytes the
assembler did not write stay visible. Pick an address the assembler leaves alone: TASM 4.12 keeps its overlay at
#8000, ALASM 5.09 compiles #8000-#BFFF into its system page (use #6000 / #7000).
STORM 1.3 clears the 48K memory when it starts and swaps its own code into it while it runs: no #AA fill (unwritten
bytes read 0), and the bytes are read after quitting to BASIC. The source is a STORM file (type C, start #C00B; `zxasm
encode --codec storm` writes one from text); --extra adds files it INCBs / INCLs. Assemble errors stop it after the
first pass: the screenshot <out>.assembled.png lists them (line numbers count from 0).
ALASM's file list is chosen by cursor: --list-position is the column and row of the file in the list `w` shows
(count them on the screenshot <out>.list.png the tool saves first, 1-based).
Screenshots of each step are written next to <out.bin>.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emulator import Emulator  # noqa: E402
import zxdisk  # noqa: E402


def prepare_disk(assembler_trd, source, work, extra=()):
    image = open(assembler_trd, 'rb').read()
    if image[:8] == b'SINCLAIR':
        image = zxdisk.scl2trd(image)
    image = zxdisk.add(image, [open(source, 'rb').read()] + [open(f, 'rb').read() for f in extra])
    path = os.path.join(work, 'oracle.trd')
    open(path, 'wb').write(image)
    return path, open(source, 'rb').read()[:8].decode('latin1').rstrip()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('assembler', choices=['tasm412', 'alasm509', 'storm13'])
    parser.add_argument('disk')
    parser.add_argument('source')
    parser.add_argument('address', type=lambda v: int(v, 0))
    parser.add_argument('length', type=lambda v: int(v, 0))
    parser.add_argument('out')
    parser.add_argument('--url')
    parser.add_argument('--id')
    parser.add_argument('--list-position', help='ALASM: column,row of the source in the file list')
    parser.add_argument('--extra', nargs='*', default=[], help='STORM: hobeta files the source includes')
    args = parser.parse_args()

    work = os.path.dirname(os.path.abspath(args.out))
    stem = os.path.splitext(args.out)[0]
    disk, name = prepare_disk(args.disk, args.source, work, args.extra)
    emu = Emulator(args.url, args.id)
    if not emu.insert_disk(disk):
        print('the disk was not inserted')
        return 1
    if args.assembler == 'tasm412':
        emu.run_trdos('TASM4.12')
        emu.tap('enter')                       # past the title
        emu.write(args.address, [0xAA] * args.length)
        emu.tap('a')                           # Assemble: asks for the work file
        emu.type(name.lower())                 # the keyboard starts in inverted case: lower case arrives as capitals
        emu.tap('enter')
        time.sleep(6)
    elif args.assembler == 'storm13':
        emu.run_trdos('STORM1.3', wait=12)
        emu.tap('break')                       # the external commands
        emu.tap('l')                           # Load source
        emu.type(name.lower())                 # BIG mode: the keys give capitals
        emu.tap('enter')
        time.sleep(3)
        emu.tap('break')
        emu.tap('a')                           # Assemble
        time.sleep(6)
        emu.screenshot(stem + '.assembled.png')
        emu.tap('space')                       # leaves an error list
        time.sleep(1)
        emu.tap('break')
        emu.tap('q')                           # Quit to BASIC: the 48K memory holds the code again
        time.sleep(3)
        data = emu.read(args.address, args.length)
        emu.stop_recording()
        open(args.out, 'wb').write(data)
        print(f'{len(data)} bytes from #{args.address:04X} written to {args.out}; check {stem}.assembled.png for errors')
        return 0
    else:
        if not args.list_position:
            print('alasm509 needs --list-position (see the screenshot)')
        emu.run_trdos('alasm64')
        emu.type('w')
        emu.tap('enter')
        emu.screenshot(stem + '.list.png')
        if not args.list_position:
            return 2
        column, row = (int(v) for v in args.list_position.split(','))
        for _ in range(column - 1):
            emu.tap('right')
        for _ in range(row - 1):
            emu.tap('down')
        emu.tap('enter')
        time.sleep(3)
        emu.write(args.address, [0xAA] * args.length)
        emu.type('a')                          # assemble
        emu.tap('enter')
        time.sleep(3)
    emu.screenshot(stem + '.assembled.png')
    data = emu.read(args.address, args.length)
    emu.stop_recording()
    open(args.out, 'wb').write(data)
    print(f'{len(data)} bytes from #{args.address:04X} written to {args.out}; check {stem}.assembled.png for errors')
    return 0


if __name__ == '__main__':
    sys.exit(main())
