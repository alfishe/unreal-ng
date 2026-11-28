#!/usr/bin/env python3
"""A long call of one client holds nobody else back: while a synchronous TTD search or a `jobs wait` is unanswered,
other MCP calls and REST requests are answered (the MCP loopback has a connection per unanswered call, long verbs run
off the HTTP workers), and an operation a long call logs still names MCP as its source. Counts answers, not times"""

import json
import sys
import tempfile
import threading
import urllib.request

from mcplib import Session, expect, main

FRAMES = 1500   # the search below replays every one: long enough for hundreds of quick calls
# Runs every frame (the ROM's interrupt), with a condition that never holds
NEVER = {'action': 'reverse_continue', 'pcs': ['0x0038'], 'all': True, 'condition': 'FRAME > 100000000'}
# Fewer than this answered meanwhile: they waited for the long call (before the fix: 0 to 2)
ENOUGH = 50


def rest(s):
    """A REST request on a connection of its own: the server hands those to its HTTP workers in turn"""
    with urllib.request.urlopen(s.url[:-len('/mcp')] + '/api/v1/emulator', timeout=120) as response:
        expect('emulators' in json.loads(response.read().decode()), 'the instance list')


def answered_while(s, long_call):
    """Starts long_call(session) on a thread (a second client) and counts the quick MCP and REST pairs answered before
    it returns; returns (pairs, its result)"""
    other = Session(s.url, s.model)
    other.next_id = 1000000
    outcome = {}

    def work():
        try:
            outcome['result'] = long_call(other)
        except BaseException as failure:   # reported by the caller's thread
            outcome['failure'] = failure

    thread = threading.Thread(target=work)
    thread.start()
    pairs = 0
    while thread.is_alive() and pairs < ENOUGH:
        s.call('emulator_manage', {'action': 'list'})
        rest(s)
        if thread.is_alive():
            pairs += 1
    thread.join(timeout=300)
    expect(not thread.is_alive(), 'the long call ended')
    if 'failure' in outcome:
        raise outcome['failure']
    return pairs, outcome['result']


def setup(s):
    s.fresh_machine()
    s.call('time_travel', {'action': 'start'})
    s.call('control_execution', {'action': 'run_frames', 'frames': FRAMES})
    s.call('time_travel', {'action': 'stop'})


def test_a_synchronous_search_holds_nobody_back(s):
    pairs, result = answered_while(s, lambda other: other.call('time_travel', NEVER))
    expect(result.structured['count'] == 0, 'the search answered', result.text)
    expect(pairs >= ENOUGH, 'quick calls were answered during the search', pairs)


def test_a_jobs_wait_holds_nobody_back(s):
    job = s.call('time_travel', dict(NEVER, **{'async': True})).structured['job']
    pairs, result = answered_while(s, lambda other: other.wait_job(job, timeout=300))
    expect(result.structured['state'] == 'done', 'the job ended', result.text)
    expect(pairs >= ENOUGH, 'quick calls were answered during the wait', pairs)


def test_a_long_verb_logs_mcp_as_its_source(s):
    with tempfile.TemporaryDirectory() as folder:
        path = folder + '/parallel.ttd'
        s.call('time_travel', {'action': 'dump', 'path': path})
        s.call('time_travel', {'action': 'load', 'path': path})
        log = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/emulator/{id}/operations'}).structured
        entries = log.get('body', log)['entries']
        loads = [entry for entry in entries if entry['summary'].startswith('load ')]
        expect(loads, 'the load is in the log', entries[-5:])
        expect(all(entry['source'] == 'mcp' for entry in loads), 'it names MCP as its source', loads)


if __name__ == '__main__':
    sys.exit(main(globals()))
