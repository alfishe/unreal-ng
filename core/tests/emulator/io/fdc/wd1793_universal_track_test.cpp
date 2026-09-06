#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/wd1793.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "_helpers/testtiminghelper.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdd.h"

/// WD1793 on the universal track model
/// (docs/inprogress/2026-09-02-universal-track-model/test-plan.md, section 3):
/// non-TR-DOS geometries, ID-only sectors, CRC regeneration on write, variable track length,
/// clock marks written by WRITE TRACK, datasheet READ ADDRESS behaviour.

static constexpr size_t const Z80_FREQUENCY = 3.5 * 1'000'000;

using Spec = DiskImage::TrackFormatSpec;

class WD1793_UniversalTrack_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    CoreCUT* _core = nullptr;
    Z80* _z80 = nullptr;

    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * 3;  // 3 seconds max per command
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;

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

    /// Bring the controller into the state the other FDC tests use: drive A, motor on, head on track.
    /// Beta-128 port #FF bit 6 selects the density: clear = MFM (double), set = FM (single).
    void prepare(WD1793CUT& fdc, DiskImage& image, uint8_t track, bool sideUp = false, bool fm = false)
    {
        fdc.getDrive()->insertDisk(&image);
        fdc._beta128Register = WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET |
                               (fm ? WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_DENSITY : 0);
        fdc._drive = 0;
        fdc.wakeUp();
        fdc._time = 1000;
        fdc.prolongFDDMotorRotation();
        fdc._trackRegister = track;
        fdc._selectedDrive->setTrack(track);
        fdc._sideUp = sideUp;
    }

    /// Issue a command the way processWD93Command does: set command / decoded-command registers, then dispatch
    void issue(WD1793CUT& fdc, uint8_t command)
    {
        WD1793CUT::WD_COMMANDS decoded = WD1793CUT::decodeWD93Command(command);
        uint8_t value = WD1793CUT::getWD93CommandValue(decoded, command);
        fdc._commandRegister = command;
        fdc._lastDecodedCmd = decoded;

        switch (decoded)
        {
            case WD1793::WD_CMD_READ_SECTOR:  fdc.cmdReadSector(value);  break;
            case WD1793::WD_CMD_WRITE_SECTOR: fdc.cmdWriteSector(value); break;
            case WD1793::WD_CMD_READ_ADDRESS: fdc.cmdReadAddress(value); break;
            case WD1793::WD_CMD_READ_TRACK:   fdc.cmdReadTrack(value);   break;
            case WD1793::WD_CMD_WRITE_TRACK:  fdc.cmdWriteTrack(value);  break;
            default: FAIL() << "unexpected command"; break;
        }
    }

    /// Run the FSM until the command finishes, collecting every byte the controller offers through DRQ
    std::vector<uint8_t> runReadUntilIdle(WD1793CUT& fdc, size_t startClk = 0)
    {
        std::vector<uint8_t> bytes;
        for (size_t clk = startClk; clk < startClk + TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
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

    /// Feed data on every DRQ until the command finishes or the source is exhausted; returns bytes accepted
    size_t runWriteUntilIdle(WD1793CUT& fdc, const std::vector<uint8_t>& source, size_t startClk = 0)
    {
        size_t written = 0;
        for (size_t clk = startClk; clk < startClk + TEST_DURATION_TSTATES && written < source.size(); clk += TEST_INCREMENT_TSTATES)
        {
            fdc._time = clk;
            fdc.process();

            if (fdc._beta128status & WD1793::DRQ)
            {
                fdc.writeDataRegister(source[written++]);
            }

            if (fdc._state == WD1793::S_IDLE)
            {
                break;
            }
        }
        return written;
    }

    void waitIdle(WD1793CUT& fdc)
    {
        size_t guard = 0;
        while (fdc._state != WD1793::S_IDLE && guard++ < 1'000'000)
        {
            fdc._time += TEST_INCREMENT_TSTATES;
            fdc.process();
        }
    }

    /// Byte stream a CPU would feed to WRITE TRACK in FM (single density): no F5/F6, marks carry their own clocks
    static std::vector<uint8_t> formatStreamFM(const Spec& spec, uint8_t cylinder, uint8_t side, uint8_t dataFill)
    {
        std::vector<uint8_t> out;
        auto fill = [&](size_t n, uint8_t v) { out.insert(out.end(), n, v); };

        if (spec.indexMark)
        {
            fill(spec.gapIndex, spec.gapFill);
            fill(spec.syncLength, 0x00);
            out.push_back(0xFC);
            fill(spec.gapPostIndex, spec.gapFill);
        }

        for (size_t i = 0; i < spec.sectorNumbers.size(); i++)
        {
            fill(spec.gapPreID, spec.gapFill);
            fill(spec.syncLength, 0x00);
            out.push_back(0xFE);
            out.push_back(cylinder);
            out.push_back(side);
            out.push_back(spec.sectorNumbers[i]);
            out.push_back(spec.sizeCodeFor(i));
            out.push_back(0xF7);
            fill(spec.gapPostID, spec.gapFill);
            fill(spec.syncLength, 0x00);
            out.push_back(spec.dataMark);
            fill(128u << (spec.sizeCodeFor(i) & 3), dataFill);
            out.push_back(0xF7);
            fill(spec.gapPostData, spec.gapFill);
        }

        while (out.size() < spec.trackLength) out.push_back(spec.gapFill);
        return out;
    }

    /// Byte stream a CPU would feed to WRITE TRACK to format one track with the given spec (MFM control bytes)
    static std::vector<uint8_t> formatStream(const Spec& spec, uint8_t cylinder, uint8_t side, uint8_t dataFill)
    {
        std::vector<uint8_t> out;
        auto fill = [&](size_t n, uint8_t v) { out.insert(out.end(), n, v); };

        if (spec.indexMark)
        {
            fill(spec.gapIndex, spec.gapFill);
            fill(spec.syncLength, 0x00);
            fill(3, 0xF6);
            out.push_back(0xFC);
            fill(spec.gapPostIndex, spec.gapFill);
        }

        for (size_t i = 0; i < spec.sectorNumbers.size(); i++)
        {
            fill(spec.gapPreID, spec.gapFill);
            fill(spec.syncLength, 0x00);
            fill(3, 0xF5);
            out.push_back(0xFE);
            out.push_back(cylinder);
            out.push_back(side);
            out.push_back(spec.sectorNumbers[i]);
            out.push_back(spec.sizeCodeFor(i));
            out.push_back(0xF7);
            fill(spec.gapPostID, spec.gapFill);
            fill(spec.syncLength, 0x00);
            fill(3, 0xF5);
            out.push_back(spec.dataMark);
            fill(128u << (spec.sizeCodeFor(i) & 3), dataFill);
            out.push_back(0xF7);
            fill(spec.gapPostData, spec.gapFill);
        }

        while (out.size() < spec.trackLength) out.push_back(spec.gapFill);
        return out;
    }
};

/// region <READ SECTOR with non-TR-DOS geometries>

/// W1: 9 x 512 (+3 layout) - READ SECTOR transfers 512 bytes
TEST_F(WD1793_UniversalTrack_Test, ReadSector_512Bytes)
{
    DiskImage image(2, 1, Spec::plus3());
    DiskImage::Track* track = image.getTrackForCylinderAndSide(1, 0);
    ASSERT_EQ(track->sectorCount(), 9u);

    std::vector<uint8_t> pattern(512);
    for (size_t i = 0; i < 512; i++) pattern[i] = static_cast<uint8_t>(i * 7);
    track->writeSectorData(4, pattern.data(), pattern.size());  // sector 5

    WD1793CUT fdc(_context);
    prepare(fdc, image, 1);
    fdc._sectorRegister = 5;
    issue(fdc, 0x80);

    std::vector<uint8_t> read = runReadUntilIdle(fdc, 1000);

    EXPECT_EQ(read.size(), 512u);
    EXPECT_EQ(read, pattern);
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_NOTFOUND);
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_CRCERR);
    EXPECT_EQ(fdc._sectorSize, 512);
}

/// W2: mixed sizes on one track - each sector transfers its own size
TEST_F(WD1793_UniversalTrack_Test, ReadSector_MixedSizes_128_1024)
{
    Spec spec = Spec::trdos(nullptr, 4);
    spec.sectorSizeCodes = { 1, 2, 0, 3 };
    DiskImage image(1, 1, spec);
    DiskImage::Track* track = image.getTrack(0);
    ASSERT_EQ(track->sectorCount(), 4u);

    std::vector<uint8_t> small(128, 0x33);
    std::vector<uint8_t> big(1024);
    for (size_t i = 0; i < 1024; i++) big[i] = static_cast<uint8_t>(i);
    track->writeSectorData(2, small.data(), small.size());
    track->writeSectorData(3, big.data(), big.size());

    WD1793CUT fdc(_context);
    prepare(fdc, image, 0);

    fdc._sectorRegister = 3;
    issue(fdc, 0x80);
    EXPECT_EQ(runReadUntilIdle(fdc, 1000), small);

    fdc._sectorRegister = 4;
    issue(fdc, 0x80);
    EXPECT_EQ(runReadUntilIdle(fdc, fdc._time + 1000), big);
}

/// W3: ID field without data field -> Record Not Found, no data transferred
TEST_F(WD1793_UniversalTrack_Test, ReadSector_IdOnly_ReturnsRNF)
{
    Spec spec = Spec::trdos();
    spec.idOnly.assign(16, 0);
    spec.idOnly[1] = 1;  // sector 2 has no data field
    DiskImage image(1, 1, spec);
    ASSERT_FALSE(image.getTrack(0)->getSector(1)->hasData);

    WD1793CUT fdc(_context);
    prepare(fdc, image, 0);
    fdc._sectorRegister = 2;
    issue(fdc, 0x80);

    std::vector<uint8_t> read = runReadUntilIdle(fdc, 1000);

    EXPECT_TRUE(read.empty());
    EXPECT_TRUE(fdc._statusRegister & WD1793::WDS_NOTFOUND);
    EXPECT_EQ(fdc._state, WD1793::S_IDLE);

    // Neighbouring sectors are unaffected
    fdc._sectorRegister = 3;
    issue(fdc, 0x80);
    EXPECT_EQ(runReadUntilIdle(fdc, fdc._time + 1000).size(), 256u);
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_NOTFOUND);
}

/// W5: multi-sector read walks consecutive numbers; running past the last sector number on the track
/// ends the command cleanly (datasheet end-of-track termination, no RNF). TR-DOS 5.04T COPY
/// skip-reads rely on this behaviour.
TEST_F(WD1793_UniversalTrack_Test, ReadSector_Multi_StopsAtFirstMissingNumber)
{
    DiskImage image(1, 1, Spec::plus3());  // sectors 1..9
    DiskImage::Track* track = image.getTrack(0);
    for (uint8_t s = 0; s < 9; s++)
    {
        std::vector<uint8_t> data(512, static_cast<uint8_t>(0xA0 + s));
        track->writeSectorData(s, data.data(), data.size());
    }

    WD1793CUT fdc(_context);
    prepare(fdc, image, 0);
    fdc._sectorRegister = 7;
    issue(fdc, 0x80 | WD1793::CMD_MULTIPLE);

    std::vector<uint8_t> read = runReadUntilIdle(fdc, 1000);

    ASSERT_EQ(read.size(), 3u * 512u) << "sectors 7, 8, 9";
    EXPECT_EQ(read[0], 0xA6);
    EXPECT_EQ(read[512], 0xA7);
    EXPECT_EQ(read[1024], 0xA8);
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_NOTFOUND) << "past-end overrun terminates cleanly, no RNF";
}

/// W5: a missing sector number inside the track (a gap below the last number) is a genuine
/// Record Not Found - the chip keeps searching for the absent ID
TEST_F(WD1793_UniversalTrack_Test, ReadSector_Multi_MissingNumberInsideTrack_ReturnsRNF)
{
    DiskImage image(1, 1, Spec::plus3());  // sectors 1..9, number 3 left out
    DiskImage::Track* track = image.getTrack(0);
    for (uint8_t s = 0; s < 9; s++)
    {
        if (s == 2)
        {
            continue;  // skip sector number 3 - a gap inside the track
        }
        std::vector<uint8_t> data(512, static_cast<uint8_t>(0xA0 + s));
        track->writeSectorData(s, data.data(), data.size());
    }

    WD1793CUT fdc(_context);
    prepare(fdc, image, 0);
    fdc._sectorRegister = 1;
    issue(fdc, 0x80 | WD1793::CMD_MULTIPLE);

    std::vector<uint8_t> read = runReadUntilIdle(fdc, 1000);

    ASSERT_EQ(read.size(), 2u * 512u) << "sectors 1, 2";
    EXPECT_EQ(read[0], 0xA0);
    EXPECT_EQ(read[512], 0xA1);
    EXPECT_TRUE(fdc._statusRegister & WD1793::WDS_NOTFOUND) << "sector 3 is a gap inside the track";
}

/// Cylinder in the ID field must match the track register (datasheet), otherwise RNF
TEST_F(WD1793_UniversalTrack_Test, ReadSector_CylinderMismatch_ReturnsRNF)
{
    DiskImage image(2, 1);
    WD1793CUT fdc(_context);
    prepare(fdc, image, 1);
    fdc._trackRegister = 0;  // head is on cylinder 1, register says 0
    fdc._sectorRegister = 1;
    issue(fdc, 0x80);

    std::vector<uint8_t> read = runReadUntilIdle(fdc, 1000);
    EXPECT_TRUE(read.empty());
    EXPECT_TRUE(fdc._statusRegister & WD1793::WDS_NOTFOUND);
}

/// endregion </READ SECTOR>

/// region <WRITE SECTOR>

/// W6: WRITE SECTOR regenerates the data CRC; a subsequent READ SECTOR has no CRC error
TEST_F(WD1793_UniversalTrack_Test, WriteSector_RecomputesDataCrc)
{
    DiskImage image(1, 1);
    DiskImage::Track* track = image.getTrack(0);

    std::vector<uint8_t> data(256);
    for (size_t i = 0; i < 256; i++) data[i] = static_cast<uint8_t>(0x5A ^ i);

    WD1793CUT fdc(_context);
    prepare(fdc, image, 0);
    fdc._sectorRegister = 5;
    issue(fdc, 0xA0);
    EXPECT_EQ(runWriteUntilIdle(fdc, data, 1000), 256u);
    waitIdle(fdc);

    DiskImage::Sector* sector = track->getSector(4);
    ASSERT_NE(sector, nullptr);
    EXPECT_EQ(std::memcmp(sector->data, data.data(), 256), 0);
    EXPECT_TRUE(sector->isDataCRCValid()) << "CRC must be regenerated by the controller";
    EXPECT_TRUE(sector->dirty);
    EXPECT_TRUE(track->isDirty());
    EXPECT_FALSE(track->isRawTrackDirty());
    EXPECT_TRUE(image.isDirty());
    EXPECT_EQ(sector->dataAddressMark(), 0xFB);

    fdc._sectorRegister = 5;
    issue(fdc, 0x80);
    std::vector<uint8_t> read = runReadUntilIdle(fdc, fdc._time + 1000);
    EXPECT_EQ(read, data);
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_CRCERR);
}

/// W7: WRITE SECTOR with a0 = 1 writes a deleted data mark; READ SECTOR reports it in status bit 5
TEST_F(WD1793_UniversalTrack_Test, WriteSector_DeletedFlag_WritesF8)
{
    DiskImage image(1, 1);
    DiskImage::Track* track = image.getTrack(0);
    std::vector<uint8_t> data(256, 0x11);

    WD1793CUT fdc(_context);
    prepare(fdc, image, 0);
    fdc._sectorRegister = 3;
    issue(fdc, 0xA1);  // deleted data mark
    EXPECT_EQ(runWriteUntilIdle(fdc, data, 1000), 256u);
    waitIdle(fdc);

    DiskImage::Sector* sector = track->getSector(2);
    EXPECT_EQ(sector->dataAddressMark(), 0xF8);
    EXPECT_TRUE(sector->deleted);
    EXPECT_TRUE(sector->isDataCRCValid());

    fdc._sectorRegister = 3;
    issue(fdc, 0x80);
    runReadUntilIdle(fdc, fdc._time + 1000);
    EXPECT_TRUE(fdc._statusRegister & WD1793::WDS_RECORDTYPE);
}

/// endregion </WRITE SECTOR>

/// region <READ TRACK / WRITE TRACK>

/// W8: READ TRACK returns the whole stream of a non-nominal track length
TEST_F(WD1793_UniversalTrack_Test, ReadTrack_VariableLength)
{
    Spec spec = Spec::trdos();
    spec.trackLength = 6300;
    DiskImage image(1, 1, spec);
    DiskImage::Track* track = image.getTrack(0);
    ASSERT_EQ(track->rawSize(), 6300u);
    track->rawData()[6299] = 0x77;

    WD1793CUT fdc(_context);
    prepare(fdc, image, 0);
    fdc.getDrive()->setMotor(true);
    issue(fdc, 0xE0);

    std::vector<uint8_t> read = runReadUntilIdle(fdc, 1000);

    ASSERT_EQ(read.size(), 6300u);
    EXPECT_EQ(std::memcmp(read.data(), track->rawData(), 6300), 0);
    EXPECT_EQ(read[6299], 0x77);
}

/// W10 + W11 + W13: a CPU-side 9 x 512 format through WRITE TRACK is indexed, clock-marked, dirty and readable
TEST_F(WD1793_UniversalTrack_Test, WriteTrack_9x512_Then_ReadSector)
{
    DiskImage image(1, 1);                       // Starts life as a TR-DOS track
    DiskImage::Track* track = image.getTrack(0);
    ASSERT_EQ(track->sectorCount(), 16u);

    Spec spec = Spec::plus3();
    std::vector<uint8_t> stream = formatStream(spec, 0, 0, 0xE5);
    ASSERT_EQ(stream.size(), 6250u);

    WD1793CUT fdc(_context);
    prepare(fdc, image, 0);
    fdc.getDrive()->setMotor(true);
    issue(fdc, 0xF0);

    size_t written = runWriteUntilIdle(fdc, stream, 1000);
    waitIdle(fdc);

    // The format control bytes (F5/F6/F7) expand to A1/C2/CRC pairs, so fewer source bytes than track bytes are consumed
    EXPECT_GT(written, 5000u);
    EXPECT_EQ(fdc._state, WD1793::S_IDLE);

    ASSERT_EQ(track->sectorCount(), 9u) << "index rebuilt from the written stream";
    EXPECT_TRUE(track->isRawTrackDirty());
    EXPECT_TRUE(track->isDirty());
    EXPECT_NE(track->indexMarkOffset(), DiskImage::Sector::NO_OFFSET) << "C2 C2 C2 FC written with F6 F6 F6 FC";

    for (size_t i = 0; i < 9; i++)
    {
        DiskImage::Sector* sector = track->getRawSector(i);
        EXPECT_EQ(sector->number(), i + 1);
        EXPECT_EQ(sector->dataSize, 512);
        EXPECT_TRUE(sector->hasData);
        EXPECT_TRUE(sector->idCrcValid) << "ID CRC generated by F7 must verify";
        EXPECT_TRUE(sector->dataCrcValid) << "data CRC generated by F7 must verify";
        EXPECT_TRUE(track->clockMark(sector->idamOffset - 1));
        EXPECT_TRUE(track->clockMark(sector->idamOffset - 3));
        EXPECT_FALSE(track->clockMark(sector->idamOffset));
        EXPECT_TRUE(track->clockMark(sector->damOffset - 1));
        EXPECT_FALSE(track->clockMark(sector->dataOffset));
    }

    // Read sector 5 back through the controller
    fdc._sectorRegister = 5;
    issue(fdc, 0x80);
    std::vector<uint8_t> read = runReadUntilIdle(fdc, fdc._time + 1000);
    EXPECT_EQ(read.size(), 512u);
    EXPECT_TRUE(std::all_of(read.begin(), read.end(), [](uint8_t b) { return b == 0xE5; }));
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_CRCERR);
}

/// W12: WRITE TRACK accepts exactly rawSize() bytes for a shorter track
TEST_F(WD1793_UniversalTrack_Test, WriteTrack_StopsAtTrackLength)
{
    DiskImage image(1, 1);
    DiskImage::Track* track = image.getTrack(0);
    track->resizeRaw(6208);

    WD1793CUT fdc(_context);
    prepare(fdc, image, 0);
    fdc.getDrive()->setMotor(true);
    issue(fdc, 0xF0);

    std::vector<uint8_t> source(7000, 0x4E);
    size_t written = runWriteUntilIdle(fdc, source, 1000);
    waitIdle(fdc);

    EXPECT_EQ(written, 6208u);
    EXPECT_EQ(track->rawSize(), 6208u);
    EXPECT_EQ(track->sectorCount(), 0u);
    EXPECT_FALSE(track->hasClockMarks());
}

/// endregion </READ TRACK / WRITE TRACK>

/// region <READ ADDRESS>

/// READ ADDRESS returns the next ID field under the head (rotational position) and copies its
/// cylinder into the sector register; consecutive calls walk the physical (interleave) order
TEST_F(WD1793_UniversalTrack_Test, ReadAddress_ReturnsNextIdUnderHead_AndCopiesCylinder)
{
    const uint8_t interleave[16] = {1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15, 8, 16};
    DiskImage image(10, 1);
    DiskImage::Track* track = image.getTrackForCylinderAndSide(7, 0);
    track->formatTrack(7, 0, Spec::trdos(interleave, 16));

    // Head position 500 bytes past the index: physical sectors 0 (IDAM @25) and 1 (@413) are behind the head,
    // the next ID field is physical sector 2 (@801) which carries number 2 in the 1:2 interleave
    const size_t period = WD1793::DISK_ROTATION_PERIOD_TSTATES;
    const size_t startClk = period * 10 + (period * 500) / 6250;

    WD1793CUT fdc(_context);
    prepare(fdc, image, 7);
    fdc._time = startClk;
    fdc.prolongFDDMotorRotation();
    fdc._sectorRegister = 0xEE;
    issue(fdc, 0xC0);

    std::vector<uint8_t> id = runReadUntilIdle(fdc, startClk);

    ASSERT_EQ(id.size(), 6u) << "C H R N CRC1 CRC2";
    EXPECT_EQ(id[0], 7) << "cylinder";
    EXPECT_EQ(id[1], 0) << "head";
    EXPECT_EQ(id[2], 2) << "sector number of the next ID field under the head";
    EXPECT_EQ(id[3], 1) << "size code";
    DiskImage::Sector* expected = track->getRawSector(2);
    EXPECT_EQ(id[4], track->rawData()[expected->idamOffset + 5]);
    EXPECT_EQ(id[5], track->rawData()[expected->idamOffset + 6]);
    EXPECT_EQ(fdc._sectorRegister, 7) << "track address of the ID field is written into the sector register";
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_CRCERR);
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_NOTFOUND);

    // The command took at least the rotational distance to the ID field (801 - 500 = 301 byte cells)
    const size_t elapsed = fdc._time - startClk;
    EXPECT_GE(elapsed, 301 * WD1793::WD93_TSTATES_PER_FDC_BYTE);
}

/// READ ADDRESS with a bad ID CRC still transfers the bytes and sets CRC ERROR
TEST_F(WD1793_UniversalTrack_Test, ReadAddress_BadIdCrc_SetsCrcError)
{
    DiskImage image(1, 1);
    DiskImage::Track* track = image.getTrack(0);
    track->getRawSector(0)->id->id_crc ^= 0xFFFF;
    track->reindex();

    WD1793CUT fdc(_context);
    prepare(fdc, image, 0);
    const size_t startClk = WD1793::DISK_ROTATION_PERIOD_TSTATES * 3;  // head at the index -> first ID field
    fdc._time = startClk;
    fdc.prolongFDDMotorRotation();
    issue(fdc, 0xC0);

    std::vector<uint8_t> id = runReadUntilIdle(fdc, startClk);
    ASSERT_EQ(id.size(), 6u);
    EXPECT_EQ(id[2], 1);
    EXPECT_TRUE(fdc._statusRegister & WD1793::WDS_CRCERR);
}

/// READ ADDRESS on an unformatted track ends with Record Not Found
TEST_F(WD1793_UniversalTrack_Test, ReadAddress_UnformattedTrack_ReturnsRNF)
{
    DiskImage image(1, 1);
    image.getTrack(0)->resizeRaw(6250);

    WD1793CUT fdc(_context);
    prepare(fdc, image, 0);
    issue(fdc, 0xC0);

    std::vector<uint8_t> id = runReadUntilIdle(fdc, 1000);
    EXPECT_TRUE(id.empty());
    EXPECT_TRUE(fdc._statusRegister & WD1793::WDS_NOTFOUND);
    EXPECT_EQ(fdc._state, WD1793::S_IDLE);
}

/// endregion </READ ADDRESS>

/// region <Rotational latency (Type II)>

/// A Type II command waits for the data field to pass under the head before the first byte is transferred.
/// The test loop steps in 100 T-state increments, so per-byte quantisation is the same for both measurements
/// and cancels out in the difference.
TEST_F(WD1793_UniversalTrack_Test, ReadSector_ChargesRotationalLatency)
{
    DiskImage image(1, 1);
    DiskImage::Track* track = image.getTrack(0);
    const size_t period = WD1793::DISK_ROTATION_PERIOD_TSTATES;
    const size_t startClk = period * 4;  // Head at the index (start of a revolution)

    auto timeRead = [&](uint8_t sectorNo) -> size_t
    {
        WD1793CUT fdc(_context);
        prepare(fdc, image, 0);
        fdc._time = startClk;
        fdc.prolongFDDMotorRotation();
        fdc._sectorRegister = sectorNo;
        issue(fdc, 0x80);
        std::vector<uint8_t> read = runReadUntilIdle(fdc, startClk);
        EXPECT_EQ(read.size(), 256u);
        size_t elapsed = fdc._time - startClk;
        fdc.getDrive()->ejectDisk();
        return elapsed;
    };

    const size_t t1 = timeRead(1);
    const size_t t12 = timeRead(12);

    const size_t latencyBytes = track->getSector(11)->dataOffset - track->getSector(0)->dataOffset;  // 11 x 388
    const size_t expected = latencyBytes * WD1793::WD93_TSTATES_PER_FDC_BYTE;
    EXPECT_GE(t12, t1 + expected - 200);
    EXPECT_LE(t12, t1 + expected + 200);

    // Sector 1 itself starts after its own data offset (70 bytes) plus the byte cell quantisation of the loop
    EXPECT_GE(t1, (70 + 256) * WD1793::WD93_TSTATES_PER_FDC_BYTE);
    EXPECT_LT(t1, (70 + 256 + 2) * 3 * 100 + 70 * WD1793::WD93_TSTATES_PER_FDC_BYTE);
}

/// Record Not Found is reported only after the search revolutions have passed
TEST_F(WD1793_UniversalTrack_Test, ReadSector_NotFound_TakesSearchRevolutions)
{
    DiskImage image(1, 1);
    WD1793CUT fdc(_context);
    prepare(fdc, image, 0);
    fdc._sectorRegister = 20;
    issue(fdc, 0x80);

    const size_t startClk = 1000;
    std::vector<uint8_t> read = runReadUntilIdle(fdc, startClk);
    EXPECT_TRUE(read.empty());
    EXPECT_TRUE(fdc._statusRegister & WD1793::WDS_NOTFOUND);
    EXPECT_GE(fdc._time - startClk, WD1793::WD93_REVOLUTIONS_LIMIT_FOR_TYPE2_INDEX_MARK_SEARCH * WD1793::DISK_ROTATION_PERIOD_TSTATES - 1000);
}

/// endregion </Rotational latency>

/// region <FM (single density)>

/// F3: FM track read with the controller in single density: 128-byte sector, 64 us byte cell
TEST_F(WD1793_UniversalTrack_Test, ReadSector_Fm)
{
    DiskImage image(1, 1, Spec::ibm3740());
    DiskImage::Track* track = image.getTrack(0);
    ASSERT_EQ(track->encoding(), DiskImage::Encoding::FM);
    ASSERT_EQ(track->sectorCount(), 16u);

    std::vector<uint8_t> pattern(128);
    for (size_t i = 0; i < 128; i++) pattern[i] = static_cast<uint8_t>(0x80 + i);
    track->writeSectorData(6, pattern.data(), pattern.size());  // sector 7

    WD1793CUT fdc(_context);
    prepare(fdc, image, 0, false, true);
    EXPECT_EQ(fdc.controllerEncoding(), DiskImage::Encoding::FM);
    EXPECT_EQ(fdc.byteCellTStates(*track), 2u * WD1793::WD93_TSTATES_PER_FDC_BYTE);

    fdc._sectorRegister = 7;
    issue(fdc, 0x80);
    std::vector<uint8_t> read = runReadUntilIdle(fdc, 1000);

    EXPECT_EQ(read, pattern);
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_NOTFOUND);
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_CRCERR);
    EXPECT_EQ(fdc._tstatesPerByte, 2u * WD1793::WD93_TSTATES_PER_FDC_BYTE);
}

/// F4: a density mismatch between controller and track means no address mark is recognised => RNF
TEST_F(WD1793_UniversalTrack_Test, DensityMismatch_ReturnsRNF)
{
    DiskImage image(2, 1);
    image.getTrack(1)->formatTrack(1, 0, Spec::ibm3740());  // cylinder 1 is FM, cylinder 0 is MFM

    // MFM controller on the FM track
    {
        WD1793CUT fdc(_context);
        prepare(fdc, image, 1, false, false);
        fdc._sectorRegister = 1;
        issue(fdc, 0x80);
        std::vector<uint8_t> read = runReadUntilIdle(fdc, 1000);
        EXPECT_TRUE(read.empty());
        EXPECT_TRUE(fdc._statusRegister & WD1793::WDS_NOTFOUND);

        // READ ADDRESS sees nothing either
        issue(fdc, 0xC0);
        read = runReadUntilIdle(fdc, fdc._time + 1000);
        EXPECT_TRUE(read.empty());
        EXPECT_TRUE(fdc._statusRegister & WD1793::WDS_NOTFOUND);
        fdc.getDrive()->ejectDisk();
    }

    // FM controller on the MFM track
    {
        WD1793CUT fdc(_context);
        prepare(fdc, image, 0, false, true);
        fdc._sectorRegister = 1;
        issue(fdc, 0x80);
        std::vector<uint8_t> read = runReadUntilIdle(fdc, 1000);
        EXPECT_TRUE(read.empty());
        EXPECT_TRUE(fdc._statusRegister & WD1793::WDS_NOTFOUND);
        fdc.getDrive()->ejectDisk();
    }
}

/// F5: a CPU-side single density format through WRITE TRACK equals the model's FM formatter byte for byte
TEST_F(WD1793_UniversalTrack_Test, WriteTrack_Fm_MatchesFormatter)
{
    DiskImage image(1, 1);                       // Starts as an MFM TR-DOS track
    DiskImage::Track* track = image.getTrack(0);

    Spec spec = Spec::ibm3740();
    std::vector<uint8_t> stream = formatStreamFM(spec, 0, 0, 0xE5);
    ASSERT_EQ(stream.size(), 3125u);

    WD1793CUT fdc(_context);
    prepare(fdc, image, 0, false, true);
    fdc.getDrive()->setMotor(true);
    issue(fdc, 0xF0);

    size_t written = runWriteUntilIdle(fdc, stream, 1000);
    waitIdle(fdc);
    EXPECT_GT(written, 3000u);

    EXPECT_EQ(track->encoding(), DiskImage::Encoding::FM) << "track re-formatted in single density";
    EXPECT_EQ(track->rawSize(), 3125u);
    ASSERT_EQ(track->sectorCount(), 16u);
    EXPECT_NE(track->indexMarkOffset(), DiskImage::Sector::NO_OFFSET);
    EXPECT_TRUE(track->isRawTrackDirty());

    // Reference: the formatter's own FM layout
    DiskImage reference(1, 1, spec);
    DiskImage::Track* ref = reference.getTrack(0);
    ASSERT_EQ(ref->rawSize(), track->rawSize());
    EXPECT_EQ(std::memcmp(ref->rawData(), track->rawData(), track->rawSize()), 0) << "stream differs from formatter";
    EXPECT_EQ(ref->clockBitmap(), track->clockBitmap()) << "clock marks differ from formatter";

    for (size_t i = 0; i < 16; i++)
    {
        DiskImage::Sector* sector = track->getRawSector(i);
        EXPECT_EQ(sector->dataSize, 128);
        EXPECT_TRUE(sector->idCrcValid) << "F7 in FM presets 0xFFFF at the mark";
        EXPECT_TRUE(sector->dataCrcValid);
        EXPECT_TRUE(track->clockMark(sector->idamOffset));
        EXPECT_TRUE(track->clockMark(sector->damOffset));
    }

    // Read one sector back in single density
    fdc._sectorRegister = 9;
    issue(fdc, 0x80);
    std::vector<uint8_t> read = runReadUntilIdle(fdc, fdc._time + 1000);
    EXPECT_EQ(read.size(), 128u);
    EXPECT_TRUE(std::all_of(read.begin(), read.end(), [](uint8_t b) { return b == 0xE5; }));
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_CRCERR);
}

/// endregion </FM>

