# -*- coding: utf-8 -*-
#!/usr/bin/env python3
"""
MFM Floppy Disk Image Generator

Generates MFM track data for full or partial disk images, with special support
for TR-DOS (ZX Spectrum Beta Disk Interface). The script can create
MFM-encoded raw disk images with correct ID Address Marks (IDAMs), Data Address
Marks (DAMs), and CRC checksums for both ID and data fields.

It supports standard IBM System 34 MFM format parameters:
- 16 sectors per track
- 256 bytes per sector (Size Code 1)
- Target MFM track length of 6250 bytes

Features:
- Generation of generic MFM formatted tracks or TR-DOS specific tracks.
- TR-DOS Track 0 generation:
    - Empty file catalog (Sectors 0-7, MFM IDAM 1-8).
    - Volume Information Block (Sector 8, MFM IDAM 9) with disk type,
      free space, label, TR-DOS signature, etc.
- Configurable disk geometry (cylinders, sides) via direct parameters or
  common format aliases (e.g., "ds_dd").
- Various data fill patterns for non-TR-DOS Track 0 sectors:
    - `fill`: Fill with a specific byte value.
    - `random`: Fill with random byte values.
    - `increment`: Fill with an incrementing byte pattern (0x00-0xFF, repeating).
    - `decrement`: Fill with a decrementing byte pattern (0xFF-0x00, repeating).
- Generation of full disk images or a specified range of tracks (cylinders).
- Automatic filename generation based on disk parameters and type.
"""

import sys
import re
import os
import struct
import argparse
import random # For random fill pattern
from typing import List, Dict, Optional, Union, Any, Literal, Tuple
from dataclasses import dataclass, field, replace

# --- MFM Core Constants ---
GAP_BYTE = 0x4E
SYNC_BYTE = 0x00
IDAM_BYTE = 0xA1  # Preamble byte for IDAM/DAM fields (3x A1 before actual mark)
IDAM_MARKER = 0xFE # ID Address Mark
DAM_MARKER = 0xFB  # Data Address Mark (0xF8 for deleted data, not used here for formatting)

DEFAULT_SECTOR_SIZE_CODE = 1 # 0=128, 1=256, 2=512, 3=1024
DEFAULT_SECTOR_SIZE = 256    # Bytes

# IBM System 34 MFM format gap sizes for 256-byte sectors
POST_INDEX_GAP_SIZE = 80     # Gap 1 (after index pulse, before first IDAM)
PRE_ID_SYNC_SIZE = 12        # Sync bytes (0x00) before IDAM preamble
POST_ID_GAP_SIZE = 22        # Gap 2 (after ID CRC, before Data Sync)
PRE_DATA_SYNC_SIZE = 12      # Sync bytes (0x00) before DAM preamble
POST_DATA_GAP_SIZE = 54      # Gap 3 (after Data CRC, before next IDAM Sync or Gap 4)
TARGET_TRACK_SIZE = 6250     # Bytes per MFM track (for 250kbit/s, 300 RPM, DD)

# --- TR-DOS Specific Constants ---
TRD_SIGNATURE = 0x10
TRD_SECTORS_PER_TRACK = 16
TRD_SECTOR_SIZE_BYTES = 256

class TRDDiskType:
    """
    Pythonic enum-like class for TR-DOS disk type codes.
    Bit3: 0 for 1 side, 1 for 2 sides.
    Bit0: 0 for 40 tracks, 1 for 80 tracks.
    """
    DS_80 = 0x16  # Double Sided, 80 Tracks
    DS_40 = 0x17  # Double Sided, 40 Tracks
    SS_80 = 0x18  # Single Sided, 80 Tracks
    SS_40 = 0x19  # Single Sided, 40 Tracks

    @staticmethod
    def get_name(value: int) -> str:
        """Gets a string name for a TRDDiskType code."""
        for name, member_val in TRDDiskType.__dict__.items():
            if isinstance(member_val, int) and member_val == value:
                return name
        return "UNKNOWN_TRD_TYPE"

@dataclass
class TRDVolumeInfo:
    """
    Represents the TR-DOS Volume Information structure, typically found in
    Track 0, Sector 8 (MFM IDAM Sector 9).
    The structure is 256 bytes, but only specific fields are actively managed.
    """
    zero_marker: int = 0 # Offset 0
    # reserved_a: 224 bytes (offsets 1-224), typically zeros.
    # Handled by initializing the pack buffer with zeros.
    first_free_sector: int = 1 # Offset 225: Logical sector on track (0-15)
    first_free_track: int = 1  # Offset 226: Logical track (0-based for TR-DOS system)
    disk_type: int = TRDDiskType.DS_80 # Offset 227
    file_count: int = 0 # Offset 228
    free_sector_count: int = 0 # Offset 229-230 (uint16_t, little-endian)
    trdos_signature: int = TRD_SIGNATURE # Offset 231
    # reserved_b: 2 bytes (offsets 232-233), typically zeros.
    password_area: bytes = field(default_factory=lambda: b' ' * 9) # Offsets 234-242
    # reserved_c: 1 byte (offset 243), typically zero.
    deleted_file_count: int = 0 # Offset 244
    label: bytes = field(default_factory=lambda: b'EMPTYDSK') # Offsets 245-252 (8 bytes)
    # reserved_d: 3 bytes (offsets 253-255), typically zeros.

    def __post_init__(self):
        """Ensures label and password_area are correctly formatted byte strings."""
        if isinstance(self.label, str):
            self.label = self.label.encode('ascii', 'replace')[:8].ljust(8, b' ')
        elif isinstance(self.label, bytes):
            self.label = self.label[:8].ljust(8, b' ')

        if isinstance(self.password_area, str):
            self.password_area = self.password_area.encode('ascii', 'replace')[:9].ljust(9, b' ')
        elif isinstance(self.password_area, bytes):
            self.password_area = self.password_area[:9].ljust(9, b' ')

    def pack(self) -> List[int]:
        """Packs the TRDVolumeInfo into a 256-byte list for a sector."""
        buffer = bytearray(TRD_SECTOR_SIZE_BYTES) # Initialize with zeros

        buffer[0] = self.zero_marker
        buffer[225] = self.first_free_sector
        buffer[226] = self.first_free_track
        buffer[227] = self.disk_type
        buffer[228] = self.file_count
        struct.pack_into("<H", buffer, 229, self.free_sector_count)
        buffer[231] = self.trdos_signature
        buffer[234:234+len(self.password_area)] = self.password_area
        buffer[244] = self.deleted_file_count
        buffer[245:245+len(self.label)] = self.label

        return list(buffer)

@dataclass
class SegmentDescriptor:
    """Describes a logical segment of the MFM track structure."""
    segment_type: str
    name_template: str # Kept for potential debugging
    length_type: Literal["FIXED", "SECTOR_SIZE_CONST", "GAP4_PLACEHOLDER"]
    fixed_length: Optional[int] = None
    args: Dict[str, Any] = field(default_factory=dict)
    is_id_crc_data_source: bool = False
    is_data_crc_data_source: bool = False

def calculate_crc(data: List[int]) -> int:
    """
    Calculates the CRC-16-CCITT (0x1021 polynomial) for the given data.
    Used for both ID field and Data field CRCs in MFM.
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

def _define_track_segments_descriptors(
        num_sectors: int, track_num_val: int, head_num_val: int,
        sector_size_val: int, sector_size_code_val: int
) -> List[SegmentDescriptor]:
    """
    Defines the structural blueprint of an MFM track.
    Creates a list of SegmentDescriptor objects representing logical parts like
    gaps, syncs, ID fields, data fields, CRCs.
    """
    descriptors: List[SegmentDescriptor] = []
    descriptors.append(SegmentDescriptor(
        "GAP_FILL", "Gap 1 (Post-Index)", "FIXED", POST_INDEX_GAP_SIZE, {'fill_byte': GAP_BYTE}
    ))
    for s_num in range(1, num_sectors + 1): # Sector numbers in IDAM are 1-based
        descriptors.extend([
            SegmentDescriptor(
                "SYNC_BYTES", f"Sector {s_num} ID Sync", "FIXED", PRE_ID_SYNC_SIZE, {'sync_byte': SYNC_BYTE}
            ),
            SegmentDescriptor(
                "IDAM_DAM_PREAMBLE", f"Sector {s_num} ID Mark Preamble", "FIXED", 3, {'preamble_byte': IDAM_BYTE}
            ),
            SegmentDescriptor(
                "ID_ADDRESS_MARK_PAYLOAD", f"Sector {s_num} ID Address Mark", "FIXED", 1, {'value': IDAM_MARKER}, is_id_crc_data_source=True
            ),
            SegmentDescriptor(
                "ID_ADDRESS_MARK_PAYLOAD", f"Sector {s_num} Track Number", "FIXED", 1, {'value': track_num_val}, is_id_crc_data_source=True
            ),
            SegmentDescriptor(
                "ID_ADDRESS_MARK_PAYLOAD", f"Sector {s_num} Head Number", "FIXED", 1, {'value': head_num_val}, is_id_crc_data_source=True
            ),
            SegmentDescriptor(
                "ID_ADDRESS_MARK_PAYLOAD", f"Sector {s_num} Sector Number", "FIXED", 1, {'value': s_num}, is_id_crc_data_source=True
            ),
            SegmentDescriptor(
                "ID_ADDRESS_MARK_PAYLOAD", f"Sector {s_num} Size Code", "FIXED", 1, {'value': sector_size_code_val}, is_id_crc_data_source=True
            ),
            SegmentDescriptor(
                "CRC_FIELD", f"Sector {s_num} ID CRC", "FIXED", 2, {'crc_type': "ID"}
            ),
            SegmentDescriptor(
                "GAP_FILL", f"Sector {s_num} Gap 2 (Post-ID)", "FIXED", POST_ID_GAP_SIZE, {'fill_byte': GAP_BYTE}
            ),
            SegmentDescriptor(
                "SYNC_BYTES", f"Sector {s_num} Data Sync", "FIXED", PRE_DATA_SYNC_SIZE, {'sync_byte': SYNC_BYTE}
            ),
            SegmentDescriptor(
                "IDAM_DAM_PREAMBLE", f"Sector {s_num} Data Mark Preamble", "FIXED", 3, {'preamble_byte': IDAM_BYTE}
            ),
            SegmentDescriptor(
                "DATA_ADDRESS_MARK_PAYLOAD", f"Sector {s_num} Data Address Mark", "FIXED", 1, {'value': DAM_MARKER}, is_data_crc_data_source=True
            ),
            SegmentDescriptor(
                "SECTOR_DATA_CONTENT", f"Sector {s_num} Data", "SECTOR_SIZE_CONST", args={'size': sector_size_val, 'sector_num_1_based': s_num}, is_data_crc_data_source=True
            ),
            SegmentDescriptor(
                "CRC_FIELD", f"Sector {s_num} Data CRC", "FIXED", 2, {'crc_type': "DATA"}
            )
        ])
        if s_num < num_sectors:
            descriptors.append(SegmentDescriptor(
                "GAP_FILL", f"Sector {s_num} Gap 3 (Inter-Sector)", "FIXED", POST_DATA_GAP_SIZE, {'fill_byte': GAP_BYTE}
            ))
    descriptors.append(SegmentDescriptor(
        "GAP_FILL", "Gap 4 (Pre-Index)", "GAP4_PLACEHOLDER", 0, {'fill_byte': GAP_BYTE}
    ))
    return descriptors


def generate_sector_payload(pattern: str, value: int, size: int) -> List[int]:
    """
    Generates a list of byte values for a sector's data payload based on a pattern.

    Args:
        pattern: The fill pattern ("fill", "random", "increment", "decrement").
        value: The byte value to use for the "fill" pattern.
        size: The size of the sector payload to generate.

    Returns:
        A list of integers representing the sector data.
    """
    payload = [0] * size
    if pattern == "fill":
        payload = [value] * size
    elif pattern == "random":
        payload = [random.randint(0, 255) for _ in range(size)]
    elif pattern == "increment":
        current_val = 0
        for i in range(size):
            payload[i] = current_val
            current_val = (current_val + 1) % 256
    elif pattern == "decrement":
        current_val = 0xFF
        for i in range(size):
            payload[i] = current_val
            current_val = (current_val - 1 + 256) % 256
    return payload

def generate_mfm_track_bytes(
        sectors_per_track_cfg: int,
        track_num_cfg: int, # Physical cylinder number
        head_num_cfg: int,  # Physical head number
        sector_size_cfg: int,
        sector_size_code_cfg: int,
        sector_payload_map: Optional[Dict[int, List[int]]] = None,
        default_payload_pattern: str = "fill",
        default_payload_value: int = 0xE5
) -> List[int]:
    """
    Generates the raw MFM byte data for a single track.

    Handles IDAMs, DAMs, CRCs, gaps, and sector data.
    Sector data can be provided via `sector_payload_map` or generated
    using `default_payload_pattern` and `default_payload_value`.
    The track length is adjusted to meet `TARGET_TRACK_SIZE`.

    Args:
        sectors_per_track_cfg: Number of sectors on this track.
        track_num_cfg: The physical cylinder number to embed in ID fields.
        head_num_cfg: The physical head number to embed in ID fields.
        sector_size_cfg: Size of each sector in bytes.
        sector_size_code_cfg: Code representing the sector size.
        sector_payload_map: Optional map of {sector_num_1_based: data_bytes_list}
                            for specific sector content.
        default_payload_pattern: Pattern for unassigned sector data.
        default_payload_value: Value for "fill" pattern.

    Returns:
        A list of integers representing the MFM track byte data.
    """
    descriptors = _define_track_segments_descriptors(
        sectors_per_track_cfg, track_num_cfg, head_num_cfg, sector_size_cfg, sector_size_code_cfg
    )

    track_data: List[int] = []
    id_crc_accumulator: List[int] = []
    data_crc_accumulator: List[int] = []

    # Calculate Gap 4 length and handle potential truncation
    current_total_len_sans_gap4 = 0
    gap4_descriptor_index = -1
    for i, desc in enumerate(descriptors):
        length = 0
        if desc.length_type == "FIXED": length = desc.fixed_length or 0
        elif desc.length_type == "SECTOR_SIZE_CONST": length = desc.args.get('size', sector_size_cfg)
        elif desc.length_type == "GAP4_PLACEHOLDER": gap4_descriptor_index = i; continue
        current_total_len_sans_gap4 += length

    bytes_for_gap4 = TARGET_TRACK_SIZE - current_total_len_sans_gap4

    final_descriptors_for_data = [replace(desc) for desc in descriptors]
    if gap4_descriptor_index != -1:
        if bytes_for_gap4 >= 0:
            final_descriptors_for_data[gap4_descriptor_index].fixed_length = bytes_for_gap4
        else:
            final_descriptors_for_data.pop(gap4_descriptor_index)

    # Truncation logic if total length (even with adjusted Gap4) exceeds TARGET_TRACK_SIZE
    current_total_length_with_gap4 = sum(
        (d.fixed_length or (d.args.get('size', sector_size_cfg) if d.length_type == "SECTOR_SIZE_CONST" else 0))
        for d in final_descriptors_for_data
    )

    if current_total_length_with_gap4 > TARGET_TRACK_SIZE:
        temp_descriptors = []
        accumulated_len = 0
        for desc_orig in final_descriptors_for_data:
            desc = replace(desc_orig)
            desc_len_val = desc.fixed_length or (desc.args.get('size', sector_size_cfg) if desc.length_type == "SECTOR_SIZE_CONST" else 0)

            if accumulated_len + desc_len_val <= TARGET_TRACK_SIZE:
                temp_descriptors.append(desc)
                accumulated_len += desc_len_val
            else:
                remaining_space = TARGET_TRACK_SIZE - accumulated_len
                if remaining_space > 0:
                    can_truncate_type = desc.segment_type in ["GAP_FILL", "SYNC_BYTES", "SECTOR_DATA_CONTENT"]
                    if can_truncate_type:
                        desc.fixed_length = remaining_space
                        desc.length_type = "FIXED"
                        if 'size' in desc.args: desc.args['size'] = remaining_space
                        temp_descriptors.append(desc)
                        accumulated_len += remaining_space
                break
        final_descriptors_for_data = temp_descriptors

    # Generate byte data from finalized descriptors
    for desc in final_descriptors_for_data:
        segment_bytes: List[int] = []
        current_segment_length = 0
        if desc.length_type == "FIXED" or desc.length_type == "GAP4_PLACEHOLDER":
            current_segment_length = desc.fixed_length or 0
        elif desc.length_type == "SECTOR_SIZE_CONST":
            current_segment_length = desc.args.get('size', sector_size_cfg)

        if current_segment_length == 0 and not (desc.length_type == "GAP4_PLACEHOLDER" and desc.fixed_length == 0):
            continue # Skip zero-length segments unless it's an explicitly zero-length Gap4

        if desc.segment_type == "GAP_FILL":
            segment_bytes = [desc.args.get('fill_byte', GAP_BYTE)] * current_segment_length
        elif desc.segment_type == "SYNC_BYTES":
            segment_bytes = [desc.args['sync_byte']] * current_segment_length
        elif desc.segment_type == "IDAM_DAM_PREAMBLE":
            segment_bytes = [desc.args['preamble_byte']] * current_segment_length
        elif desc.segment_type == "ID_ADDRESS_MARK_PAYLOAD" or \
                desc.segment_type == "DATA_ADDRESS_MARK_PAYLOAD":
            segment_bytes = [desc.args['value']]
        elif desc.segment_type == "SECTOR_DATA_CONTENT":
            s_num_1_based = desc.args.get('sector_num_1_based', -1)
            provided_data = None
            if sector_payload_map and s_num_1_based != -1:
                provided_data = sector_payload_map.get(s_num_1_based)

            if provided_data:
                if len(provided_data) == current_segment_length:
                    segment_bytes = provided_data
                else: # Adjust length if provided data doesn't match expected
                    print(f"Warning: Cyl {track_num_cfg}, Head {head_num_cfg}, Sector {s_num_1_based} "
                          f"data length mismatch. Expected {current_segment_length}, got {len(provided_data)}. Adjusting.", file=sys.stderr)
                    segment_bytes = (provided_data + generate_sector_payload(default_payload_pattern, default_payload_value, current_segment_length))[:current_segment_length]
            else: # No specific data provided, use default pattern
                segment_bytes = generate_sector_payload(default_payload_pattern, default_payload_value, current_segment_length)
        elif desc.segment_type == "CRC_FIELD":
            crc_input_list: List[int] = id_crc_accumulator if desc.args['crc_type'] == "ID" else data_crc_accumulator
            crc_val = calculate_crc(crc_input_list)
            segment_bytes = [(crc_val >> 8) & 0xFF, crc_val & 0xFF] # Big-endian CRC
            if desc.args['crc_type'] == "ID": id_crc_accumulator = []
            if desc.args['crc_type'] == "DATA": data_crc_accumulator = []
        else:
            segment_bytes = [0] * current_segment_length # Should not happen
            print(f"Warning: Unknown segment type '{desc.segment_type}' encountered.", file=sys.stderr)

        if desc.is_id_crc_data_source: id_crc_accumulator.extend(segment_bytes)
        if desc.is_data_crc_data_source: data_crc_accumulator.extend(segment_bytes)
        track_data.extend(segment_bytes)

    # Final padding/truncation to meet TARGET_TRACK_SIZE exactly
    if len(track_data) < TARGET_TRACK_SIZE:
        track_data.extend([GAP_BYTE] * (TARGET_TRACK_SIZE - len(track_data)))
    elif len(track_data) > TARGET_TRACK_SIZE:
        track_data = track_data[:TARGET_TRACK_SIZE]

    return [b & 0xFF for b in track_data]

def create_trd_volume_sector_data(
        disk_type_byte: int, file_count: int, free_sector_count: int,
        first_free_logical_sector: int, first_free_logical_track: int,
        deleted_file_count: int, label_str: str
) -> List[int]:
    """
    Creates the 256-byte data list for a TR-DOS volume information sector.

    Args:
        disk_type_byte: The TR-DOS disk type code.
        file_count: Number of active files on the disk.
        free_sector_count: Number of free sectors available.
        first_free_logical_sector: Logical sector number (0-15) of the first free sector.
        first_free_logical_track: Logical track number (0-based) of the first free sector.
        deleted_file_count: Number of deleted files.
        label_str: The disk label string (max 8 chars).

    Returns:
        A list of 256 integers representing the sector data.
    """
    vol_info = TRDVolumeInfo(
        first_free_sector=first_free_logical_sector,
        first_free_track=first_free_logical_track,
        disk_type=disk_type_byte,
        file_count=file_count,
        free_sector_count=free_sector_count,
        deleted_file_count=deleted_file_count,
        label=label_str
    )
    return vol_info.pack()

def get_trdos_track0_sector_payload_map(
        num_physical_cylinders_total: int, num_heads_total: int, disk_label: str
) -> Tuple[Dict[int, List[int]], int]:
    """
    Generates the sector data map for TR-DOS Track 0 (Cylinder 0, Head 0).
    This includes empty catalog sectors and the volume information sector.

    Args:
        num_physical_cylinders_total: Total cylinders for the entire disk format.
        num_heads_total: Total heads for the entire disk format.
        disk_label: The disk label string.

    Returns:
        A tuple: (payload_map, trd_disk_type_code).
        payload_map: {sector_num_1_based: data_bytes_list} for Track 0.
        trd_disk_type_code: The determined TR-DOS disk type byte.
    """
    sector_payload_map: Dict[int, List[int]] = {}

    # Determine TR-DOS disk type and free sectors based on *total* disk geometry
    # TR-DOS counts the system track (physical track 0, head 0) as used for data calculations.
    if num_physical_cylinders_total == 80 and num_heads_total == 2:
        trd_disk_type_val = TRDDiskType.DS_80
        trd_free_sectors = (num_physical_cylinders_total * 2 - 1) * TRD_SECTORS_PER_TRACK
    elif num_physical_cylinders_total == 40 and num_heads_total == 2:
        trd_disk_type_val = TRDDiskType.DS_40
        trd_free_sectors = (num_physical_cylinders_total * 2 - 1) * TRD_SECTORS_PER_TRACK
    elif num_physical_cylinders_total == 80 and num_heads_total == 1:
        trd_disk_type_val = TRDDiskType.SS_80
        trd_free_sectors = (num_physical_cylinders_total * 1 - 1) * TRD_SECTORS_PER_TRACK
    elif num_physical_cylinders_total == 40 and num_heads_total == 1:
        trd_disk_type_val = TRDDiskType.SS_40
        trd_free_sectors = (num_physical_cylinders_total * 1 - 1) * TRD_SECTORS_PER_TRACK
    else:
        raise ValueError(f"Unsupported TR-DOS geometry for Track 0: {num_physical_cylinders_total} cyl, {num_heads_total} heads")

    # Sectors 0-7 (MFM IDAM sectors 1-8): File Catalog (empty for new disk)
    empty_catalog_sector_data = [0] * TRD_SECTOR_SIZE_BYTES
    for i in range(1, 9): # MFM Sector numbers 1 through 8
        sector_payload_map[i] = empty_catalog_sector_data

    # Sector 8 (MFM IDAM sector 9): Volume Info
    # For an empty disk, first free data is at TR-DOS logical Track 1, Sector 0.
    volume_info_data = create_trd_volume_sector_data(
        disk_type_byte=trd_disk_type_val,
        file_count=0,
        free_sector_count=trd_free_sectors,
        first_free_logical_sector=0, # TR-DOS logical sector 0
        first_free_logical_track=1,  # TR-DOS logical track 1 (skipping track 0)
        deleted_file_count=0,
        label_str=disk_label
    )
    sector_payload_map[9] = volume_info_data # MFM Sector number 9

    # Sectors 9-15 (MFM IDAM sectors 10-16): Typically unused. Fill with 0x00.
    empty_data_sector = [0x00] * TRD_SECTOR_SIZE_BYTES
    for i in range(10, TRD_SECTORS_PER_TRACK + 1): # MFM Sector numbers 10 through 16
        sector_payload_map[i] = empty_data_sector

    return sector_payload_map, trd_disk_type_val

def create_disk_image_bytes(
        max_cylinders_for_format: int, # Max cylinders for the chosen --disk_format or --cylinders
        num_heads: int,
        start_physical_cyl: int,
        num_physical_cyl_to_gen: int,
        disk_type_param: Literal["generic_mfm", "trdos"],
        trdos_label: str = "EMPTYDSK",
        default_data_pattern: str = "fill",
        default_data_value: int = 0xE5
) -> Tuple[bytearray, Optional[int]]:
    """
    Creates MFM encoded disk image bytes for a specified range of tracks.

    Args:
        max_cylinders_for_format: The total number of cylinders defined by the disk format
                                  (e.g., 80 for DS_DD). Used for TR-DOS Track 0 context.
        num_heads: Number of heads/sides for the disk format.
        start_physical_cyl: The first physical cylinder (0-based) to generate.
        num_physical_cyl_to_gen: The number of physical cylinders to generate.
        disk_type_param: Type of disk image ("generic_mfm" or "trdos").
        trdos_label: Disk label for TR-DOS disks.
        default_data_pattern: Default fill pattern for data sectors.
        default_data_value: Value for the "fill" pattern.

    Returns:
        A tuple: (image_bytes, trd_disk_type_code).
        image_bytes: bytearray containing the MFM data.
        trd_disk_type_code: The TR-DOS disk type byte if a TR-DOS image
                            containing Track 0 was generated, otherwise None.
    """
    if start_physical_cyl < 0 or start_physical_cyl >= max_cylinders_for_format:
        raise ValueError(f"Start track {start_physical_cyl} is out of bounds for format (0-{max_cylinders_for_format-1}).")
    if num_physical_cyl_to_gen <= 0:
        raise ValueError("Number of tracks to generate must be positive.")
    if start_physical_cyl + num_physical_cyl_to_gen > max_cylinders_for_format:
        num_physical_cyl_to_gen = max_cylinders_for_format - start_physical_cyl
        print(f"Warning: Number of tracks to generate adjusted to {num_physical_cyl_to_gen} "
              f"to fit within format limit of {max_cylinders_for_format} cylinders.", file=sys.stderr)

    print(f"Generating {disk_type_param} disk: {num_physical_cyl_to_gen} cylinders (from {start_physical_cyl} to {start_physical_cyl + num_physical_cyl_to_gen - 1}), "
          f"{num_heads} heads. Format Max Cyl: {max_cylinders_for_format}.")

    all_image_bytes = bytearray()
    final_trd_disk_type_byte: Optional[int] = None

    for cyl_offset in range(num_physical_cyl_to_gen):
        current_physical_cyl = start_physical_cyl + cyl_offset
        for head_num in range(num_heads):
            current_sector_payload_map: Optional[Dict[int, List[int]]] = None

            # TR-DOS Track 0 specific logic only if generating physical cylinder 0
            if disk_type_param == "trdos" and current_physical_cyl == 0 and head_num == 0:
                print(f"  Generating TR-DOS Track 0 (Physical Cyl {current_physical_cyl}, Head {head_num})...")
                # Pass total disk geometry for correct TR-DOS volume info calculation
                payload_map, trd_type_byte = get_trdos_track0_sector_payload_map(
                    max_cylinders_for_format, num_heads, trdos_label
                )
                current_sector_payload_map = payload_map
                if final_trd_disk_type_byte is None: # Store for filename, etc.
                    final_trd_disk_type_byte = trd_type_byte
            else:
                print(f"  Generating Cyl {current_physical_cyl}, Head {head_num} (Fill: {default_data_pattern}, Value: 0x{default_data_value:02X})...")

            track_bytes_list = generate_mfm_track_bytes(
                sectors_per_track_cfg=TRD_SECTORS_PER_TRACK,
                track_num_cfg=current_physical_cyl, # This is the value written into IDAM
                head_num_cfg=head_num,
                sector_size_cfg=TRD_SECTOR_SIZE_BYTES,
                sector_size_code_cfg=DEFAULT_SECTOR_SIZE_CODE,
                sector_payload_map=current_sector_payload_map,
                default_payload_pattern=default_data_pattern,
                default_payload_value=default_data_value
            )
            all_image_bytes.extend(track_bytes_list)

    expected_size = num_physical_cyl_to_gen * num_heads * TARGET_TRACK_SIZE
    print(f"Disk image portion generation complete. Total size: {len(all_image_bytes)} bytes (Expected: {expected_size}).")
    if len(all_image_bytes) != expected_size:
        print(f"Warning: Final image size {len(all_image_bytes)} differs from expected {expected_size}.", file=sys.stderr)

    return all_image_bytes, final_trd_disk_type_byte

def construct_filename(
        max_cylinders_for_format: int,
        num_heads: int,
        start_track: int, # The actual first track *in the file*
        num_tracks_in_file: int, # Number of tracks *in the file*
        disk_type_param: Literal["generic_mfm", "trdos"],
        trd_disk_type_byte_for_naming: Optional[int] = None
) -> str:
    """
    Constructs a descriptive filename for the disk image.

    Args:
        max_cylinders_for_format: Total cylinders of the full disk format.
        num_heads: Number of heads for the disk format.
        start_track: The first physical cylinder number included in this image file.
        num_tracks_in_file: The number of cylinders included in this image file.
        disk_type_param: The type of disk ("generic_mfm" or "trdos").
        trd_disk_type_byte_for_naming: The TR-DOS disk type code, if applicable.

    Returns:
        A string for the filename.
    """
    # Base part from TR-DOS type or generic geometry
    if disk_type_param == "trdos" and trd_disk_type_byte_for_naming is not None:
        base_name = f"trdos_{TRDDiskType.get_name(trd_disk_type_byte_for_naming)}"
    else:
        sides_str = "ss" if num_heads == 1 else "ds"
        base_name = f"{disk_type_param}_{sides_str}{max_cylinders_for_format}t"

    # Add track range if it's a partial image
    is_full_image = (start_track == 0 and num_tracks_in_file == max_cylinders_for_format)
    if not is_full_image:
        end_track_in_file = start_track + num_tracks_in_file - 1
        base_name += f"_trk{start_track}-{end_track_in_file}"

    return f"{base_name}.img"

def main():
    """
    Main function to parse arguments and generate the MFM disk image.

    Use Cases & Examples:

    1.  Generate a full TR-DOS Double-Sided, Double-Density (80 tracks, 2 sides) disk image:
        ```bash
        python your_script_name.py --type trdos --disk_format ds_dd --label MYGAME01
        ```
        Output: `trdos_DS80.img`

    2.  Generate a full generic Single-Sided, Double-Density (80 tracks, 1 side) disk image
        with sectors filled with random data:
        ```bash
        python your_script_name.py --type generic_mfm --disk_format ss_dd --fill_pattern random
        ```
        Output: `generic_mfm_ss80t.img`

    3.  Generate a partial TR-DOS image: first 10 tracks (cylinders 0-9) of a
        Double-Sided, Single-Density (40 tracks, 2 sides) format:
        ```bash
        python your_script_name.py --type trdos --disk_format ds_sd --start_track 0 --num_tracks 10
        ```
        Output: `trdos_DS40_trk0-9.img` (Track 0 will be TR-DOS formatted)

    4.  Generate a partial generic image: tracks 20-29 (10 tracks starting from cyl 20)
        of an 80-track, 2-sided format, filled with incrementing byte pattern:
        ```bash
        python your_script_name.py --type generic_mfm --cylinders 80 --sides 2 --start_track 20 --num_tracks 10 --fill_pattern increment
        ```
        Output: `generic_mfm_ds80t_trk20-29.img`

    5.  Generate a full TR-DOS DS_DD disk and specify output filename:
        ```bash
        python your_script_name.py --type trdos --disk_format ds_dd --output custom_trdos_disk.img
        ```
        Output: `custom_trdos_disk.img`

    6.  Generate a single track (e.g., track 5) of a generic 40-track, 1-side format:
        ```bash
        python your_script_name.py --type generic_mfm --disk_format ss_sd --start_track 5 --num_tracks 1
        ```
        Output: `generic_mfm_ss40t_trk5-5.img`
    """
    parser = argparse.ArgumentParser(
        description="MFM Floppy Disk Image Generator.",
        formatter_class=argparse.RawTextHelpFormatter # To preserve help formatting
    )

    type_group = parser.add_argument_group(title="Disk Image Type")
    type_group.add_argument(
        "--type", choices=["generic_mfm", "trdos"], required=True,
        help="Type of disk image to generate."
    )

    geom_group = parser.add_argument_group(title="Disk Geometry Configuration")
    geom_group.add_argument(
        "--cylinders", type=int, choices=[40, 80], default=None,
        help="Number of physical cylinders for the full disk format (e.g., 40 or 80)."
    )
    geom_group.add_argument(
        "--sides", type=int, choices=[1, 2], default=None,
        help="Number of heads/sides for the full disk format (1 or 2)."
    )
    geom_group.add_argument(
        "--disk_format", choices=["ds_dd", "ss_dd", "ds_sd", "ss_sd"], default=None,
        help="Full disk format alias:\n"
             "  ds_dd: 80 cylinders, 2 sides (Double Sided, Double Density)\n"
             "  ss_dd: 80 cylinders, 1 side  (Single Sided, Double Density)\n"
             "  ds_sd: 40 cylinders, 2 sides (Double Sided, Single Density - common for 5.25\")\n"
             "  ss_sd: 40 cylinders, 1 side  (Single Sided, Single Density)\n"
             "If specified, --cylinders/--sides are derived from this. \n"
             "Explicit --cylinders/--sides can override parts of the alias with a warning."
    )

    partial_image_group = parser.add_argument_group(title="Partial Image Generation (Optional)")
    partial_image_group.add_argument(
        "--start_track", type=int, default=0,
        help="Starting physical cylinder (0-based) for the image file (default: 0)."
    )
    partial_image_group.add_argument(
        "--num_tracks", type=int, default=None,
        help="Number of physical cylinders to include in the image file.\n"
             "If not specified, generates all tracks from --start_track to the end of the format."
    )

    trdos_group = parser.add_argument_group(title="TR-DOS Specific Options (used if --type trdos)")
    trdos_group.add_argument(
        "--label", type=str, default="MYDISK",
        help="Disk label for TR-DOS disks (max 8 chars, default: MYDISK)."
    )

    fill_group = parser.add_argument_group(title="Data Sector Fill Options")
    fill_group.add_argument(
        "--fill_pattern", choices=["fill", "random", "increment", "decrement"], default="fill",
        help="Pattern for filling data sectors (default: fill)."
    )
    fill_group.add_argument(
        "--fill_value", type=lambda x: int(x, 0), default=0xE5,
        help="Value for 'fill' pattern (e.g., 0xE5, 0x00) (default: 0xE5)."
    )

    parser.add_argument(
        "--output", type=str, default=None,
        help="Output filename. If not specified, a name will be auto-generated."
    )

    args = parser.parse_args()

    # --- Determine final disk geometry (max_cylinders_for_format, actual_num_heads) ---
    max_cylinders_for_format = args.cylinders
    actual_num_heads = args.sides

    if args.disk_format:
        alias_cyl, alias_sides = None, None
        if args.disk_format == "ds_dd": alias_cyl, alias_sides = 80, 2
        elif args.disk_format == "ss_dd": alias_cyl, alias_sides = 80, 1
        elif args.disk_format == "ds_sd": alias_cyl, alias_sides = 40, 2
        elif args.disk_format == "ss_sd": alias_cyl, alias_sides = 40, 1

        if max_cylinders_for_format is not None and max_cylinders_for_format != alias_cyl:
            print(f"Warning: --cylinders {max_cylinders_for_format} overridden by --disk_format {args.disk_format} (to {alias_cyl}).", file=sys.stderr)
        max_cylinders_for_format = alias_cyl

        if actual_num_heads is not None and actual_num_heads != alias_sides:
            print(f"Warning: --sides {actual_num_heads} overridden by --disk_format {args.disk_format} (to {alias_sides}).", file=sys.stderr)
        actual_num_heads = alias_sides

    if max_cylinders_for_format is None:
        parser.error("Number of cylinders for the disk format must be specified (e.g., --cylinders 80 or via --disk_format).")
    if actual_num_heads is None:
        parser.error("Number of sides for the disk format must be specified (e.g., --sides 2 or via --disk_format).")

    if max_cylinders_for_format not in [40, 80]:
        parser.error(f"Final number of cylinders for format ({max_cylinders_for_format}) must be 40 or 80.")
    if actual_num_heads not in [1, 2]:
        parser.error(f"Final number of sides for format ({actual_num_heads}) must be 1 or 2.")

    # --- Determine track range for generation ---
    start_cyl_to_gen = args.start_track
    if args.num_tracks is None: # Generate all tracks from start_track to end of format
        num_cyl_to_gen = max_cylinders_for_format - start_cyl_to_gen
    else:
        num_cyl_to_gen = args.num_tracks

    if start_cyl_to_gen < 0 or start_cyl_to_gen >= max_cylinders_for_format:
        parser.error(f"--start_track {start_cyl_to_gen} is out of bounds for the disk format (0-{max_cylinders_for_format-1}).")
    if num_cyl_to_gen <= 0:
        parser.error("--num_tracks must be positive if specified.")
    if start_cyl_to_gen + num_cyl_to_gen > max_cylinders_for_format:
        adjusted_num_cyl = max_cylinders_for_format - start_cyl_to_gen
        print(f"Warning: --num_tracks {num_cyl_to_gen} is too large for --start_track {start_cyl_to_gen} "
              f"on a {max_cylinders_for_format}-cylinder format. Adjusting to {adjusted_num_cyl} tracks.", file=sys.stderr)
        num_cyl_to_gen = adjusted_num_cyl

    if args.type == "trdos" and len(args.label) > 8:
        print("Warning: TR-DOS label is longer than 8 characters, will be truncated.", file=sys.stderr)
        args.label = args.label[:8]

    try:
        disk_image_bytes, trd_type_byte_for_name = create_disk_image_bytes(
            max_cylinders_for_format=max_cylinders_for_format,
            num_heads=actual_num_heads,
            start_physical_cyl=start_cyl_to_gen,
            num_physical_cyl_to_gen=num_cyl_to_gen,
            disk_type_param=args.type,
            trdos_label=args.label.upper(),
            default_data_pattern=args.fill_pattern,
            default_data_value=args.fill_value
        )

        output_filename = args.output
        if not output_filename:
            output_filename = construct_filename(
                max_cylinders_for_format, actual_num_heads,
                start_cyl_to_gen, num_cyl_to_gen, # Use actual generated range for filename
                args.type, trd_type_byte_for_name
            )

        print(f"Writing disk image to '{output_filename}'...")
        with open(output_filename, 'wb') as f:
            f.write(disk_image_bytes)
        print(f"Successfully wrote {len(disk_image_bytes)} bytes to '{output_filename}'.")

    except ValueError as e:
        print(f"Configuration or Generation Error: {e}", file=sys.stderr)
        sys.exit(1)
    except IOError as e:
        print(f"File I/O Error: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"An unexpected error occurred: {e}", file=sys.stderr)
        # import traceback # Uncomment for debugging
        # traceback.print_exc()
        sys.exit(1)

if __name__ == "__main__":
    # Quick check for _define_track_segments_descriptors
    if not _define_track_segments_descriptors(1,0,0,256,1):
        print("Error: _define_track_segments_descriptors seems empty or non-functional. Please ensure it's fully included.", file=sys.stderr)
        sys.exit(1)
    main()