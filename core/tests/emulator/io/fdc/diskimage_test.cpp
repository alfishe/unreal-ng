#include <gtest/gtest.h>
#include "stdafx.h"
#include "pch.h"

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

TEST_F(DiskImage_Test, AddressMarkRecord)
{
    std::vector<std::pair<std::vector<uint8_t>, uint16_t>> referenceData =
    {
        { {0x00, 0x00, 0x01, 0x01 }, 0x0CFA },  // Track #0, Sector #1
        { {0x00, 0x00, 0x09, 0x01 }, 0xA573 }   // Track #0, Sector #9
    };

    for (size_t i = 0; i < referenceData.size(); i++)
    {
        auto item = referenceData[i];
        auto bytes = item.first;
        uint16_t referenceCRC = item.second;

        DiskImage::AddressMarkRecord record;
        record.track = bytes[0];
        record.head = bytes[1];
        record.sector = bytes[2];
        record.sector_len = bytes[3];
        record.recalculateCRC();

        uint16_t crc = record.id_crc;

        EXPECT_EQ(crc, referenceCRC) << StringHelper::Format("Test vector[%d]. Expected 0x%04X, found 0x%04X", i, referenceCRC, crc);
    }

}