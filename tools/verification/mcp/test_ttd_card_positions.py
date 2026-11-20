#!/usr/bin/env python3
"""time_travel on a sound card's CPU (gs-debugger design §8): record the PENTAGON with its NeoGS running the card's
firmware, then card_position names the card's instruction count, card_step goes N card instructions back and
forward (the main CPU follows by replay and stands at its last instruction start with the T-states into the next),
card_seek reaches a count, and every way of getting to one count gives the same card registers: a step back, a step
forward from earlier, a WebAPI seek. card_reverse_continue finds the card breakpoint's previous hit and says so when
there is none"""

import re

from mcplib import expect, main


def card_cpu(s):
    """The card CPU's registers (GET state/audio/gs): the dict the comparisons use"""
    r = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/emulator/{id}/state/audio/gs'})
    cpu = (r.structured or {}).get('body', {}).get('cpu')
    expect(isinstance(cpu, dict) and isinstance(cpu.get('pc'), int), 'the card CPU state', r.text)
    return cpu


def instruction_of(r):
    match = re.search(r'Card gs: instruction (\d+)', r.text)
    expect(match, "the card's instruction in the text", r.text)
    return int(match.group(1))


def setup(s):
    s.fresh_machine()
    s.call('control_execution', {'action': 'bp_clear', 'cpu': 'gs'}, ok=False)
    r = s.call('time_travel', {'action': 'start'})
    expect('recording started' in r.text.lower(), 'recording', r.text)
    s.call('control_execution', {'action': 'run_frames', 'frames': 12})
    s.call('control_execution', {'action': 'pause'}, ok=False)


def test_card_position_reads_the_count(s):
    r = s.call('time_travel', {'action': 'card_position'})
    expect('Card gs: instruction ' in r.text and 'as the card last ran' in r.text, 'the live card', r.text)
    r = s.call('time_travel', {'action': 'card_position', 'cpu': 'ms'})
    expect("No card CPU 'ms'" in r.text, 'no MultiSound on this machine, said so', r.text)


def test_step_back_and_forward_give_the_same_card(s):
    now = instruction_of(s.call('time_travel', {'action': 'card_position'}))
    target = now - 30000
    r = s.call('time_travel', {'action': 'card_seek', 'cpu': 'gs', 'instruction': target})
    expect(f'gs at card instruction {target}' in r.text and 'card position; main CPU at' in r.text,
           'the card moved; the text says where both CPUs stand', r.text)
    at_target = card_cpu(s)

    r = s.call('time_travel', {'action': 'card_step', 'cpu': 'gs', 'count': -5})
    expect(f'gs at card instruction {target - 5} (from {target})' in r.text, 'five card instructions back', r.text)
    back = card_cpu(s)
    expect(back != at_target, 'the card registers changed', (back, at_target))

    # The WebAPI's seek to the same count: the same card
    r = s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/emulator/{id}/ttd/seek',
                              'body': {'cpu': 'gs', 'instruction': target - 5}})
    body = (r.structured or {}).get('body', {})
    expect(body.get('reached') is True and body.get('card', {}).get('instruction') == target - 5,
           'the WebAPI seek reached it', r.text)
    expect(card_cpu(s) == back, 'WebAPI seek and MCP step back give the same card registers', (card_cpu(s), back))

    # Forward again: the forward replay's card at the target
    r = s.call('time_travel', {'action': 'card_step', 'cpu': 'neogs', 'count': 5})
    expect(f'at card instruction {target}' in r.text, 'five forward', r.text)
    expect(card_cpu(s) == at_target, 'stepping forward gives the card the target had', (card_cpu(s), at_target))

    # Single steps back, each one instruction
    for k in range(1, 4):
        r = s.call('time_travel', {'action': 'card_step', 'cpu': 'gs', 'count': -1})
        expect(f'at card instruction {target - k}' in r.text, f'step back {k}', r.text)

    # The position readout of the WebAPI: both CPUs
    r = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/emulator/{id}/ttd/position'})
    cards = (r.structured or {}).get('body', {}).get('cards', [])
    expect(cards and cards[0].get('card_position') is True and cards[0].get('instruction') == target - 3
           and 'frame' in cards[0].get('main', {}), 'GET ttd/position names the card position', r.text)


def test_main_cpu_moves_leave_the_card_position(s):
    s.call('time_travel', {'action': 'step_back_instruction'})
    r = s.call('time_travel', {'action': 'card_position'})
    expect('as the card last ran' in r.text, 'a main CPU move ends the card position', r.text)


def test_reverse_continue_to_a_card_breakpoint(s):
    r = s.call('time_travel', {'action': 'card_reverse_continue', 'cpu': 'gs'}, ok=False)
    expect(r.is_error and 'breakpoint' in r.text, 'no card breakpoints: refused, named', r.text)
    now = instruction_of(s.call('time_travel', {'action': 'card_position'}))
    s.call('time_travel', {'action': 'card_seek', 'cpu': 'gs', 'instruction': now - 20000})
    pc = card_cpu(s)['pc']
    s.call('time_travel', {'action': 'card_step', 'cpu': 'gs', 'count': 50})
    s.call('control_execution', {'action': 'bp_add', 'address': pc, 'cpu': 'gs'})
    r = s.call('time_travel', {'action': 'card_reverse_continue', 'cpu': 'gs', 'max_frames': 20})
    expect('breakpoint' in r.text and 'execute' in r.text, 'the previous hit, its breakpoint named', r.text)
    expect(card_cpu(s)['pc'] == pc, 'the card stands at the breakpoint address', card_cpu(s))
    # The WebAPI's form (no pcs: the card's own breakpoints): the hit before this one
    r = s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/emulator/{id}/ttd/reverse-continue',
                              'body': {'cpu': 'gs', 'max_frames': 20}})
    body = (r.structured or {}).get('body', {})
    expect(body.get('reached') is True and body.get('breakpoint', {}).get('address') == pc
           and body.get('card', {}).get('pc') == pc, 'POST ttd/reverse-continue {cpu} finds the earlier hit', r.text)
    s.call('control_execution', {'action': 'bp_clear', 'cpu': 'gs'})
    s.call('control_execution', {'action': 'bp_add', 'address': '0x3FFF', 'cpu': 'gs'})
    r = s.call('time_travel', {'action': 'card_reverse_continue', 'cpu': 'gs', 'max_frames': 3}, ok=False)
    expect(r.is_error and 'did not move' in r.text and 'searched' in r.text, 'no hit: an error that says so', r.text)
    s.call('control_execution', {'action': 'bp_clear', 'cpu': 'gs'})


def test_refusals(s):
    r = s.call('time_travel', {'action': 'card_seek', 'cpu': 'gs', 'instruction': 0}, ok=False)
    expect(r.is_error and 'at_start' in r.text, 'before the history', r.text)
    r = s.call('time_travel', {'action': 'card_step', 'cpu': 'z81'}, ok=False)
    expect(r.is_error and 'cpu' in r.text.lower(), 'an unknown CPU', r.text)
    r = s.call('time_travel', {'action': 'card_seek', 'cpu': 'multisound', 'instruction': 5}, ok=False)
    expect(r.is_error, 'no MultiSound card', r.text)
    s.call('time_travel', {'action': 'stop'}, ok=False)
    s.call('time_travel', {'action': 'invalidate'}, ok=False)


if __name__ == '__main__':
    raise SystemExit(main(globals()))
