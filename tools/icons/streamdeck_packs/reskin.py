"""Re-skinning the MagicWB drawings into other pixel styles (NewIcons, GlowIcons, Workbench 1.3).

The MagicWB generator draws every glyph with semantic palette keys ('g' backdrop, 'k' outline / black detail, 'w'
white, 'l' light, 'd' dark, 'b' blue accent, 'e' warm accent) through `streamdeck_pixel.Canvas`. A `RampCanvas` keeps
those keys and, for each solid shape, the position of every pixel on a light-to-shade ramp; a `Skin` turns keys and
ramp positions into colours. So every pack shows the same 148 drawings, each in its own palette and shading.
"""

import json
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
sys.path.insert(0, HERE)

import magicwb  # noqa: E402
import streamdeck_pixel as sp  # noqa: E402

ROOT_DIR = magicwb.ROOT_DIR
N = magicwb.N
KEYS = 'gkwldbe'


def ramp_index(mask, steps, light=0.9, edge=0.3):
    """Ramp position (0 = darkest .. steps-1 = lightest) inside `mask`: lit from the top left, a light top-left rim
    and a dark bottom-right rim; -1 outside."""
    ys, xs = np.nonzero(mask)
    out = np.full(mask.shape, -1, int)
    if not len(xs):
        return out
    x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    w, h = max(1, x1 - x0), max(1, y1 - y0)
    yy, xx = np.mgrid[0:mask.shape[0], 0:mask.shape[1]]
    v = 1.0 - ((xx - x0) / w + (yy - y0) / h) / 2.0 * light
    pad = np.pad(mask, 1)
    up, left = ~pad[:-2, 1:-1], ~pad[1:-1, :-2]
    down, right = ~pad[2:, 1:-1], ~pad[1:-1, 2:]
    v = v + edge * (up | left) - edge * (down | right)
    idx = np.clip(np.round(v * (steps - 1)), 0, steps - 1).astype(int)
    out[mask] = idx[mask]
    return out


class RampCanvas(sp.Canvas):
    """A Canvas that also records, per pixel, the ramp position its last solid shape gave it (`v`) and the key that
    shape painted (`vk`): a pixel later overwritten by a detail (px, line, a direct `c[mask] = key`) no longer
    matches `vk` and is drawn in the key's flat colour."""

    def __init__(self, skin, on):
        super().__init__(N, 'g', skin)
        self.skin = skin
        self.on = on
        self.v = np.full((N, N), -1, int)
        self.vk = np.full((N, N), '', dtype='<U1')
        self.shapes = np.zeros((N, N), bool)  # everything a solid painted (the silhouette, for shadows and glows)

    def solid(self, mask, colour, bevel=True, outline=True, dither=None):
        n = self.n
        if outline and self.skin.outline:
            pad = np.pad(mask, 1)
            ring = np.zeros_like(mask)
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ring |= pad[1 + dy:1 + dy + n, 1 + dx:1 + dx + n]
            ring &= ~mask
            self.c[ring] = 'k'
            self.vk[ring] = ''
            self.shapes |= ring
        self.c[mask] = colour
        self.shapes |= mask
        if bevel:
            self.v[mask] = ramp_index(mask, self.skin.steps, self.skin.light, self.skin.edge)[mask]
            self.vk[mask] = colour
        else:
            self.vk[mask] = ''

    def rgb(self):
        sk = self.skin
        img = np.zeros((N, N, 3), float)
        for key in KEYS:
            sel = self.c == key
            img[sel] = sk.flat(key, self.on)
            ramped = sel & (self.vk == key) & (self.v >= 0)
            if ramped.any():
                ramp = np.array(sk.ramp(key, self.on), float)
                img[ramped] = ramp[self.v[ramped]]
        return img


class Skin:
    """A pixel style: `steps` ramp length, `light` / `edge` of the ramp, `outline` (draw MagicWB's outline rings),
    `ramps` {key: [darkest .. lightest]}, `flats` {key: colour}, `bg` / `bg_on` backdrops."""
    steps = 5
    light = 0.9
    edge = 0.3
    outline = True
    dither = False
    ramps = {}
    flats = {}
    bg = (128, 132, 144)
    bg_on = (96, 112, 150)

    def ramp(self, key, on):
        return self.ramps[key]

    def flat(self, key, on):
        if key == 'g':
            return self.bg_on if on else self.bg
        return self.flats[key]

    # Canvas style hooks (unused: the shading is the ramp)
    @staticmethod
    def hi(c):
        return c

    @staticmethod
    def lo(c):
        return c

    def finish(self, img, canvas):
        return img


def glyph_ids():
    with open(os.path.join(ROOT_DIR, 'data', 'streamdeck', 'icons', 'glyphs.json')) as f:
        return [g['id'] for g in json.load(f)['glyphs']]


def draw(skin, gid):
    c = RampCanvas(skin, gid.endswith('.on'))
    magicwb.DRAW[gid](c)
    return skin.finish(c.rgb(), c)


def write_pack(name, skin, manifest, image_of=None):
    pack_dir = os.path.join(ROOT_DIR, 'data', 'streamdeck', 'icons', 'packs', name)
    src = os.path.join(pack_dir, 'src')
    os.makedirs(src, exist_ok=True)
    for f in os.listdir(src):
        os.remove(os.path.join(src, f))
    ids = glyph_ids()
    for gid in ids:
        img = draw(skin, gid)
        im = image_of(img) if image_of else Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), 'RGB')
        im.save(os.path.join(src, gid + '.png'), optimize=True)
    base = {'format': 'streamdeck-icon-pack', 'version': 1, 'name': name, 'author': 'unreal-ng',
            'sizes': [72, 80, 96, 112, 120], 'fallback': 'magicwb',
            'generated_by': 'tools/icons/streamdeck_packs/%s.py' % name}
    base.update(manifest)
    with open(os.path.join(pack_dir, 'pack.json'), 'w', newline='\n') as f:
        json.dump(base, f, indent=1)
        f.write('\n')
    print('%s: %d glyphs -> %s' % (name, len(ids), os.path.relpath(pack_dir, ROOT_DIR)))


LICENCE = ('Original drawings made for unreal-ng (the MagicWB pack\'s drawings re-skinned), the licence of unreal-ng; '
           'the palette and shading technique of the style are imitated, no original artwork is used')
