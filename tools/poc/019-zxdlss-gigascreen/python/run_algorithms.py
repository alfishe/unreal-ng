"""Registry of the POC algorithms, shared by run.py, regress.py and oracle2.py."""
from functools import partial

from python.dlss_v1 import DeflickerV1
from python.dlss_v2 import DeflickerV2
from python.dlss_v3 import DeflickerV3
from python.dlss_v4 import DeflickerV4
from python.dlss_v5 import DeflickerV5
from python.dlss_v6 import DeflickerV6
from python.dlss_v7 import DeflickerV7
from python.dlss_v8 import DeflickerV8
from python.dlss_v9 import DeflickerV9
from python.dlss_v10 import DeflickerV10
from python.dlss_v11 import DeflickerV11
from python.mod.pipeline import DeflickerMoD
from python.refalgs import RefAvg3, RefTwoPage

ALGORITHMS = {"v1": DeflickerV1, "v2": DeflickerV2, "v3": DeflickerV3, "v4": DeflickerV4,
              "v5": DeflickerV5, "v5m": partial(DeflickerV5, tile_motion=True), "v6": DeflickerV6,
              "v6c": partial(DeflickerV6, confirm=8), "v6c12": partial(DeflickerV6, confirm=12),
              "v6b": partial(DeflickerV6, confirm=8, bias=0.8),
              "v6p2": partial(DeflickerV6, max_period=2, key="pixel"), "v6p2c8": partial(DeflickerV6, max_period=2, confirm=8, key="pixel"),
              "v6p2c6": partial(DeflickerV6, max_period=2, confirm=6, key="pixel"),
              "v6cell": partial(DeflickerV6, max_period=2, confirm=4), "v6cellc6": partial(DeflickerV6, max_period=2, confirm=6),
              "v6mem": partial(DeflickerV6, max_period=2, confirm=6, remember=True),
              "v6la": partial(DeflickerV6, max_period=2, confirm=6, remember=True, lookahead=True),
              "v6lanomem": partial(DeflickerV6, max_period=2, confirm=6, lookahead=True),
              "v6las": partial(DeflickerV6, max_period=2, confirm=6, remember=True, lookahead=True, spatial=4),
              "v7": DeflickerV7, "v8": DeflickerV8,
              "v8e8": partial(DeflickerV8, establish=8), "v8e10": partial(DeflickerV8, establish=10),
              "v8e12": partial(DeflickerV8, establish=12),
              "v9l2": partial(DeflickerV9, lookahead=2), "v9l4": partial(DeflickerV9, lookahead=4),
              "v9l6": partial(DeflickerV9, lookahead=6), "v9l10": partial(DeflickerV9, lookahead=10),
              "v10": DeflickerV10, "v11": DeflickerV11,
              "mod": DeflickerMoD, "mod-palette": partial(DeflickerMoD, field_palette=True),
              "ref-avg3": RefAvg3, "ref-twopage": RefTwoPage,
              "mod-tp": partial(DeflickerMoD, field_render="twopage", field_override=True),
              "mod-tpg": partial(DeflickerMoD, field_render="twopage", field_override=True, field_grow=True),
              "mod-tpgw": partial(DeflickerMoD, field_render="twopage", field_override=True, field_grow=True, field_whole=0.3)}
NEEDS_PLANE_B = {"v5", "v5m"} | {k for k in ALGORITHMS if k.startswith(("v6", "v7", "v8", "v9", "v10", "v11", "mod", "ref"))}
