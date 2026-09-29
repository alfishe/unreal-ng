"""Decision graph: stages of detectors, each with a condition on the scene
class and a prior that scales the detectors' confidence.

A stage sees what the earlier stages explained (state["explained"]) and may
only claim pixels that are still free. Stages run in order; inside a stage the
consensus picks one proposal per pixel.

The default graph reproduces v10 (the accepted baseline):
  pixel   period2..period5  always (every scene may hold static GigaScreen)
  field   field             when some tiles alternate their color set
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


def default_graph(field_palette=False, field_render="avg3", field_override=False, field_grow=False, field_whole=0.0):
    return [
        Stage("pixel", [("period2", {}), ("period3", {}), ("period4", {}), ("period5", {})]),
        Stage("field", [("field", {"palette": field_palette, "render": field_render, "override": field_override,
                                   "grow": field_grow, "whole": field_whole})],
              when=lambda scene: scene.two_page > 0.0, override=field_override),
    ]
