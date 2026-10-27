#!/usr/bin/env python3
"""The sound card CPU as a debug target (gs-debugger design §4): control_execution bp_* with cpu "gs" set the card's
breakpoints (the NeoGS of the PENTAGON), a card stop parks the machine and wait names the CPU and the card instruction,
the lists are per CPU, and the card refuses what it has not (NextREG, the host's events)"""

import re
import time

from mcplib import expect, main


def setup(s):
    s.fresh_machine()
    r = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/emulator/{id}/state/audio/gs'}, ok=False)
    expect(not r.is_error, 'the PENTAGON fits a General Sound card (NeoGS)', r.text)


def clear(s):
    s.call('control_execution', {'action': 'pause'}, ok=False)
    s.call('control_execution', {'action': 'bp_clear', 'cpu': 'gs'})
    s.call('control_execution', {'action': 'bp_clear'})


def stop_of(s):
    """Resume and wait for the stop: (the wait's text, its pause object)"""
    s.call('control_execution', {'action': 'resume'})
    r = s.call('control_execution', {'action': 'wait', 'timeout_ms': 5000})
    return r.text, (r.structured or {}).get('pause', {})


def card_pc(s):
    """The instruction the card CPU is at (the machine paused)"""
    r = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/emulator/{id}/state/audio/gs'})
    pc = (r.structured or {}).get('body', {}).get('cpu', {}).get('pc')
    expect(isinstance(pc, int), 'the card CPU state has its pc', r.text)
    return pc


def test_card_breakpoint_parks_the_machine(s):
    clear(s)
    # Without an SD card the NeoGS loader polls it with interrupts off: the instruction it is at comes round at once
    pc = card_pc(s)
    r = s.call('control_execution', {'action': 'bp_add', 'address': pc, 'cpu': 'gs'})
    expect(re.search(r'#\d+', r.text), 'the id in the text', r.text)
    text, pause = stop_of(s)
    expect('at breakpoint' in text and 'of the gs CPU' in text, 'the wait names the card CPU', text)
    expect(pause.get('cpu') == 'gs' and pause.get('address') == pc and pause.get('cpu_pc') == pc,
           'the pause carries cpu and the card instruction', pause)
    expect(s.call('inspect_state', {'aspects': ['registers']}).text, 'the machine answers while parked')
    hits = s.call('control_execution', {'action': 'bp_list', 'cpu': 'gs'}).text
    expect(re.search(r'hits [1-9]', hits), 'counted on the card', hits)
    clear(s)


def test_lists_are_per_cpu(s):
    clear(s)
    s.call('control_execution', {'action': 'bp_add', 'type': 'event', 'event': 'card_command', 'cpu': 'gs',
                                 'note': 'gs-check'})
    s.call('control_execution', {'action': 'bp_add', 'address': '0x0038'})
    gs = s.call('control_execution', {'action': 'bp_list', 'cpu': 'gs'}).text
    main_list = s.call('control_execution', {'action': 'bp_list'}).text
    expect('(gs CPU)' in gs and 'event card_command' in gs and '0x0038' not in gs, 'the card list', gs)
    expect('gs-check' not in main_list and '0x0038' in main_list, 'the main list', main_list)
    clear(s)


def test_the_card_refuses_what_it_has_not(s):
    clear(s)
    r = s.call('control_execution', {'action': 'bp_add', 'type': 'nextreg', 'address': 7, 'cpu': 'gs'}, ok=False)
    expect(r.is_error, 'no NextREG on the card', r.text)
    r = s.call('control_execution', {'action': 'bp_add', 'type': 'event', 'event': 'host_command', 'cpu': 'gs'},
               ok=False)
    expect(r.is_error and 'main CPU' in r.text, "the host's event is the main CPU's", r.text)
    r = s.call('control_execution', {'action': 'bp_add', 'type': 'event', 'event': 'card_reply'}, ok=False)
    expect(r.is_error and 'gs CPU' in r.text, "the card's event is the card CPU's", r.text)
    r = s.call('control_execution', {'action': 'bp_list', 'cpu': 'z81'}, ok=False)
    expect(r.is_error and 'cpu' in r.text, 'an unknown CPU is refused', r.text)
    r = s.call('control_execution', {'action': 'bp_add', 'type': 'event', 'event': 'host_command'})
    expect(not r.is_error, "the host's event on the main CPU", r.text)
    clear(s)


def test_a_card_condition(s):
    """A condition on a card breakpoint reads the card's registers; bp_condition with cpu gs changes it, and the
    machine's own state (FD) is refused there"""
    clear(s)
    pc = card_pc(s)
    r = s.call('control_execution', {'action': 'bp_add', 'address': pc, 'cpu': 'gs', 'condition': 'PC != #%04X' % pc})
    expect(' if ' in r.text, 'the text names the condition', r.text)
    never = int(re.search(r'#(\d+)', r.text).group(1))
    s.call('control_execution', {'action': 'resume'})
    deadline = time.time() + 5
    listing = ''
    while time.time() < deadline:
        listing = s.call('control_execution', {'action': 'bp_list', 'cpu': 'gs'}).text
        if re.search(r'hits 0 matched [1-9]', listing):
            break
        time.sleep(0.05)
    s.call('control_execution', {'action': 'pause'})
    expect(re.search(r'hits 0 matched [1-9]', listing), 'a false condition matches without a hit', listing)

    r = s.call('control_execution', {'action': 'bp_condition', 'bp_id': never, 'cpu': 'gs', 'condition': 'FD == 10'},
               ok=False)
    expect(r.is_error and "the machine's" in r.text, "the machine's FD is refused on the card", r.text)
    pc = card_pc(s)
    r = s.call('control_execution', {'action': 'bp_add', 'address': pc, 'cpu': 'gs',
                                     'condition': 'PC == #%04X && M(#%04X) == M(#%04X)' % (pc, pc, pc)})
    held = int(re.search(r'#(\d+)', r.text).group(1))
    expect(held != never, 'a conditional breakpoint is never shared', r.text)
    text, pause = stop_of(s)
    expect(pause.get('cpu') == 'gs' and pause.get('cpu_pc') == pc, 'parked by the condition that holds', pause)
    r = s.call('control_execution', {'action': 'bp_condition', 'bp_id': never, 'cpu': 'gs', 'condition': ''})
    expect(not r.is_error, 'cleared on the card', r.text)
    clear(s)


def test_a_python_tap_on_the_card(s):
    """A Python callback on the card's port reads (cpu gs): MCP lists it with the CPU, and its hits count"""
    clear(s)
    code = ('import unreal_emulator as ue\n'
            'emu = ue.emu_get_selected()\n'
            'gs_seen = []\n'
            'gs_cb = emu.on_tap(lambda hit: gs_seen.append(hit["cpu"]), "port_in", 0, mask=0, cpu="gs", '
            'label="gs-check")\n'
            'print(gs_cb)\n')
    r = s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/python/exec', 'body': {'code': code}}, ok=False)
    if 'Python automation is not available' in r.text:
        print('        no Python in this build (ENABLE_PYTHON_AUTOMATION=OFF): not checked')
        return
    expect(not r.is_error, 'registered', r.text)
    s.call('control_execution', {'action': 'resume'})
    deadline = time.time() + 5
    while time.time() < deadline:
        r = s.call('control_execution', {'action': 'callbacks'})
        if re.search(r'\(gs CPU\) \(python "gs-check"\) active, policy suspend, hits [1-9]', r.text):
            break
        time.sleep(0.1)
    s.call('control_execution', {'action': 'pause'})
    expect(re.search(r'#(\d+) tap in #0000 \(gs CPU\) \(python "gs-check"\) active, policy suspend, hits [1-9]',
                     r.text), 'the card tap on one line, its hits counted', r.text)
    entry = next(e for e in r.structured['callbacks'] if e['label'] == 'gs-check')
    expect(entry['cpu'] == 'gs', 'cpu in structuredContent', entry)
    s.call('control_execution', {'action': 'callback_remove', 'callback_id': entry['id']})
    clear(s)


if __name__ == '__main__':
    raise SystemExit(main(globals()))
