#!/usr/bin/env python3
"""The still gallery: <work>/index.html over <work>/out/<frame>/<key>.png (render.py), one shader, an A | B wipe or a
matrix of 1:1 crops (<work>/crops). Open it from the work directory's web server or as a file.

usage: gallery.py [--work DIR]"""
import glob
import json
import os

from PIL import Image

from common import parser
from presets import INFO, ORDER

CROP = (480, 360)  # 1:1 piece around the frame's centre
CENTRES = {'01-kbd48': (560, 470), '09-text': (704, 500)}  # where the detail is; elsewhere the middle


def main():
    args = parser(__doc__).parse_args()
    os.chdir(args.work)
    frames = sorted(d for d in os.listdir('out') if os.path.isdir(f'out/{d}'))
    data = {'frames': [], 'shaders': []}
    os.makedirs('crops', exist_ok=True)
    present = set()
    for frame in frames:
        centre = CENTRES.get(frame, (704, 576))
        box = (centre[0] - CROP[0] // 2, centre[1] - CROP[1] // 2, centre[0] + CROP[0] // 2, centre[1] + CROP[1] // 2)
        for f in glob.glob(f'out/{frame}/*.png'):
            key = os.path.basename(f)[:-4]
            if key not in INFO:
                continue
            present.add(key)
            Image.open(f).convert('RGB').crop(box).save(f'crops/{frame}--{key}.png')
        data['frames'].append({'key': frame, 'src': f'src/{frame}.png'})
    data['shaders'] = [{'key': k, 'title': INFO[k][0], 'license': INFO[k][1], 'passes': INFO[k][2], 'note': INFO[k][3]}
                       for k in ORDER if k in present]

    html = r'''<!doctype html><html><head><meta charset="utf-8"><title>CRT shader gallery</title>
    <style>
    body{background:#111;color:#ddd;font:13px -apple-system,sans-serif;margin:0}
    #bar{position:sticky;top:0;background:#1c1c1c;padding:6px 10px;z-index:9;border-bottom:1px solid #333}
    #bar select,#bar button,#bar input{background:#2a2a2a;color:#ddd;border:1px solid #444;border-radius:3px;padding:2px 6px}
    #frames img{height:48px;margin:2px;border:2px solid transparent;cursor:pointer;image-rendering:pixelated}
    #frames img.on{border-color:#4af}
    #view{position:relative;margin:10px;display:inline-block}
    #view img{display:block}
    .px img{image-rendering:pixelated}
    #wipe{position:absolute;top:0;left:0;overflow:hidden;border-right:2px solid #4af}
    #grid{display:flex;flex-wrap:wrap;gap:8px;margin:10px}
    #grid figure{margin:0;cursor:pointer}
    #grid figcaption{font-size:12px;padding:2px 0}
    #info{color:#aaa;margin-left:10px}
    .k{color:#4af}
    </style></head><body>
    <div id="bar">
     <span id="frames"></span><br>
     Mode: <select id="mode"><option value="single">one shader</option><option value="wipe">A | B wipe</option><option value="grid">matrix (1:1 crops)</option></select>
     A: <select id="a"></select> B: <select id="b"></select>
     Size: <select id="size"><option value="dev">1:1 device pixels</option><option value="fit">fit window</option><option value="css">1:1 CSS pixels</option></select>
     Brightness: <input id="gain" type="range" min="1" max="4" step="0.1" value="1"> <span id="gainv">1.0x</span>
     <span id="info"></span><br>
     <small>keys: <span class="k">←/→</span> shader A, <span class="k">↑/↓</span> frame, <span class="k">space</span> hold = show B (in one-shader mode), <span class="k">g</span> matrix, <span class="k">w</span> wipe, <span class="k">s</span> one</small>
    </div>
    <div id="main"></div>
    <script>
    const D = __DATA__;
    let fi = 0, ai = D.shaders.findIndex(s => s.key == 'crt-lottes'), bi = D.shaders.findIndex(s => s.key == 'ours-none'), hold = false;
    const $ = id => document.getElementById(id);
    D.frames.forEach((f, i) => { const im = new Image(); im.src = f.src; im.title = f.key; im.onclick = () => { fi = i; draw(); }; $('frames').appendChild(im); });
    for (const sel of ['a', 'b']) D.shaders.forEach((s, i) => $(sel).add(new Option(s.title, i)));
    $('a').onchange = e => { ai = +e.target.value; draw(); };
    $('b').onchange = e => { bi = +e.target.value; draw(); };
    for (const id of ['mode', 'size']) $(id).onchange = draw;
    $('gain').oninput = draw;
    const path = (s) => `out/${D.frames[fi].key}/${s.key}.png`;
    function sized(img) {
      const m = $('size').value;
      if (m == 'dev') img.style.width = (1408 / devicePixelRatio) + 'px';
      else if (m == 'fit') img.style.maxWidth = 'calc(100vw - 30px)', img.style.maxHeight = 'calc(100vh - 140px)';
      return img;
    }
    function draw() {
      $('a').value = ai; $('b').value = bi;
      [...$('frames').children].forEach((im, i) => im.className = i == fi ? 'on' : '');
      const g = +$('gain').value; $('gainv').textContent = g.toFixed(1) + 'x';
      const main = $('main'); main.innerHTML = ''; main.style.filter = g == 1 ? '' : `brightness(${g})`;
      const A = D.shaders[ai], B = D.shaders[bi], mode = $('mode').value;
      $('info').innerHTML = `<b>${(hold ? B : A).title}</b> — ${(hold ? B : A).license}, ${(hold ? B : A).passes} pass(es). ${(hold ? B : A).note}`;
      if (mode == 'grid') {
        const grid = document.createElement('div'); grid.id = 'grid'; grid.className = 'px';
        D.shaders.forEach((s, i) => {
          const fig = document.createElement('figure');
          const im = new Image(); im.src = `crops/${D.frames[fi].key}--${s.key}.png`;
          if ($('size').value == 'dev') im.style.width = (480 / devicePixelRatio) + 'px';
          fig.appendChild(im); const c = document.createElement('figcaption'); c.textContent = s.title; fig.appendChild(c);
          fig.onclick = () => { ai = i; $('mode').value = 'single'; draw(); };
          grid.appendChild(fig);
        });
        main.appendChild(grid); return;
      }
      const view = document.createElement('div'); view.id = 'view'; view.className = 'px';
      const base = sized(new Image()); base.src = path(hold ? B : A); view.appendChild(base);
      if (mode == 'wipe') {
        base.src = path(B);
        const w = document.createElement('div'); w.id = 'wipe'; const top = sized(new Image()); top.src = path(A);
        w.appendChild(top); view.appendChild(w); w.style.width = '50%';
        view.onmousemove = e => { const r = view.getBoundingClientRect(); w.style.width = (e.clientX - r.left) + 'px'; };
        $('info').innerHTML = `left <b>${A.title}</b> | right <b>${B.title}</b>`;
      }
      main.appendChild(view);
    }
    document.onkeydown = e => {
      if (e.target.tagName == 'SELECT') return;
      if (e.key == 'ArrowRight') ai = (ai + 1) % D.shaders.length;
      else if (e.key == 'ArrowLeft') ai = (ai + D.shaders.length - 1) % D.shaders.length;
      else if (e.key == 'ArrowDown') fi = (fi + 1) % D.frames.length;
      else if (e.key == 'ArrowUp') fi = (fi + D.frames.length - 1) % D.frames.length;
      else if (e.key == ' ') { if (hold) return; hold = true; }
      else if (e.key == 'g') $('mode').value = 'grid';
      else if (e.key == 'w') $('mode').value = 'wipe';
      else if (e.key == 's') $('mode').value = 'single';
      else return;
      e.preventDefault(); draw();
    };
    document.onkeyup = e => { if (e.key == ' ') { hold = false; draw(); } };
    draw();
    </script></body></html>'''
    open('index.html', 'w').write(html.replace('__DATA__', json.dumps(data)))
    print(len(data['frames']), 'frames', len(data['shaders']), 'shaders')


if __name__ == '__main__':
    main()
