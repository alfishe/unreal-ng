#!/usr/bin/env python3
"""unreal-qt's shader models against librashader on the rendered gallery (render.py): per frame the mean, the 99th
percentile and the largest difference (0..255) of ours-<key>.png and <key>.png, and the mean over the frames.
unreal-qt-tests --gtest_filter='CRTBeamModel_Test.AgainstLibrashader' with UNREAL_CRT_REFERENCE=<work> checks the same.

usage: compare.py [--work DIR] [model key ...]"""
import glob
import os

import numpy as np
from PIL import Image

from common import parser
from presets import MODELS


def main():
    p = parser(__doc__)
    p.add_argument('keys', nargs='*', default=MODELS)
    args = p.parse_args()
    frames = sorted(d for d in os.listdir(os.path.join(args.work, 'out'))
                    if os.path.isdir(os.path.join(args.work, 'out', d)))
    for key in args.keys:
        means = []
        for frame in frames:
            ours = os.path.join(args.work, 'out', frame, f'ours-{key}.png')
            reference = os.path.join(args.work, 'out', frame, f'{key}.png')
            if not (os.path.exists(ours) and os.path.exists(reference)):
                continue
            a = np.asarray(Image.open(ours).convert('RGB')).astype(np.int32)
            b = np.asarray(Image.open(reference).convert('RGB')).astype(np.int32)
            d = np.abs(a - b)
            means.append(d.mean())
            print(f'{frame:16s} {key:14s} mean {d.mean():6.2f}  p99 {np.percentile(d, 99):4.0f}  max {d.max():3d}'
                  f'  means {a.mean():6.1f} / {b.mean():6.1f}')
        if means:
            print(f'{key}: mean over {len(means)} frames {np.mean(means):.2f}')
        else:
            print(f'{key}: nothing to compare (render.py {key} ours-{key})')


if __name__ == '__main__':
    main()
