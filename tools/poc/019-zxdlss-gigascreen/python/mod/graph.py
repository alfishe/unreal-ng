"""Decision graph: stages of detectors, each with a condition on the scene
class and a prior that scales the detectors' confidence.

A stage sees what the earlier stages explained (state["explained"]) and may
only claim pixels that are still free. Stages run in order; inside a stage the
consensus picks one proposal per pixel.

The default graph reproduces v10 (the accepted baseline):
  pixel   period2..period5  always (every scene may hold static GigaScreen)
  field   field             when some tiles alternate their color set
  steps   steps             (steps=True) two-page textures moving in steps
  scene   scene_avg         (scene_avg=True) whole-frame average behind a large static picture
"""
from dataclasses import dataclass, field


@dataclass
class Stage:
    name: str
    detectors: list                     # (registered name, params)
    when: callable = lambda scene: True
    prior: callable = lambda scene: 1.0
    override: bool = False              # may re-claim pixels earlier stages explained
    params: dict = field(default_factory=dict)


def default_graph(field_palette=False, field_render="avg3", field_override=False, field_grow=False, field_whole=0.0,
                  field_refine=False, steps=False, scene_avg=False, flat_veto=False, periods=(2, 3, 4, 5),
                  field_seeds_paper=False, field_seeds_border_detail=False):
    pv = {"flat_veto": True} if flat_veto else {}
    graph = [
        Stage("pixel", [(f"period{p}", pv) for p in periods]),
        Stage("field", [("field", {"palette": field_palette, "render": field_render, "override": field_override,
                                   "grow": field_grow, "whole": field_whole, "refine": field_refine,
                                   "seeds_paper": field_seeds_paper,
                                   "seeds_border_detail": field_seeds_border_detail})],
              when=lambda scene: scene.two_page > 0.0, override=field_override),
    ]
    if steps:
        # two-page textures moving in steps: only what the pixel and field stages left
        graph.append(Stage("steps", [("steps", steps if isinstance(steps, dict) else {})]))
    if scene_avg:
        # a large static picture over a moving flickering background: the whole frame averaged
        graph.append(Stage("scene", [("scene_avg", scene_avg if isinstance(scene_avg, dict) else {})], override=True))
    return graph
