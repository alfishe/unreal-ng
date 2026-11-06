#!/usr/bin/env python3
"""analyze_performance cpuprofile: the CPU time profile of a small CALL loop as an agent reads it (the routine found
without labels and its share in the text, a mark, the per-frame series, the kit export), and the refusals"""

import json
import re
import sys

from mcplib import expect, main

# DI; LD SP,$FF00; loop: CALL $8100; JR loop   /   $8100: NOP; RET
LOOP = 'F3 31 00 FF CD 00 81 18 FB'
SUB = '00 C9'


def profile(s, op, ok=True, **options):
    return s.call('analyze_performance', dict(action='cpuprofile', op=op, **options), ok=ok)


def start_loop(s):
    s.call('control_execution', {'action': 'pause'})
    s.call('memory_access', {'action': 'write', 'address': '0x8000', 'hex': LOOP})
    s.call('memory_access', {'action': 'write', 'address': '0x8100', 'hex': SUB})
    s.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/registers/pc', 'body': {'value': 0x8000}})
    r = profile(s, 'start', reset=True)
    expect('CPU profile started' in r.text, 'started, said in the text', r.text)


def test_report_names_the_routine_and_its_share(s):
    start_loop(s)
    s.call('control_execution', {'action': 'run_frames', 'frames': 10})
    r = profile(s, 'report', top=5)
    # NOP 4 + RET 10 of the 43 T a loop: 32.6 % self in sub_8100
    line = re.search(r'^\s+([\d.]+)%\s+([\d.]+)%\s+(\d+)\s+sub_8100 \(\$8100, ', r.text, re.M)
    expect(line, 'the routine found without labels, as a text row', r.text)
    expect(abs(float(line.group(1)) - 32.6) < 0.3, 'its self share in the text', r.text)
    row = next(x for x in r.structured['routines'] if x['entry'] == '$8100')
    expect(row['kinds'] == 'call' and row['calls'] > 1000, 'entered by calls', row)
    expect(row['self'] == row['inclusive'], 'a leaf: self = inclusive', row)
    expect(r.structured['window']['frames'] >= 9, 'the window counts the frames', r.structured['window'])


def test_mark_frames_and_status(s):
    start_loop(s)
    s.call('control_execution', {'action': 'run_frames', 'frames': 5})
    r = profile(s, 'mark', name='later')
    expect('Mark later set' in r.text, 'the mark in the text', r.text)
    s.call('control_execution', {'action': 'run_frames', 'frames': 3})
    r = profile(s, 'report', since='later')
    expect('since mark later' in r.text, 'the report since the mark', r.text)
    expect(2 <= r.structured['window']['frames'] <= 4, 'only the frames after the mark', r.structured['window'])
    r = profile(s, 'frames', last=3, top=2)
    expect(len(r.structured['frames']) == 3, 'three frames of the series', r.structured)
    expect(len(re.findall(r'^  frame \d+ \(\d+ active\): ', r.text, re.M)) == 3, 'one text line a frame', r.text)
    expect('sub_8100' in r.text, 'the frame lines name the routines', r.text)
    r = profile(s, 'status')
    expect('CPU profile running' in r.text and r.structured['active'], 'status running', r.text)
    expect('later' in r.structured['marks'], 'the mark listed', r.structured)


def test_exports(s):
    start_loop(s)
    s.call('control_execution', {'action': 'run_frames', 'frames': 3})
    r = profile(s, 'export', format='sym')
    expect('sub_8100 = $8100' in r.text, 'sym lines in the text', r.text)
    r = profile(s, 'export', format='kit')
    kit = json.loads(r.structured['content'])
    expect(kit['format'] == 'unreal-ng analysis L1', 'the kit is the L1 subset', kit.get('format'))
    expect(any(x['entry'] == '$8100' for x in kit['routines']), 'the routine in the kit', kit['routines'])
    rows = {row[0]: row for row in kit['instructions']}
    call = rows['$8004']
    expect(call[2] == 'CD0081', 'the CALL row carries its bytes', call)
    expect(abs(call[5] - call[4] * 17) <= 17, '17 T an execution (the last one may be unbooked)', call)


def test_stop_and_refusals(s):
    start_loop(s)
    r = profile(s, 'report', ok=False, since='nowhere')
    expect(r.is_error and 'no mark' in r.text, 'an unknown mark is refused with the reason', r.text)
    r = profile(s, 'start', ok=False, ring_kib=1)
    expect(r.is_error and 'ring_kib' in r.text, 'a bad ring size is refused', r.text)
    r = profile(s, 'jump', ok=False)
    expect(r.is_error, 'an unknown op is refused', r.text)
    s.call('control_execution', {'action': 'run_frames', 'frames': 2})
    r = profile(s, 'stop')
    expect('stopped' in r.text and not r.structured['active'], 'stopped, the counters kept', r.text)
    r = profile(s, 'status')
    expect(r.structured['frames'] == 2, 'a stop closes the last frame: two frames run, two in the window', r.structured)
    r = profile(s, 'frames', last=1)
    expect(len(r.structured['frames']) == 1, 'the series stays readable after stop', r.structured)


if __name__ == '__main__':
    sys.exit(main(globals()))
