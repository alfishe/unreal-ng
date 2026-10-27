#!/usr/bin/env python3
"""ws_surface: the topics of /api/v1/ws and its `surface` topic (control surfaces S4, docs/inprogress/2026-10-18-
control-surfaces/protocol.md; Stream Deck integration protocol.md §0), the conformance suite against a live server:
auth and its errors (companion_required, companion_not_for_agents, companion_unknown), join and welcome, actions with
rid (run.pause_toggle, ctx.set, ttd.seek_by with marks) and their refusals (companion_forbidden, bad_request,
Not Found), a press's value (a bare number refused as on REST, set.speed's parameter by name), `ctx` from one session reaching another, the default context flipping to `ttd` when a history appears,
readouts (full, then changed only) and the hub topics (run.state, surfaces.context), the `instances` topic (the events
socket's view: hello, emulator, snapshot) and the debug.* topics (DebugEventHub's events, the same numbering as
/api/v1/websocket), and 50 Hz readout subscribers beside a running machine measured: the same frames per second. A
client that stops reading is closed as a slow consumer (backpressure; mcplib.launch sets a small high-water mark and
limit; host.clients ws.dropped counts the close, so the test waits for it, not for the clock). Last: revoking the companion closes its socket.

Uses the `surface` companion mcplib.launch provisions (UNREAL_COMPANION_TOKENS); stdlib only."""

import os
import sys
import threading
import time

from mcplib import COMPANION_NARROW_TOKEN, COMPANION_SURFACE_TOKEN, WebSocket, expect, main
from test_actions import http, instance, release_mcp_session, webapi

TOKEN = COMPANION_SURFACE_TOKEN


def port(s):
    return int(webapi(s).rsplit(':', 1)[1])


def connect(s, token=TOKEN, headers=None):
    return WebSocket(port(s), token=token, headers=headers)


def join(s, token=TOKEN, name='verify-surface', target=None):
    ws = connect(s, token)
    message = {'op': 'join', 'v': 1, 'name': name, 'model': 'verification.virtual'}
    if target is not None:
        message['target'] = target
    ws.send(message)
    welcome = ws.until(lambda m: m.get('op') in ('welcome', 'error'), 'welcome')
    expect(welcome['op'] == 'welcome', 'joined', welcome)
    return ws, welcome


def act(ws, action_id, value=None, rid=1, timeout=10):
    message = {'op': 'action', 'id': action_id, 'rid': rid}
    if value is not None:
        message['value'] = value
    ws.send(message)
    return ws.until(lambda m: m.get('rid') == rid and m.get('op') in ('result', 'error'), f'the result of {action_id}',
                    timeout)


def position(s, emu):
    status, body = http(s, 'GET', f'/api/v1/emulator/{emu}/ttd/position')
    expect(status == 200, 'the position', body)
    return body


def setup(s):
    s.fresh_machine()  # paused, no history
    s.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/audio/mixer/master', 'body': {'muted': True}})
    release_mcp_session(s)


def test_join_needs_a_companion_token_and_agents_and_strangers_are_refused(s):
    ws = connect(s, token=None)
    ws.send({'op': 'join', 'v': 1, 'name': 'x', 'model': 'y'})
    error = ws.until(lambda m: m.get('op') == 'error', 'an error')
    expect(error['t'] == 'surface' and error['error'] == 'companion_required', 'companion_required', error)
    # {"op":"auth"} then join on the same socket
    ws.send({'op': 'auth', 'token': TOKEN})
    ws.until(lambda m: m.get('op') == 'auth' and m.get('ok'), 'auth ok')
    ws.send({'op': 'join', 'v': 1, 'name': 'x', 'model': 'y'})
    ws.until(lambda m: m.get('op') == 'welcome', 'welcome after auth')
    ws.send({'op': 'join', 'v': 1, 'name': 'x', 'model': 'y'})
    error = ws.until(lambda m: m.get('op') == 'error', 'joined twice')
    expect(error['error'] == 'bad_request', 'one surface per connection', error)
    ws.close()

    # A bad token on the handshake is refused by the HTTP gates before the upgrade, as any request with it
    refused = WebSocket(port(s), token=TOKEN, headers={'X-Unreal-Source': 'mcp'}, upgrade=False)
    expect(refused.status.startswith('HTTP/1.1 403'), 'an agent with a token: 403 at the handshake', refused.status)
    refused.close()
    refused = WebSocket(port(s), token='unc_nobody-knows-this-one', upgrade=False)
    expect(refused.status.startswith('HTTP/1.1 401'), 'an unknown token: 401 at the handshake', refused.status)
    refused.close()

    # ... and by the hub when the token comes in {"op":"auth"}
    agent = connect(s, token=None, headers={'X-Unreal-Source': 'mcp'})
    agent.send({'op': 'auth', 'token': TOKEN})
    agent.send({'op': 'join', 'v': 1, 'name': 'x', 'model': 'y'})
    error = agent.until(lambda m: m.get('op') == 'error', 'an error')
    expect(error['error'] == 'companion_not_for_agents', 'agents never act as companions', error)
    agent.close()

    stranger = connect(s, token=None)
    stranger.send({'op': 'auth', 'token': 'unc_nobody-knows-this-one'})
    stranger.send({'op': 'join', 'v': 1, 'name': 'x', 'model': 'y'})
    error = stranger.until(lambda m: m.get('op') == 'error', 'an error')
    expect(error['error'] == 'companion_unknown', 'an unknown token', error)
    expect(stranger.recv(3) is None and stranger.closed, 'and the socket is closed')
    stranger.close()


def test_a_join_is_welcomed_with_the_instance_its_context_and_the_scopes(s):
    emu = instance(s)
    ws, welcome = join(s)
    expect(welcome['t'] == 'surface' and welcome['v'] == 1 and welcome['session'].startswith('s'), 'welcome', welcome)
    expect(welcome['instance'] == emu and welcome['target']['instance'] == emu, 'follow: the only instance', welcome)
    expect(welcome['target']['mode'] == 'follow' and welcome['target']['model'] == 'PENTAGON', 'the target', welcome)
    expect(welcome['context'] == 'play', 'no history yet: play', welcome)
    expect({'run', 'rewind', 'debug'} <= set(welcome['scopes']), 'the scopes granted', welcome)
    ws.send({'op': 'join', 'v': 1, 'name': 'x', 'model': 'y', 'target': {'view': 'v1'}})
    ws.close()
    pinned = connect(s)
    pinned.send({'op': 'join', 'v': 1, 'name': 'x', 'model': 'y', 'target': {'view': 'v1'}})
    error = pinned.until(lambda m: m.get('op') == 'error', 'a view')
    expect(error['error'] == 'not_now', 'views come with SD4', error)
    pinned.send({'op': 'join', 'v': 1, 'name': 'x', 'model': 'y', 'target': {'instance': 0}})
    welcome = pinned.until(lambda m: m.get('op') == 'welcome', 'pinned by index')
    expect(welcome['instance'] == emu and welcome['target']['mode'] == 'instance', 'pinned', welcome)
    pinned.close()


def test_actions_answer_their_rid_and_refusals_use_the_routes_codes(s):
    ws, _ = join(s)
    ws.send({'op': 'subscribe', 'readouts': ['run.state'], 'max_hz': 50})
    first = ws.until(lambda m: m.get('op') == 'readout', 'the full readout')
    expect(first['values']['run.state']['state'] == 'paused', 'paused (setup)', first)

    result = act(ws, 'run.pause_toggle', {'p': True}, rid=11)
    expect(result['op'] == 'result' and result['ok'], 'resumed by the companion', result)
    ws.until(lambda m: m.get('op') == 'readout' and m['values'].get('run.state', {}).get('state') == 'running',
             'the readout follows: running')
    result = act(ws, 'run.pause_toggle', rid='twelve')
    expect(result['ok'] and result['rid'] == 'twelve', 'paused again; a string rid', result)
    ws.until(lambda m: m.get('op') == 'readout' and m['values'].get('run.state', {}).get('state') == 'paused',
             'the readout follows: paused')
    status, control = http(s, 'GET', f'/api/v1/emulator/{instance(s)}/control/session')
    expect(status == 200 and not control.get('held'), 'a companion opens no session', control)

    result = act(ws, 'no.such.action', rid=13)
    expect(not result['ok'] and result['error'] == 'Not Found', 'unknown id', result)
    result = act(ws, 'ttd.seek_by', 'fast', rid=14)
    expect(result['op'] == 'error' and result['error'] == 'bad_request', 'a delta takes a number', result)
    ws.send({'op': 'frobnicate'})
    error = ws.until(lambda m: m.get('op') == 'error', 'an unknown op')
    expect(error['error'] == 'bad_request', 'unknown op', error)
    ws.close()

    narrow, _ = join(s, COMPANION_NARROW_TOKEN, 'narrow')
    result = act(narrow, 'run.step_in', rid=15)
    expect(not result['ok'] and result['error'] == 'companion_forbidden', 'the debug scope is not granted', result)
    narrow.close()


def test_a_press_refuses_a_bare_number_as_rest_does_and_takes_its_params_by_name(s):
    emu = instance(s)
    ws, _ = join(s)
    ws.send({'op': 'subscribe', 'readouts': ['run.state'], 'max_hz': 50})
    first = ws.until(lambda m: m.get('op') == 'readout', 'the full readout')
    expect(first['values']['run.state']['speed'] == 1, 'speed 1 (setup)', first)

    # set.speed 4 without its name ran the press without a parameter: "next" (x2). Refused on both planes now
    result = act(ws, 'set.speed', 4, rid=21)
    expect(result['op'] == 'error' and result['error'] == 'bad_request' and 'params' in result.get('message', ''),
           'a bare number for a press: bad_request naming params', result)
    status, body = http(s, 'POST', '/api/v1/controllers/actions/set.speed', {'instance': emu, 'value': 4}, TOKEN)
    expect(status == 400, 'REST refuses the same value', (status, body))

    # The first speed change the readout shows is the named one: the refused calls ran nothing (else 2 came first)
    result = act(ws, 'set.speed', {'params': {'value': 4}}, rid=22)
    expect(result['op'] == 'result' and result['ok'], 'the parameter by name', result)
    moved = ws.until(lambda m: m.get('op') == 'readout' and m['values'].get('run.state', {}).get('speed', 1) != 1,
                     'the readout follows')
    expect(moved['values']['run.state']['speed'] == 4, 'speed 4, never "next" (2)', moved)
    result = act(ws, 'set.speed', {'value': 1}, rid=23)
    expect(result['op'] == 'result' and result['ok'], 'a parameter beside the press keys', result)
    ws.until(lambda m: m.get('op') == 'readout' and m['values'].get('run.state', {}).get('speed') == 1,
             'the readout follows: speed 1')
    ws.close()


def test_ctx_set_from_one_session_reaches_another_and_the_topic(s):
    emu = instance(s)
    a, _ = join(s, name='deck-a')
    b, _ = join(s, name='deck-b')
    topics = connect(s)
    topics.send({'op': 'subscribe', 'topics': ['surfaces.context', 'run.state'], 'max_hz': 20})
    ack = topics.until(lambda m: m.get('op') == 'subscribed', 'subscribed')
    expect(set(ack['topics']) == {'surfaces.context', 'run.state'}, 'both topics', ack)
    topics.until(lambda m: m.get('t') == 'surfaces.context' and m.get('instance') == emu, 'the context, once')

    result = act(a, 'ctx.set', 'ttd.timecode', rid=21)
    expect(result['ok'] and result['body']['context'] == 'ttd.timecode', 'set by deck a', result)
    ctx = b.until(lambda m: m.get('op') == 'ctx', 'deck b is told')
    expect(ctx['instance'] == emu and ctx['context'] == 'ttd.timecode' and ctx['by'].startswith('companion:'),
           'the context and its setter', ctx)
    state = topics.until(lambda m: m.get('t') == 'surfaces.context' and m['value']['context'] == 'ttd.timecode',
                         'the topic follows')
    expect(state['op'] == 'state' and state['value']['set'], 'set', state)

    # The agent sets it too: the last setter wins, everyone is told
    s.call('actions', {'action': 'run', 'id': 'ctx.set', 'value': 'debug.disasm'})
    release_mcp_session(s)
    ctx = a.until(lambda m: m.get('op') == 'ctx' and m['context'] == 'debug.disasm', 'deck a is told')
    expect(not ctx['by'].startswith('companion:'), 'the last setter wins: the agent', ctx)
    result = act(b, 'ctx.set', '', rid=22)
    expect(result['ok'] and result['body']['context'] == 'play', 'back to the default', result)
    a.until(lambda m: m.get('op') == 'ctx' and m['context'] == 'play', 'deck a: play')
    for ws in (a, b, topics):
        ws.close()


def test_the_default_context_flips_to_ttd_when_a_history_appears(s):
    status, body = http(s, 'POST', '/api/v1/controllers/actions/ctx.set', {'value': ''}, token=TOKEN)
    expect(status == 200 and not body['result']['set'], 'no context set', body)
    ws, welcome = join(s)
    expect(welcome['context'] == 'play', 'no history: play', welcome)
    s.call('time_travel', {'action': 'start'})
    s.call('control_execution', {'action': 'run_frames', 'frames': 160})
    s.call('time_travel', {'action': 'stop'})
    release_mcp_session(s)
    ctx = ws.until(lambda m: m.get('op') == 'ctx', 'the default flip is pushed')
    expect(ctx['context'] == 'ttd' and ctx['by'] == 'default', 'ttd, by the default (Q-7)', ctx)
    ws.close()


def test_seek_by_over_the_surface_answers_marks_and_the_readout_follows(s):
    emu = instance(s)
    at = position(s, emu)
    expect(at['history'], 'the history of the previous test', at)
    end = at['session_end']['frame']
    ws, _ = join(s)
    ws.send({'op': 'subscribe', 'readouts': ['ttd.position'], 'max_hz': 50})
    full = ws.until(lambda m: m.get('op') == 'readout', 'the full readout')
    cursor = full['values']['ttd.position']
    expect(cursor['history'] and cursor['end'] == end and 48 < cursor['fps'] < 49, 'the cursor', cursor)

    result = act(ws, 'ttd.seek_by', {'d': -3, 'rate': 1, 'unit': 'frames'}, rid=31)
    expect(result['ok'] and result['body']['arrived_at']['frame'] == result['body']['frame'], 'arrived', result)
    expect('marks' in result['body'], 'marks in the body', result)
    target = result['body']['frame']
    ws.until(lambda m: m.get('op') == 'readout' and m['values'].get('ttd.position', {}).get('frame') == target,
             'the readout follows the seek')
    result = act(ws, 'ttd.seek_by', {'d': 500, 'rate': 400}, rid=32)
    expect(result['ok'] and result['body']['frame'] == end and result['body']['marks'][0]['kind'] == 'edge',
           'clamped to the end with an edge mark', result)
    ws.close()


def test_readouts_at_50_hz_leave_the_frame_rate_unchanged(s):
    """A Stream Deck strip at 50 Hz, then ten of them, beside a machine recording at 1x: the frames per second of the
    machine are the same as alone (the hub reads the published cursor, never the machine), and each subscriber gets
    about one readout per frame"""
    emu = instance(s)
    # A new recording (the companion: no session); the machine runs
    http(s, 'POST', f'/api/v1/emulator/{emu}/ttd/stop', token=TOKEN)
    status, body = http(s, 'POST', f'/api/v1/emulator/{emu}/ttd/start', {}, token=TOKEN)
    expect(status == 200, 'recording', body)
    if position(s, emu)['paused']:
        http(s, 'POST', f'/api/v1/emulator/{emu}/resume', token=TOKEN)
    try:
        def frames_in(seconds, subscribers):
            sockets = []
            counts = []
            stop = threading.Event()

            def reader(ws, index):
                while not stop.is_set():
                    message = ws.recv(0.2)
                    if message and message.get('op') == 'readout' and 'ttd.position' in message['values']:
                        counts[index] += 1

            threads = []
            for i in range(subscribers):
                ws, _ = join(s, name=f'strip-{i}')
                ws.send({'op': 'subscribe', 'readouts': ['ttd.position', 'run.state'], 'max_hz': 50})
                ws.until(lambda m: m.get('op') == 'readout', 'the first readout')
                sockets.append(ws)
                counts.append(0)
                threads.append(threading.Thread(target=reader, args=(ws, i)))
            before = position(s, emu)['current']['frame']
            started = time.monotonic()
            for thread in threads:
                thread.start()
            time.sleep(seconds)
            after = position(s, emu)['current']['frame']
            elapsed = time.monotonic() - started
            stop.set()
            for thread in threads:
                thread.join()
            for ws in sockets:
                ws.close()
            return (after - before) / elapsed, [c / elapsed for c in counts]

        at = position(s, emu)
        expect(at['state'] == 'recording' and not at['paused'], 'recording, running', at)
        rounds = []
        for _ in range(2):  # interleaved: alone, one strip, ten strips
            alone, _ = frames_in(3, 0)
            one, rate_one = frames_in(3, 1)
            ten, rate_ten = frames_in(3, 10)
            rounds.append((alone, one, ten, rate_one[0], min(rate_ten)))
        for alone, one, ten, r1, r10 in rounds:
            print(f'        frames/s alone {alone:.2f}, with one 50 Hz subscriber {one:.2f} ({r1:.1f} readouts/s), '
                  f'with ten {ten:.2f} (the slowest {r10:.1f} readouts/s)')
        alone = sum(r[0] for r in rounds) / len(rounds)
        one = sum(r[1] for r in rounds) / len(rounds)
        ten = sum(r[2] for r in rounds) / len(rounds)
        expect(abs(one - alone) <= 0.03 * alone and abs(ten - alone) <= 0.03 * alone,
               'the subscribers do not slow the frames', rounds)
        expect(all(r[3] > 40 and r[4] > 35 for r in rounds), 'about one readout per frame (the machine at ~49 Hz)',
               rounds)
    finally:
        http(s, 'POST', f'/api/v1/emulator/{emu}/pause', token=TOKEN)
        http(s, 'POST', f'/api/v1/emulator/{emu}/ttd/stop', token=TOKEN)


def test_instances_is_the_events_sockets_view_on_the_one_socket(s):
    emu = instance(s)
    http(s, 'POST', f'/api/v1/emulator/{emu}/pause', token=TOKEN)
    anonymous = connect(s, token=None)
    anonymous.send({'op': 'subscribe', 'topics': ['instances']})
    error = anonymous.until(lambda m: m.get('op') == 'error', 'an error')
    expect(error['error'] == 'companion_required', 'a companion view', error)
    anonymous.close()

    ws = connect(s)
    ws.send({'op': 'subscribe', 'topics': ['instances', 'run.state']})
    ack = ws.until(lambda m: m.get('op') == 'subscribed', 'subscribed')
    expect(set(ack['topics']) == {'instances', 'run.state'}, 'both topics', ack)
    hello = ws.until(lambda m: m.get('t') == 'instances', 'hello')
    expect(hello['op'] == 'hello' and hello['seq'] == 1 and hello['companion']['name'] == 'surface', 'hello: me', hello)
    entry = next(e for e in hello['emulators'] if e['id'] == emu)
    expect(entry['state'] == 'paused' and entry['actions']['step']['available'], 'paused: step lit', entry)
    # The same view as the events socket
    events = WebSocket(port(s), path='/api/v1/companions/events', token=TOKEN)
    old = events.until(lambda m: m.get('op') == 'hello', 'the events socket hello')
    expect(next(e for e in old['emulators'] if e['id'] == emu) == entry, 'the same entry on both sockets', old)

    start = time.time()
    http(s, 'POST', f'/api/v1/emulator/{emu}/resume', token=TOKEN)
    running = ws.until(lambda m: m.get('t') == 'instances' and m.get('op') == 'emulator' and
                       m['emulator']['id'] == emu and not m['emulator']['actions']['step']['available'],
                       'resumed: step goes dark')
    expect(running['seq'] >= 2, 'numbered', running)
    expect(time.time() - start < 0.9, 'at once, not at the next second', time.time() - start)
    events.until(lambda m: m.get('op') == 'emulator' and m['emulator']['id'] == emu, 'the events socket goes on')
    http(s, 'POST', f'/api/v1/emulator/{emu}/pause', token=TOKEN)
    ws.until(lambda m: m.get('t') == 'instances' and m.get('op') == 'emulator' and
             m['emulator']['actions']['step']['available'], 'paused again')
    ws.send({'op': 'snapshot'})
    snapshot = ws.until(lambda m: m.get('t') == 'instances' and m.get('op') == 'snapshot', 'the whole view again')
    expect(any(e['id'] == emu for e in snapshot['emulators']), 'with the instance', snapshot)
    events.close()
    ws.close()


def test_debug_topics_carry_the_debugger_events_with_the_old_sockets_numbering(s):
    emu = instance(s)
    http(s, 'POST', f'/api/v1/emulator/{emu}/pause', token=TOKEN)
    ws = connect(s, token=None)  # this host: no token needed, as /api/v1/websocket
    ws.send({'op': 'subscribe', 'topics': ['debug.*']})
    ack = ws.until(lambda m: m.get('op') == 'subscribed', 'subscribed')
    expect(set(ack['topics']) == {'debug.events', 'debug.asm_sync'}, 'the debug topics', ack)
    old = WebSocket(port(s), path='/api/v1/websocket')
    old.send({'op': 'subscribe', 'id': 's1', 'emulator': emu, 'topics': ['debug']})
    old.until(lambda m: m.get('op') == 'subscribed', 'the old socket subscribed')

    http(s, 'POST', f'/api/v1/emulator/{emu}/resume', token=TOKEN)
    http(s, 'POST', f'/api/v1/emulator/{emu}/pause', token=TOKEN)
    resumed = ws.until(lambda m: m.get('op') == 'event' and m.get('event') == 'resumed' and m['instance'] == emu,
                       'resumed')
    paused = ws.until(lambda m: m.get('op') == 'event' and m.get('event') == 'paused', 'paused')
    expect(resumed['t'] == 'debug.events' and 'topic' not in resumed, 'in its topic', resumed)
    expect(paused['seq'] > resumed['seq'] and paused['reason'] == 'pause', 'numbered, with the reason', paused)
    old_paused = old.until(lambda m: m.get('op') == 'event' and m.get('event') == 'paused', 'the old socket: paused')
    expect(old_paused['seq'] == paused['seq'], 'one numbering for both sockets', (old_paused, paused))
    ws.send({'op': 'unsubscribe', 'topics': ['debug.*']})
    ws.until(lambda m: m.get('op') == 'unsubscribed', 'unsubscribed')
    old.close()
    ws.close()


def ws_dropped(s):
    """host.clients ws.dropped: the /api/v1/ws connections the server closed for reading too slowly"""
    status, body = http(s, 'GET', '/api/v1/monitor/host.clients')
    expect(status == 200, 'GET /api/v1/monitor/host.clients', body)
    clients = body.get('host.clients', body.get('clients', body))
    return clients['ws']['dropped']


def test_a_client_that_stops_reading_is_closed_as_a_slow_consumer(s):
    """Backpressure: a socket that reads nothing while it asks for the whole instances view again and again. Above the
    high-water mark the hub sends no more views; past the limit it queues `slow_consumer` and closes (host.clients
    ws.dropped counts it). Another client meanwhile is served. No race with the clock: the socket reads nothing until
    the server counted the close, then everything it still gets: the views sent below the mark, then the close (with
    the error before it when the socket drained within the close's grace, else the server drops what it queued)"""
    limit = int(os.environ.get('UNREAL_WS_SLOW_CONSUMER_MS', '10000')) / 1000
    before = ws_dropped(s)
    slow = WebSocket(port(s), token=TOKEN, rcvbuf=4096)
    other = connect(s)
    other.send({'op': 'subscribe', 'topics': ['run.state']})
    other.until(lambda m: m.get('op') == 'subscribed', 'another client')
    slow.send({'op': 'subscribe', 'topics': ['instances']})
    # Spaced, so each is answered with a view of its own (requests within one loop turn share one) until the kernel's
    # buffers (several hundred KiB on loopback: macOS raises a small SO_RCVBUF) and then the mark are full
    asked = 0
    started = time.time()
    while time.time() - started < min(1.0, limit / 2):
        slow.send({'op': 'snapshot'})
        asked += 1
        time.sleep(0.001)
    # Reads nothing until the server closed it as a slow consumer: congested for `limit`, however loaded the host
    deadline = time.time() + limit + 30
    while ws_dropped(s) == before and time.time() < deadline:
        time.sleep(0.05)
    expect(ws_dropped(s) == before + 1, f'the server closes it as a slow consumer within {limit + 30:.0f} s',
           (before, asked))
    other.send({'op': 'unsubscribe', 'topics': ['run.state']})
    other.until(lambda m: m.get('op') == 'unsubscribed', 'the other client is served meanwhile')
    seen = 0
    error = None
    while True:
        message = slow.recv(10)
        if message is None:
            break
        seen += 1
        if message.get('op') == 'error':
            error = message
    expect(slow.closed, 'and the socket is closed', seen)
    if error is not None:
        expect(error['error'] == 'slow_consumer' and error['t'] == 'ws', 'the error before the close is slow_consumer',
               error)
    snapshots = seen - 1 - (error is not None)  # subscribed, the error
    expect(snapshots < asked, f'fewer views than asked for ({snapshots} of {asked}): none sent above the mark',
           snapshots)
    print(f'        a slow consumer: {snapshots} views sent of {asked} asked for, then '
          f'{"slow_consumer and " if error else ""}the close')
    slow.close()
    other.close()


def test_zz_revoking_the_companion_closes_its_socket(s):
    ws, _ = join(s)
    r = s.call('companion_manage', {'action': 'revoke', 'id': 'surface'})
    expect('revoked' in r.text, 'revoked', r.text)
    revoked = ws.until(lambda m: m.get('op') == 'revoked', 'the surface is told')
    expect(revoked['t'] == 'surface', 'in its topic', revoked)
    expect(ws.recv(3) is None and ws.closed, 'and closed')
    ws.close()


if __name__ == '__main__':
    sys.exit(main(globals()))
