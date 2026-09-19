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


class TestMediaUpload:
    """Tests for embedded media upload (webapi-media-upload-tdd.md)."""

    def test_tape_upload_raw_body(self, api_client, active_emulator):
        """Upload tape via raw body with X-Filename header."""
        if not TAPE_FIXTURE.exists():
            pytest.skip(f"Tape fixture missing: {TAPE_FIXTURE}")
        emu_id = active_emulator

        # Upload using the new method
        loaded = api_client.load_tape_upload(emu_id, TAPE_FIXTURE)
        assert loaded["status"] == "success"
        assert loaded.get("uploaded") is True

        info = api_client.get_tape_info(emu_id)
        assert info["status"] == "loaded"
        assert info["format"] == "tap"

        # Cleanup
        api_client.eject_tape(emu_id)

    def test_disk_upload_raw_body(self, api_client, active_emulator):
        """Upload disk via raw body with X-Filename header."""
        if not DISK_FIXTURE.exists():
            pytest.skip(f"Disk fixture missing: {DISK_FIXTURE}")
        emu_id = active_emulator

        loaded = api_client.insert_disk_upload(emu_id, "A", DISK_FIXTURE)
        assert loaded["status"] == "success"
        assert loaded.get("uploaded") is True

        info = api_client.get_disk_info(emu_id, "A")
        assert info["status"] == "inserted"

        # Cleanup
        api_client.eject_disk(emu_id, "A")

    def test_snapshot_upload_raw_body(self, api_client, active_emulator):
        """Upload snapshot via raw body with X-Filename header."""
        if not SNAPSHOT_FIXTURE.exists():
            pytest.skip(f"Snapshot fixture missing: {SNAPSHOT_FIXTURE}")
        emu_id = active_emulator

        loaded = api_client.load_snapshot_upload(emu_id, SNAPSHOT_FIXTURE)
        assert loaded["status"] == "success"
        assert loaded.get("uploaded") is True

    def test_upload_size_limit(self, api_client, active_emulator):
        """Verify size limit enforcement (>1MB for disk should fail)."""
        emu_id = active_emulator

        # Create oversized content (>1MB)
        oversized = b'\x00' * (2 * 1024 * 1024)  # 2MB

        import requests
        try:
            api_client.insert_disk_upload(emu_id, "A", oversized, "oversized.trd")
            pytest.fail("Expected exception for oversized upload")
        except Exception as e:
            # Should get 400 or 413 with size error
            assert "too large" in str(e).lower() or "413" in str(e)

    def test_upload_invalid_extension(self, api_client, active_emulator):
        """Verify unknown extension is rejected."""
        emu_id = active_emulator

        content = b'dummy content'
        import requests
        try:
            api_client.load_tape_upload(emu_id, content, "file.xyz")
            pytest.fail("Expected exception for invalid extension")
        except Exception as e:
            assert "extension" in str(e).lower() or "400" in str(e)
