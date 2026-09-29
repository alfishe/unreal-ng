"""Detector registry and the proposal a detector returns.

A detector looks at the FrameContext and proposes, per pixel, how frame t
should be rendered (a recipe: raw frames and weights, at most 5 frames) with a
confidence in [0, 1]. Detectors never write the output; the consensus picks
one proposal per pixel. Detectors register themselves by name at import time;
the decision graph (graph.py) says which ones run on which scene.
"""
from dataclasses import dataclass, field

import numpy as np

REGISTRY = {}


def register(cls):
    REGISTRY[cls.name] = cls
    return cls


@dataclass
class Proposal:
    detector: str
    mask: np.ndarray                    # H x W bool: pixels this detector speaks for
    confidence: np.ndarray              # H x W float in [0, 1]
    weights: dict                       # source -> H x W weight (sums to 1 on mask); a source is a
                                        # frame index (raw frame) or a name in `sources`
    sources: dict = field(default_factory=dict)   # name -> H x W palette plane (a raw frame moved)
    rank: np.ndarray = None             # tie-break, lower wins (e.g. the period)
    info: dict = field(default_factory=dict)


@dataclass
class Veto:
    detector: str
    mask: np.ndarray                    # pixels that must stay raw
    info: dict = field(default_factory=dict)


class Detector:
    name = "base"
    tags = ()                           # static / dynamic / fine / field / border
    lookahead = 0                       # frames of look-ahead it needs
    history = 0                         # frames of history it needs

    def __init__(self, **params):
        self.params = params

    def skip(self):
        """Called on frames where the decision graph does not run this detector
        (stateful detectors reset what would have decayed)."""

    def run(self, ctx, state):
        """-> list of Proposal / Veto. `state` holds what earlier stages decided
        (state["explained"]: pixels already given a mixing recipe)."""
        raise NotImplementedError
