#!/usr/bin/env python3
"""analyze_performance section_timer: a section of fixed length timed at full speed as an agent reads it (the table
in the text, the net time exact with the frame interrupt landing inside, the passes on request), the pause at the
exit of a slow pass, and the refusals"""

import re
import sys

from mcplib import expect, main

# EI; Work: LD B,64; DJNZ $; Work.End: NOP; JR Work  -  Work takes 7 + 63 * 13 + 8 = 834 T, uncontended
LOOP = 'FB 06 40 10 FE 00 18 F9'
WORK_T = 834


def timer(s, op, ok=True, **options):
    return s.call('analyze_performance', dict(action='section_timer', op=op, **options), ok=ok)


def start_loop(s, **options):
    s.call('control_execution', {'action': 'pause'})
    s.call('memory_access', {'action': 'write', 'address': '0x8000', 'hex': LOOP})
    s.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/registers/pc', 'body': {'value': 0x8000}})
    r = timer(s, 'start', sections=['Work=0x8001:0x8005'], **options)
    expect('Work' in r.text and r.structured['active'], 'started, the section in the table', r.text)


def test_table_and_exact_net_time(s):
    start_loop(s)
    s.call('control_execution', {'action': 'run_frames', 'frames': 5})
    r = timer(s, 'status')
    row = re.search(r'^Work\s+#8001\.\.#8005\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)$', r.text, re.M)
    expect(row, 'a text row for the section', r.text)
    count, low, _, high, net_max, interrupted = (int(x) for x in row.groups())
    expect(count > 300 and low == WORK_T, 'every pass counted, the shortest one exact', r.text)
    expect(net_max == WORK_T, 'net time exact: the interrupts that landed inside are left out', r.text)
    expect(interrupted >= 3 and high > WORK_T, 'the frame interrupt lands inside some passes', r.text)
    expect(r.structured['passes'] == [], 'no passes unless asked for', r.structured['passes'][:3])
    r = timer(s, 'status', limit=3)
    expect(len(r.structured['passes']) == 3 and r.structured['passes'][0][0] == 'Work', 'the passes on request',
           r.structured['passes'])
    timer(s, 'stop')


def test_break_if_pauses_at_the_slow_pass(s):
    start_loop(s, break_if=['Work>{}'.format(WORK_T)])
    # run_frames skips the breakpoints, so it would not stop; a run that honours them does
    r = s.call('control_execution', {'action': 'run_until', 'condition': 'FRAME < 0', 'max_frames': 10,
                                     'breakpoints': True})
    expect('breakpoint' in r.text or r.structured.get('outcome') == 'breakpoint', 'the run ended at the stop', r.text)
    r = timer(s, 'status')
    stopped = r.structured['stopped']
    expect(stopped and stopped['section'] == 'Work' and stopped['net'] == WORK_T and stopped['elapsed'] > WORK_T,
           'stopped at the first interrupted pass', stopped)
    expect('stopped on Work>834' in r.text, 'the stop said in the text', r.text)
    pc = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/emulator/{id}/registers'}).structured
    expect('8005' in str(pc).upper() or '32773' in str(pc), 'paused at the exit, before its instruction', pc)
    timer(s, 'stop')


def test_refusals_and_stop(s):
    start_loop(s)
    r = timer(s, 'start', ok=False, sections=['NoSuchLabel'])
    expect(r.is_error and 'no label' in r.text, 'an unknown label is refused with the reason', r.text)
    r = timer(s, 'start', ok=False, sections=['Work=0x8001:0x8005'], break_if=['Work~5'])
    expect(r.is_error and 'break condition' in r.text, 'a bad condition is refused', r.text)
    r = timer(s, 'status')
    expect(r.structured['active'], 'a refused start leaves the running timer as it was', r.structured)
    r = timer(s, 'jump', ok=False)
    expect(r.is_error, 'an unknown op is refused', r.text)
    s.call('control_execution', {'action': 'run_frames', 'frames': 1})
    r = timer(s, 'stop')
    expect(not r.structured['active'] and r.structured['passes_total'] > 0, 'stopped, the results kept', r.text)
    r = timer(s, 'clear')
    expect(r.structured['passes_total'] == 0, 'clear drops them', r.structured)


if __name__ == '__main__':
    sys.exit(main(globals()))
