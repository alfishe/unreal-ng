"""The TTD session-file library: reading (ttdcontainer, session), writing (writer) and recording folders (folder).

Shared by the analyzer (tools/verification/ttd-analyzer) and the maintenance tools (tools/python/ttd/ttdtool.py).
Every error raised on purpose is a TtdError.
"""

from .ttdcontainer import TtdContainerError, TtdError

__all__ = ["TtdError", "TtdContainerError"]
