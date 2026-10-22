#!/usr/bin/env python3
"""The debug tools on MCP: breakpoint selections and groups, a keyboard breakpoint that stops the ROM's key scan,
set_register, the profilers / traces / analyzers of analyze_performance, and the catalogue's debug actions"""

import re
import sys

from mcplib import expect, main


def test_selections_and_groups(s):
    s.call('control_execution', {'action': 'bp_clear'})
    for address in ('0x9000', '0x9010'):
        s.call('control_execution', {'action': 'bp_add', 'address': address, 'group': 'loop'})
    s.call('control_execution', {'action': 'bp_add', 'type': 'port_out', 'address': '0x7FFD', 'port_mask': '0x8002'})
    r = s.call('control_execution', {'action': 'bp_groups'})
    expect('loop: 2 of 2 on' in r.text, 'the group in the text', r.text)
    r = s.call('control_execution', {'action': 'bp_toggle', 'select': 'group:loop'})
    expect(r.structured.get('selected') == 2 and r.structured.get('active') is False, 'the group switched off',
           r.structured)
    expect('loop: 0 of 2 on' in s.call('control_execution', {'action': 'bp_groups'}).text, 'shown off')
    r = s.call('control_execution', {'action': 'bp_disable', 'select': 'out'})
    expect(r.structured.get('selected') == 1, 'the port breakpoint by kind', r.structured)
    r = s.call('control_execution', {'action': 'bp_remove', 'select': 'port'})
    expect(r.structured.get('removed') == 1, 'removed by kind', r.structured)
    r = s.call('control_execution', {'action': 'bp_enable', 'select': 'whatever'}, ok=False)
    expect(r.is_error, 'a bad selection is refused')
    s.call('control_execution', {'action': 'bp_clear'})


def test_keyboard_breakpoint_stops_the_rom_scan(s):
    s.call('control_execution', {'action': 'bp_clear'})
    r = s.call('control_execution', {'action': 'bp_add', 'type': 'keyboard', 'key': 'space'})
    expect(not r.is_error, 'a keyboard breakpoint is added', r.text)
    s.call('control_execution', {'action': 'resume'})
    s.call('type_input', {'action': 'press', 'key': 'space'})
    r = s.call('control_execution', {'action': 'wait', 'timeout_ms': 5000})
    expect('at breakpoint' in r.text, 'the ROM read the keyboard and saw SPACE: stopped', r.text)
    s.call('type_input', {'action': 'release_all'})
    s.call('control_execution', {'action': 'bp_clear'})
    s.call('control_execution', {'action': 'pause'})


def test_set_register(s):
    s.call('control_execution', {'action': 'pause'})
    r = s.call('control_execution', {'action': 'set_register', 'name': 'hl', 'value': '0x1234'})
    expect(re.search(r'HL=0x1234', r.text), 'the new register state in the text', r.text)
    r = s.call('control_execution', {'action': 'set_register', 'name': 'nosuch', 'value': '1'}, ok=False)
    expect(r.is_error, 'an unknown register is refused', r.text)


def test_profilers_traces_analyzers(s):
    r = s.call('analyze_performance', {'action': 'profiler', 'profiler': 'opcode', 'op': 'start'})
    expect(not r.is_error, 'opcode profiler started', r.text)
    r = s.call('analyze_performance', {'action': 'profiler', 'profiler': 'opcode', 'op': 'status'})
    expect(not r.is_error, 'status', r.text)
    s.call('analyze_performance', {'action': 'profiler', 'profiler': 'opcode', 'op': 'stop'})
    r = s.call('analyze_performance', {'action': 'aylog', 'op': 'start', 'capacity': 1024})
    expect(not r.is_error, 'AY log started', r.text)
    s.call('analyze_performance', {'action': 'aylog', 'op': 'stop'})
    r = s.call('analyze_performance', {'action': 'pchist', 'op': 'on'})
    expect(not r.is_error, 'PC history armed', r.text)
    r = s.call('analyze_performance', {'action': 'pchist', 'op': 'read', 'depth': 4})
    expect(not r.is_error, 'PC history read', r.text)
    s.call('analyze_performance', {'action': 'pchist', 'op': 'off'})
    r = s.call('analyze_performance', {'action': 'analyzers', 'op': 'list'})
    expect('trdos' in str(r.structured), 'the analyzers listed', r.structured)


def test_catalogue_debug_actions(s):
    s.call('control_execution', {'action': 'pause'})
    r = s.call('actions', {'action': 'run', 'id': 'prof.toggle', 'params': {'name': 'memory', 'on': True}})
    expect('prof.toggle: ok' in r.text, 'memory tracking on through the catalogue', r.text)
    s.call('actions', {'action': 'run', 'id': 'prof.toggle', 'params': {'name': 'memory', 'on': False}})
    r = s.call('actions', {'action': 'run', 'id': 'dbg.breakpoint_toggle_pc'})
    expect('dbg.breakpoint_toggle_pc: ok' in r.text, 'a breakpoint at PC', r.text)
    r = s.call('control_execution', {'action': 'bp_list'})
    expect(': 1' in r.text.splitlines()[0], 'one breakpoint listed', r.text)
    s.call('actions', {'action': 'run', 'id': 'dbg.breakpoint_toggle_pc'})
    r = s.call('control_execution', {'action': 'bp_list'})
    expect(': 0' in r.text.splitlines()[0], 'the second press removed it', r.text)
    r = s.call('actions', {'action': 'run', 'id': 'port.out', 'params': {'port': 'fe', 'value': '2'}})
    expect('port.out: ok' in r.text, 'border red through the preset', r.text)


if __name__ == '__main__':
    sys.exit(main(globals()))
