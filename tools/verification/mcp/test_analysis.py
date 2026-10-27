#!/usr/bin/env python3
"""analyze_performance: frame cost and profiler status in the text, coverage over a run and its export"""

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
    # The same coverage as SkoolKit's code execution map: one $HHHH line per instruction start
    r = s.call('analyze_performance', {'action': 'coverage_export', 'format': 'skoolkit-map'})
    lines = (r.structured or {}).get('map', '').splitlines()
    expect(lines and all(len(l) == 5 and l[0] == '$' for l in lines), 'a $HHHH line per address', r.text[:300])
    expect(r.structured.get('count') == len(lines), 'count = lines', r.text[:300])
    expect(lines[0] in r.text, 'the map in the text', r.text[:300])
    r = s.call('analyze_performance', {'action': 'coverage_export', 'format': 'fuse'}, ok=False)
    expect(r.is_error, 'an unknown format is refused', r.text[:300])


if __name__ == '__main__':
    sys.exit(main(globals()))
