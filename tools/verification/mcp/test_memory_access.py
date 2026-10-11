#!/usr/bin/env python3
"""memory_access: the Z80 view and device regions - write / read round trip, the bytes in the text, address as the
alias of offset, bad requests"""

import re
import sys

from mcplib import expect, main


def test_write_then_read_round_trip(s):
    s.call('memory_access', {'action': 'write', 'offset': '0x9000', 'hex': '01 02 03 A8'})
    r = s.call('memory_access', {'action': 'read', 'offset': '0x9000', 'length': 4})
    expect('0x9000: 01 02 03 A8' in r.text, 'the bytes in the text', r.text)


def test_address_is_an_alias_of_offset(s):
    s.call('memory_access', {'action': 'write', 'address': '#9100', 'hex': '5A'})
    r = s.call('memory_access', {'action': 'read', 'address': '0x9100', 'length': 1})
    expect(r.structured['address'].upper() == '0X9100', 'read where asked, not at 0', r.structured)
    expect('0x9100: 5A' in r.text, 'the byte in the text', r.text)


def test_offset_and_address_together_is_an_error(s):
    r = s.call('memory_access', {'action': 'read', 'offset': '0x4000', 'address': '0x8000'}, ok=False)
    expect(r.is_error, 'conflicting addresses are refused', r.text)


def test_long_read_is_cut_with_a_note(s):
    r = s.call('memory_access', {'action': 'read', 'offset': '0', 'length': 8192})
    lines = [line for line in r.text.splitlines() if re.match(r'0x[0-9A-F]{4}:', line)]
    expect(len(lines) == 256, '256 lines in the text', len(lines))
    expect('more line(s)' in r.text, 'says how to get the rest', r.text[-200:])


def test_regions(s):
    r = s.call('memory_access', {'action': 'regions'})
    expect(r.text.startswith('Memories:') or 'No device memories' in r.text, 'regions or none', r.text)
    regions = r.structured.get('regions', [])
    if regions:
        name = regions[0]['name']
        read = s.call('memory_access', {'action': 'read', 'space': name, 'offset': '0', 'length': 16})
        expect(re.search(r'0x[0-9A-F]{4,6}:( [0-9A-F]{2}){16}', read.text), f'{name}: bytes in the text', read.text)


def test_bad_write_is_refused(s):
    r = s.call('memory_access', {'action': 'write', 'offset': '0x8000', 'hex': 'XYZ'}, ok=False)
    expect(r.is_error, 'bad hex is an error', r.text)


if __name__ == '__main__':
    sys.exit(main(globals()))
