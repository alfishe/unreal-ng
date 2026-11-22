#!/usr/bin/env python3
"""The handheld menu's machine picker (family -> machine -> RAM size, the "." / ".." rows) on an installed build,
through the GUI control plane of an instance of its own: offscreen, on its own ports, with its own settings folders,
windowless (no sound, controllers or Stream Deck), so a running Unreal NG and its screen are not touched. The expected
rows come from the build's own catalog (GET /emulator/machines). Prints PASS / FAIL per check; exit 0 when all pass.

On the Deck, from another machine (standard library only, so it runs from stdin):
    ssh <deck> python3 - < tools/deploy/steamdeck/check_machine_picker.py
Locally, against a build:
    python3 tools/deploy/steamdeck/check_machine_picker.py --app <path to unreal-qt or the AppImage>
"""
import argparse, json, os, shutil, signal, subprocess, sys, tempfile, time, urllib.error, urllib.request
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument('--app', default=str(Path.home() / 'Applications/unreal-ng/UnrealNG.AppImage'),
                help='unreal-qt or the AppImage (default: the Deck install)')
ap.add_argument('--port', type=int, default=8790, help='WebAPI port of the own instance; the other servers follow it')
args = ap.parse_args()
APP, PORT = Path(args.app), args.port
B = f'http://127.0.0.1:{PORT}/api/v1'
results = []


def check(name, ok, got=None):
    results.append(bool(ok))
    print(('PASS ' if ok else 'FAIL ') + name + ('' if ok else f'   got: {got}'), flush=True)


def call(method, path, body=None):
    req = urllib.request.Request(B + path, data=json.dumps(body).encode() if body is not None else None, method=method)
    req.add_header('Content-Type', 'application/json')
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            return json.loads(r.read())
    except urllib.error.HTTPError as e:
        return {'http': e.code, 'body': e.read().decode()[:300]}


# The picker's texts, as unreal-qt/src/handheld/handheldmenu.cpp and MachineCatalog::Title write them
def ram_label(kb): return f'{kb // 1024} MB' if kb >= 1024 and kb % 1024 == 0 else f'{kb}K'
def title(machine, kb): return machine['title'] + (f' {kb}K' if len(machine['ram_sizes_kb']) > 1 else '')
def family_row(f):
    m = f['machines']
    return m[0]['title'] if len(m) == 1 and len(m[0]['ram_sizes_kb']) == 1 else f['title'] + '  ›'


if not APP.exists():
    sys.exit(f'no app at {APP} (--app)')
home = Path(tempfile.mkdtemp(prefix='unreal-picker-check-'))
env = dict(os.environ, UNREAL_WEBAPI_PORT=str(PORT), UNREAL_MCP_PORT=str(PORT + 2), UNREAL_CLI_PORT=str(PORT + 5),
           UNREAL_GDB_PORT=str(PORT - 6000), UNREAL_DEZOG_PORT=str(PORT + 4000), UNREAL_ZRCP_PORT=str(PORT + 2000),
           UNREAL_HANDHELD='1', UNREAL_QT_VIEW_SETTINGS='off', UNREAL_RECENT_FILES='off',
           UNREAL_STREAMDECK_SUPERVISOR='off', QT_QPA_PLATFORM='offscreen',
           XDG_CONFIG_HOME=str(home / 'config'), XDG_DATA_HOME=str(home / 'data'), XDG_STATE_HOME=str(home / 'state'),
           XDG_CACHE_HOME=str(home / 'cache'))
env.pop('DISPLAY', None)
env.pop('WAYLAND_DISPLAY', None)
app = subprocess.Popen([str(APP), '-platform', 'offscreen'], stdout=open(home / 'app.log', 'w'),
                       stderr=subprocess.STDOUT, env=env, start_new_session=True)


def own_pids():
    """The GUI plane names a host by its process id: ours, or (an AppImage) the child with our port in its environment"""
    yield app.pid
    proc = Path('/proc')
    if not proc.is_dir():
        return
    for d in proc.iterdir():
        if d.name.isdigit() and int(d.name) != app.pid:
            try:
                if f'UNREAL_WEBAPI_PORT={PORT}\0'.encode() in (d / 'environ').read_bytes():
                    yield int(d.name)
            except OSError:
                pass


try:
    for _ in range(120):
        try:
            urllib.request.urlopen(B + '/emulator', timeout=1)
            break
        except Exception:
            time.sleep(0.5)
    version = APP.parent / 'VERSION'
    print('app', APP, version.read_text().strip() if version.exists() else '', flush=True)
    for e in call('GET', '/emulator').get('emulators', []):
        call('DELETE', f"/emulator/{e['id']}")
    tree = call('GET', '/emulator/machines')['families']

    # What to walk, whatever the catalog holds: families of several machines with a machine of several sizes (the
    # first to walk, a second to start in), a machine of one size in such a family to start (a variant when there is
    # one), and a family of one machine with several sizes (its sizes come at once) when the catalog has one
    multis = [f for f in tree if len(f['machines']) > 1 and any(len(m['ram_sizes_kb']) > 1 for m in f['machines'])]
    fixed = [(f, m) for f in tree if len(f['machines']) > 1 for m in f['machines'] if len(m['ram_sizes_kb']) == 1]
    fixed.sort(key=lambda fm: fm[1]['kind'] != 'variant')
    single = next((f for f in tree if len(f['machines']) == 1 and len(f['machines'][0]['ram_sizes_kb']) > 1), None)
    if not multis or not fixed:
        sys.exit('the catalog has no family of several machines with one of several RAM sizes')
    multi = multis[0]
    sized = next(m for m in multi['machines'] if len(m['ram_sizes_kb']) > 1)
    start_f = single or (multis[1] if len(multis) > 1 else multi)
    start = next(m for m in start_f['machines'] if len(m['ram_sizes_kb']) > 1)
    start_kb = start['ram_sizes_kb'][-1]
    print(f"walks {multi['title']} / {sized['title']}, starts in {start_f['title']} / {start['title']} {start_kb}K"
          + ('' if single else ' (no family of one machine with several sizes in this catalog)'), flush=True)
    call('POST', '/emulator/start', {'model': start['name'], 'ram_size': start_kb})

    target = None
    for pid in own_pids():
        if 'http' not in call('POST', '/gui/handheld.menu', {'on': False, 'target': f'{pid}/'}):
            target = f'{pid}/'
            break
    check('the GUI plane reaches the own instance', target)
    if not target:
        raise SystemExit

    def g(method, **params):
        params['target'] = target
        r = call('POST', f'/gui/{method}', params)
        return r.get('body', r)

    def pick(st): return st.get('picker') or {}
    def rows(st): return pick(st).get('items') or []
    def at(st): return rows(st)[pick(st)['row']]

    def choose(st, text):
        idx = rows(st).index(text)
        while pick(st).get('row') != idx:
            st = g('handheld.nav', dir='down' if idx > pick(st)['row'] else 'up')
        return g('handheld.accept')

    def machine_page():
        st = g('handheld.menu', on=True)
        for _ in range(16):
            if st.get('section') == 'machine':
                break
            st = g('handheld.section', dir='next')
        if st.get('focus') != 'page':
            st = g('handheld.accept')
        while st['items'][st['row']].get('label') != 'Model':
            st = g('handheld.nav', dir='down')
        return st

    def model_value(st): return next(i.get('value') for i in st['items'] if i.get('label') == 'Model')

    def wait_current(name, kb):
        cur = None
        for _ in range(80):
            emus = call('GET', '/emulator').get('emulators', [])
            if emus:
                cur = call('GET', f"/emulator/machines?id={emus[0]['id']}").get('current')
                if cur and cur.get('machine') == name and cur.get('ram_kb') == kb:
                    return cur
            time.sleep(0.25)
        return cur

    fam_rows = [family_row(f) for f in tree]
    sized_rows = [ram_label(kb) for kb in sized['ram_sizes_kb']]

    st = machine_page()
    check(f'Model row: {title(start, start_kb)}', model_value(st) == title(start, start_kb), model_value(st))
    st = g('handheld.accept')
    check('families: one row each, "›" where a level follows', rows(st) == fam_rows, rows(st))
    check('the cursor is on the running family', at(st) == family_row(start_f), at(st))

    st = choose(st, family_row(multi))
    want = ['..  Model'] + [m['title'] + ('  ›' if len(m['ram_sizes_kb']) > 1 else '') for m in multi['machines']]
    check(f'{multi["title"]}: ".." first, then its machines', rows(st) == want, rows(st))
    check('the cursor is on a machine, not on ".."', pick(st)['row'] >= 1, pick(st)['row'])
    sized_row = sized['title'] + '  ›'
    st = choose(st, sized_row)
    check(f'{sized["title"]}: ".", "..", then its sizes', rows(st) == ['.   Model', f'..  {multi["title"]}'] + sized_rows, rows(st))
    check('the cursor is on the default size', at(st) == ram_label(sized['default_ram_kb']), at(st))

    st = choose(st, f'..  {multi["title"]}')
    check('A on ".." goes up a level, back on the machine', pick(st).get('title') == multi['title'] and at(st) == sized_row,
          (pick(st).get('title'), at(st)))
    st = choose(st, sized_row)
    st = choose(st, '.   Model')
    check('A on "." goes to the families, back on the family', pick(st).get('title') == 'Model' and at(st) == family_row(multi),
          (pick(st).get('title'), at(st)))
    st = choose(st, family_row(multi))
    st = choose(st, sized_row)
    st = g('handheld.back')
    check('B on the sizes goes up a level, back on the machine', pick(st).get('title') == multi['title'] and at(st) == sized_row,
          (pick(st).get('title'), at(st)))
    st = g('handheld.back')
    check('B on the machines goes to the families, back on the family', pick(st).get('title') == 'Model' and at(st) == family_row(multi),
          (pick(st).get('title'), at(st)))

    st = choose(st, family_row(start_f))
    if single:
        want = ['..  Model'] + [ram_label(kb) for kb in start['ram_sizes_kb']]
        check(f'{single["title"]} (one machine): "..", then the sizes, no "."', rows(st) == want, rows(st))
    else:
        check('the cursor is on the running machine', at(st) == start['title'] + '  ›', at(st))
        st = choose(st, start['title'] + '  ›')
    check('the cursor is on the running size', at(st) == ram_label(start_kb), at(st))
    st = choose(st, '..  Model' if single else '.   Model')
    check('back to the families from the running machine', pick(st).get('title') == 'Model', pick(st).get('title'))

    f, m = fixed[0]
    st = choose(st, family_row(f))
    st = choose(st, m['title'])
    check(f'starting {m["title"]} closes the menu', not st.get('open') or not st.get('picker'),
          {k: st.get(k) for k in ('open', 'focus')})
    cur = wait_current(m['name'], m['ram_sizes_kb'][0])
    check(f'{m["name"]} runs', cur and cur.get('machine') == m['name'], cur)
    st = machine_page()
    check(f'Model row: {title(m, m["ram_sizes_kb"][0])}', model_value(st) == title(m, m['ram_sizes_kb'][0]), model_value(st))

    kb = next(k for k in sized['ram_sizes_kb'] if k != sized['default_ram_kb'])
    st = g('handheld.accept')
    st = choose(st, family_row(multi))
    st = choose(st, sized_row)
    st = choose(st, ram_label(kb))
    cur = wait_current(sized['name'], kb)
    check(f'{sized["name"]} {kb}K runs', cur and cur.get('ram_kb') == kb, cur)
    st = machine_page()
    check(f'Model row: {title(sized, kb)}', model_value(st) == title(sized, kb), model_value(st))
    g('handheld.menu', on=False)
finally:
    try:
        os.killpg(app.pid, signal.SIGTERM)
        app.wait(15)
    except Exception:
        try:
            os.killpg(app.pid, signal.SIGKILL)
        except Exception:
            pass
    shutil.rmtree(home, ignore_errors=True)
print(f'{sum(results)}/{len(results)} passed')
sys.exit(0 if results and all(results) else 1)
