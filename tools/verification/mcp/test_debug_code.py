#!/usr/bin/env python3
"""debug_code: assemble writes code that disassemble lists back, find_bytes finds it, trace lists the control flow.
The disassembler's quality against z88dk / sjasmplus is tools/verification/disasm"""

import re
import sys

from mcplib import expect, main


def test_assemble_then_disassemble(s):
    r = s.call('debug_code', {'action': 'assemble', 'code': 'ld hl,#4000\nld (hl),a\nret', 'address': '0xA000'})
    expect(r.structured['bytes'] == [0x21, 0x00, 0x40, 0x77, 0xC9], 'the bytes', r.structured)
    r = s.call('debug_code', {'action': 'disassemble', 'address': '0xA000', 'count': 3})
    expect(re.search(r'A000\s+21 00 40\s+ld hl,', r.text, re.I), 'ld hl listed', r.text)
    expect(re.search(r'A004\s+C9\s+ret', r.text, re.I), 'ret listed', r.text)


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


if __name__ == '__main__':
    sys.exit(main(globals()))
