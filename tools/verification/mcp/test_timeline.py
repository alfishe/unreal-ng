#!/usr/bin/env python3
"""The GS debugger's twin timeline, phase 1 (gs-debugger design §5.4, §7.2): the main CPU and the card's CPU on one
clock. Paused, inspect_state's 'timeline' aspect and GET /debug/timeline give NOW, the gap, each lane's recent blocks
with their measured widths and the next instruction; the first read arms the histories (a lease), so blocks appear
once the machine ran armed. Running, the read gives the density per frame (never a pause). The debugger snapshot's
pause carries both lanes' positions and the timeline; the monitoring name debug.timeline the published density. A
released lease turns the histories off again"""

from mcplib import expect, main

TIMELINE = '/api/v1/emulator/{id}/debug/timeline'


def get(s, path):
    r = s.call('invoke_api', {'method': 'GET', 'path': path}, ok=False)
    structured = r.structured or {}
    return structured.get('status'), structured.get('body', {}), r.text


def release(s):
    for client in ('mcp', 'rest'):
        s.call('invoke_api', {'method': 'POST', 'path': TIMELINE + '/arm', 'body': {'on': False, 'client': client}},
               ok=False)


def setup(s):
    s.fresh_machine()  # PENTAGON with its NeoGS
    release(s)


def test_the_first_read_arms_and_the_paused_read_has_both_lanes(s):
    status, body, text = get(s, TIMELINE + '?client=rest&lease_ms=60000')
    expect(status == 200 and body.get('available') is True, 'a card CPU is fitted', text)
    expect(body.get('started_now') is True and body.get('armed') is True, 'the first read armed it', text)
    expect(body.get('cpu') == 'gs' and [lane.get('cpu') for lane in body.get('lanes', [])] == ['main', 'gs'],
           'the main lane and the card lane', text)

    # Armed: the histories record while it runs
    s.call('control_execution', {'action': 'run_frames', 'frames': 3})
    s.call('control_execution', {'action': 'pause'}, ok=False)
    status, body, text = get(s, TIMELINE + '?client=rest&span=2000&recent=32')
    expect(status == 200 and body.get('state') == 'paused', 'a paused read', text)
    now = body.get('now', {})
    expect(isinstance(now.get('frame'), int) and 'gap_t' in now, 'NOW: frame, T and the gap', text)
    recent = body.get('recent', {})
    for lane in ('main', 'gs'):
        blocks = recent.get(lane, [])
        expect(blocks, f'recent blocks of the {lane} lane', text)
        expect(all(b['at'] <= 0.001 for b in blocks), f'{lane}: the recent blocks are in the past of NOW', blocks[-3:])
        closed = [b for b in blocks if not b.get('open')]
        expect(closed and all(b['duration_t'] > 0 for b in closed), f'{lane}: measured widths', blocks[-3:])
        expect(all(isinstance(b.get('pc'), int) and b.get('text') for b in blocks if b.get('kind') == 'instruction'),
               f'{lane}: each instruction with its PC and text', blocks[-3:])
    expect(all('duration_units' in b for b in recent['gs'] if not b.get('open')), "the card's own units", text)
    expect('main' in body.get('next', {}) and 'gs' in body.get('next', {}), 'the next instruction of each lane', text)
    expect('NOW' in body.get('text', ''), 'the text names NOW', body.get('text'))


def test_the_mcp_aspect_shows_the_text(s):
    r = s.call('inspect_state', {'aspects': ['timeline'], 'cpu': 'gs'})
    expect('[timeline]' in r.text and 'NOW' in r.text and 'blocks' in r.text, 'the agent reads the timeline', r.text)
    r = s.call('inspect_state', {'aspects': ['timeline'], 'cpu': 'ms'}, ok=False)
    expect('[timeline] unavailable' in r.text, 'no MultiSound: said so', r.text)


def test_a_card_stop_carries_both_positions(s):
    pc = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/emulator/{id}/state/audio/gs'}).structured
    pc = pc['body']['cpu']['pc']
    s.call('control_execution', {'action': 'bp_clear', 'cpu': 'gs'}, ok=False)
    s.call('control_execution', {'action': 'bp_add', 'address': pc, 'cpu': 'gs'})
    try:
        s.call('control_execution', {'action': 'resume'})
        snapshot = None
        for _ in range(50):  # the card returns to its idle loop within a frame or two
            status, snapshot, text = get(s, '/api/v1/emulator/{id}/debug/snapshot')
            if snapshot.get('state') == 'paused':
                break
        expect(snapshot.get('state') == 'paused', 'the card breakpoint stopped the machine', text)
        pause = snapshot.get('pause', {})
        positions = pause.get('positions', [])
        cpus = [p.get('cpu') for p in positions]
        expect(cpus[:2] == ['main', 'gs'], 'both lanes at the stop', positions)
        card = positions[1]
        expect(card.get('pc') == pc and 'gap_t' in card and 'now_t' in card and card.get('clock_hz', 0) > 0,
               'the card lane: PC, gap, NOW on main clock, clock', card)
        expect(isinstance(pause.get('timeline'), dict) and pause['timeline'].get('available') is True,
               'the snapshot carries the timeline', pause.get('timeline'))
    finally:
        s.call('control_execution', {'action': 'pause'}, ok=False)
        s.call('control_execution', {'action': 'bp_clear', 'cpu': 'gs'}, ok=False)


def test_running_reads_the_density_and_the_monitor_publishes_it(s):
    s.call('control_execution', {'action': 'resume'})
    try:
        body = {}
        for _ in range(200):  # each read is a published value: no pause, a frame passes between some of them
            status, body, text = get(s, TIMELINE + '?client=rest&density=20')
            expect(status == 200, 'a running read answers', text)
            if body.get('state') == 'running' and len(body.get('density', [])) >= 3:
                break
        expect(body.get('state') == 'running' and len(body.get('density', [])) >= 3, 'the density per frame', body)
        tick = body['density'][-1]
        expect('main' in tick and 'card' in tick and tick['card'].get('steps', 0) > 0,
               "a tick: the main lane's port accesses and the card's work", tick)
        expect('recent' not in body, 'running: no blocks (they would need a pause)', list(body))
        status, monitor, text = get(s, '/api/v1/monitor/debug.timeline')
        expect(status == 200 and monitor.get('armed') is True and monitor.get('density'), 'debug.timeline', text)
    finally:
        s.call('control_execution', {'action': 'pause'}, ok=False)


def test_the_last_release_disarms(s):
    release(s)
    r = s.call('invoke_api', {'method': 'POST', 'path': TIMELINE + '/arm', 'body': {'on': False, 'client': 'rest'}})
    answer = (r.structured or {}).get('body', {})
    expect(answer.get('armed') is False and answer.get('leases') == [], 'no lease left: off', r.text)
    status, body, text = get(s, TIMELINE + '?arm=false')
    expect(status == 200 and body.get('armed') is False, 'a read without a lease leaves it off', text)


if __name__ == '__main__':
    raise SystemExit(main(globals()))
