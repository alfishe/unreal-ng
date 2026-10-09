#!/usr/bin/env python3
"""Golden RAM dumps for the asm-synchronizer (docs/inprogress/2026-10-05-unreal-asm/asm-synchronizer.md §10): run an
assembler in unreal-ng, stop it in a known editor state, keep the RAM pages its text lives in, then let the assembler
save the text itself and keep the file. The synchronizer's reader must give that file from the pages, byte for byte.

    sync-dumps.py alasm509 <al509.trd> <out-dir> [--port P]      # SNAKE.H: loaded, a line being typed, edited
    sync-dumps.py alasm444 <al444.trd> <out-dir> [--port P]      # AL444nfo.H: loaded, edited
    sync-dumps.py tasm412  <tasm412.trd> <out-dir> [--port P]    # SNAKE.A: the cursor at the top, in the middle,
                                                                 # a line being typed, at the command line

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


def saved(emu, name, type_):
    data = emu.read_disk_file(name, type_)
    if data is None:
        raise SystemExit(f'{name}.{type_} not on the disk after saving')
    return data[1]


def alasm(emu, args, version):
    name, typed = ('SNAKE', 'wsnake') if version == '509' else ('AL444nfo', 'wal444NFO')
    emu.insert_disk(args.disk)
    emu.run_trdos('alasm64' if version == '509' else 'al64_444')
    emu.type(typed)                            # the keyboard is inverted: lower case arrives as capitals
    emu.tap('enter')
    time.sleep(3)
    text_page = emu.read(0x80CC, 1)[0] & 7     # IX+#0D: the driver's page id, low bits = port #7FFD
    pages = [2, text_page]
    prefix = f'alasm{version}'
    dump(emu, args.out, f'{prefix}-loaded', pages, f'{name}.H', None, {'editor': False, 'typing': False})
    emu.type('s')
    emu.tap('enter')
    time.sleep(4)
    loaded = saved(emu, name, 'H')
    finish(args.out, f'{prefix}-loaded', f'{name}.H', loaded)
    emu.type('e')
    emu.tap('enter')
    time.sleep(2)
    emu.type('        NOP')
    if version == '509':                       # a line not entered yet: not in the text, the file before it
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
    parser.add_argument('assembler', choices=['alasm509', 'alasm444', 'tasm412'])
    parser.add_argument('disk')
    parser.add_argument('out')
    parser.add_argument('--port', type=int, default=DEFAULT_PORT)
    args = parser.parse_args()
    args.disk = os.path.abspath(args.disk)
    emu = Emulator(port=args.port, model='PENTAGON')
    if args.assembler == 'tasm412':
        tasm412(emu, args)
    else:
        alasm(emu, args, args.assembler[5:])
    emu.stop_recording()
    return 0


if __name__ == '__main__':
    sys.exit(main())
