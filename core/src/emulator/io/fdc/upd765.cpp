#include "upd765.h"

#include <algorithm>
#include <cstring>

#include "common/modulelogger.h"
#include "emulator/io/fdc/flakysectoremulator.h"

/// region <Constructors / destructors>

UPD765::UPD765(EmulatorContext* context)
{
    _context = context;
    _logger = context->pModuleLogger;

    reset();
}

/// endregion </Constructors / destructors>

/// region <Methods>

void UPD765::reset()
{
    _phase = PHASE_COMMAND;
    _state = S_IDLE;
    _eventTime = 0;

    std::memset(_command, 0, sizeof(_command));
    _commandLength = 0;
    _commandPos = 0;
    std::memset(_result, 0, sizeof(_result));
    _resultLength = 0;
    _resultPos = 0;

    _st0 = _st1 = _st2 = 0;
    _dataRegister = 0xFF;
    _dataReady = false;
    _dataRequested = false;
    _byteIndex = 0;
    _transferSize = 0;
    _sectorIndex = -1;
    _endAfterSector = false;
    _formatStart = 0;
    std::memset(_formatIds, 0, sizeof(_formatIds));

    // +3DOS issues SPECIFY before its first disk command; these are its values
    _stepRateTime = 0x0A;
    _headLoadTime = 0x01;

    for (UnitState& unitState : _units)
    {
        unitState = UnitState();
    }

    // #1FFD resets to 0: motor off
    if (_motorOn)
    {
        for (uint8_t drive = 0; drive < DRIVES; drive++)
        {
            if (FDD* fdd = driveFor(drive))
                fdd->setMotor(false);
        }
    }
    _motorOn = false;
}

void UPD765::process()
{
    updateTimeFromEmulatorState();

    completeSeeks();

    // Replay the deadlines that passed, in time order. Every state either moves _eventTime forward or leaves
    // the execution phase, so the loop ends
    while (_phase == PHASE_EXECUTION && _eventTime <= _time)
    {
        runState();
    }
}

/// endregion </Methods>

/// region <Ports>

uint8_t UPD765::readMainStatus()
{
    process();

    uint8_t msr = 0;
    for (uint8_t unitNumber = 0; unitNumber < UNITS; unitNumber++)
    {
        if (_units[unitNumber].seekBusy)
            msr |= static_cast<uint8_t>(MSR_D0B << unitNumber);
    }

    switch (_phase)
    {
        case PHASE_COMMAND:
            msr |= MSR_RQM;
            if (_commandLength != 0)
                msr |= MSR_CB;
            break;

        case PHASE_EXECUTION:
            msr |= MSR_CB | MSR_EXM;
            if (_dataReady)
                msr |= MSR_RQM | MSR_DIO;
            if (_dataRequested)
                msr |= MSR_RQM;
            break;

        case PHASE_RESULT:
            msr |= MSR_RQM | MSR_DIO | MSR_CB;
            break;
    }

    return msr;
}

uint8_t UPD765::readData()
{
    process();

    if (_phase == PHASE_RESULT)
    {
        const uint8_t value = _result[_resultPos++];
        if (_resultPos >= _resultLength)
        {
            _phase = PHASE_COMMAND;
            _resultLength = 0;
            _resultPos = 0;
        }
        return value;
    }

    if (_phase == PHASE_EXECUTION && _dataReady)
    {
        _dataReady = false;
    }

    return _dataRegister;
}

void UPD765::writeData(uint8_t value)
{
    process();

    switch (_phase)
    {
        case PHASE_COMMAND:
            if (_commandLength == 0)
            {
                _command[0] = value;
                _commandLength = commandLengthFor(value & 0x1F);
                _commandPos = 1;
            }
            else
            {
                _command[_commandPos++] = value;
            }

            if (_commandPos >= _commandLength)
            {
                _commandLength = 0;
                executeCommand();
            }
            break;

        case PHASE_EXECUTION:
            if (!_dataRequested)
                break;
            _dataRequested = false;

            if (_state == S_FORMAT_DUE)
            {
                if (_byteIndex < sizeof(_formatIds))
                    _formatIds[_byteIndex] = value;
                _byteIndex++;
            }
            else if (DiskImage::Sector* sector = currentSector())
            {
                // The byte goes to the disk as it is written; the CRC follows at the end of the sector
                const size_t offset = _byteIndex - 1u;
                if (offset < sector->dataSize)
                {
                    sector->data[offset] = value;
                    currentTrack()->setWeakByte(sector->dataOffset + offset, false);
                }
            }
            break;

        case PHASE_RESULT:
            // The FDC does not take bytes while it has results to give
            break;
    }
}

void UPD765::setMotor(bool on)
{
    if (on == _motorOn)
        return;

    process();

    bool wasReady[DRIVES];
    for (uint8_t drive = 0; drive < DRIVES; drive++)
        wasReady[drive] = isReady(drive);

    _motorOn = on;
    for (uint8_t drive = 0; drive < DRIVES; drive++)
    {
        if (FDD* fdd = driveFor(drive))
            fdd->setMotor(on);
    }

    // The chip polls the ready line of every unit and reports a change through SENSE INTERRUPT STATUS
    for (uint8_t drive = 0; drive < DRIVES; drive++)
    {
        const bool ready = isReady(drive);
        UnitState& unitState = _units[drive];
        if (ready != wasReady[drive] && !unitState.interruptPending)
        {
            unitState.st0 = static_cast<uint8_t>(ST0_IC_READY_CHANGE | (ready ? 0 : ST0_NR) | drive);
            unitState.interruptPending = true;
        }
    }
}

/// endregion </Ports>

/// region <Helper methods>

uint8_t UPD765::commandLengthFor(uint8_t code)
{
    switch (code)
    {
        case CMD_SPECIFY:
        case CMD_SEEK:
            return 3;
        case CMD_SENSE_DRIVE_STATUS:
        case CMD_RECALIBRATE:
        case CMD_READ_ID:
            return 2;
        case CMD_SENSE_INTERRUPT_STATUS:
            return 1;
        case CMD_FORMAT_TRACK:
            return 6;
        case CMD_READ_TRACK:
        case CMD_WRITE_DATA:
        case CMD_READ_DATA:
        case CMD_WRITE_DELETED_DATA:
        case CMD_READ_DELETED_DATA:
        case 0x11:  // SCAN EQUAL
        case 0x19:  // SCAN LOW OR EQUAL
        case 0x1D:  // SCAN HIGH OR EQUAL
            return 9;
        default:
            return 1;  // Invalid: the first byte is the whole command
    }
}

FDD* UPD765::driveFor(uint8_t unitNumber) const
{
    // US1 is not connected: units 2 / 3 select drives 0 / 1
    return _context->coreState.diskDrives[unitNumber & 0x01];
}

bool UPD765::isReady(uint8_t unitNumber) const
{
    FDD* fdd = driveFor(unitNumber);
    return _motorOn && fdd != nullptr && fdd->isDiskInserted() && fdd->getDiskImage() != nullptr;
}

DiskImage::Track* UPD765::currentTrack() const
{
    FDD* fdd = driveFor(unit());
    if (fdd == nullptr || !fdd->isDiskInserted() || fdd->getDiskImage() == nullptr)
        return nullptr;

    return fdd->getDiskImage()->getTrackForCylinderAndSide(static_cast<uint8_t>(fdd->getTrack()), head());
}

DiskImage::Sector* UPD765::currentSector() const
{
    DiskImage::Track* track = currentTrack();
    if (track == nullptr || _sectorIndex < 0 || static_cast<size_t>(_sectorIndex) >= track->sectorCount())
        return nullptr;

    return &track->sectors()[static_cast<size_t>(_sectorIndex)];
}

uint64_t UPD765::tstatesPerMs() const
{
    const uint32_t hz = _context->emulatorState.base_z80_frequency;
    return (hz != 0 ? hz : 3'500'000u) / 1000u;
}

size_t UPD765::headByteOffset(const DiskImage::Track& track, uint64_t time) const
{
    const uint64_t rotation = rotationTStates();
    const uint64_t phase = time % rotation;  // T-states since the last index pulse
    return static_cast<size_t>((phase * track.rawSize()) / rotation);
}

size_t UPD765::byteCellTStates(const DiskImage::Track* track) const
{
    // No track (formatting past the end of the image): the nominal 6250-byte MFM track
    const size_t size = (track != nullptr && track->rawSize() != 0) ? track->rawSize() : MAX_TRACK_LEN;
    return static_cast<size_t>(rotationTStates() / size);
}

uint64_t UPD765::nextIndexPulse(uint64_t time) const
{
    const uint64_t rotation = rotationTStates();
    return (time / rotation + 1) * rotation;
}

uint64_t UPD765::headLoadTStates() const
{
    // HLT counts 4 ms units at the +3's 4 MHz FDC clock; 0 means 128
    const uint64_t units = (_headLoadTime == 0) ? 128u : _headLoadTime;
    return units * 4u * tstatesPerMs();
}

uint64_t UPD765::stepTStates() const
{
    // SRT: (16 - SRT) * 2 ms at the 4 MHz FDC clock
    return static_cast<uint64_t>(16u - (_stepRateTime & 0x0F)) * 2u * tstatesPerMs();
}

/// endregion </Helper methods>

/// region <Commands>

void UPD765::executeCommand()
{
    MLOGDEBUG("UPD765: command %02X (%u bytes) at T=%llu", _command[0], commandLengthFor(commandCode()),
              static_cast<unsigned long long>(_time));

    switch (commandCode())
    {
        case CMD_SPECIFY:
            commandSpecify();
            break;
        case CMD_SENSE_DRIVE_STATUS:
            commandSenseDriveStatus();
            break;
        case CMD_SENSE_INTERRUPT_STATUS:
            commandSenseInterruptStatus();
            break;
        case CMD_RECALIBRATE:
            commandSeek(true);
            break;
        case CMD_SEEK:
            commandSeek(false);
            break;
        case CMD_READ_DATA:
        case CMD_READ_DELETED_DATA:
        case CMD_WRITE_DATA:
        case CMD_WRITE_DELETED_DATA:
            commandReadWrite();
            break;
        case CMD_READ_ID:
            commandReadId();
            break;
        case CMD_FORMAT_TRACK:
            commandFormatTrack();
            break;
        default:
            // READ TRACK and SCAN take their parameters but are not implemented yet (design §10 phase 4)
            if (commandLengthFor(commandCode()) > 1)
            {
                MLOGWARNING("UPD765: command %02X not implemented, abnormal termination", _command[0]);
                _st0 = static_cast<uint8_t>((_command[1] & 0x07) | ST0_IC_ABNORMAL);
                _st1 = ST1_MA;
                _st2 = 0;
                enterDataResult();
            }
            else
            {
                commandInvalid();
            }
            break;
    }
}

void UPD765::commandSpecify()
{
    _stepRateTime = static_cast<uint8_t>(_command[1] >> 4);
    _headLoadTime = static_cast<uint8_t>(_command[2] >> 1);
    _phase = PHASE_COMMAND;
}

void UPD765::commandSenseDriveStatus()
{
    uint8_t st3 = static_cast<uint8_t>(_command[1] & (ST3_US | ST3_HD));

    FDD* fdd = driveFor(unit());
    if (isReady(unit()))
        st3 |= ST3_RY;
    if (fdd != nullptr)
    {
        if (fdd->isWriteProtect())
            st3 |= ST3_WP;
        if (fdd->isTrack00())
            st3 |= ST3_T0;
        if (fdd->isDiskInserted() && fdd->getDiskImage() != nullptr && fdd->getDiskImage()->getSides() > 1)
            st3 |= ST3_TS;
    }

    enterResult(&st3, 1);
}

void UPD765::commandSenseInterruptStatus()
{
    completeSeeks();

    for (UnitState& unitState : _units)
    {
        if (!unitState.interruptPending)
            continue;

        const uint8_t bytes[2] = { unitState.st0, unitState.pcn };
        unitState.interruptPending = false;
        unitState.seekBusy = false;
        enterResult(bytes, 2);
        return;
    }

    commandInvalid();
}

void UPD765::commandSeek(bool recalibrate)
{
    const uint8_t unitNumber = unit();
    UnitState& unitState = _units[unitNumber];
    FDD* fdd = driveFor(unitNumber);

    unitState.seekBusy = true;
    unitState.interruptPending = false;

    if (!isReady(unitNumber))
    {
        unitState.st0 = static_cast<uint8_t>(unitNumber | ST0_NR | ST0_IC_ABNORMAL | ST0_SE);
        unitState.interruptPending = true;
        _phase = PHASE_COMMAND;
        return;
    }

    int steps = 0;
    if (recalibrate)
    {
        // Step out until TRACK 0, at most 77 pulses: a head further out ends with EQUIPMENT CHECK
        const int physical = fdd->getTrack();
        steps = std::min<int>(physical, RECALIBRATE_STEPS);
        unitState.stepDelta = static_cast<int16_t>(-steps);
        unitState.ncn = 0;
        unitState.st0 = static_cast<uint8_t>(unitNumber | ST0_SE);
        if (physical > RECALIBRATE_STEPS)
        {
            unitState.st0 |= ST0_EC | ST0_IC_ABNORMAL;
            unitState.ncn = static_cast<uint8_t>(unitState.pcn - RECALIBRATE_STEPS);
        }
    }
    else
    {
        unitState.ncn = _command[2];
        steps = std::abs(static_cast<int>(unitState.ncn) - static_cast<int>(unitState.pcn));
        unitState.stepDelta = static_cast<int16_t>(static_cast<int>(unitState.ncn) - static_cast<int>(unitState.pcn));
        unitState.st0 = static_cast<uint8_t>((_command[1] & 0x07) | ST0_SE);
    }

    unitState.seeking = true;
    unitState.seekEndTime = _time + static_cast<uint64_t>(steps) * stepTStates();

    // The command phase is free again while the head steps
    _phase = PHASE_COMMAND;
    completeSeeks();
}

void UPD765::completeSeeks()
{
    for (uint8_t unitNumber = 0; unitNumber < UNITS; unitNumber++)
    {
        UnitState& unitState = _units[unitNumber];
        if (!unitState.seeking || unitState.seekEndTime > _time)
            continue;

        if (FDD* fdd = driveFor(unitNumber))
            fdd->setTrack(static_cast<int8_t>(fdd->getTrack() + unitState.stepDelta));

        unitState.pcn = unitState.ncn;
        unitState.seeking = false;
        unitState.stepDelta = 0;
        unitState.interruptPending = true;
    }
}

void UPD765::commandReadWrite()
{
    const uint8_t code = commandCode();
    const bool write = (code == CMD_WRITE_DATA || code == CMD_WRITE_DELETED_DATA);

    _st0 = static_cast<uint8_t>(_command[1] & 0x07);
    _st1 = 0;
    _st2 = 0;
    _endAfterSector = false;
    _sectorIndex = -1;

    if (!isReady(unit()))
    {
        _st0 |= ST0_NR | ST0_IC_ABNORMAL;
        enterDataResult();
        return;
    }

    FDD* fdd = driveFor(unit());
    if (write && fdd->isWriteProtect())
    {
        _st0 |= ST0_IC_ABNORMAL;
        _st1 |= ST1_NW;
        enterDataResult();
        return;
    }

    fdd->setSide(head() != 0);

    _phase = PHASE_EXECUTION;
    _state = S_SEARCH;
    _eventTime = _time + headLoadTStates();
}

void UPD765::commandReadId()
{
    _st0 = static_cast<uint8_t>(_command[1] & 0x07);
    _st1 = 0;
    _st2 = 0;
    std::memset(&_command[2], 0, 4);

    if (!isReady(unit()))
    {
        _st0 |= ST0_NR | ST0_IC_ABNORMAL;
        enterDataResult();
        return;
    }

    driveFor(unit())->setSide(head() != 0);

    _phase = PHASE_EXECUTION;
    _state = S_READ_ID;
    _eventTime = _time + headLoadTStates();
}

void UPD765::commandFormatTrack()
{
    _st0 = static_cast<uint8_t>(_command[1] & 0x07);
    _st1 = 0;
    _st2 = 0;

    if (!isReady(unit()))
    {
        _st0 |= ST0_NR | ST0_IC_ABNORMAL;
        enterDataResult();
        return;
    }

    FDD* fdd = driveFor(unit());
    if (fdd->isWriteProtect())
    {
        _st0 |= ST0_IC_ABNORMAL;
        _st1 |= ST1_NW;
        enterDataResult();
        return;
    }

    fdd->setSide(head() != 0);

    // Formatting starts at the index hole and asks for C, H, R, N of every sector as it writes them
    _formatStart = nextIndexPulse(_time + headLoadTStates());
    _byteIndex = 0;
    _transferSize = static_cast<uint16_t>(_command[3] * 4u);
    _phase = PHASE_EXECUTION;
    _state = (_transferSize != 0) ? S_FORMAT_REQUEST : S_FORMAT_END;
    _eventTime = (_transferSize != 0) ? _formatStart : _formatStart + rotationTStates();
    if (_transferSize != 0)
    {
        // First ID byte of the first sector
        const DiskImage::Track* track = currentTrack();
        const size_t cell = byteCellTStates(track);
        _eventTime = _formatStart + (FORMAT_PREAMBLE_BYTES + FORMAT_ID_OFFSET) * cell;
    }
}

void UPD765::commandInvalid()
{
    const uint8_t st0 = ST0_IC_INVALID;
    enterResult(&st0, 1);
}

/// endregion </Commands>

/// region <Execution phase>

void UPD765::runState()
{
    switch (_state)
    {
        case S_SEARCH:
            searchSector();
            break;
        case S_READ_BYTE:
            readByte();
            break;
        case S_WRITE_BYTE:
            writeByte();
            break;
        case S_READ_ID:
            readIdDone();
            break;
        case S_FORMAT_REQUEST:
            formatRequest();
            break;
        case S_FORMAT_DUE:
            formatDue();
            break;
        case S_FORMAT_END:
            formatEnd();
            break;
        case S_RESULT:
            enterDataResult();
            break;
        case S_IDLE:
        default:
            // Nothing scheduled: leave the execution phase rather than spin
            _st0 |= ST0_IC_ABNORMAL;
            enterDataResult();
            break;
    }
}

/// Find sector C H R N from the head position. The chip gives up after two index pulses
void UPD765::searchSector()
{
    const uint64_t start = _eventTime;
    const uint64_t giveUp = nextIndexPulse(start) + rotationTStates();
    const bool mfm = (_command[0] & CMD_FLAG_MF) != 0;

    DiskImage::Track* track = currentTrack();
    if (track == nullptr || track->sectorCount() == 0 ||
        (track->encoding() == DiskImage::Encoding::MFM) != mfm)
    {
        // No ID field this density can read
        _st0 |= ST0_IC_ABNORMAL;
        _st1 |= ST1_MA;
        scheduleResult(giveUp);
        return;
    }

    const uint8_t c = _command[2];
    const uint8_t h = _command[3];
    const uint8_t r = _command[4];
    const uint8_t n = _command[5];
    const uint8_t code = commandCode();
    const bool read = (code == CMD_READ_DATA || code == CMD_READ_DELETED_DATA);

    std::vector<DiskImage::Sector>& sectors = track->sectors();
    const size_t count = sectors.size();
    const size_t headOffset = headByteOffset(*track, start);
    const size_t cell = byteCellTStates(track);

    size_t first = 0;
    while (first < count && sectors[first].idamOffset < headOffset)
        first++;

    for (size_t i = 0; i < count; i++)
    {
        const size_t index = (first + i) % count;
        DiskImage::Sector& sector = sectors[index];

        if (sector.cylinder() != c || sector.head() != h || sector.number() != r || sector.sizeCode() != n)
        {
            // Every ID passed on the way with another cylinder is reported if the search fails
            if (sector.cylinder() != c)
                _st2 |= (sector.cylinder() == 0xFF) ? (ST2_WC | ST2_BC) : ST2_WC;
            continue;
        }
        _st2 &= static_cast<uint8_t>(~(ST2_WC | ST2_BC));

        const size_t bytesToId = track->bytesUntil(sector, headOffset);
        const uint64_t idEnd = start + (bytesToId + ID_FIELD_BYTES) * cell;

        if (!sector.idCrcValid)
        {
            _st0 |= ST0_IC_ABNORMAL;
            _st1 |= ST1_DE;
            scheduleResult(idEnd);
            return;
        }

        if (!sector.hasData)
        {
            _st0 |= ST0_IC_ABNORMAL;
            _st1 |= ST1_MA;
            _st2 |= ST2_MD;
            scheduleResult(idEnd);
            return;
        }

        const size_t bytesToData = bytesToId + (sector.dataOffset - sector.idamOffset);
        const uint64_t dataStart = start + bytesToData * cell;

        if (read && sector.deleted != (code == CMD_READ_DELETED_DATA))
        {
            // The other data address mark: SK = 1 skips the sector, SK = 0 reads it and stops after it
            _st2 |= ST2_CM;
            if (_command[0] & CMD_FLAG_SK)
            {
                _sectorIndex = static_cast<int16_t>(index);
                finishSector(dataStart + (sector.dataSize + 2u) * cell);
                return;
            }
            _endAfterSector = true;
        }

        _sectorIndex = static_cast<int16_t>(index);
        _byteIndex = 0;
        _transferSize = sector.dataSize;
        if (n == 0)
        {
            // N = 0: DTL bytes of the 128-byte sector
            const uint8_t dtl = _command[8];
            _transferSize = static_cast<uint16_t>((dtl != 0 && dtl < sector.dataSize) ? dtl : sector.dataSize);
        }

        _state = read ? S_READ_BYTE : S_WRITE_BYTE;
        _eventTime = dataStart;
        return;
    }

    _st0 |= ST0_IC_ABNORMAL;
    _st1 |= ST1_ND;
    scheduleResult(giveUp);
}

/// Byte _byteIndex arrives; the one before it must have been taken by now
void UPD765::readByte()
{
    const uint64_t now = _eventTime;
    DiskImage::Track* track = currentTrack();
    DiskImage::Sector* sector = currentSector();
    if (track == nullptr || sector == nullptr || !sector->hasData)
    {
        // The disk went away under the head
        _st0 |= ST0_IC_ABNORMAL;
        _st1 |= ST1_MA;
        scheduleResult(now);
        return;
    }

    const size_t cell = byteCellTStates(track);

    if (_dataReady)
    {
        _dataReady = false;
        _st0 |= ST0_IC_ABNORMAL;
        _st1 |= ST1_OR;
        scheduleResult(now);
        return;
    }

    if (_byteIndex < _transferSize)
    {
        uint8_t value = sector->data[_byteIndex];
        if (track->hasWeakBits())
            FlakySectorEmulator::mutateWeakDataByte(*track, sector->dataOffset + _byteIndex, now, value);

        _dataRegister = value;
        _dataReady = true;
        _byteIndex++;
        _eventTime = now + cell;
        return;
    }

    // Every byte taken: the rest of the data field and its CRC pass under the head
    const uint64_t end = now + (static_cast<size_t>(sector->dataSize - _transferSize) + 2u) * cell;
    if (!sector->dataCrcValid)
    {
        _st0 |= ST0_IC_ABNORMAL;
        _st1 |= ST1_DE;
        _st2 |= ST2_DD;
        scheduleResult(end);
        return;
    }

    finishSector(end);
}

/// Byte _byteIndex is due; the one requested before it must have been written by now
void UPD765::writeByte()
{
    const uint64_t now = _eventTime;
    DiskImage::Track* track = currentTrack();
    DiskImage::Sector* sector = currentSector();
    if (track == nullptr || sector == nullptr || !sector->hasData)
    {
        _st0 |= ST0_IC_ABNORMAL;
        _st1 |= ST1_MA;
        scheduleResult(now);
        return;
    }

    const size_t cell = byteCellTStates(track);

    if (_dataRequested)
    {
        // The rest of the field is written as zeros, then the CRC
        _dataRequested = false;
        _byteIndex--;
        commitWrittenSector();
        _st0 |= ST0_IC_ABNORMAL;
        _st1 |= ST1_OR;
        scheduleResult(now);
        return;
    }

    if (_byteIndex < _transferSize)
    {
        _dataRequested = true;
        _byteIndex++;
        _eventTime = now + cell;
        return;
    }

    commitWrittenSector();
    finishSector(now + (static_cast<size_t>(sector->dataSize - _transferSize) + 2u) * cell);
}

/// Close the data field just written: zero fill after the last byte given, data mark, CRC, dirty flags
void UPD765::commitWrittenSector()
{
    DiskImage::Track* track = currentTrack();
    DiskImage::Sector* sector = currentSector();
    if (track == nullptr || sector == nullptr || !sector->hasData)
        return;

    for (size_t offset = _byteIndex; offset < sector->dataSize; offset++)
    {
        sector->data[offset] = 0x00;
        track->setWeakByte(sector->dataOffset + offset, false);
    }

    sector->setDataAddressMark(commandCode() == CMD_WRITE_DELETED_DATA ? 0xF8 : 0xFB);
    sector->recalculateDataCRC();
    sector->dirty = true;
    track->markDirty();
}

/// The sector is done: next R, the other head (MT) or end of cylinder
void UPD765::finishSector(uint64_t time)
{
    if (_endAfterSector)
    {
        scheduleResult(time);
        return;
    }

    if (_command[4] != _command[6])
    {
        _command[4]++;
        _state = S_SEARCH;
        _eventTime = time;
        return;
    }

    // R reached EOT. TC is tied low on the +3, so the transfer ends with "end of cylinder"
    bool done = false;
    if (_command[0] & CMD_FLAG_MT)
    {
        _command[3] ^= 0x01;
        _command[4] = 1;
        _command[1] = static_cast<uint8_t>((_command[1] & ~0x04) | ((_command[3] & 0x01) << 2));
        if (_command[3] & 0x01)
        {
            _st0 |= ST0_HD;
            if (FDD* fdd = driveFor(unit()))
                fdd->setSide(true);
        }
    }

    if (!(_command[0] & CMD_FLAG_MT) || !(_command[3] & 0x01))
    {
        _st0 |= ST0_IC_ABNORMAL;
        _st1 |= ST1_EN;
        done = true;
    }

    if (done)
    {
        scheduleResult(time);
        return;
    }

    _state = S_SEARCH;
    _eventTime = time;
}

/// READ ID: the next ID field under the head
void UPD765::readIdDone()
{
    const uint64_t start = _eventTime;
    const bool mfm = (_command[0] & CMD_FLAG_MF) != 0;

    DiskImage::Track* track = currentTrack();
    if (track == nullptr || track->sectorCount() == 0 ||
        (track->encoding() == DiskImage::Encoding::MFM) != mfm)
    {
        _st0 |= ST0_IC_ABNORMAL;
        _st1 |= ST1_MA;
        scheduleResult(nextIndexPulse(start) + rotationTStates());
        return;
    }

    const size_t headOffset = headByteOffset(*track, start);
    const DiskImage::Sector* sector = track->nextSector(headOffset);
    _command[2] = sector->cylinder();
    _command[3] = sector->head();
    _command[4] = sector->number();
    _command[5] = sector->sizeCode();

    if (!sector->idCrcValid)
    {
        _st0 |= ST0_IC_ABNORMAL;
        _st1 |= ST1_DE;
    }

    scheduleResult(start + (track->bytesUntil(*sector, headOffset) + ID_FIELD_BYTES) * byteCellTStates(track));
}

/// FORMAT TRACK asks for the next ID byte
void UPD765::formatRequest()
{
    const DiskImage::Track* track = currentTrack();
    const size_t cell = byteCellTStates(track);

    _dataRequested = true;
    _state = S_FORMAT_DUE;
    _eventTime += cell;
}

/// The requested ID byte must be there by now; ask for the next one where the next ID goes
void UPD765::formatDue()
{
    const uint64_t now = _eventTime;
    if (_dataRequested)
    {
        _dataRequested = false;
        _st0 |= ST0_IC_ABNORMAL;
        _st1 |= ST1_OR;
        scheduleResult(now);
        return;
    }

    if (_byteIndex >= _transferSize)
    {
        _state = S_FORMAT_END;
        _eventTime = std::max(now, _formatStart + rotationTStates());
        return;
    }

    const DiskImage::Track* track = currentTrack();
    const size_t cell = byteCellTStates(track);
    const size_t sectorBytes = FORMAT_SLOT_BYTES + (128u << (_command[2] & 0x03)) + _command[4];
    const size_t sector = _byteIndex / 4u;
    const size_t field = _byteIndex % 4u;
    const uint64_t due = _formatStart + (FORMAT_PREAMBLE_BYTES + sector * sectorBytes + FORMAT_ID_OFFSET + field) * cell;

    _state = S_FORMAT_REQUEST;
    _eventTime = std::max(now, due);
}

/// The index pulse after the formatted revolution: the track now holds the sectors the CPU described
void UPD765::formatEnd()
{
    const uint8_t n = _command[2];
    const uint8_t sectorsRequested = _command[3];
    const uint8_t gap3 = _command[4];
    const uint8_t filler = _command[5];
    const size_t sectors = std::min<size_t>(sectorsRequested, MAX_FORMAT_SECTORS);

    if (DiskImage::Track* track = currentTrack())
    {
        DiskImage::TrackFormatSpec spec = DiskImage::TrackFormatSpec::ibm(static_cast<uint8_t>(sectors), n, 1, gap3, filler);
        if (!(_command[0] & CMD_FLAG_MF))
        {
            spec.encoding = DiskImage::Encoding::FM;
            spec.trackLength = NOMINAL_TRACK_LEN_FM;
            spec.gapFill = 0xFF;
        }

        spec.sectorCylinders.resize(sectors);
        spec.sectorHeads.resize(sectors);
        spec.sectorSizeCodes.resize(sectors);
        for (size_t i = 0; i < sectors; i++)
        {
            spec.sectorCylinders[i] = _formatIds[i * 4 + 0];
            spec.sectorHeads[i] = _formatIds[i * 4 + 1];
            spec.sectorNumbers[i] = _formatIds[i * 4 + 2];
            spec.sectorSizeCodes[i] = _formatIds[i * 4 + 3];
        }

        track->formatTrack(track->cylinder(), track->side(), spec);
        track->markRawTrackDirty();
    }

    // Result C H R N: the last ID written
    if (sectors > 0)
        std::memcpy(&_command[2], &_formatIds[(sectors - 1) * 4], 4);

    enterDataResult();
}

void UPD765::scheduleResult(uint64_t time)
{
    _state = S_RESULT;
    _eventTime = time;
}

/// Result phase of a data command: ST0 ST1 ST2 C H R N
void UPD765::enterDataResult()
{
    // CM on its own is a normal end; EN is not reported next to another error (Caprice / +3DOS)
    const bool otherError = (_st1 & (ST1_MA | ST1_NW | ST1_ND | ST1_OR | ST1_DE)) != 0 ||
                            (_st2 & (ST2_MD | ST2_DD)) != 0 || (_st0 & ST0_NR) != 0;
    if ((_st2 & ST2_CM) && !otherError)
    {
        _st0 &= static_cast<uint8_t>(~ST0_IC);
        _st1 &= static_cast<uint8_t>(~ST1_EN);
    }
    if (otherError)
        _st1 &= static_cast<uint8_t>(~ST1_EN);

    const uint8_t bytes[7] = { _st0, _st1, _st2, _command[2], _command[3], _command[4], _command[5] };

    _dataReady = false;
    _dataRequested = false;
    _sectorIndex = -1;
    enterResult(bytes, 7);
}

void UPD765::enterResult(const uint8_t* bytes, uint8_t count)
{
    std::memcpy(_result, bytes, count);
    _resultLength = count;
    _resultPos = 0;
    _state = S_IDLE;
    _phase = PHASE_RESULT;
}

/// endregion </Execution phase>

/// region <TTDSerializable>

namespace
{
#pragma pack(push, 1)
/// Fixed layout: TTD blobs are copied and hashed byte-wise. Disk image pointers are not stored; the sector
/// in transfer is its index on the track under the head
struct Upd765TTDState
{
    uint64_t time;
    uint64_t eventTime;
    uint64_t formatStart;
    uint64_t seekEndTime[UPD765::UNITS];
    int16_t stepDelta[UPD765::UNITS];
    uint8_t pcn[UPD765::UNITS];
    uint8_t ncn[UPD765::UNITS];
    uint8_t unitSt0[UPD765::UNITS];
    uint8_t unitFlags[UPD765::UNITS];  // bit 0 seeking, bit 1 interrupt pending, bit 2 seek busy
    uint16_t byteIndex;
    uint16_t transferSize;
    int16_t sectorIndex;
    uint8_t phase;
    uint8_t state;
    uint8_t command[UPD765::MAX_COMMAND_BYTES];
    uint8_t commandLength;
    uint8_t commandPos;
    uint8_t result[UPD765::MAX_RESULT_BYTES];
    uint8_t resultLength;
    uint8_t resultPos;
    uint8_t st0;
    uint8_t st1;
    uint8_t st2;
    uint8_t dataRegister;
    uint8_t flags;  // bit 0 data ready, bit 1 data requested, bit 2 end after sector, bit 3 motor
    uint8_t stepRateTime;
    uint8_t headLoadTime;
    uint8_t reserved[13];
    uint8_t formatIds[UPD765::MAX_FORMAT_SECTORS * 4];
};
#pragma pack(pop)
static_assert(sizeof(Upd765TTDState) == 384, "Upd765TTDState layout changed: bump the blob consumers");
}  // namespace

size_t UPD765::TTDStateSize() const
{
    return sizeof(Upd765TTDState);
}

void UPD765::TTDSaveState(uint8_t* dst) const
{
    if (dst == nullptr)
        return;

    Upd765TTDState blob{};
    blob.time = _time;
    blob.eventTime = _eventTime;
    blob.formatStart = _formatStart;
    for (size_t i = 0; i < UNITS; i++)
    {
        const UnitState& unitState = _units[i];
        blob.seekEndTime[i] = unitState.seekEndTime;
        blob.stepDelta[i] = unitState.stepDelta;
        blob.pcn[i] = unitState.pcn;
        blob.ncn[i] = unitState.ncn;
        blob.unitSt0[i] = unitState.st0;
        blob.unitFlags[i] = static_cast<uint8_t>((unitState.seeking ? 0x01 : 0) | (unitState.interruptPending ? 0x02 : 0) |
                                                 (unitState.seekBusy ? 0x04 : 0));
    }
    blob.byteIndex = _byteIndex;
    blob.transferSize = _transferSize;
    blob.sectorIndex = _sectorIndex;
    blob.phase = _phase;
    blob.state = _state;
    std::memcpy(blob.command, _command, sizeof(blob.command));
    blob.commandLength = _commandLength;
    blob.commandPos = _commandPos;
    std::memcpy(blob.result, _result, sizeof(blob.result));
    blob.resultLength = _resultLength;
    blob.resultPos = _resultPos;
    blob.st0 = _st0;
    blob.st1 = _st1;
    blob.st2 = _st2;
    blob.dataRegister = _dataRegister;
    blob.flags = static_cast<uint8_t>((_dataReady ? 0x01 : 0) | (_dataRequested ? 0x02 : 0) |
                                      (_endAfterSector ? 0x04 : 0) | (_motorOn ? 0x08 : 0));
    blob.stepRateTime = _stepRateTime;
    blob.headLoadTime = _headLoadTime;
    std::memcpy(blob.formatIds, _formatIds, sizeof(blob.formatIds));

    std::memcpy(dst, &blob, sizeof(blob));
}

void UPD765::TTDLoadState(const uint8_t* src)
{
    if (src == nullptr)
        return;

    Upd765TTDState blob{};
    std::memcpy(&blob, src, sizeof(blob));

    _time = blob.time;
    _eventTime = blob.eventTime;
    _formatStart = blob.formatStart;
    for (size_t i = 0; i < UNITS; i++)
    {
        UnitState& unitState = _units[i];
        unitState.seekEndTime = blob.seekEndTime[i];
        unitState.stepDelta = blob.stepDelta[i];
        unitState.pcn = blob.pcn[i];
        unitState.ncn = blob.ncn[i];
        unitState.st0 = blob.unitSt0[i];
        unitState.seeking = (blob.unitFlags[i] & 0x01) != 0;
        unitState.interruptPending = (blob.unitFlags[i] & 0x02) != 0;
        unitState.seekBusy = (blob.unitFlags[i] & 0x04) != 0;
    }
    _byteIndex = blob.byteIndex;
    _transferSize = blob.transferSize;
    _sectorIndex = blob.sectorIndex;
    _phase = static_cast<UPDPHASE>(blob.phase);
    _state = static_cast<UPDSTATE>(blob.state);
    std::memcpy(_command, blob.command, sizeof(_command));
    _commandLength = blob.commandLength;
    _commandPos = blob.commandPos;
    std::memcpy(_result, blob.result, sizeof(_result));
    _resultLength = blob.resultLength;
    _resultPos = blob.resultPos;
    _st0 = blob.st0;
    _st1 = blob.st1;
    _st2 = blob.st2;
    _dataRegister = blob.dataRegister;
    _dataReady = (blob.flags & 0x01) != 0;
    _dataRequested = (blob.flags & 0x02) != 0;
    _endAfterSector = (blob.flags & 0x04) != 0;
    _stepRateTime = blob.stepRateTime;
    _headLoadTime = blob.headLoadTime;
    std::memcpy(_formatIds, blob.formatIds, sizeof(_formatIds));

    // The motor latch comes back with #1FFD; the drives follow it (their own state is in the BetaDisk blob)
    _motorOn = (blob.flags & 0x08) != 0;
}

uint64_t UPD765::TTDHashState() const
{
    uint8_t blob[sizeof(Upd765TTDState)];
    TTDSaveState(blob);

    uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a offset basis
    for (uint8_t byte : blob)
    {
        h ^= byte;
        h *= 0x100000001b3ULL;  // FNV-1a prime
    }
    return h;
}

/// endregion </TTDSerializable>
