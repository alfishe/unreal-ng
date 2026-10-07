#!/usr/bin/env python3
"""Assemble a source with the original assembler running in unreal-ng and save the bytes it built: the oracle the
converted source is compared with (sjasmplus output must be byte-equal).

    assemble-in-emulator.py tasm412  <assembler.trd> <source.$A> <address> <length> <out.bin> [--url U] [--id ID]
    assemble-in-emulator.py alasm509 <assembler.trd> <source.$H> <address> <length> <out.bin> --list-position C,R
                            [--url U] [--id ID]
    assemble-in-emulator.py storm13  <assembler.trd> <source.$C> <address> <length> <out.bin> [--extra FILE.$T ...]
                            [--url U] [--id ID]
    assemble-in-emulator.py zasm315  <assembler.trd> <source.$a> <address> <length> <out.bin> [--extra FILE.$T ...]
                            [--url U] [--id ID]
    assemble-in-emulator.py masm11   <assembler.scl> <source.$a> <address> <length> <out.bin> [--extra FILE.$T ...]
                            [--wait S] [--url U] [--id ID]
    assemble-in-emulator.py xas7447  <assembler.trd> <source.$X> <address> <length> <out.bin> [--extra FILE.$T ...]
                            [--list-keys right] [--url U] [--id ID]
    assemble-in-emulator.py xas418   <assembler.trd> <source.$X> <address> <length> <out.bin> [--extra FILE.$T ...]
                            [--list-keys down] [--url U] [--id ID]

The source is added to a copy of the assembler's disk; the memory range is filled with #AA first, so bytes the
assembler did not write stay visible. Pick an address the assembler leaves alone: TASM 4.12 keeps its overlay at
#8000, ALASM 5.09 compiles #8000-#BFFF into its system page (use #6000 / #7000).
STORM 1.3 clears the 48K memory when it starts and swaps its own code into it while it runs: no #AA fill (unwritten
bytes read 0), and the bytes are read after quitting to BASIC. The source is a STORM file (type C, start #C00B; `zxasm
encode --codec storm` writes one from text); --extra adds files it INCBs / INCLs. Assemble errors stop it after the
first pass: the screenshot <out>.assembled.png lists them (line numbers count from 0).
ZAsm 3.15 compiles into its own pages: the source ends with `saveobj "a:out.C",<address>,<length>` and the script
reads that file from the disk afterwards (address and length are only checked against it). ZAsm looks for drive D
first; the script answers its "No Disk!" with drive A.
MASM 1.1 (MASM_11.SCL) lists only its sources (type a) and starts on the first: the source goes first, the files it
INCLUDEs / INCBINs follow with --extra. It compiles to the addresses the source names (#C000 and up into RAM page 0,
which the script reads there); --wait gives a long source its time (its own source: 50 s).
XAS (7.447 `XAS7.447`, 4.18 `XASo`) starts with the disk's XAS sources in two columns: --list-keys are the cursor
keys that reach the source from the first entry (default: right for 7.447, whose list starts with Read Me; down for
4.18, after XMACROS and Xas help). The code goes to its ORG address; the report screenshot lists the errors (XAS
goes on after an error with the value 0). --extra adds the sources LTEXT loads and the files LCODE loads.
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
    parser.add_argument('assembler', choices=['tasm412', 'alasm509', 'storm13', 'zasm315', 'masm11', 'xas7447', 'xas418'])
    parser.add_argument('disk')
    parser.add_argument('source')
    parser.add_argument('address', type=lambda v: int(v, 0))
    parser.add_argument('length', type=lambda v: int(v, 0))
    parser.add_argument('out')
    parser.add_argument('--url')
    parser.add_argument('--id')
    parser.add_argument('--list-position', help='ALASM: column,row of the source in the file list')
    parser.add_argument('--extra', nargs='*', default=[], help='STORM, ZAsm, MASM, XAS: hobeta files the source includes')
    parser.add_argument('--wait', type=float, default=6, help='MASM: seconds the assembly takes')
    parser.add_argument('--list-keys', help='XAS: comma-separated cursor keys from the first file of the list to the source')
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
    elif args.assembler == 'masm11':
        emu.run_trdos('MASM 1.1', wait=8)
        emu.tap('enter')                       # past the title
        emu.tap('w')                           # Work file: the source is the first in the list
        time.sleep(2)
        emu.tap('enter')
        time.sleep(3)
        if args.address < 0xC000:
            emu.write(args.address, [0xAA] * args.length)
        emu.tap('a')                           # Assemble
        time.sleep(args.wait)
        emu.screenshot(stem + '.assembled.png')
        if args.address >= 0xC000:             # RAM page 0 holds what it compiled for #C000-#FFFF
            page = bytes(emu.get(f'/memory/page/ram/0?offset=0&length=16384')['data'])
            data = page[args.address - 0xC000:args.address - 0xC000 + args.length]
        else:
            data = emu.read(args.address, args.length)
        emu.stop_recording()
        open(args.out, 'wb').write(data)
        print(f'{len(data)} bytes from #{args.address:04X} written to {args.out}; check {stem}.assembled.png for errors')
        return 0
    elif args.assembler in ('xas7447', 'xas418'):
        emu.run_trdos('XAS7.447' if args.assembler == 'xas7447' else 'XASo', wait=8)
        emu.screenshot(stem + '.list.png')
        for key in (args.list_keys or ('right' if args.assembler == 'xas7447' else 'down')).split(','):
            emu.tap(key)
        emu.tap('enter')                       # Load
        time.sleep(3)
        emu.write(args.address, [0xAA] * args.length)
        emu.post('/keyboard/combo', {'keys': ['cs', 'ss'], 'frames': 4})   # EXT
        emu.idle()
        time.sleep(0.4)
        emu.tap('a')                           # Assemble
        time.sleep(4)
        emu.screenshot(stem + '.assembled.png')
        data = emu.read(args.address, args.length)
        emu.stop_recording()
        open(args.out, 'wb').write(data)
        print(f'{len(data)} bytes from #{args.address:04X} written to {args.out}; check {stem}.assembled.png for errors')
        return 0
    elif args.assembler == 'zasm315':
        emu.run_trdos('boot', wait=15)
        emu.tap('enter')                       # "No Disk!" (ZAsm starts on drive D): Retry, drive A
        emu.tap('a')
        time.sleep(6)
        emu.tap('enter')                       # File
        time.sleep(2)
        emu.tap('enter')                       # Load
        time.sleep(3)
        emu.type(name)                         # as stored: ZAsm keeps the case of names
        emu.tap('enter')
        time.sleep(3)
        emu.post('/keyboard/combo', {'keys': ['cs', 'ss'], 'frames': 4})   # EXT
        emu.idle()
        time.sleep(0.5)
        emu.tap('a')                           # Assemble
        time.sleep(5)
        emu.tap('enter')                       # "No Disk!" again: Retry, drive A
        emu.tap('a')
        time.sleep(8)
        emu.screenshot(stem + '.assembled.png')
        emu.tap('n')                           # "Launch?" No
        time.sleep(2)
        data = emu.read_disk_file('out', 'C')
        emu.stop_recording()
        if data is None:
            print(f'no out.C on the disk: check {stem}.assembled.png for errors')
            return 1
        start, data = data
        if start != args.address or len(data) != args.length:
            print(f'out.C is {len(data)} bytes at #{start:04X}, not {args.length} at #{args.address:04X}')
        open(args.out, 'wb').write(data)
        print(f'{len(data)} bytes from #{start:04X} written to {args.out}')
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
