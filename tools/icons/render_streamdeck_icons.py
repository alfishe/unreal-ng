#!/usr/bin/env python3
"""Check and render the Stream Deck icon packs; draw their contact sheets.

    python3 tools/icons/render_streamdeck_icons.py                  # every pack: check, render, sheets
    python3 tools/icons/render_streamdeck_icons.py --packs magicwb --sizes 112 --out DIR
    python3 tools/icons/render_streamdeck_icons.py --validate       # only check the packs against the glyph list
    python3 tools/icons/render_streamdeck_icons.py --check          # render twice, compare the bytes

Input: data/streamdeck/icons/glyphs.json (tools/icons/streamdeck_glyphs.py) and every
data/streamdeck/icons/packs/<pack>/pack.json with its sources in <pack>/src/<glyph id>.<svg|png>.

pack.json "source":
    svg         vector, rasterised at the key size (cairosvg)
    png-pixel   pixel art at "native" [w, h]; "pixel_aspect" [ax, ay] (Workbench 1.3: [1, 2], a source pixel is
                drawn twice as tall as wide); scaled by the largest whole factor that fits with nearest neighbour,
                centred on "background"
    png-smooth  a large RGBA or RGB picture (256 px or more), scaled down smoothly (Lanczos), centred on "background"
A glyph missing in a pack is taken from its "fallback" pack (then that pack's fallback); the sheet frames it.

Output (default scratch/streamdeck-icons/): <pack>/<size>/<glyph id>.png, sheets/<pack>.png, SHA256SUMS.
PNGs carry no time stamps or text chunks: the same sources give the same bytes. Requires Pillow (+ cairosvg for svg).
"""

import argparse
import hashlib
import io
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
ICONS_DIR = os.path.join(ROOT_DIR, 'data', 'streamdeck', 'icons')
SOURCES = {'svg': '.svg', 'png-pixel': '.png', 'png-smooth': '.png'}
FONTS = ['/System/Library/Fonts/Helvetica.ttc', '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
         'C:/Windows/Fonts/arial.ttf']


def hexrgb(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def load_glyphs():
    with open(os.path.join(ICONS_DIR, 'glyphs.json')) as f:
        return json.load(f)


def load_pack(name):
    root = os.path.join(ICONS_DIR, 'packs', name)
    with open(os.path.join(root, 'pack.json')) as f:
        m = json.load(f)
    m['root'] = root
    return m


def all_packs():
    d = os.path.join(ICONS_DIR, 'packs')
    return sorted(n for n in os.listdir(d) if os.path.exists(os.path.join(d, n, 'pack.json')))


def source_path(pack, gid):
    return os.path.join(pack['root'], 'src', gid + SOURCES[pack['source']])


def resolve(pack, gid, packs):
    """(pack that has the glyph, its file) following the fallback chain, or (None, None)."""
    seen = set()
    p = pack
    while p and p['name'] not in seen:
        seen.add(p['name'])
        f = source_path(p, gid)
        if os.path.exists(f):
            return p, f
        fb = p.get('fallback')
        p = packs.get(fb) if fb else None
        if fb and p is None and fb in all_packs():
            p = packs[fb] = load_pack(fb)
    return None, None


def render_file(pack, path, size):
    bg = hexrgb(pack.get('background', '#000000'))
    out = Image.new('RGB', (size, size), bg)
    kind = pack['source']
    if kind == 'svg':
        import cairosvg

        with open(path, 'rb') as f:
            png = cairosvg.svg2png(bytestring=f.read(), output_width=size, output_height=size)
        im = Image.open(io.BytesIO(png)).convert('RGBA')
        out.paste(im, (0, 0), im)
        return out
    im = Image.open(path).convert('RGBA')
    if kind == 'png-pixel':
        ax, ay = pack.get('pixel_aspect', [1, 1])
        k = max(1, min(size // (im.width * ax), size // (im.height * ay)))
        im = im.resize((im.width * ax * k, im.height * ay * k), Image.NEAREST)
    else:
        s = min(size / im.width, size / im.height)
        im = im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))), Image.LANCZOS)
    out.paste(im, ((size - im.width) // 2, (size - im.height) // 2), im)
    return out


def png_bytes(im):
    b = io.BytesIO()
    im.save(b, 'PNG', optimize=True)
    return b.getvalue()


def validate(pack, ids):
    """Problems of one pack: missing glyphs (served by the fallback), files for unknown ids, wrong sizes."""
    problems = []
    src = os.path.join(pack['root'], 'src')
    have = {os.path.splitext(f)[0] for f in os.listdir(src)} if os.path.isdir(src) else set()
    missing = [g for g in ids if g not in have]
    extra = sorted(have - set(ids))
    if pack['source'] not in SOURCES:
        problems.append('unknown source "%s"' % pack['source'])
    if pack['source'] == 'png-pixel':
        w, h = pack['native']
        for g in sorted(have & set(ids)):
            with Image.open(source_path(pack, g)) as im:
                if im.size != (w, h):
                    problems.append('%s is %dx%d, native is %dx%d' % (g, im.width, im.height, w, h))
    if extra:
        problems.append('not in the glyph list: %s' % ', '.join(extra))
    return missing, problems


def font(n):
    for f in FONTS:
        if os.path.exists(f):
            return ImageFont.truetype(f, n)
    return ImageFont.load_default()


def contact_sheet(pack, glyphs, faces, served_by):
    """Every glyph at 112 px (the + XL key) with its 72 px face beside it and its id, grouped; fallbacks framed."""
    cols, gap, lab = 6, 14, 18
    cell_w, cell_h = 112 + 8 + 72, 112 + lab
    rows = []
    for grp in glyphs['groups']:
        ids = [g['id'] for g in glyphs['glyphs'] if g['group'] == grp['id']]
        rows.append(('head', grp['title']))
        rows += [('icons', ids[i:i + cols]) for i in range(0, len(ids), cols)]
    top, headh = 70, 30
    w = gap + cols * (cell_w + gap)
    h = top + sum(headh if k == 'head' else cell_h + gap for k, _ in rows) + gap
    sheet = Image.new('RGB', (w, h), (44, 45, 50))
    d = ImageDraw.Draw(sheet)
    d.text((gap, 16), '%s (%s): %d glyphs, 112 px and 72 px keys' % (pack['title'], pack['name'],
                                                                     len(glyphs['glyphs'])),
           fill=(240, 240, 240), font=font(28))
    small, head = font(12), font(17)
    y = top
    for kind, val in rows:
        if kind == 'head':
            d.text((gap, y + 6), val, fill=(210, 210, 210), font=head)
            y += headh
            continue
        for i, gid in enumerate(val):
            x = gap + i * (cell_w + gap)
            sheet.paste(faces[gid][112], (x, y))
            sheet.paste(faces[gid][72], (x + 120, y + 40))
            if served_by[gid] != pack['name']:
                d.rectangle([x - 2, y - 2, x + 113, y + 113], outline=(230, 80, 60), width=2)
            d.text((x, y + 114), gid, fill=(200, 200, 200), font=small)
        y += cell_h + gap
    return sheet


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', default=os.path.join(ROOT_DIR, 'scratch', 'streamdeck-icons'))
    ap.add_argument('--packs', default='')
    ap.add_argument('--sizes', default='')
    ap.add_argument('--no-sheets', action='store_true')
    ap.add_argument('--validate', action='store_true')
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    glyphs = load_glyphs()
    ids = [g['id'] for g in glyphs['glyphs']]
    names = a.packs.split(',') if a.packs else all_packs()
    packs = {n: load_pack(n) for n in names}
    bad = False
    for n, p in packs.items():
        missing, problems = validate(p, ids)
        state = 'complete' if not missing else '%d missing (%s)' % (len(missing), 'fallback ' + str(p.get('fallback')))
        print('%s: %s, %d glyphs, %s' % (n, p['source'], len(ids) - len(missing), state))
        for pr in problems:
            print('  ' + pr)
        bad |= bool(problems)
    if a.validate:
        return 1 if bad else 0
    sums = []
    for n, p in packs.items():
        sizes = [int(s) for s in a.sizes.split(',')] if a.sizes else p['sizes']
        faces, served_by = {}, {}
        for gid in ids:
            src_pack, path = resolve(p, gid, packs)
            served_by[gid] = src_pack['name'] if src_pack else None
            faces[gid] = {}
            for size in sorted(set(sizes) | {72, 112}):
                im = render_file(src_pack, path, size) if path else Image.new('RGB', (size, size), (90, 20, 20))
                faces[gid][size] = im
                if size not in sizes:
                    continue
                b = png_bytes(im)
                if a.check:
                    if path and png_bytes(render_file(src_pack, path, size)) != b:
                        print('differs: %s %d %s' % (n, size, gid))
                        bad = True
                    continue
                d = os.path.join(a.out, n, str(size))
                os.makedirs(d, exist_ok=True)
                with open(os.path.join(d, gid + '.png'), 'wb') as f:
                    f.write(b)
                sums.append('%s  %s/%d/%s.png' % (hashlib.sha256(b).hexdigest(), n, size, gid))
        if a.check:
            continue
        if not a.no_sheets:
            sd = os.path.join(a.out, 'sheets')
            os.makedirs(sd, exist_ok=True)
            contact_sheet(p, glyphs, faces, served_by).save(os.path.join(sd, n + '.png'), optimize=True)
            print('  sheet: %s' % os.path.join(sd, n + '.png'))
    if a.check:
        print('check: %s' % ('differences found' if bad else 'ok, deterministic'))
        return 1 if bad else 0
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, 'SHA256SUMS'), 'w', newline='\n') as f:
        f.write('\n'.join(sums) + '\n')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
