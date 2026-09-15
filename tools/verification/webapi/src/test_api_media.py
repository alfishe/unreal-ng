"""Media control API tests.

Actualized against the current media contracts (verified live) and switched
from dummy bytes to real fixture images from testdata/:

- Tape:  load -> {status: "success"}
         info -> {status: "loaded"|"empty", state: "idle"|"playing"|"ended",
                  block_count, format, file, position{block,...}}
         play may fast-forward to "ended" when fast/turbo tape is enabled
- Disk:  insert -> {status: "success", drive, path}
         info   -> {status: "inserted"|"empty", drive, file, write_protected}
- Snapshot: load -> {status: "success"}
            info -> {status: "loaded", file}
"""

from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[4]
TAPE_FIXTURE = REPO_ROOT / "testdata" / "sound" / "ay" / "otomata_labs-atarized.tap"
DISK_FIXTURE = REPO_ROOT / "testdata" / "sound" / "The_Viewer1.0.trd"
SNAPSHOT_FIXTURE = REPO_ROOT / "testdata" / "sound" / "covox" / "scroller_by_demarche.sna"


class TestMediaControl:

    def test_tape_lifecycle(self, api_client, active_emulator):
        """Verify tape loading, playback control, rewind and eject."""
        if not TAPE_FIXTURE.exists():
            pytest.skip(f"Tape fixture missing: {TAPE_FIXTURE}")
        emu_id = active_emulator

        # 1. Load a real tape image
        loaded = api_client.load_tape(emu_id, str(TAPE_FIXTURE))
        assert loaded["status"] == "success"

        info = api_client.get_tape_info(emu_id)
        assert info["status"] == "loaded"
        assert info["format"] == "tap"
        assert info["block_count"] >= 1
        assert str(TAPE_FIXTURE) in info["file"]

        # 2. Play / stop. With fast/turbo tape enabled the tape may race to
        #    "ended" between the two calls, so accept either active state.
        api_client.play_tape(emu_id)
        info = api_client.get_tape_info(emu_id)
        assert info["state"] in ("playing", "ended")

        api_client.stop_tape(emu_id)
        info = api_client.get_tape_info(emu_id)
        assert info["state"] == "idle"

        # 3. Rewind returns to the first block while keeping the image
        rewound = api_client.rewind_tape(emu_id)
        assert rewound["status"] == "success"
        info = api_client.get_tape_info(emu_id)
        assert info["position"]["block"] == 0

        # 4. Eject
        ejected = api_client.eject_tape(emu_id)
        assert ejected["status"] == "success"
        info = api_client.get_tape_info(emu_id)
        assert info["status"] == "empty"

    def test_disk_lifecycle(self, api_client, active_emulator):
        """Verify disk insertion, info and ejection."""
        if not DISK_FIXTURE.exists():
            pytest.skip(f"Disk fixture missing: {DISK_FIXTURE}")
        emu_id = active_emulator

        inserted = api_client.insert_disk(emu_id, "A", str(DISK_FIXTURE))
        assert inserted["status"] == "success"
        assert inserted["drive"] == "A"

        info = api_client.get_disk_info(emu_id, "A")
        assert info["status"] == "inserted"
        assert str(DISK_FIXTURE) in info["file"]
        assert isinstance(info["write_protected"], bool)

        api_client.eject_disk(emu_id, "A")
        info = api_client.get_disk_info(emu_id, "A")
        assert info["status"] == "empty"
        assert info["file"] == ""

    def test_snapshot_lifecycle(self, api_client, active_emulator):
        """Verify loading a snapshot and querying snapshot info."""
        if not SNAPSHOT_FIXTURE.exists():
            pytest.skip(f"Snapshot fixture missing: {SNAPSHOT_FIXTURE}")
        emu_id = active_emulator

        loaded = api_client.load_snapshot(emu_id, str(SNAPSHOT_FIXTURE))
        assert loaded["status"] == "success"

        info = api_client.get_snapshot_info(emu_id)
        assert info["status"] == "loaded"
        assert str(SNAPSHOT_FIXTURE) in info["file"]
