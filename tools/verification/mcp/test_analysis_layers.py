#!/usr/bin/env python3
"""analysis: the L0 access facts of the analysis layers (docs/emulator/analysis/analysis-layers.md) over a run - start,
status, explain an address the ROM runs every frame and a stack byte, the refusals, stop; the readable answer in
content[] text, the calls counted per action in host.clients.mcp.tools"""

import sys

from mcplib import expect, main


def test_start_status_explain_stop(s):
    r = s.call('analysis', {'action': 'start'})
    expect('capturing' in r.text and 'facts' in r.text, 'start: state and level in the text', r.text)
    s.call('control_execution', {'action': 'run_frames', 'frames': 5})

    r = s.call('analysis', {'action': 'status'})
    expect((r.structured or {}).get('facts', {}).get('z80_bytes_touched', 0) > 0, 'status: bytes touched', r.text)
    expect('touched' in r.text or 'bytes' in r.text, 'status: the facts in the text', r.text)

    # The IM 1 handler runs every frame: its first byte is an opcode fetch, frames counted
    r = s.call('analysis', {'action': 'explain', 'address': '$0038'})
    expect('OpcodeFetch' in r.text, 'explain $0038: OpcodeFetch in the text', r.text)
    expect('frames' in r.text and 'counters' in r.text, 'explain: frames and counters in the text', r.text)
    expect('OpcodeFetch' in (r.structured or {}).get('kinds', []), 'explain: kinds in structuredContent', r.structured)

    # The interrupt pushes the PC: the bytes under SP are stack writes
    regs = s.call('inspect_state', {'aspects': ['registers']})
    sp = None
    for token in regs.text.replace(',', ' ').split():
        if token.upper().startswith('SP='):
            sp = int(token[3:].lstrip('$#').replace('0x', ''), 16)
    if sp is not None:
        r = s.call('analysis', {'action': 'explain', 'address': (sp - 2) & 0xFFFF})
        expect('Stack' in r.text or 'not accessed' in r.text, 'explain below SP: stack kinds', r.text)

    r = s.call('analysis', {'action': 'stop'})
    expect('stopped' in r.text, 'stop: the state in the text', r.text)
    r = s.call('analysis', {'action': 'explain', 'address': 0x38})
    expect('OpcodeFetch' in r.text, 'the facts stay readable after stop', r.text)


def test_refusals(s):
    r = s.call('analysis', {'action': 'explain'}, ok=False)
    expect(r.is_error, 'explain without an address is refused', r.text)
    r = s.call('analysis', {'action': 'explain', 'address': 'no_such_label_here'}, ok=False)
    expect(r.is_error and '404' in r.text, 'an unknown label: 404', r.text)
    s.call('analysis', {'action': 'start', 'level': 'counters'})
    r = s.call('analysis', {'action': 'explain', 'address': '$0038'}, ok=False)
    expect(r.is_error and '409' in r.text, 'counters level: explain refused with 409', r.text)
    s.call('analysis', {'action': 'stop'})


def test_physical_mode_pages(s):
    r = s.call('analysis', {'action': 'start', 'mode': 'physical'})
    expect('physical' in r.text, 'physical mode in the text', r.text)
    s.call('control_execution', {'action': 'run_frames', 'frames': 2})
    r = s.call('analysis', {'action': 'status'})
    pages = (r.structured or {}).get('facts', {}).get('pages', [])
    expect(len(pages) > 0, 'pages with facts', r.text)
    r = s.call('analysis', {'action': 'explain', 'address': '$0038'})
    expect('ROM' in r.text and 'OpcodeFetch' in r.text, 'the ROM page of $0038', r.text)
    s.call('analysis', {'action': 'stop'})


def test_calls_counted_per_action(s):
    s.call('analysis', {'action': 'status'})
    r = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/monitor/host.clients'})
    body = r.structured or {}
    clients = body.get('host.clients', body.get('clients', body.get('body', body)))
    tools = clients.get('mcp', {}).get('tools', {})
    actions = tools.get('analysis', {}).get('actions', {})
    expect(actions.get('status', 0) >= 2 and actions.get('explain', 0) >= 1, 'host.clients.mcp.tools counts the actions',
           str(body)[:600])


if __name__ == '__main__':
    sys.exit(main(globals()))
