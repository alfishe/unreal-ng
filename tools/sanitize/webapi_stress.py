"""WebAPI create / run / read / delete stress against an AddressSanitizer unreal-qt (the scenario that crashed an
automation agent: many instances created and deleted while others run and are read).

Workers in parallel, each in a loop: create an instance of a random model, run it free or by run_frames, read the
state endpoints automation uses (video changes / map, screen, memory search, disasm, port trace, debug wait), maybe
switch its model, delete it. Two long-lived neighbours keep running and are read all the time.
Usage: webapi_stress.py <port> <seconds> <workers>
STRESS_SELECT_CLI_PORT=<cli port> (stress_round.sh --busy-gui): before half of the deletes the worker selects its
instance through the CLI, so the GUI binds to it and its windows read it while the delete runs (BUGS.md #11)
"""
import json, os, random, socket, sys, threading, time, urllib.request, urllib.error

PORT, SECONDS, WORKERS = int(sys.argv[1]), float(sys.argv[2]), int(sys.argv[3])
BASE = f'http://localhost:{PORT}/api/v1'
stop = time.time() + SECONDS
stats = {'ok': 0, 'http_err': 0, 'conn_err': 0, 'cycles': 0, 'selected': 0}
SELECT_CLI_PORT = int(os.environ.get('STRESS_SELECT_CLI_PORT', '0'))
lock = threading.Lock()


def call(method, path, body=None, timeout=30):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE + path, data=data, method=method, headers={'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            raw = r.read()
            with lock:
                stats['ok'] += 1
            return json.loads(raw) if raw[:1] in (b'{', b'[') else {}
    except urllib.error.HTTPError:
        with lock:
            stats['http_err'] += 1
        return {}
    except Exception:
        with lock:
            stats['conn_err'] += 1
        return None


models = [m.get('name') or m.get('short_name') for m in (call('GET', '/emulator/models') or {}).get('models', [])
          if m.get('creatable', True)]
models = [m for m in models if m] or ['PENTAGON', '48K', '128K']
print('models', len(models), models[:40], flush=True)

READS = ['/video/changes', '/video/layout', '/video/beam', '/state/screen', '/state/screen/digest', '/state/paging',
         '/registers', '/disasm?count=16', '/debug/snapshot', '/debug/wait?timeout_ms=0', '/memory/info',
         '/state/fdc', '/state/audio/ay', '/capture/ocr', '/profiler/porttrace/status', '/debug/pchist?depth=8',
         '/video/pixel?x=100&y=100']


def select_in_gui(eid):
    """The CLI's select: the GUI follows the selection and adopts the instance"""
    try:
        with socket.create_connection(('localhost', SELECT_CLI_PORT), timeout=5) as s:
            s.settimeout(2)
            s.sendall(f'select {eid}\n'.encode())
            try:
                s.recv(4096)
            except socket.timeout:
                pass
        with lock:
            stats['selected'] += 1
    except OSError:
        with lock:
            stats['conn_err'] += 1


def read_some(eid, n):
    for _ in range(n):
        call('GET', f'/emulator/{eid}' + random.choice(READS))
    call('POST', f'/emulator/{eid}/memory/find', {'pattern_hex': 'C3 ?? ??', 'space': 'ram', 'max': 4})


def worker(index):
    rnd = random.Random(index)
    while time.time() < stop:
        model = rnd.choice(models)
        r = call('POST', '/emulator/start', {'model': model, 'symbolic_id': f'stress-{index}'})
        if not r or not r.get('id'):
            continue
        eid = r['id']
        try:
            for _ in range(rnd.randint(1, 4)):
                action = rnd.random()
                if action < 0.3:
                    call('POST', f'/emulator/{eid}/run_frames', {'frames': rnd.randint(1, 20)})
                elif action < 0.5:
                    call('POST', f'/emulator/{eid}/pause')
                    read_some(eid, 3)
                    call('POST', f'/emulator/{eid}/resume')
                elif action < 0.6:
                    call('PUT', f'/emulator/{eid}/feature/porttrace', {'enabled': True})
                    call('POST', f'/emulator/{eid}/profiler/porttrace/start', {})
                elif action < 0.65:
                    call('POST', f'/emulator/{eid}/model', {'model': rnd.choice(models)})
                    lst = call('GET', '/emulator') or {}
                    ids = [e['id'] for e in lst.get('emulators', []) if e.get('symbolic_id') == f'stress-{index}']
                    if ids:
                        eid = ids[0]
                read_some(eid, rnd.randint(1, 6))
        finally:
            if SELECT_CLI_PORT and rnd.random() < 0.5:
                select_in_gui(eid)
                time.sleep(rnd.uniform(0.0, 0.2))   # the GUI adopts it and its windows start reading
            call('DELETE', f'/emulator/{eid}', timeout=60)
            with lock:
                stats['cycles'] += 1


def neighbours():
    ids = []
    for m in ('PENTAGON', 'TSL'):
        r = call('POST', '/emulator/start', {'model': m, 'symbolic_id': 'stress-neighbour'})
        if r and r.get('id'):
            ids.append(r['id'])
    while time.time() < stop:
        for eid in ids:
            read_some(eid, 4)
            if random.random() < 0.2:
                call('POST', f'/emulator/{eid}/run_frames', {'frames': 5})
                call('POST', f'/emulator/{eid}/resume')
    for eid in ids:
        call('DELETE', f'/emulator/{eid}', timeout=60)


threads = [threading.Thread(target=worker, args=(i,)) for i in range(WORKERS)] + [threading.Thread(target=neighbours)]
for t in threads:
    t.start()
last = time.time()
while any(t.is_alive() for t in threads):
    time.sleep(5)
    if time.time() - last >= 60:
        last = time.time()
        alive = call('GET', '/emulator', timeout=10)
        print(time.strftime('%H:%M:%S'), stats, 'instances', (alive or {}).get('count'), flush=True)
        if alive is None and stats['conn_err'] > 50:
            print('SERVER GONE', flush=True)
            break
for t in threads:
    t.join(timeout=5)
print('final', stats, flush=True)
