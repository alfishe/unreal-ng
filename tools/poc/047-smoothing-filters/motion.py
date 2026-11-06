#!/usr/bin/env python3
"""POC 047: how evenly a filter moves a horizontal scroller. Between consecutive refreshes, the shift of the band
rows y0..y1 that best explains the change and the pixels it leaves unexplained (torn / ghosted dots).

    motion.py <clip dir> <width> <height> <y0> <y1> [filter ...]
"""
import os, sys
import numpy as np

d, w, h, y0, y1 = sys.argv[1], *map(int, sys.argv[2:6])
names = sys.argv[6:] or sorted(f[:-5] for f in os.listdir(d) if f.endswith(".rgba"))
for name in names:
    f = np.fromfile(os.path.join(d, name + ".rgba"), dtype=np.uint32).reshape(-1, h, w)[:, y0:y1]
    steps, rest = [], []
    for j in range(1, len(f)):
        best = min(((np.roll(f[j - 1], s, axis=1) != f[j])[:, 40:-40].sum(), s) for s in range(-16, 17))
        steps.append(-best[1])
        rest.append(best[0])
    st = np.array(steps[20:140])
    print(f"{name:13s} step mean {st.mean():5.2f} sd {st.std():4.2f}  unexplained px/refresh {np.mean(rest):6.1f}  "
          f"steps {' '.join(map(str, steps[20:44]))}")
