#!/usr/bin/env python3
"""POC 055: how far a filter's pictures are from the reference's on a real clip (no ground truth there).
  agree.py <dir with layers.rgba and others> <width> <height> <filter> ...
Prints the share of pixels that differ from layers.rgba, over all refreshes, and over the pixels where the reference's
picture differs from `off` (the pixels interpolation changed)."""
import sys
import numpy as np
d, w, h = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
load = lambda n: np.fromfile(f"{d}/{n}.rgba", dtype=np.uint32).reshape(-1, h, w)
ref, off = load("layers"), load("off")
moved = ref != off
for name in sys.argv[4:]:
    x = load(name)
    diff = x != ref
    print(f"{name:8s} differs from the reference in {100 * diff.mean():6.3f} % of all pixels, "
          f"{100 * (diff & moved).sum() / max(1, moved.sum()):6.2f} % of the {100 * moved.mean():.2f} % the reference changed")
