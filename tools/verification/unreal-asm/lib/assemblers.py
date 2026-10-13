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
    emu.tap('enter')                               # Load
    time.sleep(3)
    emu.type('STUB')
    emu.tap('enter')                               # the editor opens on it
    time.sleep(3)
    emu.post('/keyboard/combo', {'keys': ['cs', 'ss'], 'frames': 4})   # COMMAND
    emu.idle()
    time.sleep(0.6)


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


# ---- GENS4 (48K) ----

def gens_boot(emu, args):
    code = [b for b in _aie().tape_blocks(open(args.disk, 'rb').read()) if len(b) == 10882][0][1:-1]   # the 'gens4' block
    emu.post('/reset')
    time.sleep(3)
    emu.write(26000, code)
    emu.post('/basic/run', {'command': 'RANDOMIZE USR 26000'})
    time.sleep(2)


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
    def __init__(self, descriptor, boot, finish, model='PENTAGON', force=False, extra=None, disk_at_finish=False, tape=False):
        self.descriptor = descriptor          # the sync descriptor (sync::SyncDescriptor id)
        self.boot = boot
        self.finish = finish
        self.model = model                    # the machine it runs on
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
    'masm11': Profile('masm-1.1', masm_boot, masm_finish),
    'gens4': Profile('gens-4', gens_boot, gens_finish, model='48K', tape=True),
    'zeus1983': Profile('zeus-1983', zeus_boot, zeus_finish, model='48K', tape=True),
    'primus': Profile('primus-2.9', primus_boot, primus_finish, force=True, disk_at_finish=True),
    'lasergenius': Profile('lasergenius-1.04', lasergenius_boot, lasergenius_finish, force=True),
    'prometheus': Profile('prometheus-48', prometheus_boot, prometheus_finish, model='48K', force=True, tape=True),
    'pasm': Profile('pasm-3.0', pasm_boot, pasm_finish, force=True),
}
