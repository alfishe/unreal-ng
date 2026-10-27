#!/usr/bin/env python3
"""control_execution: stepping and running report the registers, breakpoints are listed one per line and stop the
machine; a host script over the WebAPI and an agent over MCP drive one machine (the implicit session, take-over,
release without the id)"""

import json
import re
import sys
import time
import urllib.error
import urllib.request

from mcplib import WebSocket, expect, main
from test_actions import instance, release_mcp_session, webapi


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


def test_conditional_breakpoint(s):
    s.call('control_execution', {'action': 'bp_clear'})
    r = s.call('control_execution', {'action': 'bp_add', 'type': 'write', 'address': '0x9000', 'condition': 'A == 7 &&'},
               ok=False)
    expect(r.is_error and 'column' in r.text, 'a condition that does not compile is refused with its column', r.text)
    r = s.call('control_execution', {'action': 'bp_add', 'type': 'execution', 'address': '0x9000', 'condition': 'VAL == 6'},
               ok=False)
    expect(r.is_error and 'trigger' in r.text, 'an execution breakpoint has no written value', r.text)
    # di; ld a,5; ld (#9000),a; ld a,6; ld (#9000),a; jr back to ld a,5: only the write of 6 stops
    s.call('memory_access', {'action': 'write', 'address': '0x8000', 'hex': 'F3 3E 05 32 00 90 3E 06 32 00 90 18 F3'})
    s.call('control_execution', {'action': 'pause'})
    s.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/registers/pc', 'body': {'value': 0x8000}})
    r = s.call('control_execution', {'action': 'bp_add', 'type': 'write', 'address': '0x9000', 'condition': 'val==6'})
    expect(re.search(r'#\d+ if VAL == 6', r.text), 'the text shows the condition as understood', r.text)
    bp_id = re.search(r'#(\d+) if', r.text).group(1)
    s.call('control_execution', {'action': 'resume'})
    r = s.call('control_execution', {'action': 'wait', 'timeout_ms': 5000})
    expect('at breakpoint' in r.text, 'the write of 6 stops', r.text)
    r = s.call('control_execution', {'action': 'bp_list'})
    expect(re.search(r'hits 1 matched 2 if VAL == 6', r.text), 'two writes matched, one held', r.text)
    r = s.call('control_execution', {'action': 'bp_condition', 'bp_id': bp_id, 'condition': 'VAL == 5'})
    expect(r.text == 'Condition set: VAL == 5', 'bp_condition answers the new condition', r.text)
    r = s.call('control_execution', {'action': 'bp_condition', 'bp_id': bp_id, 'condition': 'RD == 1'}, ok=False)
    expect(r.is_error and 'trigger' in r.text, 'checked like at creation', r.text)
    r = s.call('control_execution', {'action': 'bp_condition', 'bp_id': bp_id, 'condition': ''})
    expect(r.text == 'Condition cleared', 'an empty condition clears it', r.text)
    s.call('control_execution', {'action': 'bp_clear'})


def test_script_callbacks_listed_and_managed(s):
    """A Lua callback over budget is suspended; MCP lists it a line each and resumes, re-policies and removes it, and
    the WebSocket's debug.events carries callbacks_changed"""
    emu = instance(s)
    s.call('control_execution', {'action': 'pause'})
    s.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/feature/breakpoints', 'body': {'enabled': True}})
    r = s.call('control_execution', {'action': 'callbacks'})
    expect(r.text.startswith('Script callbacks: 0'), 'none yet', r.text)
    # ei; halt; ld (#9000),a; jr back: one write a frame. The function spins 4 ms, over its 1 ms budget each call
    s.call('memory_access', {'action': 'write', 'address': '0x8000', 'hex': 'FB 76 32 00 90 18 F9'})
    s.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/registers/pc', 'body': {'value': 0x8000}})
    code = ('mcp_cb, mcp_err = on_tap({write = 0x9000}, function(hit) local t = os.clock() '
            'while os.clock() - t < 0.004 do end end, {label = "mcp-check", budget_us = 1000}) print(mcp_cb, mcp_err)')
    r = s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/lua/exec', 'body': {'code': code}})
    s.call('control_execution', {'action': 'resume'})
    deadline = time.time() + 10
    while time.time() < deadline:
        r = s.call('control_execution', {'action': 'callbacks'})
        if ' suspended, ' in r.text:
            break
        time.sleep(0.1)
    s.call('control_execution', {'action': 'pause'})
    expect(re.search(r'#\d+ tap write #9000 \(lua "mcp-check"\) suspended, policy suspend, hits \d+', r.text),
           'the Lua callback, suspended over its budget, on one line (Lua on the selected instance ' + emu + ')', r.text)
    expect('; reason: 3 calls over 1000 us within 50 frames' in r.text and 'last overrun' in r.text,
           'the reason and the last overrun in the text', r.text)
    cb = re.search(r'#(\d+) tap write #9000', r.text).group(1)
    entry = r.structured['callbacks'][0]
    expect(entry['state'] == 'suspended' and entry['overruns'] >= 3 and entry['owner'] == 'lua',
           'the Lua table\'s fields in structuredContent', entry)

    ws = WebSocket(int(webapi(s).rsplit(':', 1)[1]))  # this host: debug.* without a token
    ws.send({'op': 'subscribe', 'topics': ['debug.events']})
    ws.until(lambda m: m.get('op') == 'subscribed', 'subscribed')
    r = s.call('control_execution', {'action': 'callback_policy', 'callback_id': int(cb), 'policy': 'ignore'})
    expect(r.text.startswith(f'Callback {cb} policy ignore: #{cb} tap write #9000') and 'policy ignore' in r.text,
           'the policy and the line', r.text)
    event = ws.until(lambda m: m.get('event') == 'callbacks_changed' and m.get('instance') == emu,
                     'callbacks_changed on debug.events')
    expect(event['id'] == int(cb) and event['callback']['policy'] == 'ignore', 'the callback as GET /callbacks has it',
           event)
    r = s.call('control_execution', {'action': 'callback_resume', 'callback_id': int(cb)})
    expect(r.text.startswith(f'Callback {cb} resumed: ') and ' active, ' in r.text and 'overruns 0' in r.text,
           'active again, fresh statistics', r.text)
    r = s.call('control_execution', {'action': 'callback_policy', 'callback_id': int(cb), 'policy': 'sometimes'},
               ok=False)
    expect(r.is_error and 'suspend, disable, ignore or unpaced' in r.text, 'an unknown policy is refused', r.text)
    r = s.call('control_execution', {'action': 'callback_remove', 'callback_id': int(cb)})
    expect(r.text == f'Callback {cb} removed', 'removed', r.text)
    event = ws.until(lambda m: m.get('event') == 'callbacks_changed' and m.get('removed') is True,
                     'the removal on debug.events')
    ws.close()
    r = s.call('control_execution', {'action': 'callback_resume', 'callback_id': int(cb)}, ok=False)
    expect(r.is_error and f'No callback {cb}' in r.text, 'gone', r.text)
    r = s.call('control_execution', {'action': 'callback_remove'}, ok=False)
    expect(r.is_error and 'callback_id' in r.text, 'callback_remove needs callback_id', r.text)
    expect(s.call('control_execution', {'action': 'callbacks'}).text.startswith('Script callbacks: 0'), 'none left')


def test_runs_to_a_place(s):
    s.call('memory_access', {'action': 'write', 'address': '0x8000', 'hex': '00 ' * 32 + '18 DE'})  # 32 nops; jr -34
    s.call('control_execution', {'action': 'pause'})
    s.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/registers/pc', 'body': {'value': 0x8000}})
    r = s.call('control_execution', {'action': 'run_until_pc', 'pc': '0x8010'})
    expect(pc_of(r.text) == 0x8010 and r.structured.get('hit') is True, 'ran until PC', r.text)
    r = s.call('control_execution', {'action': 'run_until_pc'}, ok=False)
    expect(r.is_error and 'pc' in r.text, 'pc is required', r.text)
    r = s.call('control_execution', {'action': 'run_scanlines', 'count': 3})
    expect('3 scanline' in r.text and 'PC=' in r.text, 'ran scanlines', r.text)
    r = s.call('control_execution', {'action': 'run_to_scanline', 'scanline': 100})
    expect('scanline 100' in r.text, 'ran to the scanline', r.text)
    r = s.call('control_execution', {'action': 'run_to_pixel'})
    expect('paper pixel' in r.text and 'PC=' in r.text, 'ran to the first paper pixel', r.text)


def test_pace(s):
    s.call('control_execution', {'action': 'pause'})
    r = s.call('control_execution', {'action': 'pace'})
    expect('Pace ' in r.text and r.structured['pace'].get('presets'), 'the pace and its ladder', r.text)
    r = s.call('control_execution', {'action': 'pace', 'pace_op': 'set', 'preset': '1op', 'interval_ms': 500})
    expect('Pace 1op' in r.text and '500 ms' in r.text, 'the preset and interval', r.text)
    pc = pc_of(s.call('inspect_state', {'aspects': ['registers']}).text)
    r = s.call('control_execution', {'action': 'pace', 'pace_op': 'step'})
    expect(not r.is_error and r.structured.get('pc') != pc, 'one step at the pace', r.text)
    r = s.call('control_execution', {'action': 'pace', 'pace_op': 'start'})
    expect('running' in r.text, 'the pace runs', r.text)
    r = s.call('control_execution', {'action': 'pace', 'pace_op': 'stop'})
    expect('not running' in r.text and 'stopped' in r.text, 'stopped, the reason named', r.text)
    r = s.call('control_execution', {'action': 'pace', 'pace_op': 'set', 'preset': 'warp'}, ok=False)
    expect(r.is_error and 'preset' in r.text, 'an unknown preset is refused', r.text)
    s.call('control_execution', {'action': 'pace', 'pace_op': 'set', 'preset': 'full'})


def script(s, method, path, body=None, session=None):
    """A host script's REST call (urllib's own User-Agent, as a Python script sends): (status, body, headers)"""
    headers = {'Content-Type': 'application/json'}
    if session:
        headers['X-Unreal-Session'] = session
    request = urllib.request.Request(webapi(s) + path, method=method, headers=headers,
                                     data=json.dumps(body).encode() if body is not None else None)
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return response.status, json.loads(response.read().decode() or 'null'), response.headers
    except urllib.error.HTTPError as error:
        return error.code, json.loads(error.read().decode() or 'null'), error.headers


def test_a_host_script_and_mcp_share_one_machine(s):
    """The cross-plane case (execution arbiter design §6a): the script's change opens an implicit session that refuses
    MCP runs with a resolvable answer; take_control takes it over and names the id the script then sends"""
    emu = instance(s)
    release_mcp_session(s)
    base = f'/api/v1/emulator/{emu}'
    status, body, headers = script(s, 'POST', base + '/run_frames', {'frames': 1})
    implicit = headers.get('X-Unreal-Session', '')
    expect(status == 200 and implicit and body['automation_session']['id'] == implicit,
           'the script\'s change opens an implicit session, its id in the header and the JSON', (status, body))
    status, body, headers = script(s, 'POST', base + '/run_frames', {'bogus': 1})
    expect(status == 400 and headers.get('X-Unreal-Session') == implicit and
           body.get('automation_session', {}).get('id') == implicit, 'a 4xx reply names the session too', body)

    r = s.call('control_execution', {'action': 'run_frames', 'frames': 1}, ok=False)
    expect(r.is_error and 'held_by_other' in r.text and 'Python-urllib' in r.text and 'implicit' in r.text and
           'take_control' in r.text, 'MCP is refused with the holder and how to resolve it', r.text)
    r = s.call('control_execution', {'action': 'resume'}, ok=False)
    expect(r.is_error and 'already running' not in r.text, 'a refused resume is no "already running"', r.text)

    status, body, _ = script(s, 'GET', base + '/control/session')
    expect(status == 200 and body['implicit'] and body['yours'] and body['session'] == implicit and
           0 < body['lease_seconds_left'] <= 15, 'the script sees its own implicit session and its id', body)
    seen = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/emulator/{id}/control/session'}).structured['body']
    expect(seen.get('yours') is False and 'session' not in seen and seen['holder']['implicit'],
           'another client sees the holder, never the id', seen)

    r = s.call('control_execution', {'action': 'take_control', 'client': 'cross-plane check'})
    taken = r.structured['session']
    expect('Took over the implicit session of webapi (Python-urllib' in r.text and taken in r.text,
           'take_control takes the implicit session over and names the id', r.text)
    status, body, _ = script(s, 'POST', base + '/run_frames', {'frames': 1}, implicit)
    expect(status == 409 and body['error'] == 'held_by_other' and body['holder']['implicit'] is False,
           'the taken-over id is no pass: the gate refuses with the explicit holder', body)
    status, body, _ = script(s, 'POST', base + '/run_frames', {'frames': 1}, taken)
    expect(status == 200 and 'automation_session' not in body, 'the script drives with the shared id', body)
    s.call('control_execution', {'action': 'run_frames', 'frames': 1})
    r = s.call('control_execution', {'action': 'release_control'})
    expect('session ended' in r.text, 'released', r.text)
    r = s.call('control_execution', {'action': 'release_control'})
    expect('Nothing to release' in r.text, 'a second release says there was nothing', r.text)

    # MCP's own implicit session ends without the id; so does the script's
    s.call('control_execution', {'action': 'run_frames', 'frames': 1})
    r = s.call('control_execution', {'action': 'release_control'})
    expect('session ended' in r.text, 'release_control ends the implicit session of MCP', r.text)
    script(s, 'POST', base + '/run_frames', {'frames': 1})
    status, body, _ = script(s, 'DELETE', base + '/control/session')
    expect(status == 200 and body['released'] and not body['held'], 'DELETE without the header ends it', body)
    s.call('control_execution', {'action': 'pause'})


def test_unknown_action_is_an_error(s):
    r = s.call('control_execution', {'action': 'fly'}, ok=False)
    expect(r.is_error, 'refused', r.text)


if __name__ == '__main__':
    sys.exit(main(globals()))
