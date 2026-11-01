#!/usr/bin/env python3
"""time_travel: record, stop, the history in status, seek back, the machine state follows, rare coverage,
port events with their context, reverse-continue first per PC, memory-at the machine now, probe frames, the
timeline"""

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


def test_reverse_continue_first_per_pc_names_the_pcs_without_a_hit(s):
    # The history of test_record_stop_seek: the ROM's IM 1 handler runs every frame, 0xFFF0 (UDG data) never
    r = s.call('time_travel', {'action': 'reverse_continue', 'pcs': ['0x0038', '0xFFF0'], 'first_per_pc': True})
    hits = r.structured.get('hits', [])
    expect(not r.is_error and len(hits) == 1 and hits[0].get('pc') == 0x0038, 'one hit, the IM 1 handler',
           r.structured)
    expect(r.structured.get('pcs_without_hit') == [0xFFF0], 'the PC that never ran is named', r.structured)
    expect('PC 0x0038' in r.text and 'No hit in the window: 0xFFF0' in r.text, 'both in the text', r.text)


def test_memory_at_cpu_without_frame_reads_the_machine_now(s):
    r = s.call('time_travel', {'action': 'memory_at', 'space': 'cpu', 'offset': '0x4000', 'length': 16})
    expect(not r.is_error and r.structured.get('live') is True and len(r.structured.get('hex', '')) == 32,
           'sixteen bytes of the machine now', r.structured)
    expect('(the machine now)' in r.text and '[' in r.text, 'live and the banks in the text', r.text)
    r = s.call('time_travel', {'action': 'memory_at', 'space': 'cpu', 'frame': 10_000_000}, ok=False)
    expect(r.is_error and 'frame' in r.text, 'a frame past the history is an error', r.text)


def test_port_events_carry_rows_and_registers(s):
    # The history of test_record_stop_seek: the ROM's KEY-SCAN reads the half-rows one by one
    r = s.call('time_travel', {'action': 'port_events', 'event': 'in', 'port': '0xFE', 'port_mask': '0xFF',
                               'limit': 3})
    hits = r.structured.get('hits', [])
    expect(not r.is_error and len(hits) == 3, 'three keyboard reads', r.text)
    expect(r.structured.get('context') is True and all('registers' in h and 'stack_top' in h for h in hits),
           'each hit has its registers', r.structured)
    expect(r.text.count('rows 0x') == 3 and r.text.count(' HL 0x') == 3 and r.text.count('(SP) 0x') == 3,
           'the rows and registers in the text', r.text)
    r = s.call('time_travel', {'action': 'port_events', 'event': 'in', 'port': '0xFE', 'port_mask': '0xFF',
                               'limit': 3, 'context': False})
    expect(not r.is_error and ' HL 0x' not in r.text and r.structured.get('context') is False,
           'context=false: no registers', r.text)



def test_probe_frames_lists_each_probes_frames_in_the_text(s):
    # The history of test_record_stop_seek: the ROM's IM 1 handler runs every frame, its KEY-SCAN reads #FE, 0xFFF0
    # (UDG data) never runs
    r = s.call('time_travel', {'action': 'probe_frames', 'probes': [
        {'probe': 'exec', 'address': '0x0038'}, {'probe': 'port-in', 'address': '0xFE', 'port_mask': '0xFF'},
        {'probe': 'exec', 'address': 0xFFF0}, {'probe': 'read', 'address': '0x5C00', 'if': 'VAL =='}]})
    probes = r.structured.get('probes', [])
    expect(not r.is_error and len(probes) == 4, 'four probes answered', r.text)
    frames = r.structured.get('to_frame', 0) - r.structured.get('from_frame', 0) + 1
    expect(probes[0].get('frame_count') == frames and probes[1].get('frame_count') == frames,
           'IM 1 and the keyboard scan in every frame', r.structured)
    expect(probes[2].get('frame_count') == 0 and 'error' in probes[3], 'one never ran, one did not compile',
           r.structured)
    expect('exec 0x0038: ' in r.text and 'frames ' in r.text and 'not armed' in r.text and
           'the machine did not move' in r.text, 'the frames and the error in the text', r.text)
    r = s.call('time_travel', {'action': 'probe_frames'}, ok=False)
    expect(r.is_error and 'probes' in r.text, 'no probes named', r.text)


def test_timeline_names_the_phases_in_the_text(s):
    # The history of test_record_stop_seek: the ROM editor waiting for a key (a busy loop on the flag the INT sets,
    # no HALT: core-tests TTDTimeline_Test holds the `halt` tag on a HALT-paced program)
    r = s.call('time_travel', {'action': 'timeline'})
    phases = r.structured.get('phases', [])
    expect(not r.is_error and phases, 'phases answered', r.text)
    expect(all(isinstance(p.get('halted_frames'), int) for p in phases), 'halted frames per phase', r.structured)
    expect(r.text.count(' fr, halted ') == len(phases), 'halted frames in the text', r.text)
    expect(r.text.count('busiest frame') == len(phases), 'every phase in the text', r.text)
    expect(any(d.get('name', '').startswith('Keyboard') for d in r.structured.get('devices', [])),
           'the port map devices', r.structured.get('devices'))
    expect('missing writes' in r.text, 'the write journal named as missing', r.text)
    expect(not r.structured.get('buckets'), 'no buckets unless asked', r.structured)
    r = s.call('time_travel', {'action': 'timeline', 'buckets': True, 'ports': False, 'limit': 4})
    expect(not r.is_error and 0 < len(r.structured.get('buckets', [])) <= 4, 'buckets on request', r.structured)
    expect('missing ports' in r.text, 'ports=false named', r.text)
    r = s.call('time_travel', {'action': 'timeline', 'from_frame': 10_000_000}, ok=False)
    expect(r.is_error and 'history' in r.text, 'a window past the history is an error', r.text)

if __name__ == '__main__':
    sys.exit(main(globals()))
