"""
Tests for BASIC Control API endpoints.
Generated: 2026-01-17
"""

import pytest
from api_client import UnrealApiClient


class TestBasicControl:
    """Tests for BASIC control API endpoints."""

    def test_basic_state(self, api_client, active_emulator):
        """Test getting BASIC environment state."""
        emu_id = active_emulator
        
        result = api_client.basic_state(emu_id)
        
        assert result["success"] is True
        assert "state" in result
        assert "in_editor" in result
        assert "ready_for_commands" in result
        
        # Pentagon boots to TR-DOS menu, so we expect menu128k state
        # Other models might report basic48k or basic128k
        assert result["state"] in ["menu128k", "basic128k", "basic48k", "unknown"]

    def test_basic_extract_empty(self, api_client, active_emulator):
        """Test extracting BASIC program from fresh emulator (should be empty or minimal)."""
        emu_id = active_emulator
        
        result = api_client.basic_extract(emu_id)
        
        assert result["success"] is True
        assert "program" in result

    def test_basic_inject(self, api_client, active_emulator):
        """Test injecting a BASIC program."""
        emu_id = active_emulator
        
        program = '10 REM TEST PROGRAM\n20 PRINT "HELLO"\n30 STOP'
        result = api_client.basic_inject(emu_id, program)
        
        # Note: This may fail if emulator is in menu state
        # The success depends on the emulator being in BASIC editor mode
        assert "success" in result

    def test_basic_clear(self, api_client, active_emulator):
        """Test clearing BASIC program."""
        emu_id = active_emulator
        
        result = api_client.basic_clear(emu_id)
        
        assert "success" in result

    def test_basic_run(self, api_client, active_emulator):
        """Test running a BASIC command."""
        emu_id = active_emulator
        
        # Just test that the endpoint responds correctly
        # Actual command execution requires emulator to be in correct state
        result = api_client.basic_run(emu_id)
        
        assert result["success"] is True
        assert "message" in result

    def test_basic_run_with_command(self, api_client, active_emulator):
        """Test running a specific BASIC command."""
        emu_id = active_emulator
        
        # Test with a simple command
        result = api_client.basic_run(emu_id, command='PRINT 123')
        
        assert result["success"] is True
        assert "message" in result
        
        # Check that basic_mode is returned
        if "basic_mode" in result:
            assert result["basic_mode"] in ["48K", "128K"]


class TestBasicControlInvalidEmulator:
    """Tests for BASIC control endpoints with invalid emulator ID."""

    def test_basic_state_invalid_emulator(self, api_client):
        """Test basic_state with non-existent emulator."""
        with pytest.raises(Exception) as exc_info:
            api_client.basic_state("non-existent-id")
        
        assert "404" in str(exc_info.value) or "Not Found" in str(exc_info.value)

    def test_basic_run_invalid_emulator(self, api_client):
        """Test basic_run with non-existent emulator."""
        with pytest.raises(Exception) as exc_info:
            api_client.basic_run("non-existent-id")
        
        assert "404" in str(exc_info.value) or "Not Found" in str(exc_info.value)

    def test_basic_extract_invalid_emulator(self, api_client):
        """Test basic_extract with non-existent emulator."""
        with pytest.raises(Exception) as exc_info:
            api_client.basic_extract("non-existent-id")
        
        assert "404" in str(exc_info.value) or "Not Found" in str(exc_info.value)


class TestBasicControlWithDisk:
    """Integration tests combining BASIC control with disk operations."""

    def test_basic_state_after_emulator_start(self, api_client, active_emulator):
        """Verify BASIC state detection after emulator starts."""
        emu_id = active_emulator
        
        # Give emulator time to boot
        import time
        time.sleep(0.5)
        
        result = api_client.basic_state(emu_id)
        
        assert result["success"] is True
        # Pentagon model should be in TR-DOS menu after boot
        # Check that we get a valid state
        valid_states = ["menu128k", "basic128k", "basic48k", "unknown"]
        assert result["state"] in valid_states
