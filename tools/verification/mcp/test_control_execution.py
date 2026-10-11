#!/usr/bin/env python3
"""control_execution: stepping and running report the registers, breakpoints are listed one per line and stop the
machine"""

import re
import sys

from mcplib import expect, main


def pc_of(text):
    match = re.search(r'PC=0x([0-9A-F]{4})', text)
    expect(match, 'PC in the text', text)
    return int(match.group(1), 16)


def test_step_reports_registers(s):
    s.call('memory_access', {'action': 'write', 'address': '0x8000', 'hex': '00 00 00 18 FB'})  # nop x3; jr -5
    s.call('control_execution', {'action': 'pause'})
    s.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/registers/pc', 'body': {'value': 0x8000}})
    r = s.call('control_execution', {'action': 'step'})
    expect(pc_of(r.text) == 0x8001, 'one nop stepped', r.text)
    r = s.call('control_execution', {'action': 'step_n', 'count': 3})
    expect(pc_of(r.text) == 0x8000, 'nop, nop, jr back', r.text)


def test_run_tstates_and_frames(s):
    r = s.call('control_execution', {'action': 'run_tstates', 'tstates': 1000})
    expect('t-states' in r.text and 'PC=' in r.text, 'ran and reports', r.text)
    r = s.call('control_execution', {'action': 'run_frames', 'frames': 2})
    expect(not r.is_error, 'frames run', r.text)


def test_breakpoint_listed_and_hit(s):
    s.call('control_execution', {'action': 'bp_clear'})
    r = s.call('control_execution', {'action': 'bp_add', 'address': '0x0038'})
    expect('feature' not in r.text or 'switched on' in r.text, 'the breakpoints feature is on after bp_add', r.text)
    r = s.call('control_execution', {'action': 'bp_list'})
    expect(re.search(r'#\d+ 0x0038 X hits 0', r.text), 'one line for the breakpoint', r.text)
    # ei; halt; jr -3 at 0x8000: the frame interrupt (IM 1, or IM 0 with #FF on the bus) runs RST 38 whatever the
    # machine was doing
    s.call('memory_access', {'action': 'write', 'address': '0x8000', 'hex': 'FB 76 18 FC'})
    s.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/registers/pc', 'body': {'value': 0x8000}})
    r = s.call('control_execution', {'action': 'run_frames', 'frames': 2})
    expect('0x0038 X hits 0' in s.call('control_execution', {'action': 'bp_list'}).text,
           'run_frames skips breakpoints (as the tool description says)', r.text)
    s.call('control_execution', {'action': 'resume'})
    r = s.call('control_execution', {'action': 'wait', 'timeout_ms': 5000})
    expect('at breakpoint' in r.text and '0038' in r.text, 'resume stops at it', r.text)
    r = s.call('control_execution', {'action': 'bp_list'})
    expect(re.search(r'0x0038 X hits [1-9]', r.text), 'counted', r.text)
    expect(pc_of(s.call('inspect_state', {'aspects': ['registers']}).text) == 0x0038, 'stopped at 0x0038')
    s.call('control_execution', {'action': 'bp_clear'})
    expect('0x0038' not in s.call('control_execution', {'action': 'bp_list'}).text, 'cleared')


def test_breakpoint_on_a_label(s):
    s.call('control_execution', {'action': 'bp_clear'})
    s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/emulator/{id}/labels',
                          'body': {'name': 'McpCheck.End', 'address': '0x8123'}})
    r = s.call('control_execution', {'action': 'bp_add', 'address': 'McpCheck.End'})
    expect(not r.is_error, 'a label names the address', r.text)
    r = s.call('control_execution', {'action': 'bp_list'})
    expect(re.search(r'#\d+ 0x8123 X', r.text), 'the breakpoint is at the label', r.text)
    r = s.call('control_execution', {'action': 'bp_add', 'address': 'NoSuchLabel'}, ok=False)
    expect(r.is_error and 'label' in r.text, 'an unknown label is refused and named as one', r.text)
    s.call('control_execution', {'action': 'bp_clear'})
    s.call('invoke_api', {'method': 'DELETE', 'path': '/api/v1/emulator/{id}/labels/McpCheck.End'}, ok=False)


def test_unknown_action_is_an_error(s):
    r = s.call('control_execution', {'action': 'fly'}, ok=False)
    expect(r.is_error, 'refused', r.text)


if __name__ == '__main__':
    sys.exit(main(globals()))
