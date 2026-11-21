#!/usr/bin/env python3
"""ZX Spectrum Next software regression run: one unreal-qt binary, a fixed set of jobs, pictures at fixed frames.

    regress.py --binary <unreal-qt> --out <DIR> [--jobs 8] [--port-base 19500] [--collection DIR] [--card DIR]
               [--next-tests DIR --core-tests <core-tests>] [--only TEXT ...] [--limit N] [--no-titles]

Jobs (each on a fresh NEXT machine, ram_power_on zero, its own copy of the card, paused, driven only by run_frames):
  os-boot          the real chain from power on (boot ROM, TBBLUE.FW, NextZXOS), the menu, the Browser, a directory opened
  title-NNN        a collection title: .nex / .sna / .snx through POST /snapshot/load (the launcher boots NextZXOS first and
                   loads into it), .bas / .tap through NextZXOS's Browser (keys pressed frame by frame). Pictures at
                   frames 250, 750, 1500 after the start, then SPACE and ENTER are tapped and a 4th picture at 1800 ("keys")
  realboard        ZXSpectrumNextTests through tools/machines/next/realboard/run-all.py (core-tests), when --next-tests is given

Everything is frame-counted (the machine never runs on its own), so the same binary and inputs give the same pictures.
Output: <DIR>/results.json (per job: status, verdict, shots with the SHA-1 of the raw frame, PC, NR #07), <DIR>/shots/*.png,
<DIR>/logs/, <DIR>/realboard/, <DIR>/report.md and contact sheets. Compare two runs with compare.py.
"""
import argparse, base64, csv, hashlib, io, json, os, queue, re, shutil, subprocess, sys, tempfile, threading, time
import urllib.error, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..'))
SHOT_FRAMES = (250, 750, 1500)
KEYS_FRAME = 1800
IDLE_PC = 0x0C8F
WAIT_SCRIPT = 'io.read("*l")\nreturn 0\n'
TITLE_DIR = 'zt'      # the title's folder on the card root (unique first letter among dot, machines, nextzxos, sys, TBBLUE.FW)
SEARCHABLE = re.compile(r'^[A-Za-z0-9 ._-]+')


class Crash(Exception):
    pass


class Api:
    def __init__(self, port):
        self.base = 'http://127.0.0.1:%d/api/v1' % port

    def raw(self, method, path, body=None, timeout=600):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(self.base + path, data=data, method=method, headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(req, timeout=timeout) as reply:
            return reply.read(), dict(reply.headers)

    def call(self, method, path, body=None, timeout=600):
        try:
            text, _ = self.raw(method, path, body, timeout)
        except urllib.error.HTTPError as e:
            text = e.read()
            try:
                out = json.loads(text)
            except ValueError:
                out = {'raw': text.decode(errors='replace')}
            out['http_status'] = e.code
            return out
        return json.loads(text) if text else {}


class Instance:
    """One headless unreal-qt process with its own ports"""

    def __init__(self, binary, port, logdir, index):
        self.binary, self.port, self.logdir, self.index = binary, port, logdir, index
        self.process = None
        self.starts = 0

    def start(self):
        env = dict(os.environ)
        for name, off in (('UNREAL_WEBAPI_PORT', 0), ('UNREAL_CLI_PORT', 1), ('UNREAL_MCP_PORT', 2), ('UNREAL_GDB_PORT', 3),
                          ('UNREAL_DEZOG_PORT', 4), ('UNREAL_ZRCP_PORT', 5)):
            env[name] = str(self.port + off)
        env.update(UNREAL_STREAMDECK_SUPERVISOR='off', UNREAL_QT_VIEW_SETTINGS='off', UNREAL_RECENT_FILES='off')
        script = os.path.join(self.logdir, 'wait.lua')
        with open(script, 'w') as f:
            f.write(WAIT_SCRIPT)
        self.starts += 1
        self.log = open(os.path.join(self.logdir, 'instance-%d-%d.log' % (self.index, self.starts)), 'w')
        self.process = subprocess.Popen([self.binary, '--headless', '--script', script, '--exit-on-script'], env=env,
                                        stdin=subprocess.PIPE, stdout=self.log, stderr=subprocess.STDOUT)
        self.api = Api(self.port)
        deadline = time.time() + 120
        while time.time() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError('unreal-qt exited at start (%s)' % self.process.returncode)
            try:
                listed = self.api.call('GET', '/emulator', timeout=2)
                for e in listed.get('emulators', []):   # a machine the application made by itself: not ours, removed
                    self.api.call('DELETE', '/emulator/' + e['id'])
                return
            except (urllib.error.URLError, ConnectionError, OSError):
                time.sleep(0.3)
        raise RuntimeError('WebAPI on %d did not come up' % self.port)

    def alive(self):
        return self.process is not None and self.process.poll() is None

    def stop(self):
        if self.process is None:
            return
        try:
            self.process.stdin.close()
            self.process.wait(timeout=30)
        except Exception:
            self.process.kill()
            self.process.wait()
        self.log.close()
        self.process = None

    def kill(self):
        if self.process is not None:
            self.process.kill()
            self.process.wait()
            self.log.close()
            self.process = None


class Machine:
    """A NEXT machine on one instance, paused, driven by run_frames"""

    def __init__(self, inst, card):
        self.inst, self.api = inst, inst.api
        reply = self.call('POST', '/emulator/create', {'model': 'NEXT', 'ram_power_on': 'zero'})
        if 'id' not in reply:
            raise RuntimeError('create: %s' % reply)
        self.id = reply['id']
        self.e = '/emulator/' + self.id
        self.frame = 0       # frames run by us since the start marker
        self.call('PUT', self.e + '/audio/mixer/master', {'muted': True})
        r = self.call('POST', self.e + '/media/sd/insert', {'path': card})
        self.media = r
        if not r.get('ok'):
            raise RuntimeError('card insert: %s' % json.dumps(r)[:300])
        self.call('POST', self.e + '/pause')

    def call(self, method, path, body=None, timeout=600):
        try:
            return self.api.call(method, path, body, timeout)
        except (urllib.error.URLError, ConnectionError, OSError) as e:
            if not self.inst.alive():
                raise Crash('unreal-qt died (exit %s) on %s %s' % (self.inst.process.returncode, method, path))
            raise

    def run(self, frames):
        while frames > 0:
            n = min(frames, 5000)
            r = self.call('POST', self.e + '/run_frames', {'frames': n})
            if r.get('status') != 'success' and 'frames_run' not in r:
                raise RuntimeError('run_frames: %s' % r)
            frames -= n
            self.frame += n

    def regs(self):
        return self.call('GET', self.e + '/registers')

    def idle(self):
        r = self.regs()
        return bool(r['interrupt']['halted']) and r['special']['pc'] == IDLE_PC

    def run_until_idle(self, limit, step=5, settle=0):
        """Frames until NextZXOS waits for a key (HALT at #0C8F), at most @p limit; deterministic (fixed steps)"""
        ran = 0
        while ran < limit:
            self.run(step)
            ran += step
            if self.idle():
                if settle:
                    self.run(settle)
                return True
        return False

    def tap(self, key, frames=6, after=10):
        self.call('POST', self.e + '/keyboard/tap', {'key': key, 'frames': frames})
        self.run(frames + after)

    def combo(self, keys, frames=6, after=10):
        self.call('POST', self.e + '/keyboard/combo', {'keys': keys, 'frames': frames})
        self.run(frames + after)

    def type(self, text, delay=5):
        self.call('POST', self.e + '/keyboard/type', {'text': text, 'delay_frames': delay})
        self.run(len(text) * (delay + 8) + 20)

    def framebuffer(self):
        data, headers = self.api.raw('GET', self.e + '/capture/framebuffer?format=rgba')
        h = {k.lower(): v for k, v in headers.items()}
        return int(h['x-width']), int(h['x-height']), data

    def nr(self, reg):
        return self.call('GET', self.e + '/state/next/regs')

    def remove(self):
        try:
            self.call('DELETE', self.e, timeout=60)
        except Exception:
            pass


def image_of(w, h, data):
    from PIL import Image
    return Image.frombytes('RGBA', (w, h), data).convert('RGB')


def nr_value(reply, reg):
    """NR value from /state/next/regs: registers[] of {nr: "0x07", value: "0x33"}"""
    for item in reply.get('registers', []):
        try:
            if int(str(item.get('nr')), 16) == reg:
                return item.get('value')
        except ValueError:
            pass
    return None


def screen_kind(im):
    """flat (one colour), nextzxos (grey paper with text: menu, Browser, editor, error report) or picture"""
    colours = im.getcolors(1 << 20) or []
    if len(colours) <= 1:
        return 'flat'
    total = im.size[0] * im.size[1]
    count, top = max(colours)
    if top == (182, 182, 182) and 0.6 * total < count < 0.995 * total:
        return 'nextzxos'
    return 'picture'


def shoot(m, shotdir, job, label):
    w, h, data = m.framebuffer()
    im = image_of(w, h, data)
    name = '%s-%s.png' % (job, label)
    im.save(os.path.join(shotdir, name))
    regs = m.regs()
    nr07 = nr_value(m.nr(0x07), 0x07)
    clock = m.call('GET', m.e + '/state/next').get('machine', {}).get('cpu_clock_hz')
    halted = bool(regs['interrupt']['halted'])
    pc = regs['special']['pc']
    return {'label': label, 'frame': m.frame, 'png': name, 'sha1': hashlib.sha1(data).hexdigest(), 'pc': pc, 'halted': halted,
            'nr07': nr07, 'mhz': clock / 1e6 if clock else None, 'os_idle': halted and pc == IDLE_PC, 'kind': screen_kind(im)}


def clone_tree(src, dst):
    """APFS clone where possible (cp -c), a plain copy elsewhere"""
    if sys.platform == 'darwin':
        if subprocess.run(['cp', '-cR', src, dst], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
            return
    shutil.copytree(src, dst)


def make_card(base_card, workdir, job, title_dir=None):
    card = os.path.join(workdir, job)
    if os.path.exists(card):
        shutil.rmtree(card)
    clone_tree(base_card, card)
    if title_dir:
        clone_tree(title_dir, os.path.join(card, TITLE_DIR))
    return card


# ---- the Browser (frame-driven version of browser-drive/collection.py) ----

def browser_entries(folder, root=False):
    names = sorted(os.listdir(folder), key=lambda n: n.lower())
    return names if root else ['.', '..'] + names


def search_prefix(entries, name):
    match = SEARCHABLE.match(name)
    if not match:
        return None
    text = match.group(0)
    lower = [e.lower() for e in entries]
    for length in range(1, len(text) + 1):
        if sum(e.startswith(text[:length].lower()) for e in lower) == 1:
            return text[:length]
    return None


def reach(m, folder, name, root=False):
    """Cursor onto @p name in the Browser listing of host folder @p folder: H + the shortest unique prefix, else count down"""
    entries = browser_entries(folder, root)
    prefix = search_prefix(entries, name)
    if prefix:
        m.tap('h', after=20)
        m.type(prefix)
        m.run_until_idle(100)
        m.tap('enter', after=20)   # accept the search: the cursor stays on the match
        m.run_until_idle(100)
        return 'search:' + prefix
    for _ in range(entries.index(name)):
        m.tap('down', 4, after=6)
    return 'down:%d' % entries.index(name)


def boot_to_menu(m, notes):
    """Power on -> NextZXOS welcome (idle) -> SPACE -> menu"""
    if not m.run_until_idle(1500, step=10, settle=50):
        notes.append('no idle after boot')
        return False
    m.tap('space', 8, 20)
    if not m.run_until_idle(300, settle=20):
        notes.append('no idle after SPACE')
    return True


def open_browser(m, notes):
    m.tap('b', 6, 20)
    ok = m.run_until_idle(400, settle=30)
    if not ok:
        notes.append('Browser: no idle')
    return ok


# ---- jobs ----

def job_os_boot(m, shotdir, base_card):
    shots, notes = [], []
    for f in (50, 150, 250, 350):     # boot ROM / firmware / NextZXOS start
        m.run(f - m.frame)
        shots.append(shoot(m, shotdir, 'os-boot', 'f%d' % f))
    if m.run_until_idle(1500, step=10, settle=50):
        shots.append(shoot(m, shotdir, 'os-boot', 'welcome'))
        notes.append('welcome idle at frame %d' % m.frame)
    else:
        notes.append('no idle after boot')
    m.tap('space', 8, 20)
    m.run_until_idle(300, settle=20)
    shots.append(shoot(m, shotdir, 'os-boot', 'menu'))
    open_browser(m, notes)
    shots.append(shoot(m, shotdir, 'os-boot', 'browser'))
    notes.append('reach nextzxos: ' + reach(m, base_card, 'nextzxos', root=True))
    m.tap('enter', 6, 20)
    m.run_until_idle(400, settle=30)
    shots.append(shoot(m, shotdir, 'os-boot', 'dir-nextzxos'))
    m.tap('edit', 6, 20)
    m.run_until_idle(400, settle=30)
    shots.append(shoot(m, shotdir, 'os-boot', 'dir-up'))
    last = shots[-1]
    status = 'ok' if last['os_idle'] and last['kind'] == 'nextzxos' else 'fail'
    return {'status': status, 'verdict': status, 'notes': notes, 'shots': shots}


def job_title(m, shotdir, job, title, collection, card, browser_ext, force_browser=False):
    parts = title['main_file'].split('/')
    host = os.path.join(collection, *parts)
    ext = parts[-1].rsplit('.', 1)[-1].lower()
    notes, shots = [], []
    via = 'browser' if force_browser or ext in browser_ext else 'load'
    if via == 'load':
        path = os.path.join(card, TITLE_DIR, parts[-1])
        r = m.call('POST', m.e + '/snapshot/load', {'path': path, 'switch_model': False})
        if r.get('status') != 'success':
            return {'status': 'no-load', 'verdict': 'no-load', 'via': via, 'notes': [json.dumps({k: v for k, v in r.items() if k != 'automation_session'})[:600]], 'shots': []}
        m.call('POST', m.e + '/pause')
    else:
        if not boot_to_menu(m, notes):
            return {'status': 'no-boot', 'verdict': 'no-boot', 'via': via, 'notes': notes, 'shots': []}
        open_browser(m, notes)
        notes.append('dir: ' + reach(m, card, TITLE_DIR, root=True))
        m.tap('enter', 6, 20)
        m.run_until_idle(400, settle=30)
        notes.append('file: ' + reach(m, os.path.join(card, TITLE_DIR), parts[-1]))
        shots.append(shoot(m, shotdir, job, 'cursor'))
        if ext == 'tap':
            # SYMBOL SHIFT + ENTER: the TAP loader's options; N = the Next's own mode (collection.py)
            m.combo(['sym_shift', 'enter'], 6, 150)
            m.tap('n', 6, 10)
        else:
            m.tap('enter', 6, 10)
            if ext == 'sna':
                # the first run of a snapshot shows the Snapshot Loader's options ("To begin loading, press ENTER")
                m.run(100)
                m.tap('enter', 6, 10)
        if ext == 'bas':
            m.run(300)
            w, h, data = m.framebuffer()
            im = image_of(w, h, data)
            x0, y0 = (w - 512) // 2, (h - 384) // 2   # the paper in the 720 x 576 frame

            def black(box):
                region = im.crop(box)
                return sum(1 for px in getattr(region, 'get_flattened_data', region.getdata)() if px == (0, 0, 0)) / float(region.size[0] * region.size[1])
            # the NextBASIC editor: its black status bar near the bottom (character rows 20-23), no black header bar on row 0 (the Browser has one)
            if max(black((x0, y0 + r * 16, x0 + 512, y0 + r * 16 + 16)) for r in (20, 21, 22, 23)) > 0.5 and black((x0, y0, x0 + 512, y0 + 16)) < 0.4:
                # the Browser only LOADS a .bas without an autostart line: the editor shows it, RUN starts it
                m.type('run')
                m.tap('enter', 6, 10)
                notes.append('RUN typed')
    m.frame = 0   # the start marker: shots count from here
    for f in SHOT_FRAMES:
        m.run(f - m.frame)
        shots.append(shoot(m, shotdir, job, 'f%d' % f))
    m.tap('space', 8, 30)
    m.tap('enter', 8, 30)
    m.run(KEYS_FRAME - m.frame)
    shots.append(shoot(m, shotdir, job, 'keys'))
    main = [s for s in shots if s['label'].startswith('f')]
    last = main[-1]
    basic = ext in ('bas', 'tap')
    if (not basic and last['os_idle']) or (basic and last['kind'] == 'nextzxos'):
        verdict = 'stuck-os'
    elif all(s['kind'] == 'flat' for s in main):
        verdict = 'flat'
    else:
        verdict = 'picture'
    moving = len({s['sha1'] for s in main}) > 1
    return {'status': 'started' if verdict == 'picture' else verdict, 'verdict': verdict, 'via': via, 'moving': moving,
            'notes': notes, 'shots': shots}


def load_titles(collection):
    with open(os.path.join(collection, 'index.tsv'), newline='') as f:
        return list(csv.DictReader(f, delimiter='\t'))


def worker(inst, jobs, results, lock, args, dirs):
    while True:
        try:
            job = jobs.get_nowait()
        except queue.Empty:
            return
        name, title = job
        started = time.time()
        res = None
        try:
            if not inst.alive():
                inst.start()
            if name == 'os-boot':
                card = make_card(args.card, dirs['cards'], name)
            else:
                card = make_card(args.card, dirs['cards'], name,
                                 os.path.dirname(os.path.join(args.collection, *title['main_file'].split('/'))))
            m = Machine(inst, card)
            try:
                if name == 'os-boot':
                    res = job_os_boot(m, dirs['shots'], card)
                else:
                    res = job_title(m, dirs['shots'], name, title, args.collection, card, args.browser_ext.split(','), name.endswith('-br'))
                res['media_ok'] = bool(m.media.get('ok'))
            finally:
                if inst.alive():
                    m.remove()
        except Crash as e:
            res = {'status': 'crash', 'verdict': 'crash', 'notes': [str(e)], 'shots': []}
            inst.kill()
        except (urllib.error.URLError, TimeoutError, OSError) as e:
            res = {'status': 'hang' if inst.alive() else 'crash', 'verdict': 'hang' if inst.alive() else 'crash',
                   'notes': ['%s: %s' % (type(e).__name__, e)], 'shots': []}
            inst.kill()
        except Exception as e:  # a script error: kept per job, the run goes on
            res = {'status': 'error', 'verdict': 'error', 'notes': ['%s: %s' % (type(e).__name__, e)], 'shots': []}
        res['seconds'] = round(time.time() - started, 1)
        if title:
            res.update(title=title['title'], category=title['category'] + '/' + title['genre_or_kind'], main_file=title['main_file'])
        if not args.keep_cards:
            shutil.rmtree(os.path.join(dirs['cards'], name), ignore_errors=True)
        with lock:
            results[name] = res
            print('%-10s %-9s %-8s %5.1fs  %s' % (name, res.get('via', ''), res['verdict'], res['seconds'],
                                                   (title or {}).get('title', '')[:50]), flush=True)


def run_realboard(args, out):
    if not args.next_tests:
        return None
    rb = os.path.join(out, 'realboard')
    env = dict(os.environ, UNREAL_NEXT_TESTS=args.next_tests, CORE_TESTS=args.core_tests)
    started = time.time()
    with open(os.path.join(out, 'logs', 'realboard.log'), 'w') as log:
        code = subprocess.run([sys.executable, os.path.join(REPO, 'tools/machines/next/realboard/run-all.py'), '--out', rb],
                              env=env, stdout=log, stderr=subprocess.STDOUT).returncode
    rows = []
    report = os.path.join(rb, 'report.md')
    if os.path.exists(report):
        for line in open(report).read().splitlines()[2:]:
            cells = [c.strip() for c in line.strip('|').split('|')]
            if len(cells) >= 5:
                rows.append({'area': cells[0], 'test': cells[1], 'program': cells[2], 'verdict': cells[3], 'detail': cells[4]})
    # hashes of our pictures (the "look" programs are compared picture by picture)
    for r in rows:
        png = os.path.join(rb, r['program'].replace('!', '') + '.png')
        if os.path.exists(png):
            from PIL import Image
            r['sha1'] = hashlib.sha1(Image.open(png).convert('RGB').tobytes()).hexdigest()
    result = {'exit': code, 'seconds': round(time.time() - started, 1), 'programs': rows}
    if rows and all(r['verdict'] == 'NO PICTURE' for r in rows):
        result['warning'] = 'no program produced a picture: core-tests could not run them (see logs/realboard.log)'
        print('realboard: ' + result['warning'], file=sys.stderr)
    return result


def write_report(out, meta, results, realboard):
    from PIL import Image, ImageDraw
    titles = {k: v for k, v in results.items() if k.startswith('title-')}
    counts = {}
    for v in titles.values():
        counts[v['verdict']] = counts.get(v['verdict'], 0) + 1
    lines = ['# Next regression run', '', 'binary: `%s`  ' % meta['binary'], 'build: %s  ' % meta.get('build', ''),
             'started: %s, %d s, %d jobs in parallel' % (meta['started'], meta['seconds'], meta['jobs']), '']
    os_res = results.get('os-boot')
    if os_res:
        lines += ['## NextZXOS boot + Browser: **%s**' % os_res['verdict'], '', '; '.join(os_res.get('notes', [])), '']
    groups = {}
    for k, v in titles.items():
        groups.setdefault(k[:9], {})[k] = v
    def path_counts(sel):
        c = {}
        for v in sel:
            c[v['verdict']] = c.get(v['verdict'], 0) + 1
        return c
    primary = [v for k, v in titles.items() if not k.endswith('-br')]
    columns = [('primary path (load for .nex/.snx, Browser for the rest)', path_counts(primary)),
               ('direct load only', path_counts([v for v in primary if v.get('via') == 'load'])),
               ('Browser, all paths', path_counts([v for v in titles.values() if v.get('via') == 'browser']))]
    best = {}
    for g, jobs in groups.items():
        verdicts = [v['verdict'] for v in jobs.values()]
        best_v = 'picture' if 'picture' in verdicts else verdicts[0]
        best[best_v] = best.get(best_v, 0) + 1
    columns.append(('title by any path', best))
    lines += ['## Titles: %d (%d jobs)' % (len(groups), len(titles)), '', '| verdict | ' + ' | '.join(c[0] for c in columns) + ' |',
              '|:--|' + '--:|' * len(columns)]
    for k in sorted({k for c in columns for k in c[1]}):
        lines.append('| %s | ' % k + ' | '.join(str(c[1].get(k, 0)) for c in columns) + ' |')
    lines += ['', 'picture = something other than NextZXOS on screen at frame 1500; stuck-os = still NextZXOS (idle at #0C8F, or '
              'the grey NextZXOS screen for .bas/.tap); flat = one colour at every shot; no-load = the loader refused; crash / hang = '
              'unreal-qt died / stopped answering.', '']
    if realboard:
        rv = {}
        for p in realboard['programs']:
            rv[p['verdict']] = rv.get(p['verdict'], 0) + 1
        lines += ['## realboard (ZXSpectrumNextTests): ' + ', '.join('%s %d' % kv for kv in sorted(rv.items())), '',
                  '| program | verdict | detail |', '|:--|:--|:--|']
        if realboard.get('warning'):
            lines[-2:-2] = ['**Warning**: ' + realboard['warning'], '']
        for p in realboard['programs']:
            if p['verdict'] != 'look':
                lines.append('| %s | %s | %s |' % (p['program'], p['verdict'], p['detail']))
        lines.append('')
    lines += ['## Per title', '', '| job | category | title | via | verdict | moving | NR07 | PC@1500 | notes |',
              '|:--|:--|:--|:--|:--|:--|:--|:--|:--|']
    order = {'crash': 0, 'hang': 1, 'error': 2, 'no-load': 3, 'no-boot': 4, 'flat': 5, 'stuck-os': 6, 'picture': 7}
    for k, v in sorted(titles.items(), key=lambda kv: (order.get(kv[1]['verdict'], 9), kv[0])):
        last = next((s for s in v['shots'] if s['label'] == 'f1500'), {})
        lines.append('| %s | %s | %s | %s | %s | %s | %s | %s | %s |' % (
            k, v.get('category', ''), v.get('title', ''), v.get('via', ''), v['verdict'], 'yes' if v.get('moving') else '',
            last.get('nr07', ''), '%04X' % last['pc'] if 'pc' in last else '', '; '.join(v.get('notes', []))[:120].replace('|', '/')))
    with open(os.path.join(out, 'report.md'), 'w') as f:
        f.write('\n'.join(lines) + '\n')
    # contact sheets: one row per title, the 4 shots
    rows = sorted(titles.items())
    per = 20
    for page in range(0, len(rows), per):
        chunk = rows[page:page + per]
        sheet = Image.new('RGB', (4 * 244 + 8, len(chunk) * 214), (30, 30, 30))
        draw = ImageDraw.Draw(sheet)
        for i, (k, v) in enumerate(chunk):
            for j, label in enumerate(('f250', 'f750', 'f1500', 'keys')):
                s = next((s for s in v['shots'] if s['label'] == label), None)
                if s:
                    im = Image.open(os.path.join(out, 'shots', s['png'])).convert('RGB').resize((240, 192))
                    sheet.paste(im, (4 + j * 244, i * 214 + 18))
            draw.text((6, i * 214 + 3), '%s %s [%s %s]' % (k, v.get('title', '')[:60], v.get('via', ''), v['verdict']), fill=(255, 255, 0))
        sheet.save(os.path.join(out, 'sheet-%02d.png' % (page // per)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--binary', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--jobs', type=int, default=8)
    ap.add_argument('--port-base', type=int, default=19500, help='instance i uses port-base + 10 * i .. + 5')
    ap.add_argument('--collection', help='the Next software collection (index.tsv + the titles); needed unless --no-titles')
    ap.add_argument('--card', default=os.path.join(REPO, 'testdata/machines/zxnext/card'), help='the NextZXOS card copied for every job')
    ap.add_argument('--next-tests', help='ZXSpectrumNextTests clone: run realboard/run-all.py too')
    ap.add_argument('--core-tests', help='core-tests binary for realboard (default: beside --binary)')
    ap.add_argument('--only', nargs='+', action='extend', help='job names or title substrings (repeatable, several per flag)')
    ap.add_argument('--limit', type=int)
    ap.add_argument('--no-titles', action='store_true')
    ap.add_argument('--keep-cards', action='store_true')
    ap.add_argument('--load-only', action='store_true', help='no second Browser run (title-NNN-br) for the loadable titles')
    ap.add_argument('--browser-ext', default='bas,tap,sna',
                    help='main-file extensions run through the Browser; the rest go through /snapshot/load (a .sna is refused '
                         'there on a NEXT: it names no model, the launcher does not boot NextZXOS for it)')
    args = ap.parse_args()
    args.binary = os.path.abspath(args.binary)
    args.card = os.path.abspath(args.card)
    if not args.no_titles:
        if not args.collection:
            ap.error('--collection is required (or --no-titles)')
        args.collection = os.path.abspath(args.collection)
    if args.next_tests and not args.core_tests:
        d = os.path.dirname(args.binary)
        for cand in (os.path.join(d, 'core-tests'), os.path.join(d, '..', '..', '..', 'core-tests')):
            if os.path.exists(cand):
                args.core_tests = os.path.abspath(cand)
                break
    if args.next_tests:
        # core-tests finds its ROMs and test data beside itself: a lone copy of the binary fails every program quietly
        if not args.core_tests or not os.path.isdir(os.path.join(os.path.dirname(args.core_tests), 'rom')):
            ap.error('realboard needs core-tests inside a complete build bin/ directory (rom/, testdata/ beside it); '
                     'copy the whole bin/ directory, not only the app bundle, or pass --core-tests')
    out = os.path.abspath(args.out)
    dirs = {'shots': os.path.join(out, 'shots'), 'logs': os.path.join(out, 'logs'), 'cards': os.path.join(out, 'cards')}
    for d in dirs.values():
        os.makedirs(d, exist_ok=True)
    jobs = queue.Queue()
    selected = []
    if not args.only or 'os-boot' in args.only:
        selected.append(('os-boot', None))
    if not args.no_titles:
        for n, t in enumerate(load_titles(args.collection)):
            name = 'title-%03d' % n
            if args.only and name not in args.only and not any(o.lower() in t['title'].lower() for o in args.only):
                continue
            selected.append((name, t))
            if not args.load_only and not t['main_file'].lower().endswith(tuple('.' + e for e in args.browser_ext.split(','))):
                selected.append((name + '-br', t))   # a loadable title also through the Browser (cwd, NextZXOS's own loader)
    if args.limit:
        selected = selected[:args.limit]
    # the slow ones first (the Browser path), so the run does not end on one long job
    slow = tuple('.' + e for e in args.browser_ext.split(','))
    selected.sort(key=lambda j: 0 if j[1] and (j[0].endswith('-br') or j[1]['main_file'].lower().endswith(slow)) else 1)
    for j in selected:
        jobs.put(j)
    meta = {'binary': args.binary, 'started': time.strftime('%Y-%m-%d %H:%M:%S'), 'jobs': args.jobs, 'card': args.card,
            'collection': args.collection, 'shot_frames': SHOT_FRAMES, 'keys_frame': KEYS_FRAME}
    started = time.time()
    results, lock = {}, threading.Lock()
    rb_result = {}
    rb_thread = threading.Thread(target=lambda: rb_result.update(r=run_realboard(args, out)))
    rb_thread.start()
    instances = [Instance(args.binary, args.port_base + 10 * i, dirs['logs'], i) for i in range(args.jobs)]
    threads = [threading.Thread(target=worker, args=(inst, jobs, results, lock, args, dirs)) for inst in instances]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    try:
        meta['build'] = Api(instances[0].port).call('GET', '/emulator/status', timeout=5).get('server', {})
    except Exception:
        pass
    for inst in instances:
        inst.stop()
    rb_thread.join()
    meta['seconds'] = round(time.time() - started)
    with open(os.path.join(out, 'results.json'), 'w') as f:
        json.dump({'meta': meta, 'results': results, 'realboard': rb_result.get('r')}, f, indent=1, sort_keys=True)
    write_report(out, meta, results, rb_result.get('r'))
    shutil.rmtree(dirs['cards'], ignore_errors=True) if not args.keep_cards else None
    print('done in %d s: %s' % (meta['seconds'], os.path.join(out, 'report.md')))


if __name__ == '__main__':
    main()
