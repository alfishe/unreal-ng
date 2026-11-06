"""Unreal-NG TTD (Time-Travel Debugging) session file inspector.

Public API
----------
``ttdcontainer`` - Pure-Python reader for the engine's session file (schema 2,
                   ``core/src/debugger/ttd/ttd.ksy``).
``main``         - CLI entry point (``python -m src.main ...`` or ``./run.sh``).

v1's reader (``ttd_format`` and the analyses over it) went with v1 (Phase 6).
"""

__version__ = "2.0.0"
