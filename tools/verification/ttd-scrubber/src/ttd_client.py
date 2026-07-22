"""
TTD Scrubber client.

Thin subclass of the shared UnrealApiClient. Adds one convenience: a
select_instance(name_or_index) helper that turns user input (an integer
index or a substring of the instance label) into a concrete emulator id
returned by GET /api/v1/emulator.

All 10 TTD HTTP verbs live on the parent class; this module only adds
UI-shaped helpers. Keeping the HTTP methods on UnrealApiClient means the
existing pytest suite (tools/verification/webapi/src/) covers them too.
"""

import logging
from typing import List, Tuple

from api_client import UnrealApiClient


class TTDApiClient(UnrealApiClient):
    """UnrealApiClient + instance-discovery helpers for the TTD Scrubber."""

    def __init__(self, base_url: str = "http://localhost:8090"):
        super().__init__(base_url)
        self.logger = logging.getLogger("TTDApiClient")

    # ------------------------------------------------------------------
    # Instance discovery
    # ------------------------------------------------------------------

    def list_instances(self) -> List[dict]:
        """Return the list of emulator instances (empty list on failure).

        GET /api/v1/emulator returns either:
          - a JSON array of objects, or
          - a JSON object with an 'emulators' field (older shape).
        Both are handled here.
        """
        try:
            data = self.list_emulators()
        except Exception as exc:
            self.logger.debug("list_instances failed: %s", exc)
            return []

        if isinstance(data, list):
            return data
        if isinstance(data, dict):
            for key in ("emulators", "instances", "data"):
                if key in data and isinstance(data[key], list):
                    return data[key]
        return []

    def pick_instance(self, index: int = 0) -> Tuple[str, dict]:
        """Return (id, info_dict) for the nth running instance.

        Raises IndexError if no instances are running. The returned id is
        what the TTD endpoints expect as {id} in the path.
        """
        instances = self.list_instances()
        if not instances:
            raise IndexError("no emulator instances running")
        if index >= len(instances):
            raise IndexError(f"index {index} out of range ({len(instances)} instances)")

        info = instances[index]
        # The emulator API surfaces the id under different keys depending
        # on version. Try them in order.
        for key in ("id", "emulator_id", "instance_id", "uuid"):
            if key in info and info[key]:
                return str(info[key]), info
        # Fall back to the string form of the index — the API also accepts
        # 0-based integer indices as the {id} segment.
        return str(index), info

    def instance_label(self, info: dict, index: int) -> str:
        """Human-readable label for the instance picker dropdown."""
        model = info.get("model", info.get("type", "?"))
        name = info.get("name", info.get("symbolic_id"))
        label = f"{index}: {model}"
        if name:
            label += f" ({name})"
        return label

    def find_first_active(self) -> Tuple[str, dict, int]:
        """Pick the first non-destroying instance.

        Returns (id, info, index). Raises IndexError if none qualifies.
        """
        instances = self.list_instances()
        for i, info in enumerate(instances):
            if info.get("destroying", False):
                continue
            for key in ("id", "emulator_id", "instance_id", "uuid"):
                if key in info and info[key]:
                    return str(info[key]), info, i
            return str(i), info, i
        raise IndexError("no active emulator instance")
