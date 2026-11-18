// WD1772-00 variant of the WD1793 controller (MGT +D). The rules come from the WD177X-00 data sheet
// (docs/WD1793/datasheeets/WD177X-00): step rates 6 / 12 / 2 / 3 ms, head settle 15 ms, spin-up of 6 revolutions
// unless h = 1, motor off after 9 idle revolutions, status bit 7 = motor on, Type I bit 5 = spin-up done, no side
// compare, no READY (Force Interrupt I0 / I1 do nothing). See docs/inprogress/2026-11-15-mgt-plus-d/design.md §4.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "common/modulelogger.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/wd1793.h"

class WD1772_Test : public ::testing::Test
{
protected:
    static constexpr size_t TSTATES_PER_MS = 3500;
    static constexpr size_t ROTATION = WD1793::DISK_ROTATION_PERIOD_TSTATES;  // 700 000 T = 200 ms

    EmulatorContext* _context = nullptr;
    CoreCUT* _core = nullptr;
    Z80* _z80 = nullptr;
    std::array<std::unique_ptr<FDD>, 2> _cardDrives;
    std::unique_ptr<WD1793CUT> _fdc;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _context->pModuleLogger->TurnOffLoggingForAll();
        _context->pModuleLogger->SetLoggingLevel(LoggerLevel::LogError);

        _core = new CoreCUT(_context);
        _z80 = new Z80(_context);
        _core->_z80 = _z80;
        _context->pCore = _core;

        // The card's two drives, owned by the test as a card owns them; lines 2 and 3 have none
        for (auto& drive : _cardDrives)
            drive = std::make_unique<FDD>(_context);
        _fdc = std::make_unique<WD1793CUT>(_context, WdVariant::Wd1772,
                                           std::array<FDD*, 4>{_cardDrives[0].get(), _cardDrives[1].get(), nullptr, nullptr},
                                           "plusd-test");
        _fdc->wakeUp();
        _fdc->_time = 1000;
        _fdc->_lastTime = 1000;
    }

    void TearDown() override
    {
        _fdc.reset();  // the controller goes first, as in the card
        for (auto& drive : _cardDrives)
            drive.reset();
        _core->_z80 = nullptr;
        delete _z80;
        _context->pCore = nullptr;
        delete _core;
        delete _context;
    }

    /// Advance the controller by `tstates` in steps of `step`
    void Advance(size_t tstates, size_t step = 3500)
    {
        const size_t end = _fdc->_time + tstates;
        while (_fdc->_time < end)
        {
            _fdc->_time = std::min(end, _fdc->_time + step);
            _fdc->process();
        }
    }

    /// Run until BUSY drops; the T-states it took, 0 when still busy after `maxTStates`
    size_t RunUntilNotBusy(size_t maxTStates, size_t step = 3500)
    {
        const size_t start = _fdc->_time;
        while (_fdc->_time < start + maxTStates)
        {
            _fdc->_time += step;
            _fdc->process();
            if (!(_fdc->ReadRegister(WD1793::REG_COMMAND_STATUS) & WD1793::WDS_BUSY))
                return _fdc->_time - start;
        }
        return 0;
    }

    /// An MGT-style track: 10 x 512-byte MFM sectors, ID head byte `head`, data = sector number
    static void FormatMgtTrack(DiskImage::Track* track, uint8_t cylinder, uint8_t head)
    {
        DiskImage::TrackFormatSpec spec;
        spec.encoding = DiskImage::Encoding::MFM;
        spec.trackLength = 6250;
        spec.sizeCode = DiskImage::SECTOR_SIZE_512;
        for (uint8_t n = 1; n <= 10; n++)
            spec.sectorNumbers.push_back(n);
        spec.sectorHeads.assign(10, head);
        spec.dataFill = 0xA5;
        track->formatTrack(cylinder, head, spec);
    }
};

TEST_F(WD1772_Test, VariantAndDrives)
{
    EXPECT_TRUE(_fdc->Is1772());
    EXPECT_EQ(_fdc->DriveAt(0), _cardDrives[0].get());
    EXPECT_EQ(_fdc->DriveAt(1), _cardDrives[1].get());
    EXPECT_EQ(_fdc->DriveAt(2), nullptr);
    // The card's controller never touches the Beta 128 drives
    for (FDD* drive : _context->coreState.diskDrives)
        EXPECT_EQ(drive, nullptr);
}

TEST_F(WD1772_Test, StepRatesAndSettle)
{
    // Data sheet, Type I r1 r0: 00 = 6 ms, 01 = 12 ms, 10 = 2 ms, 11 = 3 ms; the clock policy has no effect
    const size_t expectedMs[] = {6, 12, 2, 3};
    for (uint8_t r = 0; r < 4; r++)
        EXPECT_EQ(_fdc->StepPeriodTStates(r), expectedMs[r] * TSTATES_PER_MS) << "r1r0 = " << int(r);
    EXPECT_EQ(_fdc->HeadSettleTStates(), 15 * TSTATES_PER_MS);
}

TEST_F(WD1772_Test, SpinUpBeforeTypeICommand)
{
    DiskImage image(80, 2);
    _cardDrives[0]->insertDisk(&image);
    _cardDrives[0]->setTrack(5);
    _fdc->selectDriveLine(0);

    // STEP OUT, h = 0, r = 2 ms, motor off: MO goes high at once, six revolutions pass before the step
    _fdc->WriteRegister(WD1793::REG_COMMAND_STATUS, 0b0110'0010);
    uint8_t status = _fdc->ReadRegister(WD1793::REG_COMMAND_STATUS);
    EXPECT_TRUE(status & WD1793::WDS_MOTORON_1772);
    EXPECT_TRUE(status & WD1793::WDS_BUSY);
    EXPECT_FALSE(status & WD1793::WDS_SPINUP_1772);

    Advance(5 * ROTATION);
    EXPECT_EQ(_cardDrives[0]->getTrack(), 5u) << "no step during the spin-up";

    const size_t took = RunUntilNotBusy(2 * ROTATION);
    ASSERT_GT(took, 0u);
    EXPECT_EQ(_cardDrives[0]->getTrack(), 4u);
    status = _fdc->ReadRegister(WD1793::REG_COMMAND_STATUS);
    EXPECT_TRUE(status & WD1793::WDS_SPINUP_1772);
    EXPECT_TRUE(status & WD1793::WDS_MOTORON_1772);

    // The motor is now on: the next command has no spin-up
    _fdc->WriteRegister(WD1793::REG_COMMAND_STATUS, 0b0110'0010);
    const size_t second = RunUntilNotBusy(ROTATION, 350);
    ASSERT_GT(second, 0u);
    EXPECT_LT(second, 10 * TSTATES_PER_MS);
    EXPECT_EQ(_cardDrives[0]->getTrack(), 3u);
}

TEST_F(WD1772_Test, SpinUpDisabledByH)
{
    DiskImage image(80, 2);
    _cardDrives[0]->insertDisk(&image);
    _cardDrives[0]->setTrack(5);
    _fdc->selectDriveLine(0);

    // STEP OUT with h = 1: the step comes after one step period, bit 5 stays clear
    _fdc->WriteRegister(WD1793::REG_COMMAND_STATUS, 0b0110'1010);
    const size_t took = RunUntilNotBusy(ROTATION, 350);
    ASSERT_GT(took, 0u);
    EXPECT_LT(took, 10 * TSTATES_PER_MS);
    EXPECT_EQ(_cardDrives[0]->getTrack(), 4u);
    const uint8_t status = _fdc->ReadRegister(WD1793::REG_COMMAND_STATUS);
    EXPECT_TRUE(status & WD1793::WDS_MOTORON_1772);
    EXPECT_FALSE(status & WD1793::WDS_SPINUP_1772);
}

TEST_F(WD1772_Test, MotorOffAfterNineIdleRevolutions)
{
    DiskImage image(80, 2);
    _cardDrives[0]->insertDisk(&image);
    _fdc->selectDriveLine(0);

    _fdc->WriteRegister(WD1793::REG_COMMAND_STATUS, 0b0000'1011);  // RESTORE, h = 1, 3 ms
    ASSERT_GT(RunUntilNotBusy(ROTATION, 350), 0u);
    EXPECT_TRUE(_cardDrives[0]->getMotor());

    Advance(8 * ROTATION);
    EXPECT_TRUE(_cardDrives[0]->getMotor()) << "still within 9 revolutions";
    Advance(ROTATION + ROTATION / 10);
    EXPECT_FALSE(_cardDrives[0]->getMotor());
    EXPECT_FALSE(_fdc->ReadRegister(WD1793::REG_COMMAND_STATUS) & WD1793::WDS_MOTORON_1772);
}

TEST_F(WD1772_Test, MotorTimeoutCountsFromTheEndOfTheCommand)
{
    DiskImage image(80, 2);
    _cardDrives[0]->insertDisk(&image);
    _cardDrives[0]->setTrack(79);
    _fdc->selectDriveLine(0);

    // RESTORE from track 79 at 12 ms a step: about 0.95 s under BUSY
    _fdc->WriteRegister(WD1793::REG_COMMAND_STATUS, 0b0000'1001);  // RESTORE, h = 1, 12 ms
    ASSERT_GT(RunUntilNotBusy(ROTATION * 6), 0u);
    EXPECT_EQ(_cardDrives[0]->getTrack(), 0u);

    // 8 revolutions after the end are about 2.55 s after the start: a timeout counted from the start (9 revolutions,
    // 1.8 s) would have stopped the motor already
    Advance(8 * ROTATION);
    EXPECT_TRUE(_cardDrives[0]->getMotor());
    Advance(ROTATION + ROTATION / 10);
    EXPECT_FALSE(_cardDrives[0]->getMotor());
}

TEST_F(WD1772_Test, ForceInterruptHasNoReadyConditions)
{
    _fdc->WriteRegister(WD1793::REG_COMMAND_STATUS, 0xD3);  // I0 + I1: no READY line on the 1772
    EXPECT_EQ(_fdc->_interruptConditions, 0);
    _fdc->WriteRegister(WD1793::REG_COMMAND_STATUS, 0xD4);  // I2: every index pulse
    EXPECT_EQ(_fdc->_interruptConditions, 0x04);
    _fdc->WriteRegister(WD1793::REG_COMMAND_STATUS, 0xD0);
}

TEST_F(WD1772_Test, NoSideCompare)
{
    DiskImage image(80, 2);
    FormatMgtTrack(image.getTrack(0), 0, 0);
    _cardDrives[0]->insertDisk(&image);
    _cardDrives[0]->setTrack(0);
    _fdc->selectDriveLine(0);
    _fdc->setSide(false);

    _fdc->WriteRegister(WD1793::REG_SECTOR, 3);
    _fdc->WriteRegister(WD1793::REG_TRACK, 0);
    // READ SECTOR with h = 1 and P = 1: on a WD1793 bits 3 / 1 would be S = 1, C = 1 (compare with side 1) and miss
    _fdc->WriteRegister(WD1793::REG_COMMAND_STATUS, 0b1000'1010);

    std::vector<uint8_t> data;
    const size_t start = _fdc->_time;
    uint8_t status = 0;
    while (_fdc->_time < start + 3 * ROTATION)
    {
        _fdc->_time += 32;
        _fdc->process();
        status = _fdc->ReadRegister(WD1793::REG_COMMAND_STATUS);
        if (status & WD1793::WDS_DRQ)
            data.push_back(_fdc->ReadRegister(WD1793::REG_DATA));
        if (!(status & WD1793::WDS_BUSY))
            break;
    }
    EXPECT_FALSE(status & WD1793::WDS_BUSY);
    EXPECT_FALSE(status & WD1793::WDS_NOTFOUND) << "status 0x" << std::hex << int(status);
    ASSERT_EQ(data.size(), 512u);
    EXPECT_EQ(data[0], 0xA5);
}

TEST_F(WD1772_Test, NoDiskReadsAsRecordNotFound)
{
    _fdc->selectDriveLine(1);  // drive 2, empty
    _fdc->WriteRegister(WD1793::REG_COMMAND_STATUS, 0b1000'1000);  // READ SECTOR, h = 1
    ASSERT_GT(RunUntilNotBusy(ROTATION), 0u);
    const uint8_t status = _fdc->ReadRegister(WD1793::REG_COMMAND_STATUS);
    EXPECT_TRUE(status & WD1793::WDS_NOTFOUND);
    EXPECT_TRUE(status & WD1793::WDS_MOTORON_1772) << "bit 7 is MO, never NOT READY";
}

TEST_F(WD1772_Test, NoDriveSelected)
{
    DiskImage image(80, 2);
    _cardDrives[0]->insertDisk(&image);
    _fdc->selectDriveLine(0);
    EXPECT_TRUE(_fdc->getFDDState().diskInserted);

    _fdc->selectDriveLine(-1);
    EXPECT_FALSE(_fdc->getFDDState().diskInserted);
    _fdc->WriteRegister(WD1793::REG_COMMAND_STATUS, 0b1000'1000);
    ASSERT_GT(RunUntilNotBusy(ROTATION), 0u);
    EXPECT_TRUE(_fdc->ReadRegister(WD1793::REG_COMMAND_STATUS) & WD1793::WDS_NOTFOUND);

    _fdc->selectDriveLine(0);
    EXPECT_TRUE(_fdc->getFDDState().diskInserted);
}

TEST_F(WD1772_Test, BetaControllerStaysWd1793)
{
    // The Beta 128 constructor: WD1793, drives in coreState
    WD1793CUT beta(_context);
    EXPECT_FALSE(beta.Is1772());
    EXPECT_EQ(beta.DriveAt(0), _context->coreState.diskDrives[0]);
    EXPECT_EQ(beta.StepPeriodTStates(2), 20 * TSTATES_PER_MS);
    EXPECT_EQ(beta.HeadSettleTStates(), 30 * TSTATES_PER_MS);
    for (FDD*& drive : _context->coreState.diskDrives)
    {
        delete drive;
        drive = nullptr;
    }
}
