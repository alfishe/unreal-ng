#!/usr/bin/env python3
"""Pentagon 2.666 LE: the model in create, the `p2666` node of inspect_state paging (Gluk's clock,
the window map), a snapshot load into the board's Pentagon 128 form, and the machine resource. The values are
asserted by the core tests (P2666Snapshot_Test, P2666Ttd_Test); this checks what only the live MCP shows."""

import os
import sys

from mcplib import main, expect

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))


def test_create_and_paging_node(s):
    s.fresh_machine('P2666')
    r = s.call('inspect_state', {'aspects': ['paging']})
    expect('p2666:' in r.text, 'the board node in the text', r.text[:2000])
    for key in ('port_xx77=', 'port_eff7=', 'window3 ', 'other_map', 'palette'):
        expect(key in r.text, 'the node names ' + key, r.text[:3000])
    # fresh_machine runs 100 frames: Gluk has set its own clock by then (its CMOS setting), whichever of the three
    expect(any('cpu_mhz %s,' % mhz in r.text for mhz in ('3.5', '14', '28')), 'the clock in the text', r.text[:3000])


def test_snapshot_loads_as_a_pentagon_128(s):
    s.fresh_machine('P2666')
    path = os.path.join(ROOT, 'testdata', 'loaders', 'sna', 'across-the-edge-second.sna')
    loaded = s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/emulator/{id}/snapshot/load', 'body': {'path': path}})
    expect(not loaded.is_error, 'the 128K snapshot loads', loaded.text[:2000])
    r = s.call('inspect_state', {'aspects': ['paging']})
    expect(r.text.count(' pentagon register=') == 4, 'every window in Pentagon mode', r.text[:3000])
    expect('mode128=1' in r.text and 'dos off' in r.text, '#EFF7 in 128 mode, DOS off', r.text[:3000])


def test_machine_resource(s):
    contents = s.rpc('resources/read', {'uri': 'unreal://machine/p2666'})['result']['contents']
    text = contents[0].get('text', '')
    expect('Pentagon 2.666 LE' in text and 'P2666Board (71)' in text and 'mode:pentagon128' in text,
           'the resource describes the board, its TTD blobs and the snapshot rule', text[:500])


if __name__ == '__main__':
    sys.exit(main(globals()))
