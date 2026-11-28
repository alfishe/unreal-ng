#!/usr/bin/env python3
"""find_last and reverse_continue as jobs (RQ-1): with async=true the search answers its job id at once, the job
ends with the answer the call gives without async, its progress counts the frames of the window and the hits, a
cancel ends it before the next frame with what it found and the machine where it stood; a bad request is refused
and no job is made"""

import sys

from mcplib import expect, main

FRAMES = 400
# Runs every frame (the ROM's interrupt), with a condition that never holds: every frame of the window is replayed
NEVER = {'action': 'reverse_continue', 'pcs': ['0x0038'], 'all': True, 'condition': 'FRAME > 100000000'}


def position(s):
    answer = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/emulator/{id}/ttd/position'}).structured
    return answer.get('body', answer)['current']


def jobs_of(s):
    return [j['job'] for j in s.call('jobs', {'action': 'list', 'state': 'all', 'limit': 1000}).structured['jobs']]


def as_job(s, args):
    """The call with async=true, waited for: the job as the jobs tool answers it"""
    started = s.call('time_travel', dict(args, **{'async': True}))
    job = started.structured.get('job')
    expect(job and f'Job {job}' in started.text, 'the answer names the job', started.text)
    expect(started.structured['kind'] == 'ttd.' + args['action'].replace('_', '-'), 'the kind', started.structured)
    waited = s.wait_job(job, timeout=120)
    expect(not waited.is_error and waited.structured['ended'], 'the job ended', waited.text)
    return waited


def verb_answer(structured):
    """The verb's own answer: the WebAPI adds automation_session to a call of a client without a control session"""
    return {name: value for name, value in structured.items() if name != 'automation_session'}


def expect_job_equals_call(s, args):
    sync = s.call('time_travel', args)
    job = as_job(s, args)
    expect(job.structured['state'] == 'done', 'done', job.text)
    expect(job.structured['result'] == verb_answer(sync.structured), 'the job holds the answer of the call',
           (job.structured['result'], sync.structured))
    return sync, job


def setup(s):
    s.fresh_machine()
    mixer = s.call('audio_mixer')
    expect('master: muted' in mixer.text, 'the machine is muted', mixer.text)
    s.call('time_travel', {'action': 'start'})
    s.call('control_execution', {'action': 'run_frames', 'frames': FRAMES})
    s.call('time_travel', {'action': 'stop'})


def test_find_last_as_a_job_answers_as_the_call(s):
    # FRAMES (0x5C78) is written by the interrupt every frame: a list cut at its limit, and the writes that changed it
    sync, job = expect_job_equals_call(s, {'action': 'find_last', 'addr': '0x5C78', 'all': True, 'limit': 20})
    expect(sync.structured['count'] == 20 and sync.structured['truncated'], 'a cut list', sync.text)
    expect(job.structured['progress']['unit'] == 'frames' and job.structured['progress']['hits'] == 20,
           'frames and hits in the progress', job.structured['progress'])
    expect_job_equals_call(s, {'action': 'find_last', 'addr': '0x5C78', 'all': True, 'changes_only': True,
                               'limit': 10})
    # A read by code that never runs: every frame that read the byte is replayed, none matches
    sync, job = expect_job_equals_call(s, {'action': 'find_last', 'addr': '0x5C78', 'access': 'read',
                                           'pc_from': '0xFFF0', 'pc_to': '0xFFFF', 'all': True,
                                           'from_frame': sync.structured['covered_to'] - 50})
    expect(sync.structured['count'] == 0, 'no such read', sync.text)
    progress = job.structured['progress']
    expect(progress['done'] == progress['total'] and 50 <= progress['total'] <= 52, 'the window walked to its floor',
           progress)


def test_reverse_continue_as_a_job_answers_as_the_call(s):
    sync, job = expect_job_equals_call(s, {'action': 'reverse_continue', 'pcs': ['0x0038'], 'all': True, 'limit': 20})
    expect(sync.structured['count'] == 20, 'the interrupt ran in every frame', sync.text)
    expect_job_equals_call(s, {'action': 'reverse_continue', 'pcs': ['0x0038', '0xFFF0'], 'first_per_pc': True})
    sync, job = expect_job_equals_call(s, NEVER)
    expect(sync.structured['count'] == 0 and not sync.structured['matched'], 'the condition never holds', sync.text)
    progress = job.structured['progress']
    expect(progress['done'] == progress['total'] and progress['total'] >= FRAMES - 1 and progress['hits'] == 0,
           'every frame of the history walked', progress)
    expect(f"job {job.structured['job']}" in job.text and 'done' in job.text and 'matched' in job.text,
           'id, state and result in the text', job.text)


def test_a_cancel_ends_the_search_with_what_it_found(s):
    before = position(s)
    whole = verb_answer(s.call('time_travel', NEVER).structured)
    cancelled = None
    for _ in range(8):   # a cancel that came after the job's end is tried again
        job = s.call('time_travel', dict(NEVER, **{'async': True})).structured['job']
        asked = s.call('jobs', {'action': 'cancel', 'job': job}, ok=False)
        waited = s.wait_job(job, timeout=120)
        # Cancelled inside its walk: the result is the verb's (a cancel before the work began has none)
        if (not asked.is_error and waited.structured['state'] == 'cancelled'
                and (waited.structured.get('result') or {}).get('cancelled')):
            cancelled = waited.structured
            break
    expect(cancelled is not None, 'a cancel reached a running search in 8 tries')
    result = cancelled['result']
    expect(result['cancelled'] is True and result['truncated'] is True and result['count'] == 0,
           'the result says cancelled', result)
    progress = cancelled['progress']
    expect(0 < progress['done'] < progress['total'], 'it stopped inside the window', progress)
    expect(result['covered_from'] > whole['covered_from'] and result['covered_to'] == whole['covered_to'],
           'the part it searched', result)
    tail = s.call('time_travel', dict(NEVER, before=result['next_before'])).structured
    expect(tail['covered_from'] == whole['covered_from'] and tail['covered_to'] <= result['covered_from'],
           'next_before goes on where it stopped', tail)
    expect(position(s) == before, 'the machine where it stood', (before, position(s)))
    expect(verb_answer(s.call('time_travel', NEVER).structured) == whole, 'the same search again answers as before')


def test_a_bad_request_is_refused_and_makes_no_job(s):
    known = jobs_of(s)
    r = s.call('time_travel', {'action': 'reverse_continue', 'pcs': ['0x0038'], 'condition': 'A ==', 'async': True},
               ok=False)
    expect(r.is_error and '400' in r.text and 'condition' in r.text, 'a condition that does not compile: 400', r.text)
    r = s.call('time_travel', {'action': 'find_last', 'addr': '0x5C78', 'access': 'read', 'changes_only': True,
                               'async': True}, ok=False)
    expect(r.is_error and 'changes_only' in r.text, 'changes_only of a read: refused', r.text)
    r = s.call('jobs', {'action': 'start', 'domain': 'ttd', 'verb': 'find-last', 'options': {'addr': '0x10000'}},
               ok=False)
    expect(r.is_error and 'addr' in r.text, 'jobs start checks the request too', r.text)
    expect(jobs_of(s) == known, 'no job was made', (known, jobs_of(s)))


if __name__ == '__main__':
    sys.exit(main(globals()))
