#!/usr/bin/env python3
"""time_travel on the sound card's CPU against oracles that do not come from a replay: the NeoGS test program
(test_ngs, it switches the card's clock) runs with a card breakpoint, and every live stop's card instruction
count, card registers and main CPU registers are kept. Rebuilt from the recording by card_seek, card_step back
and forward, they must equal the live machine. A frame start (a checkpoint, no card replay) must equal the card
reached at its count by card_seek and by a step; card_reverse_continue must land where single steps back first
meet the breakpoint's address"""

import os
import re

from mcplib import expect, main

LIVE = []  # (count, card registers, main registers) at each live stop
SCL = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..',
                   'testdata', 'sound', 'neogs', 'programs', 'test_ngs.scl')


def api(s, method, path, body=None):
    args = {'method': method, 'path': '/api/v1/emulator/{id}' + path}
    if body is not None:
        args['body'] = body
    return (s.call('invoke_api', args, ok=False).structured or {}).get('body', {})


def card(s):
    cpu = api(s, 'GET', '/state/audio/gs').get('cpu')
    expect(isinstance(cpu, dict) and isinstance(cpu.get('pc'), int), 'the card CPU state', cpu)
    return cpu


def count(s):
    r = s.call('time_travel', {'action': 'card_position'})
    match = re.search(r'Card gs: instruction (\d+)', r.text)
    expect(match, "the card's instruction count", r.text)
    return int(match.group(1))


def card_seek(s, n):
    r = s.call('time_travel', {'action': 'card_seek', 'cpu': 'gs', 'instruction': n})
    expect(f'at card instruction {n}' in r.text, f'card_seek {n}', r.text)


def setup(s):
    s.fresh_machine()
    s.call('control_execution', {'action': 'bp_clear', 'cpu': 'gs'}, ok=False)
    # The autostart resets into TR-DOS (a reset ends a session): record after it
    api(s, 'POST', '/disk/A/insert', {'path': os.path.abspath(SCL), 'autostart': True})
    s.call('control_execution', {'action': 'run_frames', 'frames': 5})
    s.call('control_execution', {'action': 'pause'}, ok=False)
    s.call('time_travel', {'action': 'start'})
    s.call('control_execution', {'action': 'run_frames', 'frames': 120})
    s.call('control_execution', {'action': 'pause'}, ok=False)
    pc = card(s)['pc']
    s.call('control_execution', {'action': 'bp_add', 'address': pc, 'cpu': 'gs'})
    for i in range(5):
        s.call('control_execution', {'action': 'run_frames', 'frames': 3 + 7 * i}, ok=False)
        LIVE.append((count(s), card(s), api(s, 'GET', '/state/registers')))
    s.call('control_execution', {'action': 'bp_clear', 'cpu': 'gs'})
    s.call('control_execution', {'action': 'run_frames', 'frames': 200})
    s.call('control_execution', {'action': 'pause'}, ok=False)
    s.call('time_travel', {'action': 'stop'}, ok=False)


def test_a_card_seek_rebuilds_every_live_stop(s):
    for i, (n, live_card, live_main) in enumerate(LIVE):
        card_seek(s, n)
        expect(card(s) == live_card, f'stop {i}: the card registers of the live machine', (card(s), live_card))
        expect(api(s, 'GET', '/state/registers') == live_main, f'stop {i}: the main CPU registers of the live machine')


def test_steps_back_and_forward_reach_the_live_stop(s):
    (n1, _, _), (n2, card2, main2), (n3, _, _) = LIVE[1], LIVE[2], LIVE[3]
    card_seek(s, n3)
    s.call('time_travel', {'action': 'card_step', 'cpu': 'gs', 'count': -(n3 - n2)})
    expect(card(s) == card2 and api(s, 'GET', '/state/registers') == main2, 'a step back from a later stop')
    card_seek(s, n1)
    for _ in range(n2 - n1):
        s.call('time_travel', {'action': 'card_step', 'cpu': 'gs', 'count': 1})
    expect(card(s) == card2 and api(s, 'GET', '/state/registers') == main2, 'single steps forward from an earlier stop')


def test_a_frame_start_equals_the_card_at_its_count(s):
    status = api(s, 'GET', '/ttd/status')
    first, last = status['session_start_frame'], status['current_end_frame']
    expect(last - first > 300, 'the recording is long', status)
    for frame in range(first + 7, last - 2, (last - first) // 8):
        s.call('time_travel', {'action': 'seek', 'frame': frame})
        n, at_checkpoint = count(s), card(s)
        s.call('time_travel', {'action': 'seek', 'frame': last - 1})
        card_seek(s, n)
        expect(card(s) == at_checkpoint, f'frame {frame}: card_seek to #{n} = the checkpoint', (card(s), at_checkpoint))
        card_seek(s, n - 1)
        s.call('time_travel', {'action': 'card_step', 'cpu': 'gs', 'count': 1})
        expect(card(s) == at_checkpoint, f'frame {frame}: a step from #{n - 1} = the checkpoint')


def test_reverse_continue_meets_single_steps_back(s):
    n = LIVE[-1][0] + 400
    card_seek(s, n)
    pc = card(s)['pc']
    s.call('control_execution', {'action': 'bp_add', 'address': pc, 'cpu': 'gs'})
    for rep in range(3):
        start = count(s)
        expected = None
        for _ in range(2000):
            s.call('time_travel', {'action': 'card_step', 'cpu': 'gs', 'count': -1})
            if card(s)['pc'] == pc:
                expected, expected_card = count(s), card(s)
                break
        expect(expected is not None, f'rep {rep}: single steps back meet {pc:#x}')
        card_seek(s, start)
        s.call('time_travel', {'action': 'card_reverse_continue', 'cpu': 'gs', 'max_frames': 200})
        expect(count(s) == expected and card(s) == expected_card,
               f'rep {rep}: reverse continue from #{start} stops at #{expected}', count(s))
    s.call('control_execution', {'action': 'bp_clear', 'cpu': 'gs'})
    s.call('time_travel', {'action': 'invalidate'}, ok=False)


if __name__ == '__main__':
    raise SystemExit(main(globals()))
