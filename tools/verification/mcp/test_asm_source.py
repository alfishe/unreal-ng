#!/usr/bin/env python3
"""asm_source: the assembler source formats and dialects (machine-free), an encode / decode round trip"""

import sys

from mcplib import expect, main


def setup(s):
    pass  # no machine needed


def test_dialects_in_the_text(s):
    r = s.call('asm_source', {'action': 'dialects'})
    expect('=' in r.text, 'dialects listed', r.text)


def test_formats(s):
    r = s.call('asm_source', {'action': 'formats'})
    expect(r.structured, 'formats', r.text)


def test_encode_decode_round_trip(s):
    formats = s.call('asm_source', {'action': 'formats'}).structured
    ids = [f.get('id') for f in formats.get('formats', []) if f.get('id')]
    codec = next((c for c in ids if 'alasm' in c), ids[0] if ids else None)
    expect(codec, 'a codec to try', ids)
    text = '\tORG #8000\nstart\tLD A,7\n\tRET\n'
    encoded = s.call('asm_source', {'action': 'encode', 'codec': codec, 'text': text})
    data = encoded.structured.get('data') or encoded.structured.get('base64')
    expect(data, 'the encoded bytes', encoded.structured)
    decoded = s.call('asm_source', {'action': 'decode', 'codec': codec, 'data': data})
    expect('LD A,7' in decoded.text.upper() or 'LD A,7' in str(decoded.structured).upper(), 'the text back',
           decoded.text[:300])


if __name__ == '__main__':
    sys.exit(main(globals()))
