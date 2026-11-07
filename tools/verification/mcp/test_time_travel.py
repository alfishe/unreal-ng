#!/usr/bin/env python3
"""time_travel: record, stop, the history in status, seek back, the machine state follows, rare coverage,
port events with their context, reverse-continue first per PC, memory-at the machine now, probe frames, the
timeline, the cause chain, find event, explain frame, diff of two moments, the replay check and the seek check,
bookmarks with notes and export"""

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


def test_cause_chain_follows_the_frame_counter(s):
    # The history of test_record_stop_seek: the ROM's IM 1 handler counts FRAMES (#5C78) every frame -
    # LD HL,(FRAMES) / INC HL / LD (FRAMES),HL - so its chain is a store of L, computed from the loaded byte, and so on
    # back one frame per round until the depth cuts it
    before = s.call('time_travel', {'action': 'status'}).text
    r = s.call('time_travel', {'action': 'cause_chain', 'addr': '0x5C78'})
    links = r.structured.get('links', [])
    expect(not r.is_error and links and links[0].get('kind') == 'store', 'the store first', r.text)
    kinds = [l.get('kind') for l in links]
    expect('computed' in kinds and 'load' in kinds, 'computed and load links', kinds)
    expect(any(l.get('stop') == 'depth' for l in links) and '[stop: depth]' in r.text, 'the depth cut said', r.text)
    expect(r.text.count('\n') >= len(links) and '(#5C78' in r.text and 'ld (' in r.text.lower(), 'one line per link', r.text)
    at_before = re.search(r'at frame (\d+)', before)
    at_after = re.search(r'at frame (\d+)', s.call('time_travel', {'action': 'status'}).text)
    expect((at_before and at_before.group(1)) == (at_after and at_after.group(1)), 'the position did not move',
           (before[:200], at_after))
    r = s.call('time_travel', {'action': 'cause_chain', 'reg': 'SP', 'depth': 1})
    expect(not r.is_error and len(r.structured.get('links', [])) >= 2, 'a register pair: both bytes', r.text)
    r = s.call('time_travel', {'action': 'cause_chain', 'reg': 'Q'}, ok=False)
    expect(r.is_error and 'reg' in r.text, 'a bad register named', r.text)
    r = s.call('time_travel', {'action': 'cause_chain'}, ok=False)
    expect(r.is_error and 'addr' in r.text, 'no target named', r.text)



def test_find_event_names_the_moment(s):
    # The history of test_record_stop_seek: the ROM's IM 1 handler counts FRAMES (#5C78) every frame
    before = s.call('time_travel', {'action': 'status'}).text
    r = s.call('time_travel', {'action': 'find_event', 'find': 'mem', 'addr': '0x5C78', 'cond': 'increased',
                               'limit': 3})
    hits = r.structured.get('hits', [])
    expect(not r.is_error and len(hits) == 3, 'three increases', r.text)
    expect(all(h.get('after') == (h.get('before') + 1) % 256 for h in hits), 'each by one', hits)
    expect(all('pc' in h and h.get('text') for h in hits), 'the writer of each', hits)
    expect(r.text.count('\n') >= 4 and '(#5C78' in r.text and 'increased' in r.text and 'more from frame' in r.text,
           'one line per hit and the summary', r.text)
    frames = [h['frame'] for h in hits]
    expect(frames == sorted(frames), 'oldest first', frames)
    r = s.call('time_travel', {'action': 'find_event', 'find': 'mem', 'addr': '0x5C78', 'cond': 'increased',
                               'order': 'last'})
    last = r.structured.get('hits', [])
    expect(len(last) == 1 and last[0]['frame'] > frames[-1], 'the last increase is newer', r.text)
    r = s.call('time_travel', {'action': 'find_event', 'find': 'exec', 'addr': '0x0038'})
    run = r.structured.get('hits', [])
    expect(not r.is_error and len(run) == 1 and run[0].get('pc') == 0x38, 'the first IM 1 entry', r.text)
    r = s.call('time_travel', {'action': 'find_event', 'find': 'key'})
    expect(not r.is_error and not r.structured.get('hits') and 'No hit' in r.text, 'no key: said so', r.text)
    at_before = re.search(r'at frame (\d+)', before)
    at_after = re.search(r'at frame (\d+)', s.call('time_travel', {'action': 'status'}).text)
    expect((at_before and at_before.group(1)) == (at_after and at_after.group(1)), 'the position did not move',
           (before[:200], at_after))
    r = s.call('time_travel', {'action': 'find_event', 'find': 'mem', 'addr': '0x5C78', 'cond': 'equals'}, ok=False)
    expect(r.is_error and 'value' in r.text, 'equals without value named', r.text)
    r = s.call('time_travel', {'action': 'find_event'}, ok=False)
    expect(r.is_error and 'find' in r.text, 'no kind named', r.text)

def test_explain_frame_tells_what_the_frame_did(s):
    # The history of test_record_stop_seek: the ROM's IM 1 handler at #0038 counts FRAMES (#5C78) and scans the
    # keyboard. The frame after the first one that ran it: on a Pentagon the INT comes at the frame end, so the handler
    # runs on into that frame (carried over from the frame before, which the session's first frame lacks)
    before = s.call('time_travel', {'action': 'status'}).text
    run = s.call('time_travel', {'action': 'find_event', 'find': 'exec', 'addr': '0x0038'}).structured.get('hits', [])
    expect(len(run) == 1, 'a frame with the IM 1 handler', run)
    frame = run[0]['frame'] + 1
    r = s.call('time_travel', {'action': 'explain_frame', 'frame': frame})
    v = r.structured
    expect(not r.is_error and v.get('frame') == frame, 'the frame explained', r.text)
    irqs = v.get('interrupts', [])
    expect(len(irqs) >= 1 and all(i.get('mode') == 1 and i.get('entry') == 0x38 for i in irqs), 'the IM 1 acknowledge',
           irqs)
    expect(any(i.get('returned') for i in irqs), 'back to the interrupted code', irqs)
    isr = [x for x in v.get('routines', []) if x.get('pc') == 0x38 and x.get('isr')]
    expect(len(isr) == 1 and isr[0].get('t', 0) > 0, 'the handler as a routine with its time', r.text)
    frames = [m for m in v.get('memory', []) if m.get('addr') == 0x5C78]
    expect(len(frames) == 1 and frames[0].get('changed', 0) >= 1 and frames[0].get('writers'),
           'FRAMES changed, with its writer', v.get('memory'))
    reads = [p for p in v.get('ports', []) if p.get('dir') == 'in']
    expect(reads and reads[0].get('count', 0) >= 8, 'the keyboard scan read the half-rows', v.get('ports'))
    t = v.get('time', {})
    expect(t.get('isr', 0) + t.get('halt', 0) + t.get('code', 0) == v.get('end_t'), 'the time adds up', t)
    expect(all(w in r.text for w in ('routines by time:', 'interrupt: ', 'IM 1', 'memory:', '#5C78', 'ports:')),
           'every section in the text', r.text)
    r = s.call('time_travel', {'action': 'explain_frame', 'frame': frame, 'sections': 'routines', 'limit': 2})
    expect(not r.is_error and len(r.structured.get('routines', [])) <= 2 and 'memory' not in r.structured
           and 'memory:' not in r.text, 'sections and limit', r.text)
    at_before = re.search(r'at frame (\d+)', before)
    at_after = re.search(r'at frame (\d+)', s.call('time_travel', {'action': 'status'}).text)
    expect((at_before and at_before.group(1)) == (at_after and at_after.group(1)), 'the position did not move',
           (before[:200], at_after))
    r = s.call('time_travel', {'action': 'explain_frame', 'sections': 'nosuch'}, ok=False)
    expect(r.is_error and 'section' in r.text, 'an unknown section named', r.text)

def test_diff_tells_what_differs_between_two_moments(s):
    # The history of test_record_stop_seek: FRAMES (#5C78) counts up once a frame, written by the ROM's IM 1 handler
    before = s.call('time_travel', {'action': 'status'}).text
    first = int(re.search(r'frames (\d+)\.\.', before).group(1))
    r = s.call('time_travel', {'action': 'diff', 'from_frame': first + 2, 'to_frame': first + 7})
    v = r.structured
    expect(not r.is_error and v.get('from', {}).get('frame') == first + 2 and v.get('to', {}).get('frame') == first + 7,
           'the two moments', r.text)
    expect(v.get('between', {}).get('frames') == 5, 'five frames between', v.get('between'))
    frames = [m for m in v.get('memory', {}).get('ranges', []) if m.get('address', 0) <= 0x5C78 < m.get('address', 0)
              + m.get('length', 0)]
    expect(len(frames) == 1 and frames[0].get('page') == 5, 'FRAMES in a range of RAM 5', v.get('memory'))
    w = frames[0].get('last_write', {}) if frames else {}
    expect(w.get('pc', 0x4000) < 0x4000 and w.get('text') and first + 2 <= w.get('frame', 0) <= first + 7,
           'its last write: the ROM, between the moments', frames)
    expect(all(x in r.text for x in ('diff f', '5 frame(s)', 'memory:', 'last write', 'devices:')),
           'the sections in the text', r.text)
    r = s.call('time_travel', {'action': 'diff', 'from_frame': first + 2, 'from_tinframe': 30000,
                               'to_frame': first + 2, 'to_tinframe': 1000, 'sections': 'cpu'})
    v = r.structured
    expect(not r.is_error and v.get('backwards') and 'memory' not in v and v.get('registers'),
           'inside one frame, backwards, the cpu section only', r.text)
    expect(v.get('from', {}).get('tinframe', 0) >= 30000, 'the boundary at or after the T asked for', v.get('from'))
    at_before = re.search(r'at frame (\d+)', before)
    at_after = re.search(r'at frame (\d+)', s.call('time_travel', {'action': 'status'}).text)
    expect((at_before and at_before.group(1)) == (at_after and at_after.group(1)), 'the position did not move',
           (before[:200], at_after))
    r = s.call('time_travel', {'action': 'diff'}, ok=False)
    expect(r.is_error and 'from_frame' in r.text, 'no first moment named', r.text)
    r = s.call('time_travel', {'action': 'diff', 'from_frame': first + 2, 'sections': 'nosuch'}, ok=False)
    expect(r.is_error and 'section' in r.text, 'an unknown section named', r.text)


def test_replay_check_and_seek_check(s):
    # The history of test_record_stop_seek: a ROM's frames replay as they were recorded
    before = s.call('time_travel', {'action': 'status'}).text
    first = int(re.search(r'frames (\d+)\.\.', before).group(1))
    r = s.call('time_travel', {'action': 'replay_check', 'from_frame': first, 'to_frame': first + 9})
    v = r.structured
    expect(not r.is_error and v.get('checked', 0) >= 8 and v.get('diverged') == 0, 'ten frames, none diverged', r.text)
    expect('replay check f' in r.text and '0 diverged' in r.text, 'the summary in the text', r.text)
    r = s.call('time_travel', {'action': 'replay_check', 'from_frame': first, 'to_frame': first + 9, 'stride': 3})
    expect(not r.is_error and r.structured.get('checked', 0) + sum(r.structured.get('skipped', {}).values()) == 4,
           'frames +0, +3, +6, +9', r.text)
    r = s.call('time_travel', {'action': 'replay_check', 'stride': 0}, ok=False)
    expect(r.is_error and 'stride' in r.text, 'a zero stride refused', r.text)

    r = s.call('time_travel', {'action': 'seek_check_on'})
    expect(not r.is_error and 'seek check' in r.text and ': on' in r.text, 'switched on', r.text)
    r = s.call('time_travel', {'action': 'seek', 'frame': first + 4, 'tinframe': 500})
    expect(not r.is_error and 'replay matches the recording' in r.text, 'the seek says its frame replays exactly',
           r.text)
    r = s.call('time_travel', {'action': 'seek_check_off'})
    expect(not r.is_error and ': off' in r.text and '1 seek(s) checked, 0 diverged' in r.text, 'the counts', r.text)
    r = s.call('time_travel', {'action': 'seek', 'frame': first + 5, 'tinframe': 500})
    expect(not r.is_error and 'replay' not in r.text, 'off: the seek checks nothing', r.text)


def test_bookmarks_keep_notes_context_and_export(s):
    # The history of test_record_stop_seek: FRAMES (#5C78) counts up once a frame
    before = s.call('time_travel', {'action': 'status'}).text
    first = int(re.search(r'frames (\d+)\.\.', before).group(1))
    r = s.call('time_travel', {'action': 'bookmark_add', 'label': 'mcp-frames', 'frame': first + 3, 'tinframe': 1000,
                               'note': 'FRAMES counts here', 'tags': ['rom', 'counter'], 'watch': ['#5C78:3']})
    b = r.structured.get('bookmark', {})
    ctx = b.get('context') or {}
    expect(not r.is_error and b.get('note') == 'FRAMES counts here' and b.get('tags') == ['rom', 'counter'],
           'the note and tags kept', r.text)
    expect(ctx.get('instruction') and 'PC #' in r.text, 'the context taken: the instruction at PC', r.text)
    watch = b.get('watch') or [{}]
    expect(watch[0].get('address') == 0x5C78 and len(watch[0].get('bytes', [])) == 3, 'the watched bytes', watch)
    at_after = re.search(r'at frame (\d+)', s.call('time_travel', {'action': 'status'}).text)
    at_before = re.search(r'at frame (\d+)', before)
    expect((at_before and at_before.group(1)) == (at_after and at_after.group(1)), 'the position did not move',
           (before[:200], at_after))
    r = s.call('time_travel', {'action': 'bookmark_note', 'label': 'mcp-frames', 'note': 'in the IM 1 handler',
                               'append': True, 'remove_tags': ['counter']})
    b = r.structured.get('bookmark', {})
    expect(not r.is_error and b.get('note') == 'FRAMES counts here\nin the IM 1 handler' and b.get('tags') == ['rom'],
           'the note appended, a tag removed', r.text)
    r = s.call('time_travel', {'action': 'bookmark_list', 'tag': 'rom'})
    expect(not r.is_error and 'mcp-frames' in r.text and 'FRAMES counts here' in r.text, 'the list shows the note',
           r.text)
    r = s.call('time_travel', {'action': 'bookmark_export', 'format': 'markdown', 'title': 'MCP test', 'tag': 'rom'})
    expect(not r.is_error and r.structured.get('count') == 1 and '### mcp-frames' in r.text and '`#5C78' in r.text,
           'the markdown section with the watched bytes in a code span', r.text)
    r = s.call('time_travel', {'action': 'bookmark_export', 'format': 'profile', 'tag': 'rom'})
    expect(not r.is_error and all(line.startswith('#') for line in r.structured.get('text', 'x').splitlines()),
           'the profile comment lines', r.text)
    r = s.call('time_travel', {'action': 'bookmark_export', 'format': 'nosuch'}, ok=False)
    expect(r.is_error and 'format' in r.text, 'an unknown format named', r.text)
    r = s.call('time_travel', {'action': 'bookmark_note', 'label': 'nosuch', 'note': 'x'}, ok=False)
    expect(r.is_error, 'an unknown label refused', r.text)
    r = s.call('time_travel', {'action': 'bookmark_add', 'label': 'mcp-bad', 'tags': ['a b']}, ok=False)
    expect(r.is_error and 'tag' in r.text, 'a bad tag refused', r.text)
    s.call('time_travel', {'action': 'bookmark_delete', 'label': 'mcp-frames'})


if __name__ == '__main__':
    sys.exit(main(globals()))
