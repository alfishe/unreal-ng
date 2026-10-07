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
    assemble-in-emulator.py zeus1983 <ZEUS.TAP> <source> <address> <length> <out.bin> [--url U] [--id ID]
    assemble-in-emulator.py zeus11   <zeus.$C> <source> <address> <length> <out.bin> [--url U] [--id ID]
    assemble-in-emulator.py zeusgg   <ZEUS_GG.SCL> <source> <address> <length> <out.bin> [--extra FILE.$T ...]
                            [--url U] [--id ID]
    assemble-in-emulator.py zeus7e   <ZEUS72ZK.SCL> <source> <address> <length> <out.bin> [--extra FILE.$T ...]
                            [--url U] [--id ID]
    assemble-in-emulator.py gens4    <DEVPAC_4.TAP> <source> <address> <length> <out.bin> [--at A] [--url U] [--id ID]

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
ZEUS keeps its source in memory: the source (raw ZEUS bytes, or a hobeta file) is written to 32768, made current with
O and assembled with A; the code is read from memory. ZEUS 1983 is the code block of its tape (run at 57344 on a 48K
model); ZEUS 1.1 (PHT_ZEUS.LZH's zeus.$C) runs at 57344 in 48 BASIC on a Pentagon (its INCLUDE / PLACE need the PHT
shell: not available here); ZEUS v7.E and the GG ZEUS ("with B-disk") run from their disks, where --extra adds the
files v7.E INCLUDEs (type Z) and PLACEs (type C) or GG INCBINs (INCBIN "name"). GG's O needs the address (O 32768). The first assemble error stops ZEUS: check the screenshot.
GENS4 is the tape build (DEVPAC_4.TAP of HiSoft Devpac 4): a 48K machine is reset, GENS4 written to --at (default
26000; it runs from any address, pick one clear of the code: 45000 for code below #9C40) and started, the macro
buffer set to 2000 bytes, the source (a hobeta file or a raw GENS file: numbered lines) loaded from a tape image with
G and assembled with A. The text and symbol table follow GENS4 in memory: "Bad ORG!" on the screenshot means the
code would overwrite them.
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


def tape_block(flag, payload):
    body = bytes([flag]) + payload
    checksum = 0
    for b in body:
        checksum ^= b
    return len(body + bytes([checksum])).to_bytes(2, 'little') + body + bytes([checksum])


def tape_blocks(data):
    at = 0
    while at + 2 <= len(data):
        length = data[at] | data[at + 1] << 8
        yield data[at + 2:at + 2 + length]
        at += 2 + length


def gens4(args, stem):
    """GENS4 from tape at --at: load the source with G, assemble with A, read the memory"""
    code = [b for b in tape_blocks(open(args.disk, 'rb').read()) if len(b) == 10882][0][1:-1]   # the 'gens4' block
    text = open(args.source, 'rb').read()
    if args.source[-3:-1] == '.$':
        text = text[17:17 + (text[11] | text[12] << 8)]   # hobeta: the file's bytes
    header = bytes([3]) + b'SOURCE    ' + len(text).to_bytes(2, 'little') + bytes(4)
    tape = os.path.join(os.path.dirname(os.path.abspath(args.out)), 'oracle.tap')
    open(tape, 'wb').write(tape_block(0, header) + tape_block(0xFF, text))
    emu = Emulator(args.url, args.id, model='48K')
    emu.post('/reset')
    time.sleep(3)
    emu.write(args.at, code)
    emu.post('/basic/run', {'command': f'RANDOMIZE USR {args.at}'})
    time.sleep(2)
    emu.type('C')                              # buffers: the include one as it is, 2000 bytes for macros
    emu.tap('enter')
    emu.tap('enter')
    emu.type('2000')
    emu.tap('enter')
    emu.write(args.address, [0xAA] * args.length)
    emu.post('/tape/load', {'path': os.path.abspath(tape)})
    emu.type('G,,')                            # the first text file on the tape
    emu.tap('enter')
    emu.post('/tape/play')
    for _ in range(240):
        time.sleep(0.5)
        if emu.get('/tape/info').get('state') in ('ended', 'stopped'):
            break
    time.sleep(1)
    emu.type('A')                              # assemble: default options (no listing), code where ORG says
    emu.tap('enter')
    time.sleep(3 + len(text) / 400)
    emu.screenshot(stem + '.assembled.png')
    data = emu.read(args.address, args.length)
    open(args.out, 'wb').write(data)
    print(f'{len(data)} bytes from #{args.address:04X} written to {args.out}; check {stem}.assembled.png for errors')
    return 0


def prepare_disk(assembler_trd, source, work, extra=()):
    image = open(assembler_trd, 'rb').read()
    if image[:8] == b'SINCLAIR':
        image = zxdisk.scl2trd(image)
    image = zxdisk.add(image, [open(source, 'rb').read()] + [open(f, 'rb').read() for f in extra])
    path = os.path.join(work, 'oracle.trd')
    open(path, 'wb').write(image)
    return path, open(source, 'rb').read()[:8].decode('latin1').rstrip()


def tap_code(path):
    """The data of the first CODE block of a TAP file"""
    data, at, code = open(path, 'rb').read(), 0, False
    while at + 2 <= len(data):
        length = data[at] | data[at + 1] << 8
        block = data[at + 2:at + 2 + length]
        at += 2 + length
        if block and block[0] == 0 and block[1] == 3:
            code = True
        elif block and block[0] == 0xFF and code:
            return block[1:-1]
    return None


def zeus(args, stem, work):
    """ZEUS assembles the source it keeps in memory at 32768"""
    source = open(args.source, 'rb').read()
    if args.source.lower().split('.')[-1].startswith('$'):
        source = source[17:17 + (source[11] | source[12] << 8)]   # a hobeta file: its data
    emu = Emulator(args.url, args.id, model='48K' if args.assembler == 'zeus1983' else 'PENTAGON')
    if args.assembler in ('zeus7e', 'zeusgg'):
        image = open(args.disk, 'rb').read()
        if image[:8] == b'SINCLAIR':
            image = zxdisk.scl2trd(image)
        if args.extra:
            image = zxdisk.add(image, [open(f, 'rb').read() for f in args.extra])
        disk = os.path.join(work, 'oracle.trd')
        open(disk, 'wb').write(image)
        emu.insert_disk(disk)
        emu.run_trdos('ZEUSv7.E' if args.assembler == 'zeus7e' else 'ZEUS', wait=10)
    else:
        emu.post('/reset')
        time.sleep(3)
        if args.assembler == 'zeus11':
            for _ in range(3):                 # 48 BASIC
                emu.tap('down')
            emu.tap('enter')
            time.sleep(2)
            code = open(args.disk, 'rb').read()[17:]
        else:
            code = tap_code(args.disk)
        for k in range(0, len(code), 1024):
            emu.write(57344 + k, code[k:k + 1024])
        emu.post('/basic/run', {'command': 'RANDOMIZE USR 57344'})
        time.sleep(5)
    emu.post('/keyboard/combo', {'keys': ['cs', '2'], 'frames': 6})   # CAPS LOCK: ZEUS takes capitals
    emu.idle()
    time.sleep(0.5)
    for k in range(0, len(source), 1024):
        emu.write(32768 + k, source[k:k + 1024])
    emu.type('o 32768')                        # the source at 32768 becomes the current one
    emu.tap('enter')
    time.sleep(1)
    for k in range(0, args.length, 1024):
        emu.write(args.address + k, [0xAA] * min(1024, args.length - k))
    emu.type('a')                              # assemble
    emu.tap('enter')
    time.sleep(8)
    emu.screenshot(stem + '.assembled.png')
    data = emu.read(args.address, args.length)
    emu.stop_recording()
    open(args.out, 'wb').write(data)
    print(f'{len(data)} bytes from #{args.address:04X} written to {args.out}; check {stem}.assembled.png for errors')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('assembler', choices=['tasm412', 'alasm509', 'storm13', 'zasm315', 'masm11', 'xas7447', 'xas418', 'zeus1983',
                                              'zeus11', 'zeusgg', 'zeus7e', 'gens4'])
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
    parser.add_argument('--at', type=lambda v: int(v, 0), default=26000, help='GENS4: where it is put and started')
    args = parser.parse_args()

    work = os.path.dirname(os.path.abspath(args.out))
    stem = os.path.splitext(args.out)[0]
    if args.assembler.startswith('zeus'):
        return zeus(args, stem, work)
    if args.assembler == 'gens4':
        return gens4(args, stem)
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
