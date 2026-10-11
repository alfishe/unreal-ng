#!/usr/bin/env python3
"""time_travel: record, stop, the history in status, seek back, the machine state follows"""

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


if __name__ == '__main__':
    sys.exit(main(globals()))
