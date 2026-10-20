#!/usr/bin/env python3
"""The demo video: <work>/demo/crt-shaders-demo.{webm,mp4}, every preset as an A | B wipe against the plain picture,
4 s each, the clips (render_clips.py) in turn, captions on top. ffmpeg with libvpx-vp9 and libx264.

usage: make_demo.py [--work DIR] [--font FILE]"""
import os
import subprocess

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from common import parser
from presets import INFO, MODELS, PRESETS

W, H, FPS, SEG = 1408, 1152, 50, 200
CLIPS = {'tape': 250, 'edge': 150, 'aleste': 150}
# The single-pass presets first (they have CPU models), then the GPU-only ones
ORDER = [k for k, _ in PRESETS if k in MODELS] + [k for k, _ in PRESETS if k not in MODELS]
FONT = '/System/Library/Fonts/HelveticaNeue.ttc'
big = mid = small = huge = None


def fonts(path):
    global big, mid, small, huge
    if not os.path.exists(path):
        raise SystemExit(f'no font {path}: pass --font <a .ttf / .ttc>')
    big, mid, small, huge = (ImageFont.truetype(path, size) for size in (44, 26, 22, 64))


def frames(path):
    """Decoded RGB frames of a webm, looped forever"""
    while True:
        p = subprocess.Popen(['ffmpeg', '-v', 'error', '-i', path, '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'],
                             stdout=subprocess.PIPE)
        while True:
            buf = p.stdout.read(W * H * 3)
            if len(buf) < W * H * 3:
                break
            yield np.frombuffer(buf, np.uint8).reshape(H, W, 3)
        p.wait()


def caption(img, lines, xy, anchor_right=False):
    d = ImageDraw.Draw(img, 'RGBA')
    sizes = [d.textbbox((0, 0), t, font=f) for t, f, _ in lines]
    w = max(b[2] for b in sizes) + 28
    h = sum(b[3] + 8 for b in sizes) + 16
    x = xy[0] - w if anchor_right else xy[0]
    d.rounded_rectangle((x, xy[1], x + w, xy[1] + h), 10, fill=(0, 0, 0, 170))
    y = xy[1] + 10
    for (t, f, colour), b in zip(lines, sizes):
        d.text((x + 14, y), t, font=f, fill=colour)
        y += b[3] + 8


def title_card(n):
    img = Image.new('RGB', (W, H), (12, 12, 14))
    d = ImageDraw.Draw(img)
    d.text((W // 2, H // 2 - 70), 'CRT shaders for unreal-ng', font=huge, fill='white', anchor='mm')
    d.text((W // 2, H // 2 + 10), f'{n} candidates, each against the plain picture (A | B wipe)', font=mid,
           fill=(190, 190, 190), anchor='mm')
    d.text((W // 2, H // 2 + 60), 'Rendered at 4x (1408 x 1152) - watch at 100%, a downscale shows moire, not the mask',
           font=small, fill=(140, 140, 140), anchor='mm')
    return np.asarray(img)


def main():
    p = parser(__doc__)
    p.add_argument('--font', default=FONT, help='caption font (default: macOS Helvetica Neue)')
    args = p.parse_args()
    fonts(args.font)
    os.chdir(args.work)
    os.makedirs('demo', exist_ok=True)
    out = subprocess.Popen(['ffmpeg', '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{W}x{H}',
                            '-r', str(FPS), '-i', '-', '-c:v', 'libvpx-vp9', '-pix_fmt', 'yuv444p', '-crf', '14',
                            '-b:v', '0', '-row-mt', '1', '-cpu-used', '4', 'demo/crt-shaders-demo.webm'],
                           stdin=subprocess.PIPE)
    card = title_card(len(ORDER)).tobytes()
    for _ in range(FPS * 3):
        out.stdin.write(card)
    clips = list(CLIPS)
    for n, key in enumerate(ORDER):
        clip = clips[n % len(clips)]
        a, b = frames(f'video/{clip}/{key}.webm'), frames(f'video/{clip}/ours-none.webm')
        title, licence, passes, note = INFO[key]
        where = 'CPU + GPU (model)' if key in MODELS else 'GPU only'
        for i in range(SEG):
            fa, fb = next(a), next(b)
            # The wipe: in from the right, then slowly back and forth between 25% and 75%
            t = i / SEG
            pos = int(W * (0.5 + 0.25 * np.sin(2 * np.pi * (t * 1.5 - 0.25))) if t > 0.08 else W * (1 - t / 0.08 * 0.25))
            frame = fb.copy()
            frame[:, :pos] = fa[:, :pos]
            frame[:, max(pos - 1, 0):pos + 1] = (80, 170, 255)
            img = Image.fromarray(frame)
            caption(img, [(f'{n + 1}/{len(ORDER)}  {title}', big, 'white'),
                          (f'{passes} pass{"es" if passes > 1 else ""}  ·  {licence}  ·  {where}', mid, (120, 200, 255)),
                          (note, small, (200, 200, 200))], (24, 20))
            caption(img, [('plain', mid, 'white')], (W - 24, 20), anchor_right=True)
            caption(img, [(f'clip: {clip}', small, (170, 170, 170))], (24, H - 70))
            out.stdin.write(img.tobytes())
        print(key, flush=True)
    out.stdin.close()
    out.wait()
    # A widely playable copy: H.264 4:2:0 at a high rate (the chroma subsampling softens the colour masks)
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', 'demo/crt-shaders-demo.webm', '-c:v', 'libx264', '-preset',
                    'slow', '-crf', '12', '-pix_fmt', 'yuv420p', '-movflags', '+faststart',
                    'demo/crt-shaders-demo.mp4'], check=True)


if __name__ == '__main__':
    main()
