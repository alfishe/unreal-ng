#!/usr/bin/env python3
"""jobs (PLAN #122 S2): a long verb with async=true answers its job id at once; the jobs tool's wait long-polls for its
end and answers the result in the text; list, status, cancel and their refusals; the REST long-poll wakes when the
job ends (not when wait_ms is up); over the WebSocket the same long-poll is a `call`, and the jobs.done topic names
the job that ended"""

import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

from contextlib import contextmanager

from mcplib import COMPANION_NARROW_TOKEN, WebSocket, expect, main

HERE = os.path.dirname(os.path.abspath(__file__))
SCRATCH = os.path.normpath(os.path.join(HERE, '..', '..', '..', 'scratch', 'mcp-jobs'))


def webapi_port(s):
    return urllib.parse.urlparse(s.url).port - 200


def http(s, method, path, body=None, session=None, timeout=90):
    data = json.dumps(body).encode() if body is not None else None
    headers = {'Content-Type': 'application/json'}
    if session:
        headers['X-Unreal-Session'] = session
    request = urllib.request.Request(f'http://127.0.0.1:{webapi_port(s)}{path}', data=data, method=method,
                                     headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.status, json.loads(response.read().decode() or '{}')
    except urllib.error.HTTPError as error:
        return error.code, json.loads(error.read().decode() or '{}')


def emulator_id(s):
    listing = s.call('emulator_manage', {'action': 'list'}).structured
    return listing['emulators'][0]['id']


@contextmanager
def control(s, eid):
    """The script takes control as a host script does: the MCP calls before left an implicit session of their own (a
    take replaces it, the release ends it); the TTD POSTs are gated, the jobs routes' reads are not"""
    s.call('control_execution', {'action': 'take_control'}, ok=False)
    s.call('control_execution', {'action': 'release_control'}, ok=False)
    status, taken = http(s, 'POST', f'/api/v1/emulator/{eid}/control/session', {'client': 'jobs check'})
    session = (taken or {}).get('session') or (taken or {}).get('id') or ''
    expect(status == 200 and session, 'the script takes control', taken)
    try:
        yield session
    finally:
        http(s, 'DELETE', f'/api/v1/emulator/{eid}/control/session', None, session)


def setup(s):
    s.fresh_machine()
    os.makedirs(SCRATCH, exist_ok=True)
    s.call('time_travel', {'action': 'start'})
    s.call('control_execution', {'action': 'run_frames', 'frames': 50})
    s.call('time_travel', {'action': 'stop'})


def test_async_dump_then_wait(s):
    path = os.path.join(SCRATCH, 'async-dump.ttd')
    if os.path.exists(path):
        os.remove(path)
    r = s.call('time_travel', {'action': 'dump', 'path': path, 'async': True})
    job = r.structured.get('job')
    expect(job and f'Job {job}' in r.text and 'wait' in r.text, 'the answer names the job and how to wait', r.text)
    expect(r.structured.get('state') in ('queued', 'running', 'done'), 'a state', r.structured)
    w = s.call('jobs', {'action': 'wait', 'job': job, 'wait_ms': 20000})
    expect(w.structured['ended'] and w.structured['state'] == 'done', 'the wait answers the end', w.text)
    expect(f'job {job}' in w.text and 'done' in w.text and 'bytes' in w.text, 'id, state, result in the text', w.text)
    expect(w.structured['result']['bytes'] > 0 and os.path.exists(path), 'the file written', w.structured['result'])
    st = s.call('jobs', {'action': 'status', 'job': job, 'result': False})
    expect('result' not in st.structured and st.structured['state'] == 'done', 'status without the result',
           st.structured)
    listed = s.call('jobs', {'action': 'list', 'state': 'finished'})
    expect(any(j['job'] == job for j in listed.structured['jobs']) and f'{job}' in listed.text, 'listed', listed.text)
    os.remove(path)


def test_refusals_are_errors_with_the_reason(s):
    r = s.call('jobs', {'action': 'wait', 'job': 1, 'wait_ms': 60001}, ok=False)
    expect(r.is_error and '60000' in r.text, 'the long-poll cap is refused, not clamped', r.text)
    r = s.call('jobs', {'action': 'status', 'job': 99999999}, ok=False)
    expect(r.is_error and '404' in r.text, 'an unknown job: 404', r.text)
    r = s.call('time_travel', {'action': 'status', 'async': True}, ok=False)
    expect(r.is_error and 'async' in r.text, 'async on a short verb is refused', r.text)
    done = s.call('time_travel', {'action': 'memory_series', 'memory_ranges': '0x5C00:2', 'async': True})
    s.call('jobs', {'action': 'wait', 'job': done.structured['job']})
    r = s.call('jobs', {'action': 'cancel', 'job': done.structured['job']}, ok=False)
    expect(r.is_error and 'ended already' in r.text, 'a cancel of an ended job: 409 with the reason', r.text)


def test_jobs_start_runs_any_long_verb(s):
    r = s.call('jobs', {'action': 'start', 'domain': 'ttd', 'verb': 'memory-series',
                        'options': {'ranges': '0x5C00:4', 'format': 'hex', 'limit': 5}})
    job = r.structured['job']
    expect(r.structured['kind'] == 'ttd.memory-series', 'the kind', r.structured)
    w = s.call('jobs', {'action': 'wait', 'job': job})
    expect(w.structured['state'] == 'done' and w.structured['result']['rows'], 'the series in the result',
           w.structured)
    r = s.call('jobs', {'action': 'start', 'domain': 'disk', 'verb': 'x'}, ok=False)
    expect(r.is_error and 'domain' in r.text, 'an unknown domain refused', r.text)


def test_rest_long_poll_wakes_at_the_end(s):
    eid = emulator_id(s)
    path = os.path.join(SCRATCH, 'rest-dump.ttd')
    with control(s, eid) as session:
        status, body = http(s, 'POST', f'/api/v1/emulator/{eid}/ttd/dump', {'path': path, 'async': True}, session)
    expect(status == 202 and body['job'] and body['status_url'].endswith(str(body['job'])), '202 with the job', body)
    started = time.time()
    status, polled = http(s, 'GET', f"/api/v1/jobs/{body['job']}?wait_ms=30000")
    elapsed = time.time() - started
    expect(status == 200 and polled['ended'] and polled['state'] == 'done', 'the poll answers the end', polled)
    expect(elapsed < 10, f'answered at the end, not after wait_ms ({elapsed:.1f} s)')
    status, listed = http(s, 'GET', f'/api/v1/emulator/{eid}/jobs?state=finished&limit=5')
    expect(status == 200 and any(j['job'] == body['job'] for j in listed['jobs']), 'the instance list', listed)
    status, refused = http(s, 'DELETE', f"/api/v1/jobs/{body['job']}")
    expect(status == 409, 'DELETE of an ended job: 409', refused)
    if os.path.exists(path):
        os.remove(path)


def test_websocket_call_long_polls_and_topic_tells_the_end(s):
    eid = emulator_id(s)
    # The jobs topics are state topics: they take a companion's socket (the narrow one, which no other script
    # revokes); the calls go over a plain socket of this host with the script's session, as REST does
    topic = WebSocket(webapi_port(s), token=COMPANION_NARROW_TOKEN)
    ws = WebSocket(webapi_port(s))
    try:
        topic.send({'op': 'subscribe', 'topics': ['jobs.done'], 'max_hz': 20})
        topic.until(lambda m: m.get('t') == 'jobs.done' and m.get('op') == 'state', 'the topic')
        with control(s, eid) as session:
            ws.send({'op': 'call', 'rid': 1, 'method': 'POST', 'path': f'/api/v1/emulator/{eid}/ttd/memory-series',
                     'body': {'ranges': '0x5C00:2', 'async': True}, 'session': session})
            started = ws.until(lambda m: m.get('op') == 'result' and m.get('rid') == 1, 'the start')
        expect(started['status'] == 202, 'started as a job over the socket', started)
        job = started['body']['job']
        ws.send({'op': 'call', 'rid': 2, 'method': 'GET', 'path': f'/api/v1/jobs/{job}?wait_ms=20000'})
        waited = ws.until(lambda m: m.get('op') == 'result' and m.get('rid') == 2, 'the long-poll', timeout=25)
        expect(waited['status'] == 200 and waited['body']['ended'], 'the socket long-poll answers the end', waited)
        topic.until(lambda m: m.get('t') == 'jobs.done' and m['value'].get('last_job') == job, 'jobs.done names it',
                    timeout=5)
    finally:
        ws.close()
        topic.close()


if __name__ == '__main__':
    sys.exit(main(globals()))
