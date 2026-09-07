#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include <common/dumphelper.h>
#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"

/// Tests for the universal track model (docs/inprogress/2026-09-02-universal-track-model/test-plan.md, section 1)

using Track = DiskImage::Track;
using Sector = DiskImage::Sector;
using Spec = DiskImage::TrackFormatSpec;
using Encoding = DiskImage::Encoding;

/// region <Test types>

class DiskImage_Test : public ::testing::Test
{
protected:
    void SetUp() override {}
    void TearDown() override {}

    /// Legacy TR-DOS sector layout: <name, offset within the 388-byte sector, size, fill byte>
    static const std::vector<std::tuple<std::string, size_t, size_t, uint8_t>>& legacySectorLayout()
    {
        static const std::vector<std::tuple<std::string, size_t, size_t, uint8_t>> layout
        {
            { "gap0",      0,   10, 0x4E },
            { "sync0",     10,  12, 0x00 },
            { "f5_token0", 22,  3,  0xA1 },
            { "idam",      25,  1,  0xFE },
            { "gap1",      32,  22, 0x4E },
            { "sync1",     54,  12, 0x00 },
            { "f5_token1", 66,  3,  0xA1 },
            { "dam",       69,  1,  0xFB },
            { "gap2",      328, 60, 0x4E },
        };
        return layout;
    }

    static constexpr size_t LEGACY_SECTOR_BYTES = 388;
    static constexpr size_t LEGACY_END_GAP = 42;
};

/// endregion </Test types>

/// region <U1: AddressMarkRecord>

TEST_F(DiskImage_Test, AddressMarkRecord_Size_And_CRC)
{
    EXPECT_EQ(sizeof(DiskImage::AddressMarkRecord), 7u) << "AddressMarkRecord must be a 7-byte packed overlay";

    std::vector<std::pair<std::vector<uint8_t>, uint16_t>> referenceData =
    {
        { {0x00, 0x00, 0x01, 0x01 }, 0x0CFA },  // Track #0, Sector #1
        { {0x00, 0x00, 0x09, 0x01 }, 0xA573 },  // Track #0, Sector #9
        { {0x50, 0x00, 0x0F, 0x01 }, 0x38AC },  // Track #80, Sector #15
    };

    for (size_t i = 0; i < referenceData.size(); i++)
    {
        auto bytes = referenceData[i].first;
        uint16_t referenceCRC = referenceData[i].second;

        DiskImage::AddressMarkRecord record;
        record.cylinder = bytes[0];
        record.head = bytes[1];
        record.sector = bytes[2];
        record.sector_size = bytes[3];
        record.recalculateCRC();

        EXPECT_EQ(record.id_crc, referenceCRC)
            << StringHelper::Format("Test vector[%d]. Expected 0x%04X, found 0x%04X", i, referenceCRC, record.id_crc);
        EXPECT_TRUE(record.isCRCValid());

        record.id_crc ^= 0x0101;
        EXPECT_FALSE(record.isCRCValid());
    }

    EXPECT_EQ(DiskImage::AddressMarkRecord{}.getSectorSize(), 256);
}

/// endregion </U1>

/// region <U2/U3: TR-DOS format is byte-identical to the legacy fixed layout>

TEST_F(DiskImage_Test, TrdosFormat_IsByteIdenticalToLegacyLayout)
{
    Track track;
    track.formatTrack(72, 0);

    ASSERT_EQ(track.rawSize(), DiskImage::RawTrack::RAW_TRACK_SIZE);
    ASSERT_EQ(track.rawSize(), 6250u);
    EXPECT_EQ(track.encoding(), Encoding::MFM);
    EXPECT_EQ(track.clockBitmap().size(), DiskImage::RawTrack::TRACK_BITMAP_SIZE_BYTES);
    EXPECT_EQ(track.clockBitmap().size(), 782u);

    const uint8_t* raw = track.rawData();

    for (size_t sector = 0; sector < 16; sector++)
    {
        const size_t base = sector * LEGACY_SECTOR_BYTES;

        for (const auto& field : legacySectorLayout())
        {
            const std::string& name = std::get<0>(field);
            const size_t offset = base + std::get<1>(field);
            const size_t size = std::get<2>(field);
            const uint8_t fill = std::get<3>(field);

            bool filled = DumpHelper::IsFilledWith(const_cast<uint8_t*>(raw + offset), size, fill);
            EXPECT_TRUE(filled) << StringHelper::Format("Sector %d field '%s' @%d len=%d expected fill 0x%02X. Actual: %s",
                                                        sector, name.c_str(), offset, size, fill,
                                                        DumpHelper::HexDumpBuffer(const_cast<uint8_t*>(raw + offset), size).c_str());
        }

        // ID field: FE C H R N
        EXPECT_EQ(raw[base + 26], 72) << "cylinder";
        EXPECT_EQ(raw[base + 27], 0) << "head";
        EXPECT_EQ(raw[base + 28], sector + 1) << "sector number";
        EXPECT_EQ(raw[base + 29], 1) << "size code 256";

        // Data: 256 zero bytes
        EXPECT_TRUE(DumpHelper::IsFilledWith(const_cast<uint8_t*>(raw + base + 70), 256, 0x00));

        // Clock marks: exactly the six A1 bytes of this sector
        for (size_t i = 0; i < LEGACY_SECTOR_BYTES; i++)
        {
            const bool expected = (i >= 22 && i < 25) || (i >= 66 && i < 69);
            EXPECT_EQ(track.clockMark(base + i), expected) << "clock mark at sector " << sector << " offset " << i;
        }
    }

    // End gap
    EXPECT_TRUE(DumpHelper::IsFilledWith(const_cast<uint8_t*>(raw + 16 * LEGACY_SECTOR_BYTES), LEGACY_END_GAP, 0x4E));
    EXPECT_EQ(16 * LEGACY_SECTOR_BYTES + LEGACY_END_GAP, 6250u);
    EXPECT_TRUE(track.hasClockMarks());
}

TEST_F(DiskImage_Test, TrdosFormat_SectorIndex)
{
    Track track;
    track.formatTrack(5, 1);

    ASSERT_EQ(track.sectorCount(), 16u);
    EXPECT_EQ(track.indexMarkOffset(), Sector::NO_OFFSET) << "TR-DOS layout has no index mark";

    for (size_t i = 0; i < 16; i++)
    {
        Sector* sector = track.getRawSector(i);
        ASSERT_NE(sector, nullptr);
        EXPECT_EQ(sector->idamOffset, i * LEGACY_SECTOR_BYTES + 25);
        EXPECT_EQ(sector->damOffset, i * LEGACY_SECTOR_BYTES + 69);
        EXPECT_EQ(sector->dataOffset, i * LEGACY_SECTOR_BYTES + 70);
        EXPECT_EQ(sector->dataSize, 256);
        EXPECT_EQ(sector->cylinder(), 5);
        EXPECT_EQ(sector->head(), 1);
        EXPECT_EQ(sector->number(), i + 1);
        EXPECT_EQ(sector->sizeCode(), 1);
        EXPECT_TRUE(sector->hasData);
        EXPECT_FALSE(sector->deleted);
        EXPECT_TRUE(sector->idCrcValid);
        EXPECT_TRUE(sector->dataCrcValid);
        EXPECT_TRUE(sector->isIDCRCValid());
        EXPECT_TRUE(sector->isDataCRCValid());
        EXPECT_EQ(sector->data, track.rawData() + sector->dataOffset);
        EXPECT_EQ(reinterpret_cast<uint8_t*>(sector->id), track.rawData() + sector->idamOffset);
        EXPECT_EQ(sector->dataAddressMark(), 0xFB);

        // Logical lookup by number
        EXPECT_EQ(track.getSector(static_cast<uint8_t>(i)), sector);
        EXPECT_EQ(track.findSector(static_cast<uint8_t>(i + 1)), sector);
        EXPECT_EQ(track.getIDForSector(static_cast<uint8_t>(i)), sector->id);
        EXPECT_EQ(track.getDataForSector(static_cast<uint8_t>(i)), sector->data);
    }

    EXPECT_EQ(track.getSector(16), nullptr) << "sector 17 does not exist";
    EXPECT_EQ(track.findSector(0), nullptr);
    EXPECT_EQ(track.getRawSector(16), nullptr);
}

/// endregion </U2/U3>

/// region <U4: interleave patterns>

TEST_F(DiskImage_Test, Interleave_Patterns)
{
    static const uint8_t PATTERNS[3][16] =
    {
        { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 },
        { 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15, 8, 16 },
        { 1, 12, 7, 2, 13, 8, 3, 14, 9, 4, 15, 10, 5, 16, 11, 6 }
    };

    for (const auto& pattern : PATTERNS)
    {
        DiskImage disk(2, 2);
        Track* track = disk.getTrackForCylinderAndSide(1, 1);
        ASSERT_NE(track, nullptr);

        track->applyInterleaveTable(pattern);

        ASSERT_EQ(track->sectorCount(), 16u);
        ASSERT_EQ(track->rawSize(), 6250u);

        for (size_t physical = 0; physical < 16; physical++)
        {
            const uint8_t number = pattern[physical];
            Sector* byPosition = track->getRawSector(physical);
            Sector* byNumber = track->getSector(static_cast<uint8_t>(number - 1));

            ASSERT_NE(byPosition, nullptr);
            EXPECT_EQ(byPosition->number(), number);
            EXPECT_EQ(byPosition->cylinder(), 1);
            EXPECT_EQ(byPosition->head(), 1);
            EXPECT_EQ(byNumber, byPosition) << "logical lookup must resolve to the physical slot of the interleave";
            EXPECT_TRUE(byPosition->isIDCRCValid());
        }
    }
}

/// endregion </U4>

/// region <U5-U10: other WD1793 geometries>

TEST_F(DiskImage_Test, Format_9x512_Plus3)
{
    Spec spec = Spec::plus3();
    EXPECT_TRUE(spec.fits()) << "9 x 512 must fit into 6250 bytes";

    Track track;
    track.formatTrack(3, 0, spec);

    ASSERT_EQ(track.sectorCount(), 9u);
    EXPECT_NE(track.indexMarkOffset(), Sector::NO_OFFSET) << "IBM layout carries an index mark";
    EXPECT_EQ(track.rawData()[track.indexMarkOffset()], 0xFC);

    for (size_t i = 0; i < 9; i++)
    {
        Sector* sector = track.getRawSector(i);
        EXPECT_EQ(sector->number(), i + 1);
        EXPECT_EQ(sector->dataSize, 512);
        EXPECT_EQ(sector->sizeCode(), 2);
        EXPECT_TRUE(sector->hasData);
        EXPECT_TRUE(sector->idCrcValid);
        EXPECT_TRUE(sector->dataCrcValid);
        EXPECT_TRUE(DumpHelper::IsFilledWith(sector->data, 512, 0xE5)) << "+3 filler";
    }

    EXPECT_EQ(track.getSector(9), nullptr);
}

TEST_F(DiskImage_Test, Format_OtherGeometries)
{
    struct Case { const char* name; Spec spec; size_t sectors; uint16_t size; };
    std::vector<Case> cases =
    {
        { "10x512 +D",  Spec::plusD(), 10, 512 },
        { "5x1024",     Spec::ibm(5, DiskImage::SECTOR_SIZE_1024, 1, 0x40), 5, 1024 },
        { "16x128 MFM", Spec::ibm(16, DiskImage::SECTOR_SIZE_128, 1, 0x20), 16, 128 },
        { "26x128 MFM", Spec::ibm(26, DiskImage::SECTOR_SIZE_128, 1, 0x10), 26, 128 },
    };

    for (const Case& c : cases)
    {
        EXPECT_TRUE(c.spec.fits()) << c.name << " total " << c.spec.totalBytes();

        Track track;
        track.formatTrack(0, 0, c.spec);

        ASSERT_EQ(track.sectorCount(), c.sectors) << c.name;
        for (size_t i = 0; i < c.sectors; i++)
        {
            Sector* sector = track.getRawSector(i);
            EXPECT_EQ(sector->number(), i + 1) << c.name;
            EXPECT_EQ(sector->dataSize, c.size) << c.name;
            EXPECT_TRUE(sector->hasData) << c.name;
            EXPECT_TRUE(sector->idCrcValid && sector->dataCrcValid) << c.name;
        }
    }
}

TEST_F(DiskImage_Test, Format_MixedSectorSizes)
{
    Spec spec = Spec::trdos(nullptr, 4);
    spec.sectorSizeCodes = { 1, 2, 0, 3 };
    ASSERT_TRUE(spec.fits());

    Track track;
    track.formatTrack(0, 0, spec);

    ASSERT_EQ(track.sectorCount(), 4u);
    EXPECT_EQ(track.getRawSector(0)->dataSize, 256);
    EXPECT_EQ(track.getRawSector(1)->dataSize, 512);
    EXPECT_EQ(track.getRawSector(2)->dataSize, 128);
    EXPECT_EQ(track.getRawSector(3)->dataSize, 1024);

    for (size_t i = 0; i < 4; i++)
    {
        EXPECT_TRUE(track.getRawSector(i)->isDataCRCValid()) << i;
        EXPECT_EQ(track.getRawSector(i)->sizeCode(), spec.sectorSizeCodes[i]);
    }
}

TEST_F(DiskImage_Test, Format_DuplicateSectorNumbers)
{
    uint8_t order[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 5, 14, 15, 16 };  // sector 5 twice, 13 absent
    Track track;
    track.formatTrack(0, 0, Spec::trdos(order, 16));

    ASSERT_EQ(track.sectorCount(), 16u) << "duplicates are kept";

    Sector* first = track.findSector(5);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, track.getRawSector(4)) << "first occurrence in stream order";

    Sector* second = track.findSector(5, first->idamOffset + 1);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second, track.getRawSector(12)) << "next occurrence after the first";

    Sector* wrapped = track.findSector(5, second->idamOffset + 1);
    EXPECT_EQ(wrapped, first) << "search wraps around the index";

    EXPECT_EQ(track.findSector(13), nullptr);
    EXPECT_EQ(track.getSector(12), nullptr);
}

TEST_F(DiskImage_Test, Format_MissingSectorNumbers)
{
    uint8_t order[15] = { 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };  // no sector 4
    Track track;
    track.formatTrack(0, 0, Spec::trdos(order, 15));

    ASSERT_EQ(track.sectorCount(), 15u);
    EXPECT_EQ(track.getSector(3), nullptr);
    EXPECT_EQ(track.getDataForSector(3), nullptr);
    EXPECT_EQ(track.getIDForSector(3), nullptr);
    EXPECT_NE(track.getSector(4), nullptr);
    EXPECT_EQ(track.getSector(4)->number(), 5);
    EXPECT_FALSE(track.isSectorDirty(3));

    // Writing to a missing sector is a no-op
    uint8_t data[256] = {0};
    track.writeSectorData(3, data, 256);
    EXPECT_FALSE(track.isDirty());
}

TEST_F(DiskImage_Test, Format_CustomCylHead)
{
    Spec spec = Spec::trdos(nullptr, 3);
    spec.sectorCylinders = { 0, 40, 255 };
    spec.sectorHeads = { 1, 0, 1 };

    Track track;
    track.formatTrack(7, 0, spec);

    ASSERT_EQ(track.sectorCount(), 3u);
    EXPECT_EQ(track.getRawSector(0)->cylinder(), 0);
    EXPECT_EQ(track.getRawSector(1)->cylinder(), 40);
    EXPECT_EQ(track.getRawSector(2)->cylinder(), 255);
    EXPECT_EQ(track.getRawSector(0)->head(), 1);
    EXPECT_EQ(track.getRawSector(1)->head(), 0);
    EXPECT_TRUE(track.getRawSector(2)->isIDCRCValid());

    // WD1793-style match: cylinder + side + number
    EXPECT_EQ(track.findSector(40, 0, 2, 0), track.getRawSector(1));
    EXPECT_EQ(track.findSector(7, 0, 2, 0), nullptr) << "physical cylinder is not what the ID says";
    EXPECT_EQ(track.findSector(0, 0, 1, 0), nullptr) << "side compare fails";
    EXPECT_EQ(track.findSector(0, -1, 1, 0), track.getRawSector(0)) << "side compare off";
}

/// endregion </U5-U10>

/// region <U11-U16: scanner behaviour on hand-built streams>

namespace
{
    /// Minimal stream builder: writes bytes and clock marks into a Track via setRaw/setClockBitmap
    struct StreamBuilder
    {
        std::vector<uint8_t> raw;
        std::vector<uint8_t> clock;

        explicit StreamBuilder(size_t size, uint8_t fill = 0x4E) : raw(size, fill), clock((size + 7) / 8, 0) {}

        size_t pos = 0;

        void put(uint8_t value, bool mark = false)
        {
            raw[pos] = value;
            if (mark) clock[pos >> 3] |= static_cast<uint8_t>(1u << (pos & 7));
            pos++;
        }
        void fill(size_t count, uint8_t value) { while (count--) put(value); }
        void sync() { fill(12, 0x00); put(0xA1, true); put(0xA1, true); put(0xA1, true); }

        /// ID field with valid CRC
        void idField(uint8_t c, uint8_t h, uint8_t r, uint8_t n)
        {
            sync();
            size_t start = pos;
            put(0xFE); put(c); put(h); put(r); put(n);
            uint16_t crc = CRCHelper::crcWD1793(&raw[start], 5);   // byte-swapped: low byte == true high byte
            put(static_cast<uint8_t>(crc & 0xFF)); put(static_cast<uint8_t>(crc >> 8));
        }

        /// Data field with valid CRC
        void dataField(uint8_t dam, size_t size, uint8_t value)
        {
            sync();
            size_t start = pos;
            put(dam);
            fill(size, value);
            uint16_t crc = CRCHelper::crcWD1793(&raw[start], static_cast<uint16_t>(size + 1));
            put(static_cast<uint8_t>(crc & 0xFF)); put(static_cast<uint8_t>(crc >> 8));
        }

        void applyTo(Track& track, bool withClock = true)
        {
            track.setRaw(raw.data(), raw.size());
            if (withClock) track.setClockBitmap(clock.data(), clock.size());
        }
    };
}

TEST_F(DiskImage_Test, Reindex_IdOnlySector_NoDataField)
{
    StreamBuilder b(6250);
    b.fill(10, 0x4E);
    b.idField(0, 0, 1, 1);
    b.fill(100, 0x4E);           // No DAM within 43 bytes
    b.idField(0, 0, 2, 1);
    b.fill(22, 0x4E);
    b.dataField(0xFB, 256, 0x11);

    Track track;
    b.applyTo(track);

    ASSERT_EQ(track.sectorCount(), 2u);
    Sector* s1 = track.getRawSector(0);
    EXPECT_EQ(s1->number(), 1);
    EXPECT_FALSE(s1->hasData);
    EXPECT_EQ(s1->data, nullptr);
    EXPECT_EQ(s1->damOffset, Sector::NO_OFFSET);
    EXPECT_EQ(track.getDataForSector(0), nullptr);
    EXPECT_FALSE(s1->isDataCRCValid());

    Sector* s2 = track.getRawSector(1);
    EXPECT_TRUE(s2->hasData);
    EXPECT_TRUE(s2->dataCrcValid);
    EXPECT_EQ(s2->data[0], 0x11);
}

TEST_F(DiskImage_Test, Reindex_DeletedDataMark)
{
    const uint8_t marks[] = { 0xF8, 0xF9, 0xFA, 0xFB };
    for (uint8_t mark : marks)
    {
        StreamBuilder b(6250);
        b.fill(10, 0x4E);
        b.idField(0, 0, 1, 0);
        b.fill(22, 0x4E);
        b.dataField(mark, 128, 0x22);

        Track track;
        b.applyTo(track);

        ASSERT_EQ(track.sectorCount(), 1u) << "mark " << (int)mark;
        Sector* s = track.getRawSector(0);
        EXPECT_TRUE(s->hasData);
        EXPECT_EQ(s->dataAddressMark(), mark);
        EXPECT_EQ(s->deleted, mark == 0xF8);
        EXPECT_EQ(s->dataSize, 128);
        EXPECT_TRUE(s->dataCrcValid);
    }
}

TEST_F(DiskImage_Test, Reindex_BadIdCrc_And_BadDataCrc_ArePreserved)
{
    Track track;
    track.formatTrack(0, 0);

    Sector* s3 = track.getSector(2);
    ASSERT_NE(s3, nullptr);
    s3->id->id_crc ^= 0xFFFF;         // corrupt ID CRC
    Sector* s7 = track.getSector(6);
    s7->data[10] ^= 0x55;              // corrupt data without fixing CRC

    track.reindex();

    s3 = track.getSector(2);
    s7 = track.getSector(6);
    ASSERT_NE(s3, nullptr);
    ASSERT_NE(s7, nullptr);
    EXPECT_FALSE(s3->idCrcValid);
    EXPECT_TRUE(s3->dataCrcValid);
    EXPECT_TRUE(s7->idCrcValid);
    EXPECT_FALSE(s7->dataCrcValid);
    EXPECT_FALSE(s7->isDataCRCValid());

    s7->recalculateDataCRC();
    EXPECT_TRUE(s7->isDataCRCValid());
    EXPECT_FALSE(s3->isIDCRCValid()) << "recalculating a data CRC must not touch the ID CRC";

    // Sector 3 keeps its bad ID CRC, sector 7 was repaired: 15 of 16 sectors fully valid
    size_t valid = 0;
    for (const Sector& s : track.sectors()) if (s.idCrcValid && s.dataCrcValid) valid++;
    EXPECT_EQ(valid, 15u);
}

TEST_F(DiskImage_Test, Reindex_UsesClockMarks)
{
    // Sector 1 whose data contains a fake "A1 A1 A1 FE 00 00 02 01" sequence without clock marks
    StreamBuilder b(6250);
    b.fill(10, 0x4E);
    b.idField(0, 0, 1, 1);
    b.fill(22, 0x4E);
    b.sync();
    size_t damStart = b.pos;
    b.put(0xFB);
    b.fill(100, 0x00);
    b.put(0xA1); b.put(0xA1); b.put(0xA1); b.put(0xFE); b.put(0x00); b.put(0x00); b.put(0x02); b.put(0x01);
    b.fill(256 - 108, 0x00);
    uint16_t crc = CRCHelper::crcWD1793(&b.raw[damStart], 257);
    b.put(static_cast<uint8_t>(crc & 0xFF)); b.put(static_cast<uint8_t>(crc >> 8));

    Track track;
    b.applyTo(track, true);
    ASSERT_EQ(track.sectorCount(), 1u) << "A1 bytes without clock marks are data, not sync";
    EXPECT_TRUE(track.getRawSector(0)->dataCrcValid);

    // Now give those A1 bytes clock marks: the scanner must see a second (ID-only) sector inside the data
    size_t fakeSync = damStart + 1 + 100;
    track.setClockMark(fakeSync, true);
    track.setClockMark(fakeSync + 1, true);
    track.setClockMark(fakeSync + 2, true);
    track.reindex();
    ASSERT_EQ(track.sectorCount(), 2u);
    EXPECT_EQ(track.getRawSector(1)->number(), 2);
    EXPECT_FALSE(track.getRawSector(1)->idCrcValid);
}

TEST_F(DiskImage_Test, Reindex_FallbackWithoutClockBitmap)
{
    // Legacy buffer: no clock information at all. Byte patterns are used and data fields are skipped.
    StreamBuilder b(6250);
    b.fill(10, 0x4E);
    b.idField(0, 0, 1, 1);
    b.fill(22, 0x4E);
    b.sync();
    size_t damStart = b.pos;
    b.put(0xFB);
    b.fill(50, 0x00);
    b.put(0xA1); b.put(0xA1); b.put(0xA1); b.put(0xFE);  // fake sync inside data
    b.fill(256 - 54, 0x00);
    uint16_t crc = CRCHelper::crcWD1793(&b.raw[damStart], 257);
    b.put(static_cast<uint8_t>(crc & 0xFF)); b.put(static_cast<uint8_t>(crc >> 8));
    b.fill(60, 0x4E);
    b.idField(0, 0, 2, 1);
    b.fill(22, 0x4E);
    b.dataField(0xFB, 256, 0x33);

    Track track;
    b.applyTo(track, false);
    EXPECT_FALSE(track.hasClockMarks());

    ASSERT_EQ(track.sectorCount(), 2u);
    EXPECT_EQ(track.getRawSector(0)->number(), 1);
    EXPECT_EQ(track.getRawSector(1)->number(), 2);
    EXPECT_TRUE(track.getRawSector(0)->dataCrcValid);
    EXPECT_TRUE(track.getRawSector(1)->dataCrcValid);
}

TEST_F(DiskImage_Test, Reindex_DataCrossingIndex)
{
    StreamBuilder b(6250);
    b.pos = 6250 - 200;             // ID field near the end of the track
    b.idField(0, 0, 1, 1);          // 256-byte data field cannot fit before the index
    b.fill(22, 0x4E);
    b.sync();
    b.put(0xFB);

    Track track;
    b.applyTo(track);

    ASSERT_EQ(track.sectorCount(), 1u);
    EXPECT_FALSE(track.getRawSector(0)->hasData);
}

/// endregion </U11-U16>

/// region <U17-U20: variable length, FM, bitmaps>

TEST_F(DiskImage_Test, TrackLength_Variable)
{
    Track track;

    track.resizeRaw(6208);
    EXPECT_EQ(track.rawSize(), 6208u);
    EXPECT_EQ(track.clockBitmap().size(), 776u);
    EXPECT_EQ(track.sectorCount(), 0u) << "gap-filled track has no sectors";
    EXPECT_TRUE(DumpHelper::IsFilledWith(track.rawData(), 6208, 0x4E));

    track.resizeRaw(6464);
    EXPECT_EQ(track.rawSize(), 6464u);
    EXPECT_EQ(track.clockBitmap().size(), 808u);

    track.resizeRaw(3125, Encoding::FM, 0xFF);
    EXPECT_EQ(track.rawSize(), 3125u);
    EXPECT_EQ(track.encoding(), Encoding::FM);
    EXPECT_TRUE(DumpHelper::IsFilledWith(track.rawData(), 3125, 0xFF));

    track.resizeRaw(1);
    EXPECT_EQ(track.rawSize(), DiskImage::RawTrack::MIN_TRACK_SIZE);
    track.resizeRaw(1000000);
    EXPECT_EQ(track.rawSize(), DiskImage::RawTrack::MAX_TRACK_SIZE);

    // Format with a non-nominal length keeps the layout and pads with gap bytes
    Spec spec = Spec::trdos();
    spec.trackLength = 6300;
    track.formatTrack(0, 0, spec);
    EXPECT_EQ(track.rawSize(), 6300u);
    EXPECT_EQ(track.sectorCount(), 16u);
    EXPECT_TRUE(DumpHelper::IsFilledWith(track.rawData() + 6208, 92, 0x4E));
}

TEST_F(DiskImage_Test, TrackLength_FmNominal)
{
    Spec spec = Spec::ibm3740();
    EXPECT_EQ(spec.encoding, Encoding::FM);
    EXPECT_EQ(spec.trackLength, 3125u);
    EXPECT_TRUE(spec.fits()) << spec.totalBytes();

    Track track;
    track.formatTrack(1, 0, spec);

    EXPECT_EQ(track.rawSize(), 3125u);
    EXPECT_EQ(track.encoding(), Encoding::FM);
    ASSERT_EQ(track.sectorCount(), 16u);
    EXPECT_NE(track.indexMarkOffset(), Sector::NO_OFFSET);

    for (size_t i = 0; i < 16; i++)
    {
        Sector* s = track.getRawSector(i);
        EXPECT_EQ(s->number(), i + 1);
        EXPECT_EQ(s->dataSize, 128);
        EXPECT_TRUE(s->hasData);
        EXPECT_TRUE(s->idCrcValid) << "FM ID CRC (preset FFFF)";
        EXPECT_TRUE(s->dataCrcValid) << "FM data CRC";
        EXPECT_TRUE(track.clockMark(s->idamOffset)) << "FE carries the C7 clock";
        EXPECT_TRUE(track.clockMark(s->damOffset)) << "DAM carries the C7 clock";
        EXPECT_FALSE(track.clockMark(s->idamOffset - 1)) << "sync bytes have normal clock";
    }

    // An MFM-style A1 A1 A1 FE without clock marks inside an FM track is not a sector
    Sector* s = track.getRawSector(0);
    s->data[0] = 0xA1; s->data[1] = 0xA1; s->data[2] = 0xA1; s->data[3] = 0xFE;
    track.reindex();
    EXPECT_EQ(track.sectorCount(), 16u);
}

TEST_F(DiskImage_Test, ClockBitmap_RoundTrip)
{
    Track track;
    track.resizeRaw(6250);
    EXPECT_FALSE(track.hasClockMarks());

    track.setClockMark(0, true);
    track.setClockMark(7, true);
    track.setClockMark(8, true);
    track.setClockMark(6249, true);
    track.setClockMark(6250, true);  // out of range - ignored

    EXPECT_TRUE(track.clockMark(0));
    EXPECT_TRUE(track.clockMark(7));
    EXPECT_TRUE(track.clockMark(8));
    EXPECT_TRUE(track.clockMark(6249));
    EXPECT_FALSE(track.clockMark(1));
    EXPECT_FALSE(track.clockMark(6250));
    EXPECT_EQ(track.clockBitmap()[0], 0x81);
    EXPECT_EQ(track.clockBitmap()[1], 0x01);
    EXPECT_TRUE(track.hasClockMarks());

    track.setClockMark(7, false);
    EXPECT_FALSE(track.clockMark(7));
    EXPECT_EQ(track.clockBitmap()[0], 0x01);

    std::vector<uint8_t> copy = track.clockBitmap();
    Track other;
    other.resizeRaw(6250);
    other.setClockBitmap(copy.data(), copy.size());
    EXPECT_EQ(other.clockBitmap(), copy);
}

TEST_F(DiskImage_Test, WeakBits_Optional)
{
    Track track;
    EXPECT_FALSE(track.hasWeakBits());
    EXPECT_FALSE(track.weakByte(100));
    EXPECT_TRUE(track.weakBitmap().empty());

    track.setWeakByte(100, true);
    EXPECT_TRUE(track.hasWeakBits());
    EXPECT_TRUE(track.weakByte(100));
    EXPECT_FALSE(track.weakByte(101));
    EXPECT_EQ(track.weakBitmap().size(), track.clockBitmap().size());

    track.resizeRaw(6250);
    EXPECT_FALSE(track.hasWeakBits()) << "resize clears weak bits";
}

/// endregion </U17-U20>

/// region <U21-U22: sector writes and dirty tracking>

TEST_F(DiskImage_Test, WriteSectorData_ClampsToSectorSize_RecomputesCrc_MarksDirty)
{
    DiskImage disk(1, 1, Spec::plus3());
    Track* track = disk.getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->sectorCount(), 9u);

    std::vector<uint8_t> data(1024, 0xC7);
    track->writeSectorData(4, data.data(), data.size());  // sector 5 is 512 bytes

    Sector* s = track->getSector(4);
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(DumpHelper::IsFilledWith(s->data, 512, 0xC7));
    EXPECT_NE(s->data[512], 0xC7) << "write must not run past the data field (into the CRC)";
    EXPECT_TRUE(s->isDataCRCValid()) << "data CRC recomputed after write";
    EXPECT_TRUE(s->dirty);
    EXPECT_TRUE(track->isSectorDirty(4));
    EXPECT_FALSE(track->isSectorDirty(3));
    EXPECT_TRUE(track->isDirty());
    EXPECT_FALSE(track->isRawTrackDirty());
    EXPECT_TRUE(disk.isDirty());

    disk.markClean();
    EXPECT_FALSE(s->dirty);
    EXPECT_FALSE(disk.isDirty());
}

TEST_F(DiskImage_Test, DirtyTracking_InitialState)
{
    DiskImage disk(80, 2);

    EXPECT_FALSE(disk.isDirty()) << "New DiskImage should not be dirty";
    EXPECT_FALSE(disk.computeDirtyState());

    for (size_t i = 0; i < disk.getCylinders() * disk.getSides(); i++)
    {
        Track* track = disk.getTrack(static_cast<uint8_t>(i));
        ASSERT_NE(track, nullptr);
        EXPECT_FALSE(track->isDirty()) << "Track " << i;
        EXPECT_FALSE(track->isRawTrackDirty()) << "Track " << i;
        EXPECT_FALSE(track->hasAnySectorDirty()) << "Track " << i;
        EXPECT_EQ(track->cylinder(), i / 2);
        EXPECT_EQ(track->side(), i % 2);
        EXPECT_EQ(track->sectorCount(), 16u);
        EXPECT_EQ(track->getRawSector(0)->cylinder(), i / 2) << "blank track carries its own cylinder in the IDs";
        EXPECT_EQ(track->getRawSector(0)->head(), i % 2);
    }
}

TEST_F(DiskImage_Test, DirtyTracking_WriteSectorData)
{
    DiskImage disk(80, 2);
    Track* track = disk.getTrack(0);
    ASSERT_NE(track, nullptr);

    uint8_t testData[256];
    std::fill(testData, testData + 256, 0xAB);

    track->writeSectorData(1, testData, 256);

    EXPECT_TRUE(track->isDirty());
    EXPECT_TRUE(track->isSectorDirty(1));
    EXPECT_FALSE(track->isSectorDirty(0));
    EXPECT_TRUE(track->hasAnySectorDirty());
    EXPECT_TRUE(disk.isDirty());
}

TEST_F(DiskImage_Test, DirtyTracking_NoChangeNoDirty)
{
    DiskImage disk(1, 1);
    Track* track = disk.getTrack(0);
    ASSERT_NE(track, nullptr);

    Sector* sector = track->getSector(0);
    ASSERT_NE(sector, nullptr);

    uint8_t originalData[256];
    std::memcpy(originalData, sector->data, 256);

    track->writeSectorData(0, originalData, 256);

    EXPECT_FALSE(track->isDirty());
    EXPECT_FALSE(track->isSectorDirty(0));
    EXPECT_FALSE(disk.isDirty());

    track->markSectorDirtyIfChanged(0, originalData, 256);
    EXPECT_FALSE(track->isDirty());

    originalData[3] ^= 1;
    track->markSectorDirtyIfChanged(0, originalData, 256);
    EXPECT_TRUE(track->isSectorDirty(0));
}

TEST_F(DiskImage_Test, DirtyTracking_MarkClean)
{
    DiskImage disk(80, 2);
    Track* track0 = disk.getTrack(0);
    Track* track1 = disk.getTrack(1);
    ASSERT_NE(track0, nullptr);
    ASSERT_NE(track1, nullptr);

    uint8_t testData[256];
    std::fill(testData, testData + 256, 0xCD);

    track0->writeSectorData(5, testData, 256);
    track1->writeSectorData(10, testData, 256);

    EXPECT_TRUE(disk.isDirty());
    EXPECT_TRUE(track0->isDirty());
    EXPECT_TRUE(track1->isDirty());
    EXPECT_TRUE(disk.computeDirtyState());

    disk.markClean();

    EXPECT_FALSE(disk.isDirty());
    EXPECT_FALSE(track0->isDirty());
    EXPECT_FALSE(track1->isDirty());
    EXPECT_FALSE(track0->hasAnySectorDirty());
    EXPECT_FALSE(track1->hasAnySectorDirty());
    EXPECT_FALSE(disk.computeDirtyState());
}

TEST_F(DiskImage_Test, DirtyTracking_MultipleSectors)
{
    DiskImage disk(1, 1);
    Track* track = disk.getTrack(0);
    ASSERT_NE(track, nullptr);

    uint8_t testData[256];

    std::fill(testData, testData + 256, 0x01);
    track->writeSectorData(0, testData, 256);
    std::fill(testData, testData + 256, 0x05);
    track->writeSectorData(5, testData, 256);
    std::fill(testData, testData + 256, 0x0F);
    track->writeSectorData(15, testData, 256);

    EXPECT_TRUE(track->isSectorDirty(0));
    EXPECT_FALSE(track->isSectorDirty(1));
    EXPECT_TRUE(track->isSectorDirty(5));
    EXPECT_FALSE(track->isSectorDirty(10));
    EXPECT_TRUE(track->isSectorDirty(15));
    EXPECT_TRUE(track->hasAnySectorDirty());
}

/// endregion </U21-U22>

/// region <U23-U25: image addressing, moves, caps>

TEST_F(DiskImage_Test, DiskImage_TrackAddressing)
{
    DiskImage diskImage(80, 2);
    EXPECT_EQ(diskImage.getCylinders(), 80);
    EXPECT_EQ(diskImage.getSides(), 2);

    std::set<const void*> seen;
    for (uint8_t cylinder = 0; cylinder < 80; cylinder++)
    {
        for (uint8_t side = 0; side < 2; side++)
        {
            Track* track = diskImage.getTrackForCylinderAndSide(cylinder, side);
            ASSERT_NE(track, nullptr) << "cylinder " << (int)cylinder << " side " << (int)side;
            EXPECT_EQ(track, diskImage.getTrack(static_cast<uint8_t>(cylinder * 2 + side)));
            EXPECT_TRUE(seen.insert(track).second) << "track objects must be unique";
            EXPECT_TRUE(seen.insert(track->rawData()).second) << "track buffers must be unique";
            EXPECT_EQ(track->getDiskImage(), &diskImage);
            EXPECT_EQ(track->cylinder(), cylinder);
            EXPECT_EQ(track->side(), side);
        }
    }

    EXPECT_EQ(diskImage.getTrackForCylinderAndSide(80, 0), nullptr);
    EXPECT_EQ(diskImage.getTrackForCylinderAndSide(0, 2), nullptr);
    EXPECT_EQ(diskImage.getTrack(160), nullptr);

    // Single sided image: track index == cylinder
    DiskImage single(40, 1);
    EXPECT_EQ(single.getTrackForCylinderAndSide(3, 0), single.getTrack(3));
    EXPECT_EQ(single.getTrackForCylinderAndSide(3, 1), nullptr);

    // Clamping
    DiskImage clamped(200, 5);
    EXPECT_EQ(clamped.getCylinders(), MAX_CYLINDERS);
    EXPECT_EQ(clamped.getSides(), 2);
}

TEST_F(DiskImage_Test, Track_Move_KeepsSectorPointersValid)
{
    Track track;
    track.formatTrack(0, 0);
    track.getSector(0)->data[0] = 0x42;
    const uint8_t* buffer = track.rawData();

    std::vector<Track> tracks;
    tracks.push_back(std::move(track));

    Track& moved = tracks[0];
    EXPECT_EQ(moved.rawData(), buffer) << "heap buffer moved, not copied";
    ASSERT_EQ(moved.sectorCount(), 16u);
    EXPECT_EQ(moved.getSector(0)->data, buffer + 70);
    EXPECT_EQ(moved.getSector(0)->data[0], 0x42);
    EXPECT_TRUE(moved.getSector(0)->isDataCRCValid() == false) << "data changed without CRC update stays detectable";
}

TEST_F(DiskImage_Test, Sector_Cap_255)
{
    // 300 ID-only fields of 22 bytes each (gap-free) = 6600 bytes
    StreamBuilder b(12000);
    for (int i = 0; i < 300; i++)
    {
        b.idField(0, 0, static_cast<uint8_t>(i & 0xFF), 0);
    }

    Track track;
    b.applyTo(track);
    EXPECT_EQ(track.sectorCount(), DiskImage::RawTrack::MAX_SECTORS_PER_TRACK);
}

TEST_F(DiskImage_Test, TrackFormatSpec_Fits)
{
    Spec spec = Spec::trdos();
    EXPECT_EQ(spec.totalBytes(), 6208u);
    EXPECT_TRUE(spec.fits());

    spec.gapPostData = 70;  // 16 x 398 = 6368 > 6250
    EXPECT_FALSE(spec.fits());

    spec.trackLength = 6464;
    EXPECT_TRUE(spec.fits());

    Spec big = Spec::ibm(30, DiskImage::SECTOR_SIZE_512, 1, 0x10);
    EXPECT_FALSE(big.fits());
}

/// endregion </U23-U25>
