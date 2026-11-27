#!/usr/bin/env python3
"""time_travel reads of the past (PLAN #122 S1, V2, E4): memory_series over a recorded history equals memory_at frame by
frame and the ROM's FRAMES counter counts in it; changes_only, the row limit and its next_from_frame in the text, the
refusals; screen_at as an MCP image block with the shown screens in the text, a screen's raw bytes equal to the series'
RAM page 5 at the next frame start; memory_at names its moment in words. The same verbs on REST, a socket call, the
CLI, Python and Lua"""

import base64
import json
import re
import socket
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

from mcplib import WebSocket, expect, main

FRAMES = '0x5C78'   # the ROM's frame counter (system variable FRAMES), counted by its IM 1 handler


def webapi(s):
    return f'http://127.0.0.1:{urllib.parse.urlparse(s.url).port - 200}'


def http(s, method, path, body=None, session=None):
    data = json.dumps(body).encode() if body is not None else None
    headers = {'Content-Type': 'application/json'}
    if session:
        headers['X-Unreal-Session'] = session
    request = urllib.request.Request(webapi(s) + path, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            return response.status, json.loads(response.read().decode() or 'null')
    except urllib.error.HTTPError as error:
        raw = error.read().decode()
        try:
            return error.code, json.loads(raw)
        except ValueError:
            return error.code, {'raw': raw}


def cli(s, emu, command):
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
        return read_until_prompt().replace('\r', '')


def interpreter(s, language, code):
    status, reply = http(s, 'POST', f'/api/v1/{language}/exec', {'code': code})
    if status != 200 or not isinstance(reply, dict):
        print(f'        {language} not checked: /exec answered {status} {str(reply)[:200]}')
        return None
    return reply.get('output', '')


HISTORY = {}


def setup(s):
    s.fresh_machine()
    s.call('time_travel', {'action': 'start'})
    s.call('control_execution', {'action': 'run_frames', 'frames': 20})
    s.call('time_travel', {'action': 'stop'})
    r = s.call('time_travel', {'action': 'status'})
    match = re.search(r'frames (\d+)\.\.(\d+)', r.text)
    expect(match, 'the recorded range in the status', r.text)
    HISTORY['first'], HISTORY['last'] = int(match.group(1)), int(match.group(2))
    HISTORY['id'] = s.call('emulator_manage', {'action': 'list'}).structured['emulators'][0]['id']


def test_memory_series_equals_memory_at_and_counts_frames(s):
    first, last = HISTORY['first'], HISTORY['last']
    r = s.call('time_travel', {'action': 'memory_series', 'memory_ranges': f'{FRAMES}:2, ram5:0x1C78:2',
                               'from_frame': first, 'to_frame': last})
    rows = r.structured.get('rows', [])
    expect(len(rows) == last - first + 1, 'a row per frame', r.structured)
    expect('the machine did not move' in r.text and 'before its first instruction' in r.text,
           'the moment in the text', r.text)
    expect(f'frame {first}: ' in r.text and f'frame {last}: ' in r.text, 'every row in the text', r.text)
    counts = []
    for row in rows:
        expect(row['data'][0] == row['data'][1], f"frame {row['frame']}: cpu and ram5 agree", row)
        counts.append(int(row['data'][0][2:4] + row['data'][0][0:2], 16))
    steps = {b - a for a, b in zip(counts, counts[1:])}
    expect(steps == {1}, 'FRAMES counts one a frame', counts)
    for row in rows[::7]:
        at = s.call('time_travel', {'action': 'memory_at', 'space': 'cpu', 'frame': row['frame'], 'offset': FRAMES,
                                    'length': 2})
        expect(at.structured.get('hex') == row['data'][0], f"memory_at frame {row['frame']} equals the row",
               (at.structured, row))
        expect(at.structured.get('moment') == 'frame_start' and 'before its first instruction' in at.text,
               'memory_at names its moment (E4)', at.text)


def test_memory_series_changes_only_limit_and_refusals(s):
    first, last = HISTORY['first'], HISTORY['last']
    r = s.call('time_travel', {'action': 'memory_series', 'memory_ranges': f'{FRAMES}:1', 'changes_only': True})
    expect(r.structured.get('row_count') == last - first + 1, 'FRAMES changes every frame', r.structured)
    r = s.call('time_travel', {'action': 'memory_series', 'memory_ranges': [f'{FRAMES}:2'], 'limit': 5})
    expect(r.structured.get('truncated') and r.structured.get('next_from_frame') == first + 5,
           'five rows, then where to go on', r.structured)
    expect(f'go on with from_frame={first + 5}' in r.text, 'the continuation in the text', r.text)
    r = s.call('time_travel', {'action': 'memory_series'}, ok=False)
    expect(r.is_error and 'memory_ranges' in r.text, 'no ranges named', r.text)
    r = s.call('time_travel', {'action': 'memory_series', 'memory_ranges': f'{FRAMES}:2', 'from_frame': last + 10,
                               'to_frame': last + 20}, ok=False)
    expect(r.is_error and 'outside the history' in r.text, 'frames past the history named', r.text)


def test_screen_at_picture_and_screens(s):
    frame = HISTORY['first'] + 3
    r = s.call('time_travel', {'action': 'screen_at', 'frame': frame})
    images = r.blocks('image')
    expect(len(images) == 1 and images[0].get('mimeType') == 'image/png', 'the picture as an image block', r.content)
    expect(base64.b64decode(images[0]['data'])[:8] == b'\x89PNG\r\n\x1a\n', 'a real PNG')
    expect('Shown at the start: normal (page 5)' in r.text and 'The machine did not move' in r.text,
           'the shown screens in the text', r.text)
    r = s.call('time_travel', {'action': 'screen_at', 'frame': frame, 'which': 'normal', 'screen_format': 'raw'})
    raw = base64.b64decode(r.structured.get('data', ''))
    expect(len(raw) == 6912 and not r.blocks('image'), 'the 6912 bytes, no image', r.structured.get('length'))
    series = s.call('time_travel', {'action': 'memory_series', 'memory_ranges': 'ram5:0:6912', 'frames': [frame + 1],
                                    'data_format': 'base64'})
    expect(base64.b64decode(series.structured['rows'][0]['data'][0]) == raw,
           'the screen at the frame end = RAM page 5 at the next frame start')
    r = s.call('time_travel', {'action': 'screen_at', 'frame': HISTORY['last']}, ok=False)
    expect(r.is_error and 'has not ended' in r.text, 'the last frame has not ended', r.text)


def test_the_other_planes_answer_the_same(s):
    emu, frame = HISTORY['id'], HISTORY['first'] + 2
    # The past reads are POSTs under /ttd, gated as memory-at is: the script takes control as a host script does
    # (the MCP calls before left an implicit session of their own: a take replaces it, the release ends it)
    s.call('control_execution', {'action': 'take_control'}, ok=False)
    s.call('control_execution', {'action': 'release_control'}, ok=False)
    status, taken = http(s, 'POST', f'/api/v1/emulator/{emu}/control/session', {'client': 'past reads check'})
    session = (taken or {}).get('session') or (taken or {}).get('id') or ''
    expect(status == 200 and session, 'the script takes control', taken)
    try:
        check_the_other_planes(s, emu, frame, session)
    finally:
        http(s, 'DELETE', f'/api/v1/emulator/{emu}/control/session', None, session)


def check_the_other_planes(s, emu, frame, session):
    body = {'ranges': [f'{FRAMES}:2'], 'frames': [frame, frame + 1], 'format': 'hex'}
    status, rest = http(s, 'POST', f'/api/v1/emulator/{emu}/ttd/memory-series', body, session)
    expect(status == 200 and len(rest.get('rows', [])) == 2, 'REST answers', rest)
    status, objects = http(s, 'POST', f'/api/v1/emulator/{emu}/ttd/memory-series',
                           {'ranges': [{'address': FRAMES, 'length': 2}], 'frames': [frame, frame + 1], 'format': 'hex'},
                           session)
    expect(status == 200 and objects.get('rows') == rest.get('rows'), 'ranges as objects read the same', objects)
    ws = WebSocket(urllib.parse.urlparse(s.url).port - 200)
    try:
        ws.send({'op': 'call', 'rid': 1, 'method': 'POST', 'path': f'/api/v1/emulator/{emu}/ttd/memory-series',
                 'body': body, 'session': session})
        message = ws.until(lambda m: m.get('op') == 'result' and m.get('rid') == 1, 'the socket call', timeout=30)
        expect((message.get('body') or {}).get('rows') == rest['rows'], 'a socket call answers as REST', message)
    finally:
        ws.close()
    hexes = [row['data'][0] for row in rest['rows']]
    text = cli(s, emu, f'ttd memory-series --ranges {FRAMES}:2 --frames {frame},{frame + 1}')
    expect('2 row(s)' in text and all(h in text for h in hexes), 'the CLI rows', text)
    text = cli(s, emu, f'ttd screen-at --frame {frame} --which normal --format attributes --encoding hex')
    expect('shown at start: normal (page 5)' in text, 'the CLI screen-at', text)
    out = interpreter(s, 'python', 'import json, unreal_emulator as ue\n'
                                   f'r = ue.emu_get_selected().ttd_memory_series("{FRAMES}:2", frames=[{frame}, '
                                   f'{frame + 1}], format="hex")\n'
                                   'print(json.dumps([row["data"][0] for row in r["rows"]]))\n')
    if out is not None:
        expect(json.loads(out.strip().splitlines()[-1]) == hexes, 'Python answers as REST', out[:400])
    out = interpreter(s, 'lua', f'local r = ttd_memory_series{{ranges = {{"{FRAMES}:2"}}, frames = {{{frame}, '
                                f'{frame + 1}}}, format = "hex"}}\n'
                                'for _, row in ipairs(r.rows) do print(row.data[1]) end\n'
                                f'local p = ttd_screen_at{{frame = {frame}}}\n'
                                'print(p.mime)')
    if out is not None:
        expect(out.split() == hexes + ['image/png'], 'Lua answers as REST', out)


def run_state(s):
    """(paused, state, recording_paused, frame, the history's last frame) as the position and the status report them
    (the end from the position: it follows every frame, the status counts are published at intervals)"""
    position = s.call('time_travel', {'action': 'position'}).structured
    status = s.call('time_travel', {'action': 'status'}).structured
    return (position.get('paused'), status.get('state'), status.get('recording_paused'),
            position.get('current', {}).get('frame'), position.get('session_end', {}).get('frame'))


def wait_frames(s, frames, seconds=20):
    start = run_state(s)[3]
    deadline = time.time() + seconds
    while time.time() < deadline:
        now = run_state(s)
        if now[3] >= start + frames:
            return now
        time.sleep(0.02)
    expect(False, f'the machine ran {frames} frames', run_state(s))


def test_store_reads_leave_a_running_recording_running(s):
    """RQ-2: memory_at at a frame start, memory_diff and memory_series read the store. A running, recording machine
    runs and records on after each; a point inside a frame parks it and pauses the recording (D8)"""
    s.call('time_travel', {'action': 'stop'}, ok=False)
    s.call('time_travel', {'action': 'invalidate'}, ok=False)
    s.call('time_travel', {'action': 'start'})
    s.call('control_execution', {'action': 'resume'})
    try:
        before = wait_frames(s, 10)
        expect(before[:3] == (False, 'recording', False), 'running and recording', before)
        frame = before[3] - 4
        reads = [
            ('memory_at cpu', {'action': 'memory_at', 'space': 'cpu', 'frame': frame, 'offset': FRAMES, 'length': 2}),
            ('memory_at ram5', {'action': 'memory_at', 'space': 'ram5', 'frame': frame, 'offset': '0x1C78', 'length': 2}),
            ('memory_diff', {'action': 'memory_diff', 'space': 'ram5', 'from_frame': frame - 2, 'to_frame': frame}),
            ('memory_series', {'action': 'memory_series', 'memory_ranges': f'{FRAMES}:2, ram5:0x1C78:2',
                               'from_frame': frame - 2, 'to_frame': frame}),
        ]
        answers = {}
        for name, args in reads:
            answers[name] = s.call('time_travel', args)
            after = run_state(s)
            expect(after[:3] == (False, 'recording', False), f'{name}: still running and recording', (before, after))
            later = wait_frames(s, 2)
            expect(later[4] > after[4], f'{name}: the history keeps growing', (after, later))
        expect(answers['memory_at cpu'].structured.get('hex') == answers['memory_at ram5'].structured.get('hex'),
               'the CPU view and page 5 agree', (answers['memory_at cpu'].structured, answers['memory_at ram5'].structured))
        rows = answers['memory_series'].structured.get('rows', [])
        expect(len(rows) == 3 and rows[-1]['data'][0] == answers['memory_at cpu'].structured.get('hex'),
               'the series row equals memory_at', rows)
        expect(answers['memory_diff'].structured.get('changed_bytes', 0) > 0, 'FRAMES changed between the frames',
               answers['memory_diff'].structured)

        # D8 stays for what replays
        recorded = wait_frames(s, 2)
        s.call('time_travel', {'action': 'memory_at', 'space': 'cpu', 'frame': frame, 'tinframe': 30000,
                               'offset': FRAMES, 'length': 2})
        inside = run_state(s)
        expect(inside[:3] == (True, 'detached', True), 'a point inside a frame parks the machine and pauses the '
               'recording (D8)', inside)
        expect(inside[4] >= recorded[4], 'what was recorded after the reads is kept', (recorded, inside))
        # The stopped recording holds the bytes read while it recorded
        s.call('time_travel', {'action': 'stop'})
        for name, args in reads:
            again = s.call('time_travel', args).structured
            first = answers[name].structured
            for key in ('hex', 'rows', 'ranges', 'changed_bytes'):
                expect(again.get(key) == first.get(key), f'{name}: {key} as read while recording', (first, again))
    finally:
        s.call('control_execution', {'action': 'pause'}, ok=False)
        s.call('time_travel', {'action': 'stop'}, ok=False)


if __name__ == '__main__':
    sys.exit(main(globals()))
