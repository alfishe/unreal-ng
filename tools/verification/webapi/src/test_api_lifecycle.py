import pytest
import time

class TestEmulatorLifecycle:
    
    def test_list_emulators(self, api_client):
        """Verify we can list emulators."""
        response = api_client.list_emulators()
        assert "emulators" in response
        assert isinstance(response["emulators"], list)
        assert "count" in response
        assert response["count"] == len(response["emulators"])

    def test_get_models(self, api_client):
        """Verify we can retrieve available models."""
        response = api_client.get_models()
        assert "models" in response
        assert "count" in response
        assert len(response["models"]) > 0
        
        # Check specific known model
        models = [m["name"] for m in response["models"]]
        assert "ZX Spectrum 48K" in models or "ZX48K" in models or "48K" in models

    def test_create_delete_cycle(self, api_client):
        """Verify creating, checking status, and deleting an emulator."""
        # 1. Create
        create_resp = api_client.create_emulator(model="48K")
        assert "id" in create_resp
        emu_id = create_resp["id"]
        
        try:
            # 2. Check details
            info = api_client.get_emulator(emu_id)
            assert info["id"] == emu_id
            assert info["state"] == "initialized" or info["state"] == "paused"
            
            # 3. Start
            api_client.start_emulator(emu_id)
            time.sleep(0.5)
            info_running = api_client.get_emulator(emu_id)
            assert info_running["state"] == "running"
            
        finally:
            # 4. Stop & Delete
            api_client.stop_emulator(emu_id)
            api_client.delete_emulator(emu_id)
            
            # Verify gone
            with pytest.raises(Exception) as excinfo:
                api_client.get_emulator(emu_id)
            assert "404" in str(excinfo.value)

    def test_execution_control(self, api_client, active_emulator):
        """Verify pause, resume, reset functionality."""
        emu_id = active_emulator
        
        # Initial state should be running (from fixture)
        info = api_client.get_emulator(emu_id)
        assert info["state"] == "running"
        
        # Pause
        api_client.pause_emulator(emu_id)
        info = api_client.get_emulator(emu_id)
        assert info["state"] == "paused"
        assert info["is_paused"] is True
        
        # Resume
        api_client.resume_emulator(emu_id)
        info = api_client.get_emulator(emu_id)
        assert info["state"] == "running"
        assert info["is_paused"] is False
        
        # Reset
        api_client.reset_emulator(emu_id)
        # Should still be running after reset
        info = api_client.get_emulator(emu_id)
        assert info["state"] == "running"

    # ------------------------------------------------------------------
    # P0 triage fixes (docs/inprogress/2026-09-14-automation-triage-gaps)
    # ------------------------------------------------------------------

    def test_status_server_block(self, api_client):
        """P0-4: status carries the build fingerprint and creatable model list."""
        response = api_client.get_status()

        assert "server" in response
        server = response["server"]
        for field in ("version", "git_branch", "git_commit", "build_type"):
            assert field in server, f"server block missing '{field}'"
            assert isinstance(server[field], str)
            # "unknown" is legal (git-less configure) - empty is not
            assert server[field]

        assert "models_creatable" in response
        creatable = response["models_creatable"]
        assert isinstance(creatable, list)
        assert "48K" in creatable

    def test_models_creatable_flags(self, api_client):
        """P0-4: every model entry carries a boolean creatable flag."""
        response = api_client.get_models()
        assert response["models"]

        for entry in response["models"]:
            assert "creatable" in entry, f"model '{entry.get('name')}' missing creatable flag"
            assert isinstance(entry["creatable"], bool)

        flags = {m["name"]: m["creatable"] for m in response["models"]}
        assert flags.get("48K") is True

    def test_create_unknown_model_returns_400(self, api_client):
        """P0-2: unknown model is a strict 400 with a reason, no fallback."""
        before = api_client.list_emulators()["count"]

        status, body = api_client.create_emulator_raw("NOSUCHMODEL")

        assert status == 400
        assert body["error"] == "Bad Request"
        assert "NOSUCHMODEL" in body["message"]
        assert body["requested_model"] == "NOSUCHMODEL"
        assert body["available_models_endpoint"] == "/api/v1/emulator/models"

        # Strict failure must not leave an orphan (fallback) instance behind
        assert api_client.list_emulators()["count"] == before

    def test_create_non_creatable_model_returns_400(self, api_client):
        """P0-2/P0-4: a listed-but-unsupported model fails with a reason."""
        models = api_client.get_models()["models"]
        non_creatable = [m["name"] for m in models if not m["creatable"]]
        if not non_creatable:
            pytest.skip("all models are creatable on this build")
        target = non_creatable[0]

        before = api_client.list_emulators()["count"]
        status, body = api_client.create_emulator_raw(target)

        assert status == 400
        assert body["error"] == "Bad Request"
        assert target in body["message"]
        assert body["message"]  # the reason itself is present
        assert api_client.list_emulators()["count"] == before

    def test_create_reports_identity_fields(self, api_client):
        """P0-1: create response and GET /{id} carry resolved machine identity."""
        create_resp = api_client.create_emulator(model="48K")
        emu_id = create_resp["id"]
        try:
            assert create_resp["model"] == "48K"
            assert create_resp["model_full_name"]
            assert create_resp["ram_kb"] == 48
            assert create_resp["config_folder"]
            assert "video_mode" in create_resp  # may be null before start
            assert "speed_multiplier" in create_resp

            info = api_client.get_emulator(emu_id)
            assert info["model"] == "48K"
            assert info["ram_kb"] == 48
            assert info["model_full_name"] == create_resp["model_full_name"]
            assert info["config_folder"] == create_resp["config_folder"]
        finally:
            api_client.delete_emulator(emu_id)

    def test_get_emulator_identity_fields_running(self, api_client):
        """P0-1: identity on a running instance, video_mode live and non-null."""
        resp = api_client.create_and_start_emulator(model="48K")
        emu_id = resp["id"]
        try:
            time.sleep(0.5)
            info = api_client.get_emulator(emu_id)

            assert info["model"] == "48K"
            assert info["ram_kb"] == 48
            assert info["model_full_name"]
            assert info["config_folder"]
            assert info["video_mode"] is not None
            assert isinstance(info["speed_multiplier"], (int, float))
            assert info["speed_multiplier"] >= 1
        finally:
            api_client.stop_emulator(emu_id)
            api_client.delete_emulator(emu_id)
