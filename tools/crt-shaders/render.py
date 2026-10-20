#!/usr/bin/env python3
"""Every gallery frame through every libretro preset (librashader-cli) and unreal-qt's own profiles (crt-render) into
<work>/out/<frame>/<key>.png, 4x (1408 x 1152); ours as ours-<profile key>. The frames: testdata/video/crt-gallery
(--frames for others), copied to <work>/src for the gallery page.

usage: render.py [--work DIR] [--frames DIR] [--slang DIR] [--cli PATH] [--render PATH] [key ...]
       (key: a preset key, ours-<profile key>, or nothing for all)"""
import glob
import os
import shutil
import time

import numpy as np
from PIL import Image

from common import FRAMES, librashader, ours, parser
from presets import OH, OURS, OW, PRESETS


def main():
    p = parser(__doc__, cli=True, render=True)
    p.add_argument('--frames', default=FRAMES, help='source frames (default: testdata/video/crt-gallery)')
    p.add_argument('--frame-count', type=int, default=60, help='FrameCount of the render (default 60)')
    p.add_argument('keys', nargs='*')
    args = p.parse_args()
    only = set(args.keys)
    os.makedirs(os.path.join(args.work, 'src'), exist_ok=True)
    sources = sorted(glob.glob(os.path.join(args.frames, '*.png')))
    for src in sources:
        shutil.copy(src, os.path.join(args.work, 'src'))
        os.makedirs(os.path.join(args.work, 'out', os.path.basename(src)[:-4]), exist_ok=True)
    for profile in OURS:
        if only and f'ours-{profile}' not in only:
            continue
        pairs = [(src, os.path.join(args.work, 'out', os.path.basename(src)[:-4], f'ours-{profile}.png'))
                 for src in sources]
        ours(args, profile, pairs, (OW, OH))
        print('ours', profile, len(pairs), 'frames', flush=True)
    for src in sources:
        frame = os.path.basename(src)[:-4]
        for key, preset in PRESETS:
            if only and key not in only:
                continue
            dst = os.path.join(args.work, 'out', frame, f'{key}.png')
            t = time.time()
            error = librashader(args, preset, src, dst, (OW, OH), args.frame_count)
            if error:
                print(frame, key, 'FAILED', error)
                continue
            mean = np.asarray(Image.open(dst).convert('RGB')).mean()
            print(frame, key, f'{time.time() - t:.1f}s', f'mean {mean:.0f}', flush=True)


if __name__ == '__main__':
    main()
