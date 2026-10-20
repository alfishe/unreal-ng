#!/usr/bin/env python3
"""XAS 9.x on screen against the codec (research-xas.md §9): every file of the XAS corpus loaded into the editor in
unreal-ng, its 20-row windows read from the screen memory, each cell compared with the decoded rows cut at 64 columns.

    xas-screen-check.py <XAS907M.trd> --boot XAS9.07m --version 9.07m [--port P] [--keep scratch/dir]
    xas-screen-check.py <XAS9_10.trd> --boot XAS9.10  --version 9.10

The files go onto a copy of the XAS disk under unique names (XAS loads a file by its name: of two "KERNEL" files the
first one comes), each with its cursor on the first line; a file without its first title byte (P#14VRZZ__KERNEL, a
35-byte header: XAS shows a new text instead) gets that byte back. XAS counts the start sentinel as line 0, an empty row
above the text; moving the cursor down past row 19 scrolls.

No font is given: each character's glyph is the bitmap most of its cells have (4x8 pixels), first learned from the
first window of each file, which places every other window (its real top: the requested one, or the one near it that
matches best), then learned again from every placed cell. A cell whose bitmap is not its character's glyph is a
difference (a key the editor took as a character shows as one too). The cursor's cell and cells that changed between
three reads are skipped. Fonts on which two characters
look alike (W = И, b = Ь on 9.x) are listed; such a pair cannot be told apart.
"""
import argparse, collections, glob, json, os, subprocess, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib'))
from emulator import Emulator   # noqa: E402
import zxdisk                   # noqa: E402

ROWS = 20
CORPUS = 'core/src/3rdparty/unreal-asm/testdata/xas'


def disk_with_corpus(xas_disk, out):
    """The XAS disk with every corpus file as F00.X, F01.X ...; the files in that order"""
    files = sorted(glob.glob(os.path.join(CORPUS, '*.$X')))
    hobetas = []
    for k, f in enumerate(files):
        b = open(f, 'rb').read()
        body = bytearray(b[17:])
        if body[35] != 1 and body[34] == 1:
            body[0:0] = b' '                  # the missing first title byte
        body[29:31] = b'\x23\xC0'             # the cursor on the first line (the start sentinel)
        body[31:34] = b'\0\0\0'               # the editor state: the cursor's column and row
        body[34] &= 0x7F                      # "text changed"
        start = b[9] | b[10] << 8             # AS, or aS for a macro file
        hobetas.append(zxdisk.hobeta(f'F{k:02d}', 'X', start, 0, bytes(body)))
    open(out, 'wb').write(zxdisk.add(open(xas_disk, 'rb').read(), hobetas))
    return files


def screen(e):
    """Rows 0-19 of the screen memory: 64 cells each, a cell as eight hex digits (its 4-pixel rows)"""
    scr = bytes(e.read(0x4000, 6144))
    rows = []
    for r in range(ROWS):
        cells = []
        for c in range(64):
            g = []
            for y in range(8):
                py = r * 8 + y
                a = ((py & 0xC0) << 5) | ((py & 7) << 8) | ((py & 0x38) << 2)
                byte = scr[a + c // 2]
                g.append((byte >> 4) if c % 2 == 0 else (byte & 15))
            cells.append(''.join('%x' % v for v in g))
        rows.append(cells)
    return rows


def decoded(zxasm, f, version):
    lines = subprocess.run([zxasm, 'decode', f, '--version', version], capture_output=True, text=True).stdout.split('\n')
    if lines and lines[-1] == '':
        lines.pop()
    return [' ' * 64] + [line[:64].ljust(64) for line in lines]   # XAS' line 0: the start sentinel


def capture(args, disk, files):
    """{file: [[requested top, cursor line, read, read, read], ...]}"""
    out = {}
    for index, f in enumerate(files):
        n = len(decoded(args.zxasm, f, args.version))
        e = Emulator(port=args.port, model='PENTAGON', ram_size=128)
        try:
            e.insert_disk(os.path.abspath(disk))
            e.run_trdos(args.boot)
            time.sleep(4)
            for _ in range(index):
                e.tap('right')
            e.tap('enter')
            time.sleep(3)
            windows, cursor, top = [], 0, 0
            while True:
                want = min(top, max(0, n - ROWS))
                target = min(n - 1, want + ROWS - 1) if want else 0
                for _ in range(target - cursor):   # CS+6: held 5 frames, or a busy host may type a "6" into the text
                    e.post('/keyboard/tap', {'key': 'down', 'frames': 5})
                    e.idle()
                    time.sleep(0.06)
                cursor = target
                time.sleep(0.4)
                reads = []
                for _ in range(3):
                    reads.append(screen(e))
                    time.sleep(0.35)
                windows.append([want, cursor] + reads)
                if want + ROWS >= n:
                    break
                top += ROWS
        finally:
            e._request('DELETE', e.base)
        out[f] = windows
        print(f'{os.path.basename(f)}: {n - 1} lines, {len(windows)} windows', flush=True)
    return out


def compare(args, data):
    rows = {f: decoded(args.zxasm, f, args.version) for f in data}

    def row(f, k):
        return rows[f][k] if 0 <= k < len(rows[f]) else ' ' * 64

    def placed(tops):
        for f, windows in data.items():
            for (top, cursor, *reads), real in zip(windows, tops[f]):
                cursor += real - top   # a key lost moves the cursor and the window alike
                for r in range(ROWS):
                    k = real + r
                    if k == 0 or k >= len(rows[f]):
                        continue
                    for c in range(64):
                        steady = len({x[r][c] for x in reads}) == 1 and (k, c) != (cursor, 0)
                        yield f, k, c, row(f, k)[c], reads[0][r][c], steady

    def learn(cells):
        votes = collections.defaultdict(collections.Counter)
        for _, _, _, ch, g, steady in cells:
            if steady:
                votes[ch][g] += 1
        return {ch: v.most_common(1)[0][0] for ch, v in votes.items()}

    requested = {f: [w[0] for w in windows] for f, windows in data.items()}
    glyph = learn(cell for cell in placed(requested) if cell[1] < ROWS)
    tops, shifts = {}, collections.Counter()
    for f, windows in data.items():
        tops[f] = []
        for top, _, *reads in windows:
            def score(t):
                return sum(glyph.get(row(f, t + r)[c]) == reads[0][r][c] for r in range(ROWS) for c in range(64))
            best = max(range(top - 3, top + 4), key=score) if top else 0
            shifts[best - top] += 1
            tops[f].append(best)
    glyph = learn(placed(tops))
    owners = collections.defaultdict(list)
    for ch, g in glyph.items():
        owners[g].append(ch)
    diff, seen, skipped, cells = collections.defaultdict(list), set(), 0, 0
    for f, k, c, ch, g, steady in placed(tops):
        seen.add((f, k))
        cells += 1
        if not steady:
            skipped += 1
        elif g != glyph[ch]:
            diff[(f, k)].append((c, ch, owners.get(g, ['?'])))
    total = sum(len(r) - 1 for r in rows.values())
    print('characters that look alike:', [chs for chs in owners.values() if len(chs) > 1])
    print('windows placed off the requested top:', {s: n for s, n in shifts.items() if s})
    print(f'{len(seen)} of {total} lines, {cells} cells ({skipped} skipped: the cursor), {len(glyph)} characters')
    print(f'{len(diff)} lines differ')
    for (f, k), d in sorted(diff.items()):
        print(f'  {os.path.basename(f)} line {k}: {rows[f][k].rstrip()!r} cells {d[:8]}')
    return 0 if not diff else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('xas_disk')
    parser.add_argument('--boot', required=True, help='the XAS program on the disk (XAS9.07m, XAS9.10)')
    parser.add_argument('--version', required=True, help='the codec version (9.07m, 9.10)')
    parser.add_argument('--port', type=int, default=8090, help="the emulator's WebAPI port")
    parser.add_argument('--zxasm', default='cmake-build-agent-release/bin/zxasm')
    parser.add_argument('--keep', default='scratch/xas-screen-check', help='the disk and the screens (screens.json)')
    parser.add_argument('--compare-only', action='store_true', help='compare the screens.json kept before')
    args = parser.parse_args()
    os.makedirs(args.keep, exist_ok=True)
    kept = os.path.join(args.keep, 'screens.json')
    if not args.compare_only:
        disk = os.path.join(args.keep, 'disk.trd')
        json.dump(capture(args, disk, disk_with_corpus(args.xas_disk, disk)), open(kept, 'w'))
    return compare(args, json.load(open(kept)))


if __name__ == '__main__':
    sys.exit(main())
