#!/usr/bin/env python3
"""media / load_software / rzx_playback: the slots, loading a tape from testdata, errors name the file, the files of
a disk, writing a file onto a TR-DOS disk, an ATM CP/M floppy read, a Profi CP/M .pro read, written and exported, a snapshot
for another machine switches only when asked, an .spg uploaded"""

import base64
import os
import re
import socket
import sys
import time
import urllib.parse

from mcplib import expect, main

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
TAPE = os.path.join(ROOT, 'testdata', 'contention', 'rak-timing-test', 'timing.tap')
SNAPSHOT = os.path.join(ROOT, 'testdata', 'loaders', 'sna', '7threality.sna')
ISDOS = os.path.join(ROOT, 'testdata', 'loaders', 'isdos', 'ISDOS50.FDI')
ATM_CPM = os.path.join(ROOT, 'testdata', 'machines', 'atm', 'cpm', 'prince.trd')
MB02 = os.path.join(ROOT, 'testdata', 'loaders', 'mb02', 'SYS308.MB2')
MDOS = os.path.join(ROOT, 'testdata', 'loaders', 'mdos', 'bootdisk.d80')
PROFI_PRO = os.path.join(ROOT, 'testdata', 'loaders', 'pro', 'ReadFile.pro')
Z80_128 = os.path.join(ROOT, 'testdata', 'loaders', 'z80', 'libspectrum', 'synth-128.z80')
SPG = os.path.join(ROOT, 'testdata', 'machines', 'tsconf', 'spg', 'sprites.spg')


def test_slots_listed(s):
    r = s.call('media', {'action': 'list'})
    expect('tape' in r.text and 'fdd.a' in r.text, 'tape and drives in the text', r.text)


def test_formats(s):
    r = s.call('media', {'action': 'formats'})
    expect('tap' in r.text.lower() and 'trd' in r.text.lower(), 'formats in the text', r.text[:300])


def test_load_tape(s):
    r = s.call('load_software', {'path': TAPE})
    expect(not r.is_error, 'loaded', r.text)
    r = s.call('media', {'action': 'list'})
    expect('timing.tap' in r.text, 'the tape in its slot', r.text)
    s.call('media', {'action': 'eject', 'slot': 'tape'}, ok=False)


def test_load_paused_leaves_the_program_unrun(s):
    s.call('control_execution', {'action': 'resume'}, ok=False)
    r = s.call('load_software', {'path': SNAPSHOT, 'paused': True})
    expect(not r.is_error, 'loaded', r.text)
    r = s.call('emulator_manage', {'action': 'status', 'response_format': 'json'})
    expect(r.structured.get('is_paused') is True, 'the machine is paused after the load', r.text[:400])
    s.call('control_execution', {'action': 'resume'}, ok=False)


def test_missing_file_is_an_error(s):
    r = s.call('load_software', {'path': '/nonexistent/x.tap'}, ok=False)
    expect(r.is_error and 'x.tap' in r.text, 'names the file', r.text)


def test_rzx_status(s):
    r = s.call('rzx_playback', {'action': 'status'})
    expect('RZX' in r.text, 'status in the text', r.text)


def test_recent_lists_and_clears(s):
    # The launched unreal-qt keeps its list in memory (mcplib sets UNREAL_RECENT_FILES=off)
    r = s.call('media', {'action': 'recent'})
    expect('No recent files' in r.text or 'recent file(s)' in r.text, 'the list in the text', r.text)
    cleared = s.call('media', {'action': 'recent_clear', 'kind': 'cd'})
    expect('cleared' in cleared.text.lower(), 'the text says cleared', cleared.text)
    cds = s.call('media', {'action': 'recent', 'kind': 'cd'})
    expect('No recent files' in cds.text, 'no cd after the clear', cds.text)
    bad = s.call('media', {'action': 'recent', 'kind': 'floppies'}, ok=False)
    expect(bad.is_error, 'an unknown kind is an error', bad.text)


def test_disk_files(s):
    r = s.call('media', {'action': 'insert', 'slot': 'A', 'path': ISDOS})
    expect(not r.is_error, 'inserted', r.text)
    r = s.call('media', {'action': 'volumes', 'slot': 'A'})
    expect('isdos: iS-DOS Classic' in r.text, 'the volume in the text', r.text)
    r = s.call('media', {'action': 'files', 'slot': 'A', 'path': 'SHELL', 'recursive': True})
    expect('SHELL/HELP/exit.hlp 663 bytes' in r.text, 'a file of the tree in the text', r.text[:600])
    r = s.call('media', {'action': 'read', 'slot': 'A', 'path': 'read_me.txt'})
    data = r.text.split('base64: ', 1)[1].strip() if 'base64: ' in r.text else ''
    expect(len(base64.b64decode(data)) == 1641, 'the bytes in the text', r.text[:200])
    r = s.call('media', {'action': 'read', 'slot': 'A', 'path': 'nothing.txt'}, ok=False)
    expect(r.is_error and 'nothing.txt' in r.text, 'a missing file names it', r.text)
    s.call('media', {'action': 'eject', 'slot': 'A', 'discard': True}, ok=False)



def test_disk_write(s):
    r = s.call('media', {'action': 'create', 'slot': 'A', 'format': 'trdos'})
    expect(not r.is_error, 'a blank TR-DOS disk', r.text)
    code = bytes([0xF3, 0xC9, 0x00, 0x01])
    r = s.call('media', {'action': 'write', 'slot': 'A', 'path': 'HELLO.C', 'start': '#8000',
                         'data': base64.b64encode(code).decode()})
    expect('wrote HELLO.C (4 bytes)' in r.text and 'unsaved:' in r.text, 'the write in the text', r.text)
    r = s.call('media', {'action': 'files', 'slot': 'A'})
    expect('HELLO.C 4 bytes, start 32768' in r.text, 'the file in the listing', r.text)
    r = s.call('media', {'action': 'read', 'slot': 'A', 'path': 'HELLO.C'})
    data = r.text.split('base64: ', 1)[1].strip() if 'base64: ' in r.text else ''
    expect(base64.b64decode(data) == code, 'the bytes read back', r.text)
    r = s.call('media', {'action': 'write', 'slot': 'A', 'path': 'HELLO.C'}, ok=False)
    expect(r.is_error and "'data'" in r.text, 'no bytes is refused', r.text)
    r = s.call('media', {'action': 'write', 'slot': 'A', 'path': 'P2.C', 'data': 'AA==', 'param2': '1'}, ok=False)
    expect(r.is_error and 'param 2' in r.text, 'a param 2 TR-DOS lacks is refused', r.text)
    r = s.call('media', {'action': 'write', 'slot': 'A', 'path': 'AT.C', 'data': 'AA==', 'attributes': 'R'},
               ok=False)
    expect(r.is_error and 'no file attributes' in r.text, 'attributes TR-DOS lacks are refused', r.text)
    r = s.call('media', {'action': 'files', 'slot': 'A'})
    expect('P2.C' not in r.text and 'AT.C' not in r.text, 'nothing written on a refusal', r.text)
    s.call('media', {'action': 'eject', 'slot': 'A', 'discard': True}, ok=False)


def test_disk_delete_and_rename_on_isdos(s):
    r = s.call('media', {'action': 'insert', 'slot': 'A', 'path': ISDOS})
    expect(not r.is_error, 'inserted', r.text)
    r = s.call('media', {'action': 'write', 'slot': 'A', 'path': 'SHELL/new.txt', 'data': base64.b64encode(b'isdos').decode()})
    expect('wrote SHELL/new.txt (5 bytes)' in r.text and 'unsaved:' in r.text, 'the iS-DOS write in the text', r.text)
    r = s.call('media', {'action': 'rename', 'slot': 'A', 'path': 'SHELL/new.txt', 'to': '/top.txt'})
    expect('renamed SHELL/new.txt to /top.txt' in r.text, 'the move in the text', r.text)
    r = s.call('media', {'action': 'read', 'slot': 'A', 'path': 'top.txt'})
    data = r.text.split('base64: ', 1)[1].strip() if 'base64: ' in r.text else ''
    expect(base64.b64decode(data) == b'isdos', 'the moved bytes read back', r.text)
    r = s.call('media', {'action': 'delete', 'slot': 'A', 'path': 'top.txt'})
    expect('deleted top.txt' in r.text, 'the delete in the text', r.text)
    r = s.call('media', {'action': 'read', 'slot': 'A', 'path': 'top.txt'}, ok=False)
    expect(r.is_error, 'the deleted file is gone', r.text)
    r = s.call('media', {'action': 'rename', 'slot': 'A', 'path': 'read_me.txt'}, ok=False)
    expect(r.is_error and "'to'" in r.text, 'rename without to is refused', r.text)
    s.call('media', {'action': 'eject', 'slot': 'A', 'discard': True}, ok=False)


def test_disk_catalogues_on_isdos(s):
    r = s.call('media', {'action': 'insert', 'slot': 'A', 'path': ISDOS})
    expect(not r.is_error, 'inserted', r.text)
    r = s.call('media', {'action': 'mkdir', 'slot': 'A', 'path': 'WORK/SUB', 'parents': True})
    expect('made WORK/SUB' in r.text, 'the catalogues made, in the text', r.text)
    r = s.call('media', {'action': 'write', 'slot': 'A', 'path': 'WORK/SUB/a.txt', 'data': base64.b64encode(b'deep').decode()})
    expect(not r.is_error, 'a file into the new catalogue', r.text)
    r = s.call('media', {'action': 'rename', 'slot': 'A', 'path': 'WORK/SUB', 'to': '/SUB'})
    expect('renamed WORK/SUB to /SUB' in r.text, 'the catalogue moved', r.text)
    r = s.call('media', {'action': 'files', 'slot': 'A', 'path': 'SUB'})
    expect('a.txt' in r.text, 'its file moved with it', r.text)
    r = s.call('media', {'action': 'rename', 'slot': 'A', 'path': 'SUB', 'to': '/SUB/IN'}, ok=False)
    expect(r.is_error and 'into itself' in r.text, 'not into itself', r.text)
    r = s.call('media', {'action': 'delete', 'slot': 'A', 'path': 'SUB'}, ok=False)
    expect(r.is_error and 'not empty' in r.text, 'a catalogue with a file stays', r.text)
    for path in ('SUB', 'WORK'):
        r = s.call('media', {'action': 'delete', 'slot': 'A', 'path': path, 'recursive': True})
        expect(f'deleted {path}' in r.text, f'{path} deleted with its contents', r.text)
    # A continuous catalogue of 4 blocks, as TASiS mkdir.com /c /s4
    r = s.call('media', {'action': 'mkdir', 'slot': 'A', 'path': 'CONT', 'continuous': True, 'blocks': 4})
    expect('made CONT' in r.text, 'the continuous catalogue made', r.text)
    r = s.call('media', {'action': 'files', 'slot': 'A'})
    line = next((l for l in r.text.splitlines() if 'CONT' in l), '')
    print('        ' + line.strip())
    expect(line != '' and 'attributes S' not in line, 'listed, not segmented', r.text)
    r = s.call('media', {'action': 'mkdir', 'slot': 'A', 'path': 'BIG', 'blocks': 17}, ok=False)
    expect(r.is_error and '1-16' in r.text, 'blocks past 16 refused', r.text)
    r = s.call('media', {'action': 'create', 'slot': 'B', 'format': 'trdos', 'discard': True})
    r = s.call('media', {'action': 'mkdir', 'slot': 'B', 'path': 'X', 'continuous': True}, ok=False)
    expect(r.is_error and 'iS-DOS' in r.text, 'continuous refused off iS-DOS', r.text)
    r = s.call('media', {'action': 'mkdir', 'slot': 'B', 'path': 'X'}, ok=False)
    expect(r.is_error and 'no directories' in r.text, 'TR-DOS has no directories', r.text)
    for slot in ('A', 'B'):
        s.call('media', {'action': 'eject', 'slot': slot, 'discard': True}, ok=False)


def test_disk_attributes_time_and_label(s):
    r = s.call('media', {'action': 'insert', 'slot': 'A', 'path': ISDOS})
    expect(not r.is_error, 'inserted', r.text)
    r = s.call('media', {'action': 'volumes', 'slot': 'A'})
    expect('RWHD' in r.text, 'the volume lists its letters', r.text)
    r = s.call('media', {'action': 'attrib', 'slot': 'A', 'path': 'read_me.txt', 'attributes': '+HD',
                         'time': '1999-12-31 23:59:58'})
    expect('changed read_me.txt: attributes' in r.text and 'HD' in r.text and '946684798' in r.text,
           'attributes and time set, in the text', r.text)
    r = s.call('media', {'action': 'attrib', 'slot': 'A', 'path': 'read_me.txt', 'attributes': '+Q'}, ok=False)
    expect(r.is_error and 'RWHD' in r.text, 'a letter iS-DOS lacks is refused with the letters', r.text)
    r = s.call('media', {'action': 'label', 'slot': 'A', 'label': 'MY_DISK'})
    expect("labelled 'MY_DISK'" in r.text, 'the device named', r.text)
    r = s.call('media', {'action': 'create', 'slot': 'B', 'format': 'trdos', 'discard': True})
    r = s.call('media', {'action': 'label', 'slot': 'B', 'label': 'Games 1'})
    expect("labelled 'Games 1'" in r.text, 'the TR-DOS title set', r.text)
    r = s.call('media', {'action': 'write', 'slot': 'B', 'path': 'A.C', 'data': 'AA=='})
    r = s.call('media', {'action': 'attrib', 'slot': 'B', 'path': 'A.C', 'attributes': '+R'}, ok=False)
    expect(r.is_error and 'no file attributes' in r.text, 'TR-DOS has no attributes', r.text)
    for slot in ('A', 'B'):
        s.call('media', {'action': 'eject', 'slot': slot, 'discard': True}, ok=False)


def test_disk_atm_cpm(s):
    r = s.call('media', {'action': 'insert', 'slot': 'A', 'path': ATM_CPM})
    expect(not r.is_error, 'inserted', r.text)
    r = s.call('media', {'action': 'volumes', 'slot': 'A'})
    expect('cpm: ATM CP/M' in r.text and 'read only' not in r.text and 'attributes RSA' in r.text,
           'the CP/M volume, writable, its letters in the text', r.text)
    r = s.call('media', {'action': 'files', 'slot': 'A', 'recursive': True})
    expect('PRINCE.DOC 11904 bytes' in r.text and 'USER15/CONFIG.DAT' in r.text, 'the files and user 15 in the text',
           r.text[:800])
    r = s.call('media', {'action': 'read', 'slot': 'A', 'path': 'PRINCE.DOC'})
    data = r.text.split('base64: ', 1)[1].strip() if 'base64: ' in r.text else ''
    expect(base64.b64decode(data)[7:23] == b'Prince of Persia', 'the bytes in the text', r.text[:200])
    r = s.call('media', {'action': 'write', 'slot': 'A', 'path': 'USER3/NEW.TXT', 'data': 'QUJD'})
    expect(not r.is_error, 'a file written to user 3', r.text)
    r = s.call('media', {'action': 'read', 'slot': 'A', 'path': 'USER3/NEW.TXT'})
    data = r.text.split('base64: ', 1)[1].strip() if 'base64: ' in r.text else ''
    expect(base64.b64decode(data)[:4] == b'ABC\x1a', 'read back, the record padded with #1A', r.text[:200])
    r = s.call('media', {'action': 'delete', 'slot': 'A', 'path': 'USER15/CONFIG.DAT'}, ok=False)
    expect(r.is_error and 'read only' in r.text, 'a read-only file is not deleted', r.text)
    s.call('media', {'action': 'eject', 'slot': 'A', 'discard': True}, ok=False)


def test_disk_mb02(s):
    r = s.call('media', {'action': 'insert', 'slot': 'A', 'path': MB02})
    expect(not r.is_error, 'inserted', r.text)
    r = s.call('media', {'action': 'volumes', 'slot': 'A'})
    expect('mb02: MB-02 BS-DOS' in r.text and 'System 308' in r.text and 'read only' not in r.text,
           'the MB-02 volume, its name, writable, in the text', r.text)
    r = s.call('media', {'action': 'files', 'slot': 'A', 'recursive': True})
    expect('BSdos308 23450 bytes' in r.text and 'Small demo/bbicons' in r.text and 'tma~10' in r.text,
           'the files, a directory and a second name in the text', r.text[:800])
    r = s.call('media', {'action': 'read', 'slot': 'A', 'path': 'Small demo/cdebusy'})
    data = r.text.split('base64: ', 1)[1].strip() if 'base64: ' in r.text else ''
    expect(len(base64.b64decode(data)) == 768, 'the bytes in the text', r.text[:200])
    r = s.call('media', {'action': 'mkdir', 'slot': 'A', 'path': 'Small demo/New dir'})
    expect(not r.is_error, 'a directory made', r.text)
    r = s.call('media', {'action': 'write', 'slot': 'A', 'path': 'Small demo/New dir/NEW', 'data': 'QUJD'})
    expect(not r.is_error, 'a file written into it', r.text)
    r = s.call('media', {'action': 'read', 'slot': 'A', 'path': 'Small demo/New dir/NEW'})
    data = r.text.split('base64: ', 1)[1].strip() if 'base64: ' in r.text else ''
    expect(base64.b64decode(data) == b'ABC', 'read back', r.text[:200])
    r = s.call('media', {'action': 'write', 'slot': 'A', 'path': 'Small demo/New dir/RUN', 'data': 'QUJD',
                         'type': 'program', 'start': '10'})
    expect(not r.is_error, 'a program written', r.text)
    r = s.call('media', {'action': 'write', 'slot': 'A', 'path': 'Small demo/New dir/ARR', 'data': 'QUJD',
                         'type': 'numbers'}, ok=False)
    expect(r.is_error and 'a program (0), CODE (3)' in r.text, 'an array type is refused with the types', r.text)
    r = s.call('media', {'action': 'write', 'slot': 'A', 'path': 'Small demo/New dir/BAD', 'data': 'QUJD',
                         'type': 'basic'}, ok=False)
    expect(r.is_error and "option 'type'" in r.text, 'an unknown type name is refused', r.text)
    s.call('media', {'action': 'eject', 'slot': 'A', 'discard': True}, ok=False)


def test_disk_mdos(s):
    r = s.call('media', {'action': 'insert', 'slot': 'A', 'path': MDOS})
    expect(not r.is_error, 'inserted', r.text)
    r = s.call('media', {'action': 'volumes', 'slot': 'A'})
    expect('mdos: MDOS' in r.text and 'bootdisk' in r.text and 'read only' not in r.text and
           'attributes HSPARWED' in r.text, 'the MDOS volume, its name, writable, its letters in the text', r.text)
    r = s.call('media', {'action': 'files', 'slot': 'A'})
    expect('run.P 94 bytes' in r.text and 'H...RWED' in r.text, 'the files and their attributes in the text',
           r.text[:800])
    r = s.call('media', {'action': 'read', 'slot': 'A', 'path': 'bootconf.B'})
    data = r.text.split('base64: ', 1)[1].strip() if 'base64: ' in r.text else ''
    expect(len(base64.b64decode(data)) == 512, 'the bytes in the text', r.text[:200])
    r = s.call('media', {'action': 'write', 'slot': 'A', 'path': 'new.B', 'data': 'QUJD'})
    expect(not r.is_error, 'a file written', r.text)
    r = s.call('media', {'action': 'read', 'slot': 'A', 'path': 'new.B'})
    data = r.text.split('base64: ', 1)[1].strip() if 'base64: ' in r.text else ''
    expect(base64.b64decode(data) == b'ABC', 'read back', r.text[:200])
    r = s.call('media', {'action': 'write', 'slot': 'A', 'path': 'vars.P', 'data': 'QUJD', 'param2': '2',
                         'attributes': 'RE'})
    expect(not r.is_error, 'a program written with param 2 and attributes', r.text)
    r = s.call('media', {'action': 'files', 'slot': 'A'})
    expect('vars.P 3 bytes' in r.text and '....R.E.' in r.text, 'the attributes set at write in the listing',
           r.text[:800])
    r = s.call('media', {'action': 'write', 'slot': 'A', 'path': 'typed.C', 'data': 'QUJD', 'type': 'code'},
               ok=False)
    expect(r.is_error and 'type' in r.text, 'MDOS takes the type from the name, a type option is refused', r.text)
    s.call('media', {'action': 'eject', 'slot': 'A', 'discard': True}, ok=False)



def test_disk_profi_cpm_pro(s):
    r = s.call('media', {'action': 'insert', 'slot': 'A', 'path': PROFI_PRO})
    expect(not r.is_error, 'inserted', r.text)
    r = s.call('media', {'action': 'volumes', 'slot': 'A'})
    expect('cpm: Profi CP/M' in r.text and 'read only' not in r.text, 'the Profi CP/M volume, writable, in the text',
           r.text)
    r = s.call('media', {'action': 'files', 'slot': 'A', 'recursive': True})
    expect('BOOTK.COM 896 bytes' in r.text and 'USER15/DSKKE9A.DRV' in r.text, 'the files and user 15 in the text',
           r.text[:800])
    r = s.call('media', {'action': 'read', 'slot': 'A', 'path': 'BOOTK.COM'})
    data = r.text.split('base64: ', 1)[1].strip() if 'base64: ' in r.text else ''
    with open(PROFI_PRO, 'rb') as f:
        image = f.read()
    expect(base64.b64decode(data) == image[4096:4096 + 896], 'BOOTK.COM is the boot sector R = 9', r.text[:200])
    r = s.call('media', {'action': 'write', 'slot': 'A', 'path': 'NEW.TXT', 'data': 'QUJD'})
    expect(not r.is_error, 'a file written', r.text)
    r = s.call('media', {'action': 'read', 'slot': 'A', 'path': 'NEW.TXT'})
    data = r.text.split('base64: ', 1)[1].strip() if 'base64: ' in r.text else ''
    expect(base64.b64decode(data)[:4] == b'ABC\x1a', 'read back, the record padded with #1A', r.text[:200])
    out = os.path.join(ROOT, 'scratch', 'mcp-media', 'profi-export.pro')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    r = s.call('media', {'action': 'export', 'slot': 'A', 'path': out})
    expect(not r.is_error and os.path.exists(out), 'exported as .pro', r.text)
    with open(out, 'rb') as f:
        exported = f.read()
    expect(len(exported) == len(image) and exported[4096:5120] == image[4096:5120],
           'the dump keeps its size and the boot sector in the fifth place of track 0', str(len(exported)))
    s.call('media', {'action': 'eject', 'slot': 'A', 'discard': True}, ok=False)


def _stop48k_tzx():
    """A synthetic TZX written to the repository's scratch/: block 0 pure data ($14, no pilot, 1 s pause), block 1
    TZX $2A (stop the tape if in 48K mode), block 2 a standard data block ($10)"""
    data = bytes([0xFF, 1, 2, 3, 0xFF ^ 1 ^ 2 ^ 3])
    tzx = bytearray(b'ZXTape!\x1a\x01\x14')
    tzx += bytes([0x14]) + (855).to_bytes(2, 'little') + (1710).to_bytes(2, 'little') + bytes([8])
    tzx += (1000).to_bytes(2, 'little') + (8).to_bytes(3, 'little') + bytes(range(8))
    tzx += bytes([0x2A]) + (0).to_bytes(4, 'little')
    tzx += bytes([0x10]) + (1000).to_bytes(2, 'little') + len(data).to_bytes(2, 'little') + data
    folder = os.path.join(ROOT, 'scratch', 'mcp-verification')
    os.makedirs(folder, exist_ok=True)
    path = os.path.join(folder, 'stop48k.tzx')
    with open(path, 'wb') as out:
        out.write(tzx)
    return path


def _tape_info(s):
    """GET /tape/info as a dict (invoke_api's text is the HTTP line only: the state is in structuredContent)"""
    r = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/emulator/{id}/tape/info'})
    return (r.structured or {}).get('body', {})


def _play_through_the_stop48k_block(s, model):
    """Loads the synthetic tape on a fresh `model` and plays block 0 with no loader listening: its data runs out in
    two frames, and in the pause after it the watchdog moves the tape on, over the $2A. Returns the play answers'
    texts (the first play, a play while it plays) and GET /tape/info after 10 frames"""
    s.fresh_machine(model)
    r = s.call('load_software', {'path': _stop48k_tzx(), 'play': True})
    expect('Tape playback started' in r.text and 'Tape: playing, block 0 of 3' in r.text,
           'the play answer in the load text: started, playing, block 0', r.text)
    first = r.text
    again = s.call('actions', {'action': 'run', 'id': 'media.tape', 'params': {'op': 'play'}})
    expect('already playing' in again.text and 'block 0 of 3' in again.text,
           'a play while it plays leaves the block in flight', again.text)
    s.call('control_execution', {'action': 'run_frames', 'frames': 10})
    return first, again.text, _tape_info(s)


def test_tape_play_answer_on_48k_stops_at_the_2a_block(s):
    _, _, info = _play_through_the_stop48k_block(s, '48K')
    brief = {k: info.get(k) for k in ('state', 'reason', 'position')}
    expect(info.get('state') == 'paused', 'paused after the $2A', brief)
    expect(info.get('reason') == 'stop_48k', 'the reason: the TZX $2A block in 48K mode', brief)
    expect(info.get('position', {}).get('block') == 2, 'stopped before block 2, not on the $2A', brief)
    expect(info['blocks'][1].get('control') == 'stop_48k', 'the catalog names the $2A entry', info['blocks'][1])

    # Resumable: play goes on with the block after the $2A, and the text says where the tape is
    r = s.call('actions', {'action': 'run', 'id': 'media.tape', 'params': {'op': 'play'}})
    expect('Tape playback started' in r.text and 'Tape: playing, block 2 of 3 (data' in r.text,
           'play after the stop: block 2 plays', r.text)
    expect((r.structured or {}).get('result', {}).get('tape', {}).get('mode_48k') is True,
           'the machine is in 48K mode', r.structured)
    s.call('control_execution', {'action': 'run_frames', 'frames': 5})
    r = s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/emulator/{id}/tape/play'})
    expect('already playing' in r.text and 'block 2 of 3' in r.text, 'the tape moved into block 2', r.text)
    expect('pulse 0,' not in r.text, 'pulses of block 2 went by', r.text)

    # The cursor on the $2A itself: play passes it and says so (a play request is not stopped by it)
    s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/emulator/{id}/tape/seek', 'body': {'block': 1}})
    r = s.call('actions', {'action': 'run', 'id': 'media.tape', 'params': {'op': 'play'}})
    expect('passed block 1 (no signal)' in r.text and 'block 2 of 3' in r.text,
           'play on the $2A block starts block 2', r.text)

    # A play with no loader listening: the watchdog pauses the tape again, and the state says who did
    s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/emulator/{id}/tape/seek', 'body': {'block': 2}})
    s.call('actions', {'action': 'run', 'id': 'media.tape', 'params': {'op': 'play'}})
    s.call('control_execution', {'action': 'run_frames', 'frames': 400})  # the watchdog rewinds the pilot: paused
    info = _tape_info(s)
    expect(info.get('state') == 'paused' and info.get('reason') == 'loader_quiet',
           'paused again by the loader watchdog, and it says so', {k: info.get(k) for k in ('state', 'reason')})
    s.fresh_machine()


def test_tape_play_answer_on_128k_passes_the_2a_block(s):
    _, _, info = _play_through_the_stop48k_block(s, '128K')
    brief = {k: info.get(k) for k in ('state', 'reason', 'position')}
    expect(info.get('reason') == 'parked', 'a 128K passes the $2A: parked by the watchdog (no loader listening)', brief)
    expect(info.get('position', {}).get('block') == 2, 'parked before block 2, not on the $2A', brief)
    r = s.call('actions', {'action': 'run', 'id': 'media.tape', 'params': {'op': 'play'}})
    expect('Tape playback started' in r.text and 'Tape: playing, block 2 of 3 (data' in r.text,
           'play: block 2 plays', r.text)
    expect((r.structured or {}).get('result', {}).get('tape', {}).get('mode_48k') is False,
           'the machine is in 128K mode', r.structured)
    s.fresh_machine()


def _cli(s, emu, command):
    """One command on the CLI plane (its port is the MCP port - 100), the selected instance `emu`. The CLI is another
    client: MCP lets run control go first (a take replaces its implicit session, the release ends it) and takes it
    back after, over the CLI's implicit session"""
    s.call('control_execution', {'action': 'take_control'}, ok=False)
    s.call('control_execution', {'action': 'release_control'}, ok=False)
    try:
        return _cli_command(s, emu, command)
    finally:
        s.call('control_execution', {'action': 'take_control'}, ok=False)


def _cli_command(s, emu, command):
    port = urllib.parse.urlparse(s.url).port - 100
    with socket.create_connection(('127.0.0.1', port), timeout=30) as sock:
        def read_until_prompt():
            data = b''
            deadline = time.time() + 30
            while not re.search(rb'\n> (\x1b\[K)?$', data) and time.time() < deadline:
                chunk = sock.recv(65536)
                if not chunk:
                    break
                data += chunk
            return data.decode(errors='replace')
        read_until_prompt()
        sock.sendall(f'select {emu}\n'.encode())
        read_until_prompt()
        sock.sendall((command + '\n').encode())
        return read_until_prompt().replace('\r', '')


def test_tape_play_answer_on_cli_lua_python(s):
    """The same report on the other planes: the CLI's two lines, Lua's `ok, report`, Python's dict"""
    s.fresh_machine('48K')
    s.call('load_software', {'path': _stop48k_tzx()})
    emu = s.call('emulator_manage', {'action': 'list'}).structured['emulators'][0]['id']
    seek = {'method': 'POST', 'path': '/api/v1/emulator/{id}/tape/seek', 'body': {'block': 1}}

    s.call('invoke_api', seek)
    text = _cli(s, emu, 'tape play')
    expect('Tape playback started, passed block 1 (no signal)' in text and 'Tape: playing, block 2 of 3 (data' in text,
           'the CLI answers what the call did and the deck after it', text)
    text = _cli(s, emu, 'tape play')
    expect('already playing' in text, 'the CLI: a second play leaves the tape as it is', text)

    s.call('invoke_api', seek)
    code = ('local ok, r = tape_play() '
            'print("lua-play", ok, r.result, r.playing, r.state, r.position.block, r.block.kind, #r.skipped_blocks, '
            'r.skipped_blocks[1], r.mode_48k)')
    r = s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/lua/exec', 'body': {'code': code}}, ok=False)
    output = ((r.structured or {}).get('body') or {}).get('output', '') + r.text
    expect(re.search(r'lua-play\s+true\s+started\s+true\s+playing\s+2(\.0)?\s+data\s+1\s+1(\.0)?\s+true', output),
           'Lua: ok and the report', output)

    s.call('invoke_api', seek)
    code = ('import unreal_emulator as ue\n'
            'emu = ue.emu_get_selected()\n'
            'r = emu.tape_play(report=True)\n'
            'print("py-play", r["result"], r["playing"], r["position"]["block"], r["block"]["kind"], '
            'r["skipped_blocks"], r["mode_48k"])\n'
            'print("py-again", emu.tape_play(), emu.tape_play(report=True)["result"])\n')
    r = s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/python/exec', 'body': {'code': code}}, ok=False)
    if 'Python automation is not available' not in r.text:
        output = ((r.structured or {}).get('body') or {}).get('output', '') + r.text
        expect('py-play started True 2 data [1] True' in output, 'Python: the report as a dict', output)
        expect('py-again True already_playing' in output, 'Python: the plain call still answers a bool', output)
    s.fresh_machine()


def test_zz_snapshot_for_another_machine_switches_only_when_asked(s):
    """A 128K .z80 on a 48K: refused with the machine it needs, switched with switch_model (the WebAPI rule)"""
    s.fresh_machine('48K')
    try:
        r = s.call('load_software', {'path': Z80_128, 'switch_model': False}, ok=False)
        expect(r.is_error and '128' in r.text, 'refused, the machine it needs named', r.text[:400])
        r = s.call('load_software', {'path': Z80_128, 'switch_model': True})
        expect(not r.is_error, 'loaded after the switch', r.text[:400])
        status = s.call('emulator_manage', {'action': 'status', 'response_format': 'json'})
        expect('128' in status.text, 'the machine is a 128K now', status.text[:400])
    finally:
        s.fresh_machine()


def test_zz_every_snapshot_format_of_the_core_uploads(s):
    """An .spg (the core opens it on TS-Conf) goes through the upload: the WebAPI takes the core's format lists"""
    s.fresh_machine('TSCONF')
    try:
        r = s.call('load_software', {'path': SPG})
        expect(not r.is_error and 'extension' not in r.text.lower(), 'the .spg loaded', r.text[:400])
    finally:
        s.fresh_machine()


if __name__ == '__main__':
    sys.exit(main(globals()))
