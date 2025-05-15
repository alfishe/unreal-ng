# -*- coding: utf-8 -*-
#!/usr/bin/env python3
"""
TR-DOS MFM Disk Image Validator

Validates a raw MFM disk image file (.img) for TR-DOS specific structures and consistency.
Checks include:
- File size consistency with MFM track structure.
- MFM track structure for each track:
    - IDAM (ID Address Mark) presence, content (Track, Head, Sector, Size), and CRC.
    - DAM (Data Address Mark) presence and Data CRC for each sector.
- TR-DOS filesystem structures on Track 0 (Physical Track 0, Head 0):
    - TRDVolumeInfo (Sector 9 / index 8): Signature, disk type, file counts, free space.
    - TRDCatalog (Sectors 1-8 / indices 0-7): File entry consistency.
"""

import sys
import os
import struct
import math
from typing import List, Dict, Optional, Any, Tuple, Union # Added Union for AnnotationRecord typing
from dataclasses import dataclass, field, replace # Added for potential MFM generator code reuse


# --- MFM Constants (inspired by mfm_track_generator.py) ---
TARGET_TRACK_SIZE = 6250  # Expected size of one MFM encoded track in bytes
IDAM_BYTE = 0xA1          # Preamble byte for MFM marks
IDAM_MARKER = 0xFE        # MFM ID Address Mark
DAM_MARKER = 0xFB         # MFM Data Address Mark
DEFAULT_SECTORS_PER_TRACK_MFM = 16
DEFAULT_SECTOR_SIZE_MFM = 256
DEFAULT_SECTOR_SIZE_CODE_MFM = 1 # Code for 256 bytes/sector in IDAM

# --- TR-DOS Constants (from trdos_h.txt) ---
TRD_SIGNATURE = 0x10
TRD_MAX_FILES = 128
TRD_VOLUME_SECTOR_INDEX = 8  # Sector 9 on track 0 (0-indexed for array access)
TRD_SECTORS_PER_TRACK = 16   # Logical sectors per TR-DOS track
TRD_SECTOR_SIZE_BYTES = 256  # Logical sector size

# Disk Types (from trdos_h.txt)
DS_80 = 0x16
DS_40 = 0x17
SS_80 = 0x18
SS_40 = 0x19

# Parameters for each disk type. 'total_logical_tracks' is the number of MFM track dumps
# expected in a full image file if sides are interleaved (H0T0, H1T0, H0T1, H1T1...).
DISK_TYPE_PARAMS = {
    DS_80: {"name": "DS_80", "sides": 2, "tracks_per_side": 80, "total_logical_tracks": 160, "free_sectors_empty": (80 * 2 - 1) * 16},
    DS_40: {"name": "DS_40", "sides": 2, "tracks_per_side": 40, "total_logical_tracks": 80,  "free_sectors_empty": (40 * 2 - 1) * 16},
    SS_80: {"name": "SS_80", "sides": 1, "tracks_per_side": 80, "total_logical_tracks": 80,  "free_sectors_empty": (80 * 1 - 1) * 16},
    SS_40: {"name": "SS_40", "sides": 1, "tracks_per_side": 40, "total_logical_tracks": 40,  "free_sectors_empty": (40 * 1 - 1) * 16},
}

# Python struct format strings for parsing TR-DOS structures (little-endian)
# TRDVolumeInfo: 256 bytes
# zeroMarker(B), reserved[224s], firstFreeSector(B), firstFreeTrack(B), diskType(B),
# fileCount(B), freeSectorCount(H), trDOSSignature(B), reserved1[2s],
# reserved2_password[9s], reserved3_unused_byte(B), deletedFileCount(B), label[8s], reserved4_zeros[3s]
TRD_VOLUME_INFO_FORMAT = "<B 224s B B B B H B 2s 9s B B 8s 3s"
TRD_VOLUME_INFO_FIELDS = [
    "zeroMarker", "reserved_A", "firstFreeSector_physical", "firstFreeTrack_physical", "diskType",
    "fileCount", "freeSectorCount", "trDOSSignature", "reserved_B", "reserved_C_password",
    "reserved_D_single_byte", "deletedFileCount", "label_raw", "reserved_E_zeros"
]

# TRDFile: 16 bytes
# name[8s], type(B), params(H), lengthInBytes(H),
# sizeInSectors(B), startSector_physical(B), startTrack_physical(B)
TRD_FILE_FORMAT = "<8s B H H B B B"
TRD_FILE_FIELDS = [
    "name_raw", "type", "params", "lengthInBytes",
    "sizeInSectors", "startSector_physical", "startTrack_physical"
]


def calculate_crc(data: List[int]) -> int:
    """
    Calculate the CRC-16-CCITT (0x1021 polynomial) for the given data.
    Used for MFM IDAM and DAM CRCs.
    """
    crc = 0xFFFF
    polynomial = 0x1021
    for byte_val in data:
        crc ^= byte_val << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) & 0xFFFF) ^ polynomial
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


class TRDOSMFMValidator:
    def __init__(self):
        self.errors: List[Dict[str, Any]] = []
        self.image_data: Optional[bytes] = None
        self.num_mfm_tracks_in_file: int = 0
        self.parsed_disk_type_code: Optional[int] = None
        self.parsed_disk_type_params: Optional[Dict[str, Any]] = None
        # track0_sectors_data stores logical sector data for TR-DOS Track 0 (physical T0, H0)
        # Keys are 0-indexed physical sector numbers (0-15).
        self.track0_physical_sectors_data: Dict[int, bytes] = {}

    def _add_error(self, message: str, mfm_track_idx: Optional[int] = None,
                   physical_track_num: Optional[int] = None, physical_head_num: Optional[int] = None,
                   physical_sector_num_1_based: Optional[int] = None, file_idx: Optional[int] = None,
                   level: str = "ERROR"):
        record = {"level": level, "message": message}
        if mfm_track_idx is not None: record["mfm_track_idx_in_file"] = mfm_track_idx
        if physical_track_num is not None: record["physical_track"] = physical_track_num
        if physical_head_num is not None: record["physical_head"] = physical_head_num
        if physical_sector_num_1_based is not None: record["physical_sector_1_based"] = physical_sector_num_1_based
        if file_idx is not None: record["trdos_file_index"] = file_idx # 0-based file index in catalog
        self.errors.append(record)

        # Context string for printing
        ctx_parts = []
        if mfm_track_idx is not None: ctx_parts.append(f"ImgTrackIdx:{mfm_track_idx}")
        if physical_track_num is not None: ctx_parts.append(f"PT:{physical_track_num}")
        if physical_head_num is not None: ctx_parts.append(f"PH:{physical_head_num}")
        if physical_sector_num_1_based is not None: ctx_parts.append(f"PS:{physical_sector_num_1_based}")
        if file_idx is not None: ctx_parts.append(f"FileIdx:{file_idx}")
        ctx_str = f" ({', '.join(ctx_parts)})" if ctx_parts else ""

        print(f"{level}: {message}{ctx_str}")

    def _find_next_pattern(self, data: bytes, pattern: bytes, start_offset: int) -> int:
        try:
            return data.index(pattern, start_offset)
        except ValueError:
            return -1

    def _parse_one_mfm_track(self, mfm_track_data: bytes, mfm_track_idx_in_file: int,
                             expected_physical_track: int, expected_physical_head: int) -> bool:
        """
        Parses a single MFM track dump, validates IDAMs/DAMs and their CRCs.
        If this is TR-DOS Track 0 (physical T0,H0), stores its sector data.
        """
        # MFM mark sequences to search for (preamble + marker byte)
        idam_preamble = bytes([IDAM_BYTE] * 3)
        idam_full_marker_pattern = idam_preamble + bytes([IDAM_MARKER])
        dam_full_marker_pattern = idam_preamble + bytes([DAM_MARKER])

        current_search_offset_in_track = 0
        sectors_successfully_parsed = 0
        is_trdos_track0 = (expected_physical_track == 0 and expected_physical_head == 0)

        for physical_sector_num_1_based in range(1, DEFAULT_SECTORS_PER_TRACK_MFM + 1):
            # Find IDAM for this sector
            idam_marker_bytes_start_offset = self._find_next_pattern(mfm_track_data, idam_full_marker_pattern, current_search_offset_in_track)

            if idam_marker_bytes_start_offset == -1:
                self._add_error(f"IDAM for sector {physical_sector_num_1_based} not found.",
                                mfm_track_idx=mfm_track_idx_in_file, physical_track_num=expected_physical_track, physical_head_num=expected_physical_head)
                return False # Critical failure for this track's integrity

            # IDAM payload starts with the IDAM_MARKER byte itself
            idam_payload_start_offset = idam_marker_bytes_start_offset + len(idam_preamble)

            # IDAM structure: Marker(1), T(1), H(1), S(1), Size(1), CRC(2) = 7 bytes total from Marker
            if idam_payload_start_offset + 7 > len(mfm_track_data):
                self._add_error(f"Track data too short for full IDAM payload/CRC (Sector {physical_sector_num_1_based}).",
                                mfm_track_idx=mfm_track_idx_in_file, physical_track_num=expected_physical_track, physical_head_num=expected_physical_head, physical_sector_num_1_based=physical_sector_num_1_based)
                return False

            # Bytes for ID CRC: ID_Marker, Track, Head, Sector, SizeCode
            id_field_for_crc_calc = list(mfm_track_data[idam_payload_start_offset : idam_payload_start_offset + 5])
            read_track_in_idam = id_field_for_crc_calc[1]
            read_head_in_idam = id_field_for_crc_calc[2]
            read_sector_num_in_idam = id_field_for_crc_calc[3]
            read_size_code_in_idam = id_field_for_crc_calc[4]

            stored_id_crc_bytes = mfm_track_data[idam_payload_start_offset + 5 : idam_payload_start_offset + 7]
            stored_id_crc = (stored_id_crc_bytes[0] << 8) | stored_id_crc_bytes[1]

            # Validate IDAM content
            if read_track_in_idam != expected_physical_track:
                self._add_error(f"IDAM Track mismatch. Expected {expected_physical_track}, got {read_track_in_idam}.",
                                mfm_track_idx=mfm_track_idx_in_file, physical_track_num=expected_physical_track, physical_head_num=expected_physical_head, physical_sector_num_1_based=physical_sector_num_1_based)
            if read_head_in_idam != expected_physical_head:
                self._add_error(f"IDAM Head mismatch. Expected {expected_physical_head}, got {read_head_in_idam}.",
                                mfm_track_idx=mfm_track_idx_in_file, physical_track_num=expected_physical_track, physical_head_num=expected_physical_head, physical_sector_num_1_based=physical_sector_num_1_based)
            if read_sector_num_in_idam != physical_sector_num_1_based:
                self._add_error(f"IDAM Sector number mismatch. Expected {physical_sector_num_1_based}, got {read_sector_num_in_idam}.",
                                mfm_track_idx=mfm_track_idx_in_file, physical_track_num=expected_physical_track, physical_head_num=expected_physical_head, physical_sector_num_1_based=physical_sector_num_1_based)
            if read_size_code_in_idam != DEFAULT_SECTOR_SIZE_CODE_MFM:
                self._add_error(f"IDAM Sector Size Code mismatch. Expected {DEFAULT_SECTOR_SIZE_CODE_MFM}, got {read_size_code_in_idam}.",
                                mfm_track_idx=mfm_track_idx_in_file, physical_track_num=expected_physical_track, physical_head_num=expected_physical_head, physical_sector_num_1_based=physical_sector_num_1_based)

            # Validate ID CRC
            calculated_id_crc = calculate_crc(id_field_for_crc_calc)
            if calculated_id_crc != stored_id_crc:
                self._add_error(f"IDAM CRC error. Stored 0x{stored_id_crc:04X}, Calculated 0x{calculated_id_crc:04X}.",
                                mfm_track_idx=mfm_track_idx_in_file, physical_track_num=expected_physical_track, physical_head_num=expected_physical_head, physical_sector_num_1_based=physical_sector_num_1_based)

            current_search_offset_in_track = idam_payload_start_offset + 7 # Move search past this IDAM

            # Find DAM for this sector (must follow its IDAM)
            dam_marker_bytes_start_offset = self._find_next_pattern(mfm_track_data, dam_full_marker_pattern, current_search_offset_in_track)
            if dam_marker_bytes_start_offset == -1:
                self._add_error(f"DAM for sector {physical_sector_num_1_based} not found after its IDAM.",
                                mfm_track_idx=mfm_track_idx_in_file, physical_track_num=expected_physical_track, physical_head_num=expected_physical_head, physical_sector_num_1_based=physical_sector_num_1_based)
                return False

            dam_marker_byte_offset = dam_marker_bytes_start_offset + len(idam_preamble)
            sector_data_bytes_start_offset = dam_marker_byte_offset + 1

            # DAM structure: Marker(1), Data(256), CRC(2) = 259 bytes total from Marker
            if sector_data_bytes_start_offset + DEFAULT_SECTOR_SIZE_MFM + 2 > len(mfm_track_data):
                self._add_error(f"Track data too short for sector data/CRC (Sector {physical_sector_num_1_based}).",
                                mfm_track_idx=mfm_track_idx_in_file, physical_track_num=expected_physical_track, physical_head_num=expected_physical_head, physical_sector_num_1_based=physical_sector_num_1_based)
                return False

            sector_data_bytes = mfm_track_data[sector_data_bytes_start_offset : sector_data_bytes_start_offset + DEFAULT_SECTOR_SIZE_MFM]

            if is_trdos_track0: # Store its sector data if it's physical T0,H0
                # Use 0-indexed physical sector number as key
                self.track0_physical_sectors_data[physical_sector_num_1_based - 1] = sector_data_bytes

            # Bytes for Data CRC: DAM_Marker + Sector_Data
            dam_and_data_for_crc_calc = [mfm_track_data[dam_marker_byte_offset]] + list(sector_data_bytes)

            stored_data_crc_offset = sector_data_bytes_start_offset + DEFAULT_SECTOR_SIZE_MFM
            stored_data_crc_bytes = mfm_track_data[stored_data_crc_offset : stored_data_crc_offset + 2]
            stored_data_crc = (stored_data_crc_bytes[0] << 8) | stored_data_crc_bytes[1]

            calculated_data_crc = calculate_crc(dam_and_data_for_crc_calc)
            if calculated_data_crc != stored_data_crc:
                self._add_error(f"Sector Data CRC error. Stored 0x{stored_data_crc:04X}, Calculated 0x{calculated_data_crc:04X}.",
                                mfm_track_idx=mfm_track_idx_in_file, physical_track_num=expected_physical_track, physical_head_num=expected_physical_head, physical_sector_num_1_based=physical_sector_num_1_based)

            current_search_offset_in_track = stored_data_crc_offset + 2 # Move search past data & CRC
            sectors_successfully_parsed += 1

        if sectors_successfully_parsed != DEFAULT_SECTORS_PER_TRACK_MFM:
            self._add_error(f"Expected {DEFAULT_SECTORS_PER_TRACK_MFM} sectors, but only parsed {sectors_successfully_parsed} through MFM marker scanning.",
                            mfm_track_idx=mfm_track_idx_in_file, physical_track_num=expected_physical_track, physical_head_num=expected_physical_head)
            return False # If not all 16 sectors were found sequentially.

        return True

    def _validate_trdos_volume_info(self) -> Optional[Dict[str, Any]]:
        """Validates TRDVolumeInfo structure from previously parsed physical sector 8 of Track 0."""
        vol_info_sector_data = self.track0_physical_sectors_data.get(TRD_VOLUME_SECTOR_INDEX)
        if not vol_info_sector_data or len(vol_info_sector_data) != TRD_SECTOR_SIZE_BYTES:
            self._add_error(f"Volume Info sector (Physical T0,H0, Sector {TRD_VOLUME_SECTOR_INDEX+1}) data missing or wrong size.",
                            mfm_track_idx=0, physical_track_num=0, physical_head_num=0)
            return None

        try:
            parsed_vol_info_tuple = struct.unpack(TRD_VOLUME_INFO_FORMAT, vol_info_sector_data)
            vol_info = dict(zip(TRD_VOLUME_INFO_FIELDS, parsed_vol_info_tuple))
        except struct.error as e:
            self._add_error(f"Failed to parse Volume Info struct: {e}",
                            mfm_track_idx=0, physical_track_num=0, physical_head_num=0, physical_sector_num_1_based=TRD_VOLUME_SECTOR_INDEX+1)
            return None

        if vol_info["trDOSSignature"] != TRD_SIGNATURE:
            self._add_error(f"Invalid TR-DOS Signature in Volume Info. Expected 0x{TRD_SIGNATURE:02X}, got 0x{vol_info['trDOSSignature']:02X}.",
                            mfm_track_idx=0, physical_track_num=0, physical_head_num=0, physical_sector_num_1_based=TRD_VOLUME_SECTOR_INDEX+1)

        self.parsed_disk_type_code = vol_info["diskType"]
        self.parsed_disk_type_params = DISK_TYPE_PARAMS.get(self.parsed_disk_type_code)

        if not self.parsed_disk_type_params:
            self._add_error(f"Unknown TR-DOS Disk Type code in Volume Info: 0x{self.parsed_disk_type_code:02X}.",
                            mfm_track_idx=0, physical_track_num=0, physical_head_num=0, physical_sector_num_1_based=TRD_VOLUME_SECTOR_INDEX+1)
        else:
            # Check MFM tracks in file against disk type (max number of logical tracks)
            expected_max_mfm_tracks_for_type = self.parsed_disk_type_params["total_logical_tracks"]
            if self.num_mfm_tracks_in_file > expected_max_mfm_tracks_for_type:
                self._add_error(f"Image file has {self.num_mfm_tracks_in_file} MFM tracks, but disk type {self.parsed_disk_type_params['name']} (0x{self.parsed_disk_type_code:02X}) supports max {expected_max_mfm_tracks_for_type}.", level="WARNING")
            # Note: Image can be smaller if truncated (last tracks empty).

        if vol_info["zeroMarker"] != 0:
            self._add_error(f"Volume Info zeroMarker is not 0 (got 0x{vol_info['zeroMarker']:02X}).",
                            mfm_track_idx=0, physical_track_num=0, physical_head_num=0, physical_sector_num_1_based=TRD_VOLUME_SECTOR_INDEX+1, level="WARNING")

        if vol_info["fileCount"] > TRD_MAX_FILES:
            self._add_error(f"Volume Info fileCount ({vol_info['fileCount']}) exceeds TRD_MAX_FILES ({TRD_MAX_FILES}).",
                            mfm_track_idx=0, physical_track_num=0, physical_head_num=0, physical_sector_num_1_based=TRD_VOLUME_SECTOR_INDEX+1)

        expected_zeros_at_end = bytes([0,0,0])
        if vol_info["reserved_E_zeros"] != expected_zeros_at_end:
            self._add_error(f"Volume Info reserved_E_zeros (3 bytes at end) not all zeros. Got {vol_info['reserved_E_zeros'].hex()}.",
                            mfm_track_idx=0, physical_track_num=0, physical_head_num=0, physical_sector_num_1_based=TRD_VOLUME_SECTOR_INDEX+1, level="WARNING")

        return vol_info

    def _validate_trdos_catalog_and_files(self, vol_info: Dict[str, Any]):
        """Validates TRDCatalog and file entries using data from physical sectors 0-7 of Track 0."""
        if not self.parsed_disk_type_params: # Should have been set by _validate_trdos_volume_info
            self._add_error("Cannot validate catalog details: TR-DOS disk type parameters are unknown.", level="WARNING")
            return

        # Assemble catalog data from physical sectors 0-7 of TR-DOS Track 0
        catalog_raw_data_list = []
        for i in range(TRD_VOLUME_SECTOR_INDEX): # Physical sectors 0 to 7 (i.e., Sector 1 to Sector 8)
            sector_data = self.track0_physical_sectors_data.get(i)
            if not sector_data or len(sector_data) != TRD_SECTOR_SIZE_BYTES:
                self._add_error(f"Catalog sector data for Physical T0,H0, Sector {i+1} missing or wrong size.",
                                mfm_track_idx=0, physical_track_num=0, physical_head_num=0)
                return
            catalog_raw_data_list.append(sector_data)

        catalog_all_bytes = b"".join(catalog_raw_data_list)
        expected_catalog_size = TRD_MAX_FILES * struct.calcsize(TRD_FILE_FORMAT)
        if len(catalog_all_bytes) != expected_catalog_size:
            self._add_error(f"Concatenated TR-DOS catalog data length ({len(catalog_all_bytes)}) is incorrect. Expected {expected_catalog_size}.",
                            mfm_track_idx=0, physical_track_num=0, physical_head_num=0)
            return

        total_sectors_used_by_active_files = 0
        actual_deleted_file_count_in_catalog = 0

        # Validate active files (up to vol_info["fileCount"])
        for i in range(vol_info["fileCount"]):
            file_entry_offset = i * struct.calcsize(TRD_FILE_FORMAT)
            file_entry_bytes = catalog_all_bytes[file_entry_offset : file_entry_offset + struct.calcsize(TRD_FILE_FORMAT)]
            try:
                parsed_file_tuple = struct.unpack(TRD_FILE_FORMAT, file_entry_bytes)
                trd_file = dict(zip(TRD_FILE_FIELDS, parsed_file_tuple))
            except struct.error as e:
                self._add_error(f"Failed to parse TRDFile entry {i}: {e}",
                                mfm_track_idx=0, physical_track_num=0, physical_head_num=0, file_idx=i)
                continue

            file_name_bytes = trd_file["name_raw"]
            if file_name_bytes[0] == 0x00 or file_name_bytes[0] == 0xE5: # Should not happen for active files
                self._add_error(f"File {i}: Name starts with 0x{file_name_bytes[0]:02X} (invalid for active file). Name: {file_name_bytes.decode('ascii', errors='replace')[:8]}",
                                mfm_track_idx=0, physical_track_num=0, physical_head_num=0, file_idx=i)

            try:
                # Check for non-printable ASCII, excluding common space padding.
                # TR-DOS names can use various characters, so this is a basic check.
                fname_str_for_check = file_name_bytes.rstrip(b'\x20 ').decode('ascii')
                if fname_str_for_check and not all(31 < ord(c) < 127 for c in fname_str_for_check):
                    self._add_error(f"File {i}: Name '{fname_str_for_check}' contains non-standard ASCII. Raw: {file_name_bytes.hex()}",
                                    mfm_track_idx=0, physical_track_num=0, physical_head_num=0, file_idx=i, level="WARNING")
            except UnicodeDecodeError:
                self._add_error(f"File {i}: Name contains non-ASCII characters. Raw: {file_name_bytes.hex()}",
                                mfm_track_idx=0, physical_track_num=0, physical_head_num=0, file_idx=i, level="WARNING")

            # startSector_physical and startTrack_physical validation
            if not (0 <= trd_file["startSector_physical"] < TRD_SECTORS_PER_TRACK):
                self._add_error(f"File {i}: Invalid startSector_physical {trd_file['startSector_physical']}.",
                                mfm_track_idx=0, physical_track_num=0, physical_head_num=0, file_idx=i)

            max_physical_track_for_disk = self.parsed_disk_type_params["tracks_per_side"] - 1
            if not (0 <= trd_file["startTrack_physical"] <= max_physical_track_for_disk):
                self._add_error(f"File {i}: Invalid startTrack_physical {trd_file['startTrack_physical']} (max: {max_physical_track_for_disk}).",
                                mfm_track_idx=0, physical_track_num=0, physical_head_num=0, file_idx=i)
            # Files are typically not on physical track 0 (system track)
            elif trd_file["startTrack_physical"] == 0 and trd_file["sizeInSectors"] > 0:
                # If on physical track 0, must start after system area (catalog + volume info = sectors 0-8)
                if trd_file["startSector_physical"] <= TRD_VOLUME_SECTOR_INDEX: # sector 0-8 (0-indexed)
                    self._add_error(f"File {i}: Starts on Physical Track 0 at sector {trd_file['startSector_physical']+1}, within system area.",
                                    mfm_track_idx=0, physical_track_num=0, physical_head_num=0, file_idx=i)

            # Validate sizeInSectors vs lengthInBytes
            calculated_sectors_needed = 0
            if trd_file["lengthInBytes"] > 0:
                calculated_sectors_needed = math.ceil(trd_file["lengthInBytes"] / TRD_SECTOR_SIZE_BYTES)

            if trd_file["lengthInBytes"] > 0 and trd_file["sizeInSectors"] != calculated_sectors_needed:
                self._add_error(f"File {i}: sizeInSectors ({trd_file['sizeInSectors']}) mismatch for lengthInBytes ({trd_file['lengthInBytes']}). Expected {calculated_sectors_needed}.",
                                mfm_track_idx=0, physical_track_num=0, physical_head_num=0, file_idx=i)
            elif trd_file["lengthInBytes"] == 0 and trd_file["sizeInSectors"] not in [0, 1]: # Empty files usually 0 or 1 sector
                self._add_error(f"File {i}: lengthInBytes is 0 but sizeInSectors is {trd_file['sizeInSectors']} (expected 0 or 1).",
                                mfm_track_idx=0, physical_track_num=0, physical_head_num=0, file_idx=i, level="WARNING")

            total_sectors_used_by_active_files += trd_file["sizeInSectors"]

        # Count actual deleted files (0xE5 marker) across all 128 catalog slots
        for i in range(TRD_MAX_FILES):
            file_entry_offset = i * struct.calcsize(TRD_FILE_FORMAT)
            first_char_of_name = catalog_all_bytes[file_entry_offset]
            if first_char_of_name == 0xE5:
                actual_deleted_file_count_in_catalog += 1

        if vol_info["deletedFileCount"] != actual_deleted_file_count_in_catalog:
            self._add_error(f"Volume Info deletedFileCount ({vol_info['deletedFileCount']}) mismatch. Found {actual_deleted_file_count_in_catalog} catalog entries starting with 0xE5.",
                            mfm_track_idx=0, physical_track_num=0, physical_head_num=0, physical_sector_num_1_based=TRD_VOLUME_SECTOR_INDEX+1)

        # Validate freeSectorCount in Volume Info
        max_free_sectors_on_empty_disk = self.parsed_disk_type_params["free_sectors_empty"]
        expected_free_sectors_now = max_free_sectors_on_empty_disk - total_sectors_used_by_active_files
        if vol_info["freeSectorCount"] != expected_free_sectors_now:
            self._add_error(f"Volume Info freeSectorCount ({vol_info['freeSectorCount']}) mismatch. Expected {expected_free_sectors_now} (MaxFree: {max_free_sectors_on_empty_disk} - UsedByFiles: {total_sectors_used_by_active_files}).",
                            mfm_track_idx=0, physical_track_num=0, physical_head_num=0, physical_sector_num_1_based=TRD_VOLUME_SECTOR_INDEX+1)

        # Basic validation of firstFreeSector_physical / firstFreeTrack_physical in Volume Info
        if vol_info["freeSectorCount"] > 0: # If disk is not full
            ffs_phys = vol_info["firstFreeSector_physical"]
            fft_phys = vol_info["firstFreeTrack_physical"]
            if not (0 <= ffs_phys < TRD_SECTORS_PER_TRACK):
                self._add_error(f"Volume Info firstFreeSector_physical ({ffs_phys}) is invalid.",
                                mfm_track_idx=0, physical_track_num=0, physical_head_num=0, physical_sector_num_1_based=TRD_VOLUME_SECTOR_INDEX+1)

            max_phys_track_for_disk = self.parsed_disk_type_params["tracks_per_side"] - 1
            if not (0 <= fft_phys <= max_phys_track_for_disk): # Can point one track beyond if last track is full
                self._add_error(f"Volume Info firstFreeTrack_physical ({fft_phys}) is invalid (max allowed for data: {max_phys_track_for_disk}).",
                                mfm_track_idx=0, physical_track_num=0, physical_head_num=0, physical_sector_num_1_based=TRD_VOLUME_SECTOR_INDEX+1)

            # If it points to physical track 0, it must be after system area
            if fft_phys == 0 and ffs_phys <= TRD_VOLUME_SECTOR_INDEX:
                self._add_error(f"Volume Info firstFree points to system area on Physical Track 0 (T:{fft_phys}, S:{ffs_phys+1}).",
                                mfm_track_idx=0, physical_track_num=0, physical_head_num=0, physical_sector_num_1_based=TRD_VOLUME_SECTOR_INDEX+1, level="WARNING")


    def validate_image_file(self, image_file_path: str) -> List[Dict[str, Any]]:
        self.errors = [] # Reset errors for new validation
        self.track0_physical_sectors_data = {} # Reset stored sector data

        if not os.path.exists(image_file_path):
            self._add_error(f"Image file not found: {image_file_path}")
            return self.errors

        try:
            with open(image_file_path, "rb") as f:
                self.image_data = f.read()
        except IOError as e:
            self._add_error(f"Error reading image file {image_file_path}: {e}")
            return self.errors

        if not self.image_data:
            self._add_error("Image file is empty.")
            return self.errors

        if len(self.image_data) % TARGET_TRACK_SIZE != 0:
            self._add_error(f"Image file size ({len(self.image_data)}) is not a multiple of MFM track size ({TARGET_TRACK_SIZE}). Possible corruption or not a raw MFM image.")

        self.num_mfm_tracks_in_file = len(self.image_data) // TARGET_TRACK_SIZE
        if self.num_mfm_tracks_in_file == 0:
            self._add_error("Image file is smaller than one MFM track.")
            return self.errors

        # --- Step 1: Parse MFM structure of the first MFM track dump in the file ---
        # This corresponds to TR-DOS Track 0 (Physical Track 0, Head 0).
        # Its sector data is needed for Volume Info and Catalog.
        first_mfm_track_dump_bytes = self.image_data[:TARGET_TRACK_SIZE]
        # For the first MFM track dump, expected physical track is 0, head is 0.
        if not self._parse_one_mfm_track(first_mfm_track_dump_bytes, mfm_track_idx_in_file=0,
                                         expected_physical_track=0, expected_physical_head=0):
            # _parse_one_mfm_track would have added specific errors.
            self._add_error("Critical errors parsing MFM structure of the first track (TR-DOS Track 0). Cannot reliably proceed with TR-DOS filesystem validation.",
                            mfm_track_idx=0, physical_track_num=0, physical_head_num=0)
            return self.errors

        # --- Step 2: Parse and validate TRDVolumeInfo from stored sector data of TR-DOS Track 0 ---
        # This also determines the disk type for subsequent multi-track checks.
        volume_info_dict = self._validate_trdos_volume_info()
        if not volume_info_dict or not self.parsed_disk_type_params:
            self._add_error("Failed to validate TRDVolumeInfo or determine disk type. Aborting further TR-DOS specific checks.",
                            mfm_track_idx=0, physical_track_num=0, physical_head_num=0)
            return self.errors # Disk type is crucial for interpreting multi-track images.

        # --- Step 3: Parse MFM structure for remaining MFM track dumps in the file ---
        # (The first MFM track dump, mfm_track_idx_in_file=0, was already processed)
        for mfm_idx in range(1, self.num_mfm_tracks_in_file):
            current_mfm_track_dump_bytes = self.image_data[mfm_idx * TARGET_TRACK_SIZE : (mfm_idx + 1) * TARGET_TRACK_SIZE]

            # Determine expected physical track and head for this mfm_idx (logical TR-DOS track number)
            # Logical TR-DOS track order: 0=H0T0, 1=H1T0, 2=H0T1, 3=H1T1 ...
            expected_phys_track_num: int
            expected_phys_head_num: int

            if self.parsed_disk_type_params["sides"] == 1: # Single-sided format
                expected_phys_head_num = 0
                expected_phys_track_num = mfm_idx # mfm_idx directly maps to physical track
            else: # Double-sided format
                expected_phys_head_num = mfm_idx % 2  # Head 0 for even mfm_idx, Head 1 for odd
                expected_phys_track_num = mfm_idx // 2 # Physical track increments every two mfm_idx

            # Sanity check: does this implied physical track exceed disk geometry?
            if expected_phys_track_num >= self.parsed_disk_type_params["tracks_per_side"]:
                self._add_error(f"MFM track dump at index {mfm_idx} implies physical track {expected_phys_track_num}, "
                                f"which exceeds max physical track ({self.parsed_disk_type_params['tracks_per_side']-1}) "
                                f"for disk type {self.parsed_disk_type_params['name']}.",
                                mfm_track_idx=mfm_idx, level="WARNING")
                # This track might be extraneous or indicates a mismatch with VolumeInfo's diskType.
                # We can still try to parse it if it's within the file, but flag it.

            self._parse_one_mfm_track(current_mfm_track_dump_bytes, mfm_track_idx_in_file=mfm_idx,
                                      expected_physical_track=expected_phys_track_num,
                                      expected_physical_head=expected_phys_head_num)
            # Errors from _parse_one_mfm_track are added to self.errors. Validation continues.

        # --- Step 4: Validate TR-DOS Catalog and File entries (using stored sector data from TR-DOS Track 0) ---
        self._validate_trdos_catalog_and_files(volume_info_dict)

        if not self.errors:
            print(f"\nValidation of '{image_file_path}' complete. No errors found based on implemented checks.")
        else:
            print(f"\nValidation of '{image_file_path}' complete. Found {len(self.errors)} issues.")

        return self.errors


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python trdos_mfm_validator.py <image_file.img>")
        sys.exit(1)

    image_file_to_validate = sys.argv[1]

    validator_instance = TRDOSMFMValidator()
    validation_issues = validator_instance.validate_image_file(image_file_to_validate)

    # Detailed summary already printed by _add_error during validation.
    # This is just a final confirmation.
    if validation_issues:
        print(f"\n--- Summary: {len(validation_issues)} issues identified in '{image_file_to_validate}'. See log above. ---")
    else:
        print(f"\n--- Summary: '{image_file_to_validate}' passed all implemented validation checks. ---")