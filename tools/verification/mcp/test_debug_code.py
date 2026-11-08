#!/usr/bin/env python3
"""debug_code: assemble writes code that disassemble lists back, find_bytes finds it, trace lists the control flow;
the memory markup's user layer marked, shown in the text, counted, cleared, and save_profile's refusals (a save
itself would write into the user's profile directory). The disassembler's quality against z88dk / sjasmplus is
tools/verification/disasm"""

import re
import sys

from mcplib import expect, main


def test_assemble_then_disassemble(s):
    r = s.call('debug_code', {'action': 'assemble', 'code': 'ld hl,#4000\nld (hl),a\nret', 'address': '0xA000'})
    expect(r.structured['bytes'] == [0x21, 0x00, 0x40, 0x77, 0xC9], 'the bytes', r.structured)
    r = s.call('debug_code', {'action': 'disassemble', 'address': '0xA000', 'count': 3})
    expect(re.search(r'A000\s+21 00 40\s+ld hl,', r.text, re.I), 'ld hl listed', r.text)
    expect(re.search(r'A004\s+C9\s+ret', r.text, re.I), 'ret listed', r.text)


def test_disassemble_before_and_snap(s):
    # NOPs below, so every backward chain converges on #A000
    s.call('memory_access', {'action': 'write', 'address': '0x9FE0', 'hex': '00 ' * 32})
    s.call('debug_code', {'action': 'assemble', 'code': 'ld hl,#4000\nld (hl),a\nret', 'address': '0xA000'})
    r = s.call('debug_code', {'action': 'disassemble', 'address': '0xA004', 'count': 1, 'before': 2})
    rows = [line['address'] for line in r.structured['instructions']]
    expect(rows == [0xA000, 0xA003, 0xA004], 'two rows above, then the address', rows)
    expect(re.search(r'A000\s+21 00 40\s+ld hl,', r.text, re.I), 'the rows above in the text', r.text)
    r = s.call('debug_code', {'action': 'disassemble', 'address': '0xA001', 'count': 1, 'snap': True})
    expect(r.structured['address'] == 0xA000, 'snapped to the instruction holding #A001', r.structured)


def test_markup_data_is_db_rows(s):
    s.call('debug_code', {'action': 'markup_clear'})
    s.call('memory_access', {'action': 'write', 'address': '0xA100', 'hex': 'C9 ' * 16})
    s.call('debug_code', {'action': 'mark', 'range': 'A100-A10B', 'kind': 'data'})
    r = s.call('debug_code', {'action': 'disassemble', 'address': '0xA100', 'count': 3})
    lines = r.structured['instructions']
    expect([(line['address'], line.get('kind')) for line in lines] == [(0xA100, 'data'), (0xA108, 'data'), (0xA10C, None)],
           'db rows of 8 and the rest, then code', lines)
    expect('db #C9,#C9' in r.text, 'the db row in the text', r.text)
    s.call('debug_code', {'action': 'markup_clear'})


def test_find_bytes(s):
    s.call('memory_access', {'action': 'write', 'address': '0xA100', 'hex': 'ED B0 C9 DE AD'})
    r = s.call('debug_code', {'action': 'find_bytes', 'pattern': 'C9 DE AD'})
    expect('0xA102' in r.text, 'the match address in the text', r.text)


def test_trace_lists_events(s):
    r = s.call('debug_code', {'action': 'trace', 'frames': 2, 'limit': 16})
    events = [line for line in r.text.splitlines() if re.match(r'0x[0-9A-F]{4} (jp|jr|call|rst|ret|reti|djnz) -> ', line)]
    expect(events, 'one line per control-flow event', r.text[:400])


def test_bad_address_is_an_error(s):
    r = s.call('debug_code', {'action': 'disassemble', 'address': 'zz'}, ok=False)
    expect(r.is_error, 'refused', r.text)


def test_markup_mark_show_unmark(s):
    s.call('debug_code', {'action': 'markup_clear'})
    r = s.call('debug_code', {'action': 'mark', 'range': '8000-80FF', 'kind': 'sprite', 'title': 'hero'})
    expect('Marked $8000-$80FF as sprite' in r.text, 'the mark in the text', r.text)
    s.call('debug_code', {'action': 'markup_comment', 'address': '0x8010', 'text': 'frame 2'})
    r = s.call('debug_code', {'action': 'markup', 'range': '8000-81FF'})
    expect('$8000-$80FF sprite [user] hero' in r.text, 'the span in the text an agent reads', r.text)
    expect('; $8010 frame 2' in r.text, 'the comment in the text', r.text)
    r = s.call('debug_code', {'action': 'markup_layers'})
    expect(re.search(r'user: .*1 claim', r.text), 'the user layer counted', r.text)
    s.call('debug_code', {'action': 'unmark', 'from': '0x8000', 'to': '0x80FF'})
    r = s.call('debug_code', {'action': 'markup', 'range': '8000-80FF', 'layer': 'user'})
    expect(r.structured['count'] == 0, 'unmarked', r.text)
    s.call('debug_code', {'action': 'markup_clear'})


def test_markup_refusals(s):
    r = s.call('debug_code', {'action': 'mark', 'range': '8000-80FF', 'kind': 'graphics'}, ok=False)
    expect(r.is_error and 'kind' in r.text, 'an unknown kind refused, naming the option', r.text)
    r = s.call('debug_code', {'action': 'save_profile'}, ok=False)
    expect(r.is_error and 'nothing to save' in r.text, 'an empty user layer saves nothing', r.text)


if __name__ == '__main__':
    sys.exit(main(globals()))
