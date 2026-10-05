// SectorWrite (sectorwrite.h): a debugger's disk sector write into the data field, as the WD1793 WRITE SECTOR puts
// it there - CRC recalculated, image modified, address mark kept; refusals with their reasons; a TTD tool edit
// (docs/inprogress/2026-10-04-debugger-additions/tdd.md §3).

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "base/featuremanager.h"
#include "debugger/media/sectorwrite.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/fdd.h"

class SectorWrite_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("sectorwrite-test", "PENTAGON",
                                                                            LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        std::string error;
        // A formatted disk: +3DOS layout, 9 sectors of 512 bytes (IDs 1-9) per track, filler #E5
        ASSERT_TRUE(_emulator->CreateBlankDisk(0, Emulator::BlankDiskFormat::Plus3, 40, 1, &error)) << error;
        _image = _context->coreState.diskDrives[0]->getDiskImage();
        ASSERT_NE(_image, nullptr);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }

    DiskImage::Sector* Sector(int cylinder, int id)
    {
        DiskImage::Track* track = _image->getTrackForCylinderAndSide(static_cast<uint8_t>(cylinder), 0);
        return track ? track->findSector(static_cast<uint8_t>(id)) : nullptr;
    }

    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    DiskImage* _image = nullptr;
};

TEST_F(SectorWrite_Test, BytesLandInTheDataField)
{
    DiskImage::Sector* sector = Sector(2, 5);
    ASSERT_NE(sector, nullptr);
    ASSERT_EQ(sector->dataSize, 512);
    const uint8_t idBefore = sector->id->sector;
    ASSERT_FALSE(_image->isDirty());

    const SectorWrite::Result result =
        SectorWrite::Write(_emulator.get(), 0, 2, 0, 5, 100, {'M', 'Y', 'D', 'I', 'S', 'K'}, "test");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.moment, "stopped");
    EXPECT_EQ(result.sectorSize, 512);

    sector = Sector(2, 5);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(sector->data + 100), 6), "MYDISK");
    EXPECT_EQ(sector->data[99], 0xE5) << "bytes before the offset kept";
    EXPECT_EQ(sector->data[106], 0xE5) << "bytes after kept";
    EXPECT_TRUE(sector->isDataCRCValid()) << "the data CRC follows, as after WRITE SECTOR";
    EXPECT_EQ(sector->id->sector, idBefore);
    EXPECT_TRUE(sector->isIDCRCValid()) << "the address mark untouched";
    EXPECT_TRUE(_image->isDirty()) << "the image counts as modified (save on eject)";
    EXPECT_EQ(Sector(2, 4)->data[100], 0xE5) << "only that sector";
}

TEST_F(SectorWrite_Test, RefusalsNameTheReason)
{
    auto error = [&](uint8_t drive, int cylinder, int sector, uint32_t offset, size_t length) {
        const SectorWrite::Result result =
            SectorWrite::Write(_emulator.get(), drive, cylinder, 0, sector, offset, std::vector<uint8_t>(length, 1), "test");
        EXPECT_FALSE(result.ok);
        return result.error;
    };
    EXPECT_NE(error(0, 0, 1, 510, 3).find("past the 512-byte data field"), std::string::npos);
    EXPECT_NE(error(0, 0, 1, 512, 1).find("past"), std::string::npos);
    EXPECT_NE(error(0, 0, 10, 0, 1).find("no sector with ID 10"), std::string::npos);
    EXPECT_NE(error(0, 60, 1, 0, 1).find("no track"), std::string::npos);
    EXPECT_NE(error(1, 0, 1, 0, 1).find("no disk in drive B"), std::string::npos);
    EXPECT_NE(error(0, 0, 1, 0, 0).find("no bytes"), std::string::npos);

    _context->coreState.diskDrives[0]->setWriteProtect(true);
    EXPECT_NE(error(0, 0, 1, 0, 1).find("write-protected"), std::string::npos);
    EXPECT_FALSE(_image->isDirty()) << "nothing written by a refusal";
}

TEST_F(SectorWrite_Test, Parsers)
{
    uint8_t drive = 9;
    std::string error;
    EXPECT_TRUE(SectorWrite::ParseDrive("b", drive, error));
    EXPECT_EQ(drive, 1);
    EXPECT_TRUE(SectorWrite::ParseDrive("3", drive, error));
    EXPECT_EQ(drive, 3);
    EXPECT_FALSE(SectorWrite::ParseDrive("E", drive, error));

    std::vector<uint8_t> bytes;
    EXPECT_TRUE(SectorWrite::ParseHex("4D 59,4d", bytes, error)) << error;
    EXPECT_EQ(bytes, (std::vector<uint8_t>{0x4D, 0x59, 0x4D}));
    EXPECT_FALSE(SectorWrite::ParseHex("4D5", bytes, error));
    EXPECT_FALSE(SectorWrite::ParseHex("XY", bytes, error));
    EXPECT_FALSE(SectorWrite::ParseHex("", bytes, error));
}

TEST_F(SectorWrite_Test, RecordingMarksTheEdit)
{
    _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    ttd::TimeTravelManager* ttd = _context->pTimeTravelManager;
    _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    ASSERT_TRUE(ttd->StartRecording());
    _emulator->RunNFrames(1, true);
    ASSERT_TRUE(SectorWrite::Write(_emulator.get(), 0, 0, 0, 1, 0, {0x42}, "test").ok);
    _emulator->RunNFrames(1, true);
    ttd->StopRecording();
    const auto markers = ttd->GetExternalEvents().SnapshotEvents();
    ASSERT_EQ(markers.size(), 1u);
    EXPECT_EQ(markers.front().kind, ttd::TTDExternalEventKind::DebuggerEdit);
    EXPECT_EQ(Sector(0, 1)->data[0], 0x42);
}
