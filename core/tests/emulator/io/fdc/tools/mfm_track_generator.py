# -*- coding: utf-8 -*-
#!/usr/bin/env python3
"""
MFM Floppy Track Generator

Generates MFM track data, prints annotated hex dump, C++ header content,
and an annotated C++ header with embedded comments.
Configured for WD1793, 16 sectors, 256 bytes/sector, IBM MFM format.
Supports a single global DUMP_BYTES_PER_LINE for all annotated outputs.
Adds full-width separator lines (dynamically sized for console) before new sectors.
C++ comments start at a fixed column after the hex data block.
Includes functions to read/write raw MFM track image files.
Read example now uses annotated dump by regenerating structure annotations separately.
"""

import sys
import re
import os
from typing import List, Dict, Optional, Union, Any, Literal
from dataclasses import dataclass, field, replace

# --- Global Configuration for All Annotated Dump Outputs ---
DUMP_BYTES_PER_LINE = 16
TARGET_LINE_WIDTH = 130 # Used for C++ separator width guidance
# ---


# --- Global variables for track parameters (set in generate_mfm_track) ---
g_sectors = 0
g_sector_size = 0
g_sector_size_code = 0
# ---


AnnotationRecord = Dict[str, Union[int, str, bool]]
g_annotations: List[AnnotationRecord] = []

GAP_BYTE = 0x4E
SYNC_BYTE = 0x00
IDAM_BYTE = 0xA1
IDAM_MARKER = 0xFE
DAM_MARKER = 0xFB

DEFAULT_SECTOR_SIZE_CODE = 1
DEFAULT_SECTOR_SIZE = 256

POST_INDEX_GAP_SIZE = 80
PRE_ID_SYNC_SIZE = 12
POST_ID_GAP_SIZE = 22
PRE_DATA_SYNC_SIZE = 12
POST_DATA_GAP_SIZE = 54
TARGET_TRACK_SIZE = 6250

@dataclass
class SegmentDescriptor:
    segment_type: str
    name_template: str
    length_type: Literal["FIXED", "SECTOR_SIZE_CONST", "GAP4_PLACEHOLDER"]
    fixed_length: Optional[int] = None
    args: Dict[str, Any] = field(default_factory=dict)
    is_id_crc_data_source: bool = False
    is_data_crc_data_source: bool = False
    starts_new_sector: bool = False


def calculate_crc(data: List[int]) -> int:
    # (Function content unchanged)
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
        num_sectors: int,
        track_num_val: int,
        head_num_val: int,
        sector_size_val: int,
        sector_size_code_val: int
) -> List[SegmentDescriptor]:
    # (Function content unchanged)
    descriptors: List[SegmentDescriptor] = []
    descriptors.append(SegmentDescriptor(
        segment_type="GAP_FILL", name_template="Gap 1 (Post-Index)",
        length_type="FIXED", fixed_length=POST_INDEX_GAP_SIZE, args={'fill_byte': GAP_BYTE}
    ))
    for s_num in range(1, num_sectors + 1):
        descriptors.append(SegmentDescriptor(
            segment_type="SYNC_BYTES", name_template=f"Sector {s_num} ID Sync ({PRE_ID_SYNC_SIZE} x 0x{SYNC_BYTE:02X})",
            length_type="FIXED", fixed_length=PRE_ID_SYNC_SIZE, args={'sync_byte': SYNC_BYTE},
            starts_new_sector=True
        ))
        descriptors.append(SegmentDescriptor(
            segment_type="IDAM_DAM_PREAMBLE", name_template=f"Sector {s_num} ID Mark Preamble (3 x 0x{IDAM_BYTE:02X})",
            length_type="FIXED", fixed_length=3, args={'preamble_byte': IDAM_BYTE}
        ))
        descriptors.append(SegmentDescriptor(
            segment_type="ID_ADDRESS_MARK_PAYLOAD", name_template=f"Sector {s_num} ID Address Mark (0x{IDAM_MARKER:02X})",
            length_type="FIXED", fixed_length=1, args={'value': IDAM_MARKER}, is_id_crc_data_source=True
        ))
        descriptors.append(SegmentDescriptor(
            segment_type="ID_ADDRESS_MARK_PAYLOAD", name_template=f"Sector {s_num} Track Number ({track_num_val})",
            length_type="FIXED", fixed_length=1, args={'value': track_num_val}, is_id_crc_data_source=True
        ))
        descriptors.append(SegmentDescriptor(
            segment_type="ID_ADDRESS_MARK_PAYLOAD", name_template=f"Sector {s_num} Head Number ({head_num_val})",
            length_type="FIXED", fixed_length=1, args={'value': head_num_val}, is_id_crc_data_source=True
        ))
        descriptors.append(SegmentDescriptor(
            segment_type="ID_ADDRESS_MARK_PAYLOAD", name_template=f"Sector {s_num} Sector Number ({s_num})",
            length_type="FIXED", fixed_length=1, args={'value': s_num}, is_id_crc_data_source=True
        ))
        descriptors.append(SegmentDescriptor(
            segment_type="ID_ADDRESS_MARK_PAYLOAD", name_template=f"Sector {s_num} Size Code ({sector_size_code_val}={sector_size_val} bytes)",
            length_type="FIXED", fixed_length=1, args={'value': sector_size_code_val}, is_id_crc_data_source=True
        ))
        descriptors.append(SegmentDescriptor(
            segment_type="CRC_FIELD", name_template=f"Sector {s_num} ID CRC (0x{{crc_val:04X}})",
            length_type="FIXED", fixed_length=2, args={'crc_type': "ID"}
        ))
        descriptors.append(SegmentDescriptor(
            segment_type="GAP_FILL", name_template=f"Sector {s_num} Gap 2 (Post-ID, {POST_ID_GAP_SIZE} x 0x{GAP_BYTE:02X})",
            length_type="FIXED", fixed_length=POST_ID_GAP_SIZE, args={'fill_byte': GAP_BYTE}
        ))
        descriptors.append(SegmentDescriptor(
            segment_type="SYNC_BYTES", name_template=f"Sector {s_num} Data Sync ({PRE_DATA_SYNC_SIZE} x 0x{SYNC_BYTE:02X})",
            length_type="FIXED", fixed_length=PRE_DATA_SYNC_SIZE, args={'sync_byte': SYNC_BYTE}
        ))
        descriptors.append(SegmentDescriptor(
            segment_type="IDAM_DAM_PREAMBLE", name_template=f"Sector {s_num} Data Mark Preamble (3 x 0x{IDAM_BYTE:02X})",
            length_type="FIXED", fixed_length=3, args={'preamble_byte': IDAM_BYTE}
        ))
        descriptors.append(SegmentDescriptor(
            segment_type="DATA_ADDRESS_MARK_PAYLOAD", name_template=f"Sector {s_num} Data Address Mark (0x{DAM_MARKER:02X})",
            length_type="FIXED", fixed_length=1, args={'value': DAM_MARKER}, is_data_crc_data_source=True
        ))
        descriptors.append(SegmentDescriptor(
            segment_type="SECTOR_DATA_CONTENT", name_template=f"Sector {s_num} Data ({sector_size_val} bytes, ...)",
            length_type="SECTOR_SIZE_CONST", args={'size': sector_size_val}, is_data_crc_data_source=True
        ))
        descriptors.append(SegmentDescriptor(
            segment_type="CRC_FIELD", name_template=f"Sector {s_num} Data CRC (0x{{crc_val:04X}})",
            length_type="FIXED", fixed_length=2, args={'crc_type': "DATA"}
        ))
        if s_num < num_sectors:
            descriptors.append(SegmentDescriptor(
                segment_type="GAP_FILL", name_template=f"Sector {s_num} Gap 3 (Inter-Sector, {POST_DATA_GAP_SIZE} x 0x{GAP_BYTE:02X})",
                length_type="FIXED", fixed_length=POST_DATA_GAP_SIZE, args={'fill_byte': GAP_BYTE}
            ))
    descriptors.append(SegmentDescriptor(
        segment_type="GAP_FILL", name_template="Gap 4 (Pre-Index, {length} x 0x{fill_byte:02X})",
        length_type="GAP4_PLACEHOLDER", fixed_length=0, args={'fill_byte': GAP_BYTE}
    ))
    return descriptors

def _populate_annotations_from_structure(
        descriptors: List[SegmentDescriptor],
        target_track_size: int,
        sector_size_config: int) -> None:
    # (Function content unchanged)
    global g_annotations
    g_annotations = []
    current_pos = 0
    current_total_len_sans_gap4 = 0
    gap4_descriptor_index = -1
    for i, desc in enumerate(descriptors):
        length = 0
        if desc.length_type == "FIXED": length = desc.fixed_length or 0
        elif desc.length_type == "SECTOR_SIZE_CONST": length = desc.args.get('size', sector_size_config)
        elif desc.length_type == "GAP4_PLACEHOLDER": gap4_descriptor_index = i; continue
        current_total_len_sans_gap4 += length
    bytes_for_gap4 = target_track_size - current_total_len_sans_gap4
    final_descriptors = [replace(desc) for desc in descriptors]
    if gap4_descriptor_index != -1:
        if bytes_for_gap4 >= 0:
            final_descriptors[gap4_descriptor_index].fixed_length = bytes_for_gap4
            fill_byte_for_gap4 = final_descriptors[gap4_descriptor_index].args.get('fill_byte', GAP_BYTE)
            final_descriptors[gap4_descriptor_index].name_template = f"Gap 4 (Pre-Index, {bytes_for_gap4} x 0x{fill_byte_for_gap4:02X})"
        else:
            final_descriptors.pop(gap4_descriptor_index)
            bytes_for_gap4 = 0
    else:
        bytes_for_gap4 = 0
    current_total_length_with_gap4 = 0
    for desc in final_descriptors:
        if desc.length_type == "FIXED" or desc.length_type == "GAP4_PLACEHOLDER": current_total_length_with_gap4 += desc.fixed_length or 0
        elif desc.length_type == "SECTOR_SIZE_CONST": current_total_length_with_gap4 += desc.args.get('size', sector_size_config)
    if current_total_length_with_gap4 > target_track_size:
        temp_descriptors = []
        accumulated_len = 0
        for desc_orig in final_descriptors:
            desc = replace(desc_orig)
            desc_len_val = 0
            if desc.length_type == "FIXED" or desc.length_type == "GAP4_PLACEHOLDER": desc_len_val = desc.fixed_length or 0
            elif desc.length_type == "SECTOR_SIZE_CONST": desc_len_val = desc.args.get('size', sector_size_config)
            if accumulated_len + desc_len_val <= target_track_size:
                temp_descriptors.append(desc)
                accumulated_len += desc_len_val
            else:
                remaining_space = target_track_size - accumulated_len
                if remaining_space > 0 :
                    can_truncate_type = desc.segment_type in ["GAP_FILL", "SYNC_BYTES", "SECTOR_DATA_CONTENT"]
                    if can_truncate_type:
                        desc.fixed_length = remaining_space
                        desc.length_type = "FIXED"
                        if 'size' in desc.args : desc.args['size'] = remaining_space
                        desc.name_template += f" (truncated to {remaining_space}B)"
                        temp_descriptors.append(desc)
                        accumulated_len += remaining_space
                break
        final_descriptors = temp_descriptors
    current_pos = 0
    for desc in final_descriptors:
        current_segment_length = 0
        if desc.length_type == "FIXED" or desc.length_type == "GAP4_PLACEHOLDER": current_segment_length = desc.fixed_length or 0
        elif desc.length_type == "SECTOR_SIZE_CONST": current_segment_length = desc.args.get('size', sector_size_config)
        is_zero_len_gap4 = desc.length_type == "GAP4_PLACEHOLDER" and current_segment_length == 0
        segment_name = desc.name_template
        try:
            format_args = {**desc.args, 'crc_val': "----", 'length': current_segment_length}
            if desc.segment_type == "CRC_FIELD":
                template_no_format = segment_name.replace(":04X}", "}")
                segment_name = template_no_format.format(crc_val="----")
            elif desc.length_type == "GAP4_PLACEHOLDER":
                pass
            elif "{length}" in segment_name or "{fill_byte}" in segment_name:
                segment_name = segment_name.format(**format_args)
        except (KeyError, ValueError) as e:
            print(f"Warning: Error formatting annotation name '{segment_name}': {e}", file=sys.stderr)
            segment_name = desc.name_template
        if current_segment_length > 0 or is_zero_len_gap4:
            g_annotations.append({'offset': current_pos, 'length': current_segment_length,
                                  'name': segment_name,
                                  'starts_new_sector': desc.starts_new_sector})
            current_pos += current_segment_length
    g_annotations.sort(key=lambda x: int(x['offset']))


def generate_mfm_track(
        sectors: int = 16,
        track_num: int = 0,
        head_num: int = 0,
        sector_size_config: int = DEFAULT_SECTOR_SIZE,
        sector_size_code_config: int = DEFAULT_SECTOR_SIZE_CODE,
        generate_data_pattern: bool = True
) -> List[int]:
    # (Function content unchanged)
    global g_annotations, g_sectors, g_sector_size, g_sector_size_code
    descriptors = _define_track_segments_descriptors(
        sectors, track_num, head_num, sector_size_config, sector_size_code_config
    )
    _populate_annotations_from_structure(descriptors, TARGET_TRACK_SIZE, sector_size_config)
    track_data: List[int] = []
    if generate_data_pattern:
        current_pos = 0
        id_crc_accumulator: List[int] = []
        data_crc_accumulator: List[int] = []
        current_total_len_sans_gap4 = 0
        gap4_descriptor_index = -1
        for i, desc in enumerate(descriptors):
            length = 0
            if desc.length_type == "FIXED": length = desc.fixed_length or 0
            elif desc.length_type == "SECTOR_SIZE_CONST": length = desc.args.get('size', sector_size_config)
            elif desc.length_type == "GAP4_PLACEHOLDER": gap4_descriptor_index = i; continue
            current_total_len_sans_gap4 += length
        bytes_for_gap4 = TARGET_TRACK_SIZE - current_total_len_sans_gap4
        final_descriptors_for_data = [replace(desc) for desc in descriptors]
        if gap4_descriptor_index != -1:
            if bytes_for_gap4 >= 0:
                final_descriptors_for_data[gap4_descriptor_index].fixed_length = bytes_for_gap4
            else: final_descriptors_for_data.pop(gap4_descriptor_index)
        current_total_length_with_gap4 = 0
        for desc in final_descriptors_for_data:
            if desc.length_type == "FIXED" or desc.length_type == "GAP4_PLACEHOLDER": current_total_length_with_gap4 += desc.fixed_length or 0
            elif desc.length_type == "SECTOR_SIZE_CONST": current_total_length_with_gap4 += desc.args.get('size', sector_size_config)
        if current_total_length_with_gap4 > TARGET_TRACK_SIZE:
            temp_descriptors_for_data = []
            accumulated_len = 0
            for desc_orig in final_descriptors_for_data:
                desc = replace(desc_orig)
                desc_len_val = 0
                if desc.length_type == "FIXED" or desc.length_type == "GAP4_PLACEHOLDER": desc_len_val = desc.fixed_length or 0
                elif desc.length_type == "SECTOR_SIZE_CONST": desc_len_val = desc.args.get('size', sector_size_config)
                if accumulated_len + desc_len_val <= TARGET_TRACK_SIZE:
                    temp_descriptors_for_data.append(desc)
                    accumulated_len += desc_len_val
                else:
                    remaining_space = TARGET_TRACK_SIZE - accumulated_len
                    if remaining_space > 0 :
                        can_truncate_type = desc.segment_type in ["GAP_FILL", "SYNC_BYTES", "SECTOR_DATA_CONTENT"]
                        if can_truncate_type:
                            desc.fixed_length = remaining_space
                            desc.length_type = "FIXED"
                            if 'size' in desc.args : desc.args['size'] = remaining_space
                            temp_descriptors_for_data.append(desc)
                            accumulated_len += remaining_space
                    break
            final_descriptors_for_data = temp_descriptors_for_data
        for desc in final_descriptors_for_data:
            segment_bytes: List[int] = []
            current_segment_length = 0
            if desc.length_type == "FIXED" or desc.length_type == "GAP4_PLACEHOLDER": current_segment_length = desc.fixed_length or 0
            elif desc.length_type == "SECTOR_SIZE_CONST": current_segment_length = desc.args.get('size', sector_size_config)
            if current_segment_length == 0: continue
            if desc.segment_type == "GAP_FILL": segment_bytes = [desc.args.get('fill_byte', GAP_BYTE)] * current_segment_length
            elif desc.segment_type == "SYNC_BYTES": segment_bytes = [desc.args['sync_byte']] * current_segment_length
            elif desc.segment_type == "IDAM_DAM_PREAMBLE": segment_bytes = [desc.args['preamble_byte']] * current_segment_length
            elif desc.segment_type == "ID_ADDRESS_MARK_PAYLOAD" or desc.segment_type == "DATA_ADDRESS_MARK_PAYLOAD": segment_bytes = [desc.args['value']]
            elif desc.segment_type == "SECTOR_DATA_CONTENT": segment_bytes = [i for i in range(current_segment_length)]
            elif desc.segment_type == "CRC_FIELD":
                crc_input_list: List[int] = id_crc_accumulator if desc.args['crc_type'] == "ID" else data_crc_accumulator
                crc_val = calculate_crc(crc_input_list)
                segment_bytes = [(crc_val >> 8) & 0xFF, crc_val & 0xFF]
                if desc.args['crc_type'] == "ID": id_crc_accumulator = []
                if desc.args['crc_type'] == "DATA": data_crc_accumulator = []
            else: segment_bytes = [0] * current_segment_length
            if desc.is_id_crc_data_source: id_crc_accumulator.extend(segment_bytes)
            if desc.is_data_crc_data_source: data_crc_accumulator.extend(segment_bytes)
            track_data.extend(segment_bytes)
            current_pos += len(segment_bytes)
    g_sectors, g_sector_size, g_sector_size_code = sectors, sector_size_config, sector_size_code_config
    if generate_data_pattern:
        final_generated_len = len(track_data)
        if final_generated_len != TARGET_TRACK_SIZE :
            print(f"Warning: Final generated track data length {final_generated_len} (internal offset {current_pos}) does not match TARGET_TRACK_SIZE {TARGET_TRACK_SIZE}.", file=sys.stderr)
        elif current_pos != final_generated_len:
            print(f"Warning: Internal data generation offset {current_pos} does not match final generated length {final_generated_len}.", file=sys.stderr)
    return [b & 0xFF for b in track_data]


def get_annotated_line_segments(data: List[int], bytes_per_line: int) -> List[Dict]:
    # (Function content unchanged)
    global g_annotations
    line_segments = []
    hex_byte_width = 2
    hex_sep_width = 1
    anno_idx = 0
    current_segment_bytes = []
    current_segment_hex_strings = []
    current_segment_start_index = -1
    current_segment_indent_cols = 0
    current_segment_line_addr = 0
    current_segment_annotation: Optional[str] = None
    current_segment_annotation_details: Optional[AnnotationRecord] = None
    current_segment_type: Optional[str] = 'data'
    pending_multiline_end_info: Optional[Dict[str, Any]] = None
    i = 0
    while i < len(data):
        addr = i
        pos_in_line = addr % bytes_per_line
        line_addr = addr - pos_in_line
        text_for_this_segment_annotation: Optional[str] = None
        details_for_this_segment_annotation: Optional[AnnotationRecord] = None
        force_new_segment_break = False
        is_sector_start_trigger = False
        if pending_multiline_end_info and \
                line_addr <= pending_multiline_end_info['end_addr'] < line_addr + bytes_per_line:
            text_for_this_segment_annotation = "End: " + str(pending_multiline_end_info['record']['name'])
            details_for_this_segment_annotation = pending_multiline_end_info['record']
            pending_multiline_end_info = None
            if pos_in_line > 0:
                force_new_segment_break = True
        if not text_for_this_segment_annotation and anno_idx < len(g_annotations):
            while anno_idx < len(g_annotations):
                anno_s_offset = int(g_annotations[anno_idx]['offset'])
                anno_l_length = int(g_annotations[anno_idx]['length'])
                if anno_s_offset + anno_l_length <= i and anno_l_length > 0:
                    anno_idx += 1
                else:
                    break
            if anno_idx < len(g_annotations) and int(g_annotations[anno_idx]['offset']) == i:
                current_anno_record = g_annotations[anno_idx]
                text_for_this_segment_annotation = str(current_anno_record['name'])
                details_for_this_segment_annotation = current_anno_record
                if bool(current_anno_record.get('starts_new_sector', False)):
                    is_sector_start_trigger = True
                anno_l_val = int(current_anno_record['length'])
                if anno_l_val > bytes_per_line:
                    text_for_this_segment_annotation = "Start: " + text_for_this_segment_annotation
                    if not pending_multiline_end_info:
                        pending_multiline_end_info = {
                            'record': current_anno_record,
                            'end_addr': i + anno_l_val - 1
                        }
                if pos_in_line > 0:
                    force_new_segment_break = True
                anno_idx += 1
        if i == 0 or \
                (pos_in_line == 0 and (current_segment_bytes or current_segment_annotation)) or \
                force_new_segment_break or \
                (is_sector_start_trigger and i > 0):
            if current_segment_bytes or current_segment_annotation:
                line_segments.append({
                    'addr': current_segment_line_addr,
                    'start_index': current_segment_start_index,
                    'hex_bytes': list(current_segment_bytes),
                    'hex_strings': list(current_segment_hex_strings),
                    'indent_cols': current_segment_indent_cols,
                    'annotation': current_segment_annotation,
                    'annotation_details': current_segment_annotation_details,
                    'type': current_segment_type
                })
            if is_sector_start_trigger and i > 0:
                if not line_segments or line_segments[-1].get('type') != 'sector_separator_line':
                    line_segments.append({
                        'addr': line_addr,
                        'start_index': i,
                        'hex_bytes': [], 'hex_strings': [], 'indent_cols': 0,
                        'annotation': None, 'annotation_details': None,
                        'type': 'sector_separator_line'
                    })
            current_segment_bytes = []
            current_segment_hex_strings = []
            current_segment_start_index = i
            current_segment_line_addr = line_addr
            current_segment_indent_cols = pos_in_line * (hex_byte_width + hex_sep_width) if (force_new_segment_break or is_sector_start_trigger) and pos_in_line > 0 else 0
            current_segment_annotation = text_for_this_segment_annotation
            current_segment_annotation_details = details_for_this_segment_annotation
            current_segment_type = 'data'
        current_segment_bytes.append(data[i])
        current_segment_hex_strings.append(f"0x{data[i]:02X}")
        if not current_segment_annotation and text_for_this_segment_annotation:
            current_segment_annotation = text_for_this_segment_annotation
            current_segment_annotation_details = details_for_this_segment_annotation
        i += 1
    if current_segment_bytes or current_segment_annotation:
        if not current_segment_annotation and pending_multiline_end_info and \
                current_segment_line_addr <= pending_multiline_end_info['end_addr'] < current_segment_line_addr + len(current_segment_bytes) :
            current_segment_annotation = "End: " + str(pending_multiline_end_info['record']['name'])
            current_segment_annotation_details = pending_multiline_end_info['record']
        line_segments.append({
            'addr': current_segment_line_addr,
            'start_index': current_segment_start_index,
            'hex_bytes': list(current_segment_bytes),
            'hex_strings': list(current_segment_hex_strings),
            'indent_cols': current_segment_indent_cols,
            'annotation': current_segment_annotation,
            'annotation_details': current_segment_annotation_details,
            'type': 'data'
        })
    return line_segments


def generate_annotated_dump_lines(data: List[int]) -> List[str]:
    global DUMP_BYTES_PER_LINE
    output_lines = []
    addr_width = 4
    hex_byte_width = 2
    hex_sep_width = 1
    annotation_separator = " ; "
    sector_separator_char = "="

    line_segments = get_annotated_line_segments(data, DUMP_BYTES_PER_LINE)

    # Calculate fixed annotation start column
    hex_display_width = DUMP_BYTES_PER_LINE * (hex_byte_width + hex_sep_width) - hex_sep_width
    annotation_start_col = addr_width + 2 + hex_display_width + 1

    # Pass 1: Calculate max line length
    max_line_len = 0
    for segment_idx, segment in enumerate(line_segments):
        current_line_len = 0
        line_start_addr = segment['addr']

        if segment.get('type') == 'sector_separator_line':
            sector_info_str = ""
            # Look ahead to get sector info for the separator
            next_data_idx = segment_idx + 1
            if next_data_idx < len(line_segments) and line_segments[next_data_idx]['annotation_details']:
                name = str(line_segments[next_data_idx]['annotation_details'].get('name',''))
                match = re.search(r"Sector (\d+)", name)
                if match: sector_info_str = f" Sector {match.group(1)} "

            prefix_len = addr_width + 2 + len(annotation_separator)
            core_len = len(sector_info_str)
            # Estimate required padding (at least 10 chars total for separator part)
            sep_len_estimate = max(10, core_len + 4) # Add some padding around core
            current_line_len = prefix_len + sep_len_estimate
        else:
            hex_dump_part_list = [f"{byte_val:02X}" for byte_val in segment['hex_bytes']]
            hex_dump_part = " ".join(hex_dump_part_list)
            actual_hex_chars_width = len(hex_dump_part)
            indent_chars = segment['indent_cols']
            line_prefix_len = addr_width + 2 + indent_chars + actual_hex_chars_width
            padding_needed = annotation_start_col - line_prefix_len
            padding = max(0, padding_needed)
            current_line_len = line_prefix_len + padding
            if segment['annotation']:
                anno_len = len(annotation_separator) + len(str(segment['annotation']))
                current_line_len += anno_len
            else:
                current_line_len = max(current_line_len, annotation_start_col)
        max_line_len = max(max_line_len, current_line_len)

    # Pass 2: Format lines using max_line_len and fixed annotation column
    for segment_idx, segment in enumerate(line_segments):
        line_start_addr = segment['addr']

        if segment.get('type') == 'sector_separator_line':
            prefix = f"{line_start_addr:0{addr_width}X}:{annotation_separator}"
            # --- Add Sector Info to Separator ---
            sector_info_str = ""
            next_data_idx = segment_idx + 1
            if next_data_idx < len(line_segments) and line_segments[next_data_idx]['annotation_details']:
                name = str(line_segments[next_data_idx]['annotation_details'].get('name',''))
                match = re.search(r"Sector (\d+)", name)
                if match: sector_info_str = f" Sector {match.group(1)} "
            # --- Calculate Centered Separator ---
            core_len = len(sector_info_str)
            total_sep_chars_needed = max_line_len - len(prefix)
            padding_chars_needed = max(0, total_sep_chars_needed - core_len)
            left_padding = padding_chars_needed // 2
            right_padding = padding_chars_needed - left_padding
            separator_line = prefix + (sector_separator_char * left_padding) + sector_info_str + (sector_separator_char * right_padding)
            # --- End Separator Calculation ---
            output_lines.append(separator_line)
            continue

        indent_chars = segment['indent_cols']
        hex_dump_part_list = [f"{byte_val:02X}" for byte_val in segment['hex_bytes']]
        hex_dump_part = " ".join(hex_dump_part_list)

        line_prefix = f"{line_start_addr:0{addr_width}X}: {' ' * indent_chars}{hex_dump_part}"
        current_prefix_len = len(line_prefix)

        line_suffix = ""
        if segment['annotation']:
            line_suffix = annotation_separator + str(segment['annotation'])

        padding_needed = annotation_start_col - current_prefix_len
        min_padding = 1 if segment['hex_bytes'] and segment['annotation'] else 0
        padding = " " * max(min_padding, padding_needed)

        line = line_prefix + padding + line_suffix
        final_padding = " " * (max_line_len - len(line))
        line += final_padding

        output_lines.append(line.rstrip())

    return output_lines


def print_annotated_hex_dump(data: List[int]) -> None:
    # (Function content unchanged)
    output_lines = generate_annotated_dump_lines(data)
    for line in output_lines:
        print(line)
    print("\n--- End of Track ---")

def generate_cpp_header(track_data: List[int], var_name: str = "floppyTrackData") -> str:
    # (Function content unchanged)
    data_len = len(track_data)
    global g_sectors, g_sector_size, g_sector_size_code
    cpp_bytes_per_line = 16
    header = f"""#ifndef MFM_FLOPPY_TRACK_H_{var_name.upper()}
#define MFM_FLOPPY_TRACK_H_{var_name.upper()}

#include <array>
#include <cstdint>

// MFM encoded floppy track data ({data_len} bytes)
// Format: {g_sectors} sectors, {g_sector_size} bytes/sector (Size Code {g_sector_size_code})
// Target: WD1793 / IBM MFM DD
// Generated by Python script
constexpr std::array<uint8_t, {data_len}> {var_name} = {{
"""
    for i in range(0, data_len, cpp_bytes_per_line):
        row = track_data[i:i+cpp_bytes_per_line]
        hex_values = [f"0x{b:02X}" for b in row]
        header += "    " + ", ".join(hex_values)
        if i + len(row) < data_len:
            header += ","
        header += "\n"
    header += "}};\n\n#endif // MFM_FLOPPY_TRACK_H_{var_name.upper()}\n"
    return header

def generate_cpp_header_annotated(track_data: List[int], var_name: str = "floppyTrackData") -> str:
    # (Function content unchanged from previous version - fixed alignment)
    global DUMP_BYTES_PER_LINE, TARGET_LINE_WIDTH
    data_len = len(track_data)
    global g_sectors, g_sector_size, g_sector_size_code
    base_cpp_indent_str = "    "
    base_cpp_indent_len = len(base_cpp_indent_str)
    cpp_byte_width = 4
    cpp_byte_width_comma = 5
    cpp_byte_separator_width = 1
    cpp_separator_char = "="
    cpp_comment_prefix = "// "
    if DUMP_BYTES_PER_LINE > 0:
        max_cpp_hex_data_width = (DUMP_BYTES_PER_LINE - 1) * (cpp_byte_width_comma + cpp_byte_separator_width) + cpp_byte_width if DUMP_BYTES_PER_LINE > 1 else cpp_byte_width
    else:
        max_cpp_hex_data_width = 0
    comment_start_col = base_cpp_indent_len + max_cpp_hex_data_width + 2
    line_segments = get_annotated_line_segments(track_data, DUMP_BYTES_PER_LINE)
    cpp_lines = []
    cpp_lines.append(f"#ifndef MFM_FLOPPY_TRACK_H_{var_name.upper()}_ANNO")
    cpp_lines.append(f"#define MFM_FLOPPY_TRACK_H_{var_name.upper()}_ANNO")
    cpp_lines.append("")
    cpp_lines.append("#include <array>")
    cpp_lines.append("#include <cstdint>")
    cpp_lines.append("")
    cpp_lines.append(f"// MFM encoded floppy track data ({data_len} bytes) with annotations")
    cpp_lines.append(f"// Format: {g_sectors} sectors, {g_sector_size} bytes/sector (Size Code {g_sector_size_code})")
    cpp_lines.append(f"// Target: WD1793 / IBM MFM DD")
    cpp_lines.append(f"// Generated by Python script (data layout: {DUMP_BYTES_PER_LINE} bytes/line)")
    cpp_lines.append(f"constexpr std::array<uint8_t, {data_len}> {var_name} = {{")
    processed_data_segment_yet = False
    for segment_idx, segment in enumerate(line_segments):
        is_sector_start = False
        current_details = segment.get('annotation_details')
        if current_details and isinstance(current_details, dict) and \
                bool(current_details.get('starts_new_sector', False)):
            if segment['hex_bytes'] or segment['annotation']:
                is_sector_start = True
        if is_sector_start and processed_data_segment_yet:
            sector_info_str = ""
            name = str(current_details.get('name', ''))
            match = re.search(r"Sector (\d+)", name)
            if match: sector_info_str = f" Sector {match.group(1)} "
            total_comment_len_target = TARGET_LINE_WIDTH - base_cpp_indent_len
            core_len = len(sector_info_str)
            padding_len = max(0, total_comment_len_target - len(cpp_comment_prefix) - core_len)
            left_padding = padding_len // 2
            right_padding = padding_len - left_padding
            cpp_separator_line = f"{base_cpp_indent_str}{cpp_comment_prefix}" \
                                 f"{cpp_separator_char * left_padding}" \
                                 f"{sector_info_str}" \
                                 f"{cpp_separator_char * right_padding}"
            if cpp_lines and cpp_lines[-1].strip() != "" and not cpp_lines[-1].strip().startswith("//"):
                cpp_lines.append("")
            cpp_lines.append(cpp_separator_line)
        if segment.get('type') == 'sector_separator_line':
            continue
        line_content_prefix = base_cpp_indent_str
        pos_in_line_cpp = segment['start_index'] % DUMP_BYTES_PER_LINE
        cpp_indent_chars = 0
        num_skipped_bytes_on_line = pos_in_line_cpp
        if num_skipped_bytes_on_line > 0:
            cpp_indent_chars = num_skipped_bytes_on_line * (cpp_byte_width_comma + cpp_byte_separator_width)
        line_content_prefix += " " * cpp_indent_chars
        hex_part = ""
        if segment['hex_bytes']:
            segment_hex_parts = []
            for idx, _ in enumerate(segment['hex_bytes']):
                byte_val = segment['hex_bytes'][idx]
                hex_str_val = f"0x{byte_val:02X}"
                byte_index = segment['start_index'] + idx
                comma = "," if byte_index < data_len - 1 else ""
                segment_hex_parts.append(hex_str_val + comma)
            hex_part = " ".join(segment_hex_parts)
        current_code_part = line_content_prefix + hex_part
        comment_text_part = ""
        if segment['annotation']:
            effective_annotation_text = str(segment['annotation'])
            anno_s, anno_l, anno_e = -1,-1,-1
            if segment['annotation_details']:
                anno_details_dict = segment['annotation_details']
                anno_s = int(anno_details_dict['offset'])
                anno_l = int(anno_details_dict['length'])
                anno_e = anno_s + anno_l -1 if anno_l > 0 else anno_s
            if anno_s != -1:
                comment_text_part = f"{cpp_comment_prefix}0x{anno_s:04X}-0x{anno_e:04X} ({anno_l}B): {effective_annotation_text}"
            else:
                comment_text_part = f"{cpp_comment_prefix}{effective_annotation_text}"
        padding_needed = comment_start_col - len(current_code_part)
        padding = " " * max(0, padding_needed) if comment_text_part else ""
        final_line = current_code_part + padding + comment_text_part
        if final_line.strip():
            cpp_lines.append(final_line.rstrip())
            if segment['hex_bytes'] or segment['annotation']:
                processed_data_segment_yet = True
    if cpp_lines and cpp_lines[-1].strip().startswith("// ==="):
        cpp_lines.pop()
        if cpp_lines and cpp_lines[-1].strip() == "":
            cpp_lines.pop()
    cpp_lines.append("}};")
    cpp_lines.append("")
    cpp_lines.append(f"#endif // MFM_FLOPPY_TRACK_H_{var_name.upper()}_ANNO")
    return "\n".join(cpp_lines)

# --- Read/Write Functions ---

def write_mfm(filename: str, track_data: List[int]) -> None:
    # (Function content unchanged)
    print(f"Attempting to write {len(track_data)} bytes to '{filename}'...")
    try:
        with open(filename, 'wb') as f:
            f.write(bytes(track_data))
        print(f"Successfully wrote MFM track data to '{filename}'.")
    except IOError as e:
        print(f"Error writing file '{filename}': {e}", file=sys.stderr)
    except Exception as e:
        print(f"An unexpected error occurred during write: {e}", file=sys.stderr)

def read_mfm(filename: str) -> Optional[List[int]]:
    # (Function content unchanged)
    print(f"Attempting to read MFM track data from '{filename}'...")
    if not os.path.exists(filename):
        print(f"Error: File not found '{filename}'", file=sys.stderr)
        return None
    try:
        with open(filename, 'rb') as f:
            byte_data = f.read()
        track_data = list(byte_data)
        print(f"Successfully read {len(track_data)} bytes from '{filename}'.")
        return track_data
    except IOError as e:
        print(f"Error reading file '{filename}': {e}", file=sys.stderr)
        return None
    except Exception as e:
        print(f"An unexpected error occurred during read: {e}", file=sys.stderr)
        return None

def print_simple_hex_dump(data: List[int], bytes_per_line: int = 16) -> None:
    # (Function content unchanged)
    addr_width = 4
    for i in range(0, len(data), bytes_per_line):
        chunk = data[i:i+bytes_per_line]
        hex_part = " ".join(f"{b:02X}" for b in chunk)
        ascii_part = "".join(chr(b) if 32 <= b <= 126 else '.' for b in chunk)
        print(f"{i:0{addr_width}X}: {hex_part:<{bytes_per_line*3 - 1}} |{ascii_part}|")
    print(f"--- End of dump ({len(data)} bytes) ---")


def main():
    global DUMP_BYTES_PER_LINE

    # --- Configuration for All Annotated Dump Outputs ---
    # The global DUMP_BYTES_PER_LINE at the top of the file is the primary default.
    # You can override it here for a specific run if needed:
    # DUMP_BYTES_PER_LINE = 16
    # DUMP_BYTES_PER_LINE = 8

    print(f"Annotated dump line length (console and C++) set to: {DUMP_BYTES_PER_LINE} bytes/line.")
    print(f"Target C++ separator/line width set to: {TARGET_LINE_WIDTH} characters.")

    sectors_per_track_cfg = 16
    track_num_to_gen_cfg = 0
    head_num_cfg = 0
    sector_size_val = DEFAULT_SECTOR_SIZE
    sector_size_code_val = DEFAULT_SECTOR_SIZE_CODE

    # --- Generate Track Data and Annotations ---
    track_data = generate_mfm_track(
        sectors=sectors_per_track_cfg,
        track_num=track_num_to_gen_cfg,
        head_num=head_num_cfg,
        sector_size_config=sector_size_val,
        sector_size_code_config=sector_size_code_val,
        generate_data_pattern=True
    )

    print(f"Generated track data: {len(track_data)} bytes for Track {track_num_to_gen_cfg}")
    print(f"Configuration: {g_sectors} Sectors, {g_sector_size} Bytes/Sector (Code {g_sector_size_code})")
    print(f"Target Length: {TARGET_TRACK_SIZE} bytes")
    if len(track_data) != TARGET_TRACK_SIZE:
        print(f"Note: Final track length {len(track_data)} differs from target {TARGET_TRACK_SIZE} bytes (may be due to truncation).", file=sys.stderr)

    # --- Print Outputs Based on Generated Data ---
    print("\nAnnotated Hex Dump (Full Track - Generated):")
    print_annotated_hex_dump(track_data)

    variable_name = f"floppyTrackData_T{track_num_to_gen_cfg}_S{g_sectors}x{g_sector_size}"
    header_content = generate_cpp_header(track_data, var_name=variable_name)
    header_annotated_content = generate_cpp_header_annotated(
        track_data,
        var_name=variable_name + "_Anno"
    )

    print(f"\n--- C++ Header Content ({variable_name}) ---")
    print(header_content)
    print(f"--- End C++ Header Content ---")

    print(f"\n--- C++ Annotated Header Initialization ({variable_name}_Anno) ---")
    print(header_annotated_content)
    print(f"--- End C++ Annotated Header Initialization ---")

    # --- Usage Examples for Read/Write ---

    # == Example 1: Write generated track to file ==
    # output_filename = f"track_{track_num_to_gen_cfg}_generated.img"
    # write_mfm(output_filename, track_data)

    # == Example 2: Read track from file and print annotated dump ==
    # # Assumes the file from Example 1 exists or another compatible file
    # input_filename = f"track_{track_num_to_gen_cfg}_generated.img"
    # read_track_data = read_mfm(input_filename)
    # if read_track_data:
    #     # Ensure the read data has the expected length before trying to annotate
    #     if len(read_track_data) != TARGET_TRACK_SIZE:
    #         print(f"\nWarning: Read data length ({len(read_track_data)}) does not match expected target size ({TARGET_TRACK_SIZE}). Annotation might be incorrect.", file=sys.stderr)

    #     print(f"\n--- Annotated Hex Dump of Read Data from '{input_filename}' ---")

    #     # 1. Define the expected structure descriptors using the config expected for the file
    #     descriptors = _define_track_segments_descriptors(
    #         num_sectors=sectors_per_track_cfg,
    #         track_num_val=track_num_to_gen_cfg, # Use track num expected IN THE FILE for annotation names
    #         head_num_val=head_num_cfg,         # Use head num expected IN THE FILE
    #         sector_size_val=sector_size_val,
    #         sector_size_code_val=sector_size_code_val
    #     )

    #     # 2. Populate global annotations based on the defined structure (calculates offsets, lengths, placeholder names)
    #     _populate_annotations_from_structure(
    #         descriptors,
    #         TARGET_TRACK_SIZE,
    #         sector_size_val
    #     )

    #     # 3. Print the actual read data using the generated structural annotations
    #     # Note: Annotation names for dynamic fields like CRC will show placeholders ("----").
    #     #       Annotation names for Track/Head/Sector will show the values passed to _define_... above,
    #     #       which might not match the actual data in the file's ID fields if it's from a different track.
    #     print_annotated_hex_dump(read_track_data)
    # else:
    #      print(f"Could not read or process data from '{input_filename}'.")


if __name__ == "__main__":
    main()