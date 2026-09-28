#include "emulator/io/fdc/upd765.h"

#include <gtest/gtest.h>

#include <vector>

#include "_helpers/romeditortesthelper.h"
#include "3rdparty/message-center/messagecenter.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"
#include "debugger/analyzers/basic-lang/commandtyper.h"
#include "emulator/io/fdc/fdd.h"
#include "_helpers/testpathhelper.h"

/// uPD765A (+3 floppy controller) driven directly: the test owns the clock (UPD765CUT) and jumps it from
/// deadline to deadline, so a whole-track transfer takes a few thousand loop iterations and no emulated frames.
/// Disks are +3 images built in memory (TrackFormatSpec::plus3: 9 x 512, sectors 1-9).
class UPD765_Test : public ::testing::Test
{
protected:
    static constexpr uint8_t RQM = UPD765::MSR_RQM;
    static constexpr uint8_t DIO = UPD765::MSR_DIO;
    static constexpr uint8_t EXM = UPD765::MSR_EXM;
    static constexpr uint8_t CB = UPD765::MSR_CB;

    static constexpr uint8_t MFM = UPD765::CMD_FLAG_MF;
    static constexpr uint8_t READ_DATA = UPD765::CMD_READ_DATA | MFM;
    static constexpr uint8_t WRITE_DATA = UPD765::CMD_WRITE_DATA | MFM;

    EmulatorContext* _context = nullptr;
    UPD765CUT* _fdc = nullptr;
    FDD* _drives[UPD765::DRIVES] = {};
    DiskImage* _disk = nullptr;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _context->emulatorState.base_z80_frequency = 3'500'000;
        for (uint8_t i = 0; i < UPD765::DRIVES; i++)
        {
            _drives[i] = new FDD(_context);
            _context->coreState.diskDrives[i] = _drives[i];
        }

        _fdc = new UPD765CUT(_context);
        _fdc->_time = 1000;
    }

    void TearDown() override
    {
        delete _fdc;
        for (FDD*& drive : _drives)
        {
            delete drive;
            drive = nullptr;
        }
        delete _disk;
        delete _context;
        MessageCenter::DisposeDefaultMessageCenter();
    }

    /// Blank +3 disk in drive A, motor on, ready-change interrupts drained
    void InsertPlus3Disk(uint8_t sides = 1)
    {
        _disk = new DiskImage(40, sides, DiskImage::TrackFormatSpec::plus3());
        _drives[0]->insertDisk(_disk);
        _fdc->setMotor(true);
        DrainInterrupts();
    }

    DiskImage::Track* Track0() { return _disk->getTrackForCylinderAndSide(0, 0); }

    DiskImage::Sector* SectorOf(uint8_t number) { return Track0()->findSector(number); }

    /// Distinct bytes per sector so a wrong sector cannot pass
    void FillSector(uint8_t number)
    {
        std::vector<uint8_t> pattern(512);
        for (size_t i = 0; i < pattern.size(); i++)
            pattern[i] = static_cast<uint8_t>(i * 7 + number * 31);
        Track0()->writeSectorData(static_cast<uint8_t>(number - 1), pattern.data(), pattern.size());
    }

    std::vector<uint8_t> SectorBytes(uint8_t number)
    {
        DiskImage::Sector* sector = SectorOf(number);
        return std::vector<uint8_t>(sector->data, sector->data + sector->dataSize);
    }

    void Command(std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t byte : bytes)
        {
            ASSERT_EQ(_fdc->readMainStatus() & (RQM | DIO), RQM) << "FDC not ready for a command byte";
            _fdc->writeData(byte);
        }
    }

    std::vector<uint8_t> Result()
    {
        std::vector<uint8_t> bytes;
        while ((_fdc->readMainStatus() & (RQM | DIO | CB)) == (RQM | DIO | CB) && bytes.size() < 16)
            bytes.push_back(_fdc->readData());
        return bytes;
    }

    void DrainInterrupts()
    {
        for (int i = 0; i < 8; i++)
        {
            Command({ UPD765::CMD_SENSE_INTERRUPT_STATUS });
            if (Result()[0] == UPD765::ST0_IC_INVALID)
                return;
        }
        FAIL() << "SENSE INTERRUPT STATUS never ran dry";
    }

    /// A CPU fast enough for every byte: jump to each deadline and serve the request at once.
    /// @param source Bytes to write when the FDC asks for them (write / format)
    std::vector<uint8_t> Transfer(const std::vector<uint8_t>& source = {})
    {
        std::vector<uint8_t> read;
        size_t written = 0;
        while (_fdc->getPhase() == UPD765::PHASE_EXECUTION)
        {
            if (_fdc->_time < _fdc->_eventTime)
                _fdc->_time = _fdc->_eventTime;

            const uint8_t msr = _fdc->readMainStatus();
            if ((msr & (RQM | DIO | EXM)) == (RQM | DIO | EXM))
                read.push_back(_fdc->readData());
            else if ((msr & (RQM | DIO | EXM)) == (RQM | EXM))
                _fdc->writeData(written < source.size() ? source[written++] : 0x00);
        }
        return read;
    }

    void Read(uint8_t r, uint8_t eot, uint8_t c = 0, uint8_t code = READ_DATA)
    {
        Command({ code, 0x00, c, 0x00, r, 0x02, eot, 0x2A, 0xFF });
    }
};

/// region <Command phase>

TEST_F(UPD765_Test, IdleWaitsForACommandByte)
{
    EXPECT_EQ(_fdc->readMainStatus(), RQM);
}

TEST_F(UPD765_Test, CommandBusyAfterTheFirstByte)
{
    _fdc->writeData(UPD765::CMD_SPECIFY);
    EXPECT_EQ(_fdc->readMainStatus(), RQM | CB);
}

TEST_F(UPD765_Test, SpecifyTakesTimesAndHasNoResult)
{
    Command({ UPD765::CMD_SPECIFY, 0xAF, 0x03 });

    EXPECT_EQ(_fdc->readMainStatus(), RQM);
    EXPECT_EQ(_fdc->_stepRateTime, 0x0A);
    EXPECT_EQ(_fdc->_headLoadTime, 0x01);
    EXPECT_EQ(_fdc->stepTStates(), 6u * 2u * 3500u);  // (16 - 10) x 2 ms
    EXPECT_EQ(_fdc->headLoadTStates(), 4u * 3500u);   // 1 x 4 ms
}

TEST_F(UPD765_Test, InvalidCommandAnswersST0_80)
{
    Command({ 0x1F });

    EXPECT_EQ(_fdc->readMainStatus(), RQM | DIO | CB);
    EXPECT_EQ(Result(), std::vector<uint8_t>({ 0x80 }));
    EXPECT_EQ(_fdc->readMainStatus(), RQM);
}

TEST_F(UPD765_Test, SenseInterruptWithNothingPendingIsInvalid)
{
    Command({ UPD765::CMD_SENSE_INTERRUPT_STATUS });
    EXPECT_EQ(Result(), std::vector<uint8_t>({ 0x80 }));
}

TEST_F(UPD765_Test, MotorOnReportsTheReadyChange)
{
    _disk = new DiskImage(40, 1, DiskImage::TrackFormatSpec::plus3());
    _drives[0]->insertDisk(_disk);

    _fdc->setMotor(true);
    EXPECT_TRUE(_drives[0]->getMotor());
    EXPECT_TRUE(_drives[1]->getMotor());

    // Drive A became ready; drive B has no disk and stays not ready
    Command({ UPD765::CMD_SENSE_INTERRUPT_STATUS });
    EXPECT_EQ(Result(), std::vector<uint8_t>({ 0xC0, 0x00 }));
    Command({ UPD765::CMD_SENSE_INTERRUPT_STATUS });
    EXPECT_EQ(Result(), std::vector<uint8_t>({ 0x80 }));
}

/// endregion </Command phase>

/// region <Drive commands>

TEST_F(UPD765_Test, SenseDriveStatusAliasesUnit2ToDriveA)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());

    Command({ UPD765::CMD_SENSE_DRIVE_STATUS, 0x02 });
    EXPECT_EQ(Result(), std::vector<uint8_t>({ UPD765::ST3_RY | UPD765::ST3_T0 | 0x02 }));

    _drives[0]->setWriteProtect(true);
    Command({ UPD765::CMD_SENSE_DRIVE_STATUS, 0x00 });
    EXPECT_EQ(Result(), std::vector<uint8_t>({ UPD765::ST3_RY | UPD765::ST3_T0 | UPD765::ST3_WP }));
}

TEST_F(UPD765_Test, SeekStepsAtTheSpecifiedRate)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());
    Command({ UPD765::CMD_SPECIFY, 0xAF, 0x03 });

    const uint64_t start = _fdc->_time;
    Command({ UPD765::CMD_SEEK, 0x00, 5 });

    // Stepping: drive A busy, the command phase free
    EXPECT_EQ(_fdc->readMainStatus(), RQM | UPD765::MSR_D0B);

    _fdc->_time = start + 5 * _fdc->stepTStates() - 1;
    Command({ UPD765::CMD_SENSE_INTERRUPT_STATUS });
    EXPECT_EQ(Result(), std::vector<uint8_t>({ 0x80 }));
    EXPECT_EQ(_drives[0]->getTrack(), 0);

    _fdc->_time = start + 5 * _fdc->stepTStates();
    Command({ UPD765::CMD_SENSE_INTERRUPT_STATUS });
    EXPECT_EQ(Result(), std::vector<uint8_t>({ UPD765::ST0_SE, 5 }));
    EXPECT_EQ(_drives[0]->getTrack(), 5);
    EXPECT_EQ(_fdc->readMainStatus(), RQM);
}

TEST_F(UPD765_Test, RecalibrateReturnsToTrack0)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());
    _drives[0]->setTrack(10);

    Command({ UPD765::CMD_RECALIBRATE, 0x00 });
    _fdc->_time += 10 * _fdc->stepTStates();
    Command({ UPD765::CMD_SENSE_INTERRUPT_STATUS });

    EXPECT_EQ(Result(), std::vector<uint8_t>({ UPD765::ST0_SE, 0 }));
    EXPECT_TRUE(_drives[0]->isTrack00());
}

TEST_F(UPD765_Test, RecalibrateGivesUpAfter77Steps)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());
    _drives[0]->setTrack(80);

    Command({ UPD765::CMD_RECALIBRATE, 0x00 });
    _fdc->_time += 77 * _fdc->stepTStates();
    Command({ UPD765::CMD_SENSE_INTERRUPT_STATUS });

    const std::vector<uint8_t> result = Result();
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], UPD765::ST0_IC_ABNORMAL | UPD765::ST0_SE | UPD765::ST0_EC);
    EXPECT_EQ(_drives[0]->getTrack(), 3);
}

TEST_F(UPD765_Test, SeekWithoutADiskIsNotReady)
{
    _fdc->setMotor(true);
    Command({ UPD765::CMD_SEEK, 0x01, 5 });
    Command({ UPD765::CMD_SENSE_INTERRUPT_STATUS });

    const std::vector<uint8_t> result = Result();
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], UPD765::ST0_IC_ABNORMAL | UPD765::ST0_SE | UPD765::ST0_NR | 0x01);
}

/// endregion </Drive commands>

/// region <READ ID / READ DATA>

TEST_F(UPD765_Test, ReadDataWithTheMotorOffIsNotReady)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());
    _fdc->setMotor(false);
    DrainInterrupts();

    Read(1, 1);

    // No execution phase: the result is there at once
    EXPECT_EQ(Result(), std::vector<uint8_t>({ UPD765::ST0_IC_ABNORMAL | UPD765::ST0_NR, 0, 0, 0, 0, 1, 2 }));
}

TEST_F(UPD765_Test, ReadIdFollowsTheRotation)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());

    Command({ UPD765::CMD_READ_ID | MFM, 0x00 });
    EXPECT_EQ(_fdc->readMainStatus(), CB | EXM);  // Head loading: no byte yet
    Transfer();
    const std::vector<uint8_t> first = Result();
    ASSERT_EQ(first.size(), 7u);
    EXPECT_EQ(first[0], 0x00);
    EXPECT_EQ(first[6], 0x02);

    Command({ UPD765::CMD_READ_ID | MFM, 0x00 });
    Transfer();
    const std::vector<uint8_t> second = Result();
    ASSERT_EQ(second.size(), 7u);

    // The head load takes 4 ms, less than a sector: the next ID is the next sector or the one after it
    const int step = (second[5] + 9 - first[5]) % 9;
    EXPECT_GE(step, 1);
    EXPECT_LE(step, 2);
}

TEST_F(UPD765_Test, ReadDataSingleSectorEndsWithEndOfCylinder)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());
    FillSector(3);

    Read(3, 3);
    EXPECT_EQ(Transfer(), SectorBytes(3));

    // TC is tied low on the +3: IC = 01 + EN, the last sector's C H R N
    EXPECT_EQ(Result(), std::vector<uint8_t>({ UPD765::ST0_IC_ABNORMAL, UPD765::ST1_EN, 0, 0, 0, 3, 2 }));
}

TEST_F(UPD765_Test, ReadDataMultiSectorRunsToEot)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());
    std::vector<uint8_t> expected;
    for (uint8_t r = 1; r <= 3; r++)
    {
        FillSector(r);
        const std::vector<uint8_t> bytes = SectorBytes(r);
        expected.insert(expected.end(), bytes.begin(), bytes.end());
    }

    Read(1, 3);
    EXPECT_EQ(Transfer(), expected);
    EXPECT_EQ(Result(), std::vector<uint8_t>({ UPD765::ST0_IC_ABNORMAL, UPD765::ST1_EN, 0, 0, 0, 3, 2 }));
}

TEST_F(UPD765_Test, ReadDataMultiTrackContinuesOnHead1)
{
    InsertPlus3Disk(2);
    ASSERT_FALSE(HasFatalFailure());

    Command({ READ_DATA | UPD765::CMD_FLAG_MT, 0x00, 0, 0, 9, 2, 9, 0x2A, 0xFF });
    EXPECT_EQ(Transfer().size(), 10u * 512u);  // side 0 sector 9, side 1 sectors 1-9

    const std::vector<uint8_t> result = Result();
    ASSERT_EQ(result.size(), 7u);
    EXPECT_EQ(result[0], UPD765::ST0_IC_ABNORMAL | UPD765::ST0_HD);
    EXPECT_EQ(result[1], UPD765::ST1_EN);
}

TEST_F(UPD765_Test, SectorNotFoundAfterTwoIndexPulses)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());
    const uint64_t start = _fdc->_time;

    Read(10, 10);
    EXPECT_TRUE(Transfer().empty());
    EXPECT_GE(_fdc->_time, start + _fdc->rotationTStates());

    EXPECT_EQ(Result(), std::vector<uint8_t>({ UPD765::ST0_IC_ABNORMAL, UPD765::ST1_ND, 0, 0, 0, 10, 2 }));
}

TEST_F(UPD765_Test, WrongCylinderIsReported)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());

    Read(1, 1, 5);
    Transfer();

    const std::vector<uint8_t> result = Result();
    ASSERT_EQ(result.size(), 7u);
    EXPECT_EQ(result[1], UPD765::ST1_ND);
    EXPECT_EQ(result[2], UPD765::ST2_WC);
}

TEST_F(UPD765_Test, SizeCodeMustMatch)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());

    Command({ READ_DATA, 0x00, 0, 0, 1, 3, 1, 0x2A, 0xFF });
    Transfer();
    EXPECT_EQ(Result()[1], UPD765::ST1_ND);
}

TEST_F(UPD765_Test, FmCommandFindsNoAddressMarkOnAnMfmTrack)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());

    Read(1, 1, 0, UPD765::CMD_READ_DATA);
    Transfer();
    EXPECT_EQ(Result()[1], UPD765::ST1_MA);
}

TEST_F(UPD765_Test, BadIdCrcIsADataError)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());
    SectorOf(2)->id->id_crc ^= 0x0101;
    ASSERT_FALSE(SectorOf(2)->isIDCRCValid());

    Read(2, 2);
    EXPECT_TRUE(Transfer().empty());
    EXPECT_EQ(Result(), std::vector<uint8_t>({ UPD765::ST0_IC_ABNORMAL, UPD765::ST1_DE, 0, 0, 0, 2, 2 }));
}

TEST_F(UPD765_Test, BadDataCrcDeliversTheDataThenStops)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());
    FillSector(2);
    SectorOf(2)->data[0] ^= 0xFF;
    ASSERT_FALSE(SectorOf(2)->isDataCRCValid());

    Read(2, 3);
    EXPECT_EQ(Transfer(), SectorBytes(2));
    EXPECT_EQ(Result(),
              std::vector<uint8_t>({ UPD765::ST0_IC_ABNORMAL, UPD765::ST1_DE, UPD765::ST2_DD, 0, 0, 2, 2 }));
}

TEST_F(UPD765_Test, IdWithoutDataFieldIsMissingDataMark)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());
    DiskImage::TrackFormatSpec spec = DiskImage::TrackFormatSpec::plus3();
    spec.idOnly = { 0, 0, 1 };
    Track0()->formatTrack(0, 0, spec);

    Read(3, 3);
    EXPECT_TRUE(Transfer().empty());
    EXPECT_EQ(Result(),
              std::vector<uint8_t>({ UPD765::ST0_IC_ABNORMAL, UPD765::ST1_MA, UPD765::ST2_MD, 0, 0, 3, 2 }));
}

TEST_F(UPD765_Test, DeletedSectorWithoutSkipIsReadAndEndsTheCommand)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());
    FillSector(2);
    SectorOf(2)->setDataAddressMark(0xF8);
    SectorOf(2)->recalculateDataCRC();

    Read(2, 3);
    EXPECT_EQ(Transfer(), SectorBytes(2));

    // CM alone is a normal end: IC = 00, no EN
    EXPECT_EQ(Result(), std::vector<uint8_t>({ 0x00, 0x00, UPD765::ST2_CM, 0, 0, 2, 2 }));
}

TEST_F(UPD765_Test, DeletedSectorWithSkipIsPassedOver)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());
    FillSector(2);
    FillSector(3);
    SectorOf(2)->setDataAddressMark(0xF8);
    SectorOf(2)->recalculateDataCRC();

    Read(2, 3, 0, READ_DATA | UPD765::CMD_FLAG_SK);
    EXPECT_EQ(Transfer(), SectorBytes(3));
    EXPECT_EQ(Result(), std::vector<uint8_t>({ 0x00, 0x00, UPD765::ST2_CM, 0, 0, 3, 2 }));
}

TEST_F(UPD765_Test, ReadDeletedDataReadsTheDeletedSector)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());
    FillSector(4);
    SectorOf(4)->setDataAddressMark(0xF8);
    SectorOf(4)->recalculateDataCRC();

    Read(4, 4, 0, UPD765::CMD_READ_DELETED_DATA | MFM);
    EXPECT_EQ(Transfer(), SectorBytes(4));
    EXPECT_EQ(Result(), std::vector<uint8_t>({ UPD765::ST0_IC_ABNORMAL, UPD765::ST1_EN, 0, 0, 0, 4, 2 }));
}

TEST_F(UPD765_Test, ByteNotTakenInTimeIsAnOverrun)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());

    Read(1, 1);
    const size_t cell = _fdc->byteCellTStates(Track0());

    // Take the first byte, then let the second one sit for a whole byte cell
    while (!(_fdc->readMainStatus() & RQM))
        _fdc->_time = _fdc->_eventTime;
    _fdc->readData();
    while (!(_fdc->readMainStatus() & RQM))
        _fdc->_time = _fdc->_eventTime;
    _fdc->_time += cell;

    EXPECT_EQ(_fdc->readMainStatus() & (RQM | DIO | CB | EXM), RQM | DIO | CB);
    const std::vector<uint8_t> result = Result();
    ASSERT_EQ(result.size(), 7u);
    EXPECT_EQ(result[0], UPD765::ST0_IC_ABNORMAL);
    EXPECT_EQ(result[1], UPD765::ST1_OR);
}

TEST_F(UPD765_Test, ByteTakenJustBeforeTheNextCellIsNoOverrun)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());
    FillSector(1);

    Read(1, 1);
    const size_t cell = _fdc->byteCellTStates(Track0());

    // A slow CPU: every byte taken one T-state before the next one arrives
    std::vector<uint8_t> read;
    while (_fdc->getPhase() == UPD765::PHASE_EXECUTION)
    {
        _fdc->_time = std::max(_fdc->_time, _fdc->_eventTime);
        if ((_fdc->readMainStatus() & (RQM | EXM)) == (RQM | EXM))
        {
            _fdc->_time += cell - 1;
            read.push_back(_fdc->readData());
        }
    }

    EXPECT_EQ(read, SectorBytes(1));
    EXPECT_EQ(Result()[1], UPD765::ST1_EN);
}

TEST_F(UPD765_Test, WeakBytesReadDifferentlyOnEachPass)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());
    FillSector(5);
    DiskImage::Sector* sector = SectorOf(5);
    for (size_t i = 100; i < 116; i++)
        Track0()->setWeakByte(sector->dataOffset + i, true);

    Read(5, 5);
    const std::vector<uint8_t> first = Transfer();
    Result();
    _fdc->_time += 12345;  // Another revolution phase
    Read(5, 5);
    const std::vector<uint8_t> second = Transfer();
    Result();

    const std::vector<uint8_t> stored = SectorBytes(5);
    ASSERT_EQ(first.size(), stored.size());
    ASSERT_EQ(second.size(), stored.size());

    size_t weakDiffers = 0;
    for (size_t i = 0; i < stored.size(); i++)
    {
        const bool weak = (i >= 100 && i < 116);
        if (!weak)
        {
            ASSERT_EQ(first[i], stored[i]) << "solid byte " << i;
            ASSERT_EQ(second[i], stored[i]) << "solid byte " << i;
        }
        else if (first[i] != second[i])
        {
            weakDiffers++;
        }
    }
    EXPECT_GT(weakDiffers, 8u);
}

/// endregion </READ ID / READ DATA>

/// region <WRITE DATA / FORMAT>

TEST_F(UPD765_Test, WriteDataStoresTheSectorWithAValidCrc)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());
    std::vector<uint8_t> pattern(512);
    for (size_t i = 0; i < pattern.size(); i++)
        pattern[i] = static_cast<uint8_t>(0xA5 ^ i);

    Command({ WRITE_DATA, 0x00, 0, 0, 4, 2, 4, 0x2A, 0xFF });
    Transfer(pattern);

    EXPECT_EQ(Result(), std::vector<uint8_t>({ UPD765::ST0_IC_ABNORMAL, UPD765::ST1_EN, 0, 0, 0, 4, 2 }));
    EXPECT_EQ(SectorBytes(4), pattern);
    EXPECT_TRUE(SectorOf(4)->isDataCRCValid());
    EXPECT_FALSE(SectorOf(4)->deleted);
    EXPECT_TRUE(Track0()->isDirty());
    EXPECT_TRUE(_disk->isDirty());
}

TEST_F(UPD765_Test, WriteDeletedDataSetsTheDeletedMark)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());

    Command({ UPD765::CMD_WRITE_DELETED_DATA | MFM, 0x00, 0, 0, 6, 2, 6, 0x2A, 0xFF });
    Transfer(std::vector<uint8_t>(512, 0x11));
    Result();

    EXPECT_TRUE(SectorOf(6)->deleted);
    EXPECT_TRUE(SectorOf(6)->isDataCRCValid());
}

TEST_F(UPD765_Test, WriteToAProtectedDiskIsRefused)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());
    _drives[0]->setWriteProtect(true);
    const std::vector<uint8_t> before = SectorBytes(1);

    Command({ WRITE_DATA, 0x00, 0, 0, 1, 2, 1, 0x2A, 0xFF });
    EXPECT_EQ(Result(), std::vector<uint8_t>({ UPD765::ST0_IC_ABNORMAL, UPD765::ST1_NW, 0, 0, 0, 1, 2 }));
    EXPECT_EQ(SectorBytes(1), before);
}

TEST_F(UPD765_Test, FormatTrackWritesTheGivenIds)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());

    // CPC data format: 9 x 512, sectors #C1-#C9, filler #E5
    std::vector<uint8_t> ids;
    for (uint8_t r = 0xC1; r <= 0xC9; r++)
        ids.insert(ids.end(), { 0, 0, r, 2 });

    Command({ UPD765::CMD_FORMAT_TRACK | MFM, 0x00, 2, 9, 0x52, 0xE5 });
    Transfer(ids);

    EXPECT_EQ(Result(), std::vector<uint8_t>({ 0x00, 0x00, 0x00, 0, 0, 0xC9, 2 }));
    DiskImage::Track* track = Track0();
    ASSERT_EQ(track->sectorCount(), 9u);
    for (size_t i = 0; i < 9; i++)
    {
        const DiskImage::Sector& sector = track->sectors()[i];
        EXPECT_EQ(sector.number(), 0xC1 + i);
        EXPECT_EQ(sector.sizeCode(), 2);
        EXPECT_TRUE(sector.idCrcValid);
        EXPECT_TRUE(sector.dataCrcValid);
        EXPECT_EQ(sector.data[0], 0xE5);
    }
    EXPECT_TRUE(track->isRawTrackDirty());
}

/// endregion </WRITE DATA / FORMAT>

/// region <TTD>

TEST_F(UPD765_Test, RestoredControllerContinuesTheSameTransfer)
{
    InsertPlus3Disk();
    ASSERT_FALSE(HasFatalFailure());
    FillSector(7);

    Read(7, 7);
    std::vector<uint8_t> head;
    while (head.size() < 100)
    {
        _fdc->_time = std::max(_fdc->_time, _fdc->_eventTime);
        if ((_fdc->readMainStatus() & (RQM | EXM)) == (RQM | EXM))
            head.push_back(_fdc->readData());
    }

    std::vector<uint8_t> blob(_fdc->TTDStateSize());
    _fdc->TTDSaveState(blob.data());
    const uint64_t hash = _fdc->TTDHashState();

    const std::vector<uint8_t> tailOriginal = Transfer();
    const std::vector<uint8_t> resultOriginal = Result();

    UPD765CUT restored(_context);
    restored.TTDLoadState(blob.data());
    EXPECT_EQ(restored.TTDHashState(), hash);
    UPD765CUT* original = _fdc;
    _fdc = &restored;  // Drive the restored instance through the same helpers
    const std::vector<uint8_t> tailRestored = Transfer();
    const std::vector<uint8_t> resultRestored = Result();
    _fdc = original;

    EXPECT_EQ(tailRestored, tailOriginal);
    EXPECT_EQ(resultRestored, resultOriginal);

    std::vector<uint8_t> whole = head;
    whole.insert(whole.end(), tailOriginal.begin(), tailOriginal.end());
    EXPECT_EQ(whole, SectorBytes(7));
}

/// endregion </TTD>

/// region <+3 ROM>

/// The real +3 ROM on the whole machine, disk commands typed through the verified command typer. These boot
/// the ROM and let +3DOS spin the drive up and turn the disk at its real speed: the only tests in this file over
/// the 50 ms budget
class UPD765Rom_Test : public RomEditorFixture
{
protected:
    CommandTyper* _typer = nullptr;

    /// +3 BASIC with a blank +3DOS disk in drive A
    void Ready()
    {
        BootEditor("Plus3-3BASIC");
        ASSERT_FALSE(HasFatalFailure());

        // The automation path (WebAPI / CLI / Lua / Python disk create): a formatted +3DOS disk on a +3
        Emulator::BlankDiskResult created;
        ASSERT_TRUE(_emulator->CreateBlankDisk(0, Emulator::BlankDiskFormat::Auto, 0, 0, nullptr, &created));
        ASSERT_EQ(created.format, Emulator::BlankDiskFormat::Plus3);

        _typer = _context->pDebugManager->GetCommandTyper();
        ASSERT_NE(_typer, nullptr);
        Run("", false);  // BootEditor left a `0` on the line
    }

    CommandTyper::Result Run(const std::string& command, bool enter = true)
    {
        CommandTyper::Options options;
        options.pressEnter = enter;
        options.waitForReport = enter;
        EXPECT_TRUE(_typer->Request(command, options));
        RunUntil([&] { return _typer->GetStatus() == CommandTyper::Status::Done; }, 4000);
        EXPECT_EQ(_typer->GetStatus(), CommandTyper::Status::Done) << "typer still running: " << command;
        return _typer->GetResult();
    }

    static std::string Describe(const CommandTyper::Result& result)
    {
        return std::string(CommandTyper::OutcomeName(result.outcome)) + " err " + std::to_string(result.errNr) +
               " " + result.message;
    }
};

TEST_F(UPD765Rom_Test, MenuShowsThePlus3)
{
    Boot("PLUS3", RM_128, "48 BASIC");
    ASSERT_FALSE(HasFatalFailure());

    // Without a controller the ROM calls itself a +2A and offers no drive A
    EXPECT_TRUE(ScreenHas("128 +3")) << Screen();
    EXPECT_TRUE(ScreenHas("Drives A: and M: available")) << Screen();
}

/// The +2A is the +3 without the controller: same ROM, no uPD765, no drive A
TEST_F(UPD765Rom_Test, Plus2AMenuHasNoDiskDrive)
{
    Boot("PLUS2A", RM_128, "48 BASIC");
    ASSERT_FALSE(HasFatalFailure());

    EXPECT_EQ(_context->pUPD765, nullptr);
    EXPECT_TRUE(ScreenHas("128 +2A")) << Screen();
    EXPECT_TRUE(ScreenHas("Drive M: available")) << Screen();
}

TEST_F(UPD765Rom_Test, CatOfABlankDisk)
{
    Ready();
    ASSERT_FALSE(HasFatalFailure());

    const CommandTyper::Result cat = Run("CAT");
    EXPECT_EQ(cat.outcome, CommandTyper::Outcome::Finished) << Describe(cat) << "\n" << Screen();
    EXPECT_EQ(cat.errNr, 0xFF) << Describe(cat) << "\n" << Screen();
    EXPECT_TRUE(ScreenHas("K free")) << Screen();
}

/// A .dsk image through Emulator::LoadDisk (what the UI, WebAPI and MCP load) reads on the real ROM
TEST_F(UPD765Rom_Test, DskImageLoadsAndCats)
{
    Boot("PLUS3", RM_128, "48 BASIC");
    ASSERT_FALSE(HasFatalFailure());
    Enter128Editor("+3 BASIC");
    ASSERT_FALSE(HasFatalFailure());
    TapUntilTaken(ZXKEY_0, '0');
    ASSERT_FALSE(HasFatalFailure());

    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(TestPathHelper::GetTestDataPath("loaders/dsk/plus3-blank.dsk"), 0, &error))
        << error;
    _typer = _context->pDebugManager->GetCommandTyper();
    ASSERT_NE(_typer, nullptr);
    Run("", false);

    const CommandTyper::Result cat = Run("CAT");
    EXPECT_EQ(cat.outcome, CommandTyper::Outcome::Finished) << Describe(cat) << "\n" << Screen();
    EXPECT_EQ(cat.errNr, 0xFF) << Describe(cat) << "\n" << Screen();
    EXPECT_TRUE(ScreenHas("K free")) << Screen();
}

TEST_F(UPD765Rom_Test, SaveThenLoadRoundTrip)
{
    Ready();
    ASSERT_FALSE(HasFatalFailure());

    EXPECT_EQ(Run("10 PRINT 4242").outcome, CommandTyper::Outcome::Stored);

    const CommandTyper::Result save = Run("SAVE \"prog\"");
    ASSERT_EQ(save.outcome, CommandTyper::Outcome::Finished) << Describe(save) << "\n" << Screen();
    ASSERT_EQ(save.errNr, 0xFF) << Describe(save) << "\n" << Screen();
    EXPECT_TRUE(_context->coreState.diskImages[0]->isDirty());

    // Another program in memory (NEW would leave for the menu on this ROM)
    const CommandTyper::Result edit = Run("10 PRINT 1111");
    EXPECT_EQ(edit.outcome, CommandTyper::Outcome::Stored) << Describe(edit) << "\n" << Screen();

    const CommandTyper::Result load = Run("LOAD \"prog\"");
    ASSERT_EQ(load.outcome, CommandTyper::Outcome::Finished) << Describe(load) << "\n" << Screen();
    ASSERT_EQ(load.errNr, 0xFF) << Describe(load) << "\n" << Screen();

    const CommandTyper::Result run = Run("RUN");
    EXPECT_EQ(run.outcome, CommandTyper::Outcome::Finished) << Describe(run);
    EXPECT_TRUE(ScreenHas("4242")) << Screen();
}

/// endregion </+3 ROM>
