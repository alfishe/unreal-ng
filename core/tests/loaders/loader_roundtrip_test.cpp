#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include <common/filehelper.h>
#include <common/stringhelper.h>

#include "_helpers/testpathhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"
#include "loaders/disk/loader_scl.h"
#include "loaders/disk/loader_trd.h"

/// Loader regression gates on the universal track model
/// (docs/inprogress/2026-09-02-universal-track-model/test-plan.md, section 4):
/// TRD and SCL images must survive load -> save byte for byte, and the strict formats must refuse
/// geometries they cannot represent instead of truncating.

class LoaderRoundTrip_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    CoreCUT* _core = nullptr;
    Z80* _z80 = nullptr;

protected:
    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _context->pModuleLogger->TurnOffLoggingForAll();

        _core = new CoreCUT(_context);
        _z80 = new Z80(_context);
        _core->_z80 = _z80;
        _context->pCore = _core;
    }

    void TearDown() override
    {
        if (_context)
        {
            if (_context->pCore)
            {
                _core->_z80 = nullptr;
                delete _z80;
                _context->pCore = nullptr;
                delete _core;
            }
            delete _context;
        }
    }

    static std::vector<uint8_t> readFile(const std::string& path)
    {
        std::vector<uint8_t> result;
        if (FileHelper::FileExists(path))
        {
            size_t size = FileHelper::GetFileSize(path);
            result.resize(size);
            if (size > 0)
            {
                FileHelper::ReadFileToBuffer(path, result.data(), size);
            }
        }
        return result;
    }

    static void removeFile(const std::string& path)
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

/// region <TRD>

/// L1: every TRD fixture written back is byte-identical to the input
TEST_F(LoaderRoundTrip_Test, TRD_RoundTrip_ByteIdentical)
{
    const char* fixtures[] = { "loaders/trd/EyeAche.trd", "loaders/trd/Satisfaction.trd",
                               "loaders/trd/atarin.trd", "loaders/trd/zx-format8.trd" };

    for (const char* fixture : fixtures)
    {
        std::string source = TestPathHelper::GetTestDataPath(fixture);
        std::string target = TestPathHelper::GetTestScratchPath("roundtrip.trd");
        removeFile(target);

        LoaderTRD loader(_context, source);
        ASSERT_TRUE(loader.loadImage()) << fixture;
        DiskImage* image = loader.getImage();
        ASSERT_NE(image, nullptr);
        EXPECT_TRUE(LoaderTRD::isTrdosGeometry(image));

        // Every track keeps the exact legacy MFM layout
        for (size_t t = 0; t < static_cast<size_t>(image->getCylinders()) * image->getSides(); t++)
        {
            DiskImage::Track* track = image->getTrack(static_cast<uint8_t>(t));
            ASSERT_EQ(track->rawSize(), 6250u);
            ASSERT_EQ(track->sectorCount(), 16u);
        }

        ASSERT_TRUE(loader.writeImage(target)) << fixture;
        EXPECT_FALSE(image->isDirty());
        EXPECT_EQ(image->getFilePath(), target);

        std::vector<uint8_t> in = readFile(source);
        std::vector<uint8_t> out = readFile(target);
        ASSERT_EQ(out.size(), in.size()) << fixture;
        EXPECT_EQ(std::memcmp(in.data(), out.data(), in.size()), 0) << fixture << " differs after round trip";

        removeFile(target);
        delete image;
    }
}

/// L2: volume sector of a loaded image is reachable by sector number, with valid CRCs
TEST_F(LoaderRoundTrip_Test, TRD_Load_SectorsIndexedByNumber)
{
    LoaderTRD loader(_context, TestPathHelper::GetTestDataPath("loaders/trd/EyeAche.trd"));
    ASSERT_TRUE(loader.loadImage());
    DiskImage* image = loader.getImage();

    DiskImage::Track* track0 = image->getTrackForCylinderAndSide(0, 0);
    DiskImage::Sector* volume = track0->getSector(TRD_VOLUME_SECTOR);
    ASSERT_NE(volume, nullptr);
    EXPECT_EQ(volume->number(), 9);
    EXPECT_EQ(volume->dataSize, 256);
    EXPECT_TRUE(volume->isIDCRCValid());
    EXPECT_TRUE(volume->isDataCRCValid());
    EXPECT_EQ(volume->data[0xE7], TRD_SIGNATURE);
    EXPECT_TRUE(track0->hasClockMarks()) << "formatter records clock marks so a UDI save is honest";

    TRDValidationReport report;
    EXPECT_TRUE(loader.validateTRDOSImage(image, report));

    delete image;
}

/// TRD holds only 16 x 256-byte sectors: a foreign track makes the save refuse without writing anything
TEST_F(LoaderRoundTrip_Test, TRD_WriteImage_RefusesNonTrdosGeometry)
{
    std::string target = TestPathHelper::GetTestScratchPath("refused.trd");
    removeFile(target);

    DiskImage image(80, 2);
    LoaderTRD loader(_context, target);
    ASSERT_TRUE(loader.format(&image));
    loader.setImage(&image);

    // Re-format one track as +3 (9 x 512)
    image.getTrackForCylinderAndSide(12, 0)->formatTrack(12, 0, DiskImage::TrackFormatSpec::plus3());

    std::string reason;
    EXPECT_FALSE(LoaderTRD::isTrdosGeometry(&image, &reason));
    EXPECT_NE(reason.find("cylinder 12"), std::string::npos) << reason;

    EXPECT_FALSE(loader.writeImage(target));
    EXPECT_FALSE(FileHelper::FileExists(target)) << "nothing must be written";
    ASSERT_FALSE(loader.lastWarnings().empty());
    EXPECT_NE(loader.lastWarnings()[0].find("TRD save refused"), std::string::npos) << loader.lastWarnings()[0];

    // Missing sector number is refused as well
    image.getTrackForCylinderAndSide(12, 0)->formatTrack(12, 0);
    ASSERT_TRUE(LoaderTRD::isTrdosGeometry(&image));
    uint8_t order[15] = {1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16};  // no sector 7
    image.getTrackForCylinderAndSide(3, 1)->formatTrack(3, 1, DiskImage::TrackFormatSpec::trdos(order, 15));
    EXPECT_FALSE(loader.writeImage(target));
    EXPECT_FALSE(FileHelper::FileExists(target));

    // Back to a valid geometry: the save works again
    image.getTrackForCylinderAndSide(3, 1)->formatTrack(3, 1);
    EXPECT_TRUE(loader.writeImage(target));
    EXPECT_TRUE(FileHelper::FileExists(target));
    removeFile(target);
}

/// endregion </TRD>

/// region <SCL>

/// L3: SCL -> disk -> SCL keeps every file (catalog entry and body) - byte-identical container
TEST_F(LoaderRoundTrip_Test, SCL_RoundTrip_ByteIdentical)
{
    const char* fixtures[] = { "loaders/scl/eyeache2.scl", "loaders/scl/insult.scl" };

    for (const char* fixture : fixtures)
    {
        std::string source = TestPathHelper::GetTestDataPath(fixture);
        std::string target = TestPathHelper::GetTestScratchPath("roundtrip.scl");
        removeFile(target);

        LoaderSCL loader(_context, source);
        ASSERT_TRUE(loader.loadImage()) << fixture;
        DiskImage* image = loader.getImage();
        ASSERT_NE(image, nullptr);

        ASSERT_TRUE(loader.writeImage(target)) << fixture;
        EXPECT_TRUE(loader.lastWarnings().empty());

        std::vector<uint8_t> in = readFile(source);
        std::vector<uint8_t> out = readFile(target);
        ASSERT_EQ(out.size(), in.size()) << fixture;
        EXPECT_EQ(std::memcmp(in.data(), out.data(), in.size()), 0) << fixture << " differs after round trip";

        removeFile(target);
        delete image;
    }
}

/// L4: SCL -> TRD -> SCL
TEST_F(LoaderRoundTrip_Test, SCL_To_TRD_To_SCL)
{
    std::string source = TestPathHelper::GetTestDataPath("loaders/scl/eyeache2.scl");
    std::string trdPath = TestPathHelper::GetTestScratchPath("via.trd");
    std::string sclPath = TestPathHelper::GetTestScratchPath("via.scl");
    removeFile(trdPath);
    removeFile(sclPath);

    LoaderSCL scl(_context, source);
    ASSERT_TRUE(scl.loadImage());
    DiskImage* image = scl.getImage();

    LoaderTRD trd(_context, trdPath);
    trd.setImage(image);
    ASSERT_TRUE(trd.writeImage(trdPath));
    delete image;

    LoaderTRD trdBack(_context, trdPath);
    ASSERT_TRUE(trdBack.loadImage());
    DiskImage* reloaded = trdBack.getImage();

    LoaderSCL sclOut(_context, sclPath);
    sclOut.setImage(reloaded);
    ASSERT_TRUE(sclOut.writeImage(sclPath));

    std::vector<uint8_t> in = readFile(source);
    std::vector<uint8_t> out = readFile(sclPath);
    ASSERT_EQ(out.size(), in.size());
    EXPECT_EQ(std::memcmp(in.data(), out.data(), in.size()), 0);

    removeFile(trdPath);
    removeFile(sclPath);
    delete reloaded;
}

/// SCL export walks the TR-DOS catalog: deleted entries are skipped, the header counts exported files only
TEST_F(LoaderRoundTrip_Test, SCL_WriteImage_SkipsDeletedFiles)
{
    std::string source = TestPathHelper::GetTestDataPath("loaders/scl/insult.scl");
    std::string target = TestPathHelper::GetTestScratchPath("deleted.scl");
    removeFile(target);

    LoaderSCL loader(_context, source);
    ASSERT_TRUE(loader.loadImage());
    DiskImage* image = loader.getImage();

    DiskImage::Track* track0 = image->getTrack(0);
    TRDVolumeInfo* volume = (TRDVolumeInfo*)track0->getSector(TRD_VOLUME_SECTOR)->data;
    const uint8_t originalCount = volume->fileCount;
    ASSERT_GE(originalCount, 2);

    // Mark the first catalog entry as deleted (TR-DOS convention: first name byte = 0x01)
    DiskImage::Sector* catalog = track0->getSector(0);
    TRDOSDirectoryEntry* entries = (TRDOSDirectoryEntry*)catalog->data;
    std::string secondName(entries[1].Name, 8);
    entries[0].Name[0] = 0x01;
    catalog->recalculateDataCRC();
    volume->deletedFileCount = 1;
    track0->getSector(TRD_VOLUME_SECTOR)->recalculateDataCRC();

    ASSERT_TRUE(loader.writeImage(target));

    LoaderSCL reload(_context, target);
    ASSERT_TRUE(reload.loadImage());
    DiskImage* exported = reload.getImage();
    TRDVolumeInfo* exportedVolume = (TRDVolumeInfo*)exported->getTrack(0)->getSector(TRD_VOLUME_SECTOR)->data;
    EXPECT_EQ(exportedVolume->fileCount, originalCount - 1);
    TRDOSDirectoryEntry* exportedEntries = (TRDOSDirectoryEntry*)exported->getTrack(0)->getSector(0)->data;
    EXPECT_EQ(std::string(exportedEntries[0].Name, 8), secondName) << "second file became the first one";

    std::vector<uint8_t> out = readFile(target);
    ASSERT_GT(out.size(), 9u);
    EXPECT_EQ(out[8], originalCount - 1) << "SCL header carries the exported count";

    removeFile(target);
    delete exported;
    delete image;
}

/// SCL export needs a TR-DOS track 0
TEST_F(LoaderRoundTrip_Test, SCL_WriteImage_RefusesNonTrdosTrack0)
{
    std::string target = TestPathHelper::GetTestScratchPath("refused.scl");
    removeFile(target);

    DiskImage image(80, 2);
    LoaderTRD trd(_context, target);
    ASSERT_TRUE(trd.format(&image));
    image.getTrackForCylinderAndSide(0, 0)->formatTrack(0, 0, DiskImage::TrackFormatSpec::plus3());

    LoaderSCL loader(_context, target);
    loader.setImage(&image);
    EXPECT_FALSE(loader.writeImage(target));
    EXPECT_FALSE(FileHelper::FileExists(target));
    ASSERT_FALSE(loader.lastWarnings().empty());
    EXPECT_NE(loader.lastWarnings()[0].find("SCL save refused"), std::string::npos);
}

/// endregion </SCL>
