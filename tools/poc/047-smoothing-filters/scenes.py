"""Generic frame-rate conversion scenes with ground truth at any time (POC 047, README "Generic suite").

Every scene is a stack of elements on a 352 x 288 ZX-like picture (256 x 192 screen at 48, 48, a border round it):

- layer: a 1-bit bitmap (tiled or a single sprite) moving rigidly at a constant velocity, clipped to a rectangle.
  Source frame k shows it at p0 + v * k (whole pixels); the truth at a time T between frames shows it at p0 + v * T,
  rounded down or up (both count as right).
- flux: content that no motion describes (a glyph stretching, colours cycling, a sprite blinking). The truth is
  either source frame, whole per 8 x 8 cell: a cell mixing them (a glyph built of rows of both) is wrong.

The kinds the suite covers, each against every other in some scene: periodic content (dither, stripes, monospaced
text, tiles) at speeds that alias with its period, unique content, all directions, static layers over and under
motion (text, panels, still sprites), parallax, sprites, and flux next to motion.

    python3 scenes.py <out dir> [scene,...]   writes <scene>.rgba (300 frames at 50 Hz) per scene
"""
import os
import sys

import numpy as np

W, H, SX, SY, SW, SH = 352, 288, 48, 48, 256, 192
FRAMES, SOURCE_HZ = 300, 50.0
ROM = os.path.join(os.path.dirname(__file__), "../../../data/rom/48.rom")

# ZX palette (0..7 normal, 8..15 bright)
PALETTE = np.array([[(0xFF if b else 0xD7) * ((c >> s) & 1) for s in (1, 2, 0)] for b in (0, 1) for c in range(8)],
                   np.uint8)
BLACK, BLUE, RED, MAGENTA, GREEN, CYAN, YELLOW, WHITE = range(8)
BRIGHT = 8


def font():
    rom = np.fromfile(ROM, np.uint8)
    return np.unpackbits(rom[0x3D00:0x3D00 + 96 * 8]).reshape(96, 8, 8)


FONT = font()


def text(s):
    """A line of the ROM font, 8 px a character, as a 0/1 bitmap"""
    return np.concatenate([FONT[min(max(ord(c), 32), 127) - 32] for c in s], 1)


def rng(seed):
    return np.random.default_rng(seed)


def noise(h, w, seed, density=0.5):
    return (rng(seed).random((h, w)) < density).astype(np.uint8)


def dither(h, w):
    y, x = np.mgrid[0:h, 0:w]
    return ((x + y) & 1).astype(np.uint8)


def stripes(h, w, period, horizontal=False):
    y, x = np.mgrid[0:h, 0:w]
    return (((y if horizontal else x) % period) < period // 2).astype(np.uint8)


def tiles(h, w, size, seed):
    tile = noise(size, size, seed, 0.45)
    tile[0, :] = tile[:, 0] = 1
    return np.tile(tile, (h // size + 1, w // size + 1))[:h, :w]


def terrain(h, w, seed):
    """A unique silhouette: a random walk of heights, solid below with a dither band at the top"""
    r = rng(seed)
    height = np.clip(np.cumsum(r.integers(-2, 3, w)) + h // 2, 2, h - 2)
    y = np.arange(h)[:, None]
    solid = (y >= height[None, :]).astype(np.uint8)
    band = (y >= height[None, :]) & (y < height[None, :] + 4)
    return np.where(band, dither(h, w), solid).astype(np.uint8)


def stars(h, w, seed):
    return noise(h, w, seed, 0.03)


def sprite(seed, size=16):
    """A closed blob with a hole and a stripe: no period inside"""
    r = rng(seed)
    y, x = np.mgrid[0:size, 0:size] - (size - 1) / 2
    radius = size / 2 - 1 + r.normal(0, 0.8, (size, size))
    shape = (np.hypot(x, y) < radius).astype(np.uint8)
    shape[size // 2 - 2:size // 2 + 1, size // 2 - 2:size // 2 + 2] = 0
    shape[3, 2:size - 2] = 1
    return shape


def dotted(bitmap, cell=8, rows=4):
    """Every pixel of a bitmap as a dot in a cell x rows grid (dot matrix text: long periodic runs of dots)"""
    dot = np.zeros((rows, cell), np.uint8)
    dot[:rows - 1, 2:cell - 2] = 1
    dot[1, 1:cell - 1] = 1
    return np.kron(bitmap, dot)


def disc_phases(cell=8):
    """A disc with a notch turning a quarter a frame (a spinning ball, a rotating coin): 4 shapes in a cell"""
    y, x = np.mgrid[0:cell, 0:cell] - (cell - 1) / 2
    disc = np.hypot(x, y) < cell / 2 - 0.6
    angle = (np.degrees(np.arctan2(y, x)) + 360) % 360
    return [(disc & ~((angle >= 90 * q) & (angle < 90 * q + 60) & (np.hypot(x, y) > 0.8))).astype(np.uint8)
            for q in range(4)]


def animated_dots(bitmap, phases):
    """Dot matrix text of animated dots: every frame the dots take the next shape"""
    return [np.kron(bitmap, phase) for phase in phases]


def animated_tiles(h, w, size, seed, count=4):
    """Tiles whose pixels flicker in place (water, lava): a fifth of a tile's pixels change between the shapes"""
    base = noise(size, size, seed, 0.45)
    base[0, :] = base[:, 0] = 1
    shapes = []
    for i in range(count):
        tile = base.copy()
        flip = noise(size, size, seed + 1 + i, 0.2).astype(bool)
        flip[0, :] = flip[:, 0] = False
        tile[flip] ^= 1
        shapes.append(np.tile(tile, (h // size + 1, w // size + 1))[:h, :w])
    return shapes


def lone(bitmap, h=192 + 48, w=256 + 48):
    """One sprite tiled with a period wider than the screen: it leaves one side and comes back on the other"""
    out = np.zeros((h, w), np.uint8)
    out[:bitmap.shape[0], :bitmap.shape[1]] = bitmap
    return out


class Layer:
    """A rigid 1-bit bitmap: ink where 1, paper where 0 (transparent when paper is None). It steps v moves(k) times by
    source frame k (default: every frame); between frames k and k + 1 the truth goes straight from one to the other
    (the best a display may show without waiting for frame k + 2)"""

    def __init__(self, name, bitmap, rect, v, p0=(0, 0), ink=WHITE, paper=None, tiled=True, moves=None, frames=None):
        self.name, self.rect, self.v, self.p0 = name, rect, v, p0
        self.bitmap = frames[0] if frames is not None else bitmap
        self.ink, self.paper, self.tiled, self.moves = ink, paper, tiled, moves or (lambda k: k)
        self.frames = frames  # animated: frame k shows frames[k % len]; the truth between k and k + 1 shows either

    def shape(self, k):
        return self.frames[k % len(self.frames)] if self.frames is not None else self.bitmap

    def steps(self, t):
        k = int(np.floor(t))
        return self.moves(k) + (self.moves(k + 1) - self.moves(k)) * (t - k)

    def seen(self):
        """The velocities the frames cannot tell from v (v plus a shift the tiled bitmap repeats under), the slowest:
        what an eye sees (stripes of period 4 moving 4 px a frame stand still)"""
        if not self.tiled or self.v == (0, 0):
            return [self.v]
        bh, bw = self.bitmap.shape
        alts = [self.v]
        for dy in range(-16, 17):
            for dx in range(-16, 17):
                if (dx or dy) and np.array_equal(np.roll(self.bitmap, (dy, dx), (0, 1)), self.bitmap):
                    alts.append((self.v[0] + dx, self.v[1] + dy))
        least = min(x * x + y * y for x, y in alts)
        return [a for a in alts if a[0] ** 2 + a[1] ** 2 == least]

    def draw(self, img, ids, eid, t, rounding, v=None, shape=0):
        x0, y0, w, h = self.rect
        v = v or self.v
        bitmap = self.shape(int(np.floor(t)) + shape)
        m = self.steps(t)
        px = self.p0[0] + v[0] * m
        py = self.p0[1] + v[1] * m
        rx, ry = (rounding, rounding) if isinstance(rounding, str) else rounding  # per axis: "floor", "ceil", "round"
        whole = {"floor": np.floor, "ceil": np.ceil, "round": lambda c: np.floor(c + 0.5)}
        px, py = (int(whole[r](c)) for c, r in ((px, rx), (py, ry)))
        bh, bw = bitmap.shape
        y, x = np.mgrid[0:h, 0:w]
        u, v = x - px, y - py
        if self.tiled:
            inside = np.ones((h, w), bool)
            bit = bitmap[v % bh, u % bw]
        else:
            inside = (u >= 0) & (u < bw) & (v >= 0) & (v < bh)
            bit = np.where(inside, bitmap[np.clip(v, 0, bh - 1), np.clip(u, 0, bw - 1)], 0)
        drawn = inside & ((bit == 1) | (self.paper is not None))
        colour = np.where(bit == 1, self.ink, self.paper if self.paper is not None else 0)
        sy, sx = slice(SY + y0, SY + y0 + h), slice(SX + x0, SX + x0 + w)
        img[sy, sx] = np.where(drawn[..., None], PALETTE[colour], img[sy, sx])
        ids[sy, sx] = np.where(drawn, eid, ids[sy, sx])


class Flux:
    """Content changing without motion: bitmap(k) -> (colour index or -1, ...) per pixel of rect, whole frames only"""

    def __init__(self, name, rect, frame):
        self.name, self.rect, self.frame = name, rect, frame

    def draw(self, img, ids, eid, t, rounding, v=None, shape=0):
        x0, y0, w, h = self.rect
        colours = self.frame(int(t) + shape)
        sy, sx = slice(SY + y0, SY + y0 + h), slice(SX + x0, SX + x0 + w)
        drawn = colours >= 0
        img[sy, sx] = np.where(drawn[..., None], PALETTE[np.maximum(colours, 0)], img[sy, sx])
        ids[sy, sx] = np.where(drawn, eid, ids[sy, sx])


def stretch_text(s, ink, seed):
    """A text line whose glyphs grow by 0..2 rows at the top (rows doubled in the middle), the bottom fixed"""
    line = text(s)
    heights = rng(seed).permutation(np.tile([0, 1, 2, 1], 8))

    def frame(k):
        grow = int(heights[k % len(heights)])
        rows = list(range(8))
        for g in range(grow):
            rows.insert(4, 3 + g)
        out = np.full((16, line.shape[1]), -1)
        block = line[rows]
        out[16 - len(rows):] = np.where(block == 1, ink, -1)
        return out

    return frame


def cycle_text(s, seed):
    line = text(s)
    order = rng(seed).permutation([BLUE, RED, MAGENTA, GREEN, CYAN, YELLOW, WHITE]) + BRIGHT

    def frame(k):
        return np.where(line == 1, order[k % len(order)], -1)

    return frame


def blink(bitmap, ink, period):
    def frame(k):
        return np.where((bitmap == 1) & ((k // period) % 2 == 0), ink, -1)

    return frame


def marching(w, h, ink):
    """A dashed frame whose dashes run round it 1 px a frame: motion along a path, not a rigid layer"""
    perimeter = 2 * (w + h) - 4
    path = [(x, 0) for x in range(w)] + [(w - 1, y) for y in range(1, h)] + \
           [(x, h - 1) for x in range(w - 2, -1, -1)] + [(0, y) for y in range(h - 2, 0, -1)]

    def frame(k):
        out = np.full((h, w), -1)
        for i, (x, y) in enumerate(path):
            if (i + k) % 5 < 3:
                out[y, x] = ink
        return out

    assert len(path) == perimeter
    return frame


LOREM = "PRESS ANY KEY TO START THE GAME. HIGH SCORE 012340 LIVES 3 "


def scenes():
    s = {}
    # Horizontal strips: the periods against the speeds that alias with them, and unique content
    s["strips"] = [
        Layer("dither -1", dither(24, 256), (0, 0, 256, 24), (-1, 0), ink=CYAN),
        Layer("dither -2", dither(24, 256), (0, 24, 256, 24), (-2, 0), ink=GREEN),
        Layer("stripes4 -4", stripes(24, 256, 4), (0, 48, 256, 24), (-4, 0), ink=YELLOW),
        Layer("text -8", np.tile(text(LOREM)[:, :256], (3, 1)), (0, 72, 256, 24), (-8, 0), ink=WHITE + BRIGHT),
        Layer("text -2", text(LOREM[::-1]), (0, 96, 256, 8), (-2, 0), ink=YELLOW + BRIGHT),
        Layer("tiles16 +3", tiles(32, 256, 16, 1), (0, 112, 256, 32), (3, 0), ink=MAGENTA),
        Layer("noise -5", noise(24, 256, 2), (0, 144, 256, 24), (-5, 0), ink=WHITE),
        Layer("terrain -3", terrain(24, 512, 3), (0, 168, 256, 24), (-3, 0), ink=GREEN + BRIGHT),
    ]
    # Vertical and diagonal motion: columns and a full-screen layer under a static panel
    s["vertical"] = [
        Layer("dither v+1", dither(192, 64), (0, 0, 64, 192), (0, 1), ink=CYAN),
        Layer("text v-8", np.concatenate([text(LOREM[i:i + 8]) for i in range(0, 48, 8)], 0), (64, 0, 64, 192),
              (0, -8), ink=YELLOW + BRIGHT),
        Layer("tiles v+4", tiles(192, 64, 8, 4), (128, 0, 64, 192), (0, 4), ink=GREEN),
        Layer("noise v-3", noise(192, 64, 5), (192, 0, 64, 192), (0, -3), ink=WHITE),
    ]
    s["diagonal"] = [
        Layer("terrain (2,1)", terrain(160, 512, 6) | stars(160, 512, 7), (0, 0, 256, 160), (-2, 1), ink=GREEN),
        Layer("panel", np.zeros((32, 256), np.uint8), (0, 160, 256, 32), (0, 0), ink=WHITE, paper=BLUE),
        Layer("panel text", text("SCORE 001230  HI 045600 *** "[:32]), (0, 172, 256, 8), (0, 0), ink=YELLOW + BRIGHT),
        Layer("dither (-3,-2)", dither(48, 96), (160, 16, 96, 48), (-3, -2), ink=CYAN, paper=BLACK),
    ]
    # Static things over and under motion: text and still sprites over a scroll, an opaque panel beside it
    still = np.zeros((192, 256), np.uint8)
    for i, (x, y) in enumerate([(40, 30), (120, 70), (200, 110), (60, 140)]):
        still[y:y + 16, x:x + 16] |= sprite(10 + i)
    s["overlay"] = [
        Layer("tiles -4", tiles(160, 256, 8, 8), (0, 0, 256, 160), (-4, 0), ink=CYAN),
        Layer("still sprites", still[:160], (0, 0, 256, 160), (0, 0), ink=WHITE + BRIGHT),
        Layer("still text", text("STATIC TEXT OVER THE SCROLL"), (16, 96, 216, 8), (0, 0), ink=YELLOW + BRIGHT),
        Layer("panel", dither(32, 256) * 0, (0, 160, 256, 32), (0, 0), paper=RED),
        Layer("panel text", text(" LIVES 3   FUEL ######  ZONE 4 "), (0, 172, 248, 8), (0, 0), ink=WHITE + BRIGHT),
    ]
    # A text scroller between static text lines in the same font (the -8 alias of the static lines)
    s["textlines"] = [
        Layer("static above", text(LOREM[:32]), (0, 64, 256, 8), (0, 0), ink=YELLOW + BRIGHT),
        Layer("scroller -8", text(LOREM * 2), (0, 80, 256, 8), (-8, 0), ink=WHITE + BRIGHT),
        Layer("static below", text(LOREM[16:48]), (0, 96, 256, 8), (0, 0), ink=YELLOW + BRIGHT),
        Layer("scroller -1", text(LOREM[::-1] * 2), (0, 120, 256, 8), (-1, 0), ink=CYAN + BRIGHT),
        Layer("static after", text(LOREM[8:40]), (0, 136, 256, 8), (0, 0), ink=GREEN + BRIGHT),
        Layer("dots -8", stripes(8, 256, 8) * stripes(8, 256, 8, True), (0, 152, 256, 8), (-8, 0), ink=RED + BRIGHT),
        Layer("dots -4", stripes(24, 256, 8) * stripes(24, 256, 8, True), (0, 168, 256, 24), (-4, 0), ink=WHITE + BRIGHT),
    ]
    # Parallax: transparent layers at different speeds, sprites across them
    s["parallax"] = [
        Layer("stars -1", stars(192, 256, 20), (0, 0, 256, 192), (-1, 0), ink=WHITE),
        Layer("tiles -2", tiles(48, 256, 16, 21) * (np.arange(48)[:, None] > 24), (0, 96, 256, 48), (-2, 0),
              ink=BLUE + BRIGHT),
        Layer("terrain -4", terrain(48, 512, 22), (0, 144, 256, 48), (-4, 0), ink=GREEN + BRIGHT),
        Layer("ship +1,+1", lone(sprite(23)), (0, 0, 256, 192), (1, 1), p0=(20, 20), ink=YELLOW + BRIGHT),
        Layer("ship -5,+2", lone(sprite(24)), (0, 0, 256, 192), (-5, 2), p0=(240, 40), ink=MAGENTA + BRIGHT),
        Layer("ship +3,0", lone(sprite(25)), (0, 0, 256, 192), (3, 0), p0=(0, 150), ink=CYAN + BRIGHT),
    ]
    # Sprites over a static detailed background
    s["sprites"] = [
        Layer("background", tiles(192, 256, 16, 30) & dither(192, 256), (0, 0, 256, 192), (0, 0), ink=BLUE),
        Layer("sprite +2,0", lone(sprite(31)), (0, 0, 256, 192), (2, 0), p0=(-16, 20), ink=WHITE + BRIGHT),
        Layer("sprite -1,+1", lone(sprite(32)), (0, 0, 256, 192), (-1, 1), p0=(200, 10), ink=YELLOW + BRIGHT),
        Layer("sprite 0,-4", lone(sprite(33)), (0, 0, 256, 192), (0, -4), p0=(120, 300), ink=GREEN + BRIGHT),
        Layer("sprite +5,+3", lone(sprite(34)), (0, 0, 256, 192), (5, 3), p0=(0, 0), ink=RED + BRIGHT),
        Layer("sprite -8,0", lone(sprite(35)), (0, 0, 256, 192), (-8, 0), p0=(400, 100), ink=CYAN + BRIGHT),
    ]
    # Motion that is not steady: every 2nd or 3rd frame, speeding up, stopping and starting
    s["cadence"] = [
        Layer("text -8 every 2nd", text(LOREM * 2), (0, 8, 256, 8), (-8, 0), ink=WHITE + BRIGHT, moves=lambda k: k // 2),
        Layer("tiles +4 every 3rd", tiles(32, 256, 16, 50), (0, 24, 256, 32), (4, 0), ink=CYAN + BRIGHT,
              moves=lambda k: k // 3),
        Layer("terrain -2 every 2nd", terrain(40, 512, 51), (0, 64, 256, 40), (-2, 0), ink=GREEN + BRIGHT,
              moves=lambda k: k // 2),
        Layer("noise speeding up", noise(32, 256, 52), (0, 112, 256, 32), (-1, 0), ink=YELLOW + BRIGHT,
              moves=lambda k: k * k // 60),  # 0 .. 10 px a frame
        Layer("text stop-start", text(LOREM * 2), (0, 152, 256, 8), (-4, 0), ink=MAGENTA + BRIGHT,
              moves=lambda k: (k // 40) * 25 + min(k % 40, 25)),  # 25 frames moving, 15 still
        Layer("sprite stop-start", lone(sprite(53)), (0, 0, 256, 192), (3, -2), p0=(40, 180), ink=RED + BRIGHT,
              moves=lambda k: (k // 30) * 20 + min(k % 30, 20)),
    ]
    # Dot matrix text: the dot pitch matches the speed or half of it, look-alike shifts everywhere but at the ends
    s["dotmatrix"] = [
        Layer("D", text("D8"), (0, 8, 16, 8), (0, 0), ink=WHITE + BRIGHT),
        Layer("dots text -8", dotted(text(" BEST TEST SCORE")[:4, :])[:, :], (24, 0, 232, 16), (-8, 0), ink=WHITE + BRIGHT),
        Layer("dots text -4", dotted(text(" FULL DOTS LINE ")[2:6, :]), (24, 32, 232, 16), (-4, 0), ink=YELLOW + BRIGHT),
        Layer("dots text -6", dotted(text(" ZX SPECTRUM 48 ")[1:5, :]), (24, 64, 232, 16), (-6, 0), ink=CYAN + BRIGHT),
        Layer("dots text v-4", dotted(text("A1")[:, :12].T.copy(), 4, 8), (120, 96, 32, 96), (0, -4), ink=GREEN + BRIGHT),
    ]
    # Thin moving lines (1-2 px, less than a block): a dashed box whose sides run round it, rules, a dotted column,
    # a dashed rule right under static text
    s["thin"] = [
        Layer("box top +1", stripes(2, 232, 8), (12, 8, 232, 2), (1, 0), ink=WHITE + BRIGHT),
        Layer("box bottom -1", stripes(2, 232, 8), (12, 38, 232, 2), (-1, 0), ink=WHITE + BRIGHT),
        Layer("box left v+1", stripes(28, 2, 8, True), (12, 10, 2, 28), (0, 1), ink=WHITE + BRIGHT),
        Layer("box right v-1", stripes(28, 2, 8, True), (242, 10, 2, 28), (0, -1), ink=WHITE + BRIGHT),
        Layer("box text bounce", np.vstack([text("TEXT BOUNCING IN A BOX"), np.zeros((3, 176), np.uint8)]),
              (20, 12, 176, 11), (0, 1), tiled=False, moves=lambda k: (0, 1, 2, 3, 3, 2, 1, 0)[k % 8], ink=YELLOW + BRIGHT),
        Layer("rule +2", stripes(1, 256, 6), (0, 60, 256, 1), (2, 0), ink=CYAN + BRIGHT),
        Layer("rule -3", stripes(2, 256, 10), (0, 80, 256, 2), (-3, 0), ink=YELLOW + BRIGHT),
        Layer("dotted v+2", stripes(96, 1, 4, True), (128, 100, 1, 80), (0, 2), ink=GREEN + BRIGHT),
        Layer("label", text("STATIC LABEL OVER A RULE"), (16, 112, 192, 8), (0, 0), ink=WHITE + BRIGHT),
        Layer("rule under +1", stripes(1, 256, 4), (0, 120, 256, 1), (1, 0), ink=WHITE + BRIGHT),
    ]
    # Flux next to motion: stretching text, cycling colours, blinking, marching dashes, a scroller below
    s["flux"] = [
        Flux("stretch text", (16, 32, 224, 16), stretch_text(LOREM[:28], YELLOW + BRIGHT, 40)),
        Flux("cycle text", (16, 64, 224, 8), cycle_text(LOREM[28:56], 41)),
        Flux("blink sprite", (120, 88, 16, 16), blink(sprite(42), WHITE + BRIGHT, 2)),
        Flux("marching frame", (8, 24, 240, 56), marching(240, 56, CYAN + BRIGHT)),
        Layer("scroller -8", text(LOREM * 2), (0, 128, 256, 8), (-8, 0), ink=WHITE + BRIGHT),
        Layer("dots -2", stripes(8, 256, 4) * stripes(8, 256, 4, True), (0, 144, 256, 16), (-2, 0), ink=GREEN + BRIGHT),
    ]
    # Motion with animation: the content changes shape every frame while it moves (spinning dots of a dot matrix
    # title, flickering water tiles, a spinning sprite); the truth shows frame k's or k + 1's shape at the place due
    s["animated"] = [
        Layer("anim dots -8", None, (0, 8, 256, 48), (-8, 0), ink=WHITE + BRIGHT,
              frames=animated_dots(text(" PRESS KEY ")[1:7, :], disc_phases())),
        Layer("anim dots -4", None, (0, 64, 256, 48), (-4, 0), ink=YELLOW + BRIGHT,
              frames=animated_dots(text(" FIRE ")[1:7, :], disc_phases()[::-1])),
        Layer("water -2", None, (0, 120, 256, 32), (-2, 0), ink=CYAN, frames=animated_tiles(32, 256 + 32, 16, 60)),
        Layer("spinning +3,+1", None, (0, 0, 256, 192), (3, 1), p0=(20, 100), ink=RED + BRIGHT, tiled=False,
              frames=[lone(np.rot90(sprite(61), q)) for q in range(4)]),
    ]
    return s


def render(elements, t, rounding="floor", velocities=None, shapes=0):
    """The picture and the element id per pixel (0: the plain screen, -1: the border) at time t in source frames;
    shapes: the animated layers show frame k's shape (0) or k + 1's (1), all or per element ({e: 0 or 1})"""
    img = np.zeros((H, W, 3), np.uint8)
    img[:] = PALETTE[BLUE]
    img[SY:SY + SH, SX:SX + SW] = PALETTE[BLACK]
    ids = np.full((H, W), -1, np.int16)
    ids[SY:SY + SH, SX:SX + SW] = 0
    for e, element in enumerate(elements):
        if isinstance(element, Layer):
            shape = shapes.get(e, 0) if isinstance(shapes, dict) else shapes
            element.draw(img, ids, e + 1, t, rounding, (velocities or {}).get(e), shape)
        else:
            shape = shapes.get(e, 0) if isinstance(shapes, dict) else shapes
            element.draw(img, ids, e + 1, t, rounding, (velocities or {}).get(e), shape)
    return img, ids


def write(elements, path):
    with open(path, "wb") as f:
        for k in range(FRAMES):
            img, _ = render(elements, k)
            rgba = np.concatenate([img, np.full((H, W, 1), 255, np.uint8)], 2)
            f.write(rgba.tobytes())


if __name__ == "__main__":
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    all_scenes = scenes()
    for name in (sys.argv[2].split(",") if len(sys.argv) > 2 else all_scenes):
        write(all_scenes[name], os.path.join(out, name + ".rgba"))
        print(name)
