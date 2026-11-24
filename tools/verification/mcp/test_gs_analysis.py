#!/usr/bin/env python3
"""The GS card CPU analysis (gs-debugger TODO P5.6; requirements §4.13 X8 / X10 / X11): the `gs_analysis` tool on the
run's PENTAGON (a NeoGS in the GS slot). Off by default; on binds at the card's next run; coverage keys the marks by
physical page (flash / RAM), lists the firmware's handlers and the never-written reads; the heat map lists what the
channels touched in the last frames; reset clears, release frees. The text carries it all (an agent may see only it),
and REST answers the same report."""

import json
import sys
import urllib.error
import urllib.parse
import urllib.request

from mcplib import expect, main


def setup(s):
    s.fresh_machine()
    s.call('gs_analysis', {'action': 'release'}, ok=False)


def rest(s, path):
    port = urllib.parse.urlparse(s.url).port - 200
    listing = s.call('emulator_manage', {'action': 'list'})
    eid = listing.structured['emulators'][0]['id']
    try:
        with urllib.request.urlopen(f'http://127.0.0.1:{port}/api/v1/emulator/{eid}{path}', timeout=30) as r:
            return r.status, json.loads(r.read().decode())
    except urllib.error.HTTPError as error:
        return error.code, json.loads(error.read().decode() or '{}')


def test_off_by_default_then_on_binds_at_the_next_run(s):
    r = s.call('gs_analysis')
    expect('analysis off' in r.text and 'no data' in r.text, 'off by default, with how to start', r.text)
    expect(r.structured.get('allocated') is False and r.structured.get('memory_bytes') == 0, 'nothing allocated',
           r.structured)
    r = s.call('gs_analysis', {'action': 'on', 'decay': 4})
    expect(r.structured.get('enabled') is True and r.structured.get('memory_bytes', 0) > 1024 * 1024,
           'on allocates the arrays', r.text)
    expect(r.structured.get('decay') == 4, 'the decay set', r.structured)
    s.call('control_execution', {'action': 'run_frames', 'frames': 20})
    s.call('control_execution', {'action': 'pause'})
    r = s.call('gs_analysis', {'action': 'status'})
    expect('analysis on' in r.text and r.structured.get('recording') is True, 'bound after the card ran', r.text)


def test_coverage_by_physical_page_in_the_text(s):
    r = s.call('gs_analysis', {'action': 'coverage'})
    expect('flash (32 pages)' in r.text, 'the NeoGS flash is the ROM space', r.text)
    expect('RAM (' in r.text, 'the RAM space', r.text)
    # The firmware runs from flash or from the RAM it copied itself to (the idle loop): a page line with code ranges
    expect(('rom ' in r.text or 'ram ' in r.text) and 'code:' in r.text, 'a page the CPU ran on, with its code ranges',
           r.text)
    expect('handlers' in r.text, 'the handlers line (executed, or why not)', r.text)
    expect('never-written reads:' in r.text, 'the never-written line', r.text)
    spaces = r.structured['spaces']
    expect(spaces[0]['space'] == 'rom', 'the flash first', spaces[0].get('space'))
    expect(sum(sp['totals']['exec'] for sp in spaces) > 0, 'instruction starts counted',
           [sp.get('totals') for sp in spaces])
    one = s.call('gs_analysis', {'action': 'coverage', 'space': 'rom', 'page': 0, 'raw': True})
    page = one.structured['spaces'][0]['page_list'][0]
    expect(len(page.get('flags', '')) == 21848, "the page's 16,384 flags as base64", len(page.get('flags', '')))
    bad = s.call('gs_analysis', {'action': 'coverage', 'page': 2}, ok=False)
    expect(bad.is_error and 'space' in bad.text, 'page needs space', bad.text)


def test_heatmap_text_and_rest(s):
    r = s.call('gs_analysis', {'action': 'heatmap', 'recent': 'fetch,read', 'frames': 1000})
    expect('heat map:' in r.text and 'fetch' in r.text and 'zx_dma' in r.text, 'every channel in the text', r.text)
    expect('in the last 1000 frames' in r.text, 'the recent ranges', r.text)
    expect('data' not in r.structured['channels'][0], 'the grid left out by default here', r.structured['channels'][0])
    status, body = rest(s, '/state/audio/gs/heatmap?cells=256&channels=fetch')
    expect(status == 200 and body['data_cells'] == 256 and len(body['channels']) == 1, 'REST: one channel, 256 cells',
           (status, body.get('data_cells')))
    expect(len(body['channels'][0]['data']) == 344, 'REST: the grid in base64 by default', body['channels'][0])
    status, body = rest(s, '/state/audio/gs/coverage?channels=x&space=bogus')
    expect(status == 400, 'REST: a bad parameter is 400', (status, body))
    status, body = rest(s, '/state/audio/gs/coverage?cpu=ms')
    expect(status == 404, 'REST: no MultiSound fitted', (status, body))


def test_reset_clears_and_release_frees(s):
    r = s.call('gs_analysis', {'action': 'reset'})
    expect(not r.is_error, 'reset', r.text)
    r = s.call('gs_analysis', {'action': 'coverage'})
    expect(all(sp['totals']['exec'] == 0 for sp in r.structured['spaces']), 'cleared (the machine paused)',
           r.structured['spaces'])
    r = s.call('gs_analysis', {'action': 'off'})
    expect('analysis off' in r.text, 'off keeps the data', r.text)
    r = s.call('gs_analysis', {'action': 'release'})
    expect(r.structured.get('allocated') is False, 'release frees the arrays', r.text)
    r = s.call('gs_analysis', {'action': 'nope'}, ok=False)
    expect(r.is_error, 'an unknown action is refused', r.text)


if __name__ == '__main__':
    sys.exit(main(globals()))
