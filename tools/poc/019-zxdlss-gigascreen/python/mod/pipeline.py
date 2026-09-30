"""Mixture-of-detectors pipeline (MoD): classify the scene, run the detectors
the decision graph selects, build the mask by consensus, render.

Same interface as the monolithic versions (process(plane, attr, ink) ->
(RGB, class map)); `delay` frames of look-ahead.

Render rule (unchanged): a pixel with a recipe is the weighted average of at
most 5 raw frames; every other pixel is the raw frame t.
"""
import numpy as np

import python.mod.detectors  # noqa: F401  (registers the detectors)
from python.dlss_v1 import P2, PASS
from python.mod.classifier import SceneClassifier
from python.mod.consensus import consensus
from python.mod.context import FrameContext
from python.mod.graph import default_graph
from python.mod.registry import REGISTRY

FIELD = 7            # class code (v10)
DETECTOR_CLASS = {"period2": P2, "period3": P2 + 1, "period4": P2 + 2, "period5": P2 + 3, "field": FIELD, "steps": FIELD, "scene_avg": FIELD}


class DeflickerMoD:
    def __init__(self, shape, mixer, lookahead=6, graph=None, threshold=0.5, field_palette=False,
                 field_render="avg3", field_override=False, field_grow=False, field_whole=0.0, field_refine=False, steps=False, scene_avg=False, flat_veto=False,
                 periods=(2, 3, 4, 5), field_seeds_paper=False, field_seeds_border_detail=False):
        self.h, self.w = shape
        self.mixer = mixer
        self.delay = lookahead
        self.graph = graph if graph is not None else default_graph(field_palette=field_palette, field_render=field_render,
                                                                   field_override=field_override, field_grow=field_grow,
                                                                   field_whole=field_whole, field_refine=field_refine, steps=steps, scene_avg=scene_avg,
                                                                   flat_veto=flat_veto, periods=periods,
                                                                   field_seeds_paper=field_seeds_paper,
                                                                   field_seeds_border_detail=field_seeds_border_detail)
        self.threshold = threshold
        self.stages = [(st, [REGISTRY[name](**params) for name, params in st.detectors]) for st in self.graph]
        history = max(d.history for _, ds in self.stages for d in ds)
        self.ctx = FrameContext(shape, lookahead, depth=lookahead + history + 2, palette_rgb=mixer.palette_rgb)
        self.classifier = SceneClassifier()
        self.last_motion = np.zeros((self.h, self.w // 8), bool)
        self.last_scene = None

    def _render(self, weights, sources=None):
        keys = list(weights)
        planes = np.stack([sources[k] if isinstance(k, str) else self.ctx.plane(k) for k in keys])
        return self.mixer.mix(planes, np.stack([weights[k] for k in keys]))

    def process(self, plane, attr, ink):
        ctx = self.ctx
        ctx.push(plane, attr, ink)
        L = ctx.t
        shape = (self.h, self.w)
        weights = {L: np.ones(shape)}
        cls = np.full(shape, PASS, np.uint8)
        raw = self._render(weights)
        out = raw
        if ctx.n < 3:
            return out, cls[:, ::8]

        scene = self.classifier.classify(ctx)
        self.last_scene = scene
        explained = np.zeros(shape, bool)
        sources = {}
        motion_veto = np.zeros(shape, bool)
        for stage, detectors in self.stages:
            if not stage.when(scene):
                for d in detectors:
                    d.skip()
                continue
            items = []
            for d in detectors:
                items += d.run(ctx, {"explained": explained, "scene": scene})
            free = np.ones(shape, bool) if stage.override else ~explained
            claimed, w_stage, winner, names, src = consensus(items, free, prior=stage.prior(scene), threshold=self.threshold)
            sources.update(src)
            for it in items:
                vm = getattr(it, "info", {}).get("vetoed_motion")
                if vm is not None:
                    motion_veto |= vm
            if not claimed.any():
                continue
            if claimed.all():
                # the stage renders the whole frame: its recipe alone, in its own source
                # order (the summation order of the mix - C++ parity)
                weights = {}
            for i in list(weights):
                weights[i] = np.where(claimed, 0.0, weights[i])
            for i, w in w_stage.items():
                weights.setdefault(i, np.zeros(shape))
                weights[i] = weights[i] + np.where(claimed, w, 0.0)
            for k, name in enumerate(names):
                cls[winner == k] = DETECTOR_CLASS.get(name, PASS)
            out = self._render(weights, sources)
            explained = np.any(out != raw, axis=2)      # a recipe equal to raw t explains nothing (v10)

        seg = cls.reshape(self.h, self.w // 8, 8).max(axis=2)
        self.last_motion = motion_veto.reshape(self.h, self.w // 8, 8).any(axis=2) & (seg == PASS)
        return out, seg
