"""The NextZXOS Browser navigation of testdata/machines/zxnext/browser-navigation.json: the keys, hold frames, waits, the idle loop,
the search rule and the Browser's order, shared by drive.py / suite.py / collection.py (real time), regress.py (frame-counted) and
NexBrowserRun_Test (core/tests/loaders/nex/nexbrowser_test.cpp, the same file read by the C++ test).

Frame-counted drivers pass a machine with tap(key, hold, after), combo(keys, hold, after), type(text, delay, frames), run(frames)
and run_until_idle(limit, step, settle); real-time drivers one with tap(key, frames), combo(keys, frames), idle(settle) (drive.Machine).
"""
import json, os, re, time

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..'))
PATH = os.path.join(REPO, 'testdata', 'machines', 'zxnext', 'browser-navigation.json')

with open(PATH) as _f:
    NAV = json.load(_f)

IDLE_PC = int(NAV['idle']['pc'], 16)
SEARCHABLE = re.compile(NAV['search']['searchable'])
STEPS = NAV['steps']
BASIC_RUN = NAV['basic_run']


def entries(folder, root=False):
    """The Browser's listing of host folder @p folder: '.' and '..' (not at the card root), then the names case-insensitively"""
    names = sorted(os.listdir(folder), key=lambda n: n.lower())
    dots = NAV['listing']['dot_entries'] if not root or NAV['listing']['dot_entries_at_root'] else []
    return list(dots) + names


def search_prefix(listing, name):
    """The shortest prefix of @p name's searchable part that only one entry of @p listing starts with; None: count down"""
    match = SEARCHABLE.match(name)
    if not match:
        return None
    text = match.group(0)
    lower = [e.lower() for e in listing]
    for length in range(1, len(text) + 1):
        if sum(e.startswith(text[:length].lower()) for e in lower) == 1:
            return text[:length]
    return None


def run_steps(name):
    """The steps that run the file @p name from the Browser (by its extension)"""
    ext = name.rsplit('.', 1)[-1].lower() if '.' in name else ''
    return NAV['run'].get(ext, NAV['run']['*'])


def type_frames(text):
    """Frames a frame driver runs after queueing @p text"""
    t = STEPS['type']
    return len(text) * t['frames_per_char'] + t['after']


# ---- frame-counted drivers ----

def frame_step(m, step):
    """One step (a name of "steps" or a step object) on a frame-counted machine; False when its idle wait ran out"""
    if isinstance(step, str):
        step = STEPS[step]
    if 'key' in step:
        m.tap(step['key'], step['hold'], step['after'])
    elif 'combo' in step:
        m.combo(step['combo'], step['hold'], step['after'])
    elif 'frames' in step:
        m.run(step['frames'])
    idle = step.get('idle')
    if idle:
        return m.run_until_idle(idle['frames'], idle.get('step', 5), idle.get('settle', 0))
    return True


def frame_type(m, text, wait_idle=True):
    """Type @p text (the type step) on a frame-counted machine"""
    t = STEPS['type']
    m.type(text, t['delay'], type_frames(text))
    if wait_idle:
        idle = t['idle']
        m.run_until_idle(idle['frames'], idle.get('step', 5), idle.get('settle', 0))


def frame_reach(m, folder, name, root=False):
    """Cursor onto @p name in the Browser listing of host folder @p folder: H + the shortest unique prefix, else count down"""
    listing = entries(folder, root)
    prefix = search_prefix(listing, name)
    if prefix:
        frame_step(m, 'search')
        frame_type(m, prefix)
        frame_step(m, 'accept')
        return 'search:' + prefix
    for _ in range(listing.index(name)):
        frame_step(m, 'down')
    return 'down:%d' % listing.index(name)


def frame_run(m, name):
    """Run the file @p name under the cursor"""
    for step in run_steps(name):
        frame_step(m, step)


# ---- real-time drivers ----

def realtime_step(machine, step, chars=0):
    """One step on a real-time machine: the key, then the step's seconds (with idle: a sleep, then the key-wait loop)"""
    if isinstance(step, str):
        step = STEPS[step]
    if 'key' in step:
        machine.tap(step['key'], step['hold'])
    elif 'combo' in step:
        machine.combo(step['combo'], step['hold'])
    seconds = step.get('seconds', 0) + step.get('seconds_per_char', 0) * chars
    if step.get('idle'):
        return machine.idle(seconds)
    if seconds:
        time.sleep(seconds)
    return True
