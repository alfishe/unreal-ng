import pytest

class TestStateInspection:
    
    def test_memory_state(self, api_client, active_emulator):
        """Verify memory state endpoints."""
        emu_id = active_emulator
        
        # Overview
        mem = api_client.get_memory_overview(emu_id)
        assert "total_ram" in mem
        assert "total_rom" in mem
        
        # RAM
        ram = api_client.get_ram_state(emu_id)
        assert "size" in ram
        assert "banks" in ram
        
        # ROM
        rom = api_client.get_rom_state(emu_id)
        assert "size" in rom
        
    def test_screen_state(self, api_client, active_emulator):
        """Verify screen state endpoints."""
        emu_id = active_emulator
        
        # Overview
        screen = api_client.get_screen_state(emu_id)
        assert "mode" in screen
        
        # Mode
        mode = api_client.get_screen_mode(emu_id)
        assert "mode" in mode
        assert "resolution" in mode
        
        # Flash
        flash = api_client.get_screen_flash(emu_id)
        assert "enabled" in flash
        
    def test_audio_state(self, api_client, active_emulator):
        """Verify audio state endpoints."""
        emu_id = active_emulator
        
        # AY Chips
        ay_overview = api_client.get_ay_chips(emu_id)
        assert "chip_count" in ay_overview
        
        if ay_overview["chip_count"] > 0:
            # Detail of first chip
            chip = api_client.get_ay_chip(emu_id, 0)
            assert "registers" in chip
            
            # Specific register
            reg = api_client.get_ay_register(emu_id, 0, 0)
            assert "value" in reg
            
        # Beeper
        beeper = api_client.get_beeper_state(emu_id)
        assert "enabled" in beeper
        
        # Channels
        channels = api_client.get_audio_channels(emu_id)
        assert "channel_count" in channels
