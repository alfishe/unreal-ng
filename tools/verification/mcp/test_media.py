#!/usr/bin/env python3
"""media / load_software / rzx_playback: the slots, loading a tape from testdata, errors name the file, the files of
a disk, writing a file onto a TR-DOS disk"""

import base64
import os
import sys

from mcplib import expect, main

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
TAPE = os.path.join(ROOT, 'testdata', 'contention', 'rak-timing-test', 'timing.tap')
SNAPSHOT = os.path.join(ROOT, 'testdata', 'loaders', 'sna', '7threality.sna')
ISDOS = os.path.join(ROOT, 'testdata', 'loaders', 'isdos', 'ISDOS50.FDI')


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

if __name__ == '__main__':
    sys.exit(main(globals()))
