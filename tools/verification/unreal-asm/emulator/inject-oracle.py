#!/usr/bin/env python3
"""The part B oracle (asm-synchronizer.md §18): a host source written into a running assembler through the WebAPI
(POST /asm/sync/inject), assembled there by the assembler itself, and its bytes compared with what sjasmplus builds
from the same source. The assembler disk is the collection's; the tool creates its own instance and deletes it.

    inject-oracle.py alasm509 <ALASM509.trd> <source.asm> <address> [--sjasmplus PATH] [--port P] [--keep]
    inject-oracle.py alasm <disk> <source.asm> <address> --boot NAME --assembler alasm-4.44 [--help-key]   # any build
    inject-oracle.py alasm509 --template <alasm509.szx|.sna|.z80> <source.asm> <address> [--zxasm PATH] ...
    inject-oracle.py alasm <disk> <main.asm> <address> --boot NAME --assembler alasm-X --project   # with its INCLUDEs
    inject-oracle.py xas <XAS disk> <source.asm> <address> --boot XAS7.447 --assembler xas-7.447 [--combo-frames 12]
    inject-oracle.py storm <STORM disk> <source.asm> <address> [--boot STORM1.3] [--assembler storm-1.3]
    inject-oracle.py tasm412 <TASM_412 disk> <source.asm> <address> [--wait 15]
    inject-oracle.py zasm315 <ZASM315 disk with STUB.a> <source.asm> <address>
    inject-oracle.py masm11 <MASM_11 disk> <source.asm> <address> [--wait 20]
    inject-oracle.py gens4 <DEVPAC_4.TAP> <source.asm> <address> [--wait 240]
    inject-oracle.py zeus1983 <ZEUS.TAP> <source.asm> <address> [--wait 60]
    inject-oracle.py primus <PRIMUS29.SCL> <source.asm> <address> [--wait 30]
    inject-oracle.py lasergenius <LG_1_04.SCL> <source.asm> <address> [--wait 20]
    inject-oracle.py prometheus <prometheus-48.tap> <source.asm> <address> [--wait 10]
    inject-oracle.py pasm <PASM3_0.SCL> <source.asm> <address> [--wait 5]

XAS (Z3): started from its disk, the first source of its list loaded (or a new text), its command line opened (EXT;
9.x need the keys held 12 frames), the source written there and assembled with A.

STORM (Z3): started from its disk, the source written while its editor waits for a key (STORM's PGUP then shows it
from the top: <source>.storm-1.3.editor.png), assembled with BREAK A, the bytes read after quitting to BASIC (STORM
swaps its code into the 48K memory while it runs and clears it when it starts: unwritten bytes read 0).

TASM 4.12 (Z3): started from its disk, the source written at its command line (TASM128>) as its LOAD leaves a file,
the work name set to INJECTED, assembled with A. TASM's stack is at #617C: give sources from #6200 up (its overlay
is at #8000).

ZAsm 3.15 (Z3): started from its disk (its "No Disk!" answered with drive A), a source STUB loaded from the disk
(File / Load: its editor opens on it), the source written at its COMMAND prompt (CS+SS) with a SAVEOBJ of the range
added, by force (ZAsm keeps no "changed" flag: a text it holds is not replaced otherwise), assembled with A; the bytes
are the file it saved.

MASM 1.1 (Z3): started from its disk, the source written at its MASM128> prompt as its LOAD leaves a file, assembled
with A.

GENS4 (Z4): its tape block written at 26000 of a 48K machine and started there, the source written at its > prompt
as G leaves a text, assembled with A,8000 (a symbol table of 8000 bytes). Give sources below 26000 or above its text.

ZEUS 1983 (Z4): its code written at 57344 of a 48K machine and started there, the source written at its prompt as
"o" leaves a text, assembled with A.

Primus 2.9 (Z4): started from its disk (it loads its manual PRI.DOC), the source written at its prompt by force (no
"changed" flag), assembled with "a out" (the code saved as out), the bytes the file it saved.

Laser Genius 1.04 (Z4): started from its disk (the loader's questions: tool-kit y, hash extensions n, disk d), the
source written in its editor by force (no "changed" flag), assembled with "assem". It stores code only after a PUT
and between #68D2 and #7F22: give sources from #6900.

PROMETHEUS 48K (Z4): loaded from its tape on a 48K machine (installed at 24000), the source written in its editor
by force (no "changed" flag), assembled with SS+A. Its source and names lie from #9C28 up: give sources from 60000.

Power Assembler 3.0 (Z4): started from its disk on a Pentagon 128, the source written in its editor by force (no
"changed" flag) as ZAP and GET leave it, compiled with the menu's L (SYMBOL SHIFT, then CAPS SHIFT, then L). PASM
puts the code from #8000 up in an image of the 48K memory (#8000-#BFFF in RAM page 3, #C000-#FFFF in page 0), code
below #8000 from #2000 of page 4 whatever the ORG (give the ORG as the address there): the bytes are read there. It
takes one ORG (a later one forward is written as zeros). 674 lines take it about 30 s: --wait 45.

With --project the source goes as a path, and each file it INCLUDEs into an ALASM text page of its own (the INCLUDE
names the page); --template always takes the INCLUDEs (zxasm inject reads them beside the source).

With --template (Z1) the source goes into the template snapshot with `zxasm inject` instead, without an emulator; the
snapshot written is then loaded into a fresh instance and assembled there (make-template.py makes templates).

The source is sjasmplus (any dialect with a frontend works with --from). ALASM 5.09 assembles #8000-#BFFF into its
system page: give sources that ORG below #8000. Exit 0 when the bytes agree; a screenshot of the assembler's report
is saved next to the source as <source>.<assembler>.png.
"""
import argparse
import importlib.util
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


def storm(emu, args, expected):
    emu.insert_disk(os.path.abspath(args.disk))
    emu.run_trdos(args.boot or 'STORM1.3', wait=12)
    body = {'from': args.from_, 'assembler': args.descriptor, 'title': 'INJECTED'}
    with open(args.source, encoding='utf-8') as f:
        body['text'] = f.read()
    reply = emu.post('/asm/sync/inject', body)
    print('inject:', {k: reply.get(k) for k in ('assembler', 'name', 'page', 'bytes', 'lines', 'previous', 'message')})
    for d in reply.get('diagnostics', []):
        if d.get('severity') != 'info':
            print('  ', d)
    time.sleep(1)                                      # PGUP redraws the editor from the text's start
    stem = os.path.abspath(args.source) + '.' + args.descriptor
    emu.screenshot(stem + '.editor.png')
    emu.tap('break')
    emu.tap('a')                                       # Assemble
    time.sleep(args.wait)
    emu.screenshot(stem + '.png')
    emu.tap('space')                                   # leaves an error list
    time.sleep(1)
    emu.tap('break')
    emu.tap('q')                                       # Quit to BASIC: the 48K memory holds the code again
    time.sleep(3)
    return bytes(emu.read(args.address, len(expected)))


def tasm412(emu, args, expected):
    emu.insert_disk(os.path.abspath(args.disk))
    emu.run_trdos(args.boot or 'TASM4.12')
    emu.tap('enter')                                   # past the title
    time.sleep(1)
    body = {'from': args.from_, 'assembler': 'tasm-4.12', 'title': 'INJECTED'}
    with open(args.source, encoding='utf-8') as f:
        body['text'] = f.read()
    reply = emu.post('/asm/sync/inject', body)
    print('inject:', {k: reply.get(k) for k in ('assembler', 'name', 'bytes', 'lines', 'previous', 'message')})
    for d in reply.get('diagnostics', []):
        if d.get('severity') != 'info':
            print('  ', d)
    emu.write(args.address, [0xAA] * len(expected))   # bytes TASM does not write stay visible
    emu.tap('a')                                       # Assemble: the text in memory, under the work name
    time.sleep(args.wait)
    emu.screenshot(os.path.abspath(args.source) + '.tasm-4.12.png')
    return bytes(emu.read(args.address, len(expected)))


def zasm315(emu, args, expected):
    emu.insert_disk(os.path.abspath(args.disk))
    emu.run_trdos(args.boot or 'boot', wait=15)
    emu.tap('enter')                                   # "No Disk!" (ZAsm starts on drive D): Retry, drive A
    emu.tap('a')
    time.sleep(6)
    emu.tap('enter')                                   # File
    time.sleep(2)
    emu.tap('enter')                                   # Load
    time.sleep(3)
    emu.type('STUB')
    emu.tap('enter')                                   # the editor opens on it
    time.sleep(3)
    emu.post('/keyboard/combo', {'keys': ['cs', 'ss'], 'frames': 4})   # COMMAND
    emu.idle()
    time.sleep(0.6)
    with open(args.source, encoding='utf-8') as f:
        text = f.read()
    text += f'\n        SAVEBIN "out.C",#{args.address:04X},{len(expected)}\n'   # ZAsm compiles into its own pages
    reply = emu.post('/asm/sync/inject', {'from': args.from_, 'assembler': 'zasm-3.15', 'title': 'INJECTED', 'text': text, 'force': True})
    print('inject:', {k: reply.get(k) for k in ('assembler', 'bytes', 'lines', 'previous', 'message')})
    for d in reply.get('diagnostics', []):
        if d.get('severity') != 'info':
            print('  ', d)
    emu.tap('a')                                       # Assemble
    time.sleep(5)
    emu.tap('enter')                                   # "No Disk!" again: Retry, drive A
    emu.tap('a')
    time.sleep(args.wait)
    emu.screenshot(os.path.abspath(args.source) + '.zasm-3.15.png')
    emu.tap('n')                                       # "Launch?" No
    time.sleep(2)
    data = emu.read_disk_file('out', 'C')
    return bytes(data[1]) if data else b''


def masm11(emu, args, expected):
    emu.insert_disk(os.path.abspath(args.disk))
    emu.run_trdos(args.boot or 'MASM 1.1', wait=8)
    emu.tap('enter')                                   # past the title
    time.sleep(1)
    with open(args.source, encoding='utf-8') as f:
        body = {'from': args.from_, 'assembler': 'masm-1.1', 'title': 'INJECTED', 'text': f.read()}
    reply = emu.post('/asm/sync/inject', body)
    print('inject:', {k: reply.get(k) for k in ('assembler', 'bytes', 'lines', 'previous', 'message')})
    for d in reply.get('diagnostics', []):
        if d.get('severity') != 'info':
            print('  ', d)
    emu.write(args.address, [0xAA] * len(expected))
    emu.tap('a')                                       # Assemble
    time.sleep(args.wait)
    emu.screenshot(os.path.abspath(args.source) + '.masm-1.1.png')
    return bytes(emu.read(args.address, len(expected)))


def gens4(emu, args, expected):
    spec = importlib.util.spec_from_file_location('aie', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'assemble-in-emulator.py'))
    aie = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(aie)
    code = [b for b in aie.tape_blocks(open(args.disk, 'rb').read()) if len(b) == 10882][0][1:-1]   # the 'gens4' block
    emu.post('/reset')
    time.sleep(3)
    emu.write(26000, code)
    emu.post('/basic/run', {'command': 'RANDOMIZE USR 26000'})
    time.sleep(2)
    with open(args.source, encoding='utf-8') as f:
        body = {'from': args.from_, 'assembler': 'gens-4', 'title': 'INJECTED', 'text': f.read()}
    reply = emu.post('/asm/sync/inject', body)
    print('inject:', {k: reply.get(k) for k in ('assembler', 'bytes', 'lines', 'previous', 'message')})
    for d in reply.get('diagnostics', []):
        if d.get('severity') != 'info':
            print('  ', d)
    emu.write(args.address, [0xAA] * len(expected))
    emu.type('A,8000')                                 # Assemble with a symbol table of 8000 bytes
    emu.tap('enter')
    time.sleep(args.wait)
    emu.screenshot(os.path.abspath(args.source) + '.gens-4.png')
    return bytes(emu.read(args.address, len(expected)))


def zeus1983(emu, args, expected):
    spec = importlib.util.spec_from_file_location('aie', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'assemble-in-emulator.py'))
    aie = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(aie)
    code = aie.tap_code(args.disk)
    emu.post('/reset')
    time.sleep(3)
    for k in range(0, len(code), 1024):
        emu.write(57344 + k, code[k:k + 1024])
    emu.post('/basic/run', {'command': 'RANDOMIZE USR 57344'})
    time.sleep(5)
    emu.post('/keyboard/combo', {'keys': ['cs', '2'], 'frames': 6})   # CAPS LOCK: ZEUS takes capitals
    emu.idle()
    time.sleep(0.5)
    with open(args.source, encoding='utf-8') as f:
        body = {'from': args.from_, 'assembler': 'zeus-1983', 'title': 'INJECTED', 'text': f.read()}
    reply = emu.post('/asm/sync/inject', body)
    print('inject:', {k: reply.get(k) for k in ('assembler', 'bytes', 'lines', 'previous', 'message')})
    for d in reply.get('diagnostics', []):
        if d.get('severity') != 'info':
            print('  ', d)
    emu.write(args.address, [0xAA] * len(expected))
    emu.type('a')                                      # Assemble
    emu.tap('enter')
    time.sleep(args.wait)
    emu.screenshot(os.path.abspath(args.source) + '.zeus-1983.png')
    return bytes(emu.read(args.address, len(expected)))


def primus(emu, args, expected):
    emu.insert_disk(os.path.abspath(args.disk))
    emu.run_trdos(args.boot or 'P_AS_2.9', wait=12)
    with open(args.source, encoding='utf-8') as f:
        body = {'from': args.from_, 'assembler': 'primus-2.9', 'title': 'INJECTED', 'text': f.read(), 'force': True}
    reply = emu.post('/asm/sync/inject', body)
    print('inject:', {k: reply.get(k) for k in ('assembler', 'bytes', 'lines', 'previous', 'message')})
    for d in reply.get('diagnostics', []):
        if d.get('severity') != 'info':
            print('  ', d)
    emu.type('a out')                                  # Assemble, the code saved as "out" (caps lock: capitals)
    emu.tap('enter')
    time.sleep(args.wait)
    emu.screenshot(os.path.abspath(args.source) + '.primus-2.9.png')
    for name in ('OUT', 'out'):
        for kind in ('C', 'c'):
            data = emu.read_disk_file(name, kind)
            if data:
                return bytes(data[1])
    return b''


def lasergenius(emu, args, expected):
    emu.insert_disk(os.path.abspath(args.disk))
    emu.run_trdos(args.boot or 'L_G_ASS.', wait=6)
    emu.tap('1')                                       # the loader's menu is a BASIC INPUT
    emu.tap('enter')
    time.sleep(4)
    for key in ('y', 'n', 'd'):                        # tool-kit, hash extensions, from disk
        emu.tap(key)
        emu.tap('enter')
        time.sleep(3)
    time.sleep(6)
    with open(args.source, encoding='utf-8') as f:
        body = {'from': args.from_, 'assembler': 'lasergenius-1.04', 'title': 'INJECTED', 'text': f.read(), 'force': True}
    reply = emu.post('/asm/sync/inject', body)
    print('inject:', {k: reply.get(k) for k in ('assembler', 'bytes', 'lines', 'previous', 'message')})
    for d in reply.get('diagnostics', []):
        if d.get('severity') != 'info':
            print('  ', d)
    emu.write(args.address, [0xAA] * len(expected))
    emu.type('assem')
    emu.tap('enter')
    time.sleep(args.wait)
    emu.screenshot(os.path.abspath(args.source) + '.lasergenius-1.04.png')
    return bytes(emu.read(args.address, len(expected)))


def prometheus(emu, args, expected):
    emu.post('/tape/load', {'path': os.path.abspath(args.disk)})
    emu.post('/basic/run', {'command': 'LOAD ""'})
    emu.post('/tape/play')
    for _ in range(240):
        time.sleep(0.5)
        if emu.get('/tape/info').get('state') in ('ended', 'stopped'):
            break
    time.sleep(2)
    emu.tap('enter')                                   # the installer: run the assembler
    time.sleep(3)
    emu.post('/tape/eject')
    with open(args.source, encoding='utf-8') as f:
        body = {'from': args.from_, 'assembler': 'prometheus-48', 'title': 'INJECTED', 'text': f.read(), 'force': True}
    reply = emu.post('/asm/sync/inject', body)
    print('inject:', {k: reply.get(k) for k in ('assembler', 'bytes', 'lines', 'previous', 'message')})
    for d in reply.get('diagnostics', []):
        if d.get('severity') != 'info':
            print('  ', d)
    emu.write(args.address, [0xAA] * len(expected))
    emu.post('/keyboard/combo', {'keys': ['ss', 'a'], 'frames': 4})
    time.sleep(0.5)
    emu.tap('enter')
    time.sleep(args.wait)
    emu.screenshot(os.path.abspath(args.source) + '.prometheus-48.png')
    return bytes(emu.read(args.address, len(expected)))


def pasm(emu, args, expected):
    emu.insert_disk(os.path.abspath(args.disk))
    emu.run_trdos(args.boot or 'PASM3.0', wait=8)
    with open(args.source, encoding='utf-8') as f:
        body = {'from': args.from_, 'assembler': 'pasm-3.0', 'title': 'INJECTED', 'text': f.read(), 'force': True}
    reply = emu.post('/asm/sync/inject', body)
    print('inject:', {k: reply.get(k) for k in ('assembler', 'bytes', 'lines', 'previous', 'message')})
    for d in reply.get('diagnostics', []):
        if d.get('severity') != 'info':
            print('  ', d)
    page, offset = pasm_object_place(args.address)
    emu.post(f'/memory/page/ram/{page}', {'offset': offset, 'data': [0xAA] * len(expected)})
    # the menu: SYMBOL SHIFT, then CAPS SHIFT, both released (a combined tap may type the letter), then L (compile)
    emu.post('/keyboard/press', {'key': 'ss'})
    time.sleep(0.1)
    emu.post('/keyboard/press', {'key': 'cs'})
    time.sleep(0.15)
    emu.post('/keyboard/release', {'key': 'cs'})
    emu.post('/keyboard/release', {'key': 'ss'})
    time.sleep(0.6)
    emu.tap('l')
    time.sleep(args.wait)
    emu.screenshot(os.path.abspath(args.source) + '.pasm-3.0.png')
    data = emu.get(f'/memory/page/ram/{page}?offset={offset}&length={len(expected)}')['data']
    return bytes(data)


def pasm_object_place(address):
    """Where PASM 3.0 puts the code it compiles (seen in unreal-ng): code from #8000 up in an image of the 48K memory
    (#8000-#BFFF in RAM page 3, #C000-#FFFF in page 0), code below #8000 from #2000 of page 4 (#E000 there) whatever
    its ORG; give the ORG as the address there"""
    if address >= 0xC000:
        return 0, address - 0xC000
    if address >= 0x8000:
        return 3, address - 0x8000
    return 4, 0x2000


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('assembler', choices=['alasm509', 'alasm', 'xas', 'storm', 'tasm412', 'zasm315', 'masm11', 'gens4', 'zeus1983', 'primus', 'lasergenius', 'prometheus', 'pasm'])
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
    emu = Emulator(port=args.port, model='48K' if args.assembler in ('gens4', 'zeus1983', 'prometheus') else 'PENTAGON')
    try:
        if args.assembler == 'xas':
            built = xas(emu, args, expected)
        elif args.assembler == 'primus':
            built = primus(emu, args, expected)
        elif args.assembler == 'lasergenius':
            built = lasergenius(emu, args, expected)
        elif args.assembler == 'prometheus':
            built = prometheus(emu, args, expected)
        elif args.assembler == 'pasm':
            built = pasm(emu, args, expected)
        elif args.assembler == 'zeus1983':
            built = zeus1983(emu, args, expected)
        elif args.assembler == 'gens4':
            built = gens4(emu, args, expected)
        elif args.assembler == 'masm11':
            built = masm11(emu, args, expected)
        elif args.assembler == 'zasm315':
            built = zasm315(emu, args, expected)
        elif args.assembler == 'tasm412':
            built = tasm412(emu, args, expected)
        elif args.assembler == 'storm':
            if args.descriptor == 'alasm-5.09':
                args.descriptor = 'storm-1.3'
            built = storm(emu, args, expected)
        else:
            built = alasm509(emu, args, expected)   # every ALASM build: the same steps
    finally:
        if not args.keep:
            emu._request('DELETE', emu.base)   # the instance this run created
    if args.save:
        with open(args.save, 'wb') as f:
            f.write(built)
    if built == expected:
        print(f'OK: {len(expected)} bytes from #{args.address:04X} agree with sjasmplus')
        return 0
    if len(built) < len(expected):
        print(f'DIFFER: the assembler gave {len(built)} bytes, sjasmplus {len(expected)}')
        return 1
    first = next(i for i in range(len(expected)) if built[i] != expected[i])
    print(f'DIFFER at #{args.address + first:04X}: assembler {built[first:first + 8].hex()} sjasmplus {expected[first:first + 8].hex()}')
    return 1


if __name__ == '__main__':
    sys.exit(main())
