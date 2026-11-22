#!/usr/bin/env python3
"""Counting breakpoints with per-hit frames (PLAN #122 V4): a breakpoint that only counts records each hit's frame, T,
PC, address and value; MCP bp_hits, REST GET .../breakpoints/{bp_id}/hits, the WebSocket call of the same route, the
CLI bphits, Lua and Python bp_hit_log read the same record; the reset empties it; the sound card CPU's breakpoint
records the card's own clock"""

import re
import socket
import sys
import time
import urllib.parse

from mcplib import WebSocket, expect, main
from test_actions import http, instance

# di; ld a,5; ld (#9000),a; ld a,6; ld (#9000),a; jr back: two writes a pass
PROGRAM = 'F3 3E 05 32 00 90 3E 06 32 00 90 18 F3'


def setup(s):
    s.fresh_machine()  # paused
    s.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/audio/mixer/master', 'body': {'muted': True}})


def clear(s):
    s.call('control_execution', {'action': 'pause'}, ok=False)
    s.call('control_execution', {'action': 'bp_clear', 'cpu': 'gs'}, ok=False)
    s.call('control_execution', {'action': 'bp_clear'})


def bp_id_of(text):
    m = re.search(r'#(\d+)', text)
    expect(m, 'the id in the text', text)
    return int(m.group(1))


def run_a_while(s, seconds=0.3):
    s.call('control_execution', {'action': 'resume'})
    time.sleep(seconds)  # a host script: the machine runs at full speed meanwhile
    s.call('control_execution', {'action': 'pause'})


def cli(s, emu, command):
    """The CLI's answer to @p command on the machine @p emu"""
    port = urllib.parse.urlparse(s.url).port - 100
    with socket.create_connection(('127.0.0.1', port), timeout=30) as sock:
        def read_until_prompt():
            data = b''
            deadline = time.time() + 30
            while not re.search(rb'\n> (\x1b\[K)?$', data) and time.time() < deadline:
                chunk = sock.recv(65536)
                if not chunk:
                    break
                data += chunk
            return data.decode(errors='replace')
        read_until_prompt()
        sock.sendall(f'select {emu}\n'.encode())
        read_until_prompt()
        sock.sendall((command + '\n').encode())
        answer = re.sub(r'\n> (\x1b\[K)?$', '', read_until_prompt()).replace('\r', '')
        return answer.split('\n', 1)[1] if answer.startswith(command) else answer


def test_counting_breakpoint_records_every_plane(s):
    clear(s)
    emu = instance(s)
    s.call('memory_access', {'action': 'write', 'address': '0x8000', 'hex': PROGRAM})
    s.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/registers/pc', 'body': {'value': 0x8000}})
    r = s.call('control_execution', {'action': 'bp_add', 'type': 'write', 'address': '0x9000', 'stop': False,
                                     'hit_log': 6})
    write_id = bp_id_of(r.text)
    r = s.call('control_execution', {'action': 'bp_add', 'address': '0x8000', 'stop': False, 'hit_log': 4,
                                     'hit_log_keep': 'last'})
    exec_id = bp_id_of(r.text)
    r = s.call('control_execution', {'action': 'bp_add', 'address': '0x8000', 'hit_log': 4}, ok=False)
    expect(r.is_error and 'hit_log' in r.text, 'a record needs a breakpoint that only counts', r.text)
    r = s.call('control_execution', {'action': 'bp_list'})
    expect('count only log first 6' in r.text and 'count only log last 4' in r.text, 'the list names the record',
           r.text)
    run_a_while(s)

    # MCP: the text is a line per hit
    r = s.call('control_execution', {'action': 'bp_hits', 'bp_id': write_id, 'hit_limit': 4})
    body = r.structured
    expect(body['hit_count'] > 100 and body['log']['stored'] == 6 and body['log']['keep'] == 'first',
           'counted, the first 6 kept', body)
    hits = body['hits']
    expect(len(hits) == 4 and body['more'] is True and body['next_from'] == 4, 'a page of 4, more', body)
    expect([h['value'] for h in hits] == [5, 6, 5, 6] and all(h['address'] == 0x9000 for h in hits),
           'the bytes written, in order', hits)
    expect([h['pc'] for h in hits] == [0x8003, 0x8008, 0x8003, 0x8008], 'the instructions doing the writes', hits)
    expect(all(h['kind'] == 'write' and 'cpu_time' not in h for h in hits), 'main CPU writes', hits)
    expect(hits[1]['frame'] > hits[0]['frame'] or hits[1]['t'] > hits[0]['t'], 'later in time', hits)
    expect(re.search(r'\[0\] hit 1 frame \d+ t \d+ pc 0x8003 write 0x9000 = 5', r.text) and 'more: hit_from 4' in r.text,
           'the text has the hits', r.text)

    r = s.call('control_execution', {'action': 'bp_hits', 'bp_id': exec_id})
    body = r.structured
    expect(body['log']['keep'] == 'last' and body['log']['stored'] == 4 and
           body['log']['oldest'] == body['hit_count'] - 4, 'the newest 4', body)
    expect([h['hit'] for h in body['hits']] == list(range(body['hit_count'] - 3, body['hit_count'] + 1)),
           'numbered as the hits', body['hits'])
    expect(all(h['pc'] == 0x8000 and h['kind'] == 'exec' for h in body['hits']), 'the instruction about to run', body)

    # REST, and the WebSocket call of the same route, answer the same record
    status, rest = http(s, 'GET', f'/api/v1/emulator/{emu}/breakpoints/{write_id}/hits?from=4&limit=10')
    expect(status == 200 and rest['first_index'] == 4 and [h['value'] for h in rest['hits']] == [5, 6] and
           rest['more'] is False, 'REST pages on', rest)
    status, refused = http(s, 'GET', f'/api/v1/emulator/{emu}/breakpoints/999/hits')
    expect(status == 404, 'an unknown breakpoint', refused)
    status, refused = http(s, 'GET', f'/api/v1/emulator/{emu}/breakpoints/{write_id}/hits?limit=0')
    expect(status == 400, 'a bad limit', refused)
    ws = WebSocket(urllib.parse.urlparse(s.url).port - 200)
    try:
        ws.send({'op': 'call', 'rid': 1, 'method': 'GET',
                 'path': f'/api/v1/emulator/{emu}/breakpoints/{write_id}/hits?limit=2'})
        result = ws.until(lambda m: m.get('op') == 'result' and m.get('rid') == 1, 'the call')
        expect(result['status'] == 200 and [h['value'] for h in result['body']['hits']] == [5, 6],
               'the socket call answers as REST', result)
    finally:
        ws.close()

    # CLI
    text = cli(s, emu, f'bphits {write_id} --limit 2')
    expect('record keeps the first 6, holds 6' in text and re.search(r'\[1\] hit 2 +frame \d+ +t \d+ +PC #8008 +write #9000 = #06', text)
           and 'more: --from 2' in text, 'the CLI lists the hits', text)

    # Lua and Python
    code = (f'local r = bp_hit_log({write_id}, {{from = 1, limit = 1}}) '
            'print("lua-log", r.ok, r.log.stored, r.hits[1].index, r.hits[1].value, r.hits[1].pc)')
    r = s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/lua/exec', 'body': {'code': code}}, ok=False)
    output = ((r.structured or {}).get('body') or {}).get('output', '') + r.text
    expect(re.search(r'lua-log\s+true\s+6\s+1\s+6\s+32776', output), 'Lua reads the record', output)
    code = ('import unreal_emulator as ue\n'
            'emu = ue.emu_get_selected()\n'
            f'r = emu.bp_hit_log({write_id}, from_index=2, limit=1)\n'
            'print("py-log", r["ok"], r["log"]["stored"], r["hits"][0]["index"], r["hits"][0]["value"])\n')
    r = s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/python/exec', 'body': {'code': code}}, ok=False)
    if 'Python automation is not available' not in r.text:
        output = ((r.structured or {}).get('body') or {}).get('output', '') + r.text
        expect('py-log True 6 2 5' in output, 'Python reads the record', output)

    # The reset empties the record with the count
    s.call('control_execution', {'action': 'bp_reset_hits', 'bp_id': write_id})
    r = s.call('control_execution', {'action': 'bp_hits', 'bp_id': write_id})
    expect(r.structured['hit_count'] == 0 and r.structured['log']['total'] == 0 and r.structured['hits'] == [],
           'reset: no hits', r.structured)
    r = s.call('control_execution', {'action': 'bp_hits', 'bp_id': exec_id})
    expect(r.structured['log']['stored'] == 4, 'the other record stays', r.structured)
    clear(s)


def test_a_breakpoint_that_stops_has_no_record(s):
    clear(s)
    r = s.call('control_execution', {'action': 'bp_add', 'address': '0x3FFF'})
    r = s.call('control_execution', {'action': 'bp_hits', 'bp_id': bp_id_of(r.text)})
    expect(r.structured['stop'] is True and r.structured['log'] is None and 'it stops, no per-hit record' in r.text,
           'no record', r.text)
    r = s.call('control_execution', {'action': 'bp_hits'}, ok=False)
    expect(r.is_error and 'bp_id' in r.text, 'bp_hits needs the id', r.text)
    clear(s)


def test_card_cpu_records_its_own_clock(s):
    clear(s)
    r = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/emulator/{id}/state/audio/gs'}, ok=False)
    if r.is_error:
        return  # no General Sound card on this machine
    pc = ((r.structured or {}).get('body') or {}).get('cpu', {}).get('pc')
    expect(isinstance(pc, int), 'the card CPU state has its pc', r.text)
    r = s.call('control_execution', {'action': 'bp_add', 'address': pc, 'cpu': 'gs', 'stop': False,
                                     'hit_log_keep': 'last'})
    card_id = bp_id_of(r.text)
    run_a_while(s, 0.2)
    r = s.call('control_execution', {'action': 'bp_hits', 'bp_id': card_id, 'cpu': 'gs', 'hit_limit': 1000})
    body = r.structured
    expect(body['cpu'] == 'gs' and body['hit_count'] > 0 and body['log']['capacity'] == 1000, 'the card counted',
           body)
    hits = body['hits']
    expect(hits and all(h['pc'] == pc and h['kind'] == 'exec' and h['cpu_time'] >= 0 for h in hits),
           'the card instruction, the card clock', hits[:3])
    expect(all(b['cpu_time'] > a['cpu_time'] and b['frame'] >= a['frame'] for a, b in zip(hits, hits[1:])),
           'in time order', hits[:3])
    expect('(gs CPU)' in r.text and 'cpu_time' in r.text, 'the text names the card', r.text[:300])
    s.call('control_execution', {'action': 'bp_reset_hits', 'bp_id': card_id, 'cpu': 'gs'})
    r = s.call('control_execution', {'action': 'bp_hits', 'bp_id': card_id, 'cpu': 'gs'})
    expect(r.structured['log']['total'] == 0, 'the card reset', r.structured)
    clear(s)


if __name__ == '__main__':
    sys.exit(main(globals()))
