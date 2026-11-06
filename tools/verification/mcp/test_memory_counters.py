#!/usr/bin/env python3
"""analyze_performance memory_counters / memory_watch: cells in the text, one cell in two spaces, a masked port watch"""

import re
import sys

from mcplib import expect, main


def start_session(s):
    s.call('control_execution', {'action': 'pause'})
    s.call('analyze_performance', {'action': 'memory_watch', 'op': 'clear'})
    s.call('analyze_performance', {'action': 'profiler', 'profiler': 'memory', 'op': 'start'})


def test_top_cells_in_the_text(s):
    start_session(s)
    s.call('control_execution', {'action': 'run_frames', 'frames': 5})
    r = s.call('analyze_performance', {'action': 'memory_counters', 'view': 'top', 'kinds': 'x', 'limit': 8})
    cells = re.findall(r'^0x[0-9A-F]{4,} r=\d+ w=\d+ x=(\d+)$', r.text, re.M)
    expect(len(cells) == 8, 'eight cells as text lines', r.text[:600])
    expect([int(x) for x in cells] == sorted((int(x) for x in cells), reverse=True), 'heaviest first', r.text[:600])
    expect(len(r.structured['cells']) == 8, 'the same cells in structuredContent', r.structured)


def test_one_cell_in_cpu_and_phys(s):
    start_session(s)
    s.call('control_execution', {'action': 'run_frames', 'frames': 5})
    top = s.call('analyze_performance', {'action': 'memory_counters', 'view': 'top', 'kinds': 'x', 'limit': 1})
    address, reads, writes, executes = top.structured['cells'][0][:4]
    r = s.call('analyze_performance', {'action': 'memory_counters', 'addresses': hex(address)})
    expect(r.structured['cells'][0][3] == executes, 'the cell by address', r.text[:300])
    r = s.call('analyze_performance', {'action': 'memory_counters', 'space': 'phys', 'view': 'top', 'kinds': 'x',
                                       'limit': 1})
    expect(re.search(r'\((ram|rom|cache|misc)\d* 0x[0-9A-F]{4}\)', r.text), 'phys cells name their page', r.text[:300])
    r = s.call('analyze_performance', {'action': 'memory_counters', 'space': 'nowhere'}, ok=False)
    expect(r.is_error, 'an unknown space is refused', r.text[:300])


def test_masked_port_watch(s):
    start_session(s)
    r = s.call('analyze_performance', {'action': 'memory_watch', 'op': 'add', 'name': 'ula', 'port': '0xFE',
                                       'mask': '0x00FF', 'values': True})
    expect('ula' in r.text, 'the watch is listed', r.text)
    s.call('control_execution', {'action': 'run_frames', 'frames': 20})
    r = s.call('analyze_performance', {'action': 'memory_watch'})
    watch = next(w for w in r.structured['watches'] if w['name'] == 'ula')
    expect(watch['reads'] > 0, 'the ROM reads the keyboard through #FE', r.text)
    expect(str(watch['reads']) in r.text, 'the counts in the text', r.text)
    r = s.call('analyze_performance', {'action': 'memory_watch', 'op': 'add', 'name': 'ula', 'port': '0xFE'}, ok=False)
    expect(r.is_error and 'exists' in r.text, 'a name is unique', r.text)
    s.call('analyze_performance', {'action': 'memory_watch', 'op': 'remove', 'name': 'ula'})
    r = s.call('analyze_performance', {'action': 'memory_watch'})
    expect('no watches' in r.text, 'removed', r.text)


if __name__ == '__main__':
    sys.exit(main(globals()))
