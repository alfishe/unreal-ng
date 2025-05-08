// ImHex Pattern for a raw MFM Disk Image

#pragma endian big

import std.core;
import std.mem;
import std.io;

namespace mfm_raw_disk_format
{
// --- Native CRC-16-CCITT Function ---
fn crc16_ccitt_pl(u64 address, u64 length)
{
    u16 crc = 0xFFFF; const u16 polynomial = 0x1021; u64 current_addr = address; u64 end_addr = address + length;
    while (current_addr < end_addr)
    {
        u16 byte_val = u16(std::mem::read_unsigned(current_addr, 1)); crc ^= (byte_val << 8);
        for (u8 bit_index = 0, bit_index < 8, bit_index += 1)
        {
            if ((crc & 0x8000) != 0) { crc = ((crc << 1) & 0xFFFF) ^ polynomial; } else { crc = (crc << 1) & 0xFFFF; }
        }

        current_addr += 1;
    }

    return crc;
};

// --- Constants ---
const u32 MFM_TRACK_SIZE = 6250;
const u32 TRACK_NUM_SECTORS = 16;
const u32 TRACK_SECTOR_DATA_SIZE = 256;
const u32 SIZE_POST_INDEX_GAP = 80;
const u32 SIZE_PRE_ID_SYNC = 12;
const u32 SIZE_IDAM_DAM_PREAMBLE = 3;
const u32 SIZE_ID_PAYLOAD = 4;
const u32 SIZE_CRC = 2;
const u32 SIZE_POST_ID_GAP = 22;
const u32 SIZE_PRE_DATA_SYNC = 12;
const u32 SIZE_POST_DATA_GAP = 54;
const u32 SIZE_ID_MARK_BLOCK = 1 + SIZE_ID_PAYLOAD + SIZE_CRC;
const u32 SIZE_DATA_MARK_AND_PAYLOAD = 1 + TRACK_SECTOR_DATA_SIZE;

const u8  MFM_GAP_BYTE = 0x4E;
const u8  MFM_SYNC_BYTE = 0x00;
const u8  MFM_PREAMBLE_BYTE = 0xA1;
const u8  MFM_IDAM_MARKER = 0xFE;
const u8  MFM_DAM_MARKER = 0xFB;

// --- Calculated Composite & Gap 4 Size ---
const u32 SIZE_ID_FIELD_BLOCK = SIZE_PRE_ID_SYNC + SIZE_IDAM_DAM_PREAMBLE + SIZE_ID_MARK_BLOCK;
const u32 SIZE_DATA_FIELD_BLOCK = SIZE_PRE_DATA_SYNC + SIZE_IDAM_DAM_PREAMBLE + SIZE_DATA_MARK_AND_PAYLOAD + SIZE_CRC;
const u32 SIZE_SECTOR_BODY = SIZE_ID_FIELD_BLOCK + SIZE_POST_ID_GAP + SIZE_DATA_FIELD_BLOCK;
const u32 SIZE_ALL_SECTORS_STRUCTURE = ((TRACK_NUM_SECTORS - 1) * (SIZE_SECTOR_BODY + SIZE_POST_DATA_GAP)) + SIZE_SECTOR_BODY;
const u32 SIZE_FIXED_PARTS_BEFORE_GAP4 = SIZE_POST_INDEX_GAP + SIZE_ALL_SECTORS_STRUCTURE;
const u32 SIZE_CALCULATED_GAP4 = (MFM_TRACK_SIZE >= SIZE_FIXED_PARTS_BEFORE_GAP4) ? (MFM_TRACK_SIZE - SIZE_FIXED_PARTS_BEFORE_GAP4) : 0;

// --- Structures ---

struct IDAMPPayload
{
    u8 track_num        [[name("Track Number"), comment("Cylinder number (0-based)")]];
    u8 head_num         [[name("Head Number"), comment("Head/Side number (0 or 1)")]];
    u8 sector_num       [[name("Sector Number"), comment("Sector number on track (1-based)")]];
    u8 sector_size_code [[name("Sector Size Code"), comment("Code for sector size (0=128, 1=256, 2=512, 3=1024)")]];
};

struct IDAddressMarkBlock
{
    u8 idam_marker [[name(std::format("ID Address Mark (IDAM = 0x{:02X})", MFM_IDAM_MARKER)), comment("Special byte (0xFE) indicating start of ID field")]];
    IDAMPPayload id_payload [[name("ID Payload"), comment("Address information for this sector")]];
    u16 id_crc [[ name("ID CRC (16-bit)"), comment("CRC-16-CCITT covering ID Address Mark and ID Payload"), color("FF0000") ]]; // Added color attribute
};

struct IDField
{
    u8 sync[SIZE_PRE_ID_SYNC] [[name(std::format("ID Sync ({} x 0x{:02X})", SIZE_PRE_ID_SYNC, MFM_SYNC_BYTE)), comment("Synchronization bytes before ID field (Always filled with 0x00)")]];
    u8 preamble[SIZE_IDAM_DAM_PREAMBLE] [[name(std::format("ID Mark Preamble ({} x 0x{:02X})", SIZE_IDAM_DAM_PREAMBLE, MFM_PREAMBLE_BYTE)), comment("Preamble bytes before ID Address Mark (Always filled with 0xA1)")]];
    IDAddressMarkBlock id_mark_block [[name("ID Address Mark Block"), comment("Contains IDAM, address info, and ID CRC")]];
};

struct DataField
{
    u8 sync[SIZE_PRE_DATA_SYNC] [[name(std::format("Data Sync ({} x 0x{:02X})", SIZE_PRE_DATA_SYNC, MFM_SYNC_BYTE)), comment("Synchronization bytes before Data field (Always filled with 0x00)")]];
    u8 preamble[SIZE_IDAM_DAM_PREAMBLE] [[name(std::format("Data Mark Preamble ({} x 0x{:02X})", SIZE_IDAM_DAM_PREAMBLE, MFM_PREAMBLE_BYTE)), comment("Preamble bytes before Data Address Mark (Always filled with 0xA1)")]];
    u8 dam_marker [[name(std::format("Data Address Mark (DAM = 0x{:02X})", MFM_DAM_MARKER)), comment("Special byte (0xFB) indicating start of Data field")]];
    u8 sector_data[TRACK_SECTOR_DATA_SIZE] [[name(std::format("Sector Data ({} bytes)", TRACK_SECTOR_DATA_SIZE)), comment("Actual user data for the sector")]];
    u16 data_crc [[ name("Data CRC (16-bit)"), comment("CRC-16-CCITT covering Data Address Mark and Sector Data"), color("FF0000") ]]; // Added color attribute
};

struct SectorUnit {
    IDField id_field [[name(std::format("Sector {} ID Field", std::core::array_index() + 1)), comment("Identifier and address field for this sector")]];
    u8 gap2[SIZE_POST_ID_GAP] [[name(std::format("Gap 2 ({} x 0x{:02X})", SIZE_POST_ID_GAP, MFM_GAP_BYTE)), comment("Gap between ID field and Data field (Always filled with 0x4E)")]];
    DataField data_field [[name(std::format("Sector {} Data Field", std::core::array_index() + 1)), comment("Data content field for this sector")]];
    if (std::core::array_index() < (TRACK_NUM_SECTORS - 1)) {
        u8 gap3[SIZE_POST_DATA_GAP] [[name(std::format("Gap 3 (Inter-Sector, {} x 0x{:02X})", SIZE_POST_DATA_GAP, MFM_GAP_BYTE)), comment("Gap between this sector's data and the next sector's ID (Always filled with 0x4E)")]];
    }
};

struct EmptyPlaceholder {};

struct MFMTrack
{
    u8 gap1[SIZE_POST_INDEX_GAP] [[name(std::format("Gap 1 ({} x 0x{:02X})", SIZE_POST_INDEX_GAP, MFM_GAP_BYTE)), comment("Gap after the physical index pulse, before first sector (Always filled with 0x4E)")]];
    SectorUnit sectors[TRACK_NUM_SECTORS] [[name("Sectors"), comment("Track sectors")]];
    if (SIZE_CALCULATED_GAP4 > 0)
    {
        u8 gap4[SIZE_CALCULATED_GAP4] [[name(std::format("Gap 4 (Pre-Index, {} x 0x{:02X})", SIZE_CALCULATED_GAP4, MFM_GAP_BYTE)), comment("Gap after last sector, before physical index pulse (Always filled with 0x4E)")]];
    }
    else if (SIZE_CALCULATED_GAP4 == 0 && MFM_TRACK_SIZE == SIZE_FIXED_PARTS_BEFORE_GAP4)
    {
        EmptyPlaceholder gap4_info @ "Gap 4 (0 bytes)" [[comment("Track ends exactly after last sector, no Gap 4 needed")]];
    }
} [[static]];

struct MFMDisk
{
    u64 track_count = std::mem::size() / MFM_TRACK_SIZE;
    MFMTrack tracks[track_count] [[name(std::format("MFM Tracks ({})", track_count)), comment("List of MFM track structures in the image")]];
    u64 consumed_by_tracks = track_count * MFM_TRACK_SIZE;
    u64 remaining_bytes = std::mem::size() - consumed_by_tracks;
    if (remaining_bytes > 0)
    {
        u8 trailing_data[remaining_bytes] [[name(std::format("Trailing Data ({} bytes)", remaining_bytes)), comment("Extra data at the end of the file beyond the last full track")]];
    }
};

} // namespace mfm_raw_disk_format

// Entry point
mfm_raw_disk_format::MFMDisk disk_image @ 0x00;