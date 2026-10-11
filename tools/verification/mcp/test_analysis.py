#!/usr/bin/env python3
"""analyze_performance: frame cost and profiler status in the text, coverage over a run"""

import sys

from mcplib import expect, main


def test_frame_cost_and_profiler_status(s):
    for action in ('frame_cost', 'profile_status'):
        r = s.call('analyze_performance', {'action': action})
        expect('=' in r.text, f'{action}: fields in the text', r.text)


def test_coverage_over_a_run(s):
    s.call('analyze_performance', {'action': 'coverage_start'})
    s.call('control_execution', {'action': 'run_frames', 'frames': 5})
    s.call('analyze_performance', {'action': 'coverage_stop'})
    r = s.call('analyze_performance', {'action': 'coverage_read'})
    expect(r.structured, 'coverage data', r.text)
    expect(any(ch.isdigit() for ch in r.text), 'numbers in the text', r.text[:300])


if __name__ == '__main__':
    sys.exit(main(globals()))
