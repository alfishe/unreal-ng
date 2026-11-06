#!/usr/bin/env python3
"""Close unreal-qt's window while HTTP keep-alive pollers and WebSockets are connected to the WebAPI; report the exit.

Usage: tools/verification/webapi_stop_under_load.py <unreal-qt binary> [rounds] [port]
The window is closed by lldb calling QApplication::closeAllWindows() on the main thread (the app has no quit hook).
"""
import base64, glob, os, signal, socket, subprocess, sys, threading, time

binary = sys.argv[1]
rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 5
port = int(sys.argv[3]) if len(sys.argv) > 3 else 18090
REPORTS = os.path.expanduser('~/Library/Logs/DiagnosticReports/unreal-qt-*.ips')


def poller(stop):
    """A keep-alive connection polling like the Stream Deck daemon does"""
    try:
        s = socket.create_connection(('127.0.0.1', port), timeout=2)
        req = b'GET /api/v1/emulator/models HTTP/1.1\r\nHost: x\r\nConnection: keep-alive\r\n\r\n'
        while not stop.is_set():
            s.sendall(req)
            s.recv(65536)
            time.sleep(0.02)
    except OSError:
        pass


def websocket(stop):
    try:
        s = socket.create_connection(('127.0.0.1', port), timeout=2)
        key = base64.b64encode(os.urandom(16)).decode()
        s.sendall(f'GET /api/v1/ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
                  f'Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n'.encode())
        while not stop.is_set():
            try:
                if not s.recv(65536):
                    return
            except socket.timeout:
                pass
    except OSError:
        pass


results = []
for n in range(rounds):
    before = set(glob.glob(REPORTS))
    env = dict(os.environ, UNREAL_WEBAPI_PORT=str(port), UNREAL_CLI_PORT=str(port + 100), UNREAL_MCP_PORT=str(port + 200),
               UNREAL_GDB_PORT=str(port + 300), UNREAL_DEZOG_PORT=str(port + 400), UNREAL_ZRCP_PORT=str(port + 500),
               UNREAL_STREAMDECK_SUPERVISOR='off', UNREAL_QT_VIEW_SETTINGS='off', UNREAL_RECENT_FILES='off')
    proc = subprocess.Popen([binary, '-platform', 'offscreen'], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(120):
        try:
            socket.create_connection(('127.0.0.1', port), timeout=1).close()
            break
        except OSError:
            if proc.poll() is not None:
                sys.exit(f'exited early rc={proc.returncode}')
            time.sleep(0.5)
    time.sleep(2)
    stop = threading.Event()
    threads = [threading.Thread(target=poller, args=(stop,), daemon=True) for _ in range(16)]
    threads += [threading.Thread(target=websocket, args=(stop,), daemon=True) for _ in range(4)]
    for t in threads:
        t.start()
    time.sleep(1.5)
    subprocess.run(['xcrun', 'lldb', '-b', '-p', str(proc.pid), '-o', 'thread select 1', '-o',
                    'expr -- (void)((void(*)())_ZN12QApplication15closeAllWindowsEv)()', '-o', 'detach'],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
    # lldb's batch detach sometimes leaves the target stopped (state T); that looked like the window not closing
    os.kill(proc.pid, signal.SIGCONT)
    try:
        rc = proc.wait(timeout=20)
    except subprocess.TimeoutExpired:
        rc = 'HANG'
        subprocess.run(['sample', str(proc.pid), '2', '-file', f'scratch/webapi-stop-hang-{n}.txt'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        proc.kill()
    stop.set()
    time.sleep(2)
    new = sorted(set(glob.glob(REPORTS)) - before)
    results.append((rc, [os.path.basename(p) for p in new]))
    print(f'round {n + 1}: rc={rc} new reports={new}', flush=True)
print('crashes:', sum(1 for rc, rep in results if rep or (isinstance(rc, int) and rc < 0)), 'of', rounds)
