#!/usr/bin/env python3
"""manage_symbols: the ROM labels resolve both ways with the answer in the text; list filters by name and address
range and writes one label per line"""

import re
import sys

from mcplib import expect, main


def test_resolve_by_name(s):
    r = s.call('manage_symbols', {'action': 'resolve', 'name': 'CLS'})
    expect(re.search(r'CLS = 0x0D6B', r.text), 'the address in the text', r.text)


def test_resolve_by_address(s):
    r = s.call('manage_symbols', {'action': 'resolve', 'address': '0x0D6B'})
    expect('0x0D6B' in r.text and '=' in r.text, 'the label in the text', r.text)


def test_list_by_name(s):
    r = s.call('manage_symbols', {'action': 'list', 'name': 'print'})
    lines = [line for line in r.text.splitlines() if re.match(r'0x[0-9A-F]{4} ', line)]
    expect(lines and all('print' in line.lower() for line in lines), 'only matching labels', r.text[:400])
    expect(len(lines) == r.structured['count'] or 'more:' in r.text, 'every match listed or the rest counted')


def test_list_by_range(s):
    r = s.call('manage_symbols', {'action': 'list', 'from': '0x0000', 'to': '0x00FF', 'limit': 500})
    addresses = [int(m, 16) for m in re.findall(r'^0x([0-9A-F]{4}) ', r.text, re.M)]
    expect(addresses and max(addresses) <= 0xFF, 'labels inside the range', addresses[:10])


def test_list_limit_and_note(s):
    r = s.call('manage_symbols', {'action': 'list', 'limit': 5})
    lines = [line for line in r.text.splitlines() if re.match(r'0x[0-9A-F]{4} ', line)]
    expect(len(lines) == 5 and 'more:' in r.text, '5 lines and how to narrow', r.text)


def test_bad_range_is_an_error(s):
    r = s.call('manage_symbols', {'action': 'list', 'from': 'zz'}, ok=False)
    expect(r.is_error, 'refused', r.text)


if __name__ == '__main__':
    sys.exit(main(globals()))
