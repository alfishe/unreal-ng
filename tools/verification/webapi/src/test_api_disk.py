"""
Disk Inspection API Verification Tests
Tests the new disk inspection endpoints for reading sector/track data,
system info, and file catalog from mounted disk images.
"""
import pytest
import os
import base64

# Path to test resources
RESOURCES_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), "resources")
TEST_DISK = os.path.join(RESOURCES_DIR, "test_disk.trd")


class TestDiskInspection:
    """Tests for disk inspection API endpoints."""

    @pytest.fixture
    def disk_mounted(self, api_client, active_emulator):
        """Fixture that mounts test disk in drive A."""
        emu_id = active_emulator
        
        if not os.path.exists(TEST_DISK):
            pytest.skip(f"Test disk not found: {TEST_DISK}")
        
        api_client.insert_disk(emu_id, "A", TEST_DISK)
        yield emu_id
        
        # Cleanup: eject disk
        try:
            api_client.eject_disk(emu_id, "A")
        except Exception:
            pass

    def test_list_disk_drives(self, api_client, active_emulator):
        """Test GET /disk - list all drives with status."""
        emu_id = active_emulator
        
        result = api_client.list_disk_drives(emu_id)
        
        assert "drives" in result
        assert len(result["drives"]) == 4  # A, B, C, D
        
        for drive in result["drives"]:
            assert "letter" in drive
            assert "mounted" in drive  # API uses 'mounted' not 'inserted'
            assert drive["letter"] in ["A", "B", "C", "D"]

    def test_list_disk_drives_with_disk(self, api_client, disk_mounted):
        """Test drive list shows mounted disk."""
        emu_id = disk_mounted
        
        result = api_client.list_disk_drives(emu_id)
        
        drive_a = next(d for d in result["drives"] if d["letter"] == "A")
        assert drive_a["mounted"] is True

    def test_read_disk_sector(self, api_client, disk_mounted):
        """Test GET /disk/{drive}/sector/{cyl}/{side}/{sec} - read sector data."""
        emu_id = disk_mounted
        
        # Read sector 1 from track 0, side 0
        result = api_client.read_disk_sector(emu_id, "A", 0, 0, 1)
        
        assert "address_mark" in result
        assert "data_base64" in result
        assert "data_crc_valid" in result
        
        # Address mark should reflect what we requested and contain CRC info
        addr = result["address_mark"]
        assert addr["cylinder"] == 0
        assert addr["head"] == 0
        assert "crc_valid" in addr  # ID CRC is inside address_mark
        
        # Data should be base64 encoded
        data_b64 = result["data_base64"]
        data = base64.b64decode(data_b64)
        assert len(data) == 256  # Standard TR-DOS sector size

    def test_read_disk_sector_raw(self, api_client, disk_mounted):
        """Test GET /disk/{drive}/sector/.../raw - raw sector bytes."""
        emu_id = disk_mounted
        
        result = api_client.read_disk_sector_raw(emu_id, "A", 0, 0, 1)
        
        assert "raw_base64" in result
        # Raw data includes gaps, marks, etc - should be larger than 256
        data = base64.b64decode(result["raw_base64"])
        assert len(data) >= 256

    def test_read_disk_track(self, api_client, disk_mounted):
        """Test GET /disk/{drive}/track/{cyl}/{side} - track summary."""
        emu_id = disk_mounted
        
        result = api_client.read_disk_track(emu_id, "A", 0, 0)
        
        assert "cylinder" in result
        assert "side" in result
        assert "sectors" in result
        
        assert result["cylinder"] == 0
        assert result["side"] == 0
        
        # TR-DOS has 16 sectors per track
        sectors = result["sectors"]
        assert len(sectors) == 16
        
        for sec in sectors:
            assert "id_sector" in sec
            assert "id_crc_valid" in sec
            assert "data_crc_valid" in sec

    def test_read_disk_track_raw(self, api_client, disk_mounted):
        """Test GET /disk/{drive}/track/.../raw - raw track bytes."""
        emu_id = disk_mounted
        
        result = api_client.read_disk_track_raw(emu_id, "A", 0, 0)
        
        assert "raw_base64" in result
        # Raw track is ~6250 bytes
        data = base64.b64decode(result["raw_base64"])
        assert len(data) >= 6000

    def test_get_disk_sysinfo(self, api_client, disk_mounted):
        """Test GET /disk/{drive}/sysinfo - TR-DOS system sector."""
        emu_id = disk_mounted
        
        result = api_client.get_disk_sysinfo(emu_id, "A")
        
        # TR-DOS system info fields
        assert "disk_type" in result
        assert "file_count" in result
        assert "free_sectors" in result
        assert "trdos_signature" in result  # API uses trdos_signature
        assert "signature_valid" in result
        
        # Valid TR-DOS disk has signature 0x10
        assert result["trdos_signature"] == "10"
        assert result["signature_valid"] is True

    def test_get_disk_catalog(self, api_client, disk_mounted):
        """Test GET /disk/{drive}/catalog - file listing."""
        emu_id = disk_mounted
        
        result = api_client.get_disk_catalog(emu_id, "A")
        
        assert "files" in result
        assert "file_count" in result
        
        # Each file entry should have name, type, length info
        if result["file_count"] > 0:
            file = result["files"][0]
            assert "name" in file
            assert "type" in file  # API uses 'type' not 'extension'

    def test_get_disk_image(self, api_client, disk_mounted):
        """Test GET /disk/{drive}/image - full disk dump."""
        emu_id = disk_mounted
        
        result = api_client.get_disk_image(emu_id, "A")
        
        assert "image_base64" in result
        assert "cylinders" in result
        assert "sides" in result
        
        # Decode and check size
        data = base64.b64decode(result["image_base64"])
        # 80 tracks * 2 sides * 16 sectors * 256 bytes = 655,360 bytes for DS disk
        assert len(data) >= 327680  # At least single-sided

    def test_sector_invalid_params(self, api_client, disk_mounted):
        """Test error handling for invalid sector parameters."""
        emu_id = disk_mounted
        
        with pytest.raises(Exception) as exc_info:
            api_client.read_disk_sector(emu_id, "A", 100, 0, 1)  # Invalid cylinder
        assert "400" in str(exc_info.value) or "404" in str(exc_info.value)

    def test_no_disk_error(self, api_client, active_emulator):
        """Test error when no disk is mounted."""
        emu_id = active_emulator
        
        with pytest.raises(Exception) as exc_info:
            api_client.read_disk_sector(emu_id, "A", 0, 0, 1)
        assert "400" in str(exc_info.value) or "404" in str(exc_info.value)

    def test_create_blank_disk(self, api_client, active_emulator):
        """Test creating a blank unformatted disk with default geometry."""
        emu_id = active_emulator
        
        result = api_client.create_disk(emu_id, "A")
        
        assert result["success"] is True
        assert result["cylinders"] == 80
        assert result["sides"] == 2
        
        # Verify disk is mounted
        drives = api_client.list_disk_drives(emu_id)
        drive_a = next(d for d in drives["drives"] if d["letter"] == "A")
        assert drive_a["mounted"] is True
        
        # Cleanup
        api_client.eject_disk(emu_id, "A")

    def test_create_disk_custom_geometry(self, api_client, active_emulator):
        """Test creating disk with custom geometry (40 tracks, single-sided)."""
        emu_id = active_emulator
        
        result = api_client.create_disk(emu_id, "B", cylinders=40, sides=1)
        
        assert result["success"] is True
        assert result["cylinders"] == 40
        assert result["sides"] == 1
        assert result["drive"] == "B"
        
        # Cleanup
        api_client.eject_disk(emu_id, "B")
