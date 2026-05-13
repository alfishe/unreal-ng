import pytest

class TestSettings:
    
    def test_settings_manipulation(self, api_client, active_emulator):
        """Verify getting and updating settings."""
        emu_id = active_emulator
        
        # 1. Get all settings
        settings = api_client.get_settings(emu_id)
        assert "settings" in settings
        
        # 2. Pick a setting to change (assuming 'fast_tape' or similar exists from docs)
        # If no specific setting is known safe to change, we try to find one.
        target_setting = "fast_tape"
        
        # Check if it exists
        try:
            val = api_client.get_setting(emu_id, target_setting)
            # Assuming boolean or string
            
            # 3. Update setting
            # Toggle value (crudely)
            new_val = "true" if str(val).lower() in ["false", "off", "0"] else "false"
            
            api_client.update_setting(emu_id, target_setting, new_val)
            
            # 4. Verify update
            updated_val = api_client.get_setting(emu_id, target_setting)
            assert str(updated_val).lower() == new_val.lower()
            
        except Exception as e:
            if "404" in str(e):
                pytest.skip(f"Setting '{target_setting}' not found")
            else:
                raise e
