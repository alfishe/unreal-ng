"""The assemblers part B writes into (asm-synchronizer.md §18), each as a profile: how it is started in a fresh
instance until it waits at its prompt (`boot`), how a text put there is assembled and its bytes read (`finish`), and
what the inject needs (`force`: no "changed" flag to honour; `extra`: lines the source gets). inject-oracle.py runs
them live or through a template, make-template.py saves the moment `boot` ends as an idle template.

Every `boot(emu, args)` gets the assembler's disk or tape in `args.disk` and may use `args.boot` (the program that
starts it), `args.right`, `args.combo_frames`, `args.help_key`; every `finish(emu, args, expected)` returns the bytes
the assembler built from `args.address`, `len(expected)` of them.
"""
import importlib.util
import os
import subprocess
import tempfile
import time


def _aie():
    """assemble-in-emulator.py as a module (its tape readers)"""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'emulator', 'assemble-in-emulator.py')
    spec = importlib.util.spec_from_file_location('aie', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _shot(emu, args, descriptor, suffix=''):
    emu.screenshot(os.path.abspath(args.source) + '.' + descriptor + suffix + '.png')


# ---- ALASM (every build: the same steps) ----

def alasm_boot(emu, args):
    emu.insert_disk(os.path.abspath(args.disk))
    emu.run_trdos(args.boot or 'alasm64')
    time.sleep(3)                                  # the memory test, then the A> prompt
    if args.help_key:
        emu.tap('enter')                           # 3.8c shows its help screen first
        time.sleep(1)


def alasm_finish(emu, args, expected):
    emu.type('a')                                  # ASSEMBLE
    emu.tap('enter')
    time.sleep(args.wait)
    _shot(emu, args, args.descriptor)
    return bytes(emu.read(args.address, len(expected)))


# ---- XAS ----

def xas_boot(emu, args):
    emu.insert_disk(os.path.abspath(args.disk))
    emu.run_trdos(args.boot)
    time.sleep(4)                                  # the file list
    for _ in range(args.right):
        emu.tap('right')                           # past XMACROS (4.x / 5.05SE list the macro file first)
    emu.tap('enter')                               # the first source (or a new text) into the editor
    time.sleep(3)
    emu.post('/keyboard/combo', {'keys': ['cs', 'ss'], 'frames': args.combo_frames})   # EXT: the command line
    time.sleep(1)


def xas_finish(emu, args, expected):
    emu.tap('a')                                   # ASSEMBLE
    time.sleep(args.wait)
    _shot(emu, args, args.descriptor)
    return bytes(emu.read(args.address, len(expected)))


# ---- STORM ----

def storm_boot(emu, args):
    emu.insert_disk(os.path.abspath(args.disk))
    emu.run_trdos(args.boot or 'STORM1.3', wait=12)


def storm_finish(emu, args, expected):
    time.sleep(1)                                  # PGUP redraws the editor from the text's start
    _shot(emu, args, args.descriptor, '.editor')
    emu.tap('break')
    emu.tap('a')                                   # Assemble
    time.sleep(args.wait)
    _shot(emu, args, args.descriptor)
    emu.tap('space')                               # leaves an error list
    time.sleep(1)
    emu.tap('break')
    emu.tap('q')                                   # Quit to BASIC: the 48K memory holds the code again
    time.sleep(3)
    return bytes(emu.read(args.address, len(expected)))


# ---- TASM 4.12 ----

def tasm_boot(emu, args):
    emu.insert_disk(os.path.abspath(args.disk))
    emu.run_trdos(args.boot or 'TASM4.12')
    emu.tap('enter')                               # past the title
    time.sleep(1)


def tasm_finish(emu, args, expected):
    emu.write(args.address, [0xAA] * len(expected))   # bytes TASM does not write stay visible
    time.sleep(1)                                  # 4.0 drops a key that comes at once
    emu.tap('a')                                   # Assemble: the text in memory, under the work name
    time.sleep(args.wait)
    _shot(emu, args, args.descriptor)
    return bytes(emu.read(args.address, len(expected)))


# ---- ZAsm 3.15 ----

def zasm_boot(emu, args):
    emu.insert_disk(os.path.abspath(args.disk))
    emu.run_trdos(args.boot or 'boot', wait=15)
    emu.tap('enter')                               # "No Disk!" (ZAsm starts on drive D): Retry, drive A
    emu.tap('a')
    time.sleep(6)
    emu.tap('enter')                               # File
    time.sleep(2)
    if args.descriptor == 'zasm-4.0x8':            # its drive D is built in (no settings file): Drive, A, then Load
        emu.tap('d')
        time.sleep(1)
        emu.tap('a')
        time.sleep(2)
        emu.tap('l')
    else:
        emu.tap('enter')                           # Load
    time.sleep(3)
    emu.type('STUB')
    emu.tap('enter')                               # the editor opens on it
    time.sleep(3)
    emu.post('/keyboard/combo', {'keys': ['cs', 'ss'], 'frames': 4})   # COMMAND
    emu.idle()
    time.sleep(0.6)


# the keys from RUN "boot" to the main menu of each Pentagon 512 ZAsm (.recipe/assemblers/zxasm.md): a key, or a pause
ZASM_START_KEYS = {
    'zasm-3.2x': ['space', 3],
    'zasm-lite-1.07': [],
    'zasm-3.3.02': ['space', 3],
    'zasm-3.3.51': ['space', 4],
    'zasm-3.3.final': ['space', 4],
    'zasm-3.80.4': ['space', 4],
    'zasm-3.4': ['space', 4],
    'zasm-x64.1': ['space', 4],
    'zasm-4.0x8': ['space', 5, 'space', 3],
    'zasm-4.x64': [6, 'space', 4, ('cs', 'space'), 3],
    'zasm-4.20': ['space', 4],
}


def _drive_a_disk(disk):
    """A copy of the disk whose ZAsm settings file (type s / S, bytes +3 / +4 the system and overlays drives) says
    drive A where it says D: the shipped D asks "No Disk" before every load"""
    data = bytearray(open(disk, 'rb').read())
    for k in range(128):
        entry = data[k * 16:k * 16 + 16]
        if entry[0] in (0, 1):
            continue
        if chr(entry[8]) in 'sS':
            at = (entry[15] * 16 + entry[14]) * 256
            for off in (3, 4):
                if data[at + off] == ord('D'):
                    data[at + off] = ord('A')
    out = os.path.join(tempfile.gettempdir(), 'unreal-asm-zasm-' + os.path.basename(disk))
    open(out, 'wb').write(data)
    return out


def zasmr_media(emu, args, disk):
    """The disk a Pentagon 512 ZAsm needs besides the program: the copy whose settings say drive A (its overlays and
    files go to the system drive; inject-oracle --template's --media disk is the original); 4.0x8 has drive D built
    in, so the disk goes into D too (a copy: one source takes one slot)"""
    if not os.path.basename(disk).startswith('unreal-asm-zasm-'):
        disk = _drive_a_disk(disk)
        emu.post('/media/A/swap', {'path': disk, 'discard': True, 'immediate': True})
    if args.descriptor == 'zasm-4.0x8':
        copy = os.path.join(tempfile.gettempdir(), 'unreal-asm-zasm-d-' + os.path.basename(disk))
        open(copy, 'wb').write(open(disk, 'rb').read())
        emu.post('/media/D/swap', {'path': copy, 'discard': True, 'immediate': True})


def zasmr_boot(emu, args):
    """A Pentagon 512 ZAsm (3.2x ... 4.20): started, its STUB source loaded (File / Load: the editor opens on it), its
    COMMAND prompt opened (CS+SS)"""
    disk = _drive_a_disk(os.path.abspath(args.disk))
    emu.insert_disk(disk)
    zasmr_media(emu, args, disk)
    emu.run_trdos(args.boot or 'boot', wait=10)
    for key in ZASM_START_KEYS[args.descriptor]:
        if isinstance(key, (int, float)):
            time.sleep(key)
        elif isinstance(key, tuple):
            emu.post('/keyboard/combo', {'keys': list(key), 'frames': 4})
            emu.idle()
        else:
            emu.tap(key)
    emu.tap('enter')                               # File
    time.sleep(2)
    if args.descriptor == 'zasm-4.0x8':            # its drive D is built in (no settings file): Drive, A, then Load
        emu.tap('d')
        time.sleep(1)
        emu.tap('a')
        time.sleep(2)
        emu.tap('l')
    else:
        emu.tap('enter')                           # Load
    time.sleep(3)
    emu.type('STUB')
    emu.tap('enter')                               # the editor opens on it
    time.sleep(3)
    emu.post('/keyboard/combo', {'keys': ['cs', 'ss'], 'frames': 4})   # COMMAND
    emu.idle()
    time.sleep(0.6)


def zasm310_boot(emu, args):
    """ZX-ASM 3.10 (and ZX ASM 3.0): its STUB.A (3.0: STUB.C) loaded (File / Load; the name input types capitals),
    COMMAND opened (CS+SS)"""
    v30 = args.descriptor == 'zasm-3.0'
    emu.insert_disk(os.path.abspath(args.disk))
    emu.run_trdos(args.boot or ('ZX-TASM3' if v30 else 'ZASM3.10'), wait=10)
    emu.tap('enter')                               # File
    time.sleep(1)
    emu.tap('enter')                               # Load
    time.sleep(2)
    emu.type('stub' if v30 else 'stub.a')
    emu.tap('enter')
    time.sleep(3)
    emu.post('/keyboard/combo', {'keys': ['cs', 'ss'], 'frames': 4})   # COMMAND
    emu.idle()
    time.sleep(0.6)


def zasm2_boot(emu, args):
    """ZX-ASM 2.4 / 2.5 / 2.6: started; the text is written at its menu (it starts on Edit with an empty text)"""
    emu.insert_disk(os.path.abspath(args.disk))
    boot = {'zasm-2.4': 'ZXASM2.4', 'zasm-2.5': 'ZXASM2.5', 'zasm-2.6': 'boot'}[args.descriptor]
    emu.run_trdos(args.boot or boot, wait=10)


def zasm2_finish(emu, args, expected):
    """ZX-ASM 2.x: Compile / Assemble from its menu; it puts the code right behind the text, from its end word (seen in
    unreal-ng: 2.6 assembles into that buffer whatever the ORG)"""
    emu.tap('right')                               # Compile
    emu.tap('enter')
    time.sleep(1)
    emu.tap('enter')                               # Assemble
    time.sleep(args.wait)
    _shot(emu, args, args.descriptor)
    end_at = 0x6005 if args.descriptor == 'zasm-2.6' else 0x6275
    end = emu.read(end_at, 2)
    return bytes(emu.read(end[0] | end[1] << 8, len(expected)))


def zasm30_finish(emu, args, expected):
    """ZX ASM 3.0: COMMAND Q to the menu, Compile / Assemble; the code for #4000-#7FFF is in RAM page 4 (seen in
    unreal-ng)"""
    import base64
    if not 0x4000 <= args.address < 0x8000:
        raise SystemExit('ZX ASM 3.0: give a source at #4000-#7FFF (its code is read from RAM page 4)')
    emu.tap('q')                                   # COMMAND Q: the main menu
    time.sleep(1)
    emu.tap('right')                               # Compile
    emu.tap('right')
    emu.tap('enter')
    time.sleep(1)
    emu.tap('enter')                               # Assemble
    time.sleep(args.wait)
    _shot(emu, args, args.descriptor)
    page = emu.get(f'/memory/page/ram/4?offset={args.address & 0x3FFF}&length={len(expected)}')
    return bytes(page['data']) if isinstance(page.get('data'), list) else base64.b64decode(page.get('data_base64', ''))


def zasmr_finish(emu, args, expected):
    emu.tap('a')                                   # Assemble
    time.sleep(args.wait)
    for _ in range(2):
        if emu.read_disk_file('out', 'C'):
            break
        # "No Disk": a drive it was not given (Lite's built-in D; after a template's disk went in, the others ask once
        # too): Retry, drive A
        emu.tap('enter')
        time.sleep(1)
        emu.tap('a')
        time.sleep(args.wait)
    _shot(emu, args, args.descriptor)
    emu.tap('n')                                   # "Launch?" No
    time.sleep(2)
    data = emu.read_disk_file('out', 'C')
    return bytes(data[1]) if data else b''


def zasmr_extra(args, expected):
    return f'\n        SAVEBIN "a:out.C",#{args.address:04X},{len(expected)}\n'


def zasm_extra(args, expected):
    return f'\n        SAVEBIN "out.C",#{args.address:04X},{len(expected)}\n'   # ZAsm compiles into its own pages


def zasm_finish(emu, args, expected):
    emu.tap('a')                                   # Assemble
    time.sleep(5)
    emu.tap('enter')                               # "No Disk!" again: Retry, drive A
    emu.tap('a')
    time.sleep(args.wait)
    _shot(emu, args, args.descriptor)
    emu.tap('n')                                   # "Launch?" No
    time.sleep(2)
    data = emu.read_disk_file('out', 'C')
    return bytes(data[1]) if data else b''


# ---- MASM 1.1 ----

def masm_boot(emu, args):
    emu.insert_disk(os.path.abspath(args.disk))
    emu.run_trdos(args.boot or 'MASM 1.1', wait=8)
    emu.tap('enter')                               # past the title
    time.sleep(1)


def masm_finish(emu, args, expected):
    emu.write(args.address, [0xAA] * len(expected))
    emu.tap('a')                                   # Assemble
    time.sleep(args.wait)
    _shot(emu, args, args.descriptor)
    return bytes(emu.read(args.address, len(expected)))


def masm_save_finish(emu, args, expected):
    """MASM 2.0 and the 1.0 demo (they assemble nothing): the text each holds saved by its own Save under the work
    name and compared with what the converter wrote; `expected` when they agree (no bytes are built)"""
    version = {'masm-2.0': '2.0', 'masm-1.0-demo': '1.0'}[args.descriptor]
    emu.tap('s')                                   # Save file: the work name
    time.sleep(1)
    emu.tap('enter')
    time.sleep(4)
    _shot(emu, args, args.descriptor)
    saved = emu.read_disk_file('INJECTED', 'a')
    with tempfile.TemporaryDirectory() as tmp:
        hobeta = os.path.join(tmp, 'x.$a')
        subprocess.run([args.zxasm, 'inject', args.source, os.path.join(tmp, 'y.$a'), '--codec', 'masm', '--version', version,
                        '-o', hobeta], check=True, capture_output=True)
        raw = open(hobeta, 'rb').read()
        written = raw[17:17 + (raw[11] | raw[12] << 8)]   # hobeta: whole sectors, the length in its header
    if saved and saved[1].rstrip(b'\xff') == written.rstrip(b'\xff'):
        print(f'{args.descriptor} saved the injected text: {len(written)} bytes, the converter\'s')
        return expected
    print(f'{args.descriptor} saved {len(saved[1]) if saved else None} bytes, the converter wrote {len(written)}')
    return b''


# ---- GENS4 (48K) ----

def gens_boot(emu, args):
    code = [b for b in _aie().tape_blocks(open(args.disk, 'rb').read()) if len(b) == 10882][0][1:-1]   # the 'gens4' block
    emu.post('/reset')
    time.sleep(3)
    emu.write(26000, code)
    emu.post('/basic/run', {'command': 'RANDOMIZE USR 26000'})
    time.sleep(2)


def gens3m21_boot(emu, args):
    code = [b for b in _aie().tape_blocks(open(args.disk, 'rb').read()) if len(b) == 10037][0][1:-1]   # 'GENS3M21'
    emu.post('/reset')
    time.sleep(3)
    for k in range(0, len(code), 1024):
        emu.write(49000 + k, code[k:k + 1024])
    emu.post('/basic/run', {'command': 'RANDOMIZE USR 49000'})
    time.sleep(2)


def gens3_finish(emu, args, expected):
    emu.write(args.address, [0xAA] * len(expected))
    emu.tap('a')                                   # Assemble: it asks the table size (ENTER: its default) and options
    emu.tap('enter')
    time.sleep(0.5)
    emu.tap('enter')
    time.sleep(0.5)
    emu.type('4')                                  # no listing (GENS3 lists by default and stops at the scroll)
    emu.tap('enter')
    time.sleep(args.wait)
    _shot(emu, args, args.descriptor)
    return bytes(emu.read(args.address, len(expected)))


def gens4b_boot(emu, args):
    """GENS4B (MOA B-Disk, its hobeta gens4b.$c) calls TR-DOS through #3D13, which needs TR-DOS's system variables:
    TR-DOS entered once from 48K BASIC (an empty disk in A) and left, then the code at 30000 and started there"""
    blank = os.path.join(tempfile.gettempdir(), 'unreal-asm-blank.trd')
    image = bytearray(16 * 256 * 160)
    image[8 * 256 + 0xE1:8 * 256 + 0xE8] = bytes([0, 1, 0x16, 0, 0xF0, 0x09, 0x10])   # first free 1/0, 80x2, 2544 free
    image[8 * 256 + 0xF5:8 * 256 + 0xFD] = b'BLANK   '
    open(blank, 'wb').write(image)
    emu.insert_disk(blank)
    emu.post('/reset')
    time.sleep(3)
    for _ in range(3):                             # the menu: 48 BASIC
        emu.tap('down')
    emu.tap('enter')
    time.sleep(2)
    emu.post('/basic/run', {'command': 'RANDOMIZE USR 15616'})
    time.sleep(2)
    emu.post('/basic/run', {'command': 'RETURN'})
    time.sleep(2)
    code = open(args.disk, 'rb').read()[17:]       # the hobeta file's data
    for k in range(0, len(code), 1024):
        emu.write(30000 + k, code[k:k + 1024])
    emu.post('/basic/run', {'command': 'RANDOMIZE USR 30000'})
    time.sleep(3)


def gens43t_boot(emu, args):
    emu.insert_disk(os.path.abspath(args.disk))
    emu.run_trdos(args.boot or 'GENS4tf', wait=8)


def gens43t_finish(emu, args, expected):
    emu.type('A,,OBJ')                             # Assemble to the disk file OBJ (a plain A ends in a disc error)
    emu.tap('enter')
    time.sleep(args.wait)
    _shot(emu, args, args.descriptor)
    saved = emu.read_disk_file('OBJ', 'C')
    if not saved or saved[0] != args.address:
        return b''
    return saved[1]


def gens_finish(emu, args, expected):
    emu.write(args.address, [0xAA] * len(expected))
    emu.type('A,8000')                             # Assemble with a symbol table of 8000 bytes
    emu.tap('enter')
    time.sleep(args.wait)
    _shot(emu, args, args.descriptor)
    return bytes(emu.read(args.address, len(expected)))


# ---- ZEUS 1983 (48K) ----

def zeus_boot(emu, args):
    code = _aie().tap_code(args.disk)
    emu.post('/reset')
    time.sleep(3)
    for k in range(0, len(code), 1024):
        emu.write(57344 + k, code[k:k + 1024])
    emu.post('/basic/run', {'command': 'RANDOMIZE USR 57344'})
    time.sleep(5)
    emu.post('/keyboard/combo', {'keys': ['cs', '2'], 'frames': 6})   # CAPS LOCK: ZEUS takes capitals
    emu.idle()
    time.sleep(0.5)


def zeus_finish(emu, args, expected):
    emu.write(args.address, [0xAA] * len(expected))
    emu.type('a')                                  # Assemble
    emu.tap('enter')
    time.sleep(args.wait)
    _shot(emu, args, args.descriptor)
    return bytes(emu.read(args.address, len(expected)))


# ---- ZEUS GG (B-disk) and v7.E (ZKSoft): from a TR-DOS disk on a 128K ----

def zeus_disk_boot(emu, args):
    emu.insert_disk(os.path.abspath(args.disk))
    emu.run_trdos(args.boot, wait=10)
    emu.post('/keyboard/combo', {'keys': ['cs', '2'], 'frames': 6})   # CAPS LOCK: ZEUS takes capitals
    emu.idle()
    time.sleep(0.5)


# ---- ZEUS 1.1 beta (PHT): its hobeta file's code at 57344 under 48 BASIC of a 128K ----

def zeus11_boot(emu, args):
    emu.post('/reset')
    time.sleep(3)
    for _ in range(3):                             # the menu: 48 BASIC
        emu.tap('down')
    emu.tap('enter')
    time.sleep(2)
    code = open(args.disk, 'rb').read()[17:]       # the hobeta file's data
    for k in range(0, len(code), 1024):
        emu.write(57344 + k, code[k:k + 1024])
    emu.post('/basic/run', {'command': 'RANDOMIZE USR 57344'})
    time.sleep(5)
    emu.post('/keyboard/combo', {'keys': ['cs', '2'], 'frames': 6})   # CAPS LOCK: ZEUS takes capitals
    emu.idle()
    time.sleep(0.5)


# ---- Primus 2.9 ----

def primus_boot(emu, args):
    emu.insert_disk(os.path.abspath(args.disk))
    emu.run_trdos(args.boot or 'P_AS_2.9', wait=12)


def primus_finish(emu, args, expected):
    emu.type('a out')                              # Assemble, the code saved as "out" (caps lock: capitals)
    emu.tap('enter')
    time.sleep(args.wait)
    _shot(emu, args, args.descriptor)
    for name in ('OUT', 'out'):
        for kind in ('C', 'c'):
            data = emu.read_disk_file(name, kind)
            if data:
                return bytes(data[1])
    return b''


# ---- Laser Genius 1.04 ----

def lasergenius_boot(emu, args):
    emu.insert_disk(os.path.abspath(args.disk))
    emu.run_trdos(args.boot or 'L_G_ASS.', wait=6)
    emu.tap('1')                                   # the loader's menu is a BASIC INPUT
    emu.tap('enter')
    time.sleep(4)
    for key in ('y', 'n', 'd'):                    # tool-kit, hash extensions, from disk
        emu.tap(key)
        emu.tap('enter')
        time.sleep(3)
    time.sleep(6)


def lasergenius_finish(emu, args, expected):
    emu.write(args.address, [0xAA] * len(expected))
    emu.type('assem')
    emu.tap('enter')
    time.sleep(args.wait)
    _shot(emu, args, args.descriptor)
    return bytes(emu.read(args.address, len(expected)))


# ---- PROMETHEUS 48K ----

def prometheus_boot(emu, args):
    emu.post('/tape/load', {'path': os.path.abspath(args.disk)})
    emu.post('/basic/run', {'command': 'LOAD ""'})
    emu.post('/tape/play')
    for _ in range(240):
        time.sleep(0.5)
        if emu.get('/tape/info').get('state') in ('ended', 'stopped'):
            break
    time.sleep(2)
    emu.tap('enter')                               # the installer: run the assembler
    time.sleep(3)
    emu.post('/tape/eject')


def prometheus_finish(emu, args, expected):
    emu.write(args.address, [0xAA] * len(expected))
    emu.post('/keyboard/combo', {'keys': ['ss', 'a'], 'frames': 4})
    time.sleep(0.5)
    emu.tap('enter')
    time.sleep(args.wait)
    _shot(emu, args, args.descriptor)
    return bytes(emu.read(args.address, len(expected)))


# ---- Power Assembler 3.0 ----

def pasm_object_place(address):
    """Where PASM 3.0 puts the code it compiles (seen in unreal-ng): code from #8000 up in an image of the 48K memory
    (#8000-#BFFF in RAM page 3, #C000-#FFFF in page 0), code below #8000 from #2000 of page 4 (#E000 there) whatever
    its ORG; give the ORG as the address there"""
    if address >= 0xC000:
        return 0, address - 0xC000
    if address >= 0x8000:
        return 3, address - 0x8000
    return 4, 0x2000


def pasm_boot(emu, args):
    emu.insert_disk(os.path.abspath(args.disk))
    emu.run_trdos(args.boot or 'PASM3.0', wait=8)


def pasm_finish(emu, args, expected):
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
    _shot(emu, args, args.descriptor)
    data = emu.get(f'/memory/page/ram/{page}?offset={offset}&length={len(expected)}')['data']
    return bytes(data)


class Profile:
    def __init__(self, descriptor, boot, finish, model='PENTAGON', force=False, extra=None, disk_at_finish=False, tape=False,
                 ram_size=None, media=None):
        self.descriptor = descriptor          # the sync descriptor (sync::SyncDescriptor id)
        self.boot = boot
        self.finish = finish
        self.model = model                    # the machine it runs on
        self.ram_size = ram_size              # its RAM in KB when the model's default is not enough (ZAsm 3.2x+: 512)
        self.media = media                    # inject-oracle --template: what else the loaded template needs with --media
        self.force = force                    # no "changed" flag: the inject replaces the text by force
        self.extra = extra                    # (args, expected) -> text added to the source
        self.disk_at_finish = disk_at_finish  # it saves the code to its disk: the disk back in after a template
        self.tape = tape                      # loaded from a tape (the medium to eject is the tape)


PROFILES = {
    'alasm509': Profile('alasm-5.09', alasm_boot, alasm_finish),
    'alasm': Profile('alasm-5.09', alasm_boot, alasm_finish),            # any build: --boot, --assembler
    'xas': Profile('xas-7.447', xas_boot, xas_finish),
    'storm': Profile('storm-1.3', storm_boot, storm_finish),
    'tasm412': Profile('tasm-4.12', tasm_boot, tasm_finish),
    'zasm315': Profile('zasm-3.15', zasm_boot, zasm_finish, force=True, extra=zasm_extra, disk_at_finish=True),
    'zasm2': Profile('zasm-2.6', zasm2_boot, zasm2_finish, force=True, disk_at_finish=True),   # --assembler zasm-2.4 / -2.5 / -2.6
    'zasm30': Profile('zasm-3.0', zasm310_boot, zasm30_finish, force=True, disk_at_finish=True),                                            # a STUB.C on the disk
    'zasm310': Profile('zasm-3.10', zasm310_boot, zasmr_finish, force=True, extra=zasmr_extra, disk_at_finish=True),   # a STUB.A on the disk
    'zasmr': Profile('zasm-4.20', zasmr_boot, zasmr_finish, force=True, extra=zasmr_extra, disk_at_finish=True, ram_size=512, media=zasmr_media),   # --assembler zasm-3.2x ... (a STUB.a on the disk)
    'masm11': Profile('masm-1.1', masm_boot, masm_finish),
    'masm30': Profile('masm-3.0', masm_boot, masm_finish, force=True),     # --boot TSM
    'masm20': Profile('masm-2.0', masm_boot, masm_save_finish, force=True, disk_at_finish=True),  # --boot m2: no compiler in it, its Save checked
    'masmdemo': Profile('masm-1.0-demo', masm_boot, masm_save_finish, force=True, disk_at_finish=True),  # Spectrofon 15, --boot MASMdemo: the same
    'gens4': Profile('gens-4', gens_boot, gens_finish, model='48K', tape=True),
    'gens3m21': Profile('gens-3m21', gens3m21_boot, gens3_finish, model='48K', tape=True),   # DEVPAC3M.TAP
    'gens4b': Profile('gens-4b', gens4b_boot, gens_finish),                                 # gens4b.$c (MONSGENS.LZH)
    'gens43t': Profile('gens-4.3t', gens43t_boot, gens43t_finish, disk_at_finish=True),                              # GENS4TF.SCL
    'zeus1983': Profile('zeus-1983', zeus_boot, zeus_finish, model='48K', tape=True),
    'primus': Profile('primus-2.9', primus_boot, primus_finish, force=True, disk_at_finish=True),
    'zeusgg': Profile('zeus-gg', zeus_disk_boot, zeus_finish),          # --boot ZEUS
    'zeus7e': Profile('zeus-7e', zeus_disk_boot, zeus_finish),          # --boot ZEUSv7.E
    'zeus11': Profile('zeus-1.1', zeus11_boot, zeus_finish, tape=True),  # its hobeta file ZEUS.$C (no disk to eject)
    'lasergenius': Profile('lasergenius-1.04', lasergenius_boot, lasergenius_finish, force=True),
    'prometheus': Profile('prometheus-48', prometheus_boot, prometheus_finish, model='48K', force=True, tape=True),
    'pasm': Profile('pasm-3.0', pasm_boot, pasm_finish, force=True),
}
