"""Whole-frame renderers of the oracle2 references, run as algorithms to see
what each approach scores on its own (one frame of look-ahead).

  ref-avg3     1/4 t-1 + 1/2 t + 1/4 t+1 everywhere
  ref-twopage  1/2 t + 1/2 other page at t (twopage.py) everywhere
  ref-twopage-steps  the same with the step-aware other page (other_page_steps)
"""
import numpy as np

from python.twopage import other_page_steps, two_page_mix


class _Ref:
    delay = 1

    def __init__(self, shape, mixer):
        self.mixer = mixer
        self.planes = []
        self.h, self.w = shape

    def process(self, plane, attr, ink):
        self.planes.insert(0, plane)
        del self.planes[3:]
        cls = np.zeros((self.h, self.w // 8), np.uint8)
        if len(self.planes) < 3:
            p = self.planes[-1]
            return self.mixer.mix(p[None], np.ones((1,) + p.shape)), cls
        nxt, cur, prv = self.planes
        return self.render(cur, prv, nxt), cls


class RefAvg3(_Ref):
    def render(self, cur, prv, nxt):
        w = [np.full(cur.shape, v) for v in (0.25, 0.5, 0.25)]
        return self.mixer.mix(np.stack([prv, cur, nxt]), np.stack(w))


class RefTwoPage(_Ref):
    def render(self, cur, prv, nxt):
        return two_page_mix(self.mixer, cur, prv, nxt)


class RefTwoPageSteps:
    """ref-twopage with the step-aware other page: two frames of look-ahead."""
    delay = 2

    def __init__(self, shape, mixer):
        self.mixer = mixer
        self.planes = []
        self.h, self.w = shape

    def process(self, plane, attr, ink):
        self.planes.insert(0, plane)
        del self.planes[5:]
        cls = np.zeros((self.h, self.w // 8), np.uint8)
        if len(self.planes) < 5:
            p = self.planes[min(2, len(self.planes) - 1)]
            return self.mixer.mix(p[None], np.ones((1,) + p.shape)), cls
        nxt2, nxt, cur, prv, prv2 = self.planes
        fp, fn = other_page_steps(prv2, prv, cur, nxt, nxt2)
        w = [np.full(cur.shape, v) for v in (0.5, 0.25, 0.25)]
        return self.mixer.mix(np.stack([cur, fp, fn]), np.stack(w)), cls
