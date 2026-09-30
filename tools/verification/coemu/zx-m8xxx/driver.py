#!/usr/bin/env python3
"""Run one program on one ZX-M8XXX machine in headless Chrome, and save its memory dump.

usage: driver.py --m8xxx DIR --chrome BIN --machine ID --media FILE --type tape|trd
                 --done N --start N --end N --max-frames N --out PREFIX --profile DIR [--rom NAME=FILE]...

It serves the ZX-M8XXX checkout (read only) over HTTP on 127.0.0.1, with this folder's harness.html, the
program file and the ROM files given with --rom next to it, and opens the harness in headless Chrome. The
harness drives the emulator through window.zxDebug and posts back log lines, progress and the result. Writes PREFIX.bin (START..PROBEEND-1) and
PREFIX.screen.txt (the screen at #4000 as text, matched against the 48K ROM font).

Exit status: 0 DONE was set, 1 DONE not set after --max-frames, 2 anything else (see the log on stderr).
The run is bounded by emulated frames. The only wall-clock limit is a stall guard: no word from the page for
--stall seconds (default 180) means the browser is stuck, and the run ends as an error.
"""
import argparse
import http.server
import json
import mimetypes
import os
import queue
import shutil
import signal
import socketserver
import subprocess
import sys
import threading
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))


def log(*a):
    print(*a, file=sys.stderr, flush=True)


def make_server(root, media, roms, events):
    class Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *a, **kw):
            super().__init__(*a, directory=root, **kw)

        def end_headers(self):
            # No caching: Chromium caches ES modules hard (M8XXX.md, rule 6)
            self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
            super().end_headers()

        def guess_type(self, path):
            if path.endswith(('.js', '.mjs')):
                return 'text/javascript'
            return mimetypes.guess_type(path)[0] or 'application/octet-stream'

        def translate_path(self, path):
            p = urllib.parse.urlparse(path).path
            if p == '/__coemu/harness.html':
                return os.path.join(HERE, 'harness.html')
            if p == '/__coemu/media/' + os.path.basename(media):
                return media
            if p.startswith('/__coemu/'):
                return os.path.join(HERE, 'nonexistent')
            if p.startswith('/roms/'):
                # Only the ROMs given with --rom; the checkout's own roms/ folder is never used
                return roms.get(p[6:], os.path.join(HERE, 'nonexistent'))
            return super().translate_path(path)

        def do_POST(self):
            route = urllib.parse.urlparse(self.path).path
            body = self.rfile.read(int(self.headers.get('Content-Length') or 0))
            self.send_response(200)
            self.send_header('Content-Length', '0')
            self.end_headers()
            try:
                data = json.loads(body or b'{}')
            except ValueError:
                data = {}
            events.put((route.rsplit('/', 1)[-1], data))

        def log_message(self, *a):
            pass

    socketserver.TCPServer.allow_reuse_address = True
    httpd = socketserver.ThreadingTCPServer(('127.0.0.1', 0), Handler)
    httpd.daemon_threads = True
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd


def screen_text(scr, rom):
    font = {}
    for c in range(96):
        g = rom[0x3D00 + c * 8:0x3D00 + c * 8 + 8]
        ch = '©' if c == 95 else chr(32 + c)
        font.setdefault(g, ch)
        font.setdefault(bytes(~b & 0xFF for b in g), ch)
    lines = []
    for row in range(24):
        line = ''
        for col in range(32):
            cell = bytes(scr[((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2) | col]
                         for y in range(row * 8, row * 8 + 8))
            line += font.get(cell, '?')
        lines.append(line.rstrip())
    return '\n'.join(lines) + '\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--m8xxx', required=True)
    ap.add_argument('--chrome', required=True)
    ap.add_argument('--machine', required=True)
    ap.add_argument('--media', required=True)
    ap.add_argument('--type', choices=['tape', 'trd'], required=True)
    ap.add_argument('--done', type=int, required=True)
    ap.add_argument('--start', type=int, required=True)
    ap.add_argument('--end', type=int, required=True)
    ap.add_argument('--max-frames', type=int, required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--profile', required=True)
    ap.add_argument('--rom', action='append', default=[], metavar='NAME=FILE',
                    help='serve FILE as roms/NAME (ZX-M8XXX fetches its ROMs from roms/ at start; any other is 404)')
    ap.add_argument('--stall', type=int, default=180)
    a = ap.parse_args()

    events = queue.Queue()
    media = os.path.abspath(a.media)
    roms = {}
    for r in a.rom:
        name, _, path = r.partition('=')
        roms[name] = os.path.abspath(path)
    httpd = make_server(os.path.abspath(a.m8xxx), media, roms, events)
    port = httpd.server_address[1]
    query = urllib.parse.urlencode({
        'machine': a.machine, 'media': '/__coemu/media/' + os.path.basename(media), 'type': a.type,
        'done': a.done, 'start': a.start, 'end': a.end, 'max': a.max_frames})
    url = f'http://127.0.0.1:{port}/__coemu/harness.html?{query}'

    # A fresh profile per run (M8XXX.md, rule 6); always headless, no sound
    shutil.rmtree(a.profile, ignore_errors=True)
    os.makedirs(a.profile, exist_ok=True)
    cmd = [a.chrome, '--headless=new', '--disable-gpu', '--no-first-run', '--no-default-browser-check',
           '--disable-extensions', '--mute-audio', '--disable-background-timer-throttling',
           '--disable-renderer-backgrounding', '--disable-backgrounding-occluded-windows',
           '--user-data-dir=' + os.path.abspath(a.profile), url]
    log('driver: serving on port', port, '- chrome', a.chrome)
    chrome = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL, start_new_session=True)
    result = None
    try:
        while result is None:
            try:
                kind, data = events.get(timeout=a.stall)
            except queue.Empty:
                log(f'driver: error: no word from the page for {a.stall} s (the browser is stuck)')
                return 2
            if kind == 'log':
                log('m8xxx:', data.get('line', ''))
            elif kind == 'progress':
                log('m8xxx: frame', data.get('frames'))
            elif kind == 'result':
                result = data
            if chrome.poll() is not None and result is None:
                log('driver: error: Chrome exited with status', chrome.returncode)
                return 2
    finally:
        try:
            os.killpg(chrome.pid, signal.SIGTERM)
            chrome.wait(timeout=10)
        except (ProcessLookupError, subprocess.TimeoutExpired):
            try:
                os.killpg(chrome.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        httpd.shutdown()
        shutil.rmtree(a.profile, ignore_errors=True)

    if 'error' in result:
        log('driver: error:', result['error'].splitlines()[0])
        log(result['error'])
        return 2
    with open(a.out + '.bin', 'wb') as f:
        f.write(bytes(result['dump']))
    try:
        with open(roms['48.rom'], 'rb') as f:
            rom = f.read()
        with open(a.out + '.screen.txt', 'w', encoding='utf-8') as f:
            f.write(screen_text(bytes(result['screen']), rom))
    except OSError as e:
        log('driver: no screen text:', e)
    except KeyError:
        log('driver: no screen text: no 48.rom given')
    log(f"driver: {result['machineType']} (ZX-M8XXX {result['version']}): DONE "
        f"{'set' if result['done'] else 'not set'} after {result['frames']} frames")
    return 0 if result['done'] else 1


if __name__ == '__main__':
    sys.exit(main())
