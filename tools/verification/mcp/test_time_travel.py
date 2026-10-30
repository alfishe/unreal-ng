#!/usr/bin/env python3
"""time_travel: record, stop, the history in status, seek back, the machine state follows, rare coverage"""

import re
import sys

from mcplib import expect, main


def test_record_stop_seek(s):
    r = s.call('time_travel', {'action': 'start'})
    expect('recording started' in r.text.lower(), 'started', r.text)
    s.call('control_execution', {'action': 'run_frames', 'frames': 20})
    s.call('time_travel', {'action': 'stop'})
    r = s.call('time_travel', {'action': 'status'})
    match = re.search(r'frames (\d+)\.\.(\d+)', r.text)
    expect(match and int(match.group(2)) - int(match.group(1)) >= 15, 'the recorded range in the text', r.text)
    first = int(match.group(1))
    r = s.call('time_travel', {'action': 'seek', 'frame': first + 5})
    expect(not r.is_error, 'seek inside the history', r.text)
    r = s.call('inspect_state', {'aspects': ['ttd']})
    at = re.search(r'at frame (\d+)', r.text)
    # A seek without tinframe lands at the frame's end: the position reads as the next frame
    expect(at and int(at.group(1)) in (first + 5, first + 6), 'the position in the text', r.text)


def test_seek_past_the_end_is_an_error_and_moves_nothing(s):
    before = s.call('time_travel', {'action': 'status'}).text
    r = s.call('time_travel', {'action': 'seek', 'frame': 10_000_000}, ok=False)
    expect(r.is_error and 'past the history' in r.text, 'an error naming the end', r.text)
    expect('frame 0' not in r.text, 'no "stopped at frame 0"', r.text)
    at_before = re.search(r'at frame (\d+)', before)
    at_after = re.search(r'at frame (\d+)', s.call('time_travel', {'action': 'status'}).text)
    expect((at_before and at_before.group(1)) == (at_after and at_after.group(1)), 'the position did not move',
           (before[:200], at_after))


def test_reverse_step_by_scanlines_and_the_build_cancel(s):
    # The history of test_record_stop_seek: back two scanlines from inside it
    r = s.call('time_travel', {'action': 'reverse_step', 'scanlines': 2})
    expect(not r.is_error and 'back' in r.text.lower(), 'stepped back two scanlines', r.text)
    expect(r.structured.get('mode') == 'scanlines' and r.structured.get('tstates', 0) > 0, 'the mode and t-states',
           r.structured)
    r = s.call('time_travel', {'action': 'reverse_step', 'scanlines': 1, 'count': 1}, ok=False)
    expect(r.is_error, 'one unit only', r.text)
    r = s.call('time_travel', {'action': 'journal_build_cancel'})
    expect(not r.is_error and 'journal' in r.text, 'nothing to cancel, said so', r.text)


def test_coverage_rare_lists_blocks_in_the_text(s):
    # The history of test_record_stop_seek: the ROM's code; every block is in the text with its frames
    r = s.call('time_travel', {'action': 'coverage_rare', 'max_frames': 3, 'limit': 5})
    expect(not r.is_error and 'addresses in at most 3 frames' in r.text, 'the totals in the text', r.text)
    blocks = r.structured.get('blocks', [])
    expect(r.structured.get('index_available') and r.structured.get('distinct_keys', 0) > 0, 'the index answered',
           r.structured)
    expect(len(blocks) <= 5 and r.text.count('frames ') >= len(blocks), 'each listed block in the text', r.text)
    r = s.call('time_travel', {'action': 'coverage_rare', 'max_frames': 0}, ok=False)
    expect(r.is_error and 'max_frames' in r.text, 'a bad bound named', r.text)


if __name__ == '__main__':
    sys.exit(main(globals()))
