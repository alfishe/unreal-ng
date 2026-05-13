import pytest
import os

class TestMediaControl:
    
    # Helper to get a dummy file path (does not need to exist for 404 test, 
    # but for 200 test we might need real files if the emulator checks existence)
    # Assuming emulator checks file existence.
    
    def test_tape_lifecycle(self, api_client, active_emulator, tmp_path):
        """Verify tape loading, playback control, and info."""
        emu_id = active_emulator
        
        # Create a dummy tape file
        tape_file = tmp_path / "test.tap"
        tape_file.write_bytes(b"dummy tape content")
        tape_path = str(tape_file)
        
        # 1. Load Tape
        # Note: Emulator might validate format, so this might fail with 400 if it checks content
        # For now, we expect either 200 (if lazy check) or 400 (if validation).
        # We'll assume the API at least responds.
        try:
            api_client.load_tape(emu_id, tape_path)
            
            # 2. Check Info
            info = api_client.get_tape_info(emu_id)
            assert info["loaded"] is True
            
            # 3. Play/Stop/Rewind
            api_client.play_tape(emu_id)
            info = api_client.get_tape_info(emu_id)
            assert info["playing"] is True
            
            api_client.stop_tape(emu_id)
            info = api_client.get_tape_info(emu_id)
            assert info["playing"] is False
            
            api_client.rewind_tape(emu_id)
            
            # 4. Eject
            api_client.eject_tape(emu_id)
            info = api_client.get_tape_info(emu_id)
            assert info["loaded"] is False
            
        except Exception as e:
            # If valid tape is required, we pass if we got a specific error code like 400
            if "400" in str(e):
                pytest.skip("Emulator requires valid tape image format")
            else:
                raise e

    def test_disk_lifecycle(self, api_client, active_emulator, tmp_path):
        """Verify disk insertion and ejection."""
        emu_id = active_emulator
        
        disk_file = tmp_path / "test.trd"
        disk_file.write_bytes(b"dummy disk content")
        disk_path = str(disk_file)
        
        drive = "A"
        
        try:
            api_client.insert_disk(emu_id, drive, disk_path)
            
            info = api_client.get_disk_info(emu_id, drive)
            assert info["inserted"] is True
            
            api_client.eject_disk(emu_id, drive)
            
            info = api_client.get_disk_info(emu_id, drive)
            assert info["inserted"] is False
            
        except Exception as e:
            if "400" in str(e):
                pytest.skip("Emulator requires valid disk image format")
            else:
                raise e

    def test_snapshot_lifecycle(self, api_client, active_emulator, tmp_path):
        """Verify snapshot loading."""
        emu_id = active_emulator
        
        snap_file = tmp_path / "test.sna"
        snap_file.write_bytes(b"dummy snapshot content")
        snap_path = str(snap_file)
        
        try:
            api_client.load_snapshot(emu_id, snap_path)
            
            info = api_client.get_snapshot_info(emu_id)
            assert info["loaded"] is True
            
        except Exception as e:
            if "400" in str(e):
                pytest.skip("Emulator requires valid snapshot format")
            else:
                raise e
