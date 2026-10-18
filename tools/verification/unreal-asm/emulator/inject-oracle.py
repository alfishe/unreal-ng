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
    inject-oracle.py lasergenius <LG_1_04.SCL> <source.asm> <address> [--wait 20]   # lasergeniushx: the hash extensions loaded
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
names the page), or onto the disk in drive A for XAS (LTEXT); --template always takes the INCLUDEs (zxasm inject reads
them beside the source), with --project those XAS reads from its disk go onto <source>.includes.trd, put in drive A.

With --template (Z1) the source goes into the template snapshot with `zxasm inject` instead, without an emulator; the
snapshot written is then loaded into a fresh instance and assembled there (make-template.py makes templates, every
profile). An assembler that saves its code to its disk (ZAsm, Primus) gets that disk back with --media <disk>.

The profiles (how each assembler starts, assembles and gives its bytes) are in ../lib/assemblers.py.

The source is sjasmplus (any dialect with a frontend works with --from). ALASM 5.09 assembles #8000-#BFFF into its
system page: give sources that ORG below #8000. Exit 0 when the bytes agree; a screenshot of the assembler's report
is saved next to the source as <source>.<assembler>.png.
"""
import argparse
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib'))
from assemblers import PROFILES  # noqa: E402
from emulator import DEFAULT_PORT, Emulator  # noqa: E402


def sjasmplus_bytes(sjasmplus, source, address):
    """What sjasmplus builds from `source`, from `address` to the end"""
    import tempfile
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


def report(reply):
    print('inject:', {k: reply.get(k) for k in ('assembler', 'name', 'page', 'bytes', 'lines', 'previous', 'includes', 'message')})
    for d in reply.get('diagnostics', []):
        if d.get('severity') != 'info':
            print('  ', d)


def inject(emu, args, profile, expected):
    """The source into the assembler: live (the profile's boot, then POST /asm/sync/inject) or, with --template, by
    zxasm into the template snapshot, which is then loaded"""
    with open(args.source, encoding='utf-8') as f:
        text = f.read()
    extra = profile.extra(args, expected) if profile.extra else ''
    if args.template:
        source = args.source
        if extra:
            source = os.path.abspath(args.source) + '.extra' + os.path.splitext(args.source)[1]   # beside it: its INCLUDEs resolve
            with open(source, 'w', encoding='utf-8') as f:
                f.write(text + extra)
        injected = os.path.abspath(args.source) + '.injected' + os.path.splitext(args.disk)[1]
        command = [args.zxasm, 'inject', source, args.disk, '-o', injected, '--from', args.from_, '--title', 'INJECTED',
                   '--assembler', args.descriptor] + (['--force'] if profile.force else [])
        includes = None
        if args.project:
            # an assembler that reads its INCLUDEs from the disk (XAS) gets them on a disk of their own, put in drive A
            includes = os.path.abspath(args.source) + '.includes.trd'
            if os.path.exists(includes):
                os.remove(includes)
            command += ['--disk', includes]
        run = subprocess.run(command, capture_output=True, text=True)
        for line in run.stderr.strip().splitlines():
            print('zxasm inject:', line)
        if run.returncode != 0:
            raise SystemExit(run.stderr)
        if includes and os.path.exists(includes):
            emu.insert_disk(includes)
        # the disk goes in before the snapshot is loaded and the recording insert_disk starts is stopped: under TTD
        # recording the FDC runs with real timing (no fast disk), and ZAsm 3.80.4's disk check times out (TODO.md)
        if profile.disk_at_finish:
            if not args.media:
                raise SystemExit(f'{args.assembler} saves its code to its disk: give the disk with --media')
            emu.insert_disk(os.path.abspath(args.media))
            if profile.media:
                profile.media(emu, args, os.path.abspath(args.media))
        emu.stop_recording()
        reply = emu.post('/snapshot/load', {'path': injected})
        if not reply.get('ok', True) or reply.get('error'):
            raise SystemExit(f'snapshot load: {reply}')
        emu.ensure_running()
        time.sleep(1)
        return
    profile.boot(emu, args)
    body = {'from': args.from_, 'assembler': args.descriptor, 'title': 'INJECTED'}
    if profile.force:
        body['force'] = True
    if args.project and not extra:
        body['path'] = os.path.abspath(args.source)    # the emulator reads the sources it INCLUDEs beside it
    else:
        body['text'] = text + extra
    report(emu.post('/asm/sync/inject', body))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('assembler', choices=sorted(PROFILES))
    parser.add_argument('disk', help='the assembler disk or tape, or with --template an idle template snapshot')
    parser.add_argument('--template', action='store_true', help='inject with zxasm into the template snapshot instead (Z1)')
    parser.add_argument('--media', help='--template: the assembler disk put back in after the load (ZAsm, Primus save to it)')
    parser.add_argument('--zxasm', default=os.environ.get('ZXASM', 'zxasm'))
    parser.add_argument('--boot', help='the BASIC program that starts it (alasm default alasm64, xas: XAS7.447 ...)')
    parser.add_argument('--assembler', dest='descriptor', help='the descriptor (alasm-4.44, xas-4.18 ...; default the profile\'s)')
    parser.add_argument('--project', action='store_true',
                        help='give the source as a path: the files it INCLUDEs go into texts of their own (ALASM pages)')
    parser.add_argument('--combo-frames', type=int, default=4, help='xas: frames EXT is held (9.x: 12)')
    parser.add_argument('--right', type=int, default=0, help='xas: cursor moves right in the file list before loading (past XMACROS)')
    parser.add_argument('--install', type=int, help='prometheus: the address its installer moves the code to (default its own)')
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
    profile = PROFILES[args.assembler]
    args.descriptor = args.descriptor or profile.descriptor
    expected = sjasmplus_bytes(args.sjasmplus, args.source, args.address)
    emu = Emulator(port=args.port, model=profile.model, ram_size=profile.ram_size)
    try:
        inject(emu, args, profile, expected)
        built = profile.finish(emu, args, expected)
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
