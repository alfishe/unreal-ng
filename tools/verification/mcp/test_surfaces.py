#!/usr/bin/env python3
"""surfaces: the control-surface actions of phase S2 (docs/inprogress/2026-10-18-control-surfaces): the surface
context (GET /api/v1/surfaces/context, ctx.set; the last setter wins), ttd.seek_by with its curve, clamp and marks,
run.step_by both ways, on MCP, REST and the WebSocket's calls with the same answers; the targets of Stream Deck SD4
(docs/inprogress/2026-10-19-streamdeck-integration/targeting.md: the window as a view, "no id" follows the focused
view, pins hold and fall back, the readouts, the activity feed, the catalogue fields); and the TTD position read a
companion polls at 50 Hz, measured beside a recording machine (it must not slow the frames)"""

import json
import statistics
import sys
import threading
import time
import urllib.error
import urllib.request

from mcplib import COMPANION_NARROW_TOKEN, expect, main
from test_actions import http, instance, release_mcp_session, webapi, ws_calls


def rest(s, method, path, body=None, session=None):
    """http() carrying an automation session; returns (status, body, the implicit session the reply names)"""
    headers = {'Content-Type': 'application/json'}
    if session:
        headers['X-Unreal-Session'] = session
    request = urllib.request.Request(webapi(s) + path, method=method, headers=headers,
                                     data=json.dumps(body).encode() if body is not None else None)
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return response.status, json.loads(response.read().decode() or 'null'), response.headers.get(
                'X-Unreal-Session', '')
    except urllib.error.HTTPError as error:
        return error.code, json.loads(error.read().decode() or 'null'), error.headers.get('X-Unreal-Session', '')


def end_session(s, emu, session):
    """The implicit session this script's REST client opened ends, so the MCP calls after it are not refused"""
    if session:
        rest(s, 'DELETE', f'/api/v1/emulator/{emu}/control/session', None, session)


def setup(s):
    s.fresh_machine()  # paused
    s.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/audio/mixer/master', 'body': {'muted': True}})


def seek_by(s, emu, delta, rate=0, **params):
    """ttd.seek_by over REST; returns (the result, the implicit session the call opened)"""
    body = {'instance': emu, 'value': {'delta': delta, 'rate': rate}}
    if params:
        body['params'] = params
    status, reply, session = rest(s, 'POST', '/api/v1/controllers/actions/ttd.seek_by', body)
    expect(status == 200 and reply['ok'], f'seek_by {delta} at {rate}', reply)
    return reply['result'], session


def position(s, emu):
    status, body = http(s, 'GET', f'/api/v1/emulator/{emu}/ttd/position')
    expect(status == 200, 'the position', body)
    return body


def test_the_context_is_the_same_on_every_plane_and_the_last_setter_wins(s):
    emu = instance(s)
    release_mcp_session(s)
    status, rest = http(s, 'GET', '/api/v1/surfaces/context?instance=' + emu)
    expect(status == 200 and rest['ok'] and rest['context'] in ('play', 'ttd') and not rest['set'],
           'the default context (play, or ttd with a history)', rest)

    # A Stream Deck page names its context: a companion with the run scope only (ctx.set needs none)
    status, body = http(s, 'POST', '/api/v1/controllers/actions/ctx.set', {'instance': emu, 'value': 'ttd.timecode'},
                        token=COMPANION_NARROW_TOKEN)
    expect(status == 200 and body['ok'] and body['result']['context'] == 'ttd.timecode', 'the companion sets it', body)
    expect(body['result']['by'].startswith('companion:'), 'by the companion', body)

    r = s.call('actions', {'action': 'context'})
    expect('context ttd.timecode' in r.text and 'set by companion:' in r.text, 'MCP names it and the setter', r.text)
    status, rest = http(s, 'GET', '/api/v1/surfaces/context?instance=' + emu)
    socket = ws_calls(s, [('GET', '/api/v1/surfaces/context?instance=' + emu, None)])
    expect(socket[1]['status'] == 200 and socket[1]['body'] == rest == r.structured,
           'REST, the socket and MCP answer the same', (rest, socket[1], r.structured))

    # Then the agent: the last one wins; a name that is none is refused; "" returns to the default
    r = s.call('actions', {'action': 'run', 'id': 'ctx.set', 'value': 'debug.disasm'})
    expect('context debug.disasm (was ttd.timecode)' in r.text, 'the change in the text', r.text)
    r = s.call('actions', {'action': 'run', 'id': 'ctx.set', 'value': 'Debug Disasm'}, ok=False)
    expect(r.is_error and 'Bad Request' in r.text, 'not a context name', r.text)
    status, body = http(s, 'POST', '/api/v1/controllers/actions/ctx.set', {'instance': emu, 'value': ''})
    expect(status == 200 and not body['result']['set'] and body['result']['context'] == body['result']['default'],
           'back to the default', body)
    release_mcp_session(s)


def test_seek_by_scrubs_the_history_with_the_curve_the_clamp_and_marks(s):
    emu = instance(s)
    r = s.call('actions', {'action': 'run', 'id': 'ttd.seek_by', 'value': -1}, ok=False)
    if r.is_error:
        expect('record first' in r.text, 'without a history: record first', r.text)
    s.call('time_travel', {'action': 'start'})
    s.call('control_execution', {'action': 'run_frames', 'frames': 160})
    s.call('time_travel', {'action': 'stop'})
    at = position(s, emu)
    expect(at['history'] and at['first_frame'] < at['session_end']['frame'] and 48 < at['fps'] < 49,
           'the cursor: a history, its first frame, the Pentagon frame rate', at)
    first, end = at['first_frame'], at['session_end']['frame']
    here = at['current']['frame']

    # MCP first (its own implicit session), then REST, then the socket: each ends its session before the next
    r = s.call('actions', {'action': 'run', 'id': 'ttd.seek_by', 'value': {'delta': -2, 'rate': 0}})
    expect(f'frame {here} -> {here - 2}' in r.text and 'marks:' in r.text, 'MCP names the move and the marks', r.text)
    release_mcp_session(s)

    # One step of 2 s at full speed: 98 frames back, over two second boundaries: one mark, nearest the target
    result, session = seek_by(s, emu, -1, 400, unit='seconds')
    expect(result['frames'] == -98 and result['halt_reason'] == 'target', '2 s = 98 frames on a Pentagon', result)
    expect(result['arrived_at']['frame'] == result['frame'] == max(first, result['from'] - 98), 'it arrived', result)
    kinds = [m['kind'] for m in result['marks']]
    expect(kinds and kinds[0] in ('second', 'ten_seconds', 'edge'), 'a second crossed', result['marks'])

    # Far back: clamped to the first frame with an edge mark, never a target before it
    result, _ = seek_by(s, emu, -100, 400)
    expect(result['frame'] == first and result['marks'][0] == {'kind': 'edge', 'frame': first}, 'the edge', result)
    # Far forward: the end, not out_of_range
    result, _ = seek_by(s, emu, 50, 400)
    expect(result['frame'] == end and result['halt_reason'] == 'target' and result['marks'][0]['kind'] == 'edge',
           'clamped to the end', result)
    end_session(s, emu, session)

    # The socket's call answers the same shape; three frames back at a slow rate
    socket = ws_calls(s, [('POST', '/api/v1/controllers/actions/ttd.seek_by',
                           {'instance': emu, 'value': {'delta': -3, 'rate': 1}})])
    body = socket[1]['body']
    expect(socket[1]['status'] == 200 and body['result']['arrived_at']['frame'] == end - 3, 'over the socket',
           socket[1])
    expect(position(s, emu)['current']['frame'] == end - 3, 'the position follows', end - 3)
    ws_calls(s, [('DELETE', f'/api/v1/emulator/{emu}/control/session', None, socket[1].get('session', ''))])


def test_step_by_steps_in_forward_and_back_through_the_history(s):
    r = s.call('actions', {'action': 'run', 'id': 'run.step_by', 'value': -3})
    expect('3 step(s) back' in r.text, 'back through the history', r.text)
    r = s.call('actions', {'action': 'run', 'id': 'run.step_by', 'value': 40})
    expect('16 step(s) forward' in r.text, 'forward: step in, 16 at most', r.text)
    release_mcp_session(s)


def test_no_id_follows_the_focused_view_and_pins_hold(s):
    emu = instance(s)
    release_mcp_session(s)
    status, views = http(s, 'GET', '/api/v1/surfaces/views')
    expect(status == 200 and views['ok'], 'the views', views)
    main_view = [v for v in views['views'] if v['owner'] == 'unreal-qt']
    expect(len(main_view) == 1 and main_view[0]['active'] == emu, 'the main window is a view showing the instance',
           views)
    main_id = main_view[0]['id']
    expect(views['followed']['instance'] == emu and views['followed']['view']['id'] == main_id and
           views['followed']['mode'] == 'follow', 'the followed instance is the window\'s', views['followed'])
    r = s.call('actions', {'action': 'views'})
    expect(f'({emu}) in {main_id} Main window' in r.text and r.structured == views, 'MCP: the same, in the text',
           r.text)
    socket = ws_calls(s, [('GET', '/api/v1/surfaces/views', None)])
    expect(socket[1]['status'] == 200 and socket[1]['body'] == views, 'the socket call answers the same', socket[1])

    # A second instance, and a second window showing it (a view of the automation plane, as a test hook)
    second = s.call('emulator_manage', {'action': 'create', 'model': '48K'}).structured
    other = second.get('id') or second.get('emulator_id') or second.get('emulator', {}).get('id')
    expect(other and other != emu, 'a second instance', second)
    status, views = http(s, 'GET', '/api/v1/surfaces/views')
    main_active = [v for v in views['views'] if v['id'] == main_id][0]['active']
    status, added = http(s, 'POST', '/api/v1/surfaces/views', {'name': 'Window 2', 'active': emu, 'activate': True})
    expect(status == 200 and added['followed']['instance'] == emu and added['followed']['view']['name'] == 'Window 2',
           'the focused window is followed', added)
    window2 = added['view']['id']
    try:
        # "No id" acts on the followed instance (T-1), whatever is selected
        status, listed = http(s, 'GET', '/api/v1/controllers/actions')
        expect(status == 200 and listed['instance'] == emu, '"no id" is the followed instance', listed['instance'])
        expect(all('description' in a and 'toggle' in a for a in listed['actions']) and listed.get('rev', 0) > 0,
               'the catalogue fields: description, toggle, rev', listed['actions'][0])
        toggles = {a['id'] for a in listed['actions'] if a['toggle']}
        expect({'run.pause_toggle', 'mach.turbo_toggle', 'audio.mute_toggle'} <= toggles, 'the toggles', toggles)
        status, moved = http(s, 'PUT', f'/api/v1/surfaces/views/{window2}', {'active': other})
        status, context = http(s, 'GET', '/api/v1/surfaces/context')
        expect(status == 200 and context['instance'] == other, 'the window shows the other one: no id follows', context)

        # A surface pinned to the main window keeps it while the focus is elsewhere; an instance pin stays put
        status, opened = http(s, 'POST', '/api/v1/surfaces/sessions', {'name': 'REST deck', 'target': 'view:' + main_id})
        expect(status == 200 and opened['session']['target']['instance'] == main_active and
               opened['session']['target']['mode'] == 'view', 'pinned to the main window', opened)
        deck = opened['session']['id']
        r = s.call('actions', {'action': 'sessions'})
        expect(f'{deck} REST deck [api] ->' in r.text, 'MCP lists the session and its target', r.text)
        r = s.call('actions', {'action': 'pin', 'session': deck, 'pin': 'instance:' + other})
        expect(f'({other})' in r.text and 'instance:' in r.text, 'MCP pins it to the instance', r.text)
        status, target = http(s, 'GET', '/api/v1/surfaces/target?session=' + deck)
        expect(target['target']['instance'] == other and target['target']['view']['id'] == window2,
               'the target names the window showing it', target)
        status, bad = http(s, 'PUT', f'/api/v1/surfaces/sessions/{deck}/pin', {'target': 'view:v9999'})
        expect(status == 404 and bad['error'] == 'Not Found', 'an unknown view', bad)

        # The readouts and the activity feed of the followed instance
        status, read = http(s, 'GET', '/api/v1/surfaces/readouts?names=run.state,machine,toggle.mach.turbo_toggle')
        expect(status == 200 and read['instance'] == other and read['values']['machine']['model'] == '48K' and
               read['values']['run.state']['state'] in ('running', 'paused', 'stopped') and
               read['values']['toggle.mach.turbo_toggle']['on'] in (True, False), 'the readouts', read)
        status, feed = http(s, 'GET', '/api/v1/surfaces/activity?limit=1')
        last = feed['last']
        status, ran = http(s, 'POST', '/api/v1/controllers/actions/dbg.breakpoint_hits_reset', {'instance': other})
        expect(status == 200 and ran['ok'], 'an action', ran)
        status, feed = http(s, 'GET', f'/api/v1/surfaces/activity?since={last}&instance={other}')
        expect(status == 200 and [e['id'] for e in feed['entries']] == ['dbg.breakpoint_hits_reset'] and
               feed['entries'][0]['origin'] == 'webapi', 'the activity names it', feed)
        r = s.call('actions', {'action': 'activity', 'since': last})
        expect('dbg.breakpoint_hits_reset' in r.text and 'by webapi' in r.text, 'MCP: the activity in the text', r.text)

        # The window closes: the focus falls back to the main window; the instance goes: the pin falls back
        status, removed = http(s, 'DELETE', f'/api/v1/surfaces/views/{window2}')
        window2 = None
        expect(status == 200 and removed['followed']['view']['id'] == main_id, 'back to the main window', removed)
        s.call('emulator_manage', {'action': 'destroy', 'target': other}, ok=False)
        status, session = http(s, 'GET', '/api/v1/surfaces/sessions/' + deck)
        expect(session['session']['pin'] == 'follow' and 'gone' in session['session'].get('notice', ''),
               'the instance pin fell back, with a notice', session)
        status, refused = http(s, 'DELETE', f'/api/v1/surfaces/views/{main_id}')
        expect(status == 409, 'the main window is the client\'s', refused)
        status, closed = http(s, 'DELETE', '/api/v1/surfaces/sessions/' + deck)
        expect(status == 200, 'the session closes', closed)
    finally:
        if window2:
            http(s, 'DELETE', f'/api/v1/surfaces/views/{window2}')
        release_mcp_session(s)


def test_the_position_polled_at_50_hz_does_not_slow_a_recording_machine(s):
    """The Stream Deck strip polls /ttd/position 50 times a second (2026-10-18 a poll parked the recording machine
    each time); here the machine records at 1x for 3 s alone and 3 s beside the poller: the same frames per second"""
    emu = instance(s)
    release_mcp_session(s)
    # Leave the past: record from here (the resume runs the machine)
    status, body, session = rest(s, 'POST', f'/api/v1/emulator/{emu}/ttd/resume', {})
    expect(status == 200 and body.get('resumed'), 'recording again from the position', body)
    if position(s, emu)['paused']:
        rest(s, 'POST', f'/api/v1/emulator/{emu}/resume', None, session)
    try:
        def frames_in(seconds, poll):
            latencies = []
            stop = threading.Event()

            def poller():
                next_at = time.monotonic()
                while not stop.is_set():
                    started = time.monotonic()
                    position(s, emu)
                    latencies.append((time.monotonic() - started) * 1000)
                    next_at += 0.02
                    time.sleep(max(0.0, next_at - time.monotonic()))

            thread = threading.Thread(target=poller) if poll else None
            before = position(s, emu)['current']['frame']
            started = time.monotonic()
            if thread:
                thread.start()
            time.sleep(seconds)
            stop.set()
            if thread:
                thread.join()
            after = position(s, emu)['current']['frame']
            return (after - before) / (time.monotonic() - started), latencies

        at = position(s, emu)
        expect(at['state'] == 'recording' and not at['paused'], 'recording, running', at)
        alone, _ = frames_in(3, False)
        polled, latencies = frames_in(3, True)
        latencies.sort()
        p50 = statistics.median(latencies)
        p99 = latencies[int(len(latencies) * 0.99) - 1]
        print(f'        frames/s alone {alone:.2f}, with 50 Hz polling {polled:.2f}; {len(latencies)} polls, '
              f'p50 {p50:.2f} ms, p99 {p99:.2f} ms, max {latencies[-1]:.2f} ms')
        expect(abs(polled - alone) <= 0.03 * alone, 'the poller does not slow the frames', (alone, polled))
        expect(len(latencies) >= 120, 'about 50 polls a second', len(latencies))
        expect(p99 < 25, 'a poll answers without waiting for the machine', p99)
    finally:
        rest(s, 'POST', f'/api/v1/emulator/{emu}/pause', None, session)
        rest(s, 'POST', f'/api/v1/emulator/{emu}/ttd/stop', None, session)
        end_session(s, emu, session)


def test_host_devices_show_role_layers_context_and_the_output_verb_checks(s):
    """S3: the devices verb names each device's role, layers and context on MCP, REST and the socket alike; the
    output verb refuses what is not a control surface's output; a connected Surface Dial plays a pattern"""
    status, rest = http(s, 'GET', '/api/v1/controllers/devices')
    if status == 503:
        return  # a process without the Qt client: no host input
    expect(status == 200 and rest['ok'], 'the host devices', rest)
    keyboard = next((d for d in rest['devices'] if d['id'] == 'kbd:system'), None)
    expect(keyboard is not None and keyboard['role'] == 'debugger-remote' and keyboard['role_set'],
           'the window keyboard: a remote by choice', rest)
    expect(keyboard['layers'][0] == 'debug' and keyboard['outputs'] == [], 'its layers, no outputs', keyboard)
    socket = ws_calls(s, [('GET', '/api/v1/controllers/devices', None)])
    expect(socket[1]['status'] == 200 and socket[1]['body']['devices'] == rest['devices'], 'the socket agrees', socket)
    r = s.call('controllers', {'action': 'devices'})
    expect('kbd:system' in r.text and 'role debugger-remote' in r.text and 'layers debug' in r.text,
           'MCP names the role and the layers in its text', r.text)

    status, body = http(s, 'POST', '/api/v1/controllers/output', {'device': 'kbd:system', 'pattern': 'tick'})
    expect(status == 503 and body['error'] == 'unavailable', 'a keyboard plays no patterns', body)
    status, body = http(s, 'POST', '/api/v1/controllers/output', {'device': 'kbd:system', 'pattern': 'thud'})
    expect(status == 400 and body['error'] == 'bad-request', 'not a pattern', body)
    status, body = http(s, 'GET', '/api/v1/controllers/output?device=kbd:system&pattern=tick')
    expect(status == 405, 'output is a POST', body)
    r = s.call('controllers', {'action': 'output', 'device': 'kbd:system', 'marks': 'minute'}, ok=False)
    expect(r.is_error and 'unknown mark' in r.text, 'MCP: an unknown mark', r.text)

    dial = next((d for d in rest['devices'] if d['family'] == 'surface-dial'), None)
    if dial is None:
        return  # no Dial connected to this host: the rest is the owner's
    expect(dial['role'] == 'debugger-remote' and 'haptic' in dial['outputs'], 'the Dial: a remote with outputs', dial)
    r = s.call('controllers', {'action': 'output', 'device': dial['id'], 'pattern': 'tick'})
    expect('tick on' in r.text, 'a tick on the Dial', r.text)


def test_host_devices_show_battery_light_and_notice_and_the_light_verb_checks(s):
    """Every device in devices carries its battery ({known, text}: unknown when the OS says nothing), whether its light
    can be set and, when something is missing, a notice; MCP's text names them; the light verb refuses a keyboard, a
    colour it cannot read and a GET, and sets a pad's light when one has it"""
    status, rest = http(s, 'GET', '/api/v1/controllers/devices')
    if status == 503:
        return  # a process without the Qt client: no host input
    expect(status == 200 and rest['ok'], 'the host devices', rest)
    for device in rest['devices']:
        battery = device.get('battery')
        expect(battery is not None and isinstance(battery.get('text'), str) and 'known' in battery,
               'a battery on every device', device)
        expect(battery['known'] or (battery['text'] == 'unknown' and 'level' not in battery), 'unknown is honest',
               battery)
        expect('available' in device.get('light', {}), 'the light on every device', device)
        expect(device['light']['available'] or device['light'].get('reason'), 'no light says why', device)
        expect(device.get('notice', 'x') != '', 'a notice is absent or says something', device)
    keyboard = next((d for d in rest['devices'] if d['id'] == 'kbd:system'), None)
    expect(keyboard is not None and not keyboard['battery']['known'] and not keyboard['light']['available'],
           'the window keyboard: no battery known, no light', keyboard)
    r = s.call('controllers', {'action': 'devices'})
    expect('battery unknown, light none' in r.text, 'MCP names the battery and the light in its text', r.text)

    status, body = http(s, 'POST', '/api/v1/controllers/light', {'device': 'kbd:system', 'color': '#ff0000'})
    expect(status == 503 and body['error'] == 'unavailable' and 'keyboards' in body['message'],
           'a keyboard has no light the emulator sets', body)
    status, body = http(s, 'POST', '/api/v1/controllers/light', {'device': 'kbd:system', 'color': 'orange'})
    expect(status == 400 and body['error'] == 'bad-request', 'not a colour', body)
    status, body = http(s, 'GET', '/api/v1/controllers/light?device=kbd:system&color=off')
    expect(status == 405, 'light is a POST', body)
    socket = ws_calls(s, [('POST', '/api/v1/controllers/light', {'device': 'kbd:system', 'color': 'off'})])
    expect(socket[1]['status'] == 503, 'the socket call answers as REST', socket)
    r = s.call('controllers', {'action': 'light', 'device': 'nope', 'color': 'off'}, ok=False)
    expect(r.is_error and 'unknown-device' in r.text, 'MCP: an unknown device', r.text)

    pad = next((d for d in rest['devices'] if d['light']['available']), None)
    if pad is None:
        return  # no pad with a settable light on this host: the rest is the owner's
    r = s.call('controllers', {'action': 'light', 'device': pad['id'], 'color': '#0040ff'})
    expect('light #0040ff on' in r.text, 'the pad takes the colour', r.text)


if __name__ == '__main__':
    sys.exit(main(globals()))
