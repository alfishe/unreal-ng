#!/usr/bin/env python3
"""The motion gallery: <work>/videos.html over <work>/video/<clip>/<key>.webm (render_clips.py), an A | B wipe or side
by side, the two videos synchronised.

usage: videos.py [--work DIR]"""
import glob
import json
import os

from common import parser
from presets import INFO, ORDER


def main():
    args = parser(__doc__).parse_args()
    os.chdir(args.work)
    clips = sorted(d for d in os.listdir('video') if os.path.isdir(f'video/{d}'))
    present = {os.path.basename(f)[:-5] for f in glob.glob('video/*/*.webm')}
    data = {'clips': clips,
            'shaders': [{'key': k, 'title': INFO[k][0], 'note': INFO[k][3]} for k in ORDER if k in present]}

    html = r'''<!doctype html><html><head><meta charset="utf-8"><title>CRT shaders in motion</title>
    <style>
    body{background:#111;color:#ddd;font:13px -apple-system,sans-serif;margin:0}
    #bar{position:sticky;top:0;background:#1c1c1c;padding:6px 10px;z-index:9;border-bottom:1px solid #333}
    select,button,input{background:#2a2a2a;color:#ddd;border:1px solid #444;border-radius:3px;padding:2px 6px}
    #stage{position:relative;margin:10px;display:inline-block}
    #stage video{display:block}
    #wipe{position:absolute;top:0;left:0;overflow:hidden;border-right:2px solid #4af}
    .side{display:flex;gap:6px;margin:10px}
    .k{color:#4af}
    </style></head><body>
    <div id="bar">
     Clip: <select id="clip"></select>
     Mode: <select id="mode"><option value="wipe">A | B wipe</option><option value="side">side by side</option><option value="one">A only</option></select>
     A: <select id="a"></select> B: <select id="b"></select>
     Size: <select id="size"><option value="dev">1:1 device pixels</option><option value="fit">fit</option></select>
     <button id="play">pause</button>
     Speed: <select id="speed"><option>1</option><option>0.5</option><option>0.25</option></select>
     Brightness: <input id="gain" type="range" min="1" max="4" step="0.1" value="1"> <span id="gainv">1.0x</span>
     <span id="info"></span><br>
     <small>keys: <span class="k">←/→</span> shader A, <span class="k">⇧←/⇧→</span> shader B, <span class="k">↑/↓</span> clip, <span class="k">space</span> play/pause, <span class="k">,/.</span> frame step (paused)</small>
    </div>
    <div id="main"></div>
    <script>
    const D = __DATA__;
    const $ = id => document.getElementById(id);
    let ci = 0, ai = Math.max(0, D.shaders.findIndex(s => s.key == 'crt-lottes')), bi = Math.max(0, D.shaders.findIndex(s => s.key == 'ours-none'));
    let playing = true, vids = [];
    D.clips.forEach((c, i) => $('clip').add(new Option(c, i)));
    for (const sel of ['a', 'b']) D.shaders.forEach((s, i) => $(sel).add(new Option(s.title, i)));
    $('clip').onchange = e => { ci = +e.target.value; build(); };
    $('a').onchange = e => { ai = +e.target.value; build(); };
    $('b').onchange = e => { bi = +e.target.value; build(); };
    for (const id of ['mode', 'size']) $(id).onchange = build;
    $('speed').onchange = () => vids.forEach(v => v.playbackRate = +$('speed').value);
    $('gain').oninput = () => { const g = +$('gain').value; $('gainv').textContent = g.toFixed(1) + 'x'; $('main').style.filter = g == 1 ? '' : `brightness(${g})`; };
    $('play').onclick = toggle;
    function video(s) {
      const v = document.createElement('video');
      v.src = `video/${D.clips[ci]}/${s.key}.webm`; v.loop = true; v.muted = true; v.playsInline = true; v.preload = 'auto';
      if ($('size').value == 'dev') v.style.width = (1408 / devicePixelRatio) + 'px';
      else v.style.maxWidth = $('mode').value == 'side' ? 'calc(50vw - 20px)' : 'calc(100vw - 30px)', v.style.maxHeight = 'calc(100vh - 110px)';
      v.playbackRate = +$('speed').value;
      return v;
    }
    function sync() {
      // B follows A: a drift over half a frame is corrected
      if (vids.length < 2) return;
      const [a, b] = vids;
      if (Math.abs(b.currentTime - a.currentTime) > 0.01) b.currentTime = a.currentTime;
      if (a.requestVideoFrameCallback) a.requestVideoFrameCallback(sync); else requestAnimationFrame(sync);
    }
    function build() {
      $('clip').value = ci; $('a').value = ai; $('b').value = bi;
      const A = D.shaders[ai], B = D.shaders[bi], mode = $('mode').value, main = $('main');
      main.innerHTML = '';
      $('info').innerHTML = mode == 'one' ? `<b>${A.title}</b> ${A.note}` : `left <b>${A.title}</b> | right <b>${B.title}</b>`;
      if (mode == 'one') { vids = [video(A)]; const st = document.createElement('div'); st.id = 'stage'; st.appendChild(vids[0]); main.appendChild(st); }
      else if (mode == 'side') { vids = [video(A), video(B)]; const d = document.createElement('div'); d.className = 'side'; vids.forEach(v => d.appendChild(v)); main.appendChild(d); }
      else {
        const st = document.createElement('div'); st.id = 'stage';
        const b = video(B), a = video(A), w = document.createElement('div'); w.id = 'wipe'; w.style.width = '50%';
        w.appendChild(a); st.appendChild(b); st.appendChild(w); main.appendChild(st);
        st.onmousemove = e => { const r = st.getBoundingClientRect(); w.style.width = (e.clientX - r.left) + 'px'; };
        vids = [a, b];
      }
      Promise.all(vids.map(v => new Promise(r => v.oncanplay = r))).then(() => { if (playing) vids.forEach(v => v.play()); sync(); });
    }
    function toggle() { playing = !playing; $('play').textContent = playing ? 'pause' : 'play'; vids.forEach(v => playing ? v.play() : v.pause()); }
    document.onkeydown = e => {
      if (e.target.tagName == 'SELECT') return;
      const n = D.shaders.length;
      if (e.key == 'ArrowRight' && e.shiftKey) bi = (bi + 1) % n;
      else if (e.key == 'ArrowLeft' && e.shiftKey) bi = (bi + n - 1) % n;
      else if (e.key == 'ArrowRight') ai = (ai + 1) % n;
      else if (e.key == 'ArrowLeft') ai = (ai + n - 1) % n;
      else if (e.key == 'ArrowDown') ci = (ci + 1) % D.clips.length;
      else if (e.key == 'ArrowUp') ci = (ci + D.clips.length - 1) % D.clips.length;
      else if (e.key == ' ') { e.preventDefault(); toggle(); return; }
      else if ((e.key == '.' || e.key == ',') && !playing) { const t = vids[0].currentTime + (e.key == '.' ? 0.02 : -0.02); vids.forEach(v => v.currentTime = Math.max(0, t)); return; }
      else return;
      e.preventDefault(); const t = vids.length ? vids[0].currentTime : 0; build(); vids.forEach(v => v.currentTime = t);
    };
    build();
    </script></body></html>'''
    open('videos.html', 'w').write(html.replace('__DATA__', json.dumps(data)))
    print(len(clips), 'clips', len(data['shaders']), 'shaders')


if __name__ == '__main__':
    main()
