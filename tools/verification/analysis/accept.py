#!/usr/bin/env python3
"""Acceptance of the graphics claims of a corpus run, by an oracle that does not share the detectors' reasoning: the
screens the run saved (screens/*.scr) against the bytes the claims name.

  accept.py <run folder> [...]        one game's folder (or several); writes accept.json in each, prints a line each
  accept.py --all <work folder>       every game folder under it, and a total

A sprite or tile claim is verified when a frame of it, cut by the claimed geometry (bytes a row, rows, mask, layout,
stride, prefix, pitch), shows on a saved screen: at some pixel position (any bit shift, mirrored or not) every ink pixel of
the frame is ink on the screen and the pixels the frame covers but leaves paper are paper (90 %; a masked frame: 95 %
of the pixels its mask clears). Frames with too little shape to tell (fewer than 8 ink pixels, more than 85 % ink,
fewer than 3 distinct rows) prove nothing. A cut off by a byte, a wrong row width or random bytes do not show
anywhere. A piece of a larger picture does show; it is told by its surroundings: where the frame shows at two or more
different places and the screen next to one side of it is the same non-empty band at every place, the picture goes on
past that side (a fragment); the frame's own edge repeated there (a wall of one tile) is no continuation.

A backdrop (one frame 128 pixels wide or more) that does not show so is read row by row: sprites move over it, and a
scroll by part of a byte, made when it is copied to the screen, moves each row (each band of a parallax its own) right
by 0..7 pixels, its last pixels pushed out. Only rows that tell their place count (ink and paper, not the same moved
by 1..8 pixels: a sky of stripes matches a byte off too); at one position 3 or more of them must show exact and 9 in 10
show, exact or but for one stretch inside, at most half the row and away from both ends (a sprite over it). A whole
byte off breaks every such row's end; the first and last rows with ink must show too (a row of something else at an
end: the claim takes in more). The row of memory just before (after) the claim showing on the line above
(below) it makes it a fragment. The memory is also read 1 and 2 frames before each screen (snapshots-before/): a game
scrolls its buffer after copying it to the screen.

A font claim (96 characters, codes 32..127, 8 bytes each) is verified when the saved screens, read through it cell by
cell (character cells, ink on paper or inverse), give strings of 4 or more characters that the game holds in memory as
bytes (the codes, or the last with bit 7 set): at least two different ones. The same reading through the claim moved by
1..3 characters either way must give fewer such strings (the claimed base is the one the text is in), and the letters
A..Z must be 26 different non-empty glyphs. A font of fewer characters (a claim of other than 96: codes unknown) is read
by its shapes: 8 or more glyphs look like the character after (before) the one their neighbour looks like (0..9, A..Z in
order), and the screens read through it show 4 or more of its cells, more than twice as many as through the claim moved
by 1..7 bytes (the glyph lattice). A font of shapes of its own (stylised digits, fewer than 8 glyphs in order) is told
by its placement instead: 64 or more cells read through it, eight times more than moved by 1..7 bytes, and its first and
last glyph with ink each on some cell (else its range takes in a glyph more). A game that shows no text it holds (digits
of a score, words drawn as pictures) is told by the shapes instead: each glyph of 0..9 and A..Z is matched to the
nearest glyph of the reference font (the 48 K ROM's at $3D00, as is or bold, moved by up to a pixel or two): 8 or more,
a quarter of them, must be recognised as themselves, three times more than through the claim moved by 1..3 characters
either way.

Coverage: of the ink pixels of the saved screens, the share the verified claims explain (the placements found, the
cells read as characters). A run passes when no graphics claim is unverified or a fragment.

Memory: the run's pages/ram-N.bin (16 K each) when they are there, else ram.bin (the Z80 view at the end) for claims
in the page mapped there."""

import argparse
import glob
import json
import os
import sys

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
REFERENCE_ROM = os.path.join(ROOT, 'data', 'rom', '48.rom')
MAX_SCREENS = 32
MAX_FRAMES = 48


# ---------------------------------------------------------------------------------------------------- screens

def screen_pixels(data):
    """6912 (or 6144) bytes of a Spectrum screen -> 192 x 256 bool (ink)"""
    bits = np.unpackbits(np.frombuffer(data[:6144], dtype=np.uint8)).reshape(192, 256)
    out = np.empty_like(bits)
    for y in range(192):
        row = ((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2)
        out[y] = bits[row // 32]
    return out.astype(bool)


def load_screens(folder):
    """The saved screens (screens/*.scr) and both screens of each snapshot (pages 5 and 7), distinct, at most
    MAX_SCREENS spread over the run"""
    found = [(os.path.basename(p), read_file(p)) for p in sorted(glob.glob(os.path.join(folder, 'screens', '*.scr')))]
    for d in sorted(glob.glob(os.path.join(folder, 'snapshots', '*'))):
        for page in (5, 7):
            p = os.path.join(d, f'ram-{page}.bin')
            if os.path.exists(p):
                found.append((f'{os.path.basename(d)}:{page}', read_file(p)))
    distinct, seen = [], set()
    for name, data in found:
        if len(data) < 6144 or data[:6144] in seen or not any(data[:6144]):
            continue
        seen.add(data[:6144])
        distinct.append((name, data))
    if len(distinct) > MAX_SCREENS:
        step = len(distinct) / MAX_SCREENS
        distinct = [distinct[int(i * step)] for i in range(MAX_SCREENS)]
    return [(name, screen_pixels(data)) for name, data in distinct]


# ---------------------------------------------------------------------------------------------------- memory

def read_file(path):
    with open(path, 'rb') as f:
        return f.read()


class Memory:
    def __init__(self, folder):
        self.pages = {}
        for p in glob.glob(os.path.join(folder, 'pages', 'ram-*.bin')):
            n = int(os.path.basename(p)[4:-4])
            self.pages[n] = read_file(p)
        rom = os.path.join(folder, 'pages', 'rom.bin')
        self.rom = read_file(rom) if os.path.exists(rom) else None
        path = os.path.join(folder, 'ram.bin')
        self.z80 = read_file(path) if os.path.exists(path) else b''
        self.snapshots = []
        for d in sorted(glob.glob(os.path.join(folder, 'snapshots', '*')) +
                        glob.glob(os.path.join(folder, 'snapshots-before', '*'))):
            pages = {}
            for p in glob.glob(os.path.join(d, 'ram-*.bin')):
                pages[int(os.path.basename(p)[4:-4])] = read_file(p)
            self.snapshots.append(pages)

    def region_bytes(self, region):
        start, end = int(region['start'][1:], 16), int(region['end'][1:], 16)
        page = region.get('page')
        if page in self.pages and start >= 0x4000:
            off = start & 0x3FFF
            return self.pages[page][off:off + end - start + 1]
        return self.z80[start:end + 1]

    def variants(self, region):
        """The claim's bytes as the run left them, then each different content a snapshot held there (graphics a
        level unpacks and the next one overwrites)"""
        out = [self.region_bytes(region)]
        start, end = int(region['start'][1:], 16), int(region['end'][1:], 16)
        page = region.get('page')
        if start >= 0x4000 and page is not None:
            for pages in self.snapshots:
                if page in pages:
                    data = pages[page][start & 0x3FFF:(start & 0x3FFF) + end - start + 1]
                    if data not in out:
                        out.append(data)
        return out

    def all_bytes(self):
        return [self.z80] + list(self.pages.values()) + [p for pages in self.snapshots for p in pages.values()]


# ---------------------------------------------------------------------------------------------------- sprites

def frames_of(region, data):
    """(ink, cover) bool arrays of each frame; cover None: no mask"""
    g = region.get('geometry') or {}
    w, h = int(g.get('width', 0)), int(g.get('height', 0))
    if w == 0 or h == 0 or w % 8:
        return []
    cols = w // 8
    mask = bool(g.get('mask'))
    pair = 2 if mask else 1
    prefix = int(g.get('prefix', 0))
    columns = g.get('layout') == 'columns'
    lines, length = (cols, h) if columns else (h, cols)
    pitch = int(g.get('pitch', 0)) or length * pair  # bytes from one row's (column's) start to the next
    size = (lines - 1) * pitch + length * pair + prefix
    stride = int(g.get('stride', 0)) or size
    out = []
    base = 0
    while base + size <= len(data) and len(out) < MAX_FRAMES:
        gfx = np.zeros((h, cols), dtype=np.uint8)
        msk = np.zeros((h, cols), dtype=np.uint8)
        for y in range(h):
            for c in range(cols):
                at = base + prefix + (c * pitch + y * pair if columns else y * pitch + c * pair)
                gfx[y, c] = data[at + (1 if mask else 0)]
                if mask:
                    msk[y, c] = data[at]
        ink = np.unpackbits(gfx, axis=1).astype(bool)
        cover = None
        if mask:
            # AND-mask convention: a clear mask bit is the sprite's own area
            cover = ~np.unpackbits(msk, axis=1).astype(bool) | ink
        out.append((ink, cover))
        base += stride
    return out


def informative(ink):
    n = int(ink.sum())
    rows = {bytes(np.packbits(r)) for r in ink if r.any()}
    return n >= 8 and n <= 0.85 * ink.size and len(rows) >= 3


def find(shot, ink, cover, limit=8):
    """Positions (y, x) where @p ink shows on @p shot"""
    h, w = ink.shape
    H, W = shot.shape[0] - h + 1, shot.shape[1] - w + 1
    if H <= 0 or W <= 0:
        return []
    valid = np.ones((H, W), dtype=bool)
    ys, xs = np.nonzero(ink)
    points = None
    for dy, dx in zip(ys, xs):
        if points is None:
            valid &= shot[dy:dy + H, dx:dx + W]
            if valid.sum() <= 4096:  # few candidates left: test them by index, not whole planes
                points = np.argwhere(valid)
        else:
            points = points[shot[points[:, 0] + dy, points[:, 1] + dx]]
        if (points is not None and len(points) == 0) or (points is None and not valid.any()):
            return []
    if points is None:
        points = np.argwhere(valid)
    paper = (cover & ~ink) if cover is not None else ~ink
    need = 0.95 if cover is not None else 0.90
    py, px = np.nonzero(paper)
    if len(py) == 0:
        return [(int(y), int(x)) for y, x in points[:limit]]
    out = []
    for at in range(0, len(points), 512):
        chunk = points[at:at + 512]
        inked = shot[chunk[:, 0:1] + py, chunk[:, 1:2] + px].sum(axis=1)  # paper pixels the screen inks
        for (y, x), n in zip(chunk, inked):
            if 1.0 - n / len(py) >= need:
                out.append((int(y), int(x)))
                if len(out) >= limit:
                    return out
    return out


BACKDROP_WIDTH = 128  # pixels: a frame this wide is a backdrop, sprites move over it
BACKDROP_ROWS = 0.9  # of its rows with shape, the share that must show at one place
BACKDROP_EXACT = 3  # rows with shape that must show exact there (the place's votes)


def backdrop_row(shot_row, row):
    """0: @p row shows exact on @p shot_row (both from the frame's place), moved right by 0..7 pixels (its last pixels
    pushed out); 1: but for one stretch inside, at most half the row, away from both ends (a sprite over the backdrop);
    2: no"""
    best = 2
    for d in range(8):
        n = len(row) - d
        diff = np.nonzero(shot_row[d:d + n] != row[:n])[0]
        if len(diff) == 0:
            return 0
        if diff[0] >= 8 and diff[-1] < n - 8 and diff[-1] - diff[0] < len(row) // 2:
            best = 1
    return best


def aligning(row):
    """A row that tells its place: ink and paper, and not the same moved by 1..8 pixels (a pattern a byte or less
    wide, a sky of stripes, matches a byte off too)"""
    return bool(row.any() and not row.all() and all(not np.array_equal(row[k:], row[:-k]) for k in range(1, 9)))


def find_backdrop(shot, ink):
    """Positions (y, x) where @p ink shows row by row: a scroll by part of a byte, made when the backdrop is copied to
    the screen, moves each row (each band of a parallax its own) right by 0..7 pixels; sprites over it break a stretch
    inside a row. Nine rows in ten of those that tell their place show, three or more exact, and the first and last
    rows with ink; a whole byte off breaks every such row's end"""
    h, w = ink.shape
    W = shot.shape[1] - w + 1
    if W <= 0 or h > shot.shape[0]:
        return []
    lines = [int.from_bytes(np.packbits(shot[y]).tobytes(), 'big') for y in range(shot.shape[0])]
    rows = [r for r in range(h) if aligning(ink[r])]
    if len(rows) < BACKDROP_EXACT:
        return []
    votes = {}
    for d in range(8):
        n = w - d  # the row's pixels still in the window
        places = {}
        for y, line in enumerate(lines):
            for x in range(d, W + d):
                places.setdefault((line >> (shot.shape[1] - x - n)) & ((1 << n) - 1), []).append((y, x - d))
        for r in rows:
            value = int.from_bytes(np.packbits(ink[r][:n]).tobytes(), 'big') >> (-n % 8)
            for y, x in places.get(value, []):
                if y - r >= 0 and y - r + h <= shot.shape[0]:
                    votes.setdefault((y - r, x), set()).add(r)
    inked = [r for r in range(h) if ink[r].any()]
    ends = {inked[0], inked[-1]}  # a row of something else at an end: the claim takes in more than the backdrop
    best = None
    for exact, (y, x) in sorted(((len(v), at) for at, v in votes.items() if len(v) >= BACKDROP_EXACT), reverse=True):
        shown = sum(1 for r in rows if backdrop_row(shot[y + r, x:x + w], ink[r]) < 2)
        if any(backdrop_row(shot[y + r, x:x + w], ink[r]) == 2 for r in ends):
            continue
        if shown >= BACKDROP_ROWS * len(rows) and (best is None or (shown, exact) > best[0]):
            best = ((shown, exact), (y, x))
    return [best[1]] if best else []  # one place: the moved rows match a pixel or two either side too


def bands(shot, y, x, h, w, depth=4):
    """The screen just outside the box on each side (None: off the screen)"""
    out = {}
    out['left'] = shot[y:y + h, x - depth:x].copy() if x >= depth else None
    out['right'] = shot[y:y + h, x + w:x + w + depth].copy() if x + w + depth <= shot.shape[1] else None
    out['top'] = shot[y - depth:y, x:x + w].copy() if y >= depth else None
    out['bottom'] = shot[y + h:y + h + depth, x:x + w].copy() if y + h + depth <= shot.shape[0] else None
    return out


def backdrop_goes_on(ink, shot, y, x, around):
    """The sides of a backdrop shown at (y, x) past which the picture goes on: the row of memory before (after) the
    claim, @p around, shows on the screen line above (below) it"""
    h, w = ink.shape
    out = set()
    for side, row, at in (('top', around[0], y - 1), ('bottom', around[1], y + h)):
        if row is None or len(row) * 8 != w or not 0 <= at < shot.shape[0]:
            continue
        bits = np.unpackbits(np.frombuffer(bytes(row), dtype=np.uint8)).astype(bool)
        if bits.any() and not bits.all() and backdrop_row(shot[at, x:x + w], bits) < 2:
            out.add(side)
    return out


def check_sprite(region, data, shots, around=None):
    """@p around: the rows of memory just before and after a claim of one frame stored row after row (None: unknown)"""
    frames = frames_of(region, data)
    result = {'frames': len(frames), 'informative': 0, 'shown': 0, 'fragment': [], 'placements': []}
    if not frames:
        result['status'] = 'unverified'
        result['why'] = 'no whole frame in the claimed geometry'
        return result
    fragment_sides = None
    for fi, (ink, cover) in enumerate(frames):
        if not informative(ink):
            continue
        result['informative'] += 1
        places = []
        for flip in (False, True):
            i = ink[:, ::-1] if flip else ink
            c = (cover[:, ::-1] if cover is not None else None) if flip else cover
            for si, (name, shot) in enumerate(shots):
                found = find(shot, i, c)
                if not found and c is None and not flip and i.shape[1] >= BACKDROP_WIDTH:
                    found = find_backdrop(shot, i)
                    if found and around:
                        fragment_sides = (fragment_sides or set()) | backdrop_goes_on(i, shot, *found[0], around)
                for (y, x) in found:
                    places.append((si, y, x, flip))
        if not places:
            continue
        result['shown'] += 1
        for si, y, x, flip in places:
            result['placements'].append({'frame': fi, 'screen': shots[si][0], 'x': x, 'y': y, 'flip': flip})
        # The picture goes on past a side: the same non-empty band there at two or more different places
        distinct = {(si, y, x) for si, y, x, _ in places}
        if len({(y, x) for _, y, x in distinct}) >= 2:
            h, w = ink.shape
            # The frame repeated next to itself (a wall of one tile) is no continuation
            own = {'left': ink[:, w - 4:], 'right': ink[:, :4], 'top': ink[h - 4:, :], 'bottom': ink[:4, :]}
            sides = None
            for si, y, x in distinct:
                b = bands(shots[si][1], y, x, h, w)
                b = {k: (None if v is not None and v.shape == own[k].shape and np.array_equal(v, own[k]) else v)
                     for k, v in b.items()}
                if sides is None:
                    sides = {k: v for k, v in b.items() if v is not None and v.any()}
                else:
                    sides = {k: v for k, v in sides.items() if b.get(k) is not None and np.array_equal(b[k], v)}
            fragment_sides = (fragment_sides or set()) | set(sides or {})
    result['fragment'] = sorted(fragment_sides or [])
    if result['informative'] == 0:
        result['status'] = 'trivial'
        result['why'] = 'no frame with enough shape to tell'
    elif result['shown'] == 0:
        result['status'] = 'unverified'
        result['why'] = f"none of {result['informative']} frames shows on any of {len(shots)} screens"
    elif result['fragment']:
        result['status'] = 'fragment'
        result['why'] = 'the picture goes on past ' + ', '.join(result['fragment'])
    else:
        result['status'] = 'verified'
    return result


# ---------------------------------------------------------------------------------------------------- fonts

def read_cells(shots, glyphs):
    """Strings read off the screens through @p glyphs (code -> 8 bytes): runs of 4+ characters, not all spaces"""
    table = {}
    for code, g in glyphs.items():
        if any(g) or code == 32:
            table.setdefault(bytes(g), code)
            table.setdefault(bytes(255 - b for b in g), code)
    strings, cells = set(), []
    for name, shot in shots:
        packed = np.packbits(shot, axis=1)  # 192 x 32
        for row in range(24):
            run = []
            for col in range(33):
                code = table.get(bytes(packed[row * 8:row * 8 + 8, col])) if col < 32 else None
                if code is not None:
                    run.append(code)
                    if code != 32:
                        cells.append((name, row, col))
                    continue
                text = bytes(run).strip(b' ')
                if len(text) >= 4:
                    strings.add(text)
                run = []
    return strings, cells


def held(strings, memories):
    found = set()
    for s in strings:
        for word in {s} | set(s.split(b' ')):
            if len(word) < 4:
                continue
            variants = (word, word[:-1] + bytes([word[-1] | 0x80]))
            if any(v in m for m in memories for v in variants):
                found.add(word)
    return found


SHAPE_CODES = list(range(48, 58)) + list(range(65, 91))
_reference = None


def glyph_bits(g):
    return np.unpackbits(np.frombuffer(bytes(g), dtype=np.uint8)).reshape(8, 8).astype(bool)


def reference_glyphs():
    """The ROM glyphs of 0..9, A..Z, as is and bold, padded by 2 pixels: (codes, 2, 12 x 12 pixels), or None"""
    global _reference
    if _reference is None and os.path.exists(REFERENCE_ROM):
        rom = read_file(REFERENCE_ROM)[0x3D00:0x4000]
        refs = []
        for c in SHAPE_CODES:
            r = glyph_bits(rom[(c - 32) * 8:(c - 32) * 8 + 8])
            refs.append([np.pad(r, 2), np.pad(r | np.roll(r, 1, 1), 2)])
        _reference = np.array(refs).reshape(len(SHAPE_CODES), 2, 1, 144)
    return _reference


def nearest_code(g):
    """The code (0..9, A..Z) of the reference glyph nearest @p g (8 x 8 bool): differing pixels over the pixels of
    both, at a move of up to 1 line and 2 pixels"""
    ref = reference_glyphs()
    padded = np.pad(g, 2)
    moves = np.array([np.roll(np.roll(padded, dy, 0), dx, 1).ravel()
                      for dy in (-1, 0, 1) for dx in (-2, -1, 0, 1, 2)])  # 15 x 144
    differ = (moves ^ ref).sum(axis=3)
    both = np.maximum(1, (moves | ref).sum(axis=3))
    return SHAPE_CODES[int(np.argmin((differ / both).min(axis=(1, 2))))]


def recognised(data, offset):
    """How many of 0..9, A..Z (non-empty) at @p offset (the glyph of code 32 there) look most like themselves in the
    reference font"""
    if reference_glyphs() is None:
        return 0, 0
    ok = total = 0
    for c in SHAPE_CODES:
        at = offset + (c - 32) * 8
        if at < 0 or at + 8 > len(data):
            continue
        g = glyph_bits(data[at:at + 8])
        if not g.any():
            continue
        total += 1
        ok += nearest_code(g) == c
    return ok, total


def in_sequence(data):
    """A font of unknown codes: the glyphs (8 bytes each) recognised as the character after the one before them or before
    the one after them (0..9, A..Z in their order); (count, the code of the first glyph most of them imply)"""
    if reference_glyphs() is None:
        return 0, None
    codes = []
    for i in range(len(data) // 8):
        g = glyph_bits(data[8 * i:8 * i + 8])
        codes.append(nearest_code(g) if g.any() and not g.all() else None)
    hits, bases = 0, {}
    for i, c in enumerate(codes):
        if c is None:
            continue
        if (i > 0 and codes[i - 1] == c - 1) or (i + 1 < len(codes) and codes[i + 1] == c + 1):
            hits += 1
            bases[c - i] = bases.get(c - i, 0) + 1
    return hits, (max(bases, key=bases.get) if bases else None)


def ends_shown(data, shots):
    """The first and the last glyph of @p data with ink both show on a character cell of the screens (as is or
    inverse)"""
    cells = set()
    for _, shot in shots:
        packed = np.packbits(shot, axis=1)
        for row in range(24):
            for col in range(32):
                cell = bytes(packed[row * 8:row * 8 + 8, col])
                cells.add(cell)
                cells.add(bytes(255 - b for b in cell))
    glyphs = [bytes(data[at:at + 8]) for at in range(0, len(data) // 8 * 8, 8)]
    glyphs = [g for g in glyphs if any(g)]  # a blank glyph at an end (a space) shows anywhere: no evidence
    return bool(glyphs) and glyphs[0] in cells and glyphs[-1] in cells


MIN_PLACED_CELLS = 64  # character cells a font of its own shapes must be read on, eight times its misaligned reads


def check_custom_font(region, data, shots):
    """A font whose codes the claim does not name: glyphs in the order of the characters they look like, and the
    screens' character cells read through it, more than through the claim moved by 1..7 bytes (the glyph lattice).
    Shapes of its own (no glyph looks like a standard character): many cells read as claimed, almost none moved"""
    result = {}
    hits, base = in_sequence(data)
    result['first_code'] = base
    result['in_sequence'] = hits

    def table(offset):
        first = base if base is not None else 0
        return {first + i: bytes(data[offset + 8 * i:offset + 8 * i + 8]) for i in range((len(data) - offset) // 8)}

    strings, cells = read_cells(shots, table(0))
    moved = max(len(read_cells(shots, table(k))[1]) for k in range(1, 8)) if len(data) > 8 else 0
    result['strings'] = sorted(t.decode('latin-1') for t in strings if all(32 <= c < 127 for c in t))[:12]
    result['cells'] = len(cells)
    result['cells_moved'] = moved
    if hits < 8 and (len(cells) < MIN_PLACED_CELLS or len(cells) < 8 * moved):
        # A drawn font of its own shapes (stylised digits): read on the screens' character cells only as claimed
        result['status'] = 'unverified'
        result['why'] = (f'{hits} glyphs look like the character after (before) their neighbour, fewer than 8; '
                         f'{len(cells)} cells read as claimed, {moved} moved by bytes')
    elif hits < 8 and not ends_shown(data, shots):
        # The lattice is right, the claim's range is not: a glyph at an end no cell shows (one before the font, after)
        result['status'] = 'misaligned'
        result['why'] = f'{len(cells)} cells read as claimed, but its first or last glyph shows on no cell'
    elif hits < 8:
        result['status'] = 'verified'
        result['by'] = 'placement'
    elif len(cells) < 4:
        result['status'], result['why'] = 'unverified', f'{len(cells)} character cells on the screens read through it'
    elif len(cells) <= 2 * moved:
        result['status'] = 'misaligned'
        result['why'] = f'moved by bytes it reads {moved} cells, {len(cells)} as claimed'
    else:
        result['status'] = 'verified'
        result['by'] = 'shapes'
    return result, cells


def check_font(region, data, shots, memory):
    result = {}
    if int((region.get('geometry') or {}).get('frames', 96)) != 96:
        return check_custom_font(region, data, shots)
    if len(data) < 768:
        return {'status': 'unverified', 'why': f'{len(data)} bytes, a font of 96 characters has 768'}, []

    def glyphs_at(offset):
        out = {}
        for code in range(32, 128):
            at = offset + (code - 32) * 8
            out[code] = data[at:at + 8] if 0 <= at and at + 8 <= len(data) else b''
        return {c: g for c, g in out.items() if len(g) == 8}

    letters = [bytes(data[(c - 32) * 8:(c - 32) * 8 + 8]) for c in range(65, 91)]
    result['letters'] = len({g for g in letters if any(g)})
    memories = memory.all_bytes()
    strings, cells = read_cells(shots, glyphs_at(0))
    words = held(strings, memories)
    result['strings'] = len(strings)
    result['held'] = sorted(w.decode('latin-1') for w in words)[:12]
    result['cells'] = len(cells)
    shifted = {}
    for k in (-3, -2, -1, 1, 2, 3):
        s2, _ = read_cells(shots, glyphs_at(8 * k))
        shifted[k] = len(held(s2, memories))
    result['shifted'] = shifted
    best_other = max(shifted.values()) if shifted else 0
    shapes, total = recognised(data, 0)
    shapes_moved = {k: recognised(data, 8 * k)[0] for k in (-3, -2, -1, 1, 2, 3)}
    result['shapes'] = f'{shapes}/{total}'
    result['shapes_moved'] = shapes_moved
    by_shape = shapes >= 8 and shapes * 4 >= total and shapes >= 3 * max(shapes_moved.values())
    if result['letters'] < 26:
        result['status'], result['why'] = 'unverified', f"{result['letters']} distinct non-empty letters A..Z, not 26"
    elif len(words) < 2 and by_shape and cells:
        result['status'] = 'verified'
        result['by'] = 'shapes'
    elif len(words) < 2 and max(shapes_moved.values()) >= 8 and max(shapes_moved.values()) > shapes:
        result['status'] = 'misaligned'
        result['why'] = f'moved by characters it recognises {max(shapes_moved.values())} glyphs, more than {shapes}'
    elif len(words) < 2:
        result['status'] = 'unverified'
        result['why'] = (f'{len(strings)} strings read off the screens, {len(words)} of them in memory; '
                         f'{shapes}/{total} glyphs recognised')
    elif best_other >= len(words):
        result['status'] = 'misaligned'
        result['why'] = f'moved by characters it reads {best_other} held strings, as many as the claim ({len(words)})'
    else:
        result['status'] = 'verified'
    return result, cells


# ---------------------------------------------------------------------------------------------------- a run

GRAPHICS = ('sprite', 'tile', 'font')
# Of the contents a claim held during the run, the one that does best
RANK = {'verified': 4, 'fragment': 3, 'misaligned': 2, 'unverified': 1, 'trivial': 0}


def is_backdrop(region):
    """One frame BACKDROP_WIDTH or wider, stored row after row, no mask"""
    g = region.get('geometry') or {}
    return (int(g.get('width', 0)) >= BACKDROP_WIDTH and not g.get('mask') and g.get('layout') != 'columns' and
            int(g.get('frames', 1)) == 1)


def moved_region(region, delta):
    """@p region moved by @p delta bytes, in the same page (None: it would leave the page)"""
    start, end = int(region['start'][1:], 16) + delta, int(region['end'][1:], 16) + delta
    if start < 0x4000 or end > 0xFFFF or start >> 14 != (start - delta) >> 14 or end >> 14 != (end - delta) >> 14:
        return None
    return dict(region, start=f'${start:04X}', end=f'${end:04X}')


def check_contents(memory, region, variants, shots):
    """(the best result and its data, every (result, data) that verifies) over the contents of a sprite claim: every
    content that verifies explains its placements (a scrolled backdrop: each screen its own)"""
    best, shown = None, []
    for data, around in with_rows_around(memory, region, variants):
        res = check_sprite(region, data, shots, around)
        if best is None or RANK[res['status']] > RANK[best[0]['status']]:
            best = (res, data)
        if res['status'] == 'verified':
            shown.append((res, data))
    return best, shown


def with_rows_around(memory, region, variants):
    """(data, around) of each content: a backdrop (one frame, BACKDROP_WIDTH or wider, stored row after row) with the
    rows of memory just before and after it in the same page, read from the same content; else around None"""
    g = region.get('geometry') or {}
    w = int(g.get('width', 0))
    start, end = int(region['start'][1:], 16), int(region['end'][1:], 16)
    pitch = int(g.get('pitch', 0)) or w // 8
    if (not is_backdrop(region) or (start - pitch) >> 14 != start >> 14 or (end + pitch) >> 14 != end >> 14 or start < 0x4000):
        return [(data, None) for data in variants]
    wide = dict(region, start=f'${start - pitch:04X}', end=f'${end + pitch:04X}')
    out = []
    for ext in memory.variants(wide):
        data = ext[pitch:len(ext) - pitch]
        if data in variants and all(data != d for d, _ in out):
            out.append((data, (ext[:w // 8], ext[len(ext) - pitch:len(ext) - pitch + w // 8])))
    return out + [(data, None) for data in variants if all(data != d for d, _ in out)]


def accept(folder):
    regions_path = os.path.join(folder, 'regions-all.json')
    if not os.path.exists(regions_path):
        return None
    regions = json.load(open(regions_path)).get('regions', [])
    shots = load_screens(folder)
    memory = Memory(folder)
    explained = [np.zeros((192, 256), dtype=bool) for _ in shots]
    claims = []
    for r in regions:
        if r['kind'] not in GRAPHICS:
            continue
        entry = {'kind': r['kind'], 'start': r['start'], 'end': r['end'], 'detector': r.get('detector'),
                 'geometry': r.get('geometry')}
        variants = memory.variants(r)
        entry['contents'] = len(variants)
        if r['kind'] == 'font':
            best = None
            for data in variants:
                res, cells = check_font(r, data, shots, memory)
                if best is None or RANK[res['status']] > RANK[best[0]['status']]:
                    best = (res, cells)
                if res['status'] == 'verified':
                    break
            res, cells = best
            entry.update(res)
            if res.get('status') == 'verified':
                names = [n for n, _ in shots]
                for name, row, col in cells:
                    explained[names.index(name)][row * 8:row * 8 + 8, col * 8:col * 8 + 8] = True
        else:
            best, shown = check_contents(memory, r, variants, shots)
            res, data = best
            entry.update({k: v for k, v in res.items() if k != 'placements'})
            entry['placements'] = res['placements'][:4]
            if res['status'] == 'verified' and is_backdrop(r):
                # A backdrop sparse in one content shows a byte (a row) off too: the claim moved must show less
                screens = {p['screen'] for res2, _ in shown for p in res2['placements']}
                pitch = int(r['geometry'].get('pitch', 0)) or int(r['geometry']['width']) // 8
                for delta in (-1, 1, -pitch, pitch):
                    moved = moved_region(r, delta)
                    if moved is None:
                        continue
                    _, shown2 = check_contents(memory, moved, memory.variants(moved), shots)
                    screens2 = {p['screen'] for res2, _ in shown2 for p in res2['placements']}
                    if len(screens2) > len(screens):
                        entry['status'] = 'misaligned'
                        entry['why'] = (f'moved by {delta} bytes it shows on {len(screens2)} screens, '
                                        f'{len(screens)} as claimed')
                        shown = []
                        break
            names = [n for n, _ in shots]
            for res, data in shown:
                frames = frames_of(r, data)
                for p in res['placements']:
                    ink = frames[p['frame']][0]
                    h, w = ink.shape
                    y, x = p['y'], max(0, p['x'])
                    explained[names.index(p['screen'])][y:y + h, x:p['x'] + w] = True
        claims.append(entry)
    ink_total = sum(int(s.sum()) for _, s in shots)
    ink_explained = sum(int((s & e).sum()) for (_, s), e in zip(shots, explained))
    counts = {}
    for c in claims:
        counts[f"{c['kind']}:{c['status']}"] = counts.get(f"{c['kind']}:{c['status']}", 0) + 1
    bad = [c for c in claims if c['status'] in ('unverified', 'fragment', 'misaligned')]
    report = {
        'folder': folder, 'screens': len(shots), 'claims': len(claims), 'counts': counts,
        'verified': sum(1 for c in claims if c['status'] == 'verified'),
        'bad': len(bad),
        'coverage': round(ink_explained / ink_total, 3) if ink_total else 0.0,
        'pass': not bad and any(c['status'] == 'verified' for c in claims),
        'details': claims,
    }
    with open(os.path.join(folder, 'accept.json'), 'w') as f:
        json.dump(report, f, indent=1)
    coverage_picture(os.path.join(folder, 'accept.png'), shots, explained)
    return report


def coverage_picture(path, shots, explained, across=6):
    """The screens: ink a verified claim explains green, the rest of the ink white (PIL, when it is there)"""
    try:
        from PIL import Image
    except ImportError:
        return
    if not shots:
        return
    down = (len(shots) + across - 1) // across
    sheet = np.zeros((down * 196, across * 260, 3), dtype=np.uint8)
    for i, ((_, shot), e) in enumerate(zip(shots, explained)):
        y, x = (i // across) * 196, (i % across) * 260
        tile = np.zeros((192, 256, 3), dtype=np.uint8)
        tile[shot & ~e] = (255, 255, 255)
        tile[shot & e] = (40, 220, 60)
        tile[~shot & e] = (0, 60, 0)
        sheet[y:y + 192, x:x + 256] = tile
    Image.fromarray(sheet).save(path)


def line(r):
    key = os.path.basename(os.path.normpath(r['folder']))
    counts = ' '.join(f'{k}={v}' for k, v in sorted(r['counts'].items()))
    return f"{key:14} {'PASS' if r['pass'] else 'FAIL'} cover {r['coverage'] * 100:5.1f}%  {counts}"


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('folders', nargs='*')
    p.add_argument('--all', help='a work folder: every game folder under it')
    args = p.parse_args()
    folders = list(args.folders)
    if args.all:
        folders += sorted(os.path.dirname(x) for x in glob.glob(os.path.join(args.all, '*', 'regions-all.json')))
    reports = [r for r in (accept(f) for f in folders) if r]
    for r in reports:
        print(line(r))
    if len(reports) > 1:
        passed = sum(1 for r in reports if r['pass'])
        cover = sum(r['coverage'] for r in reports) / len(reports)
        print(f'{passed} / {len(reports)} pass, mean coverage {cover * 100:.1f}%')
    return 0 if reports and all(r['pass'] for r in reports) else 1


if __name__ == '__main__':
    sys.exit(main())
