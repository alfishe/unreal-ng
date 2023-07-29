#include <gtest/gtest.h>

#include <cstddef> // offsetof macro
#include <algorithm>
#include <random>
#include <cstdint>

#include <common/dumphelper.h>
#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"

class DiskImage_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;

protected:
    //void SetUp() override;
    //void TearDown() override;
};

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
        record.sector_len = bytes[3];
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
    EXPECT_EQ(sizeof(track.clockMarksBitmap), 782) << "There must be 3782 bytes size for RawTrack.clockMarksBitmap";
    EXPECT_EQ(sizeof(track.badBytesBitmap), 782) << "There must be 3782 bytes size for RawTrack.badBytesBitmap";
    EXPECT_EQ(sizeof(track), 7814) << "RawTrack structure must be exactly 7814 bytes in size"; // 6250 + 782 + 782

    // Check all constants are in sync with real object size
    EXPECT_EQ(sizeof(track), DiskImage::RawTrack::RAW_TRACK_SIZE + 2 * DiskImage::RawTrack::TRACK_BITMAP_SIZE_BYTES);

    // Check end gap filled
    bool filledCorrectly = DumpHelper::IsFilledWith(track.endGap, sizeof(track.endGap), 0x4E);
    EXPECT_EQ(filledCorrectly, true) << "RawTrack::endGap must be filled with 0x4E values";
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