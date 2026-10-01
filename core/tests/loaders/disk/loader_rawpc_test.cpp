#include "loaders/disk/loader_rawpc.h"

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/wd1793.h"

/// Raw PC floppy images, 720 KB (DD) and 1.44 MB (HD): detection by size, the PC MFM track layout whose length
/// tells the WD1793 the density, reading through the controller at the matching data rate, and the strict save.
/// docs/inprogress/2026-09-28-sprinter/tdd-storage.md §2.4

namespace
{
    constexpr size_t kSector = LoaderRawPcFloppy::SECTOR_SIZE;

    /// Byte i of sector (cylinder, side, number): a tag in the first two bytes, then a pattern
    uint8_t PatternByte(uint8_t cylinder, uint8_t side, uint8_t number, size_t i)
    {
        if (i == 0) return cylinder;
        if (i == 1) return static_cast<uint8_t>((side << 5) | number);
        return static_cast<uint8_t>((cylinder * 7 + side * 13 + number * 3 + i) & 0xFF);
    }

    /// A dump in PC order (c0/h0, c0/h1, c1/h0, ...) with every sector tagged by its own address
    std::vector<uint8_t> MakeDump(uint8_t sectorsPerTrack)
    {
        std::vector<uint8_t> dump(static_cast<size_t>(80) * 2 * sectorsPerTrack * kSector);
        size_t pos = 0;
        for (uint8_t c = 0; c < 80; c++)
            for (uint8_t h = 0; h < 2; h++)
                for (uint8_t n = 1; n <= sectorsPerTrack; n++)
                    for (size_t i = 0; i < kSector; i++)
                        dump[pos++] = PatternByte(c, h, n, i);
        return dump;
    }

    std::vector<uint8_t> ExpectedTrack(uint8_t cylinder, uint8_t side, uint8_t sectorsPerTrack)
    {
        std::vector<uint8_t> track;
        for (uint8_t n = 1; n <= sectorsPerTrack; n++)
            for (size_t i = 0; i < kSector; i++)
                track.push_back(PatternByte(cylinder, side, n, i));
        return track;
    }

    std::string WriteScratch(const std::string& leaf, const std::vector<uint8_t>& bytes)
    {
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(leaf);
        FILE* file = FileHelper::OpenFile(path, "wb");
        if (file)
        {
            FileHelper::SaveBufferToFile(file, const_cast<uint8_t*>(bytes.data()), bytes.size());
            FileHelper::CloseFile(file);
        }
        return path;
    }

    std::vector<uint8_t> ReadFile(const std::string& path)
    {
        std::vector<uint8_t> bytes(FileHelper::GetFileSize(path));
        if (!bytes.empty())
            FileHelper::ReadFileToBuffer(path, bytes.data(), bytes.size());
        return bytes;
    }
}  // namespace

class LoaderRawPcFloppy_Test : public ::testing::Test
{
protected:
    static constexpr size_t kRotation = WD1793::DISK_ROTATION_PERIOD_TSTATES;  // 700 000 T = 200 ms

    EmulatorContext* _context = nullptr;
    CoreCUT* _core = nullptr;
    Z80* _z80 = nullptr;

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
        _core->_z80 = nullptr;
        delete _z80;
        _context->pCore = nullptr;
        delete _core;
        delete _context;
    }

    DiskImage* Parse(const std::vector<uint8_t>& dump)
    {
        LoaderRawPcFloppy loader(_context, "");
        std::vector<std::string> warnings;
        DiskImage* image = loader.parse(dump.data(), dump.size(), warnings);
        EXPECT_NE(image, nullptr) << (warnings.empty() ? "" : warnings.front());
        return image;
    }

    /// Load a dump through the file path and check the geometry, the density every track reports to the
    /// WD1793 and every sector's ID and content
    void CheckLoad(uint8_t spt, const char* leaf)
    {
        const std::string path = WriteScratch(leaf, MakeDump(spt));
        LoaderRawPcFloppy loader(_context, path);
        ASSERT_TRUE(loader.loadImage()) << (loader.lastWarnings().empty() ? "" : loader.lastWarnings().front());
        std::unique_ptr<DiskImage> image(loader.getImage());
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(image->getCylinders(), 80);
        EXPECT_EQ(image->getSides(), 2);
        EXPECT_FALSE(image->isDirty());

        const FdcDataRate rate = spt == 9 ? FdcDataRate::Rate250Kbps : FdcDataRate::Rate500Kbps;
        for (uint8_t c = 0; c < 80; c++)
        {
            for (uint8_t h = 0; h < 2; h++)
            {
                DiskImage::Track* track = image->getTrackForCylinderAndSide(c, h);
                ASSERT_NE(track, nullptr);
                ASSERT_EQ(track->sectorCount(), spt);
                ASSERT_EQ(track->RecordedDataRate(), rate) << "cyl " << int(c) << " side " << int(h);
                for (uint8_t n = 1; n <= spt; n++)
                {
                    DiskImage::Sector* sector = track->findSector(n);
                    ASSERT_NE(sector, nullptr);
                    ASSERT_EQ(sector->dataSize, 512);
                    ASSERT_EQ(sector->cylinder(), c);
                    ASSERT_EQ(sector->head(), h);
                    ASSERT_EQ(sector->id->sector_size, 2) << "N = 2";
                    ASSERT_TRUE(sector->dataCrcValid);
                    ASSERT_EQ(sector->data[0], c);
                    ASSERT_EQ(sector->data[1], (h << 5) | n) << "cyl " << int(c) << " side " << int(h) << " sector " << int(n);
                }
            }
        }
    }

    /// Load, let the guest change one sector, save to another file: the file is the dump with that change
    void CheckSaveRoundTrip(uint8_t spt, const char* leaf)
    {
        std::vector<uint8_t> dump = MakeDump(spt);
        LoaderRawPcFloppy loader(_context, WriteScratch(leaf, dump));
        ASSERT_TRUE(loader.loadImage());
        std::unique_ptr<DiskImage> image(loader.getImage());

        DiskImage::Sector* sector = image->getTrackForCylinderAndSide(79, 1)->findSector(spt);  // the last one
        ASSERT_NE(sector, nullptr);
        std::memset(sector->data, 0xA5, kSector);
        sector->recalculateDataCRC();
        std::memset(dump.data() + dump.size() - kSector, 0xA5, kSector);

        const std::string target = TestPathHelper::GetUniqueTestScratchPath(std::string("out-") + leaf);
        ASSERT_TRUE(loader.writeImage(target)) << (loader.lastWarnings().empty() ? "" : loader.lastWarnings().front());
        EXPECT_EQ(ReadFile(target), dump);
        EXPECT_FALSE(image->isDirty()) << "a successful save marks the disk clean";
    }

    /// Drive A, motor on, head and track register on the cylinder, MFM, the given side
    static void Prepare(WD1793CUT& fdc, DiskImage* image, uint8_t cylinder, uint8_t side)
    {
        fdc.getDrive()->insertDisk(image);
        fdc._beta128Register = WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET;  // MFM (DENSITY bit clear)
        fdc._drive = 0;
        fdc.wakeUp();
        fdc._time = 1000;
        fdc._lastTime = 1000;
        fdc.prolongFDDMotorRotation();
        fdc.getDrive()->setMotor(true);
        fdc._trackRegister = cylinder;
        fdc._selectedDrive->setTrack(cylinder);
        fdc._sideUp = side != 0;
    }

    static void SetHighDensity(WD1793CUT& fdc)
    {
        fdc.SetClockPolicy(FdcClockPolicy::Latched);
        ASSERT_TRUE(fdc.SetLatchedClock(FdcClock::Clock2MHz, FdcDataRate::Rate500Kbps));
    }

    /// READ SECTOR (multiple, command #90) from sector 1: collects `bytes` data bytes, i.e. the whole track in
    /// about one revolution. Stops as soon as they are in (the command would go on to look for sector N + 1)
    static std::vector<uint8_t> ReadWholeTrack(WD1793CUT& fdc, size_t bytes)
    {
        fdc._sectorRegister = 1;
        fdc._commandRegister = 0x90;
        fdc._lastDecodedCmd = WD1793::WD_CMD_READ_SECTOR;
        fdc._statusRegister |= WD1793::WDS_BUSY;
        fdc.cmdReadSector(WD1793CUT::getWD93CommandValue(WD1793::WD_CMD_READ_SECTOR, 0x90));

        std::vector<uint8_t> read;
        const size_t start = fdc._time;
        for (size_t clk = start + 8; clk < start + kRotation * 3 && read.size() < bytes; clk += 8)
        {
            fdc._time = clk;
            fdc.process();
            if (fdc._beta128status & WD1793::DRQ)
                read.push_back(fdc.readDataRegister());
            if (fdc._state == WD1793::S_IDLE)
                break;
        }
        EXPECT_FALSE(fdc._statusRegister & (WD1793::WDS_NOTFOUND | WD1793::WDS_CRCERR | WD1793::WDS_LOSTDATA));
        return read;
    }

    /// READ SECTOR (single) of one sector; returns its data (empty on Record Not Found)
    static std::vector<uint8_t> ReadOneSector(WD1793CUT& fdc, uint8_t number, uint8_t* status = nullptr)
    {
        fdc._sectorRegister = number;
        fdc._commandRegister = 0x80;
        fdc._lastDecodedCmd = WD1793::WD_CMD_READ_SECTOR;
        fdc._statusRegister |= WD1793::WDS_BUSY;
        fdc.cmdReadSector(0x00);

        std::vector<uint8_t> read;
        const size_t start = fdc._time;
        for (size_t clk = start + 8; clk < start + kRotation * 7; clk += (read.empty() ? 256 : 8))
        {
            fdc._time = clk;
            fdc.process();
            if (fdc._beta128status & WD1793::DRQ)
                read.push_back(fdc.readDataRegister());
            if (fdc._state == WD1793::S_IDLE)
                break;
        }
        EXPECT_EQ(fdc._state, WD1793::S_IDLE) << "READ SECTOR did not finish";
        if (status) *status = fdc._statusRegister;
        return read;
    }
};

/// region <Detection and layout>

TEST_F(LoaderRawPcFloppy_Test, DetectBySize)
{
    EXPECT_TRUE(LoaderRawPcFloppy::detect(737280));
    EXPECT_TRUE(LoaderRawPcFloppy::detect(1474560));
    EXPECT_EQ(LoaderRawPcFloppy::sectorsForSize(737280), 9);
    EXPECT_EQ(LoaderRawPcFloppy::sectorsForSize(1474560), 18);

    EXPECT_FALSE(LoaderRawPcFloppy::detect(819200)) << "MGT (80 x 2 x 10 x 512)";
    EXPECT_FALSE(LoaderRawPcFloppy::detect(655360)) << "TRD";
    EXPECT_FALSE(LoaderRawPcFloppy::detect(368640)) << "360 KB is not handled";
    EXPECT_FALSE(LoaderRawPcFloppy::detect(0));
}

/// GAP4a 80 + SYNC 12 + IAM 4 + GAP1 50 = 146, then per sector SYNC 12 + IDAM 4 + ID 6 + GAP2 22 + SYNC 12 +
/// DAM 4 + 512 + CRC 2 + GAP3: DD 146 + 9 x 658 = 6 068 of 6 250, HD 146 + 18 x 682 = 12 422 of 12 500
TEST_F(LoaderRawPcFloppy_Test, PcTrackLayoutFitsTheRawTrack)
{
    const DiskImage::TrackFormatSpec dd = LoaderRawPcFloppy::trackSpec(9);
    EXPECT_EQ(dd.trackLength, 6250u);
    EXPECT_EQ(dd.totalBytes(), 6068u);
    EXPECT_TRUE(dd.fits());

    const DiskImage::TrackFormatSpec hd = LoaderRawPcFloppy::trackSpec(18);
    EXPECT_EQ(hd.trackLength, 12500u);
    EXPECT_EQ(hd.totalBytes(), 12422u);
    EXPECT_TRUE(hd.fits());
    EXPECT_TRUE(hd.indexMark) << "the WD1793 does not need the IAM but must read past it";
}

TEST_F(LoaderRawPcFloppy_Test, Load720K)
{
    CheckLoad(9, "rawpc-dd.img");
}

TEST_F(LoaderRawPcFloppy_Test, Load144M)
{
    CheckLoad(18, "rawpc-hd.ima");
}

TEST_F(LoaderRawPcFloppy_Test, LoadRefusesOtherSizes)
{
    const std::string path = WriteScratch("rawpc-mgt-sized.img", std::vector<uint8_t>(819200, 0xE5));
    LoaderRawPcFloppy loader(_context, path);
    EXPECT_FALSE(loader.loadImage());
    ASSERT_FALSE(loader.lastWarnings().empty());
    EXPECT_NE(loader.lastWarnings().front().find("819200"), std::string::npos) << loader.lastWarnings().front();
}

/// endregion </Detection and layout>

/// region <Save>

TEST_F(LoaderRawPcFloppy_Test, SaveRoundTrip720K)
{
    CheckSaveRoundTrip(9, "rawpc-rt-dd.img");
}

TEST_F(LoaderRawPcFloppy_Test, SaveRoundTrip144M)
{
    CheckSaveRoundTrip(18, "rawpc-rt-hd.img");
}

/// A track the guest re-formatted to another layout cannot go back into a raw dump: the save is refused with
/// the reason, and the file is not touched
TEST_F(LoaderRawPcFloppy_Test, SaveRefusesAnIrregularLayout)
{
    const std::vector<uint8_t> dump = MakeDump(9);
    const std::string path = WriteScratch("rawpc-irregular.img", dump);
    LoaderRawPcFloppy loader(_context, path);
    ASSERT_TRUE(loader.loadImage());
    DiskImage* image = loader.getImage();

    image->getTrackForCylinderAndSide(40, 0)->formatTrack(40, 0, DiskImage::TrackFormatSpec::plusD());  // 10 x 512

    EXPECT_FALSE(loader.writeImage());
    ASSERT_FALSE(loader.lastWarnings().empty());
    const std::string& reason = loader.lastWarnings().front();
    EXPECT_NE(reason.find("cylinder 40 side 0 has 10 sectors"), std::string::npos) << reason;
    EXPECT_NE(reason.find("UDI"), std::string::npos) << reason;
    EXPECT_EQ(ReadFile(path), dump) << "the original file is left alone";

    std::string why;
    EXPECT_FALSE(LoaderRawPcFloppy::isRegularGeometry(image, nullptr, &why));
    EXPECT_FALSE(why.empty());
    delete image;
}

/// endregion </Save>

/// region <Controller>

/// A 720 KB disk reads through the WD1793 at its default 250 kbit/s: every sector of the first and the last track
TEST_F(LoaderRawPcFloppy_Test, Wd1793ReadsTheDDImageAt250Kbps)
{
    DiskImage* image = Parse(MakeDump(9));
    ASSERT_NE(image, nullptr);

    for (const auto& [cylinder, side] : {std::pair<uint8_t, uint8_t>{0, 0}, std::pair<uint8_t, uint8_t>{79, 1}})
    {
        WD1793CUT fdc(_context);
        Prepare(fdc, image, cylinder, side);
        ASSERT_EQ(fdc.GetDataRate(), FdcDataRate::Rate250Kbps);
        const std::vector<uint8_t> read = ReadWholeTrack(fdc, 9 * kSector);
        EXPECT_EQ(read, ExpectedTrack(cylinder, side, 9)) << "cyl " << int(cylinder) << " side " << int(side);
        fdc.getDrive()->ejectDisk();
    }
    delete image;
}

/// A 1.44 MB disk has 500 kbit/s tracks: at the default 250 kbit/s the controller finds no ID (Record Not
/// Found, the BIOS density probe's cue); with the Latched policy set to 2 MHz / 500 kbit/s (Sprinter #BD HD)
/// every sector reads
TEST_F(LoaderRawPcFloppy_Test, Wd1793ReadsTheHDImageOnlyAt500Kbps)
{
    DiskImage* image = Parse(MakeDump(18));
    ASSERT_NE(image, nullptr);

    {
        WD1793CUT fdc(_context);
        Prepare(fdc, image, 0, 0);
        uint8_t status = 0;
        const std::vector<uint8_t> read = ReadOneSector(fdc, 1, &status);
        EXPECT_TRUE(read.empty());
        EXPECT_TRUE(status & WD1793::WDS_NOTFOUND) << "an HD track at the DD rate shows no address mark";
        fdc.getDrive()->ejectDisk();
    }

    for (const auto& [cylinder, side] : {std::pair<uint8_t, uint8_t>{0, 0}, std::pair<uint8_t, uint8_t>{79, 1}})
    {
        WD1793CUT fdc(_context);
        Prepare(fdc, image, cylinder, side);
        SetHighDensity(fdc);
        const std::vector<uint8_t> read = ReadWholeTrack(fdc, 18 * kSector);
        EXPECT_EQ(read, ExpectedTrack(cylinder, side, 18)) << "cyl " << int(cylinder) << " side " << int(side);
        fdc.getDrive()->ejectDisk();
    }
    delete image;
}

/// A FAT12 boot sector (the DSS 1.62 boot floppy layout: OEM "DSS 1.60", 10 reserved sectors, one FAT, 2 880
/// sectors, 18 per track, 2 heads) reads back through the controller at HD with its BPB intact. The machine
/// that boots it (Sprinter) is not emulated yet; this is the part the floppy path owns.
/// Kept next to the real-image tests below (region <Real DSS boot floppy>) because it needs no fixture file and
/// so still runs where testdata/machines/sprinter/ is absent
TEST_F(LoaderRawPcFloppy_Test, Wd1793ReadsAFat12BootSector)
{
    std::vector<uint8_t> dump(LoaderRawPcFloppy::IMAGE_SIZE_HD, 0xF6);
    uint8_t* boot = dump.data();
    const uint8_t header[] = {0xEB, 0x3C, 0x90, 'D', 'S', 'S', ' ', '1', '.', '6', '0',
                              0x00, 0x02,        // bytes per sector 512
                              0x01,              // sectors per cluster
                              0x0A, 0x00,        // reserved sectors 10
                              0x01,              // FATs 1
                              0xE0, 0x00,        // root entries 224
                              0x40, 0x0B,        // total sectors 2 880
                              0xF0,              // media descriptor
                              0x09, 0x00,        // sectors per FAT 9
                              0x12, 0x00,        // sectors per track 18
                              0x02, 0x00};       // heads 2
    std::memcpy(boot, header, sizeof(header));
    boot[510] = 0x55;
    boot[511] = 0xAA;

    DiskImage* image = Parse(dump);
    ASSERT_NE(image, nullptr);
    WD1793CUT fdc(_context);
    Prepare(fdc, image, 0, 0);
    SetHighDensity(fdc);
    uint8_t status = 0;
    const std::vector<uint8_t> read = ReadOneSector(fdc, 1, &status);
    ASSERT_EQ(read.size(), kSector);
    EXPECT_FALSE(status & (WD1793::WDS_NOTFOUND | WD1793::WDS_CRCERR));
    EXPECT_EQ(std::string(read.begin() + 3, read.begin() + 11), "DSS 1.60");
    EXPECT_EQ(read[11] | (read[12] << 8), 512);
    EXPECT_EQ(read[19] | (read[20] << 8), 2880);
    EXPECT_EQ(read[24] | (read[25] << 8), 18);
    EXPECT_EQ(read[26] | (read[27] << 8), 2);
    EXPECT_EQ(read[510], 0x55);
    EXPECT_EQ(read[511], 0xAA);
    fdc.getDrive()->ejectDisk();
    delete image;
}

/// endregion </Controller>

/// region <Real DSS boot floppy>

/// The real Sprinter DSS 1.62.92 boot floppy (testdata/machines/sprinter/README.md), read the way the Sprinter
/// reads it: through the WD1793 with the Latched clock policy at 2 MHz / 500 kbit/s (port #BD, HD). Every byte
/// checked here comes out of the controller, not out of the file
class LoaderRawPcFloppyDss_Test : public LoaderRawPcFloppy_Test
{
protected:
    static constexpr uint8_t kSectorsPerTrack = 18;
    static constexpr uint8_t kHeads = 2;

    std::unique_ptr<DiskImage> _image;

    void TearDown() override
    {
        _image.reset();
        LoaderRawPcFloppy_Test::TearDown();
    }

    /// Load the real image through the file path. False (and the caller skips) when the fixture is absent
    bool LoadDssFloppy()
    {
        const std::string path = TestPathHelper::GetTestDataPath("machines/sprinter/dss_1_62_92.img");
        if (!FileHelper::FileExists(path))
            return false;
        LoaderRawPcFloppy loader(_context, path);
        EXPECT_TRUE(loader.loadImage()) << (loader.lastWarnings().empty() ? "" : loader.lastWarnings().front());
        _image.reset(loader.getImage());
        return _image != nullptr;
    }

    /// One sector by its PC logical block address: LBA -> (cylinder, side, sector) for 18 sectors per track and
    /// two heads, then READ SECTOR at HD. Returns the 512 data bytes (empty on Record Not Found)
    std::vector<uint8_t> ReadLba(uint32_t lba, uint8_t* status = nullptr)
    {
        const uint8_t cylinder = static_cast<uint8_t>(lba / (kSectorsPerTrack * kHeads));
        const uint8_t side = static_cast<uint8_t>((lba / kSectorsPerTrack) % kHeads);
        const uint8_t number = static_cast<uint8_t>(lba % kSectorsPerTrack + 1);

        WD1793CUT fdc(_context);
        Prepare(fdc, _image.get(), cylinder, side);
        SetHighDensity(fdc);
        std::vector<uint8_t> read = ReadOneSector(fdc, number, status);
        fdc.getDrive()->ejectDisk();
        return read;
    }

    static uint16_t Word(const std::vector<uint8_t>& bytes, size_t offset)
    {
        return static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
    }
};

/// The boot sector's BIOS Parameter Block as the DSS 1.62.92 floppy ships it, and the DSS loader that the
/// Sprinter ROM starts from LBA 1 (it begins with the text "Starting...")
TEST_F(LoaderRawPcFloppyDss_Test, Wd1793ReadsTheRealBootSectorAndLoaderAt500Kbps)
{
    if (!LoadDssFloppy())
        GTEST_SKIP() << "testdata/machines/sprinter/dss_1_62_92.img is missing (see testdata/machines/sprinter/README.md)";
    EXPECT_EQ(_image->getCylinders(), 80);
    EXPECT_EQ(_image->getSides(), 2);

    uint8_t status = 0;
    const std::vector<uint8_t> boot = ReadLba(0, &status);
    ASSERT_EQ(boot.size(), kSector);
    EXPECT_FALSE(status & (WD1793::WDS_NOTFOUND | WD1793::WDS_CRCERR | WD1793::WDS_LOSTDATA));

    EXPECT_EQ(std::string(boot.begin() + 3, boot.begin() + 11), "DSS 1.60") << "OEM name";
    EXPECT_EQ(Word(boot, 11), 512) << "bytes per sector";
    EXPECT_EQ(boot[13], 1) << "sectors per cluster";
    EXPECT_EQ(Word(boot, 14), 10) << "reserved sectors: the boot sector and the 9-sector DSS loader";
    EXPECT_EQ(boot[16], 1) << "number of FATs";
    EXPECT_EQ(Word(boot, 17), 224) << "root directory entries";
    EXPECT_EQ(Word(boot, 19), 2880) << "total sectors";
    EXPECT_EQ(boot[21], 0xF0) << "media descriptor (1.44 MB)";
    EXPECT_EQ(Word(boot, 22), 9) << "sectors per FAT";
    EXPECT_EQ(Word(boot, 24), kSectorsPerTrack) << "sectors per track";
    EXPECT_EQ(Word(boot, 26), kHeads) << "heads";
    EXPECT_EQ(std::string(boot.begin() + 54, boot.begin() + 62), "FAT12   ") << "file system type";
    EXPECT_EQ(boot[510], 0x55);
    EXPECT_EQ(boot[511], 0xAA);

    const std::vector<uint8_t> loader = ReadLba(1, &status);
    ASSERT_EQ(loader.size(), kSector);
    EXPECT_FALSE(status & (WD1793::WDS_NOTFOUND | WD1793::WDS_CRCERR | WD1793::WDS_LOSTDATA));
    EXPECT_EQ(std::string(loader.begin(), loader.begin() + 11), "Starting...");
}

/// The root directory, located from the BPB read off the disk (reserved + FATs x sectors per FAT = LBA 19, i.e.
/// cylinder 0, side 1, sector 2), holds the DSS kernel SYSTEM.DOS and the shell SYSTEM.EXE
TEST_F(LoaderRawPcFloppyDss_Test, Wd1793ReadsTheRealRootDirectoryAt500Kbps)
{
    if (!LoadDssFloppy())
        GTEST_SKIP() << "testdata/machines/sprinter/dss_1_62_92.img is missing (see testdata/machines/sprinter/README.md)";

    const std::vector<uint8_t> boot = ReadLba(0);
    ASSERT_EQ(boot.size(), kSector);
    const uint32_t rootLba = Word(boot, 14) + boot[16] * Word(boot, 22);
    const uint32_t rootSectors = (Word(boot, 17) * 32u + kSector - 1) / kSector;
    EXPECT_EQ(rootLba, 19u);
    EXPECT_EQ(rootSectors, 14u);

    uint32_t dosSize = 0;
    uint32_t exeSize = 0;
    bool end = false;
    for (uint32_t lba = rootLba; lba < rootLba + rootSectors && !end && !(dosSize && exeSize); lba++)
    {
        uint8_t status = 0;
        const std::vector<uint8_t> dir = ReadLba(lba, &status);
        ASSERT_EQ(dir.size(), kSector) << "root directory LBA " << lba;
        ASSERT_FALSE(status & (WD1793::WDS_NOTFOUND | WD1793::WDS_CRCERR | WD1793::WDS_LOSTDATA)) << "LBA " << lba;
        for (size_t entry = 0; entry < kSector; entry += 32)
        {
            if (dir[entry] == 0x00)  // end of the directory
            {
                end = true;
                break;
            }
            const std::string name(dir.begin() + entry, dir.begin() + entry + 11);
            const uint32_t size = Word(dir, entry + 28) | (static_cast<uint32_t>(Word(dir, entry + 30)) << 16);
            if (name == "SYSTEM  DOS")
                dosSize = size;
            else if (name == "SYSTEM  EXE")
                exeSize = size;
        }
    }
    EXPECT_EQ(dosSize, 16035u) << "SYSTEM.DOS (the DSS 1.62 kernel) in the root directory";
    EXPECT_EQ(exeSize, 7424u) << "SYSTEM.EXE (the DSS 1.62 shell) in the root directory";
}

/// The same disk at the default DD rate (1 MHz / 250 kbit/s): the controller sees no address mark on the HD
/// track, Record Not Found - the cue the Sprinter BIOS density probe switches #BD to HD on
TEST_F(LoaderRawPcFloppyDss_Test, Wd1793FindsNoRecordOnTheRealImageAt250Kbps)
{
    if (!LoadDssFloppy())
        GTEST_SKIP() << "testdata/machines/sprinter/dss_1_62_92.img is missing (see testdata/machines/sprinter/README.md)";

    WD1793CUT fdc(_context);
    Prepare(fdc, _image.get(), 0, 0);
    ASSERT_EQ(fdc.GetDataRate(), FdcDataRate::Rate250Kbps);
    uint8_t status = 0;
    const std::vector<uint8_t> read = ReadOneSector(fdc, 1, &status);
    EXPECT_TRUE(read.empty());
    EXPECT_TRUE(status & WD1793::WDS_NOTFOUND) << "an HD track at the DD rate shows no address mark";
    fdc.getDrive()->ejectDisk();
}

/// endregion </Real DSS boot floppy>
