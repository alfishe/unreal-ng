#!/usr/bin/env python3
"""time_travel on the sound card's CPU against oracles that do not come from a replay: the NeoGS test program
(test_ngs, it switches the card's clock) runs with a card breakpoint, and every live stop's card instruction
count, card registers and main CPU registers are kept. Rebuilt from the recording by card_seek, card_step back
and forward, they must equal the live machine. A frame start (a checkpoint, no card replay) must equal the card
reached at its count by card_seek and by a step; card_reverse_continue must land where single steps back first
meet the breakpoint's address. The firmware's wait loop calls no routine there, so the step over checks run on a
program of the card's own (nested routines planted in its RAM, a short recording of its own): card_reverse_step_over
must land where single steps back first meet the stack level again; card_step_over / card_step_out after the
routine single steps forward run under the level; card_run_to_cursor must land where single steps forward first meet
the cursor (never reached: an error, nothing moved). After a live card step at the history's end (the machine stands
inside the card's run) the moves in time work: a card step back, a frame step back"""

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


def below(sp, level):
    """sp is below the stack level (the stack grew), modulo 64K"""
    return ((sp - level) & 0xFFFF) >= 0x8000


def moved_to(r, what):
    match = re.search(r'at card instruction (\d+) \(from (\d+)\)', r.text)
    expect(match, what, r.text)
    return int(match.group(1))


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


PLANTED = []  # the card's own program's recording, made once: [its base address] when recorded


def plant_card_routines(s):
    """The card's own program for the step over tests, recorded: the firmware waits in a loop without a CALL for
    the whole recording above, so nested routines go into a RAM window of the card that takes writes, the card runs
    them with interrupts off, and a short history of its own is recorded (the checks above are done with theirs):
      B+#00 DI / B+#01 CALL B+#10 / B+#04 NOP / B+#05 JR B+#01
      B+#10 CALL B+#20 / B+#13 NOP / B+#14 RET
      B+#20 NOP / NOP / NOP / B+#23 RET"""
    if PLANTED:
        return
    s.call('control_execution', {'action': 'pause'}, ok=False)
    s.call('time_travel', {'action': 'invalidate'}, ok=False)
    r = s.call('inspect_state', {'aspects': ['memory_map'], 'cpu': 'gs'})
    windows = (r.structured or {}).get('memory_map', {}).get('windows', [])
    base = None
    for window in (w for w in windows if w.get('kind') == 'ram'):
        at = window['start'] + 0x100
        w = s.call('memory_access', {'action': 'write', 'space': 'gs', 'offset': '0x%04X' % at, 'hex': 'A55A'})
        rd = s.call('memory_access', {'action': 'read', 'space': 'gs', 'offset': '0x%04X' % at, 'length': 2})
        if re.search(r'A5\s*5A', rd.text.upper()) and 'kept by a read-only window' not in w.text:
            base = at
            break
    expect(base is not None, 'a RAM window of the card that takes writes', windows)

    def word(a):
        return '%02X%02X' % (a & 0xFF, a >> 8)
    chunks = {base: 'F3' + 'CD' + word(base + 0x10) + '00' + '18FA',
              base + 0x10: 'CD' + word(base + 0x20) + '00' + 'C9',
              base + 0x20: '000000C9'}
    for at, code in chunks.items():
        s.call('memory_access', {'action': 'write', 'space': 'gs', 'offset': '0x%04X' % at, 'hex': code})
    for name, value in (('pc', base), ('sp', base + 0x0F00)):
        r = s.call('control_execution', {'action': 'set_register', 'name': name, 'value': value, 'cpu': 'gs'})
        expect(not r.is_error, f"the card's {name} set", r.text)
    s.call('control_execution', {'action': 'run_frames', 'frames': 1})
    s.call('control_execution', {'action': 'pause'}, ok=False)
    s.call('time_travel', {'action': 'start'})
    s.call('control_execution', {'action': 'run_frames', 'frames': 6})
    s.call('control_execution', {'action': 'pause'}, ok=False)
    s.call('time_travel', {'action': 'stop'}, ok=False)
    PLANTED.append(base)


def find_routine_return(s, start):
    """The first card instruction from start whose step over back passes a routine: (n, landing, skipped)"""
    for n in range(start, start + 400):
        card_seek(s, n)
        r = s.call('time_travel', {'action': 'card_reverse_step_over', 'cpu': 'gs'})
        landing = moved_to(r, f'card_reverse_step_over from #{n}')
        over = re.search(r'stepped back over a routine of (\d+) instruction', r.text)
        if over:
            return n, landing, int(over.group(1))
        expect(landing == n - 1, f'#{n}: not over a routine, one instruction back', r.text)
    return None


def planted_start(s, frames_in):
    """A card instruction of the planted program's recording, `frames_in` frames after its start"""
    plant_card_routines(s)
    status = api(s, 'GET', '/ttd/status')
    s.call('time_travel', {'action': 'seek', 'frame': status['session_start_frame'] + frames_in})
    return count(s)


def test_reverse_step_over_meets_single_steps_back(s):
    found = find_routine_return(s, planted_start(s, 2))
    expect(found, 'a card routine return within 400 instructions')
    n, landing, skipped = found
    expect(skipped == n - 1 - landing, 'skipped names the routine passed', (n, landing, skipped))
    # Single steps back: the stack under the level until the landing, at it there
    card_seek(s, n)
    level = card(s)['sp']
    s.call('time_travel', {'action': 'card_step', 'cpu': 'gs', 'count': -1})
    expect(below(card(s)['sp'], level), 'the return is under the level')
    at = n - 1
    while below(card(s)['sp'], level) and at > landing - 1:
        s.call('time_travel', {'action': 'card_step', 'cpu': 'gs', 'count': -1})
        at -= 1
    expect(at == landing, f'single steps back from #{n} first meet the level at #{landing}', at)


def test_step_over_and_out_meet_single_steps_forward(s):
    found = find_routine_return(s, planted_start(s, 4))
    expect(found, 'a card routine return within 400 instructions')
    n, landing, _ = found
    card_seek(s, landing)
    level = card(s)['sp']
    r = s.call('time_travel', {'action': 'card_step_over', 'cpu': 'gs'})
    over = moved_to(r, f'card_step_over from #{landing}')
    expect(over == n, f'step over the CALL at #{landing} lands after its return at #{n}', over)
    # Single steps forward: inside the routine the stack is under the level, after it at the level again
    card_seek(s, landing)
    for at in range(landing + 1, n):
        s.call('time_travel', {'action': 'card_step', 'cpu': 'gs', 'count': 1})
        expect(below(card(s)['sp'], level), f'#{at} inside the routine', card(s))
    s.call('time_travel', {'action': 'card_step', 'cpu': 'gs', 'count': 1})
    expect(not below(card(s)['sp'], level), f'#{n} at the level again')
    # Out of the routine from its first instruction: the same place
    card_seek(s, landing + 1)
    r = s.call('time_travel', {'action': 'card_step_out', 'cpu': 'gs'})
    expect(moved_to(r, f'card_step_out from #{landing + 1}') == n, f'step out lands at #{n}', r.text)


def test_run_to_cursor_meets_single_steps_forward(s):
    start = planted_start(s, 3)
    cursor = PLANTED[0] + 0x20  # the innermost routine
    card_seek(s, start)
    expected = None
    for _ in range(64):
        s.call('time_travel', {'action': 'card_step', 'cpu': 'gs', 'count': 1})
        if card(s)['pc'] == cursor:
            expected, expected_card = count(s), card(s)
            break
    expect(expected is not None, f'single steps forward from #{start} meet #{cursor:04X}')
    card_seek(s, start)
    r = s.call('time_travel', {'action': 'card_run_to_cursor', 'cpu': 'gs', 'addr': '0x%04X' % cursor})
    expect(moved_to(r, f'card_run_to_cursor from #{start}') == expected and card(s) == expected_card,
           f'run to the cursor from #{start} lands at #{expected}', r.text)
    expect('at the cursor' in r.text, 'the text says the card is at the cursor', r.text)
    # Never reached (a byte of the planted window the program never runs): an error, nothing moved
    r = s.call('time_travel', {'action': 'card_run_to_cursor', 'cpu': 'gs', 'addr': PLANTED[0] + 0x30, 'max_frames': 50},
               ok=False)
    expect(r.is_error and 'no_hit' in r.text, 'never reached: no_hit', r.text)
    expect(count(s) == expected, 'not reached: the card where it stood', count(s))


def test_moves_after_a_live_card_step_leave_the_cards_run(s):
    plant_card_routines(s)
    end = api(s, 'GET', '/ttd/status')['current_end_frame']
    s.call('time_travel', {'action': 'seek', 'frame': end})
    r = s.call('control_execution', {'action': 'step', 'cpu': 'gs'})
    expect(not r.is_error, "a live card step at the history's end", r.text)
    r = s.call('time_travel', {'action': 'card_step', 'cpu': 'gs', 'count': -1}, ok=False)
    expect(not r.is_error and 'at card instruction' in r.text, 'a card step back right after it', r.text)
    s.call('time_travel', {'action': 'seek', 'frame': end})
    r = s.call('control_execution', {'action': 'step', 'cpu': 'gs'})
    expect(not r.is_error, "a live card step at the history's end again", r.text)
    r = s.call('time_travel', {'action': 'step_back_frame'}, ok=False)
    expect(not r.is_error, 'a frame step back right after it', r.text)
    status = api(s, 'GET', '/ttd/status')
    expect(status.get('state') == 'detached', 'in the history again', status)


if __name__ == '__main__':
    raise SystemExit(main(globals()))
