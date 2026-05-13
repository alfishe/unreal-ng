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
        create_resp = api_client.create_emulator(model="ZX48K")
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
