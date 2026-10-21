#!/usr/bin/env python3
"""actions: the host actions of the core registry (list with availability, run by id) and the same routes over REST and
the WebSocket's calls (/api/v1/ws): the same answers, the gates of each caller"""

import json
import sys
import urllib.error
import urllib.parse
import urllib.request

from mcplib import COMPANION_NARROW_TOKEN, expect, main


def webapi(s):
    """The WebAPI base of the MCP server under test (MCP = WebAPI + 200)"""
    url = urllib.parse.urlparse(s.url)
    return f'http://{url.hostname}:{url.port - 200}'


def http(s, method, path, body=None, token=None):
    headers = {'Content-Type': 'application/json'}
    if token:
        headers['Authorization'] = 'Bearer ' + token
    request = urllib.request.Request(webapi(s) + path, method=method, headers=headers,
                                     data=json.dumps(body).encode() if body is not None else None)
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return response.status, json.loads(response.read().decode() or 'null')
    except urllib.error.HTTPError as error:
        return error.code, json.loads(error.read().decode() or 'null')


def ws_exchange(socket, calls):
    for rid, call in enumerate(calls, start=1):
        method, path, body = call[:3]
        message = {'op': 'call', 'rid': rid, 'method': method, 'path': path}
        if body is not None:
            message['body'] = body
        if len(call) > 3:
            message['session'] = call[3]
        socket.send(json.dumps(message))
    results = {}
    while len(results) < len(calls):
        reply = json.loads(socket.recv())
        expect(reply.get('op') == 'result', 'a call is answered with a result', reply)
        results[reply['rid']] = reply
    return results


def ws_calls(s, calls):
    """Sends each (method, path, body[, session]) as a call on /api/v1/ws; returns the results by rid"""
    uri = webapi(s).replace('http', 'ws') + '/api/v1/ws'
    try:
        from websockets.sync.client import connect  # websockets >= 12
    except ImportError:
        import websocket  # websocket-client
        socket = websocket.create_connection(uri, timeout=30)
        try:
            return ws_exchange(socket, calls)
        finally:
            socket.close()
    with connect(uri, open_timeout=30) as socket:
        return ws_exchange(socket, calls)


def release_mcp_session(s):
    """The MCP calls leave an implicit session: an explicit take replaces it, the release ends it"""
    s.call('control_execution', {'action': 'take_control'}, ok=False)
    s.call('control_execution', {'action': 'release_control'}, ok=False)


def instance(s):
    return s.call('emulator_manage', {'action': 'list'}).structured['emulators'][0]['id']


def setup(s):
    s.fresh_machine()  # paused
    s.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/audio/mixer/master', 'body': {'muted': True}})


def test_list_names_every_action_and_why_not(s):
    r = s.call('actions', {'action': 'list'})
    for action_id in ('run.pause_toggle', 'run.step_in', 'ttd.jog_frames', 'mach.nmi'):
        expect(action_id in r.text, f'{action_id} in the text', r.text[:800])
    expect('dbg.open' in r.text and 'gui' in r.text, 'the Qt client registered its GUI actions', r.text[:2000])
    expect('not available yet' in r.text, 'a stub says why', r.text[:3000])
    rows = {a['id']: a for a in r.structured['actions']}
    expect(rows['run.step_in']['available'], 'a step can run on the paused machine', rows['run.step_in'])
    expect(rows['ttd.jog_frames']['kind'] == 'delta', 'the jog is a delta', rows['ttd.jog_frames'])


def test_run_steps_the_paused_machine(s):
    r = s.call('actions', {'action': 'run', 'id': 'run.step_in'})
    expect('run.step_in: ok' in r.text and 'pc' in r.text, 'the step and its pc in the text', r.text)
    expect(r.structured['result']['state'] == 'paused', 'still paused', r.structured)


def test_a_step_while_running_is_refused_with_the_reason(s):
    s.call('actions', {'action': 'run', 'id': 'run.pause_toggle'})  # resumes
    try:
        r = s.call('actions', {'action': 'run', 'id': 'run.step_in'}, ok=False)
        expect(r.is_error and 'needs a pause' in r.text, 'not_now with the reason', r.text)
    finally:
        s.call('control_execution', {'action': 'pause'})
    r = s.call('actions', {'action': 'run', 'id': 'no.such_action'}, ok=False)
    expect(r.is_error and 'Not Found' in r.text, 'an unknown id', r.text)
    r = s.call('actions', {'action': 'run', 'id': 'ttd.jog_frames', 'value': 'ten'}, ok=False)
    expect(r.is_error and 'Bad Request' in r.text, 'a delta takes a number', r.text)


def test_rest_and_websocket_answer_the_same(s):
    emu = instance(s)
    release_mcp_session(s)
    status, listed = http(s, 'GET', '/api/v1/controllers/actions?instance=' + emu)
    expect(status == 200 and listed['ok'], 'REST list', listed)
    results = ws_calls(s, [('GET', '/api/v1/controllers/actions?instance=' + emu, None),
                           ('POST', '/api/v1/controllers/actions/run.frame_step', {'instance': emu}),
                           ('POST', '/api/v1/controllers/actions/no.such_action', {'instance': emu})])
    expect(results[1]['status'] == 200, 'the list over the socket', results[1])
    expect([a['id'] for a in results[1]['body']['actions']] == [a['id'] for a in listed['actions']],
           'the same catalogue on both planes', results[1]['body'])
    expect(results[2]['status'] == 200 and results[2]['body']['ok'], 'a frame step over the socket', results[2])
    status, rest_missing = http(s, 'POST', '/api/v1/controllers/actions/no.such_action', {'instance': emu})
    expect(results[3]['status'] == status == 404 and results[3]['body'] == rest_missing,
           'the same error on both planes', (results[3], rest_missing))
    # The frame step opened the socket client's implicit session; the result names it, a later call carries it
    session = results[2].get('session', '')
    expect(session != '', 'the result names the implicit session', results[2])
    ended = ws_calls(s, [('DELETE', f'/api/v1/emulator/{emu}/control/session', None, session)])
    expect(ended[1]['status'] == 200 and ended[1]['body'].get('released'), 'the session ends over the socket', ended)


def test_a_companion_is_held_to_its_scopes(s):
    emu = instance(s)
    release_mcp_session(s)
    # The narrow companion has the run scope only: a step needs the debug scope
    status, body = http(s, 'POST', '/api/v1/controllers/actions/run.step_in', {'instance': emu},
                        token=COMPANION_NARROW_TOKEN)
    expect(status == 403 and body['error'] == 'companion_forbidden', 'a step needs the debug scope', body)
    status, body = http(s, 'POST', '/api/v1/controllers/actions/mach.turbo_toggle', {'instance': emu},
                        token=COMPANION_NARROW_TOKEN)
    expect(status == 200 and body['ok'], 'turbo is in the run scope', body)
    http(s, 'POST', '/api/v1/controllers/actions/mach.turbo_toggle', {'instance': emu}, token=COMPANION_NARROW_TOKEN)


if __name__ == '__main__':
    sys.exit(main(globals()))
