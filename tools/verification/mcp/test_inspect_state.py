#!/usr/bin/env python3
"""inspect_state: every aspect answers with text that carries its data (no "see structuredContent"), the classic ones
in their own form (registers, memory hexdump, disasm listing, attribute grid)"""

import re
import sys

from mcplib import expect, main

# Aspects that need arguments or another machine: checked to answer, not for their data
NEEDS_ARGS = {'next_pixel', 'memory_region'}


def test_every_aspect_answers_with_data_in_the_text(s):
    aspects = s.tools()['inspect_state']['inputSchema']['properties']['aspects']['items']['enum']
    expect(len(aspects) > 50, f'the aspect list ({len(aspects)})')
    bare = []
    for aspect in aspects:
        r = s.call('inspect_state', {'aspects': [aspect]}, ok=False)
        expect(not r.is_error or aspect in NEEDS_ARGS, f'{aspect} answers', r.text)
        section = r.text.split(f'[{aspect}]', 1)[-1].strip()
        if 'see structuredContent' in r.text or not section:
            bare.append(aspect)
    expect(not bare, 'every aspect writes its data in the text', bare)


def test_registers(s):
    r = s.call('inspect_state', {'aspects': ['registers']})
    for name in ('PC=0x', 'SP=0x', 'AF=0x', 'BC=0x', 'HL=0x', 'IX=0x', 'IY=0x'):
        expect(name in r.text, f'{name} in the text', r.text)
    pc = r.structured['registers']['pc'] if 'pc' in r.structured.get('registers', {}) else None
    if pc is not None:
        expect(f'PC=0x{pc:04X}' in r.text, 'the text PC is the structured PC', r.text)


def test_memory_hexdump_at_a_hex_string_address(s):
    s.call('memory_access', {'action': 'write', 'address': '0x8000', 'hex': 'DEADBEEF'})
    r = s.call('inspect_state', {'aspects': ['memory'], 'address': '0x8000', 'size': 16})
    expect('DE AD BE EF' in r.text, 'the bytes in the text', r.text)


def test_disasm_listing(s):
    s.call('memory_access', {'action': 'write', 'address': '0x8000', 'hex': '3E07C9'})
    r = s.call('inspect_state', {'aspects': ['disasm'], 'address': '0x8000', 'count': 2})
    expect(re.search(r'8000\s+3E 07\s+ld a,(#07|7|0x07|\$07)', r.text, re.I), 'ld a,7 listed', r.text)
    expect(re.search(r'8002\s+C9\s+ret', r.text, re.I), 'ret listed', r.text)


def test_screen_attributes_grid(s):
    r = s.call('inspect_state', {'aspects': ['screen_attributes'], 'screen': 0})
    rows = [line for line in r.text.splitlines() if re.fullmatch(r'\s+[0-9A-F]{64}', line)]
    expect(len(rows) == 24, '24 rows of 32 attribute bytes', r.text[:400])


def test_key_value_aspects(s):
    for aspect, field in (('machine', 'model='), ('timing', 'frame_timing.frame_tstates='), ('memory_banks', 'paging.')):
        r = s.call('inspect_state', {'aspects': [aspect]})
        expect(field in r.text, f'{aspect}: {field} in the text', r.text)


def test_monitor_aspect_has_host_and_instance_stats_in_the_text(s):
    r = s.call('inspect_state', {'aspects': ['monitor']})
    expect('[monitor] host CPU' in r.text and 'MB, threads' in r.text, 'the host line', r.text)
    expect(' fps, speed ' in r.text and ' MHz' in r.text, 'the instance line', r.text)
    monitor = r.structured.get('monitor', {})
    expect(monitor.get('host', {}).get('rss_bytes', 0) > 0, 'host.stats rss_bytes', monitor.get('host'))
    instances = monitor.get('instances', [])
    expect(len(instances) == 1 and 'emulator.stats' in instances[0], 'the instance', instances)


def test_breakpoints_aspect_carries_the_groups(s):
    r = s.call('inspect_state', {'aspects': ['breakpoints']})
    expect('groups' in r.structured.get('breakpoints', {}), 'groups beside the list', r.structured.get('breakpoints'))


def test_unknown_aspect_is_an_error(s):
    r = s.call('inspect_state', {'aspects': ['no_such_aspect']}, ok=False)
    expect(r.is_error and 'no_such_aspect' in r.text, 'names the aspect', r.text)


if __name__ == '__main__':
    sys.exit(main(globals()))
