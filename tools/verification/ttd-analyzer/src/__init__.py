"""Unreal-NG TTD (Time-Travel Debugging) session file inspector.

Public API
----------
``ttdlib``       - the shared pure-Python reader of the engine's session file
                   (schema 2, ``core/src/debugger/ttd/ttd.ksy``), in
                   ``tools/python/ttd/ttdlib`` (with the maintenance tools,
                   ``tools/python/ttd/ttdtool.py``); put on ``sys.path`` here.
``main``         - CLI entry point (``python -m src.main ...`` or ``./run.sh``).

v1's reader (``ttd_format`` and the analyses over it) went with v1 (Phase 6).
"""

import sys
from pathlib import Path

_TTDLIB = Path(__file__).resolve().parents[3] / "python" / "ttd"
if str(_TTDLIB) not in sys.path:
    sys.path.insert(0, str(_TTDLIB))

__version__ = "2.0.0"
