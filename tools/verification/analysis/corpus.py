#!/usr/bin/env python3
"""The analysis corpus: many games from their own tapes and disks, no reference disassembly, measures that need no human.

  corpus.py select --zxdb <ZXDB mirror> --zxdb-sqlite <zxdb.sqlite> --vtrd <vtrd mirror> --data <root of both>
                   --out <manifest.tsv>
      Picks games (ZXDB genres "Adventure" .. "Game: Simulation") of two kinds: `tape` (a tap / tzx of the ZXDB
      mirror, run on 48K or 128K) and `disk` (a trd / scl of the vtrd.in mirror's western games, run on a Pentagon).
      A game with an AY rip in ZXDB gets it (the music reference). The order is a hash of the row's key, not a
      choice: `dev` games are the ones tuned on, `holdout` games are only run to confirm.
  corpus.py run --manifest ... --data ... --key <key> --work scratch/corpus [--frames 15000] [--port-base 9720]
      Loads the game in unreal-qt offscreen (tape: LOAD "" typed in emulated frames, then playback; disk: TR-DOS
      boot), waits for the load, finds the keys to play (Explorer: keys tried from saved states until the game
      answers the stick), then captures with the analysis: those keys again, then seeded random keyboard and
      Kempston play (with the attract mode and start keys first when no keys reached play). Builds L1 / L2, exports the geometry, the gallery (one PNG per
      font, sprite, tile), the music claims, the AY register writes as a PSG file (music.psg; music-chip2.psg for
      the second TurboSound chip) and a screen every 250 frames.
  corpus.py run-all --manifest ... --data ... --set dev --work scratch/corpus [--shard i/n --port-base ...]
      `run` for every game of the set (skips games with a result unless --again); --shard runs every n-th game
      from the i-th, so n runs on other port bases share a set.
  corpus.py score --work scratch/corpus
      Per game: the share of the screens' character-like cells (one picture in three or more places) that a claimed
      font, a claimed tile, the ROM font nobody claimed, or nothing explains; fonts, sprites, music claims, PSG.

Files of the games stay outside the repository: the manifest holds paths into the mirrors only.
"""

import argparse
import glob
import hashlib
import json
import os
import random
import re
import shutil
import sqlite3
import subprocess
import sys
import time
import urllib.request
import zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'verification', 'mcp'))
import mcplib  # noqa: E402

DEFAULT_BINARY = os.path.join(ROOT, 'cmake-build-agent-release', 'bin', 'unreal-qt.app', 'Contents', 'MacOS',
                              'unreal-qt')
COLUMNS = ['key', 'set', 'kind', 'entry', 'title', 'model', 'media', 'ay']
AY_TYPE = 'Ripped in-game/theme music in AY format'
GAME_GENRES = range(1, 34)  # ZXDB genretypes "Adventure Game: Dungeon Crawl" .. "Game: Simulation"
TAPE_MACHINES = {'ZX-Spectrum 16K': '48K', 'ZX-Spectrum 16K/48K': '48K', 'ZX-Spectrum 48K': '48K',
                 'ZX-Spectrum 48K/128K': '128K', 'ZX-Spectrum 128K': '128K'}
MEDIA_SUFFIXES = {'tape': ('.tzx', '.tap'), 'disk': ('.trd', '.scl')}
PAGES_BEFORE = 2  # frames before each saved screen the RAM pages are saved too
SCREEN_EVERY = 250

# Generic input: what most games read. Keys of the start / menu screens, then play keys (QAOP, Sinclair 6789-0,
# cursor 5678-0, space, M, Z/X) and the Kempston stick
START_KEYS = ['SPACE', 'ENTER', '0', '1', 'S', '5', 'KF', '2', '4', 'P']
PLAY_KEYS = ['Q', 'A', 'O', 'P', 'SPACE', 'M', 'Z', 'X', '6', '7', '8', '9', '0', '5', 'ENTER']
KEMPSTON = {'KF': 'fire', 'KL': 'left', 'KR': 'right', 'KU': 'up', 'KD': 'down'}
KEY_NAMES = {'ENTER': 'enter', 'SPACE': 'space'}
TAPE_BOOT_FRAMES = 200  # the ROM's start (the RAM test, the copyright or the 128K menu) before LOAD "" is typed


def read_manifest(path):
    games = []
    with open(path) as f:
        for line in f:
            if line.startswith('#') or not line.strip():
                continue
            games.append(dict(zip(COLUMNS, line.rstrip('\n').split('\t'))))
    return games


def zip_members(path, suffixes):
    try:
        with zipfile.ZipFile(path) as z:
            return sorted(n for n in z.namelist() if n.lower().endswith(suffixes) and not n.endswith('/'))
    except (zipfile.BadZipFile, OSError, NotImplementedError):
        return []


def has_media(path, suffixes):
    return bool(zip_members(path, suffixes)) if path.lower().endswith('.zip') else path.lower().endswith(suffixes)


def games_of_zxdb(sqlite_path):
    db = sqlite3.connect(sqlite_path)
    rows = db.execute('SELECT id, title FROM entries WHERE genretype_id BETWEEN ? AND ?',
                      (GAME_GENRES.start, GAME_GENRES.stop - 1))
    return {str(entry): title for entry, title in rows}


def zxdb_files(zxdb):
    """entry id -> {filetype: [paths]} of the files the mirror holds, and entry id -> machine type"""
    files, machines = {}, {}
    with open(os.path.join(zxdb, 'list.tsv')) as f:
        for line in f:
            if line.startswith('#'):
                continue
            p = line.rstrip('\n').split('\t')
            if len(p) < 16 or not p[9] or p[5] not in ('ok', 'skip:already-have'):
                continue
            machines.setdefault(p[9], p[12])
            files.setdefault(p[9], {}).setdefault(p[11], []).append(p[2])
    return files, machines


def vtrd_releases(vtrd):
    """vtrd file path (files/gamez/...) -> ZXDB entry id, from the release pages' zxinfo.dk screen links"""
    linked = {}
    for page in glob.glob(os.path.join(vtrd, 'pages', 'release', '*.html')):
        with open(page, encoding='utf-8', errors='replace') as f:
            html = f.read()
        entry = re.search(r'zxscreens/0*(\d+)/', html)
        for link in set(re.findall(r'href="/(gamez/[^"]+\.zip)"', html, re.IGNORECASE)):
            if entry:
                linked['files/' + link] = entry.group(1)
    return linked


def hashed(key):
    return hashlib.sha1(key.encode()).hexdigest()


def select(args):
    games = games_of_zxdb(args.zxdb_sqlite)
    files, machines = zxdb_files(args.zxdb)
    candidates = {'tape': [], 'disk': []}
    for entry, title in games.items():
        model = TAPE_MACHINES.get(machines.get(entry, ''))
        tapes = [t for t in files.get(entry, {}).get('Tape image', [])
                 if has_media(os.path.join(args.zxdb, t), MEDIA_SUFFIXES['tape'])] if model else []
        if tapes:
            candidates['tape'].append({'key': 't' + entry, 'kind': 'tape', 'entry': entry, 'title': title, 'model': model,
                                       'media': os.path.join(args.zxdb, tapes[0])})
    linked = vtrd_releases(args.vtrd)
    for path in sorted(glob.glob(os.path.join(args.vtrd, 'files', 'gamez', '*', '*.[zZ][iI][pP]'))):
        if not zip_members(path, MEDIA_SUFFIXES['disk']):
            continue
        relative = os.path.relpath(path, args.vtrd)
        entry = linked.get(relative, '')
        if entry and entry not in games:
            continue  # a ZXDB entry that is not a game (a utility, a demo)
        name = os.path.splitext(os.path.basename(path))[0]
        candidates['disk'].append({'key': 'd' + name, 'kind': 'disk', 'entry': entry,
                                   'title': games.get(entry, name), 'model': 'PENTAGON', 'media': path})
    rows = []
    for kind, (dev, holdout) in (('tape', (args.dev_tapes, args.holdout_tapes)),
                                 ('disk', (args.dev_disks, args.holdout_disks))):
        order = sorted(candidates[kind], key=lambda g: hashed(g['key']))
        for i, g in enumerate(order[:dev + holdout]):
            ay = files.get(g['entry'], {}).get(AY_TYPE, []) if g['entry'] else []
            g['ay'] = os.path.join(args.zxdb, ay[0]) if ay else ''
            g['set'] = 'dev' if i < dev else 'holdout'
            rows.append(g)
    rows.sort(key=lambda g: (g['set'], g['kind'], g['key']))
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, 'w') as f:
        f.write('# Analysis corpus (corpus.py select): ' + '\t'.join(COLUMNS) + '\n')
        f.write(f"# Candidates: {len(candidates['tape'])} tapes, {len(candidates['disk'])} disks\n")
        f.write('# media / ay paths are relative to --data\n')
        for g in rows:
            for c in ('media', 'ay'):
                g[c] = os.path.relpath(g[c], args.data) if g[c] else ''
            f.write('\t'.join(str(g[c]).replace('\t', ' ') for c in COLUMNS) + '\n')
    print(f"{len(rows)} games of {len(candidates['tape'])} tapes and {len(candidates['disk'])} disks: " +
          ', '.join(f"{s} {sum(1 for g in rows if g['set'] == s)}" for s in ('dev', 'holdout')) +
          f", with an AY rip {sum(1 for g in rows if g['ay'])}")


def unpack(src, folder, suffixes):
    """The file itself, or the first member with one of @p suffixes of a ZIP, copied into @p folder"""
    os.makedirs(folder, exist_ok=True)
    if src.lower().endswith('.zip'):
        names = zip_members(src, suffixes)
        if not names:
            return None
        target = os.path.join(folder, 'game' + os.path.splitext(names[0])[1].lower())
        with zipfile.ZipFile(src) as z, z.open(names[0]) as i, open(target, 'wb') as o:
            shutil.copyfileobj(i, o)
        return target
    target = os.path.join(folder, 'game' + os.path.splitext(src)[1].lower())
    shutil.copyfile(src, target)
    return target


class Input:
    """Holds keys and Kempston directions of one segment, releases them before the next"""

    def __init__(self, s):
        self.s = s
        self.held = []

    def set(self, keys):
        for key in self.held:
            if key in KEMPSTON:
                self.s.call('joystick_input', {'action': 'release', 'buttons': KEMPSTON[key]}, ok=False)
            else:
                self.s.call('type_input', {'action': 'release', 'key': KEY_NAMES.get(key, key.lower())}, ok=False)
        for key in keys:
            if key in KEMPSTON:
                self.s.call('joystick_input', {'action': 'press', 'buttons': KEMPSTON[key]}, ok=False)
            else:
                self.s.call('type_input', {'action': 'press', 'key': KEY_NAMES.get(key, key.lower())}, ok=False)
        self.held = list(keys)


def generic_input(seed, frames, start=True):
    """[(frames, keys)]: the attract mode, start keys (unless the explorer reached play: @p start false), then random
    play with a start key now and then (a game over goes back to the menu)"""
    rng = random.Random(seed)
    plan = []
    if start:
        plan = [(1500, [])]
        for key in START_KEYS:
            plan += [(8, [key]), (120, [])]
    total = sum(n for n, _ in plan)
    while total < frames:
        if rng.random() < 0.04:
            step = (8, [rng.choice(START_KEYS)])
        else:
            pool = PLAY_KEYS + list(KEMPSTON)
            step = (rng.randint(6, 40), rng.sample(pool, rng.choice((1, 1, 2))) if rng.random() < 0.85 else [])
        plan.append(step)
        total += step[0]
    return plan


# The explorer: from the loaded game to play by trying keys from saved states (SZX save + load is lossless)
EXPLORE_KEYS = ['WAIT', 'LONGWAIT', 'ENTER', 'SPACE', '1', '2', '3', '4', '5', '0', 'Y', 'N', 'S', 'P', 'G', 'K', 'J', 'KF', 'M', 'Q',
                'A', 'O', 'Z', 'X', 'C', 'I', 'B', 'H', 'L', 'R', 'E', 'T', 'D', 'F', 'U', 'W', 'V', '6', '7', '8', '9']
CONTROL_PAIRS = [(['O', 'KL'], ['P', 'KR']), (['Q', 'KU'], ['A', 'KD']), (['6'], ['7']), (['9'], ['8'])]
EXPLORE_IDLE = 100   # frames before the first key (the attract mode settles)
EXPLORE_AFTER = 100  # frames after a key before the screen is compared
EXPLORE_HOLD = 6
EXPLORE_DEPTH = 10  # keys in a row at most
EXPLORE_BEAM = 6    # states kept at each depth (at most two reached by the same key)
CONTROL_CELLS = (3, 120)  # a moved player: a few cells, not a page
CONTROL_FRAMES = 40


def screen_signature(m):
    """A coarse picture of the screen: per 8 x 8 cell its attribute and how many pixels are set (in fours)"""
    scr = m.read(0x4000, 6912)
    cells = bytearray(1536)
    for cell in range(768):
        row, col = cell // 32, cell % 32
        base = ((row & 0x18) << 8) | ((row & 7) << 5) | col
        ink = sum(bin(scr[base + (line << 8)]).count('1') for line in range(8))
        cells[2 * cell] = scr[6144 + cell]
        cells[2 * cell + 1] = ink // 4
    return bytes(cells)


def cells_differ(a, b):
    return sum(1 for k in range(768) if a[2 * k] != b[2 * k] or a[2 * k + 1] != b[2 * k + 1])


class Explorer:
    """Finds the keys from the loaded game to play. A state is under control when holding opposite directions
    from it leaves two different screens a few cells apart (a moved player, not a new menu page). Keys are searched
    breadth first with a beam (run()); the steps replay exactly from the start state (the run is deterministic)."""

    def __init__(self, m, player, folder):
        self.m, self.player, self.folder = m, player, folder
        self.start = os.path.join(folder, 'explore-start.szx')

    def save(self, path):
        self.m.post('/snapshot/save', {'path': path, 'force': True})

    def restore(self, path):
        self.m.post('/snapshot/load', {'path': path})

    def tap(self, key):
        """A key held EXPLORE_HOLD frames, then EXPLORE_AFTER frames; WAIT the same frames with no key (time is a
        step too: a page that prints slowly reads no keys), LONGWAIT 500 frames"""
        if key in ('WAIT', 'LONGWAIT'):
            run_frames(self.m, EXPLORE_HOLD + EXPLORE_AFTER if key == 'WAIT' else 500, None, 0)
            return
        self.player.set([key])
        run_frames(self.m, EXPLORE_HOLD, None, 0)
        self.player.set([])
        run_frames(self.m, EXPLORE_AFTER, None, 0)

    def controlled(self, state):
        for one, other in CONTROL_PAIRS:
            shots = []
            for keys in (one, other):
                self.restore(state)
                self.player.set(keys)
                run_frames(self.m, CONTROL_FRAMES, None, 0)
                self.player.set([])
                shots.append(screen_signature(self.m))
            d = cells_differ(*shots)
            if CONTROL_CELLS[0] <= d <= CONTROL_CELLS[1]:
                return one + other, d
        return None, 0

    def run(self):
        """Beam search: from each of the EXPLORE_BEAM states kept, every key; each new screen (once) is checked
        for control, the ones most unlike their state become the next states"""
        self.save(self.start)
        run_frames(self.m, EXPLORE_IDLE, None, 0)
        first = os.path.join(self.folder, 'explore-0-0.szx')
        self.save(first)
        self.restore(first)
        here = screen_signature(self.m)
        seen = {here}
        keys, d = self.controlled(first)
        if keys:
            return [], [{'depth': 0, 'control': keys, 'cells': d}], True
        frontier = [(first, [], here)]
        trace = []
        for depth in range(1, EXPLORE_DEPTH + 1):
            candidates = []
            for state, steps, shot0 in frontier:
                for key in EXPLORE_KEYS:
                    self.restore(state)
                    self.tap(key)
                    shot = screen_signature(self.m)
                    if shot in seen:
                        continue
                    seen.add(shot)
                    path = os.path.join(self.folder, f'explore-{depth}-{len(candidates)}.szx')
                    self.save(path)
                    candidates.append((cells_differ(shot0, shot), path, steps + [key], shot))
            if not candidates:
                trace.append({'depth': depth, 'stuck': True})
                break
            candidates.sort(key=lambda c: -c[0])
            for d0, path, steps, _ in candidates:
                keys, d = self.controlled(path)
                if keys:
                    trace.append({'depth': depth, 'keys': steps, 'control': keys, 'cells': d,
                                  'candidates': len(candidates)})
                    return steps, trace, True
            frontier, by_key = [], {}
            for _, path, steps, shot in candidates:
                if len(frontier) < EXPLORE_BEAM and by_key.get(steps[-1], 0) < 2:
                    by_key[steps[-1]] = by_key.get(steps[-1], 0) + 1
                    frontier.append((path, steps, shot))
            trace.append({'depth': depth, 'candidates': [(c[2][-1], c[0]) for c in candidates],
                          'kept': [st for _, st, _ in frontier]})
        return frontier[0][1], trace, False

    def replay(self):
        """Back to the start state: play() then runs the same frames and keys as the exploration"""
        self.restore(self.start)

    def play(self, steps):
        run_frames(self.m, EXPLORE_IDLE, None, 0)
        for key in steps:
            self.tap(key)
        return EXPLORE_IDLE + sum(500 if k == 'LONGWAIT' else EXPLORE_HOLD + EXPLORE_AFTER for k in steps)

    def clean(self):
        for f in glob.glob(os.path.join(self.folder, 'explore-*.szx')):
            os.remove(f)


class Machine:
    """The session and the instance's WebAPI (memory reads go there: binary, no MCP text)"""

    def __init__(self, s, port_base):
        self.s = s
        listing = s.call('emulator_manage', {'action': 'list'}, ok=False)
        self.url = f"http://127.0.0.1:{port_base}/api/v1/emulator/{listing.structured['emulators'][0]['id']}"

    def read(self, address, length):
        with urllib.request.urlopen(f'{self.url}/memory/read/{address}?length={length}&format=binary', timeout=30) as r:
            return r.read()

    def read_page(self, kind, page):
        with urllib.request.urlopen(f'{self.url}/memory/page/{kind}/{page}?length=16384&format=binary', timeout=30) as r:
            return r.read()

    def get(self, path):
        with urllib.request.urlopen(self.url + path, timeout=30) as r:
            return json.loads(r.read())

    def post(self, path, body):
        """A change goes through the session (it holds the machine: a plain WebAPI POST is refused)"""
        r = self.s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/emulator/{id}' + path, 'body': body})
        return (r.structured or {}).get('body') or {}

    def rom_editor(self):
        """The ROM waits for a line (the command line or a BASIC INPUT): the CPU in the ROM with the BASIC IY"""
        regs = self.get('/registers')
        return regs['special']['pc'] < 0x4000 and regs['index']['iy'] == 0x5C3A


class AyLog:
    """The AY register writes of the run (the WebAPI's AY log, a ring of 64K entries: drained at every screen save)"""

    def __init__(self, m):
        self.m = m
        self.writes = []  # (frame, chip, register, value)
        self.recording = bool(m.post('/ay/log', {'action': 'start', 'capacity': 65536}).get('recording'))

    def drain(self):
        if not self.recording:
            return
        log = self.m.get('/ay/log?limit=65536')
        self.writes += [(e['frame'], e['chip'], e['reg'], e['value']) for e in log.get('entries', [])
                        if e['type'] == 'write']
        self.m.post('/ay/log', {'action': 'clear'})

    def save_psg(self, path, chip):
        """A PSG file (the register writes of each 50 Hz frame after a $FF; $FE n: 4 n empty frames); False when
        the chip was not written"""
        writes = [w for w in self.writes if w[1] == chip and w[2] < 14]
        if not writes:
            return False
        out = bytearray(b'PSG\x1a' + bytes(12))
        frame, empty = writes[0][0], 0
        out.append(0xFF)
        for f, _, reg, value in writes:
            if f != frame:
                empty = f - frame - 1
                while empty >= 4:
                    n = min(empty // 4, 255)
                    out += bytes([0xFE, n])
                    empty -= 4 * n
                out += b'\xff' * (empty + 1)
                frame = f
            out += bytes([reg, value])
        with open(path, 'wb') as f:
            f.write(out)
        return True


class Basic:
    """A BASIC program needs lines, not play keys: an answer to its INPUT, RUN when it stopped (an error, BREAK, the
    end). Checked at each screen save; two waits in a row (500 frames) count, a ROM call of a running game does not"""

    ANSWERS = ['1', '2', '3', 'Y', 'N']
    FLAGX = 0x5C71  # bit 5: INPUT mode

    def __init__(self, m, seed):
        self.m = m
        self.rng = random.Random(seed + ':basic')
        self.waits = 0
        self.inputs = 0
        self.runs = 0

    def keys(self, *taps):
        for tap in taps:
            if isinstance(tap, list):
                self.m.s.call('type_input', {'action': 'combo', 'keys': tap, 'frames': 2}, ok=False)
            else:
                self.m.s.call('type_input', {'action': 'tap', 'key': tap, 'frames': 2}, ok=False)
            self.m.s.call('control_execution', {'action': 'run_frames', 'frames': 5})

    def check(self, player):
        self.waits = self.waits + 1 if self.m.rom_editor() else 0
        if self.waits < 2:
            return
        self.waits = 0
        player.set([])
        flagx = self.m.read(self.FLAGX, 1)[0]
        # Whatever the play keys typed goes first (DELETE: CAPS SHIFT + 0)
        self.keys(*[['caps_shift', '0']] * 40)
        if flagx & 0x20:
            self.inputs += 1
            self.keys(self.rng.choice(self.ANSWERS).lower(), 'enter')
            return
        self.runs += 1
        if self.m.get('/basic/state').get('state') == 'basic128k':
            self.keys('r', 'u', 'n', 'enter')  # the 128K editor takes letters
        else:
            self.keys('r', 'enter')  # 48K: K mode, R is RUN


def save_pages(m, folder):
    """Every RAM page as it is now (16 K each, ram-N.bin): the bytes the claims name and both screens of a 128K"""
    os.makedirs(folder, exist_ok=True)
    for page in range(8):
        try:
            data = m.read_page('ram', page)
        except OSError:
            break
        if len(data) == 0x4000:
            with open(os.path.join(folder, f'ram-{page}.bin'), 'wb') as f:
                f.write(data)


def run_frames(m, frames, screens, at, on_screen=None):
    """Runs @p frames, saving the screen and the RAM pages (snapshots/NNNNNN, accept.py: graphics a game overwrites
    later, a screen in page 7) whenever the count passes a multiple of SCREEN_EVERY (and calling @p on_screen then);
    the pages also PAGES_BEFORE frames and less before it (snapshots-before/NNNNNN-K: a buffer the game scrolls after
    copying it to the screen shows on the screen as it was a frame or two before); returns the new count"""
    while frames > 0:
        left = SCREEN_EVERY - at % SCREEN_EVERY
        step = min(frames, (left - PAGES_BEFORE if left > PAGES_BEFORE else 1) if screens else left)
        m.s.call('control_execution', {'action': 'run_frames', 'frames': step})
        at += step
        frames -= step
        left = SCREEN_EVERY - at % SCREEN_EVERY
        if screens and left <= PAGES_BEFORE and at % SCREEN_EVERY != 0:
            save_pages(m, os.path.join(os.path.dirname(screens), 'snapshots-before', f'{at + left:06d}-{left}'))
        if at % SCREEN_EVERY == 0 and screens:
            with open(os.path.join(screens, f'{at:06d}.scr'), 'wb') as f:
                f.write(m.read(0x4000, 6912))
            save_pages(m, os.path.join(os.path.dirname(screens), 'snapshots', f'{at:06d}'))
            if on_screen:
                on_screen()
    return at


def key_tap(m, keys, hold=5, gap=10):
    """Holds @p keys for @p hold emulated frames, then leaves them up for @p gap: typing in the machine's time (the
    ROM takes the same key twice only after it was up a while)"""
    for key in keys:
        m.s.call('type_input', {'action': 'press', 'key': key}, ok=False)
    run_frames(m, hold, None, 0)
    for key in keys:
        m.s.call('type_input', {'action': 'release', 'key': key}, ok=False)
    run_frames(m, gap, None, 0)


def load(m, game, media, report, args):
    """Loads the game the way a user does; returns the frames it took. A tape is not autostarted by the tool (that
    types LOAD "" with the machine running in host time: no two runs alike): the ROM boots, LOAD "" (on 128K the
    menu's Tape Loader) is typed in emulated frames, then the tape plays"""
    tape = game['kind'] != 'disk'
    r = m.s.call('load_software', {'path': media, 'autostart': not tape, 'play': False}, ok=False)
    report['load'] = [line for line in (r.text or '').splitlines() if not line.startswith('Note:')]
    if r.is_error:
        raise RuntimeError('load_software: ' + (r.text or ''))
    if not tape:
        return run_frames(m, args.disk_boot_frames, None, 0)
    frames = run_frames(m, TAPE_BOOT_FRAMES, None, 0)
    if game['model'] == '48K':
        for keys in (['j'], ['symbol', 'p'], ['symbol', 'p'], ['enter']):
            key_tap(m, keys)
    else:
        key_tap(m, ['enter'])
    m.post('/tape/play', None)
    state = ''
    while frames < args.load_frames:
        frames = run_frames(m, 500, None, frames)
        tape = m.s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/emulator/{id}/tape'}, ok=False)
        state = ((tape.structured or {}).get('body') or {}).get('state', '')
        if state in ('ended', 'idle'):
            break
    report['tape_state'] = state
    return frames


def run_game(game, args):
    out = os.path.abspath(os.path.join(args.work, game['key']))
    os.makedirs(out, exist_ok=True)
    media = unpack(os.path.join(args.data, game['media']), out, MEDIA_SUFFIXES[game['kind']])
    if game['ay']:
        unpack(os.path.join(args.data, game['ay']), out, ('.ay',))
    report = {k: game[k] for k in ('key', 'set', 'kind', 'entry', 'title', 'model')}
    report['frames'] = 0
    if media is None:
        report['error'] = 'no media in the archive'
        return report
    process = mcplib.launch(args.binary, args.port_base)
    began = time.time()
    try:
        s = mcplib.Session(f'http://127.0.0.1:{args.port_base + 200}/mcp', game['model'])
        s.fresh_machine(game['model'], ram_power_on='zero')  # random RAM would make each run another game
        s.call('emulator_manage', {'action': 'slots_plug', 'slot': 'edge.1', 'card': 'kempston'}, ok=False)
        # The plug restarts the machine: wait for the wired stick before anything else touches the machine
        for _ in range(100):
            if 'slots_plug slot=edge.1' not in s.call('joystick_input', {'action': 'status'}, ok=False).text:
                break
            time.sleep(0.1)
        s.call('control_execution', {'action': 'pause'}, ok=False)
        s.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/audio/mixer/master',
                              'body': {'muted': True}}, ok=False)
        m = Machine(s, args.port_base)
        report['load_frames'] = load(m, game, media, report, args)
        player = Input(s)
        explorer = Explorer(m, player, out)
        steps, trace, reached = explorer.run()
        report['explore'] = {'keys': steps, 'reached_control': reached, 'trace': trace}
        explorer.replay()
        s.call('analysis', {'action': 'start', 'record': True})
        screens = os.path.join(out, 'screens')
        os.makedirs(screens, exist_ok=True)
        ay = AyLog(m)
        report['explore']['frames'] = explorer.play(steps)
        explorer.clean()
        basic = Basic(m, game['key'])

        def on_screen():
            ay.drain()
            basic.check(player)

        frames = 0
        for count, keys in generic_input(game['key'], args.frames, start=not reached):
            player.set(keys)
            frames = run_frames(m, min(count, args.frames - frames), screens, frames, on_screen)
            if frames >= args.frames:
                break
        player.set([])
        ay.drain()
        report['ay_writes'] = len(ay.writes)
        report['psg'] = [name for chip, name in ((0, 'music.psg'), (1, 'music-chip2.psg'))
                         if ay.save_psg(os.path.join(out, name), chip)]
        report['frames'] = frames
        report['basic'] = {'inputs': basic.inputs, 'runs': basic.runs}
        # The glyphs and sprites the gallery names, as the game left them (the score compares screens to them)
        with open(os.path.join(out, 'ram.bin'), 'wb') as f:
            f.write(m.read(0, 0x10000))
        # Every RAM page as the game left it: the claims of a 128K game name pages the end did not map (accept.py)
        save_pages(m, os.path.join(out, 'pages'))
        s.call('analysis', {'action': 'build', 'path': os.path.join(out, 'l1.json'), 'wait': 30000})
        state = s.call('analysis', {'action': 'status'}).text
        while ': running' in state:
            time.sleep(5)
            state = s.call('analysis', {'action': 'status'}).text
        report['status'] = state.splitlines()[-1:] if state else []
        for fmt, name in (('geometry', 'draft.geometry.json'), ('ctl', 'draft.ctl'), ('map', 'draft.map.txt')):
            s.call('analysis', {'action': 'export', 'format': fmt, 'path': os.path.join(out, name),
                                'ranges': '4000-FFFF'}, ok=False)
        listing = s.call('analysis', {'action': 'gallery', 'limit': 400}, ok=False)
        regions = (listing.structured or {}).get('regions', [])
        with open(os.path.join(out, 'gallery.json'), 'w') as f:
            json.dump(regions, f, indent=1)
        gallery = os.path.join(out, 'gallery')
        os.makedirs(gallery, exist_ok=True)
        for region in regions[:args.gallery_limit]:
            start = region.get('start')
            if start is None:
                continue
            s.call('analysis', {'action': 'gallery', 'address': start, 'scale': 2,
                                'path': os.path.join(gallery, f"{region.get('kind', 'x')}-{str(start).lstrip('$#')}.png")},
                   ok=False)
        music = s.call('analysis', {'action': 'query', 'kind': 'music', 'limit': 200}, ok=False)
        with open(os.path.join(out, 'music.json'), 'w') as f:
            json.dump(music.structured or {}, f, indent=1)
        for kind in ('text', 'all'):
            q = s.call('analysis', {'action': 'query', 'kind': kind, 'limit': 100000}, ok=False)
            with open(os.path.join(out, f'regions-{kind}.json'), 'w') as f:
                json.dump(q.structured or {}, f, indent=1)
        if getattr(args, 'explain', None):
            with open(os.path.join(out, 'explain.txt'), 'w') as f:
                for address in args.explain.split(','):
                    for what in ('byte', 'routine'):
                        e = s.call('analysis', {'action': 'explain', 'address': address, 'what': what}, ok=False)
                        f.write(f'=== {address} {what}\n{e.text}\n')
    except Exception as error:  # one game's failure is a row of the report, not the end of the corpus run
        report['error'] = f'{type(error).__name__}: {error}'
    finally:
        process.terminate()
        try:
            process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            process.kill()
        report['seconds'] = round(time.time() - began, 1)
        with open(os.path.join(out, 'run.json'), 'w') as f:
            json.dump(report, f, indent=1)
    return report


def screen_cells(scr):
    """The 768 character cells of a screen as 8-byte strings"""
    cells = []
    for row in range(24):
        for col in range(32):
            base = ((row & 0x18) << 8) | ((row & 7) << 5) | col
            cells.append(bytes(scr[base + (line << 8)] for line in range(8)))
    return cells


def canonical(cell):
    inverted = bytes(b ^ 0xFF for b in cell)
    return min(cell, inverted)


def region_glyphs(ram, region):
    start, end = int(region['start'].lstrip('$'), 16), int(region['end'].lstrip('$'), 16)
    step = region.get('geometry', {}).get('stride', 8) if region['kind'] == 'font' else 8
    step = step if step >= 8 else 8
    return {canonical(ram[a:a + 8]) for a in range(start, end - 6, step)}


def score_game(folder):
    """Measures of one game's run that need no reference: how much of the screens the claimed fonts and tiles explain,
    the sprites, the music"""
    result = {'key': os.path.basename(folder)}
    with open(os.path.join(folder, 'run.json')) as f:
        run = json.load(f)
    result.update({k: run.get(k) for k in ('set', 'kind', 'title', 'error')})
    ram_path, regions_path = os.path.join(folder, 'ram.bin'), os.path.join(folder, 'regions-all.json')
    if run.get('error') or not os.path.exists(ram_path) or not os.path.exists(regions_path):
        return result
    with open(ram_path, 'rb') as f:
        ram = f.read()
    with open(regions_path) as f:
        regions = json.load(f).get('regions', [])
    fonts = [r for r in regions if r['kind'] == 'font']
    tiles = [r for r in regions if r['kind'] == 'tile']
    sprites = [r for r in regions if r['kind'] == 'sprite']
    font_glyphs = set().union(*[region_glyphs(ram, r) for r in fonts]) if fonts else set()
    tile_glyphs = set().union(*[region_glyphs(ram, r) for r in tiles]) if tiles else set()
    rom_glyphs = {canonical(ram[a:a + 8]) for a in range(0x3D00, 0x4000, 8)}
    counts = {}
    shots = sorted(glob.glob(os.path.join(folder, 'screens', '*.scr')))
    for shot in shots:
        with open(shot, 'rb') as f:
            scr = f.read()
        for cell in screen_cells(scr):
            if len(set(cell)) == 1 and cell[0] in (0, 0xFF):
                continue
            key = canonical(cell)
            counts[key] = counts.get(key, 0) + 1
    # Character-like cells: one picture in three or more places over the run (letters, digits, tiles of a map)
    repeated = {c: n for c, n in counts.items() if n >= 3}
    total = sum(repeated.values())
    by = {'font': 0, 'tile': 0, 'rom_unclaimed': 0, 'other': 0}
    for cell, n in repeated.items():
        if cell in font_glyphs:
            by['font'] += n
        elif cell in tile_glyphs:
            by['tile'] += n
        elif cell in rom_glyphs:
            by['rom_unclaimed'] += n
        else:
            by['other'] += n
    result.update({
        'screens': len(shots), 'repeated_cells': total,
        'explained': {k: round(v / total, 3) if total else 0 for k, v in by.items()},
        'fonts': [f"{r['start']}-{r['end']} {r['detector']}" for r in fonts],
        'tiles': len(tiles), 'sprites': len(sprites),
        'sprite_frames': sum(r.get('geometry', {}).get('frames', 1) for r in sprites),
    })
    music_path = os.path.join(folder, 'music.json')
    if os.path.exists(music_path):
        with open(music_path) as f:
            music = json.load(f).get('regions', [])
        result['music'] = len(music)
    result['ay_rip'] = bool(glob.glob(os.path.join(folder, 'game.ay')))
    result['psg'] = bool(run.get('psg'))
    return result


def score(args):
    rows = []
    for run in sorted(glob.glob(os.path.join(args.work, '*', 'run.json'))):
        rows.append(score_game(os.path.dirname(run)))
    with open(os.path.join(args.work, 'score.json'), 'w') as f:
        json.dump(rows, f, indent=1)
    print(f"{'key':14} {'set':7} {'title':24} {'scr':>4} {'cells':>6} {'font':>5} {'tile':>5} {'rom?':>5} "
          f"{'other':>5} {'fnt':>3} {'spr':>4} {'mus':>3} psg ay")
    for r in rows:
        if 'explained' not in r:
            print(f"{r['key']:14} {r.get('set') or '':7} {(r.get('title') or '')[:24]:24} {r.get('error') or 'no result'}")
            continue
        e = r['explained']
        print(f"{r['key']:14} {r['set']:7} {r['title'][:24]:24} {r['screens']:4} {r['repeated_cells']:6} "
              f"{e['font']:5.2f} {e['tile']:5.2f} {e['rom_unclaimed']:5.2f} {e['other']:5.2f} {len(r['fonts']):3} "
              f"{r['sprites']:4} {r.get('music', 0):3} {'yes' if r['psg'] else '':3} {'yes' if r['ay_rip'] else ''}")
    done = [r for r in rows if 'explained' in r]
    if done:
        mean = lambda k: sum(r['explained'][k] for r in done) / len(done)  # noqa: E731
        print(f"{len(done)} games: font {mean('font'):.3f}, tile {mean('tile'):.3f}, ROM font not claimed "
              f"{mean('rom_unclaimed'):.3f}, other {mean('other'):.3f}; with a font "
              f"{sum(1 for r in done if r['fonts'])}, with sprites {sum(1 for r in done if r['sprites'])}, "
              f"with music {sum(1 for r in done if r.get('music'))}, with a PSG {sum(1 for r in done if r.get('psg'))}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('select')
    p.add_argument('--zxdb', required=True)
    p.add_argument('--zxdb-sqlite', required=True)
    p.add_argument('--vtrd', required=True)
    p.add_argument('--out', required=True)
    p.add_argument('--data', required=True, help='the root the manifest paths are relative to')
    p.add_argument('--dev-tapes', type=int, default=40)
    p.add_argument('--holdout-tapes', type=int, default=20)
    p.add_argument('--dev-disks', type=int, default=20)
    p.add_argument('--holdout-disks', type=int, default=10)
    for name in ('run', 'run-all'):
        p = sub.add_parser(name)
        p.add_argument('--manifest', required=True)
        p.add_argument('--work', required=True)
        p.add_argument('--data', required=True, help='the root the manifest paths are relative to')
        p.add_argument('--frames', type=int, default=15000)
        p.add_argument('--load-frames', type=int, default=30000)
        p.add_argument('--disk-boot-frames', type=int, default=1500)
        p.add_argument('--port-base', type=int, default=9720)
        p.add_argument('--binary', default=DEFAULT_BINARY)
        p.add_argument('--gallery-limit', type=int, default=60)
        if name == 'run':
            p.add_argument('--key', required=True)
            p.add_argument('--explain', help='addresses (comma separated) whose byte and routine facts go to '
                                             'explain.txt after the build')
        else:
            p.add_argument('--set', default='dev')
            p.add_argument('--kind', choices=('tape', 'disk'))
            p.add_argument('--again', action='store_true')
            p.add_argument('--shard', default='0/1', help='i/n: every n-th game from the i-th (parallel runs on '
                                                          'other --port-base)')
    p = sub.add_parser('score')
    p.add_argument('--work', required=True)
    args = parser.parse_args()
    if args.command == 'select':
        select(args)
        return
    if args.command == 'score':
        score(args)
        return
    games = read_manifest(args.manifest)
    if args.command == 'run':
        games = [g for g in games if g['key'] == args.key]
        if not games:
            raise SystemExit(f'{args.key} is not in the manifest')
    else:
        games = [g for g in games if g['set'] == args.set and (not args.kind or g['kind'] == args.kind)]
        index, count = (int(v) for v in args.shard.split('/'))
        games = games[index::count]
    for game in games:
        done = os.path.join(args.work, game['key'], 'run.json')
        if args.command == 'run-all' and not args.again and os.path.exists(done):
            continue
        report = run_game(game, args)
        print(f"{game['key']:>14} {game['title'][:30]:30} load {report.get('load_frames', 0):6} "
              f"frames {report['frames']:6} {report.get('seconds', 0):6.0f} s {report.get('error', '')}", flush=True)


if __name__ == '__main__':
    main()
