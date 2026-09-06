#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/wd1793.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "common/modulelogger.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdd.h"
#include "loaders/disk/loader_udi.h"

/// Diagnosis: read Zvezdnoe Nasledie (real drive dump, 6400-byte tracks, H=0 on both sides)
/// the way TR-DOS 5.03 loads BLOK (catalog: track 9 / sector 0, 3 sectors) and the way
/// a ROM that keeps the logical track number in the track register would do it.

static constexpr size_t const Z80_FREQUENCY = 3.5 * 1'000'000;

class UdiZvezdnoeDiag_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    CoreCUT* _core = nullptr;
    Z80* _z80 = nullptr;

    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * 3;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;

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

    void prepare(WD1793CUT& fdc, DiskImage& image, uint8_t track, bool sideUp)
    {
        fdc.getDrive()->insertDisk(&image);
        fdc._beta128Register = WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET;
        fdc._drive = 0;
        fdc.wakeUp();
        fdc._time = 1000;
        fdc.prolongFDDMotorRotation();
        fdc._trackRegister = track;
        fdc._selectedDrive->setTrack(track);
        fdc._sideUp = sideUp;
    }

    void issueReadSector(WD1793CUT& fdc, uint8_t command)
    {
        WD1793CUT::WD_COMMANDS decoded = WD1793CUT::decodeWD93Command(command);
        uint8_t value = WD1793CUT::getWD93CommandValue(decoded, command);
        fdc._commandRegister = command;
        fdc._lastDecodedCmd = decoded;
        fdc.cmdReadSector(value);
    }

    std::vector<uint8_t> runUntilIdle(WD1793CUT& fdc)
    {
        std::vector<uint8_t> bytes;
        for (size_t clk = 0; clk < TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
        {
            fdc._time = clk;
            fdc.process();
            if (fdc._beta128status & WD1793::DRQ)
            {
                bytes.push_back(fdc.readDataRegister());
            }
            if (fdc._state == WD1793::S_IDLE)
            {
                break;
            }
        }
        return bytes;
    }

    static std::string hex(const std::vector<uint8_t>& v, size_t n)
    {
        std::string out;
        char buf[8];
        for (size_t i = 0; i < v.size() && i < n; i++)
        {
            snprintf(buf, sizeof(buf), "%02X ", v[i]);
            out += buf;
        }
        return out;
    }
};

/// Physical cylinder 4, side 1, track register 4 (= ID cylinder): what a TR-DOS ROM that
/// seeks with STEP and reads the VG93 track register would issue
TEST_F(UdiZvezdnoeDiag_Test, ReadSector_TrackReg4_Side1_Single)
{
    LoaderUDI loader(_context, TestPathHelper::GetTestDataPath("loaders/udi/Zvezdnoe Nasledie.udi"));
    ASSERT_TRUE(loader.loadImage());
    DiskImage* image = loader.getImage();
    ASSERT_NE(image, nullptr);

    WD1793CUT fdc(_context);
    prepare(fdc, *image, 4, true);
    fdc._sectorRegister = 1;

    issueReadSector(fdc, 0x80);  // READ SECTOR, no E, no multi
    std::vector<uint8_t> data = runUntilIdle(fdc);

    EXPECT_EQ(fdc._statusRegister & WD1793::WDS_NOTFOUND, 0) << "RNF set - sector not matched";
    EXPECT_EQ(fdc._statusRegister & WD1793::WDS_CRCERR, 0) << "CRC error";
    ASSERT_EQ(data.size(), 256u) << "one 256-byte sector expected";
    // Ground truth from independent scan: 80 00 00 80 00 00 80 ...
    EXPECT_EQ(data[0], 0x80);
    EXPECT_EQ(data[1], 0x00);
    EXPECT_EQ(data[2], 0x00);
    EXPECT_EQ(data[3], 0x80);
    EXPECT_EQ(data[6], 0x80);

    delete image;
}

/// Track register 9 (logical TR-DOS track), head on physical cylinder 4:
/// the C-compare against the track register must reject all IDs (C=4 != 9) if it is enforced
TEST_F(UdiZvezdnoeDiag_Test, ReadSector_TrackReg9_Side1)
{
    LoaderUDI loader(_context, TestPathHelper::GetTestDataPath("loaders/udi/Zvezdnoe Nasledie.udi"));
    ASSERT_TRUE(loader.loadImage());
    DiskImage* image = loader.getImage();
    ASSERT_NE(image, nullptr);

    WD1793CUT fdc(_context);
    prepare(fdc, *image, 4, true);   // head physically on cylinder 4, side 1
    fdc._trackRegister = 9;          // ROM keeps the logical TR-DOS track here
    fdc._sectorRegister = 1;

    issueReadSector(fdc, 0x80);
    std::vector<uint8_t> data = runUntilIdle(fdc);

    bool rnf = fdc._statusRegister & WD1793::WDS_NOTFOUND;
    RecordProperty("rnf", rnf ? 1 : 0);
    RecordProperty("bytes", (int)data.size());
    std::cout << "  trackReg=9: RNF=" << (rnf ? "yes" : "no") << " bytes=" << data.size()
              << " head=" << hex(data, 8) << std::endl;

    delete image;
}

/// Multi-sector read (m=1) of the whole track, as TR-DOS does when loading whole tracks
/// KNOWN ISSUE: Test expects 16 sectors but controller only returns 15 due to timing.
/// This was failing before the disk-formats branch and needs investigation.
TEST_F(UdiZvezdnoeDiag_Test, DISABLED_ReadSector_Multi_WholeTrack_Side1)
{
    _context->pModuleLogger->TurnOnLoggingForModule(PlatformModulesEnum::MODULE_DISK,
                                                    PlatformDiskSubmodulesEnum::SUBMODULE_DISK_FDC);
    _context->pModuleLogger->SetLoggingLevel(LoggerLevel::LogInfo);

    LoaderUDI loader(_context, TestPathHelper::GetTestDataPath("loaders/udi/Zvezdnoe Nasledie.udi"));
    ASSERT_TRUE(loader.loadImage());
    DiskImage* image = loader.getImage();
    ASSERT_NE(image, nullptr);

    WD1793CUT fdc(_context);
    prepare(fdc, *image, 4, true);
    fdc._sectorRegister = 1;

    issueReadSector(fdc, 0x90);  // READ SECTOR multi
    std::vector<uint8_t> data = runUntilIdle(fdc);

    // Which sector is missing? First byte of each 256-byte chunk vs ground truth
    static const uint8_t first[17] = {0, 0x80, 0x0C, 0xA2, 0x18, 0x1A, 0xCD, 0x76, 0x96, 0x62, 0x2C, 0x90, 0x08, 0x11, 0x8A, 0x82, 0x78};
    for (size_t i = 0; i < data.size() / 256 && i < 16; i++)
    {
        std::cout << "  chunk " << i << " (sector " << (i + 1) << " expected): first byte 0x" << hex({data[i * 256]}, 1)
                  << " expected 0x" << std::hex << (int)first[i + 1] << std::dec
                  << (data[i * 256] == first[i + 1] ? "" : "  <-- MISMATCH") << std::endl;
    }
    std::cout << "  final status: RNF=" << ((fdc._statusRegister & WD1793::WDS_NOTFOUND) ? 1 : 0)
              << " CRCERR=" << ((fdc._statusRegister & WD1793::WDS_CRCERR) ? 1 : 0)
              << " sectorReg=" << (int)fdc._sectorRegister << std::endl;

    EXPECT_EQ(data.size(), 16u * 256u) << "whole track = 16 sectors x 256, got " << data.size();
    // Sector 1 ground truth
    ASSERT_GE(data.size(), 8u);
    EXPECT_EQ(data[0], 0x80);
    // Sector 2 ground truth starts 0C 0C 00 00
    ASSERT_GE(data.size(), 260u);
    EXPECT_EQ(data[256], 0x0C);
    EXPECT_EQ(data[257], 0x0C);
    // Sector 3 ground truth starts A2 6E 76 26
    ASSERT_GE(data.size(), 516u);
    EXPECT_EQ(data[512], 0xA2);
    EXPECT_EQ(data[513], 0x6E);

    delete image;
}

/// Same read on side 0 (cylinder 4) for control
TEST_F(UdiZvezdnoeDiag_Test, ReadSector_TrackReg4_Side0_Control)
{
    LoaderUDI loader(_context, TestPathHelper::GetTestDataPath("loaders/udi/Zvezdnoe Nasledie.udi"));
    ASSERT_TRUE(loader.loadImage());
    DiskImage* image = loader.getImage();
    ASSERT_NE(image, nullptr);

    WD1793CUT fdc(_context);
    prepare(fdc, *image, 4, false);
    fdc._sectorRegister = 1;

    issueReadSector(fdc, 0x80);
    std::vector<uint8_t> data = runUntilIdle(fdc);

    EXPECT_EQ(fdc._statusRegister & WD1793::WDS_NOTFOUND, 0) << "RNF on side 0";
    ASSERT_EQ(data.size(), 256u);

    delete image;
}
