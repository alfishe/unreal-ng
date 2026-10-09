#!/usr/bin/env python3
"""Golden RAM dumps for the asm-synchronizer (docs/inprogress/2026-10-05-unreal-asm/asm-synchronizer.md §10): run an
assembler in unreal-ng, stop it in a known editor state, keep the RAM pages its text lives in, then let the assembler
save the text itself and keep the file. The synchronizer's reader must give that file from the pages, byte for byte.

    sync-dumps.py alasm509 <al509.trd> <out-dir> [--port P]      # SNAKE.H: loaded, a line being typed, edited
    sync-dumps.py alasm444 <al444.trd> <out-dir> [--port P]      # AL444nfo.H: loaded, edited
    sync-dumps.py tasm412  <tasm412.trd> <out-dir> [--port P]    # SNAKE.A: the cursor at the top, in the middle,
                                                                 # a line being typed, at the command line
    sync-dumps.py alasm <disk> <out-dir> --boot NAME --source NAME --tag TAG [--help-key] [--no-loaded]
                                                                 # any other ALASM build: TAG-typing, TAG-edited
    sync-dumps.py xas <disk-with-PROBE.X> <out-dir> --boot NAME --list-keys right[,down...] --tag TAG
                                                                 # XAS: TAG-typing, TAG-edited (the text at #C000)
    sync-dumps.py storm <disk-with-NAME.C> <out-dir> --boot NAME --source NAME --tag TAG
                                                                 # STORM: TAG-typing, TAG-edited (page 6 from #C00B)
    sync-dumps.py zasm <ZASM315.trd> <out-dir> --source ovlib --big service --tag zasm315
                                                                 # ZAsm 3.15: TAG-typing, TAG-edited, TAG-big
    sync-dumps.py masm <disk-with-NAME.a first> <out-dir> --source NAME --tag masm11
                                                                 # MASM 1.1: TAG-typing, TAG-edited, TAG-menu

Each case is a folder <out-dir>/<assembler>-<case>/ with machine.json (the RAM page mapped at each 16 KB window, the
pages kept, the file expected, the editor state), page<N>.bin per kept page and the expected file. A case whose text
holds a line not yet entered (ALASM keeps it apart: NotInText) expects the file saved before the typing.
The assembler disks are the collection's (ALASM 5.09 / 4.44, TASM 4.12); the tool creates its own Pentagon instance.
"""
import argparse
import json
import os
import sys
import time
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib'))
from emulator import DEFAULT_PORT, Emulator  # noqa: E402


def page(emu, n):
    return urllib.request.urlopen(f'{emu.base}/memory/page/ram/{n}?format=binary&offset=0&length=16384').read()


def windows(emu):
    banks = json.loads(urllib.request.urlopen(f'{emu.base}/state/paging').read()).get('banks', [])
    return [b['page'] if b.get('type') == 'RAM' else -1 for b in banks]


def extend(emu, key):
    emu.post('/keyboard/combo', {'keys': ['cs', 'ss'], 'frames': 12})
    emu.idle()
    time.sleep(0.4)
    emu.tap(key)
    time.sleep(1)


def dump(emu, out, case, pages, expected_name, expected, state):
    """Keep the pages now (paused), the file expected and the state"""
    folder = os.path.join(out, case)
    os.makedirs(folder, exist_ok=True)
    time.sleep(0.6)
    emu.post('/pause')
    kept = {n: page(emu, n) for n in pages}
    mapped = windows(emu)
    emu.post('/resume')
    for n, b in kept.items():
        open(os.path.join(folder, f'page{n}.bin'), 'wb').write(b)
    meta = {'windows': mapped, 'pages': sorted(kept), 'expected': expected_name, 'state': state}
    json.dump(meta, open(os.path.join(folder, 'machine.json'), 'w'), indent=2)
    if expected is not None:
        open(os.path.join(folder, expected_name), 'wb').write(expected)
    print(case, meta)
    return folder


def finish(out, case, expected_name, expected):
    open(os.path.join(out, case, expected_name), 'wb').write(expected)


def saved_sectors(emu, name, type_):
    """The last catalog entry NAME.T as whole sectors (XAS leaves the length field 0 and writes whole sectors)"""
    import base64
    entries = [f for f in emu.get('/disk/A/catalog').get('files', []) if f['name'].strip() == name and f['type'] == type_]
    if not entries:
        raise SystemExit(f'{name}.{type_} not on the disk')
    f = entries[-1]
    data = b''
    for k in range(f['sectors']):
        track, sector = divmod(f['first_track'] * 16 + f['first_sector'] + k, 16)
        data += base64.b64decode(emu.get(f'/disk/A/sector/{track // 2}/{track % 2}/{sector + 1}')['data_base64'])
    return data


def xas(emu, args):
    """XAS: the source PROBE.X on the disk, chosen in the file list with --list-keys; the text lives at #C000"""
    emu.insert_disk(args.disk)
    emu.run_trdos(args.boot, wait=8)
    for key in filter(None, args.list_keys.split(',')):
        emu.tap(key)
        time.sleep(0.3)
    emu.tap('enter')                           # load
    time.sleep(3)
    loaded = saved_sectors(emu, 'PROBE', 'X')
    pages = [2, windows(emu)[3]]
    emu.type('        nop')                    # a line on the screen row, not packed into the text until Enter
    dump(emu, args.out, f'{args.tag}-typing', pages, 'PROBE.X', loaded, {'editor': True, 'typing': True})
    emu.tap('enter')
    time.sleep(1)
    dump(emu, args.out, f'{args.tag}-edited', pages, 'PROBE.X', None, {'editor': True, 'typing': False})
    extend(emu, 's')                           # Save text NAME: Enter keeps the name
    emu.tap('enter')
    time.sleep(4)
    finish(args.out, f'{args.tag}-edited', 'PROBE.X', saved_sectors(emu, 'PROBE', 'X'))


def storm(emu, args):
    """STORM: BREAK L loads (the keyboard is in BIG mode: lower case arrives as capitals), BREAK S saves; a line joins
    the text when the cursor leaves it, so the edited case is taken after Enter and a step down"""
    emu.insert_disk(args.disk)
    emu.run_trdos(args.boot, wait=12)
    emu.tap('break')
    emu.tap('l')
    emu.type(args.source.lower())
    emu.tap('enter')
    time.sleep(3)
    loaded = saved(emu, args.source, 'C')
    pages = [2, windows(emu)[3]]
    emu.type(' nop')
    dump(emu, args.out, f'{args.tag}-typing', pages, f'{args.source}.C', loaded, {'editor': True, 'typing': True})
    emu.tap('enter')
    emu.tap('down')
    time.sleep(1)
    dump(emu, args.out, f'{args.tag}-edited', pages, f'{args.source}.C', None, {'editor': True, 'typing': False})
    emu.tap('break')
    emu.tap('s')
    time.sleep(1)
    emu.tap('enter')                           # the name it loaded
    time.sleep(4)
    finish(args.out, f'{args.tag}-edited', f'{args.source}.C', saved(emu, args.source, 'C'))


def zasm(emu, args):
    """ZAsm 3.15: File / Load; COMMAND (Extend) SS+2 saves under the name (with a ";!" editor-state first line). The
    text runs from (#8829) to (#8837); its part above #C000 is in RAM page 6"""
    def start():
        emu.insert_disk(args.disk)
        emu.run_trdos('boot', wait=15)
        emu.tap('enter')                       # "No Disk!" (it starts on drive D): Retry, drive A
        emu.tap('a')
        time.sleep(6)

    def load(name):
        emu.tap('enter')                       # File
        time.sleep(2)
        emu.tap('enter')                       # Load
        time.sleep(3)
        emu.type(name)                         # as stored: ZAsm keeps the case
        emu.tap('enter')
        time.sleep(3)

    start()
    load(args.big)
    dump(emu, args.out, f'{args.tag}-big', [2, 5, 6], f'{args.big}.a', saved(emu, args.big, 'a'), {'editor': True, 'typing': False})
    start()
    load(args.source)
    loaded = saved(emu, args.source, 'a')
    pages = [2, 5, 6]
    emu.type(' nop')
    dump(emu, args.out, f'{args.tag}-typing', pages, f'{args.source}.a', loaded, {'editor': True, 'typing': True})
    emu.tap('enter')
    emu.tap('down')
    time.sleep(1)
    dump(emu, args.out, f'{args.tag}-edited', pages, f'{args.source}.a', None, {'editor': True, 'typing': False})
    emu.post('/keyboard/combo', {'keys': ['cs', 'ss'], 'frames': 6})   # COMMAND:
    emu.idle()
    time.sleep(0.8)
    emu.post('/keyboard/combo', {'keys': ['ss', '2'], 'frames': 4})    # Save Changes
    time.sleep(4)
    finish(args.out, f'{args.tag}-edited', f'{args.source}.a', saved(emu, args.source, 'a'))


def masm(emu, args):
    """MASM 1.1: W takes the first source, E edits; SS+Enter saves from the editor, EXT Q goes back to the menu. The
    edited case is taken in the editor (the cursor line out of the text), the menu case after leaving it"""
    emu.insert_disk(args.disk)
    emu.run_trdos('MASM 1.1', wait=8)
    emu.tap('enter')                           # past the title
    emu.tap('w')
    time.sleep(2)
    emu.tap('enter')
    time.sleep(3)
    emu.tap('e', 8)
    time.sleep(1.5)
    loaded = saved(emu, args.source, 'a')
    pages = [2, 5, windows(emu)[3]]
    emu.type(' nop')
    dump(emu, args.out, f'{args.tag}-typing', pages, f'{args.source}.a', loaded, {'editor': True, 'typing': True})
    emu.tap('enter')
    for _ in range(3):
        emu.tap('down')
    time.sleep(1)
    dump(emu, args.out, f'{args.tag}-edited', pages, f'{args.source}.a', None, {'editor': True, 'typing': False})
    emu.post('/keyboard/combo', {'keys': ['symbol', 'enter'], 'frames': 4})   # SS+Enter: save
    time.sleep(4)
    edited = saved(emu, args.source, 'a')
    finish(args.out, f'{args.tag}-edited', f'{args.source}.a', edited)
    emu.post('/keyboard/combo', {'keys': ['caps', 'symbol'], 'frames': 4})   # EXT
    time.sleep(0.5)
    emu.tap('q')
    time.sleep(1)
    dump(emu, args.out, f'{args.tag}-menu', pages, f'{args.source}.a', edited, {'editor': False, 'typing': False})


def saved(emu, name, type_):
    data = emu.read_disk_file(name, type_)
    if data is None:
        raise SystemExit(f'{name}.{type_} not on the disk after saving')
    return data[1]


def inverted(name):
    """What to type for a name: the keyboard is inverted, lower case arrives as capitals"""
    return ''.join(c.lower() if c.isupper() else c.upper() for c in name)


def alasm(emu, args, version):
    if version == '509':
        name, boot, prefix = 'SNAKE', 'alasm64', 'alasm509'
    elif version == '444':
        name, boot, prefix = 'AL444nfo', 'al64_444', 'alasm444'
    else:
        name, boot, prefix = args.source, args.boot, args.tag
    emu.insert_disk(args.disk)
    emu.run_trdos(boot)
    if args.help_key:
        emu.tap('enter')                       # 3.8c shows a help screen first: one key closes it
        time.sleep(1)
    emu.type('w' + inverted(name))
    emu.tap('enter')
    time.sleep(3)
    text_page = emu.read(0x80CC, 1)[0] & 7     # IX+#0D: the driver's page id, low bits = port #7FFD
    pages = [2, text_page]
    if not args.no_loaded:
        dump(emu, args.out, f'{prefix}-loaded', pages, f'{name}.H', None, {'editor': False, 'typing': False})
    emu.type('s')
    emu.tap('enter')
    time.sleep(4)
    loaded = saved(emu, name, 'H')
    if not args.no_loaded:
        finish(args.out, f'{prefix}-loaded', f'{name}.H', loaded)
    emu.type('e')
    emu.tap('enter')
    time.sleep(2)
    emu.type('        NOP')
    if version != '444':                       # a line not entered yet: not in the text, the file before it
        dump(emu, args.out, f'{prefix}-typing', pages, f'{name}.H', loaded, {'editor': True, 'typing': True})
    emu.tap('enter')
    time.sleep(1)
    extend(emu, 'q')
    emu.tap('enter')
    time.sleep(1)
    dump(emu, args.out, f'{prefix}-edited', pages, f'{name}.H', None, {'editor': False, 'typing': False})
    emu.type('s')
    emu.tap('enter')
    time.sleep(4)
    finish(args.out, f'{prefix}-edited', f'{name}.H', saved(emu, name, 'H'))


def tasm412(emu, args):
    emu.insert_disk(args.disk)
    emu.run_trdos('TASM4.12')
    emu.tap('enter')                           # past the title
    time.sleep(1)
    emu.tap('w')
    emu.type('snake')
    emu.tap('enter')
    time.sleep(3)
    pages = [2, 6]                             # the lower part in the #8000 window, the upper part at #C000

    def save_into(case):
        extend(emu, 'q')
        emu.tap('s')
        time.sleep(4)
        finish(args.out, case, 'SNAKE.A', saved(emu, 'SNAKE', 'A'))

    emu.tap('e')
    time.sleep(2)
    dump(emu, args.out, 'tasm412-top', pages, 'SNAKE.A', None, {'editor': True, 'typing': False})
    save_into('tasm412-top')
    emu.tap('e')
    time.sleep(2)
    for _ in range(20):
        emu.tap('down')
    dump(emu, args.out, 'tasm412-middle', pages, 'SNAKE.A', None, {'editor': True, 'typing': False})
    save_into('tasm412-middle')
    emu.tap('e')
    time.sleep(2)
    for _ in range(5):
        emu.tap('down')
    emu.type('zz')                             # over the start of the line (the line buffer), not entered
    dump(emu, args.out, 'tasm412-typing', pages, 'SNAKE.A', None, {'editor': True, 'typing': True})
    emu.tap('enter')
    time.sleep(1)
    save_into('tasm412-typing')
    dump(emu, args.out, 'tasm412-command', pages, 'SNAKE.A', saved(emu, 'SNAKE', 'A'), {'editor': False, 'typing': False})


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('assembler', choices=['alasm509', 'alasm444', 'tasm412', 'alasm', 'xas', 'storm', 'zasm', 'masm'])
    parser.add_argument('disk')
    parser.add_argument('out')
    parser.add_argument('--port', type=int, default=DEFAULT_PORT)
    parser.add_argument('--boot', help='alasm: the BASIC program that starts it')
    parser.add_argument('--source', help='alasm: the H file on its disk to load')
    parser.add_argument('--tag', help='alasm: the case folder prefix (alasm446 ...)')
    parser.add_argument('--help-key', action='store_true', help='alasm: a key closes a help screen at the start (3.8c)')
    parser.add_argument('--no-loaded', action='store_true', help='alasm: no case right after loading')
    parser.add_argument('--list-keys', default='right', help='xas: the cursor keys that reach PROBE in the file list')
    parser.add_argument('--big', help='zasm: a long text (over #C000) for a loaded case')
    args = parser.parse_args()
    args.disk = os.path.abspath(args.disk)
    emu = Emulator(port=args.port, model='PENTAGON')
    if args.assembler == 'tasm412':
        tasm412(emu, args)
    elif args.assembler == 'alasm':
        alasm(emu, args, 'other')
    elif args.assembler == 'xas':
        xas(emu, args)
    elif args.assembler == 'storm':
        storm(emu, args)
    elif args.assembler == 'zasm':
        zasm(emu, args)
    elif args.assembler == 'masm':
        masm(emu, args)
    else:
        alasm(emu, args, args.assembler[5:])
    emu.stop_recording()
    return 0


if __name__ == '__main__':
    sys.exit(main())
