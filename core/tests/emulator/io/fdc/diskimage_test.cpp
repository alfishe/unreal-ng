#include <gtest/gtest.h>

#include <cstddef> // offsetof macro
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip> // for std::hex, std::setw, std::setfill
#include <set>
#include <cstdint>

// Helper function to encode pattern/// @brief Encodes cylinder, head, sector, and counter into a 16-bit pattern
/// @param track Track number (0-127, 7 bits)
/// @param head Head/disk side (0-1, 1 bit)
/// @param sector Sector number (0-15, 4 bits)
/// @param counter Sequence counter (0-15, 4 bits)
/// @return Packed 16-bit value with the following bit layout:
///         [15]   [14:8]   [7]   [6:3]   [2:0]
///         Head | Track | Head | Sector | Counter
/// @note Bit breakdown:
///       - Bits 15:    Head (1 bit)
///       - Bits 14-8:  Track number (7 bits)
///       - Bits 7:     Head (1 bit) - repeated for easier decoding
///       - Bits 6-3:   Sector (4 bits)
///       - Bits 2-0:   Counter (3 bits)
/// @warning Input values beyond their bit ranges will be truncated
uint16_t encodePattern(uint8_t track, uint8_t head, uint8_t sector, uint8_t counter)
{
    // Pattern: track (7 bits) + head (1 bit) + sector (4 bits) + counter (4 bits)
    uint16_t pattern = ((track & 0x7F) << 8) | 
                      ((head & 0x01) << 15) |
                      ((head & 0x01) << 7) |
                      ((sector & 0x0F) << 3) |
                      (counter & 0x07);
    return pattern;
}

/// @brief Decodes a 16-bit pattern into track, head, sector, and counter components
/// @param pattern Packed 16-bit value created by encodePattern()
/// @return Human-readable string with format:
///         "Track: X, Head: Y, Sector: Z, Counter: W"
/// @note Reverse operation of encodePattern(), extracts:
///       - Track:    Bits 14-8 (7 bits)
///       - Head:     Bits 15 & 7 (1 bit)
///       - Sector:   Bits 6-3 (4 bits)
///       - Counter:  Bits 2-0 (3 bits)
/// @example decodePattern(0x7B35) might return "Track: 123, Head: 1, Sector: 5, Counter: 5"
std::string decodePattern(uint16_t pattern)
{
    // Extract pattern components
    uint8_t track = (pattern & 0x7F00) >> 8;   // 7 bits, shifted right by 8
    uint8_t head = (pattern & 0x8000) >> 15;   // 1 bit, shifted right by 15
    uint8_t sector = (pattern & 0x00F8) >> 3;  // 4 bits, shifted right by 3
    uint8_t counter = pattern & 0x0007;        // 3 bits
    
    std::stringstream ss;
    ss << "Track: " << static_cast<int>(track) 
       << ", Head: " << static_cast<int>(head) 
       << ", Sector: " << static_cast<int>(sector) 
       << ", Counter: " << static_cast<int>(counter);
    return ss.str();
}

#include <common/dumphelper.h>
#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"

// Forward declare Track type from DiskImage
using Track = DiskImage::Track;

/// region <Test types>

class DiskImage_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;

protected:
    void SetUp() override
    {

    };

    void TearDown() override
    {

    }
};

/// endregion <Test types>

TEST_F(DiskImage_Test, StructureSizes)
{
    size_t addressMarkRecordSize = sizeof(DiskImage::AddressMarkRecord);
    EXPECT_EQ(addressMarkRecordSize, 7);

    size_t rawSectorBytesSize = sizeof(DiskImage::RawSectorBytes);
    EXPECT_EQ(rawSectorBytesSize, 388);

    size_t rawTrackSize = sizeof(DiskImage::RawTrack);
    EXPECT_EQ(rawTrackSize, DiskImage::FullTrack::RAW_TRACK_SIZE);
}

TEST_F(DiskImage_Test, SectorAccessConsistency)
{
    // Create a disk with 1 cylinder and 1 head
    DiskImage disk(1, 1);
    Track* track = disk.getTrack(0);

    // Access the interleave table directly (1:1 mapping by default)
    const uint8_t (&interleave)[Track::SECTORS_PER_TRACK] = track->sectorInterleaveTable;

    // Map to store physical sector -> buffer address (1-based)
    std::map<uint8_t, uint8_t*> physicalSectorToBuffer;

    // 1. Record all raw sector buffer addresses (using physical sector numbers)
    for (uint8_t physicalSector = 0; physicalSector < Track::SECTORS_PER_TRACK; physicalSector++)
    {
        DiskImage::RawSectorBytes* rawSector = track->getRawSector(physicalSector); // 0-based index
        ASSERT_NE(rawSector, nullptr) << "Null raw sector for physical sector " << (int)physicalSector;
        physicalSectorToBuffer[physicalSector] = rawSector->data;

        // Debug output
        /*
        std::cout << "Physical sector " << (int)physicalSector
                  << " at buffer: " << (void*)rawSector->data
                  << " (interleave table maps to logical sector "
                  << (int)(physicalSector) << ")" << std::endl;
        */
    }

    // 2. Verify getDataForSector returns correct buffers according to interleave
    for (uint8_t logicalSector = 1; logicalSector <= Track::SECTORS_PER_TRACK; logicalSector++)
    {
        uint8_t physicalSector = interleave[logicalSector - 1] + 1; // Table is 0-based
        uint8_t* expectedBuffer = physicalSectorToBuffer[physicalSector];

        uint8_t* actualBuffer = track->getDataForSector(logicalSector);

        EXPECT_EQ(actualBuffer, expectedBuffer)
            << "Buffer mismatch for:\n"
            << "Logical sector: " << (int)logicalSector << "\n"
            << "Mapped physical sector: " << (int)physicalSector << "\n"
            << "Expected buffer: " << (void*)expectedBuffer << "\n"
            << "Actual buffer: " << (void*)actualBuffer;
    }
}

TEST_F(DiskImage_Test, TrackPositioning)
{
    // Create a disk image with 80 tracks and 2 heads
    DiskImage diskImage(80, 2);
    const uint8_t numTracks = diskImage.getCylinders();
    const uint8_t numHeads = diskImage.getSides();
    
    // Map to store track/head combinations and their buffer addresses
    std::map<std::pair<uint8_t, uint8_t>, const void*> trackHeadToBufferMap;
    
    // Test all tracks and heads
    for (uint8_t track = 0; track < numTracks; ++track)
    {
        for (uint8_t head = 0; head < numHeads; ++head)
        {
            // Get track pointer
            const Track* trackBuffer = diskImage.getTrackForCylinderAndSide(track, head);
            
            // Verify track pointer is not null
            ASSERT_NE(trackBuffer, nullptr) << "Track pointer for track " << (int)(track)
                                       << ", head " << (int)(head) << " is null";
            
            // Create track/head pair
            std::pair<uint8_t, uint8_t> trackHeadPair(track, head);
            
            // Verify this track/head combination hasn't been seen before
            ASSERT_EQ(trackHeadToBufferMap.count(trackHeadPair), 0)
                << "Duplicate track/head combination: track " << (int)(track)
                << ", head " << (int)(head);
            
            // Store the buffer address in the map
            trackHeadToBufferMap[trackHeadPair] = trackBuffer;

#ifdef _DEBUG
            //std::cout << "Buffer pointer for track: " << (int)(track) << ", head: " << (int)(head) << " " << trackBuffer << std::endl;
#endif // _DEBUG
        }
    }
    
    // Verify that all track pointers are unique and have correct spacing
    std::set<const void*> uniquePointers;
    std::vector<const void*> sortedPointers;
    
    // First collect all pointers
    for (const auto& entry : trackHeadToBufferMap)
    {
        const void* ptr = entry.second;
        sortedPointers.push_back(ptr);
        
        if (!uniquePointers.insert(ptr).second)
        {
            std::stringstream ss;
            ss << "Duplicate track pointer found for track " << (int)entry.first.first
               << ", head " << (int)entry.first.second << ": " << ptr;
            FAIL() << ss.str();
        }
    }

    // Sort pointers to check spacing
    std::sort(sortedPointers.begin(), sortedPointers.end());
    
    // Verify spacing between consecutive tracks
    ptrdiff_t trackSpacing = 0;
    for (size_t i = 1; i < sortedPointers.size(); ++i)
    {
        ptrdiff_t diff = (uint8_t*)sortedPointers[i] - (uint8_t*)sortedPointers[i-1];
        //std::cout << "Spacing between track " << (i-1) << " and track " << i << ": " << diff << " bytes" << std::endl;
        
        // The actual spacing will be larger than RAW_TRACK_SIZE due to vector alignment
        // We expect it to be consistent across all tracks
        if (i == 1)
        {
            // Store the first spacing as our reference
            trackSpacing = diff;
        }
        else if (diff != trackSpacing)
        {
            std::stringstream ss;
            ss << "Inconsistent track spacing. Expected "
               << trackSpacing << " bytes between tracks, but got "
               << diff << " bytes between track " << (i-1) << " and track " << i;
            FAIL() << ss.str();
        }
    }

    // Verify all buffer addresses are unique using a set
    std::set<const void*> seenBuffers;
    
    for (const auto& entry : trackHeadToBufferMap)
    {
        auto [it, inserted] = seenBuffers.insert(entry.second);
        if (!inserted)
        {
            // Find the first track/head pair with this buffer address
            auto it2 = std::find_if(trackHeadToBufferMap.begin(), trackHeadToBufferMap.end(),
                [buffer = entry.second](const auto& pair) { return pair.second == buffer; });
            
            ASSERT_TRUE(inserted)
                << "Buffer address for track/head combinations is not unique."
                << " First occurrence: Track " << (int)(it2->first.first) 
                << ", Head " << (int)(it2->first.second)
                << " Second occurrence: Track " << (int)(entry.first.first)
                << ", Head " << (int)(entry.first.second);
        }
    }
}

/// Test AddressMarkRecord CRC recalculation via CRCHelper
TEST_F(DiskImage_Test, AddressMarkRecordCRC)
{
    std::vector<std::pair<std::vector<uint8_t>, uint16_t>> referenceData =
    {
        { {0x00, 0x00, 0x01, 0x01 }, 0x0CFA },  // Track #0, Sector #1
        { {0x00, 0x00, 0x09, 0x01 }, 0xA573 },  // Track #0, Sector #9
        { {0x50, 0x00, 0x0F, 0x01 }, 0x38AC },  // Track #80, Sector #15
    };

    for (size_t i = 0; i < referenceData.size(); i++)
    {
        auto item = referenceData[i];
        auto bytes = item.first;
        uint16_t referenceCRC = item.second;

        DiskImage::AddressMarkRecord record;
        record.cylinder = bytes[0];
        record.head = bytes[1];
        record.sector = bytes[2];
        record.sector_size = bytes[3];
        record.recalculateCRC();

        uint16_t crc = record.id_crc;

        EXPECT_EQ(crc, referenceCRC) << StringHelper::Format("Test vector[%d]. Expected 0x%04X, found 0x%04X", i, referenceCRC, crc);
    }
}

/// Test all sizes / consistency for empty raw track creation (used in WRITE_TRACK WD1793 flow)
TEST_F(DiskImage_Test, RawSectorBytesConsistency)
{
    // Sector gaps / sync sequences must be filled accordingly
    // Verification vector in format: <Name>, <offset>, <size>, <fillByte>
    static const std::vector<std::tuple<std::string, uint16_t, uint8_t, uint8_t>> testVectors
    {
        { "Gap0",       offsetof(DiskImage::RawSectorBytes, gap0),      sizeof(DiskImage::RawSectorBytes::gap0),        0x4E },   // Gap0
        { "Sync0",      offsetof(DiskImage::RawSectorBytes, sync0),     sizeof(DiskImage::RawSectorBytes::sync0),       0x00 },   // Sync0
        { "f5_token0",  offsetof(DiskImage::RawSectorBytes, f5_token0), sizeof(DiskImage::RawSectorBytes::f5_token0),   0xA1 },   // f5_token0
        { "Gap1",       offsetof(DiskImage::RawSectorBytes, gap1),      sizeof(DiskImage::RawSectorBytes::gap1),        0x4E },   // Gap1
        { "Sync1",      offsetof(DiskImage::RawSectorBytes, sync1),     sizeof(DiskImage::RawSectorBytes::sync1),       0x00 },   // Sync1
        { "f5_token1",  offsetof(DiskImage::RawSectorBytes, f5_token1), sizeof(DiskImage::RawSectorBytes::f5_token1),   0xA1 },   // f5_token1
        { "Gap2",       offsetof(DiskImage::RawSectorBytes, gap2),      sizeof(DiskImage::RawSectorBytes::gap2),        0x4E },   // Gap2
    };

    EXPECT_EQ(sizeof(DiskImage::AddressMarkRecord), 7) << "AddressMarkRecord must be 7 bytes in size";
    EXPECT_EQ(sizeof(DiskImage::RawSectorBytes), 388) << "RawSectorBytes must be 388 bytes in size";

    DiskImage::RawSectorBytes sectorBytes;
    uint8_t * pSectorBytes = (uint8_t*)&sectorBytes;

    for (const auto& record : testVectors)
    {
        std::string fieldName = std::get<0>(record);
        uint16_t offset = std::get<1>(record);
        uint8_t size = std::get<2>(record);
        uint8_t fillValue = std::get<3>(record);

        bool filledCorrectly = DumpHelper::IsFilledWith(pSectorBytes + offset, size, fillValue);
        std::string dump = DumpHelper::HexDumpBuffer(pSectorBytes + offset, size);
        EXPECT_EQ(filledCorrectly, true) << StringHelper::Format("Field '%s' with len=%d expected to be filled with 0x%02X.\nActual: %s", fieldName.c_str(), size, fillValue, dump.c_str());
    }
}

/// Test RawSectorBytes data CRC recalculation via CRCHelper
TEST_F(DiskImage_Test, RawSectorBytesDataCRC)
{
    // Verification vector in format: <fillByte>, <dataCRC>
    std::vector<std::pair<uint8_t, uint16_t>> referenceData =
    {
        { 0x00, 0x22E1 },  // All sector data filled with 0x00
        { 0xAA, 0x58F2 },  // All sector data filled with 0xAA
        { 0xFF, 0xE5FB },  // All sector data filled with 0xFF
    };

    for (size_t i = 0; i < referenceData.size(); i++)
    {
        auto item = referenceData[i];
        uint8_t fillByte = item.first;
        uint16_t referenceCRC = item.second;

        DiskImage::RawSectorBytes sectorBytes;
        std::fill(sectorBytes.data, sectorBytes.data + sizeof(sectorBytes.data), fillByte);
        sectorBytes.recalculateDataCRC();
        uint16_t crc = sectorBytes.data_crc;

        EXPECT_EQ(crc, referenceCRC) << StringHelper::Format("Test vector[%d]. Expected 0x%04X, found 0x%04X", i, referenceCRC, crc);
    }
}

/// Test RawTrack structure consistency
TEST_F(DiskImage_Test, RawTrackConsistency)
{
    DiskImage::RawTrack track;

    // Check all constants
    EXPECT_EQ(DiskImage::RawTrack::RAW_TRACK_SIZE, 6250);
    EXPECT_EQ(DiskImage::RawTrack::TRACK_BITMAP_SIZE_BYTES, 782);
    EXPECT_EQ(DiskImage::RawTrack::TRACK_END_GAP_BYTES, 42);

    // Check field sizes
    EXPECT_EQ(sizeof(track.sectors), 388 * 16) << "There must be 388 x 16 bytes size for RawTrack.sectors";
    EXPECT_EQ(sizeof(track), 6250) << "RawTrack structure must be exactly 6250 bytes in size";

    // Check all constants are in sync with real object size
    EXPECT_EQ(sizeof(track), DiskImage::RawTrack::RAW_TRACK_SIZE);

    // Check end gap filled
    bool filledCorrectly = DumpHelper::IsFilledWith(track.endGap, sizeof(track.endGap), 0x4E);
    EXPECT_EQ(filledCorrectly, true) << "RawTrack::endGap must be filled with 0x4E values";
}

/// Test Track structure consistency
TEST_F(DiskImage_Test, FullTrackConsistency)
{
    DiskImage::FullTrack track;

    EXPECT_EQ(sizeof(track.clockMarksBitmap), 782) << "There must be 3782 bytes size for RawTrack.clockMarksBitmap";
    EXPECT_EQ(sizeof(track.badBytesBitmap), 782) << "There must be 3782 bytes size for RawTrack.badBytesBitmap";

    // Check all constants are in sync with real object size
    EXPECT_EQ(sizeof(track), DiskImage::RawTrack::RAW_TRACK_SIZE + 2 * DiskImage::RawTrack::TRACK_BITMAP_SIZE_BYTES) << "RawTrack structure must be exactly 7814 bytes in size"; // 6250 + 782 + 782
}

/// Test RawTrack formatTrack method
TEST_F(DiskImage_Test, FormatTrack)
{
    DiskImage::Track track;

    /// region <Make a mess in sector data>
    uint8_t* start = (uint8_t*)&track;
    uint8_t* end = start + sizeof(track);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(start, end, gen);
    /// endregion </Make a mess in sector data>

    // Do low level formatting
    track.formatTrack(72, 0);

    /// region <Check formatting consistency>

    // Sector gaps / sync sequences must be filled accordingly
    // Verification vector in format: <Name>, <offset>, <size>, <fillByte>
    static const std::vector<std::tuple<std::string, uint16_t, uint8_t, uint8_t>> testVectors
    {
        { "Gap0",       offsetof(DiskImage::RawSectorBytes, gap0),      sizeof(DiskImage::RawSectorBytes::gap0),        0x4E },   // Gap0
        { "Sync0",      offsetof(DiskImage::RawSectorBytes, sync0),     sizeof(DiskImage::RawSectorBytes::sync0),       0x00 },   // Sync0
        { "f5_token0",  offsetof(DiskImage::RawSectorBytes, f5_token0), sizeof(DiskImage::RawSectorBytes::f5_token0),   0xA1 },   // f5_token0
        { "Gap1",       offsetof(DiskImage::RawSectorBytes, gap1),      sizeof(DiskImage::RawSectorBytes::gap1),        0x4E },   // Gap1
        { "Sync1",      offsetof(DiskImage::RawSectorBytes, sync1),     sizeof(DiskImage::RawSectorBytes::sync1),       0x00 },   // Sync1
        { "f5_token1",  offsetof(DiskImage::RawSectorBytes, f5_token1), sizeof(DiskImage::RawSectorBytes::f5_token1),   0xA1 },   // f5_token1
        { "Gap2",       offsetof(DiskImage::RawSectorBytes, gap2),      sizeof(DiskImage::RawSectorBytes::gap2),        0x4E },   // Gap2
    };

    for (size_t i = 0; i < 16; i++)
    {
        DiskImage::RawSectorBytes* sectorBytes = track.getRawSector(i);
        uint8_t *pSectorBytes = (uint8_t*)sectorBytes;

        // Check all gaps / clock sync marks
        for (const auto &record: testVectors)
        {
            std::string fieldName = std::get<0>(record);
            uint16_t offset = std::get<1>(record);
            uint8_t size = std::get<2>(record);
            uint8_t fillValue = std::get<3>(record);

            bool filledCorrectly = DumpHelper::IsFilledWith(pSectorBytes + offset, size, fillValue);
            std::string dump = DumpHelper::HexDumpBuffer(pSectorBytes + offset, size);
            EXPECT_EQ(filledCorrectly, true) << StringHelper::Format(
                                "Field '%s' with len=%d expected to be filled with 0x%02X.\nActual: %s",
                                fieldName.c_str(), size, fillValue, dump.c_str());
        }
    }
    /// endregion </Check formatting consistency>

    /// region <Check track ID information>
    for (size_t i = 0; i < DiskImage::RawTrack::SECTORS_PER_TRACK; i++)
    {
        DiskImage::RawSectorBytes& sectorBytes = *track.getRawSector(i);
        DiskImage::AddressMarkRecord& markRecord = sectorBytes.address_record;

        EXPECT_EQ(markRecord.cylinder, 72) << StringHelper::Format("Sector %d ID block must contain cylinder no. = 72. Found: %d", i, markRecord.cylinder);
        EXPECT_EQ(markRecord.head, 0) << StringHelper::Format("Sector %d ID block must contain side no. = 0. Found: %d", i, markRecord.head);
    }
    /// endregion </Check track ID information>
}