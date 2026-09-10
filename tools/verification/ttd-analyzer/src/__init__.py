"""Unreal-NG TTD (Time-Travel Debugging) dump analyzer.

Public API
----------
``ttd_format``       — Pure-Python reader for the ``.ttd`` binary format.
``integrity_check``  — Structural / format-level validation.
``anomaly_detector`` — Heuristic detection of likely capture/restore bugs.
``framebuffer_renderer`` — Decode and render ZX Spectrum screen RAM to PNG/PPM.
``timeline_report``  — Markdown summary generator.
``main``             — CLI entry point (``python -m src.main ...`` or ``./run.sh``).

The hand-written parser mirrors the canonical Kaitai Struct schema at
``core/src/debugger/ttd/ttd.ksy``. Regenerating a Kaitai parser from that
schema is supported as a drop-in replacement.
"""

__version__ = "1.0.0"
