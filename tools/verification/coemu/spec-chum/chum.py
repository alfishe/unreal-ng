#!/usr/bin/env python3
"""Drive one headless spec_chum (its `spec-chum-agent` loopback HTTP server) through a co-emulation run.

  chum.py run --bin <spec-chum-agent> --romroot <dir with roms/> --model <slug> --load <how>
              --tap <file> [--trd <file>] --done <addr> --start <addr> --end <addr>
              --max-frames <n> [--load-frames <n>] --out <prefix>

<how> is how a user would load the program on that machine:
  keyword     48K: LOAD "" typed in keyword mode, then the tape plays
  menu        128K / +2 / +2A / Pentagon: Enter on the start menu's first item (Tape Loader / Loader), the tape plays
  plus3basic  +3: the menu's +3 BASIC, LOAD "t:", then LOAD "", then the tape plays
  diskboot    Scorpion: the .trd in drive A, then power on (the machine boots into TR-DOS, which runs "boot")

The server only advances when asked (POST /v1/run), so the machine runs as fast as the host allows; the frame
count is exact. The program's code must be in memory (its first bytes as on the tape) within --load-frames
(default 6000), else the run stops there. Writes <prefix>.bin (START..END-1) and <prefix>.scr (the screen).
Exit status: 0 DONE was set, 1 DONE not set after --max-frames, 2 an error ("chum: error: ..." on stderr).
"""
import argparse
import json
import os
import secrets
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

# Keyboard matrix: (half-row, bit). Half-rows 0..7 are the ports #FEFE, #FDFE, ... #7FFE.
CAPS = (0, 0)
SYMBOL = (7, 1)
ENTER = (6, 0)
SPACE = (7, 0)
LETTERS = {
    'z': (0, 1), 'x': (0, 2), 'c': (0, 3), 'v': (0, 4),
    'a': (1, 0), 's': (1, 1), 'd': (1, 2), 'f': (1, 3), 'g': (1, 4),
    'q': (2, 0), 'w': (2, 1), 'e': (2, 2), 'r': (2, 3), 't': (2, 4),
    '1': (3, 0), '2': (3, 1), '3': (3, 2), '4': (3, 3), '5': (3, 4),
    '0': (4, 0), '9': (4, 1), '8': (4, 2), '7': (4, 3), '6': (4, 4),
    'p': (5, 0), 'o': (5, 1), 'i': (5, 2), 'u': (5, 3), 'y': (5, 4),
    'l': (6, 1), 'k': (6, 2), 'j': (6, 3), 'h': (6, 4),
    'm': (7, 2), 'n': (7, 3), 'b': (7, 4), ' ': SPACE,
}


class ChumError(Exception):
    pass


class Chum:
    def __init__(self, port, token):
        self.base = 'http://127.0.0.1:%d' % port
        self.token = token
        self.frames = 0

    def call(self, method, path, body=None, timeout=600):
        data = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(self.base + path, data=data, method=method)
        req.add_header('Authorization', 'Bearer ' + self.token)
        if data is not None:
            req.add_header('Content-Type', 'application/json')
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            raise ChumError('%s %s: HTTP %d %s' % (method, path, e.code, e.read().decode(errors='replace').strip()))

    def run(self, frames):
        self.call('POST', '/v1/run', {'frames': frames})
        self.frames += frames

    def keys(self, keys, pressed):
        self.call('POST', '/v1/keys', {'keys': [{'row': r, 'bit': b, 'pressed': pressed} for r, b in keys]})

    def tap(self, keys, hold=6, gap=6):
        """Press keys together for `hold` frames, release, wait `gap` frames (the ROM's debounce)."""
        self.keys(keys, True)
        self.run(hold)
        self.call('POST', '/v1/keys', {'clear': True})
        self.run(gap)

    def type_text(self, text):
        for ch in text:
            self.tap([LETTERS[ch]])

    def peek(self, addr, length):
        """Memory through the CPU's view, from the hexdump that GET /v1/peek returns (4096 bytes at most)."""
        out = bytearray()
        while length > 0:
            n = min(length, 4096)
            text = self.call('GET', '/v1/peek?addr=0x%04X&len=%d' % (addr, n)).decode()
            for line in text.splitlines():
                hexpart = line[6:6 + 48].split()
                out += bytes(int(h, 16) for h in hexpart)
            addr += n
            length -= n
        return bytes(out)


def free_port():
    s = socket.socket()
    s.bind(('127.0.0.1', 0))
    port = s.getsockname()[1]
    s.close()
    return port


def start_server(args, port, token):
    env = dict(os.environ)
    env['SPEC_CHUM_ROM_ROOT'] = args.romroot
    env.pop('SPEC_CHUM_AGENT', None)
    env.pop('SPEC_CHUM_AGENT_INSECURE', None)
    cmd = [args.bin, '--model', args.model, '--port', str(port), '--token', token]
    print('chum: %s (in %s)' % (' '.join(cmd[:-1] + ['<token>']), args.romroot), flush=True)
    # The ROM root is the working directory too: spec_chum looks for roms/ there first
    proc = subprocess.Popen(cmd, cwd=args.romroot, env=env, stdin=subprocess.DEVNULL)
    chum = Chum(port, token)
    for _ in range(100):
        if proc.poll() is not None:
            raise ChumError('spec-chum-agent exited with status %d at start' % proc.returncode)
        try:
            chum.call('GET', '/v1/health', timeout=2)
            return proc, chum
        except (OSError, ChumError):
            time.sleep(0.1)
    proc.kill()
    raise ChumError('spec-chum-agent did not answer on port %d' % port)


def type_line(chum, keys):
    """Type one line into the 128 / +3 BASIC editor (letters, or (row, bit) lists for symbols), then Enter."""
    for k in keys:
        if isinstance(k, str):
            chum.type_text(k)
        else:
            chum.tap(k)
    chum.tap([ENTER])


def load(chum, args):
    how = args.load
    if how == 'diskboot':
        # The disk goes in, then the machine is switched on: it boots into TR-DOS, which runs the disk's boot
        chum.call('POST', '/v1/trd', {'path': os.path.abspath(args.trd)})
        chum.call('POST', '/v1/reset')
        return
    chum.run(150)  # power on: the copyright message or the start menu
    # The tape goes in paused, as a user puts it in the deck; stock tape settings (played as sound, 1x)
    chum.call('POST', '/v1/tape/open', {'path': os.path.abspath(args.tap)})
    quote = [SYMBOL, LETTERS['p']]
    if how == 'keyword':
        chum.tap([LETTERS['j']])            # LOAD
        chum.tap(quote)
        chum.tap(quote)
        chum.tap([ENTER])
    elif how == 'menu':
        chum.tap([ENTER])                   # the menu's first item: Tape Loader (128K, +2, Pentagon), Loader (+2A)
    elif how == 'plus3basic':
        chum.tap([CAPS, LETTERS['6']])      # cursor down to +3 BASIC
        chum.tap([ENTER])
        chum.run(150)
        type_line(chum, ['load ', quote, 't', [SYMBOL, LETTERS['z']], quote])  # LOAD "t:"
        chum.run(50)
        chum.tap([ENTER])                   # clears the "0 OK" report (the editor eats the first key after it)
        chum.run(50)
        type_line(chum, ['load ', quote, quote])                              # LOAD ""
    else:
        raise ChumError('unknown load method ' + how)
    chum.run(25)
    chum.call('POST', '/v1/tape/play')


def code_bytes(tap, start, n=8):
    """The first n bytes the tape loads at `start` (the data block after a CODE header for that address).

    8 bytes: the program keeps its settings from START + 8 on and changes them as it runs (see ctprobe.asm).
    """
    d = open(tap, 'rb').read()
    i = 0
    want = None
    while i + 2 <= len(d):
        size = d[i] | d[i + 1] << 8
        block = d[i + 2:i + 2 + size]
        if want is not None and block[:1] == b'\xff':
            return block[1 + want:1 + want + n]
        want = None
        if size == 19 and block[0] == 0 and block[1] == 3:
            at = block[14] | block[15] << 8
            if at <= start < at + (block[12] | block[13] << 8):
                want = start - at
        i += 2 + size
    raise ChumError('no CODE block for %d in %s' % (start, tap))


def cmd_run(args):
    token = secrets.token_hex(16)
    port = free_port()
    proc, chum = start_server(args, port, token)
    try:
        load(chum, args)
        # The program must be in memory within --load-frames (a tape at normal speed takes about 1 minute)
        expect = code_bytes(args.tap, args.start)
        while chum.peek(args.start, len(expect)) != expect:
            if chum.frames >= args.load_frames:
                raise ChumError('the program did not load within %d frames'
                                % args.load_frames)
            chum.run(100)
        print('chum: loaded after %d frames' % chum.frames, flush=True)
        done = False
        step = 100
        while chum.frames < args.max_frames:
            chum.run(step)
            if chum.peek(args.done, 1)[0] == 1:
                done = True
                break
            if chum.frames % 3000 < step:
                print('chum: frame %d, DONE not set yet' % chum.frames, flush=True)
        print('chum: %s after %d frames' % ('DONE' if done else 'no DONE', chum.frames), flush=True)
        dump = chum.peek(args.start, args.end - args.start)
        with open(args.out + '.bin', 'wb') as f:
            f.write(dump)
        with open(args.out + '.scr', 'wb') as f:
            f.write(chum.peek(0x4000, 6912))
        return 0 if done else 1
    except ChumError:
        try:
            state = json.loads(chum.call('GET', '/v1/inspect'))
            beta = state.get('beta') or {}
            print('chum: at frame %d: PC #%04X, #7FFD #%02X, TR-DOS ROM paged in: %s' % (
                chum.frames, state.get('pc', 0), state.get('page_7ffd') or 0,
                'yes' if beta.get('paged') else 'no'), flush=True)
            screen = chum.peek(0x4000, 6912)
            with open(args.out + '.scr', 'wb') as f:
                f.write(screen)
        except (ChumError, OSError):
            pass
        raise
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()


def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('run')
    r.add_argument('--bin', required=True)
    r.add_argument('--romroot', required=True)
    r.add_argument('--model', required=True)
    r.add_argument('--load', required=True)
    r.add_argument('--tap', required=True)
    r.add_argument('--trd')
    r.add_argument('--done', type=int, required=True)
    r.add_argument('--start', type=int, required=True)
    r.add_argument('--end', type=int, required=True)
    r.add_argument('--max-frames', type=int, default=60000)
    r.add_argument('--load-frames', type=int, default=6000)
    r.add_argument('--out', required=True)
    args = p.parse_args()
    try:
        return cmd_run(args)
    except (ChumError, OSError) as e:
        print('chum: error: %s' % e, file=sys.stderr, flush=True)
        return 2


if __name__ == '__main__':
    sys.exit(main())
