#!/usr/bin/env python3
"""analysis: the L0 access facts of the analysis layers (docs/emulator/analysis/analysis-layers.md) over a run - start,
status, explain an address the ROM runs every frame and a stack byte, the refusals, stop; the L1 build from a
recording (start record=true, build, the analysis.ready topic's value) and a routine's fact sheet; the L2 structure
(query by kind and range, conflicts, the refusals) and the draft export (ctl / skool / fields in the answer, skool to a
file); the readable answer in content[] text, the calls counted per action in host.clients.mcp.tools"""

import os
import re
import sys

from mcplib import expect, main


def any_ready(node):
    """True when a value of the answer (per instance, however nested) has ready = true"""
    if isinstance(node, dict):
        return node.get('ready') is True or any(any_ready(v) for v in node.values())
    if isinstance(node, list):
        return any(any_ready(v) for v in node)
    return False


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


def test_build_and_explain_routine(s):
    r = s.call('analysis', {'action': 'start', 'record': True})
    expect('recording' in r.text, 'start record=true: the recording in the text', r.text)
    s.call('control_execution', {'action': 'run_frames', 'frames': 50})

    r = s.call('analysis', {'action': 'build'})
    expect('done' in r.text and 'routines' in r.text, 'build: done with routines in the text', r.text)
    expect((r.structured or {}).get('state') == 'done', 'build: state done in structuredContent', r.structured)
    expect((r.structured or {}).get('job', 0) > 0, 'build: a job id', r.structured)

    r = s.call('analysis', {'action': 'status'})
    expect('L1 ready' in r.text, 'status: L1 ready in the text', r.text)

    # The 48K ROM's IM 1 handler: an interrupt entry, explained as a routine with its fact sheet
    r = s.call('analysis', {'action': 'explain', 'address': '$0038'})
    expect('Routine' in r.text and 'interrupt' in r.text, 'explain $0038: the routine fact sheet', r.text)
    sheet = r.text[r.text.find('Routine'):]
    expect(len(sheet.encode()) <= 1024, 'the fact sheet within 1 KB', len(sheet.encode()))
    expect((r.structured or {}).get('what') == 'routine', 'explain: what = routine', r.structured)
    r = s.call('analysis', {'action': 'explain', 'address': '$0038', 'what': 'byte'})
    expect('OpcodeFetch' in r.text, 'what=byte: the L0 facts of the byte', r.text)

    r = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/monitor/analysis.ready'})
    expect(any_ready(r.structured) or re.search(r'"ready"\s*:\s*true', r.text) is not None, 'the analysis.ready value',
           r.text[:600])
    r = s.call('analysis', {'action': 'cancel'}, ok=False)
    expect(r.is_error and '409' in r.text, 'cancel with no build running: 409', r.text)

    # L2: the claims of the build, by kind and range, in the text and in structuredContent
    r = s.call('analysis', {'action': 'query'})
    expect(re.search(r'L2 all in \$0000-\$FFFF: \d+ regions', r.text) is not None, 'query: the count in the text', r.text[:600])
    regions = (r.structured or {}).get('regions', [])
    expect(len(regions) > 0 and (r.structured or {}).get('count', 0) >= len(regions), 'query: regions and count',
           str(r.structured)[:600])
    r = s.call('analysis', {'action': 'query', 'kind': 'code', 'range': '0000-3FFF', 'limit': 1})
    expect('L2 code in $0000-$3FFF' in r.text, 'query kind=code range=0000-3FFF: in the text', r.text[:600])
    found = (r.structured or {}).get('regions', [])
    expect(len(found) <= 1, 'query limit=1: at most one region', r.structured)
    if (r.structured or {}).get('count', 0) > 1:
        expect('(first 1)' in r.text and (r.structured or {}).get('truncated') is True, 'query: truncated', r.text[:300])
    r = s.call('analysis', {'action': 'query', 'kind': 'conflicts', 'limit': 5})
    expect('conflicts' in r.text, 'query kind=conflicts: in the text', r.text[:300])
    r = s.call('analysis', {'action': 'query', 'kind': 'no_such_kind'}, ok=False)
    expect(r.is_error and '400' in r.text, 'query: an unknown kind is refused with 400', r.text)
    r = s.call('analysis', {'action': 'query', 'range': '9000-8000'}, ok=False)
    expect(r.is_error and '400' in r.text, 'query: an inverted range is refused with 400', r.text)

    # The draft: ctl / fields in the answer (the file in content[] text), skool to a file
    r = s.call('analysis', {'action': 'export', 'format': 'ctl', 'ranges': '0000-3FFF'})
    expect('L2 ctl (0000-3FFF)' in r.text and re.search(r'^[cbtuwg] \$0', r.text, re.M) is not None,
           'export ctl: the control file in the text', r.text[:600])
    expect((r.structured or {}).get('entries', 0) > 0, 'export: entries in structuredContent', str(r.structured)[:300])
    r = s.call('analysis', {'action': 'export', 'format': 'fields', 'ranges': '0000-3FFF'})
    expect('L2 fields' in r.text and '{' in r.text, 'export fields: JSON in the text', r.text[:300])
    scratch = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'scratch', 'mcp-verify')
    os.makedirs(scratch, exist_ok=True)
    path = os.path.abspath(os.path.join(scratch, 'analysis-draft.skool'))
    r = s.call('analysis', {'action': 'export', 'format': 'skool', 'ranges': '0000-3FFF', 'path': path, 'width': 4})
    expect('written to ' + path in r.text, 'export skool path=: written in the text', r.text)
    if os.path.exists(path):  # the emulator runs on this host
        with open(path) as f:
            skool = f.read()
        expect(skool.startswith('; Skool draft') and '@start' in skool, 'the skool file', skool[:300])
        os.remove(path)
    r = s.call('analysis', {'action': 'export'}, ok=False)
    expect(r.is_error and 'format' in r.text, 'export without a format is refused', r.text)
    r = s.call('analysis', {'action': 'export', 'format': 'pdf'}, ok=False)
    expect(r.is_error and '400' in r.text, 'export: an unknown format is refused with 400', r.text)
    r = s.call('analysis', {'action': 'export', 'format': 'ctl', 'ranges': '0000-3FFF,3000-4000'}, ok=False)
    expect(r.is_error and '400' in r.text, 'export: overlapping ranges are refused with 400', r.text)
    s.call('analysis', {'action': 'stop'})


def test_calls_counted_per_action(s):
    s.call('analysis', {'action': 'status'})
    r = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/monitor/host.clients'})
    body = r.structured or {}
    clients = body.get('host.clients', body.get('clients', body.get('body', body)))
    tools = clients.get('mcp', {}).get('tools', {})
    actions = tools.get('analysis', {}).get('actions', {})
    expect(actions.get('status', 0) >= 2 and actions.get('explain', 0) >= 1 and actions.get('query', 0) >= 1 and
           actions.get('export', 0) >= 1, 'host.clients.mcp.tools counts the actions',
           str(body)[:600])


if __name__ == '__main__':
    sys.exit(main(globals()))
