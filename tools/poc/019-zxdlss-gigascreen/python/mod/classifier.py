"""Scene classifier: cheap features of the frames around t -> probabilities of
content types. The decision graph turns them into which detectors run and
with which prior.

Content types (probabilities in [0, 1]):
  gigascreen   the screen alternates between two pages: frames t and t-2 are
               closer than t and t-1 (share of changed paper pixels)
  two_page     some 16x16 tiles alternate their color SET every frame (the
               field detector's own precondition; share of such tiles)
  static       little translation in the frame (share of moved pixels, inverse)
"""
from dataclasses import dataclass

import numpy as np

from python.mod.detectors import FieldDetector


@dataclass
class SceneClass:
    gigascreen: float
    two_page: float
    static: float
    features: dict


class SceneClassifier:
    def classify(self, ctx):
        L = ctx.t
        feats = {}
        if ctx.n >= L + 3:
            paper = ctx.paper
            d1 = float(np.mean(ctx.plane(L)[paper] != ctx.plane(L + 1)[paper]))
            d2 = float(np.mean(ctx.plane(L)[paper] != ctx.plane(L + 2)[paper]))
            feats.update(d1=d1, d2=d2)
            gigascreen = float(np.clip((d1 - d2) / max(d1, 1e-6), 0, 1)) if d1 > 0.01 else 0.0
            _, set_alt = FieldDetector.tile_features(ctx)
            two_page = float(set_alt.mean())
            moved = float(np.mean(ctx.motion(L)))
            feats.update(set_alt_tiles=int(set_alt.sum()), moved=moved)
            static = float(np.clip(1.0 - 10 * moved, 0, 1))
        else:
            gigascreen, two_page, static = 0.0, 0.0, 1.0
        return SceneClass(gigascreen, two_page, static, feats)
