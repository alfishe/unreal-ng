#include "loaders/disk/loader_udi.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <vector>

#include <common/filehelper.h>
#include <common/stringhelper.h>

#include "_helpers/testpathhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/wd1793.h"
#include "loaders/disk/loader_trd.h"

/// UDI loader tests (docs/inprogress/2026-09-02-universal-track-model/loader-udi.md, section 4)

using Spec = DiskImage::TrackFormatSpec;

class LoaderUDI_Test : public ::testing::Test
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
            if (size > 0) FileHelper::ReadFileToBuffer(path, result.data(), size);
        }
        return result;
    }

    static void removeFile(const std::string& path)
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    static std::string scratch(const char* name) { return TestPathHelper::GetTestScratchPath(name); }
    static std::string fixture(const char* name) { return TestPathHelper::GetTestDataPath(name); }

    /// Fresh in-memory UDI of a synthetic image (via the loader itself)
    std::vector<uint8_t> serializeImage(DiskImage& image)
    {
        LoaderUDI loader(_context, "");
        std::vector<uint8_t> out;
        std::vector<std::string> warnings;
        EXPECT_TRUE(loader.serialize(&image, out, warnings));
        EXPECT_TRUE(warnings.empty());
        return out;
    }

    static bool tracksEqual(DiskImage::Track* a, DiskImage::Track* b)
    {
        return a->rawSize() == b->rawSize() && a->encoding() == b->encoding() &&
               std::memcmp(a->rawData(), b->rawData(), a->rawSize()) == 0 && a->clockBitmap() == b->clockBitmap();
    }
};

/// region <Detection and validation>

TEST_F(LoaderUDI_Test, Detect_Signature)
{
    const uint8_t good[] = {'U', 'D', 'I', '!', 0, 0, 0, 0};
    const uint8_t packed[] = {'u', 'd', 'i', '!', 0, 0, 0, 0};
    const uint8_t other[] = {'F', 'D', 'I', 0, 0, 0, 0, 0};
    bool compressed = true;

    EXPECT_TRUE(LoaderUDI::detect(good, sizeof(good), &compressed));
    EXPECT_FALSE(compressed);
    EXPECT_TRUE(LoaderUDI::detect(packed, sizeof(packed), &compressed));
    EXPECT_TRUE(compressed);
    EXPECT_FALSE(LoaderUDI::detect(other, sizeof(other), &compressed));
    EXPECT_FALSE(LoaderUDI::detect(good, 3, nullptr));
    EXPECT_FALSE(LoaderUDI::detect(nullptr, 0, nullptr));
}

TEST_F(LoaderUDI_Test, Reject_Compressed_And_BadVersion)
{
    DiskImage image(1, 1);
    std::vector<uint8_t> data = serializeImage(image);

    // Compressed signature
    std::vector<uint8_t> packed = data;
    std::memcpy(packed.data(), "udi!", 4);
    LoaderUDI loader(_context, "");
    std::vector<std::string> warnings;
    EXPECT_EQ(loader.parse(packed.data(), packed.size(), warnings), nullptr);
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings[0].find("Compressed"), std::string::npos);

    // Version byte
    std::vector<uint8_t> versioned = data;
    versioned[8] = 1;
    uint32_t crc = LoaderUDI::computeCrc(versioned.data(), versioned.size() - 4);
    std::memcpy(versioned.data() + versioned.size() - 4, &crc, 4);
    warnings.clear();
    EXPECT_EQ(loader.parse(versioned.data(), versioned.size(), warnings), nullptr);
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings[0].find("version"), std::string::npos);

    // Truncated
    warnings.clear();
    EXPECT_EQ(loader.parse(data.data(), 10, warnings), nullptr);
}

TEST_F(LoaderUDI_Test, Crc_Verified)
{
    std::vector<uint8_t> data = readFile(fixture("loaders/udi/beta128-empty.udi"));
    ASSERT_GT(data.size(), 20u);

    // The reference CRC of the fixture (signed arithmetic-shift CRC-32 variant used by every UDI tool)
    uint32_t stored;
    std::memcpy(&stored, data.data() + data.size() - 4, 4);
    EXPECT_EQ(stored, 0xEA4E96EFu);
    EXPECT_EQ(LoaderUDI::computeCrc(data.data(), data.size() - 4), stored);

    // Flip one data byte: strict load fails, ignoreCrc loads with a warning
    data[1000] ^= 0x01;
    LoaderUDI strict(_context, "");
    std::vector<std::string> warnings;
    EXPECT_EQ(strict.parse(data.data(), data.size(), warnings), nullptr);
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings[0].find("CRC mismatch"), std::string::npos);

    LoaderUDI lenient(_context, "");
    lenient.setIgnoreCrc(true);
    warnings.clear();
    DiskImage* image = lenient.parse(data.data(), data.size(), warnings);
    ASSERT_NE(image, nullptr);
    EXPECT_FALSE(warnings.empty());
    delete image;
}

/// endregion </Detection and validation>

/// region <Fixtures>

TEST_F(LoaderUDI_Test, Load_Beta128Empty_IsValidTrdos)
{
    LoaderUDI loader(_context, fixture("loaders/udi/beta128-empty.udi"));
    ASSERT_TRUE(loader.loadImage()) << (loader.lastWarnings().empty() ? "" : loader.lastWarnings()[0]);
    DiskImage* image = loader.getImage();
    ASSERT_NE(image, nullptr);

    EXPECT_EQ(image->getCylinders(), 86);
    EXPECT_EQ(image->getSides(), 2);
    EXPECT_TRUE(image->getLoaded());
    EXPECT_FALSE(image->isDirty());
    EXPECT_EQ(image->getFilePath(), fixture("loaders/udi/beta128-empty.udi"));

    for (size_t t = 0; t < 172; t++)
    {
        DiskImage::Track* track = image->getTrack(static_cast<uint8_t>(t));
        ASSERT_NE(track, nullptr);
        EXPECT_EQ(track->rawSize(), 6250u) << "track " << t;
        EXPECT_EQ(track->encoding(), DiskImage::Encoding::MFM);
        EXPECT_TRUE(track->hasClockMarks());
        EXPECT_EQ(track->sectorCount(), 16u) << "track " << t;
        for (uint8_t s = 0; s < 16; s++)
        {
            DiskImage::Sector* sector = track->getSector(s);
            ASSERT_NE(sector, nullptr) << "track " << t << " sector " << (int)s + 1;
            EXPECT_EQ(sector->dataSize, 256);
            EXPECT_TRUE(sector->idCrcValid);
            EXPECT_TRUE(sector->dataCrcValid);
        }
    }

    // The image is a formatted, empty TR-DOS disk
    LoaderTRD trd(_context, "");
    TRDValidationReport report;
    EXPECT_TRUE(trd.validateEmptyTRDOSImage(image, report));
    EXPECT_TRUE(LoaderTRD::isTrdosGeometry(image));

    delete image;
}

TEST_F(LoaderUDI_Test, Load_ZvezdnoeNasledie_RealDriveDump)
{
    LoaderUDI loader(_context, fixture("loaders/udi/Zvezdnoe Nasledie.udi"));
    ASSERT_TRUE(loader.loadImage()) << (loader.lastWarnings().empty() ? "" : loader.lastWarnings()[0]);
    DiskImage* image = loader.getImage();
    ASSERT_NE(image, nullptr);

    EXPECT_EQ(image->getCylinders(), 80);
    EXPECT_EQ(image->getSides(), 2);

    // Real-drive dump: 6400-byte tracks (fast drive), not the nominal 6250
    size_t tracksWithSectors = 0;
    for (size_t t = 0; t < 160; t++)
    {
        DiskImage::Track* track = image->getTrack(static_cast<uint8_t>(t));
        ASSERT_NE(track, nullptr);
        EXPECT_EQ(track->rawSize(), 6400u) << "track " << t;
        EXPECT_NE(track->rawSize(), DiskImage::RawTrack::RAW_TRACK_SIZE) << "variable-length path must be exercised";
        if (track->sectorCount() > 0) tracksWithSectors++;
    }
    EXPECT_GT(tracksWithSectors, 150u);

    // Track 0 side 0 is a TR-DOS system track
    DiskImage::Track* track0 = image->getTrackForCylinderAndSide(0, 0);
    EXPECT_EQ(track0->sectorCount(), 16u);
    DiskImage::Sector* volume = track0->getSector(TRD_VOLUME_SECTOR);
    ASSERT_NE(volume, nullptr);
    EXPECT_EQ(volume->data[0xE7], TRD_SIGNATURE);

    // The TRX2X comment between the last track and the CRC is preserved
    LoaderUDICUT& cut = static_cast<LoaderUDICUT&>(loader);
    EXPECT_FALSE(cut._trailer.empty());
    EXPECT_NE(std::string(cut._trailer.begin(), cut._trailer.end()).find("TRX2X"), std::string::npos);

    delete image;
}

TEST_F(LoaderUDI_Test, ClockBitmap_DrivesIndex)
{
    LoaderUDI loader(_context, fixture("loaders/udi/beta128-empty.udi"));
    ASSERT_TRUE(loader.loadImage());
    DiskImage* image = loader.getImage();

    DiskImage::Track* track = image->getTrackForCylinderAndSide(5, 1);
    ASSERT_EQ(track->sectorCount(), 16u);

    // Clock marks sit exactly on the A1 bytes of the legacy layout
    for (size_t s = 0; s < 16; s++)
    {
        size_t base = s * 388;
        EXPECT_TRUE(track->clockMark(base + 22) && track->clockMark(base + 24) && track->clockMark(base + 66));
        EXPECT_FALSE(track->clockMark(base + 25) || track->clockMark(base + 70));
    }

    // Without the clock information the scanner falls back to byte patterns and still finds the sectors
    std::vector<uint8_t> empty(track->clockBitmap().size(), 0);
    track->setClockBitmap(empty.data(), empty.size());
    EXPECT_FALSE(track->hasClockMarks());
    EXPECT_EQ(track->sectorCount(), 16u);

    // Marking the A1 bytes of one sector as ordinary data removes that sector from the index (strict mode)
    LoaderUDI loader2(_context, fixture("loaders/udi/beta128-empty.udi"));
    ASSERT_TRUE(loader2.loadImage());
    DiskImage::Track* track2 = loader2.getImage()->getTrackForCylinderAndSide(5, 1);
    const uint8_t victimNumber = track2->getRawSector(3)->number();  // physical position 3 (interleaved layout)
    track2->setClockMark(3 * 388 + 22, false);
    track2->reindex();
    EXPECT_EQ(track2->sectorCount(), 15u);
    EXPECT_EQ(track2->findSector(victimNumber), nullptr);

    delete loader2.getImage();
    delete image;
}

/// endregion </Fixtures>

/// region <Save>

TEST_F(LoaderUDI_Test, Save_RoundTrip_ByteExact)
{
    const char* fixtures[] = {"loaders/udi/beta128-empty.udi", "loaders/udi/Zvezdnoe Nasledie.udi"};

    for (const char* name : fixtures)
    {
        std::string source = fixture(name);
        std::string target = scratch("roundtrip.udi");
        removeFile(target);

        LoaderUDI loader(_context, source);
        ASSERT_TRUE(loader.loadImage()) << name;
        DiskImage* image = loader.getImage();

        ASSERT_TRUE(loader.writeImage(target)) << name;
        EXPECT_TRUE(loader.lastWarnings().empty());
        EXPECT_FALSE(image->isDirty());
        EXPECT_EQ(image->getFilePath(), target);

        std::vector<uint8_t> in = readFile(source);
        std::vector<uint8_t> out = readFile(target);
        ASSERT_EQ(out.size(), in.size()) << name;
        EXPECT_EQ(std::memcmp(in.data(), out.data(), in.size()), 0) << name << " differs after round trip";

        removeFile(target);
        delete image;
    }
}

TEST_F(LoaderUDI_Test, Save_FromTrd_And_Back)
{
    std::string trdSource = fixture("loaders/trd/EyeAche.trd");
    std::string udiPath = scratch("from-trd.udi");
    std::string trdPath = scratch("from-udi.trd");
    removeFile(udiPath);
    removeFile(trdPath);

    LoaderTRD trd(_context, trdSource);
    ASSERT_TRUE(trd.loadImage());
    DiskImage* image = trd.getImage();

    LoaderUDI udi(_context, udiPath);
    udi.setImage(image);
    ASSERT_TRUE(udi.writeImage());
    EXPECT_TRUE(udi.lastWarnings().empty());
    delete image;

    // UDI -> model -> TRD equals the original TRD
    LoaderUDI udiBack(_context, udiPath);
    ASSERT_TRUE(udiBack.loadImage());
    DiskImage* reloaded = udiBack.getImage();
    EXPECT_EQ(reloaded->getCylinders(), 80);
    EXPECT_EQ(reloaded->getSides(), 2);

    LoaderTRD trdOut(_context, trdPath);
    trdOut.setImage(reloaded);
    ASSERT_TRUE(trdOut.writeImage(trdPath));

    std::vector<uint8_t> in = readFile(trdSource);
    std::vector<uint8_t> out = readFile(trdPath);
    ASSERT_EQ(out.size(), in.size());
    EXPECT_EQ(std::memcmp(in.data(), out.data(), in.size()), 0);

    // Saving the same image twice produces identical files (deterministic, no timestamps)
    std::vector<uint8_t> first = readFile(udiPath);
    ASSERT_TRUE(udiBack.writeImage(udiPath));
    std::vector<uint8_t> second = readFile(udiPath);
    EXPECT_EQ(first, second);

    removeFile(udiPath);
    removeFile(trdPath);
    delete reloaded;
}

TEST_F(LoaderUDI_Test, Save_PreservesNonTrdosContent)
{
    // Mixed image: TR-DOS tracks, a +3 track, an FM track, a track with a bad CRC and a deleted mark,
    // a 6464-byte track, an ID-only sector and duplicate sector numbers
    DiskImage image(4, 2);
    image.getTrackForCylinderAndSide(1, 0)->formatTrack(1, 0, Spec::plus3());
    image.getTrackForCylinderAndSide(1, 1)->formatTrack(1, 1, Spec::ibm3740());

    Spec longSpec = Spec::trdos();
    longSpec.trackLength = 6464;
    image.getTrackForCylinderAndSide(2, 0)->formatTrack(2, 0, longSpec);

    uint8_t dupOrder[16] = {1, 2, 3, 4, 5, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    Spec dupSpec = Spec::trdos(dupOrder, 16);
    dupSpec.idOnly.assign(16, 0);
    dupSpec.idOnly[7] = 1;
    image.getTrackForCylinderAndSide(2, 1)->formatTrack(2, 1, dupSpec);

    DiskImage::Track* damaged = image.getTrackForCylinderAndSide(3, 0);
    damaged->getSector(2)->data[7] ^= 0xFF;         // data CRC now wrong
    damaged->getSector(4)->setDataAddressMark(0xF8);
    damaged->getSector(4)->recalculateDataCRC();
    damaged->getSector(6)->id->id_crc ^= 0x0F0F;    // ID CRC wrong
    damaged->reindex();

    std::vector<uint8_t> data = serializeImage(image);

    LoaderUDI loader(_context, "");
    std::vector<std::string> warnings;
    DiskImage* reloaded = loader.parse(data.data(), data.size(), warnings);
    ASSERT_NE(reloaded, nullptr) << (warnings.empty() ? "" : warnings[0]);
    EXPECT_TRUE(warnings.empty());

    for (uint8_t c = 0; c < 4; c++)
    {
        for (uint8_t h = 0; h < 2; h++)
        {
            DiskImage::Track* a = image.getTrackForCylinderAndSide(c, h);
            DiskImage::Track* b = reloaded->getTrackForCylinderAndSide(c, h);
            EXPECT_TRUE(tracksEqual(a, b)) << "cylinder " << (int)c << " side " << (int)h;
            EXPECT_EQ(a->sectorCount(), b->sectorCount());
        }
    }

    DiskImage::Track* plus3 = reloaded->getTrackForCylinderAndSide(1, 0);
    EXPECT_EQ(plus3->sectorCount(), 9u);
    EXPECT_EQ(plus3->getRawSector(0)->dataSize, 512);

    DiskImage::Track* fm = reloaded->getTrackForCylinderAndSide(1, 1);
    EXPECT_EQ(fm->encoding(), DiskImage::Encoding::FM);
    EXPECT_EQ(fm->rawSize(), 3125u);
    EXPECT_EQ(fm->sectorCount(), 16u);
    EXPECT_EQ(fm->getRawSector(0)->dataSize, 128);

    EXPECT_EQ(reloaded->getTrackForCylinderAndSide(2, 0)->rawSize(), 6464u);

    DiskImage::Track* dup = reloaded->getTrackForCylinderAndSide(2, 1);
    EXPECT_EQ(dup->sectorCount(), 16u);
    EXPECT_EQ(dup->findSector(5, dup->findSector(5)->idamOffset + 1)->idamOffset, dup->getRawSector(5)->idamOffset);
    EXPECT_FALSE(dup->getRawSector(7)->hasData);

    DiskImage::Track* dmg = reloaded->getTrackForCylinderAndSide(3, 0);
    EXPECT_FALSE(dmg->getSector(2)->dataCrcValid);
    EXPECT_TRUE(dmg->getSector(4)->deleted);
    EXPECT_FALSE(dmg->getSector(6)->idCrcValid);

    delete reloaded;
}

TEST_F(LoaderUDI_Test, Save_AfterWriteTrack_ReadBackThroughController)
{
    // Format a 9 x 512 track "in the emulator" (formatTrack stands in for the CPU-side WRITE TRACK, which is
    // covered by WD1793_UniversalTrack_Test.WriteTrack_9x512_Then_ReadSector), save as UDI, reload and read a sector
    std::string udiPath = scratch("write-track.udi");
    removeFile(udiPath);

    LoaderTRD trd(_context, fixture("loaders/trd/EyeAche.trd"));
    ASSERT_TRUE(trd.loadImage());
    DiskImage* image = trd.getImage();
    DiskImage::Track* track = image->getTrackForCylinderAndSide(10, 0);
    track->formatTrack(10, 0, Spec::plus3());
    std::vector<uint8_t> pattern(512);
    for (size_t i = 0; i < 512; i++) pattern[i] = static_cast<uint8_t>(i ^ 0x3C);
    track->writeSectorData(4, pattern.data(), pattern.size());

    // TRD refuses this image now...
    EXPECT_FALSE(trd.writeImage(scratch("refused.trd")));

    // ...UDI keeps it
    LoaderUDI udi(_context, udiPath);
    udi.setImage(image);
    ASSERT_TRUE(udi.writeImage());
    delete image;

    LoaderUDI back(_context, udiPath);
    ASSERT_TRUE(back.loadImage());
    DiskImage* reloaded = back.getImage();

    WD1793CUT fdc(_context);
    fdc.getDrive()->insertDisk(reloaded);
    fdc._beta128Register = WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET;
    fdc._drive = 0;
    fdc.wakeUp();
    fdc._time = 1000;
    fdc.prolongFDDMotorRotation();
    fdc._trackRegister = 10;
    fdc._selectedDrive->setTrack(10);
    fdc._sideUp = false;
    fdc._sectorRegister = 5;
    fdc._commandRegister = 0x80;
    fdc._lastDecodedCmd = WD1793::WD_CMD_READ_SECTOR;
    fdc.cmdReadSector(0x00);

    std::vector<uint8_t> read;
    for (size_t clk = 1000; clk < 1000 + 3'500'000 * 3 && fdc._state != WD1793::S_IDLE; clk += 100)
    {
        fdc._time = clk;
        fdc.process();
        if (fdc._beta128status & WD1793::DRQ) read.push_back(fdc.readDataRegister());
    }

    EXPECT_EQ(read, pattern);
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_CRCERR);

    fdc.getDrive()->ejectDisk();
    removeFile(udiPath);
    removeFile(scratch("refused.trd"));
    delete reloaded;
}

TEST_F(LoaderUDI_Test, Fm_Track_Type1)
{
    DiskImage image(2, 1);
    image.getTrack(1)->formatTrack(1, 0, Spec::ibm3740());
    std::vector<uint8_t> data = serializeImage(image);

    // Track type byte of track 1: header 16 + track 0 (3 + 6250 + 782)
    size_t track1 = 16 + 3 + 6250 + 782;
    EXPECT_EQ(data[track1], LoaderUDI::TRACK_TYPE_FM);
    EXPECT_EQ(data[track1 + 1] | (data[track1 + 2] << 8), 3125);
    EXPECT_EQ(data[16], LoaderUDI::TRACK_TYPE_MFM);

    LoaderUDI loader(_context, "");
    std::vector<std::string> warnings;
    DiskImage* reloaded = loader.parse(data.data(), data.size(), warnings);
    ASSERT_NE(reloaded, nullptr);
    EXPECT_EQ(reloaded->getTrack(1)->encoding(), DiskImage::Encoding::FM);
    EXPECT_EQ(reloaded->getTrack(1)->rawSize(), 3125u);
    EXPECT_EQ(reloaded->getTrack(1)->sectorCount(), 16u);
    EXPECT_EQ(reloaded->getTrack(0)->encoding(), DiskImage::Encoding::MFM);
    delete reloaded;
}

TEST_F(LoaderUDI_Test, Header_ExtendedHeader_And_Trailer_Preserved)
{
    DiskImage image(1, 2);
    LoaderUDI writer(_context, "");
    writer.setExtendedHeader({0xDE, 0xAD, 0xBE, 0xEF, 0x01});
    std::vector<uint8_t> trailer = {'\r', '\n', 'h', 'i', 0};
    writer.setTrailer(trailer);

    std::vector<uint8_t> data;
    std::vector<std::string> warnings;
    ASSERT_TRUE(writer.serialize(&image, data, warnings));

    // Header fields
    EXPECT_EQ(std::memcmp(data.data(), "UDI!", 4), 0);
    uint32_t size; std::memcpy(&size, data.data() + 4, 4);
    EXPECT_EQ(size, data.size() - 4);
    EXPECT_EQ(data[8], 0);
    EXPECT_EQ(data[11], 0) << "reserved byte defaults to 0";
    EXPECT_EQ(data[9], 0) << "max cylinder";
    EXPECT_EQ(data[10], 1) << "max head";
    uint32_t ext; std::memcpy(&ext, data.data() + 12, 4);
    EXPECT_EQ(ext, 5u);
    EXPECT_EQ(data[16], 0xDE);
    EXPECT_EQ(data.size(), 16u + 5 + 2 * (3 + 6250 + 782) + trailer.size() + 4);

    LoaderUDICUT reader(_context, "");
    DiskImage* reloaded = reader.parse(data.data(), data.size(), warnings);
    ASSERT_NE(reloaded, nullptr);
    EXPECT_EQ(reader._extendedHeader, std::vector<uint8_t>({0xDE, 0xAD, 0xBE, 0xEF, 0x01}));
    EXPECT_EQ(reader._trailer, trailer);

    std::vector<uint8_t> again;
    ASSERT_TRUE(reader.serialize(reloaded, again, warnings));
    EXPECT_EQ(again, data);
    delete reloaded;
}

TEST_F(LoaderUDI_Test, Load_Truncated_And_MultiRevolution_Rejected)
{
    DiskImage image(2, 1);
    std::vector<uint8_t> data = serializeImage(image);

    LoaderUDI loader(_context, "");
    std::vector<std::string> warnings;

    // Cut inside the second track, fix size + CRC so only the track bounds check can catch it
    std::vector<uint8_t> cut(data.begin(), data.begin() + 16 + 3 + 6250 + 782 + 100);
    uint32_t size = static_cast<uint32_t>(cut.size());
    std::memcpy(cut.data() + 4, &size, 4);
    uint32_t crc = LoaderUDI::computeCrc(cut.data(), cut.size());
    cut.insert(cut.end(), reinterpret_cast<uint8_t*>(&crc), reinterpret_cast<uint8_t*>(&crc) + 4);
    EXPECT_EQ(loader.parse(cut.data(), cut.size(), warnings), nullptr);
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings.back().find("truncated"), std::string::npos);

    // Multi-revolution flag on track 0
    std::vector<uint8_t> multi = data;
    multi[16] |= LoaderUDI::TRACK_TYPE_MULTIREV_FLAG;
    crc = LoaderUDI::computeCrc(multi.data(), multi.size() - 4);
    std::memcpy(multi.data() + multi.size() - 4, &crc, 4);
    warnings.clear();
    EXPECT_EQ(loader.parse(multi.data(), multi.size(), warnings), nullptr);
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings.back().find("multi-revolution"), std::string::npos);

    // Mixed FM/MFM track (type 2) loads as MFM with a warning
    std::vector<uint8_t> mixed = data;
    mixed[16] = LoaderUDI::TRACK_TYPE_MIXED;
    mixed.insert(mixed.begin() + 16 + 3 + 6250 + 782, 782, 0);  // second bitmap
    size = static_cast<uint32_t>(mixed.size() - 4);
    std::memcpy(mixed.data() + 4, &size, 4);
    crc = LoaderUDI::computeCrc(mixed.data(), mixed.size() - 4);
    std::memcpy(mixed.data() + mixed.size() - 4, &crc, 4);
    warnings.clear();
    DiskImage* reloaded = loader.parse(mixed.data(), mixed.size(), warnings);
    ASSERT_NE(reloaded, nullptr);
    EXPECT_FALSE(warnings.empty());
    EXPECT_EQ(reloaded->getTrack(0)->encoding(), DiskImage::Encoding::MFM);
    EXPECT_EQ(reloaded->getTrack(1)->sectorCount(), 16u);
    delete reloaded;
}

/// endregion </Save>
