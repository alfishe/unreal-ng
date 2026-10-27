#!/usr/bin/env python3
"""media / load_software / rzx_playback: the slots, loading a tape from testdata, errors name the file, the files of
a disk, writing a file onto a TR-DOS disk, an ATM CP/M floppy read"""

import base64
import os
import sys

from mcplib import expect, main

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
TAPE = os.path.join(ROOT, 'testdata', 'contention', 'rak-timing-test', 'timing.tap')
SNAPSHOT = os.path.join(ROOT, 'testdata', 'loaders', 'sna', '7threality.sna')
ISDOS = os.path.join(ROOT, 'testdata', 'loaders', 'isdos', 'ISDOS50.FDI')
ATM_CPM = os.path.join(ROOT, 'testdata', 'machines', 'atm', 'cpm', 'prince.trd')
MB02 = os.path.join(ROOT, 'testdata', 'loaders', 'mb02', 'SYS308.MB2')
MDOS = os.path.join(ROOT, 'testdata', 'loaders', 'mdos', 'bootdisk.d80')


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
    r = s.call('media', {'action': 'create', 'slot': 'B', 'format': 'trdos', 'discard': True})
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
    expect('cpm: ATM CP/M' in r.text and 'read only' in r.text, 'the CP/M volume, read only, in the text', r.text)
    r = s.call('media', {'action': 'files', 'slot': 'A', 'recursive': True})
    expect('PRINCE.DOC 11904 bytes' in r.text and 'USER15/CONFIG.DAT' in r.text, 'the files and user 15 in the text',
           r.text[:800])
    r = s.call('media', {'action': 'read', 'slot': 'A', 'path': 'PRINCE.DOC'})
    data = r.text.split('base64: ', 1)[1].strip() if 'base64: ' in r.text else ''
    expect(base64.b64decode(data)[7:23] == b'Prince of Persia', 'the bytes in the text', r.text[:200])
    r = s.call('media', {'action': 'write', 'slot': 'A', 'path': 'NEW.TXT', 'data': 'AA=='}, ok=False)
    expect(r.is_error and 'read only' in r.text, 'a write is refused', r.text)
    s.call('media', {'action': 'eject', 'slot': 'A', 'discard': True}, ok=False)


def test_disk_mb02(s):
    r = s.call('media', {'action': 'insert', 'slot': 'A', 'path': MB02})
    expect(not r.is_error, 'inserted', r.text)
    r = s.call('media', {'action': 'volumes', 'slot': 'A'})
    expect('mb02: MB-02 BS-DOS' in r.text and 'System 308' in r.text and 'read only' in r.text,
           'the MB-02 volume, its name, read only, in the text', r.text)
    r = s.call('media', {'action': 'files', 'slot': 'A', 'recursive': True})
    expect('BSdos308 23450 bytes' in r.text and 'Small demo/bbicons' in r.text and 'tma~10' in r.text,
           'the files, a directory and a second name in the text', r.text[:800])
    r = s.call('media', {'action': 'read', 'slot': 'A', 'path': 'Small demo/cdebusy'})
    data = r.text.split('base64: ', 1)[1].strip() if 'base64: ' in r.text else ''
    expect(len(base64.b64decode(data)) == 768, 'the bytes in the text', r.text[:200])
    r = s.call('media', {'action': 'write', 'slot': 'A', 'path': 'NEW', 'data': 'AA=='}, ok=False)
    expect(r.is_error and 'read only' in r.text, 'a write is refused', r.text)
    s.call('media', {'action': 'eject', 'slot': 'A', 'discard': True}, ok=False)


def test_disk_mdos(s):
    r = s.call('media', {'action': 'insert', 'slot': 'A', 'path': MDOS})
    expect(not r.is_error, 'inserted', r.text)
    r = s.call('media', {'action': 'volumes', 'slot': 'A'})
    expect('mdos: MDOS' in r.text and 'bootdisk' in r.text and 'read only' in r.text,
           'the MDOS volume, its name, read only, in the text', r.text)
    r = s.call('media', {'action': 'files', 'slot': 'A'})
    expect('run.P 94 bytes' in r.text and 'H...RWED' in r.text, 'the files and their attributes in the text',
           r.text[:800])
    r = s.call('media', {'action': 'read', 'slot': 'A', 'path': 'bootconf.B'})
    data = r.text.split('base64: ', 1)[1].strip() if 'base64: ' in r.text else ''
    expect(len(base64.b64decode(data)) == 512, 'the bytes in the text', r.text[:200])
    r = s.call('media', {'action': 'write', 'slot': 'A', 'path': 'new.B', 'data': 'AA=='}, ok=False)
    expect(r.is_error and 'read only' in r.text, 'a write is refused', r.text)
    s.call('media', {'action': 'eject', 'slot': 'A', 'discard': True}, ok=False)


if __name__ == '__main__':
    sys.exit(main(globals()))
