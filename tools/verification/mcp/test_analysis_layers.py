#!/usr/bin/env python3
"""analysis: the L0 access facts of the analysis layers (docs/emulator/analysis/analysis-layers.md) over a run - start,
status, explain an address the ROM runs every frame and a stack byte, the refusals, stop; the L1 build from a
recording (start record=true, build, the analysis.ready topic's value) and a routine's fact sheet; the L2 structure
(query by kind and range, conflicts, the refusals), the draft export (ctl / skool / fields in the answer, skool to a
file) and the gallery (the list, one region as a PNG image block or a file); L3 (findings, review and resolve,
experiments, coverage; the queue by reason and offset, entry candidates, naming batches, a kind change, no implicit
session); the readable answer in content[] text, the calls counted per action in host.clients.mcp.tools"""

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


def test_gallery(s):
    """The sprite fixture (16 x 16, 4 frames, an interleaved mask) assembled and run: the gallery lists its sprite,
    draws it as one PNG image block (not as base64 in structuredContent) or into a file, and refuses bad options"""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, '..', '..', '..', 'testdata', 'analysis', 'fixtures', 'sprite-16x16.asm')) as f:
        source = f.read()
    r = s.call('debug_code', {'action': 'assemble', 'code': source, 'address': '0x8000'})
    s.call('control_execution', {'action': 'set_register', 'name': 'pc', 'value': '0x8000'})
    s.call('analysis', {'action': 'start', 'record': True})
    # Ten frames, as AnalysisDetectorSprites_Test: a sprite's rows link where the screen shows them as a frame ends,
    # and three frame ends show some of the four frames only part drawn
    s.call('control_execution', {'action': 'run_frames', 'frames': 10})
    r = s.call('analysis', {'action': 'build'})
    expect((r.structured or {}).get('state') == 'done', 'build of the sprite fixture: done', r.text[:300])

    r = s.call('analysis', {'action': 'gallery', 'kind': 'sprite'})
    expect('L2 gallery:' in r.text and 'graphics regions' in r.text, 'gallery: the list in the text', r.text[:600])
    regions = (r.structured or {}).get('regions', [])
    sprite = next((g for g in regions if g.get('geometry', {}).get('width') == 16), None)
    expect(sprite is not None, 'gallery: the 16 x 16 sprite listed', r.text[:600])
    if sprite:
        geometry = sprite['geometry']
        expect(geometry.get('height') == 16 and geometry.get('frames') == 4 and geometry.get('mask') is True,
               'gallery: 16 x 16, 4 frames, masked', geometry)
        expect('16x16x4 mask' in r.text, 'gallery: the shape in the text', r.text[:600])
        r = s.call('analysis', {'action': 'gallery', 'address': sprite['start'], 'scale': 1})
        expect('PNG' in r.text and '4 frames' in r.text, 'gallery address=: the picture in the text', r.text[:300])
        images = r.blocks('image')
        expect(len(images) == 1 and images[0].get('mimeType') == 'image/png' and
               images[0].get('data', '').startswith('iVBOR'), 'gallery: one PNG image block', str(images)[:200])
        expect('iVBOR' not in str((r.structured or {}).get('png', '')), 'gallery: no base64 in structuredContent',
               str(r.structured)[:300])
        expect((r.structured or {}).get('width') == 4 * 17 + 1, 'gallery: 4 frames a row at scale 1',
               r.structured)
        scratch = os.path.join(here, '..', '..', '..', 'scratch', 'mcp-verify')
        os.makedirs(scratch, exist_ok=True)
        png = os.path.abspath(os.path.join(scratch, 'analysis-gallery.png'))
        r = s.call('analysis', {'action': 'gallery', 'address': sprite['start'], 'path': png})
        expect('written to ' + png in r.text and not r.blocks('image'), 'gallery path=: written, no image block',
               r.text[:300])
        if os.path.exists(png):  # the emulator runs on this host
            with open(png, 'rb') as f:
                expect(f.read(8) == b'\x89PNG\r\n\x1a\n', 'the PNG file')
            os.remove(png)
    r = s.call('analysis', {'action': 'gallery', 'kind': 'code'}, ok=False)
    expect(r.is_error and '400' in r.text, 'gallery: a kind with no picture is refused with 400', r.text)
    r = s.call('analysis', {'action': 'gallery', 'scale': 9}, ok=False)
    expect(r.is_error and '400' in r.text, 'gallery: scale 9 is refused with 400', r.text)
    r = s.call('analysis', {'action': 'gallery', 'address': '$0000'}, ok=False)
    expect(r.is_error and '404' in r.text, 'gallery: no graphics at $0000: 404', r.text)
    s.call('analysis', {'action': 'stop'})


def test_review_experiment_cover(s):
    """L3 (P5) on the poke-flip fixture (a mode byte the loop compares with CP 5, never 5 in the session): the findings
    register (table, markdown), the review queue (next with its sheet, resolve with a label the skool draft carries),
    an observed and a refuted experiment (spec as an object, as an array), the coverage driver flipping the jr z,
    the refusals"""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, '..', '..', '..', 'testdata', 'analysis', 'fixtures', 'poke-flip.asm')) as f:
        source = f.read()
    s.call('debug_code', {'action': 'assemble', 'code': source, 'address': '0x8000'})
    # The fixture's layout at $8000: main $8004, jr z,rare $800D, rare $8011, mode $801C, seen $801D, ticks $801E
    rare, mode, seen = '$8011', '$801C', '$801D'
    s.call('control_execution', {'action': 'set_register', 'name': 'pc', 'value': '0x8000'})
    s.call('analysis', {'action': 'start', 'record': True})
    s.call('control_execution', {'action': 'run_frames', 'frames': 3})
    r = s.call('analysis', {'action': 'build'})
    expect((r.structured or {}).get('state') == 'done', 'build of the poke-flip fixture: done', r.text[:300])

    r = s.call('analysis', {'action': 'findings'})
    expect('Findings:' in r.text and re.search(r'\bF1\b', r.text), 'findings: the register as a table in the text',
           r.text[:600])
    bugs = [f for f in (r.structured or {}).get('findings', []) if f.get('kind') == 'bug']
    expect(any(b.get('subject') == 'counter-wrap' and b.get('start') == '$801E' for b in bugs),
           'findings: the wrapping counter is a candidate bug', str(bugs)[:400])
    r = s.call('analysis', {'action': 'findings', 'format': 'markdown'})
    expect('| ID | Status | Claim | Evidence | Refs |' in (r.structured or {}).get('content', ''),
           'findings format=markdown: the zx-reaper findings table', r.text[:400])

    r = s.call('analysis', {'action': 'review'})
    item = (r.structured or {}).get('item') or {}
    expect(item.get('id', '').startswith('F') and 'op=resolve id=' + item.get('id', '') in r.text,
           'review: the next item with its sheet and the resolve hint in the text', r.text[:800])
    expect(0 < (r.structured or {}).get('sheet_bytes', 0) <= 1024, 'review: the sheet is at most 1 KB', r.structured)

    claims = s.call('analysis', {'action': 'findings', 'kind': 'claim', 'range': '8000-80FF'}).structured or {}
    code = next((f for f in claims.get('findings', []) if f.get('start') == rare and f.get('subject') == 'code'), None)
    expect(code is not None, 'findings: a code claim at the rare routine', str(claims)[:600])
    if code:
        r = s.call('analysis', {'action': 'review', 'op': 'resolve', 'id': code['id'], 'verdict': 'ok',
                                'label': 'RareMode', 'title': 'Mode 5 handler'})
        expect(((r.structured or {}).get('finding') or {}).get('status') == 'confirmed', 'review resolve ok: confirmed',
               r.text[:300])
        r = s.call('analysis', {'action': 'export', 'format': 'skool', 'ranges': '8000-80FF'})
        content = (r.structured or {}).get('content', '')
        expect('@label=RareMode' in content and 'Mode 5 handler' in content, 'export: the resolved label and title',
               content[:800])
        r = s.call('analysis', {'action': 'experiment', 'finding': code['id'],
                                'spec': {'poke': {mode: 5}, 'frames': 2, 'watch': [{'pc': rare, 'as': 'rare'}],
                                         'expect': {'rare': 'reached', 'mem:' + seen: '$AA'}}})
        expect((r.structured or {}).get('observed') == 1, 'experiment: mode 5 reaches the rare code (observed)',
               r.text[:600])
    r = s.call('analysis', {'action': 'experiment',
                            'spec': [{'id': 'nopoke', 'frames': 2, 'watch': [{'pc': rare, 'as': 'rare'}],
                                      'expect': {'rare': 'reached'}},
                                     {'id': 'mode1', 'poke': {mode: 1}, 'frames': 2, 'expect': {'mem:' + seen: '0'}}]})
    results = (r.structured or {}).get('results', [])
    expect([x.get('verdict') for x in results] == ['refuted', 'observed'],
           'experiment array: no poke refuted, mode 1 observed', r.text[:600])
    expect('nopoke refuted' in r.text, 'experiment: the verdicts in the text', r.text[:600])

    r = s.call('analysis', {'action': 'cover', 'frames': 3})
    expect((r.structured or {}).get('flipped', 0) >= 1 and 'poke $801C=$05' in r.text,
           'cover: the jr z flipped by holding mode at 5', r.text[:800])

    r = s.call('analysis', {'action': 'experiment'}, ok=False)
    expect(r.is_error, 'experiment without a spec is refused', r.text)
    r = s.call('analysis', {'action': 'experiment', 'spec': {'frames': 2}}, ok=False)
    expect(r.is_error and '400' in r.text and 'expect' in r.text, 'experiment: a spec with no expect: 400', r.text)
    r = s.call('analysis', {'action': 'review', 'op': 'resolve', 'id': 'F99999', 'verdict': 'ok'}, ok=False)
    expect(r.is_error and '404' in r.text, 'review resolve of no such finding: 404', r.text)
    s.call('analysis', {'action': 'stop'})


def test_review_entries_names_kind(s):
    """L3 (P5b) on the tail-entry fixture: the queue by reason (entry candidates, naming batches) and by offset, an
    entry candidate accepted splits the ctl draft, a naming batch's names reach the skool draft, a kind change, no
    review call opens a session implicitly"""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, '..', '..', '..', 'testdata', 'analysis', 'fixtures', 'tail-entry.asm')) as f:
        source = f.read()
    s.call('debug_code', {'action': 'assemble', 'code': source, 'address': '0x8000'})
    s.call('control_execution', {'action': 'set_register', 'name': 'pc', 'value': '0x8000'})
    s.call('analysis', {'action': 'start', 'record': True})
    s.call('control_execution', {'action': 'run_frames', 'frames': 3})
    r = s.call('analysis', {'action': 'build'})
    expect((r.structured or {}).get('state') == 'done', 'build of the tail-entry fixture: done', r.text[:300])

    r = s.call('analysis', {'action': 'review', 'op': 'list', 'reason': 'entry'})
    items = (r.structured or {}).get('items', [])
    entries = [i for i in items if i.get('reason') == 'entry']
    expect(len(entries) == 2 and len(entries) == len([i for i in items if i.get('id') != 'more']),
           'review list reason=entry: the two entry candidates only', r.text[:600])
    expect('implicitly' not in r.text, 'review: no implicit session note', r.text[:300])
    r = s.call('analysis', {'action': 'review', 'op': 'list', 'limit': 1000})
    every = [i['id'] for i in (r.structured or {}).get('items', []) if i.get('id') != 'more']
    r = s.call('analysis', {'action': 'review', 'op': 'list', 'offset': 1, 'limit': 1000})
    paged = [i['id'] for i in (r.structured or {}).get('items', []) if i.get('id') != 'more']
    expect(len(every) >= 2 and paged == every[1:], 'review list offset=1: the same queue from its second item',
           (every[:6], paged[:6]))
    if entries:
        # The tail candidate never ran: its sheet decodes the start image. The prologue candidate runs inside the
        # routine before it, so accepting it splits that ctl entry.
        tail = next((e for e in entries if 'tail' in e.get('claim', '')), entries[0])
        pro = next((e for e in entries if e is not tail), tail)
        r = s.call('analysis', {'action': 'review', 'op': 'show', 'id': tail['id']})
        expect('not executed' in r.text, 'an entry candidate sheet: the code decoded, marked not executed', r.text[:700])
        r = s.call('analysis', {'action': 'export', 'format': 'ctl', 'ranges': '8000-80FF'})
        before = (r.structured or {}).get('content', '')
        r = s.call('analysis', {'action': 'review', 'op': 'resolve', 'id': pro['id'], 'verdict': 'ok'})
        expect('a routine starts at ' + pro['start'] in r.text, 'resolve an entry candidate ok', r.text[:300])
        r = s.call('analysis', {'action': 'export', 'format': 'ctl', 'ranges': '8000-80FF'})
        after = (r.structured or {}).get('content', '')
        expect('\nc ' + pro['start'] not in before and '\nc ' + pro['start'] in after,
               'export ctl: the accepted entry splits there', after[:900])

    r = s.call('analysis', {'action': 'review', 'op': 'list', 'reason': 'name', 'limit': 100})
    batches = [i for i in (r.structured or {}).get('items', []) if i.get('reason') == 'name']
    expect(batches and batches[0].get('id') == 'N1' and batches[0].get('subjects'),
           'review list reason=name: naming batches N1.. with their subjects', r.text[:600])
    if batches:
        at = batches[0]['subjects'][0]['at']
        r = s.call('analysis', {'action': 'review', 'op': 'show', 'id': 'N1'})
        expect('op=resolve id=N1 names=' in r.text and 0 < (r.structured or {}).get('sheet_bytes', 0) <= 2048,
               'review show N1: the batch sheet (<= 2 KB) with the resolve line', r.text[:900])
        r = s.call('analysis', {'action': 'review', 'op': 'resolve', 'id': 'N1',
                                'names': {at: {'label': 'McpNamed', 'title': 'Named over MCP'}}})
        expect('N1 resolved: 1 named' in r.text, 'resolve N1 with names (an object)', r.text[:300])
        r = s.call('analysis', {'action': 'export', 'format': 'skool', 'ranges': '8000-80FF'})
        expect('@label=McpNamed' in (r.structured or {}).get('content', ''), 'export skool: the batch name',
               (r.structured or {}).get('content', '')[:900])
        r = s.call('analysis', {'action': 'review', 'op': 'resolve', 'id': 'N1', 'names': {'$0001': 'X'}}, ok=False)
        expect(r.is_error and '400' in r.text, 'names for no subject of the batch: 400', r.text)

    claims = s.call('analysis', {'action': 'findings', 'kind': 'claim', 'range': '8000-80FF',
                                 'limit': 500}).structured or {}
    var = next((f for f in claims.get('findings', []) if f.get('subject') == 'variable'), None)
    expect(var is not None, 'findings: a variable claim', str(claims)[:400])
    if var:
        r = s.call('analysis', {'action': 'review', 'op': 'resolve', 'id': var['id'], 'kind': 'text'})
        expect('made text' in r.text, 'resolve kind=text', r.text[:300])
        r = s.call('analysis', {'action': 'export', 'format': 'ctl', 'ranges': '8000-80FF'})
        expect('\nt ' + var['start'] in (r.structured or {}).get('content', ''), 'export ctl: the range is text now',
               (r.structured or {}).get('content', '')[:900])
    s.call('analysis', {'action': 'stop'})


def test_calls_counted_per_action(s):
    s.call('analysis', {'action': 'status'})
    r = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/monitor/host.clients'})
    body = r.structured or {}
    clients = body.get('host.clients', body.get('clients', body.get('body', body)))
    tools = clients.get('mcp', {}).get('tools', {})
    actions = tools.get('analysis', {}).get('actions', {})
    expect(actions.get('status', 0) >= 2 and actions.get('explain', 0) >= 1 and actions.get('query', 0) >= 1 and
           actions.get('export', 0) >= 1 and actions.get('gallery', 0) >= 1 and actions.get('findings', 0) >= 1 and
           actions.get('review', 0) >= 1 and actions.get('experiment', 0) >= 1 and actions.get('cover', 0) >= 1,
           'host.clients.mcp.tools counts the actions',
           str(body)[:600])


if __name__ == '__main__':
    sys.exit(main(globals()))
