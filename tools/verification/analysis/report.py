#!/usr/bin/env python3
"""The evidence of a corpus run, game by game, as HTML: report.py --work scratch/corpus [--seconds 90]

Per game (<work>/<key>/report.html): screens through the run, every font / sprite / tile / picture the analysis
claimed as the gallery drew it (address, geometry, the routines that draw it), the music: the claimed data and its
player, the AY writes as PSG, a WAV of them (psg2wav.py), a piano roll and how close the tone periods sit to an
equal-tempered scale; the logic: what each routine was seen doing (prints with a font, draws sprites, plays music,
copies the screen, reads a level map) and the signatures matched. <work>/index.html is the table of all games."""

import argparse
import glob
import html
import json
import math
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import psg2wav  # noqa: E402
import scr2png  # noqa: E402

CLOCKS = {'48K': 1773400.0, '128K': 1773400.0, 'PENTAGON': 1750000.0}
GRAPHIC_KINDS = ('font', 'sprite', 'tile', 'screen', 'level_map')
ROLES = {'font': 'prints with the font', 'sprite': 'draws sprites', 'tile': 'draws tiles', 'text': 'prints text',
         'music': 'plays music', 'screen': 'copies a picture / back buffer', 'level_map': 'reads a level map',
         'records': 'walks records', 'handler_table': 'dispatches through a handler table'}


def regions_of(path):
    if not os.path.exists(path):
        return []
    r = json.load(open(path))
    return r if isinstance(r, list) else r.get('regions', [])


def addr(text):
    return int(str(text).lstrip('$#'), 16)


def screens_sheet(game_dir, out):
    files = sorted(glob.glob(os.path.join(game_dir, 'screens', '*.scr')))
    if not files:
        return None
    pick = files[::max(1, len(files) // 12)][:12]
    shots = [Image.frombytes('RGB', (256, 192), bytes(c for row in scr2png.pixels(open(f, 'rb').read())
                                                       for px in row for c in px)) for f in pick]
    sheet = Image.new('RGB', (4 * 260, ((len(shots) + 3) // 4) * 196), (40, 40, 40))
    for i, shot in enumerate(shots):
        sheet.paste(shot, ((i % 4) * 260, (i // 4) * 196))
    sheet.save(out)
    return [os.path.basename(f)[:-4] for f in pick]


def music_evidence(game_dir, model, seconds):
    """PSG -> WAV, piano roll, the scale fit: (frames, sounding frames, scale fit, files)"""
    psg = os.path.join(game_dir, 'music.psg')
    if not os.path.exists(psg):
        return None
    frames = psg2wav.read_psg(psg)
    clock = CLOCKS.get(model, 1773400.0)
    wav = os.path.join(game_dir, 'music.wav')
    pcm = psg2wav.render(frames[:int(seconds * 50)], clock, 44100)
    import wave
    with wave.open(wav, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes((np.clip(pcm, -1, 1) * 32000).astype('<i2').tobytes())
    # The registers frame by frame
    regs = [0] * 16
    regs[7] = 0xFF
    notes = []  # (frame, channel, midi note as a float)
    for f, written in enumerate(frames):
        for r, v in written.items():
            regs[r] = v
        for ch in range(3):
            vol = regs[8 + ch]
            if (regs[7] >> ch) & 1 or (vol & 0x1F) == 0:
                continue
            period = (regs[2 * ch] | (regs[2 * ch + 1] << 8)) & 0xFFF
            if period < 8:
                continue
            freq = clock / (16 * period)
            if 30 <= freq <= 8000:
                notes.append((f, ch, 69 + 12 * math.log2(freq / 440.0)))
    sounding = len({f for f, _, _ in notes})
    fit = None
    if notes:
        # How tightly the notes sit on one semitone grid (whatever the tuning): 1 = all on it, ~0 = no grid
        phase = np.array([n for _, _, n in notes]) * 2 * np.pi
        fit = float(abs(np.exp(1j * phase).mean()))
    roll = os.path.join(game_dir, 'music-roll.png')
    width = min(len(frames), int(seconds * 50))
    img = Image.new('RGB', (max(1, width // 2), 88 * 3), (16, 16, 16))
    px = img.load()
    colors = [(255, 90, 90), (90, 255, 90), (110, 140, 255)]
    for f, ch, n in notes:
        if f >= width:
            continue
        y = int(round(108 - n)) * 3
        if 0 <= y < img.height - 2:
            for dy in range(2):
                px[f // 2, y + dy] = colors[ch]
    img.save(roll)
    return {'frames': len(frames), 'sounding': sounding, 'fit': fit, 'wav': 'music.wav', 'roll': 'music-roll.png',
            'seconds': min(seconds, len(frames) / 50)}


def describe(region):
    g = region.get('geometry') or {}
    parts = []
    if g.get('width'):
        parts.append(f"{g.get('width')}x{g.get('height')}")
    if g.get('frames', 1) > 1:
        parts.append(f"{g['frames']} frames")
    if g.get('mask'):
        parts.append('masked')
    return ', '.join(parts)


def game_report(game_dir, seconds):
    run = json.load(open(os.path.join(game_dir, 'run.json')))
    regions = regions_of(os.path.join(game_dir, 'regions-all.json'))
    gallery = json.load(open(os.path.join(game_dir, 'gallery.json'))) if os.path.exists(
        os.path.join(game_dir, 'gallery.json')) else []
    music_claims = regions_of(os.path.join(game_dir, 'music.json'))
    shots = screens_sheet(game_dir, os.path.join(game_dir, 'screens.png'))
    music = music_evidence(game_dir, run.get('model'), seconds)

    # The logic: what each routine was seen doing
    roles = {}
    for r in regions:
        kind = r.get('kind')
        if kind not in ROLES:
            continue
        for u in r.get('users') or []:
            roles.setdefault(u, {}).setdefault(kind, []).append(r.get('start'))
    signatures = sorted({(r.get('start'), r.get('label') or '', r.get('title') or '') for r in regions
                         if r.get('detector') == 'signatures' and r.get('kind') == 'code'})
    code = sum(r.get('size', 0) for r in regions if r.get('kind') == 'code' and r.get('detector') == 'code_map')
    counts = {}
    for r in gallery:
        counts[r.get('kind')] = counts.get(r.get('kind'), 0) + 1
    frames = {k: sum((r.get('geometry') or {}).get('frames', 1) for r in gallery if r.get('kind') == k)
              for k in counts}

    e = html.escape
    out = [f"<html><head><meta charset='utf-8'><title>{e(run['title'])}</title><style>"
           "body{font-family:-apple-system,sans-serif;background:#1b1b1b;color:#ddd;margin:20px}"
           "img{image-rendering:pixelated;border:1px solid #444;margin:4px;background:#000}"
           "td,th{padding:3px 8px;border-bottom:1px solid #333;vertical-align:top;text-align:left}"
           "table{border-collapse:collapse}.card{display:inline-block;vertical-align:top;margin:6px;max-width:520px}"
           "h2{border-bottom:1px solid #555}a{color:#8cf}</style></head><body>",
           f"<p><a href='../index.html'>all games</a></p><h1>{e(run['title'])} <small>({e(run['key'])}, "
           f"{e(run.get('model', ''))}, {e(run.get('kind', ''))})</small></h1>",
           f"<p>{e('; '.join(run.get('load', [])))}<br>load {run.get('load_frames')} frames, then "
           f"{run.get('frames')} frames of generic input; AY writes {run.get('ay_writes', 0)}</p>"]
    out.append("<h2>Screens during the run</h2>")
    out.append(f"<img src='screens.png'><br><small>frames {', '.join(shots or [])}</small>" if shots else
               "<p>no screens</p>")
    for kind, heading in (('font', 'Fonts'), ('sprite', 'Sprites'), ('tile', 'Tiles'),
                          ('screen', 'Pictures and back buffers'), ('level_map', 'Level maps')):
        items = [r for r in gallery if r.get('kind') == kind]
        out.append(f"<h2>{heading}: {len(items)}</h2>")
        for r in items:
            png = f"gallery/{kind}-{str(r.get('start')).lstrip('$#')}.png"
            exists = os.path.exists(os.path.join(game_dir, png))
            users = ', '.join(r.get('users') or [])
            out.append(f"<div class='card'><b>{e(str(r.get('start')))}-{e(str(r.get('end')))}</b> "
                       f"{e(describe(r))} <small>({e(r.get('detector', ''))}, conf {r.get('confidence')})</small><br>"
                       f"<small>{e(r.get('title') or '')}</small><br>"
                       + (f"<img src='{png}'><br>" if exists else "<i>not drawn (gallery limit)</i><br>")
                       + f"<small>drawn by {e(users)}</small></div>")
    out.append("<h2>Music</h2>")
    if music_claims:
        out.append("<table><tr><th>data</th><th>title</th><th>read by</th></tr>" + ''.join(
            f"<tr><td>{e(str(r.get('start')))}-{e(str(r.get('end')))}</td><td>{e(r.get('title') or '')}</td>"
            f"<td>{e(', '.join(r.get('users') or []))}</td></tr>" for r in music_claims) + "</table>")
    else:
        out.append("<p>no music data claimed</p>")
    if music:
        fit = f"{music['fit']:.2f}" if music['fit'] is not None else 'n/a'
        out.append(f"<p>AY writes: {music['frames']} frames ({music['frames'] / 50:.0f} s), a tone sounds in "
                   f"{music['sounding']} of them; tone periods on a semitone grid: {fit} (1 = every note on the "
                   f"scale, about 0 = no scale)</p><audio controls src='{music['wav']}'></audio> "
                   f"<a href='music.psg'>music.psg</a> <a href='{music['wav']}'>music.wav</a> (first "
                   f"{music['seconds']:.0f} s)<br><img src='{music['roll']}'><br><small>piano roll, 2 frames a pixel, "
                   f"A red, B green, C blue, MIDI 20 (bottom) to 108 (top)</small>")
    else:
        out.append("<p>no AY writes</p>")
    out.append(f"<h2>Logic: {len(roles)} routines with a role, {code} bytes of code</h2><table>"
               "<tr><th>routine</th><th>seen doing</th></tr>")
    for routine in sorted(roles, key=addr):
        what = '; '.join(f"{ROLES[k]} ({', '.join(sorted(set(v), key=addr)[:4])}"
                         f"{'...' if len(set(v)) > 4 else ''})" for k, v in sorted(roles[routine].items()))
        out.append(f"<tr><td>{e(routine)}</td><td>{e(what)}</td></tr>")
    out.append("</table>")
    if signatures:
        out.append("<h3>Known code matched</h3><table>" + ''.join(
            f"<tr><td>{e(str(s))}</td><td>{e(label)}</td><td>{e(title)}</td></tr>" for s, label, title in signatures)
                   + "</table>")
    out.append("</body></html>")
    open(os.path.join(game_dir, 'report.html'), 'w').write('\n'.join(out))
    return {'key': run['key'], 'title': run['title'], 'model': run.get('model'), 'counts': counts, 'frames': frames,
            'music_claims': len(music_claims), 'music': music, 'roles': len(roles), 'code': code,
            'signatures': len(signatures), 'error': run.get('error')}


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('--work', required=True)
    p.add_argument('--seconds', type=float, default=90)
    args = p.parse_args()
    rows = []
    for game_dir in sorted(glob.glob(os.path.join(args.work, '*', 'run.json'))):
        game_dir = os.path.dirname(game_dir)
        try:
            rows.append(game_report(game_dir, args.seconds))
        except Exception as ex:  # one broken game does not stop the table
            rows.append({'key': os.path.basename(game_dir), 'title': '?', 'error': repr(ex)})
    score = {}
    if os.path.exists(os.path.join(args.work, 'score.json')):
        score = {g['key']: g for g in json.load(open(os.path.join(args.work, 'score.json')))}
    e = html.escape
    out = ["<html><head><meta charset='utf-8'><title>Corpus evidence</title><style>"
           "body{font-family:-apple-system,sans-serif;background:#1b1b1b;color:#ddd;margin:20px}"
           "td,th{padding:3px 8px;border-bottom:1px solid #333;text-align:left}table{border-collapse:collapse}"
           "a{color:#8cf}.no{color:#f77}</style></head><body><h1>Corpus evidence</h1><table><tr><th>game</th>"
           "<th>model</th><th>fonts</th><th>sprites (frames)</th><th>tiles</th><th>pictures</th><th>screen cells "
           "a font explains</th><th>music data</th><th>AY s</th><th>scale fit</th><th>routines with a role</th>"
           "<th>signatures</th></tr>"]
    for r in rows:
        c, f = r.get('counts', {}), r.get('frames', {})
        m = r.get('music')
        sc = score.get(r['key'], {}).get('explained', {})
        cell = lambda v: f"<td>{v}</td>" if v else "<td class='no'>0</td>"  # noqa: E731
        out.append(f"<tr><td><a href='{e(r['key'])}/report.html'>{e(r['title'])}</a>"
                   + (f" <span class='no'>{e(r['error'])}</span>" if r.get('error') else '') + "</td>"
                   f"<td>{e(str(r.get('model')))}</td>" + cell(c.get('font', 0))
                   + cell(f"{c['sprite']} ({f['sprite']})" if c.get('sprite') else 0) + cell(c.get('tile', 0))
                   + cell(c.get('screen', 0)) + f"<td>{sc.get('font', '')}</td>" + cell(r.get('music_claims', 0))
                   + cell(f"{m['frames'] / 50:.0f}" if m else 0)
                   + cell(f"{m['fit']:.2f}" if m and m['fit'] is not None else 0) + cell(r.get('roles', 0))
                   + cell(r.get('signatures', 0)) + "</tr>")
    out.append("</table></body></html>")
    open(os.path.join(args.work, 'index.html'), 'w').write('\n'.join(out))
    print(f"{os.path.join(args.work, 'index.html')}: {len(rows)} games")


if __name__ == '__main__':
    sys.exit(main())
