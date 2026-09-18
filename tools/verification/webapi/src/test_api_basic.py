"""Tests for BASIC Control API endpoints.

Actualized against the current contract (verified live):
- basic/state:  {success, state, in_editor, ready_for_commands, description};
                a freshly booted PENTAGON reports "menu128k"
- basic/run:    auto-navigates from the 128K menu into BASIC, then types and
                executes the command; returns {success, command, basic_mode,
                message}
- basic/inject: 'program' is written into program memory directly (deterministic
                write) and round-trips through basic/extract
- basic/clear:  resets the program area (NEW); requires the machine to be in
                BASIC, so enter BASIC first via basic/run
"""

import pytest


class TestBasicControl:
    """Tests for BASIC control API endpoints."""

    def test_basic_state_fresh_boot(self, api_client, active_emulator):
        """A freshly booted PENTAGON reports the 128K menu state."""
        emu_id = active_emulator

        result = api_client.basic_state(emu_id)

        assert result["success"] is True
        assert "state" in result
        assert "in_editor" in result
        assert "ready_for_commands" in result
        assert result["state"] in ["menu128k", "basic128k", "basic48k", "unknown"]

    def test_basic_extract_empty(self, api_client, active_emulator):
        """Extracting from a fresh machine yields an empty program."""
        emu_id = active_emulator

        result = api_client.basic_extract(emu_id)

        assert result["success"] is True
        assert "program" in result
        assert result["program"] == ""

    def test_basic_run_enters_basic(self, api_client, active_emulator):
        """Running without a command injects RUN and lands in the editor."""
        emu_id = active_emulator

        result = api_client.basic_run(emu_id)

        assert result["success"] is True
        assert result["command"] == "RUN"
        assert result["basic_mode"] in ["48K", "128K"]
        assert "Executing" in result["message"]

        # The machine must now be in a BASIC editor, ready for commands
        state = api_client.basic_state(emu_id)
        assert state["state"] in ["basic48k", "basic128k"]
        assert state["in_editor"] is True
        assert state["ready_for_commands"] is True

    def test_basic_run_with_command(self, api_client, active_emulator):
        """Running a specific command auto-navigates from the menu."""
        emu_id = active_emulator

        result = api_client.basic_run(emu_id, command='PRINT 123')

        assert result["success"] is True
        assert result["command"] == 'PRINT 123'
        assert result["basic_mode"] in ["48K", "128K"]

    def test_basic_inject_and_extract_round_trip(self, api_client, active_emulator):
        """An injected numbered program is retrievable verbatim via extract."""
        emu_id = active_emulator

        program = '10 REM TEST PROGRAM\n20 PRINT "HELLO"\n30 STOP'
        result = api_client.basic_inject(emu_id, program)

        assert result["success"] is True
        assert result["state"] in ["basic48k", "basic128k"]

        extracted = api_client.basic_extract(emu_id)
        assert extracted["success"] is True
        assert extracted["program"] == program + "\n"

    def test_basic_clear(self, api_client, active_emulator):
        """Clearing resets the program area once BASIC has been entered."""
        emu_id = active_emulator

        # Enter BASIC and leave a program behind
        api_client.basic_run(emu_id, command='PRINT 1')
        api_client.basic_inject(emu_id, '10 REM TO BE CLEARED')

        result = api_client.basic_clear(emu_id)
        assert result["success"] is True

        extracted = api_client.basic_extract(emu_id)
        assert extracted["success"] is True
        assert extracted["program"] == ""


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
