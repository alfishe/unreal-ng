#!/usr/bin/env python3
"""What a client that hands the model only content[] (claude.ai connectors) sees: for each call below, the share of
the structuredContent values the default text carries. A call under its floor fails: its text went back to a
headline. The same measurement ranked the calls when the text was filled in (2026-10-10)"""

import sys

from mcplib import expect, main

# (tool, arguments, floor %): the calls whose text carries their data; 80 % = the model needs no second call
CALLS = [
    ('inspect_state', {'aspects': ['machine']}, 80),
    ('inspect_state', {'aspects': ['screen']}, 80),
    ('inspect_state', {'aspects': ['video']}, 80),
    ('inspect_state', {'aspects': ['timing']}, 80),
    ('inspect_state', {'aspects': ['screen_flash']}, 80),
    ('inspect_state', {'aspects': ['memory_banks']}, 80),
    ('inspect_state', {'aspects': ['screen_image']}, 80),
    ('inspect_state', {'aspects': ['screen_attributes']}, 80),
    ('inspect_state', {'aspects': ['registers']}, 80),
    ('inspect_state', {'aspects': ['paging']}, 50),
    ('type_input', {'action': 'list_keys'}, 80),
    ('debug_code', {'action': 'trace', 'frames': 2, 'limit': 32}, 80),
    ('emulator_manage', {'action': 'status'}, 80),
    ('analyze_performance', {'action': 'profile_status'}, 80),
    ('analyze_performance', {'action': 'frame_cost'}, 80),
    ('capture_media', {'action': 'record_status'}, 80),
    ('capture_media', {'action': 'screen_digest'}, 80),
    ('mouse_input', {'action': 'status'}, 80),
    ('joystick_input', {'action': 'status'}, 80),
    ('type_input', {'action': 'status'}, 80),
    ('asm_source', {'action': 'dialects'}, 80),
    ('emulator_manage', {'action': 'server'}, 80),
    ('media', {'action': 'formats'}, 80),
    ('bus_analyzer', {'action': 'list'}, 80),
    ('streamdeck', {'action': 'status'}, 50),
]


def leaves(value, out):
    if isinstance(value, dict):
        for item in value.values():
            leaves(item, out)
    elif isinstance(value, list):
        for item in value:
            leaves(item, out)
    elif isinstance(value, bool) or value is None:
        pass
    elif isinstance(value, (int, float)) and abs(value) < 10:
        pass  # 0..9 are found anywhere: no evidence
    elif isinstance(value, str) and (len(value) < 3 or len(value) > 200):
        pass  # one-letter ids; blobs (base64, register dumps)
    else:
        out.append(value)


def found(value, text):
    if isinstance(value, str):
        return value.lower() in text or value.lower().replace('0x', '') in text
    forms = {str(value)}
    if isinstance(value, int) and value >= 0:
        forms |= {f'{value:x}', f'{value:02x}', f'{value:04x}'}
    if isinstance(value, float):
        forms |= {f'{value:.1f}', f'{value:.2f}', f'{value:.3f}', str(round(value)), f'{value:g}', f'{value:.10g}'}
    return any(form.lower() in text for form in forms)


def coverage(result):
    values = []
    leaves(result.structured, values)
    if not values:
        return 100
    text = result.text.lower()
    return round(100 * sum(found(v, text) for v in values) / len(values))


def test_text_carries_the_data(s):
    low = []
    for tool, args, floor in CALLS:
        r = s.call(tool, dict(args))
        share = coverage(r)
        if share < floor:
            low.append(f'{tool} {args}: {share}% < {floor}%')
    expect(not low, 'every call at or above its floor', '\n'.join(low))


def test_listings_in_the_text(s):
    s.call('memory_access', {'action': 'write', 'address': '0x8000', 'hex': '3E07C9'})
    r = s.call('memory_access', {'action': 'read', 'address': '0x8000', 'length': 3})
    expect('3E 07 C9' in r.text, 'memory_access read: the bytes', r.text)
    r = s.call('debug_code', {'action': 'disassemble', 'address': '0x8000', 'count': 2})
    expect('ld a,' in r.text.lower() and 'ret' in r.text.lower(), 'disassemble: the listing', r.text)
    s.call('control_execution', {'action': 'bp_add', 'address': '0x1234'})
    r = s.call('control_execution', {'action': 'bp_list'})
    expect('0x1234' in r.text, 'bp_list: the breakpoint', r.text)
    s.call('control_execution', {'action': 'bp_clear'})


if __name__ == '__main__':
    sys.exit(main(globals()))
