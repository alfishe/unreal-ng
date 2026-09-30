// WD1793 controller clock (1 / 2 MHz, turbo VG, latched), data separator rate (250 / 500 kbit/s) and
// Lost Data on writes. See docs/WD1793/WD1793_Timeouts.md, "Controller clock and data rate in unreal-ng".
#include "emulator/io/fdc/wd1793.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testtiminghelper.h"
#include "common/modulelogger.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/fdd.h"

class WD1793Clock_Test : public ::testing::Test
{
protected:
    static constexpr size_t TSTATES_PER_MS = 3500;
    static constexpr size_t ROTATION = WD1793::DISK_ROTATION_PERIOD_TSTATES;  // 700 000 T = 200 ms

    EmulatorContext* _context = nullptr;
    CoreCUT* _core = nullptr;
    Z80* _z80 = nullptr;
    TestTimingHelper* _timingHelper = nullptr;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _context->pModuleLogger->TurnOffLoggingForAll();
        _context->pModuleLogger->SetLoggingLevel(LoggerLevel::LogError);

        _core = new CoreCUT(_context);
        _z80 = new Z80(_context);
        _core->_z80 = _z80;
        _context->pCore = _core;

        _timingHelper = new TestTimingHelper(_context);
        _timingHelper->resetClock();
    }

    void TearDown() override
    {
        delete _timingHelper;
        _core->_z80 = nullptr;
        delete _z80;
        _context->pCore = nullptr;
        delete _core;
        delete _context;
    }

    /// Drive A, motor on, disk in, head and track register on the given cylinder, MFM
    void Prepare(WD1793CUT& fdc, DiskImage& image, uint8_t cylinder, size_t time = 1000)
    {
        fdc.getDrive()->insertDisk(&image);
        fdc._beta128Register = WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET;
        fdc._drive = 0;
        fdc.wakeUp();
        fdc._time = time;
        fdc._lastTime = time;
        fdc.prolongFDDMotorRotation();
        fdc.getDrive()->setMotor(true);
        fdc._trackRegister = cylinder;
        fdc._selectedDrive->setTrack(cylinder);
        fdc._sideUp = false;
    }

    /// Dispatch a command the way processWD93Command does (without the BUSY gate)
    void Issue(WD1793CUT& fdc, uint8_t command)
    {
        const WD1793CUT::WD_COMMANDS decoded = WD1793CUT::decodeWD93Command(command);
        const uint8_t value = WD1793CUT::getWD93CommandValue(decoded, command);
        fdc._commandRegister = command;
        fdc._lastDecodedCmd = decoded;
        fdc._lastCmdValue = value;
        fdc._statusRegister |= WD1793::WDS_BUSY;

        switch (decoded)
        {
            case WD1793::WD_CMD_RESTORE:      fdc.cmdRestore(value); break;
            case WD1793::WD_CMD_SEEK:         fdc.cmdSeek(value); break;
            case WD1793::WD_CMD_STEP:         fdc.cmdStep(value); break;
            case WD1793::WD_CMD_STEP_IN:      fdc.cmdStepIn(value); break;
            case WD1793::WD_CMD_STEP_OUT:     fdc.cmdStepOut(value); break;
            case WD1793::WD_CMD_READ_SECTOR:  fdc.cmdReadSector(value); break;
            case WD1793::WD_CMD_WRITE_SECTOR: fdc.cmdWriteSector(value); break;
            case WD1793::WD_CMD_READ_ADDRESS: fdc.cmdReadAddress(value); break;
            case WD1793::WD_CMD_READ_TRACK:   fdc.cmdReadTrack(value); break;
            case WD1793::WD_CMD_WRITE_TRACK:  fdc.cmdWriteTrack(value); break;
            default: FAIL() << "unexpected command"; break;
        }
    }

    /// Advance the FSM in steps until it is idle; returns the time it went idle (0 = timeout)
    size_t RunUntilIdle(WD1793CUT& fdc, size_t maxTStates, size_t step)
    {
        const size_t start = fdc._time;
        for (size_t clk = start + step; clk <= start + maxTStates; clk += step)
        {
            fdc._time = clk;
            fdc.process();
            if (fdc._state == WD1793::S_IDLE)
                return clk;
        }
        return 0;
    }

    /// Put the FSM back to idle between direct command calls
    static void Idle(WD1793CUT& fdc)
    {
        fdc._state = WD1793::S_IDLE;
        fdc._state2 = WD1793::S_IDLE;
        fdc._delayTStates = 0;
        fdc._statusRegister = 0;
    }

    /// 1.44 MB layout: 18 x 512-byte sectors on a 12 500-byte (500 kbit/s) MFM track
    static void FormatHD(DiskImage::Track* track, uint8_t cylinder, uint8_t fill = 0xE5)
    {
        DiskImage::TrackFormatSpec spec;
        spec.encoding = DiskImage::Encoding::MFM;
        spec.trackLength = 12500;
        spec.sizeCode = DiskImage::SECTOR_SIZE_512;
        for (uint8_t n = 1; n <= 18; n++)
            spec.sectorNumbers.push_back(n);
        spec.dataFill = fill;
        track->formatTrack(cylinder, 0, spec);
    }

    static void SetLatched2MHzHD(WD1793CUT& fdc)
    {
        fdc.SetClockPolicy(FdcClockPolicy::Latched);
        ASSERT_TRUE(fdc.SetLatchedClock(FdcClock::Clock2MHz, FdcDataRate::Rate500Kbps));
    }
};

/// region <Defaults and policy resolution>

TEST_F(WD1793Clock_Test, Defaults_Fixed1MHz_DDSeparator)
{
    WD1793CUT fdc(_context);
    EXPECT_EQ(fdc.GetClockPolicy(), FdcClockPolicy::Fixed1MHz);
    EXPECT_EQ(fdc.GetClock(), FdcClock::Clock1MHz);
    EXPECT_EQ(fdc.GetDataRate(), FdcDataRate::Rate250Kbps);
    EXPECT_EQ(fdc.HeadSettleTStates(), 30 * TSTATES_PER_MS);
}

TEST_F(WD1793Clock_Test, ResolveClockPolicy_ConfigOverride)
{
    using P = FdcClockPolicy;
    EXPECT_EQ(WD1793::ResolveClockPolicy(P::Fixed1MHz, -1), P::Fixed1MHz);
    EXPECT_EQ(WD1793::ResolveClockPolicy(P::AutoStepTurbo, -1), P::AutoStepTurbo);
    EXPECT_EQ(WD1793::ResolveClockPolicy(P::Fixed1MHz, 1), P::AutoStepTurbo) << "TurboVG=1 on a Pentagon (magazine mod)";
    EXPECT_EQ(WD1793::ResolveClockPolicy(P::AutoStepTurbo, 0), P::Fixed1MHz) << "TurboVG=0 turns the ZX-Evo turbo off";
    EXPECT_EQ(WD1793::ResolveClockPolicy(P::Latched, 0), P::Latched) << "a latch machine keeps its latch";
    EXPECT_EQ(WD1793::ResolveClockPolicy(P::Latched, 1), P::Latched);
}

/// endregion </Defaults and policy resolution>

/// region <Clock-derived timers>

TEST_F(WD1793Clock_Test, StepRate_HalvesAt2MHz)
{
    static constexpr size_t ms1MHz[] = {6, 12, 20, 30};
    static constexpr size_t ms2MHz[] = {3, 6, 10, 15};

    WD1793CUT fdc(_context);
    DiskImage image(80, 1);
    Prepare(fdc, image, 10);

    for (uint8_t r = 0; r < 4; r++)
    {
        Idle(fdc);
        Issue(fdc, static_cast<uint8_t>(0x40 | r));  // STEP IN, h=0, V=0
        EXPECT_EQ(fdc._state, WD1793::S_WAIT);
        EXPECT_EQ(fdc._state2, WD1793::S_STEP);
        EXPECT_EQ(static_cast<size_t>(fdc._delayTStates) + 1, ms1MHz[r] * TSTATES_PER_MS) << "r1r0=" << int(r) << " @1 MHz";
        EXPECT_EQ(fdc._steppingMotorRate, ms1MHz[r]);
    }

    SetLatched2MHzHD(fdc);
    for (uint8_t r = 0; r < 4; r++)
    {
        Idle(fdc);
        Issue(fdc, static_cast<uint8_t>(0x40 | r));
        EXPECT_EQ(static_cast<size_t>(fdc._delayTStates) + 1, ms2MHz[r] * TSTATES_PER_MS) << "r1r0=" << int(r) << " @2 MHz";
        EXPECT_EQ(fdc._steppingMotorRate, ms2MHz[r]);
    }
}

TEST_F(WD1793Clock_Test, VerifySettle_30msAt1MHz_15msAt2MHz)
{
    WD1793CUT fdc(_context);
    DiskImage image(80, 1);
    Prepare(fdc, image, 5);

    // SEEK to the current track with V=1: no step, straight to the settle before verify
    fdc._dataRegister = 5;
    Issue(fdc, 0x14);
    EXPECT_EQ(fdc._state2, WD1793::S_VERIFY);
    EXPECT_EQ(static_cast<size_t>(fdc._delayTStates) + 1, 30 * TSTATES_PER_MS);

    Idle(fdc);
    SetLatched2MHzHD(fdc);
    Issue(fdc, 0x14);
    EXPECT_EQ(fdc._state2, WD1793::S_VERIFY);
    EXPECT_EQ(static_cast<size_t>(fdc._delayTStates) + 1, 15 * TSTATES_PER_MS);
}

/// Every Type II / III command with E=1 waits the settle; E=0 does not
TEST_F(WD1793Clock_Test, EFlagSettle_AllTypeIIAndIIICommands)
{
    const uint8_t withE[] = {0x84, 0xA4, 0xC4, 0xE4, 0xF4};  // read/write sector, read address, read/write track
    const uint8_t withoutE[] = {0x80, 0xA0, 0xC0, 0xE0, 0xF0};

    for (FdcClock clock : {FdcClock::Clock1MHz, FdcClock::Clock2MHz})
    {
        const size_t settle = (clock == FdcClock::Clock1MHz ? 30 : 15) * TSTATES_PER_MS;
        for (size_t i = 0; i < 5; i++)
        {
            WD1793CUT fdc(_context);
            DiskImage image(2, 1);
            Prepare(fdc, image, 0);
            fdc._sectorRegister = 1;
            if (clock == FdcClock::Clock2MHz)
            {
                fdc.SetClockPolicy(FdcClockPolicy::Latched);
                fdc.SetLatchedClock(FdcClock::Clock2MHz, FdcDataRate::Rate250Kbps);
            }

            Issue(fdc, withE[i]);
            EXPECT_EQ(fdc._state, WD1793::S_WAIT) << std::hex << int(withE[i]);
            EXPECT_EQ(static_cast<size_t>(fdc._delayTStates) + 1, settle) << std::hex << int(withE[i]);

            Idle(fdc);
            Issue(fdc, withoutE[i]);
            EXPECT_NE(fdc._state, WD1793::S_WAIT) << std::hex << int(withoutE[i]) << ": no settle without E";
            fdc.getDrive()->ejectDisk();
        }
    }
}

/// endregion </Clock-derived timers>

/// region <AutoStepTurbo (ZX-Evo turbo VG)>

TEST_F(WD1793Clock_Test, AutoStepTurbo_StepSelects2MHz_DrqReturnsTo1MHz)
{
    WD1793CUT fdc(_context);
    DiskImage image(80, 1);
    Prepare(fdc, image, 0);
    fdc.SetClockPolicy(FdcClockPolicy::AutoStepTurbo);
    ASSERT_EQ(fdc.GetClock(), FdcClock::Clock1MHz);

    // SEEK 0 -> 5, r1r0 = 00, no verify: the first step pulse switches to 2 MHz, steps are 3 ms
    fdc._dataRegister = 5;
    Issue(fdc, 0x10);
    EXPECT_EQ(fdc.GetClock(), FdcClock::Clock2MHz);
    EXPECT_EQ(static_cast<size_t>(fdc._delayTStates) + 1, 3 * TSTATES_PER_MS);

    const size_t start = fdc._time;
    const size_t end = RunUntilIdle(fdc, 100 * TSTATES_PER_MS, 100);
    ASSERT_NE(end, 0u);
    EXPECT_EQ(fdc._selectedDrive->getTrack(), 5);
    EXPECT_NEAR(static_cast<double>(end - start), 5.0 * 3 * TSTATES_PER_MS, 1000.0) << "5 steps x 3 ms";
    EXPECT_EQ(fdc.GetClock(), FdcClock::Clock2MHz) << "no DRQ yet: the turbo phase lasts";

    // READ SECTOR: the ID search still runs at 2 MHz, the first DRQ returns to 1 MHz
    fdc._sectorRegister = 1;
    Issue(fdc, 0x80);
    EXPECT_EQ(fdc.GetClock(), FdcClock::Clock2MHz);
    bool sawDrq = false;
    for (size_t clk = fdc._time; clk < fdc._time + ROTATION * 2; clk += 20)
    {
        fdc._time = clk;
        fdc.process();
        if (fdc._drq_out)
        {
            sawDrq = true;
            break;
        }
    }
    ASSERT_TRUE(sawDrq);
    EXPECT_EQ(fdc.GetClock(), FdcClock::Clock1MHz) << "DRQ ends the turbo phase";
    EXPECT_EQ(fdc._tstatesPerByte, 112u) << "data transfer is unchanged: the separator stays at 250 kbit/s";
}

/// SEEK 0 -> 40 with verify: 40 x 3 ms + 15 ms on turbo VG versus 40 x 6 ms + 30 ms at a fixed 1 MHz
TEST_F(WD1793Clock_Test, AutoStepTurbo_SeekWithVerify_TakesHalfTheTime)
{
    for (FdcClockPolicy policy : {FdcClockPolicy::Fixed1MHz, FdcClockPolicy::AutoStepTurbo})
    {
        WD1793CUT fdc(_context);
        DiskImage image(80, 1);
        Prepare(fdc, image, 0);
        fdc.SetClockPolicy(policy);

        fdc._dataRegister = 40;
        Issue(fdc, 0x14);  // SEEK, V=1, r1r0=00
        const size_t start = fdc._time;
        const size_t end = RunUntilIdle(fdc, 400 * TSTATES_PER_MS, 100);
        ASSERT_NE(end, 0u);

        const double expectedMs = policy == FdcClockPolicy::AutoStepTurbo ? 40 * 3 + 15 : 40 * 6 + 30;
        // Steps + settle, then the verify waits for the next ID field with C = 40 to pass under the head:
        // at most one TR-DOS sector spacing (388 bytes x 112 T = 12.4 ms) plus the ID itself, plus sampling
        const double elapsedMs = static_cast<double>(end - start) / TSTATES_PER_MS;
        EXPECT_GE(elapsedMs, expectedMs) << WD1793::ClockPolicyName(policy);
        EXPECT_LE(elapsedMs, expectedMs + 14.0) << WD1793::ClockPolicyName(policy);
        EXPECT_FALSE(fdc.getStatusRegister() & WD1793::WDS_SEEKERR);
        fdc.getDrive()->ejectDisk();
    }
}

TEST_F(WD1793Clock_Test, AutoStepTurbo_NoStepPulse_StaysAt1MHz_ResetReturnsTo1MHz)
{
    WD1793CUT fdc(_context);
    DiskImage image(80, 1);
    Prepare(fdc, image, 7);
    fdc.SetClockPolicy(FdcClockPolicy::AutoStepTurbo);

    // SEEK to the current track: no step pulse, so the verify settle is the 1 MHz one
    fdc._dataRegister = 7;
    Issue(fdc, 0x14);
    EXPECT_EQ(fdc.GetClock(), FdcClock::Clock1MHz);
    EXPECT_EQ(static_cast<size_t>(fdc._delayTStates) + 1, 30 * TSTATES_PER_MS);

    Idle(fdc);
    Issue(fdc, 0x48);  // STEP IN
    EXPECT_EQ(fdc.GetClock(), FdcClock::Clock2MHz);

    fdc.internalReset();
    EXPECT_EQ(fdc.GetClock(), FdcClock::Clock1MHz) << "chip reset ends the turbo phase";
    EXPECT_EQ(fdc.GetClockPolicy(), FdcClockPolicy::AutoStepTurbo) << "the policy is machine configuration";
}

/// endregion </AutoStepTurbo (ZX-Evo turbo VG)>

/// region <Latched (Sprinter #BD style)>

TEST_F(WD1793Clock_Test, Latched_SetsClockAndRateTogether_IgnoresStepAndDrq)
{
    WD1793CUT fdc(_context);
    DiskImage image(80, 1);
    Prepare(fdc, image, 0);

    EXPECT_FALSE(fdc.SetLatchedClock(FdcClock::Clock2MHz, FdcDataRate::Rate500Kbps)) << "only a Latched machine has the latch";
    EXPECT_EQ(fdc.GetClock(), FdcClock::Clock1MHz);
    EXPECT_EQ(fdc.GetDataRate(), FdcDataRate::Rate250Kbps);

    fdc.SetClockPolicy(FdcClockPolicy::Latched);
    EXPECT_TRUE(fdc.SetLatchedClock(FdcClock::Clock2MHz, FdcDataRate::Rate500Kbps));
    EXPECT_EQ(fdc.GetClock(), FdcClock::Clock2MHz);
    EXPECT_EQ(fdc.GetDataRate(), FdcDataRate::Rate500Kbps);

    // DD mode on the latch: a STEP pulse does not turbo the clock
    EXPECT_TRUE(fdc.SetLatchedClock(FdcClock::Clock1MHz, FdcDataRate::Rate250Kbps));
    Issue(fdc, 0x48);
    EXPECT_EQ(fdc.GetClock(), FdcClock::Clock1MHz);

    // HD mode survives a chip reset (the latch belongs to the machine)
    fdc.SetLatchedClock(FdcClock::Clock2MHz, FdcDataRate::Rate500Kbps);
    fdc.internalReset();
    EXPECT_EQ(fdc.GetClock(), FdcClock::Clock2MHz);
    EXPECT_EQ(fdc.GetDataRate(), FdcDataRate::Rate500Kbps);
}

/// endregion </Latched (Sprinter #BD style)>

/// region <Data rate and medium>

TEST_F(WD1793Clock_Test, RecordedDataRate_FromTrackLengthAndEncoding)
{
    using Enc = DiskImage::Encoding;
    DiskImage image(1, 1);
    DiskImage::Track* track = image.getTrack(0);

    struct Case { size_t size; Enc enc; FdcDataRate rate; };
    const Case cases[] = {
        {6250, Enc::MFM, FdcDataRate::Rate250Kbps},  {6464, Enc::MFM, FdcDataRate::Rate250Kbps},
        {9374, Enc::MFM, FdcDataRate::Rate250Kbps},  {9375, Enc::MFM, FdcDataRate::Rate500Kbps},
        {12500, Enc::MFM, FdcDataRate::Rate500Kbps}, {3125, Enc::FM, FdcDataRate::Rate250Kbps},
        {4687, Enc::FM, FdcDataRate::Rate250Kbps},   {4688, Enc::FM, FdcDataRate::Rate500Kbps},
        {6250, Enc::FM, FdcDataRate::Rate500Kbps},
    };
    for (const Case& c : cases)
    {
        track->resizeRaw(c.size, c.enc);
        EXPECT_EQ(track->RecordedDataRate(), c.rate) << c.size << (c.enc == Enc::MFM ? " MFM" : " FM");
    }

    EXPECT_EQ(DiskImage::RawTrack::NominalTrackSize(Enc::MFM, FdcDataRate::Rate250Kbps), 6250u);
    EXPECT_EQ(DiskImage::RawTrack::NominalTrackSize(Enc::MFM, FdcDataRate::Rate500Kbps), 12500u);
    EXPECT_EQ(DiskImage::RawTrack::NominalTrackSize(Enc::FM, FdcDataRate::Rate250Kbps), 3125u);
    EXPECT_EQ(DiskImage::RawTrack::NominalTrackSize(Enc::FM, FdcDataRate::Rate500Kbps), 6250u);
}

/// HD medium in a DD machine: READ SECTOR, WRITE SECTOR and READ ADDRESS find no ID field and end with
/// Record Not Found after the revolution limit
TEST_F(WD1793Clock_Test, RateMismatch_HDMediumDDSeparator_RecordNotFound)
{
    struct Case { uint8_t command; size_t revolutions; };
    const Case cases[] = {{0x80, 4}, {0xA0, 4}, {0xC0, 5}};

    for (const Case& c : cases)
    {
        WD1793CUT fdc(_context);
        DiskImage image(1, 1);
        FormatHD(image.getTrack(0), 0);
        Prepare(fdc, image, 0);
        fdc._sectorRegister = 1;

        Issue(fdc, c.command);
        const size_t start = fdc._time;
        const size_t end = RunUntilIdle(fdc, ROTATION * 6, 5000);
        ASSERT_NE(end, 0u) << std::hex << int(c.command);
        EXPECT_TRUE(fdc._statusRegister & WD1793::WDS_NOTFOUND) << std::hex << int(c.command);
        EXPECT_GE(end - start, c.revolutions * ROTATION) << std::hex << int(c.command);
        EXPECT_FALSE(fdc._drq_out);
        fdc.getDrive()->ejectDisk();
    }
}

/// DD medium read with an HD separator (latched HD mode): no ID field either
TEST_F(WD1793Clock_Test, RateMismatch_DDMediumHDSeparator_RecordNotFound)
{
    WD1793CUT fdc(_context);
    DiskImage image(1, 1);  // TR-DOS 6250-byte tracks
    Prepare(fdc, image, 0);
    SetLatched2MHzHD(fdc);
    fdc._sectorRegister = 1;

    Issue(fdc, 0x80);
    const size_t end = RunUntilIdle(fdc, ROTATION * 6, 5000);
    ASSERT_NE(end, 0u);
    EXPECT_TRUE(fdc._statusRegister & WD1793::WDS_NOTFOUND);
    fdc.getDrive()->ejectDisk();
}

/// Type I verify on a track at the other rate: Seek Error after 5 revolutions; matched rate verifies clean
TEST_F(WD1793Clock_Test, RateMismatch_Verify_SeekError)
{
    for (bool hd : {true, false})
    {
        WD1793CUT fdc(_context);
        DiskImage image(1, 1);
        if (hd)
            FormatHD(image.getTrack(0), 0);
        Prepare(fdc, image, 0);

        fdc._dataRegister = 0;
        Issue(fdc, 0x14);  // SEEK to the current track with verify
        const size_t start = fdc._time;
        const size_t end = RunUntilIdle(fdc, ROTATION * 7, 5000);
        ASSERT_NE(end, 0u);
        if (hd)
        {
            EXPECT_TRUE(fdc.getStatusRegister() & WD1793::WDS_SEEKERR);
            EXPECT_GE(end - start, 5 * ROTATION);
        }
        else
        {
            EXPECT_FALSE(fdc.getStatusRegister() & WD1793::WDS_SEEKERR);
            EXPECT_LT(end - start, ROTATION);
        }
        fdc.getDrive()->ejectDisk();
    }
}

/// HD medium with an HD separator: the sector reads, one byte every 16 us (56 T-states at 3.5 MHz)
TEST_F(WD1793Clock_Test, MatchedHD_ReadSector_HalvedBytePeriod)
{
    WD1793CUT fdc(_context);
    DiskImage image(1, 1);
    FormatHD(image.getTrack(0), 0, 0xE5);
    Prepare(fdc, image, 0);
    SetLatched2MHzHD(fdc);
    fdc._sectorRegister = 3;

    Issue(fdc, 0x80);
    std::vector<uint8_t> bytes;
    for (size_t clk = fdc._time; clk < fdc._time + ROTATION * 2; clk += 8)
    {
        fdc._time = clk;
        fdc.process();
        if (fdc._beta128status & WD1793::DRQ)
            bytes.push_back(fdc.readDataRegister());
        if (fdc._state == WD1793::S_IDLE)
            break;
    }

    EXPECT_EQ(fdc._state, WD1793::S_IDLE);
    EXPECT_EQ(fdc._tstatesPerByte, 56u);
    ASSERT_EQ(bytes.size(), 512u);
    EXPECT_TRUE(std::all_of(bytes.begin(), bytes.end(), [](uint8_t b) { return b == 0xE5; }));
    EXPECT_FALSE(fdc._statusRegister & (WD1793::WDS_NOTFOUND | WD1793::WDS_CRCERR | WD1793::WDS_LOSTDATA));
}

/// WRITE TRACK at 2 MHz lays down an HD track: a DD track under the head becomes a 12 500-byte stream
TEST_F(WD1793Clock_Test, WriteTrack_At2MHz_FormatsHDTrack)
{
    WD1793CUT fdc(_context);
    DiskImage image(1, 1);
    DiskImage::Track* track = image.getTrack(0);
    ASSERT_EQ(track->RecordedDataRate(), FdcDataRate::Rate250Kbps);
    Prepare(fdc, image, 0, ROTATION - 2000);  // just before an index pulse
    SetLatched2MHzHD(fdc);

    Issue(fdc, 0xF0);
    fdc.writeDataRegister(0x4E);
    for (size_t clk = fdc._time; clk < fdc._time + 3000; clk += 20)  // up to the index pulse
    {
        fdc._time = clk;
        fdc.process();
        if (fdc._state == WD1793::S_WRITE_TRACK || fdc._state2 == WD1793::S_WRITE_TRACK)
            break;
    }

    EXPECT_EQ(track->rawSize(), 12500u);
    EXPECT_EQ(track->RecordedDataRate(), FdcDataRate::Rate500Kbps);
    EXPECT_EQ(fdc._tstatesPerByte, 56u);
}

/// endregion </Data rate and medium>

/// region <Lost Data on writes>

TEST_F(WD1793Clock_Test, WriteSector_LostData_FirstByte_TerminatesWithoutWriting)
{
    WD1793CUT fdc(_context);
    DiskImage image(1, 1);
    Prepare(fdc, image, 0);
    DiskImage::Sector* sector = image.getTrack(0)->getSector(0);
    ASSERT_NE(sector, nullptr);
    std::memset(sector->data, 0xAA, sector->dataSize);
    fdc._sectorRegister = 1;

    Issue(fdc, 0xA0);
    const size_t end = RunUntilIdle(fdc, ROTATION * 2, 20);  // the host never loads the Data Register
    ASSERT_NE(end, 0u);

    EXPECT_TRUE(fdc._statusRegister & WD1793::WDS_LOSTDATA);
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_BUSY);
    EXPECT_TRUE(fdc._intrq_out);
    EXPECT_TRUE(std::all_of(sector->data, sector->data + sector->dataSize, [](uint8_t b) { return b == 0xAA; }))
        << "nothing is written when the first byte is missing";
}

TEST_F(WD1793Clock_Test, WriteSector_LostData_LaterByte_WritesZeroAndContinues)
{
    WD1793CUT fdc(_context);
    DiskImage image(1, 1);
    Prepare(fdc, image, 0);
    DiskImage::Sector* sector = image.getTrack(0)->getSector(0);
    ASSERT_NE(sector, nullptr);
    fdc._sectorRegister = 1;

    Issue(fdc, 0xA0);
    size_t served = 0;
    size_t stallUntil = 0;
    for (size_t clk = fdc._time; clk < fdc._time + ROTATION * 2; clk += 8)
    {
        fdc._time = clk;
        fdc.process();
        if (fdc._state == WD1793::S_IDLE)
            break;
        if ((fdc._beta128status & WD1793::DRQ) && clk >= stallUntil)
        {
            fdc.writeDataRegister(0x55);
            // The next DRQ comes one 112 T cell after this one; do not answer it before 1.5 cells later, so
            // exactly one byte cell passes without data
            if (++served == 100)
                stallUntil = clk + 112 + 170;
        }
    }

    ASSERT_EQ(fdc._state, WD1793::S_IDLE);
    EXPECT_TRUE(fdc._statusRegister & WD1793::WDS_LOSTDATA);
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_NOTFOUND);
    EXPECT_EQ(sector->data[99], 0x55);
    EXPECT_EQ(sector->data[100], 0x00) << "the missed byte is written as zeros";
    EXPECT_EQ(sector->data[101], 0x55) << "the command continued";
    EXPECT_EQ(sector->data[255], 0x55);
    EXPECT_EQ(std::count(sector->data, sector->data + 256, 0x00), 1);
    EXPECT_TRUE(sector->isDataCRCValid());
}

/// Short 640-byte (DD) track: one byte cell is ~1094 T-states, so the test runs in coarse steps
TEST_F(WD1793Clock_Test, WriteTrack_LostData_FirstByteMissingAtIndex_Terminates)
{
    WD1793CUT fdc(_context);
    DiskImage image(1, 1);
    DiskImage::Track* track = image.getTrack(0);
    track->resizeRaw(640);
    const std::vector<uint8_t> before(track->rawData(), track->rawData() + track->rawSize());
    Prepare(fdc, image, 0, ROTATION - 2000);

    Issue(fdc, 0xF0);
    EXPECT_TRUE(fdc._drq_out) << "DRQ is raised when the command is accepted";
    const size_t start = fdc._time;
    const size_t end = RunUntilIdle(fdc, ROTATION, 100);
    ASSERT_NE(end, 0u);

    EXPECT_TRUE(fdc._statusRegister & WD1793::WDS_LOSTDATA);
    EXPECT_LT(end - start, 3000u) << "ends at the index pulse";
    EXPECT_TRUE(std::equal(before.begin(), before.end(), track->rawData())) << "nothing written";
}

TEST_F(WD1793Clock_Test, WriteTrack_LostData_LaterByte_WritesZeroAndContinues)
{
    WD1793CUT fdc(_context);
    DiskImage image(1, 1);
    DiskImage::Track* track = image.getTrack(0);
    track->resizeRaw(640);
    const size_t cell = ROTATION / 640;
    Prepare(fdc, image, 0, ROTATION - 2000);

    Issue(fdc, 0xF0);
    size_t served = 0;
    size_t stallUntil = 0;
    for (size_t clk = fdc._time; clk < fdc._time + ROTATION * 2; clk += 50)
    {
        fdc._time = clk;
        fdc.process();
        if (fdc._state == WD1793::S_IDLE)
            break;
        if ((fdc._beta128status & WD1793::DRQ) && clk >= stallUntil)
        {
            fdc.writeDataRegister(0x4E);
            if (++served == 50)
                stallUntil = clk + cell + cell * 3 / 2;  // the next DRQ is one cell away: miss exactly one cell
        }
    }

    ASSERT_EQ(fdc._state, WD1793::S_IDLE);
    EXPECT_TRUE(fdc._statusRegister & WD1793::WDS_LOSTDATA);
    EXPECT_EQ(std::count(track->rawData(), track->rawData() + track->rawSize(), 0x00), 1)
        << "one zero byte substituted, the rest of the revolution written";
    EXPECT_EQ(track->rawData()[track->rawSize() - 1], 0x4E) << "written to the end of the track";
}

/// endregion </Lost Data on writes>

/// region <Type I verify>
/// Datasheet Type I flowchart: after the settle, IDs pass under the head; C == Track Register with a good CRC
/// ends the command, a bad CRC on a match sets CRC ERROR and the search goes on, no match by the fifth index
/// hole is Seek Error. The side is not compared.

class WD1793Verify_Test : public WD1793Clock_Test
{
protected:
    /// SEEK to the track register's own value with V=1: no step pulse, just settle + verify
    size_t Verify(WD1793CUT& fdc, size_t step = 20)
    {
        fdc._dataRegister = fdc._trackRegister;
        Issue(fdc, 0x14);
        const size_t start = fdc._time;
        const size_t end = RunUntilIdle(fdc, ROTATION * 7, step);
        EXPECT_NE(end, 0u) << "verify never finished";
        return end ? end - start : 0;
    }

    static constexpr size_t SETTLE = 30 * 3500;
};

TEST_F(WD1793Verify_Test, MatchingId_PassesAtTheIdPosition)
{
    WD1793CUT fdc(_context);
    DiskImage image(1, 1);
    DiskImage::Track* track = image.getTrack(0);
    Prepare(fdc, image, 0, 1000);

    // Where the head will be once the settle is over, and the first ID it meets there
    const size_t headAfterSettle = static_cast<size_t>(
        (static_cast<uint64_t>((1000 + SETTLE) % ROTATION) * track->rawSize()) / ROTATION);
    const DiskImage::Sector* first = track->nextSector(headAfterSettle);
    ASSERT_NE(first, nullptr);
    const size_t expected = SETTLE + (track->bytesUntil(*first, headAfterSettle) + 7) * (ROTATION / track->rawSize());

    const size_t elapsed = Verify(fdc, 4);
    EXPECT_NEAR(static_cast<double>(elapsed), static_cast<double>(expected), 16.0);
    EXPECT_FALSE(fdc.getStatusRegister() & WD1793::WDS_SEEKERR);
    EXPECT_FALSE(fdc.getStatusRegister() & WD1793::WDS_CRCERR);
}

/// Track register says 3, the head reads cylinder 0 (IDs carry C = 0): Seek Error at the fifth index pulse
TEST_F(WD1793Verify_Test, TrackRegisterMismatch_SeekErrorAtFifthIndex)
{
    WD1793CUT fdc(_context);
    DiskImage image(4, 1);
    Prepare(fdc, image, 0, 1000);
    fdc._trackRegister = 3;

    const size_t elapsed = Verify(fdc, 1000);
    const size_t fifthIndex = (ROTATION - (1000 + SETTLE) % ROTATION) + 4 * ROTATION;
    EXPECT_NEAR(static_cast<double>(elapsed), static_cast<double>(SETTLE + fifthIndex), 1000.0);
    EXPECT_TRUE(fdc.getStatusRegister() & WD1793::WDS_SEEKERR);
    EXPECT_FALSE(fdc.getStatusRegister() & WD1793::WDS_CRCERR);
}

/// Copy protection: the IDs of physical track 2 say C = 7. Verify with TR = 2 fails, as on hardware
TEST_F(WD1793Verify_Test, ProtectedTrackWithForeignCylinder_SeekError)
{
    WD1793CUT fdc(_context);
    DiskImage image(4, 1);
    DiskImage::Track* track = image.getTrack(2);
    for (DiskImage::Sector& sector : track->sectors())
    {
        sector.id->cylinder = 7;
        sector.id->recalculateCRC();
    }
    track->reindex();
    Prepare(fdc, image, 2);

    Verify(fdc, 1000);
    EXPECT_TRUE(fdc.getStatusRegister() & WD1793::WDS_SEEKERR);

    // TR = 7 matches what the IDs say: verify passes
    WD1793CUT fdc2(_context);
    Prepare(fdc2, image, 2);
    fdc2._trackRegister = 7;
    Verify(fdc2, 100);
    EXPECT_FALSE(fdc2.getStatusRegister() & WD1793::WDS_SEEKERR);
}

TEST_F(WD1793Verify_Test, CrcErrors_SetCrcAndKeepSearching)
{
    // Every matching ID has a bad CRC: CRC ERROR + Seek Error
    {
        WD1793CUT fdc(_context);
        DiskImage image(1, 1);
        DiskImage::Track* track = image.getTrack(0);
        for (DiskImage::Sector& sector : track->sectors())
            sector.id->id_crc ^= 0xFFFF;
        track->reindex();
        Prepare(fdc, image, 0);

        Verify(fdc, 1000);
        EXPECT_TRUE(fdc.getStatusRegister() & WD1793::WDS_SEEKERR);
        EXPECT_TRUE(fdc.getStatusRegister() & WD1793::WDS_CRCERR);
    }
    // One bad ID among good ones: the search continues and succeeds, CRC ERROR reset at the end
    {
        WD1793CUT fdc(_context);
        DiskImage image(1, 1);
        DiskImage::Track* track = image.getTrack(0);
        track->sectors()[0].id->id_crc ^= 0xFFFF;
        track->reindex();
        Prepare(fdc, image, 0);

        Verify(fdc, 100);
        EXPECT_FALSE(fdc.getStatusRegister() & WD1793::WDS_SEEKERR);
        EXPECT_FALSE(fdc.getStatusRegister() & WD1793::WDS_CRCERR);
    }
}

/// Verify compares the track only: IDs with H = 1 on side 0 still pass
TEST_F(WD1793Verify_Test, SideIsNotCompared)
{
    WD1793CUT fdc(_context);
    DiskImage image(1, 1);
    DiskImage::Track* track = image.getTrack(0);
    for (DiskImage::Sector& sector : track->sectors())
    {
        sector.id->head = 1;
        sector.id->recalculateCRC();
    }
    track->reindex();
    Prepare(fdc, image, 0);

    Verify(fdc, 100);
    EXPECT_FALSE(fdc.getStatusRegister() & WD1793::WDS_SEEKERR);
}

/// 40-track disk in an 80-track drive: TR-DOS keeps TR = n with the head at 2n; the IDs there say n
TEST_F(WD1793Verify_Test, FortyTrackDiskIn80TrackDrive)
{
    DiskImage image(40, 1);
    image.setFortyTrack(true);

    WD1793CUT fdc(_context);
    Prepare(fdc, image, 0);
    fdc._selectedDrive->setTrack(10);  // head at 10 = disk track 5
    fdc._trackRegister = 5;
    Verify(fdc, 100);
    EXPECT_FALSE(fdc.getStatusRegister() & WD1793::WDS_SEEKERR) << "head 2n reads disk track n, ID C = n";

    WD1793CUT fdc2(_context);
    Prepare(fdc2, image, 0);
    fdc2._selectedDrive->setTrack(11);  // between two 48 tpi tracks: nothing readable
    fdc2._trackRegister = 5;
    Verify(fdc2, 1000);
    EXPECT_TRUE(fdc2.getStatusRegister() & WD1793::WDS_SEEKERR);
}

/// FM host setting on an MFM disk: no ID field is recognised
TEST_F(WD1793Verify_Test, EncodingMismatch_SeekError)
{
    WD1793CUT fdc(_context);
    DiskImage image(1, 1);
    Prepare(fdc, image, 0);
    fdc._beta128Register |= WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_DENSITY;  // FM

    Verify(fdc, 1000);
    EXPECT_TRUE(fdc.getStatusRegister() & WD1793::WDS_SEEKERR);
}

/// endregion </Type I verify>

/// region <READ TRACK at a mismatched rate>

TEST_F(WD1793Clock_Test, ReadTrack_RateMismatch_ReturnsDeterministicNoise)
{
    auto readTrack = [this](DiskImage& image) {
        WD1793CUT fdc(_context);
        Prepare(fdc, image, 0, ROTATION - 2000);
        Issue(fdc, 0xE0);
        std::vector<uint8_t> bytes;
        for (size_t clk = fdc._time; clk < fdc._time + ROTATION * 2; clk += 20)
        {
            fdc._time = clk;
            fdc.process();
            if (fdc._beta128status & WD1793::DRQ)
                bytes.push_back(fdc.readDataRegister());
            if (fdc._state == WD1793::S_IDLE)
                break;
        }
        EXPECT_EQ(fdc._state, WD1793::S_IDLE);
        EXPECT_EQ(fdc._tstatesPerByte, 112u) << "bytes assembled at the 250 kbit/s separator rate";
        fdc.getDrive()->ejectDisk();
        return bytes;
    };

    DiskImage image(1, 1);
    DiskImage::Track* track = image.getTrack(0);
    FormatHD(track, 0);

    const std::vector<uint8_t> first = readTrack(image);
    const std::vector<uint8_t> second = readTrack(image);
    ASSERT_EQ(first.size(), 6250u) << "one revolution at 250 kbit/s MFM";
    EXPECT_EQ(first, second) << "deterministic";
    EXPECT_FALSE(std::equal(first.begin(), first.end(), track->rawData())) << "not the HD stream";
    EXPECT_EQ(std::count(first.begin(), first.end(), 0xA1), 0);
    EXPECT_EQ(std::count(first.begin(), first.end(), 0xFE), 0);
    EXPECT_GT(std::set<uint8_t>(first.begin(), first.end()).size(), 200u) << "noise, not a fill pattern";
}

/// endregion </READ TRACK at a mismatched rate>

/// region <Machine wiring>

/// Boots real machines to check the board policy reaches the controller (~2 emulator inits, beyond the 50 ms
/// budget but the only way to cover Core::Init wiring)
TEST_F(WD1793Clock_Test, Machines_ZxEvoHasTurboVg_OthersFixed1MHz)
{
    struct Case { const char* model; FdcClockPolicy policy; };
    const Case cases[] = {{"ATM3", FdcClockPolicy::AutoStepTurbo}, {"PENTAGON", FdcClockPolicy::Fixed1MHz}};
    for (const Case& c : cases)
    {
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(c.model);
        ASSERT_NE(emulator, nullptr) << c.model;
        WD1793* fdc = emulator->GetContext()->pBetaDisk;
        ASSERT_NE(fdc, nullptr) << c.model;
        EXPECT_EQ(fdc->GetClockPolicy(), c.policy) << c.model;
        EXPECT_EQ(fdc->GetClock(), FdcClock::Clock1MHz) << c.model;
        EXPECT_EQ(fdc->GetDataRate(), FdcDataRate::Rate250Kbps) << c.model;
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
}

/// endregion </Machine wiring>
