#include "stdafx.h"

#include "atadevice.h"

using namespace ata;

AtaDevice::AtaDevice(AtaDeviceKind kind) : _kind(kind) {}

/// region <Medium>

void AtaDevice::AttachMedium(IBlockDevice& medium, const DriveConfig& config)
{
    _medium = &medium;
    _config = config;
    MediumChanged();
}

void AtaDevice::DetachMedium()
{
    _medium = nullptr;
    MediumChanged();
}

/// endregion </Medium>

/// region <Bus>

void AtaDevice::WriteRegister(uint8_t reg, uint8_t value)
{
    if (reg == Control)
    {
        const bool wasReset = _s.control & DeviceControl::SRST;
        const bool isReset = value & DeviceControl::SRST;
        _s.control = value;
        if (isReset && !wasReset)
        {
            _s.status = Status::BSY;
            _s.phase = static_cast<uint8_t>(AtaPhase::Idle);
            _s.intrq = 0;
        }
        else if (!isReset && wasReset)
        {
            SoftResetDone();
        }
        return;
    }

    // A busy device ignores the task file (a reset in progress)
    if (_s.status & Status::BSY)
        return;

    // Writing the task file clears HOB; the previous value moves to the HOB copy
    _s.control &= static_cast<uint8_t>(~DeviceControl::HOB);
    switch (reg)
    {
        case ErrorFeatures:
            _s.hobFeatures = _s.features;
            _s.features = value;
            break;
        case SectorCount:
            _s.hobSectorCount = _s.sectorCount;
            _s.sectorCount = value;
            break;
        case SectorNumber:
            _s.hobLbaLow = _s.lbaLow;
            _s.lbaLow = value;
            break;
        case CylinderLow:
            _s.hobLbaMid = _s.lbaMid;
            _s.lbaMid = value;
            break;
        case CylinderHigh:
            _s.hobLbaHigh = _s.lbaHigh;
            _s.lbaHigh = value;
            break;
        case DeviceHead:
            _s.device = value;
            break;
        case StatusCommand:
            // A new command ends any transfer in progress
            _s.intrq = 0;
            _s.error = 0;
            _s.phase = static_cast<uint8_t>(AtaPhase::Idle);
            _s.bufferPos = 0;
            _s.bufferLen = 0;
            _s.command = value;
            ExecuteCommand(value);
            break;
        default:
            break;
    }
}

uint8_t AtaDevice::ReadRegister(uint8_t reg)
{
    const bool hob = _s.control & DeviceControl::HOB;
    switch (reg)
    {
        case ErrorFeatures:
            return _s.error;
        case SectorCount:
            return hob ? _s.hobSectorCount : _s.sectorCount;
        case SectorNumber:
            return hob ? _s.hobLbaLow : _s.lbaLow;
        case CylinderLow:
            return hob ? _s.hobLbaMid : _s.lbaMid;
        case CylinderHigh:
            return hob ? _s.hobLbaHigh : _s.lbaHigh;
        case DeviceHead:
            return _s.device;
        case StatusCommand:
            _s.intrq = 0;  // reading the status acknowledges the interrupt
            return _s.status;
        case Control:
            return _s.status;  // alternate status: no side effects
        default:
            return 0xFF;
    }
}

uint16_t AtaDevice::ReadData()
{
    if (static_cast<AtaPhase>(_s.phase) != AtaPhase::DataIn || !(_s.status & Status::DRQ) ||
        static_cast<size_t>(_s.bufferPos) + 1 >= sizeof(_s.buffer))
        return 0xFFFF;

    const uint16_t word = static_cast<uint16_t>(_s.buffer[_s.bufferPos] | (_s.buffer[_s.bufferPos + 1] << 8));
    _s.bufferPos += 2;
    if (_s.bufferPos >= _s.bufferLen)
    {
        _s.status &= static_cast<uint8_t>(~Status::DRQ);
        if (_activity && CountsAsActivity())
            _activity->fetch_add(1, std::memory_order_relaxed);
        DataInDone();
    }
    return word;
}

void AtaDevice::WriteData(uint16_t word)
{
    const AtaPhase phase = static_cast<AtaPhase>(_s.phase);
    if ((phase != AtaPhase::DataOut && phase != AtaPhase::PacketCommand) || !(_s.status & Status::DRQ) ||
        static_cast<size_t>(_s.bufferPos) + 1 >= sizeof(_s.buffer))
        return;

    _s.buffer[_s.bufferPos] = static_cast<uint8_t>(word & 0xFF);
    _s.buffer[_s.bufferPos + 1] = static_cast<uint8_t>(word >> 8);
    _s.bufferPos += 2;
    if (_s.bufferPos >= _s.bufferLen)
    {
        _s.status &= static_cast<uint8_t>(~Status::DRQ);
        if (_activity && CountsAsActivity())
            _activity->fetch_add(1, std::memory_order_relaxed);
        DataOutDone();
    }
}

bool AtaDevice::Intrq() const
{
    return _s.intrq && !(_s.control & DeviceControl::nIEN);
}

void AtaDevice::HardReset()
{
    const uint8_t unitAttention = _s.unitAttention;
    _s = AtaDeviceState{};
    _s.unitAttention = unitAttention;  // a disc change survives a bus reset until reported
    SoftResetDone();
    PowerOnReset();
}

void AtaDevice::RunDiagnostic(bool interrupt)
{
    _s.command = Command::ExecuteDiagnostic;
    SoftResetDone();
    _s.intrq = interrupt ? 1 : 0;
}

/// endregion </Bus>

/// region <Helpers for the command sets>

void AtaDevice::SoftResetDone()
{
    _s.phase = static_cast<uint8_t>(AtaPhase::Idle);
    _s.bufferPos = 0;
    _s.bufferLen = 0;
    _s.sectorsLeft = 0;
    _s.multiple = 0;
    _s.heads = 0;  // the command set restores its default translation
    _s.sectors = 0;
    _s.cylinders = 0;
    _s.intrq = 0;
    _s.error = 0x01;  // diagnostic code: no error
    _s.device = 0;    // ATA: 00h after a reset or a diagnostic (the master is selected)
    SetSignature();
    _s.status = ResetStatus();
}

void AtaDevice::Complete()
{
    _s.phase = static_cast<uint8_t>(AtaPhase::Idle);
    _s.status = static_cast<uint8_t>(ReadyStatus() & ~(Status::BSY | Status::DRQ | Status::ERR));
    _s.intrq = 1;
}

void AtaDevice::Abort(uint8_t errorBits)
{
    _s.phase = static_cast<uint8_t>(AtaPhase::Idle);
    _s.error = errorBits;
    _s.status = static_cast<uint8_t>((ReadyStatus() & ~(Status::BSY | Status::DRQ)) | Status::ERR);
    _s.intrq = 1;
}

void AtaDevice::StartDataIn(uint16_t length, bool interrupt)
{
    _s.phase = static_cast<uint8_t>(AtaPhase::DataIn);
    _s.bufferPos = 0;
    _s.bufferLen = length;
    _s.status = static_cast<uint8_t>((ReadyStatus() & ~(Status::BSY | Status::ERR)) | Status::DRQ);
    if (interrupt)
        _s.intrq = 1;
}

void AtaDevice::StartDataOut(uint16_t length, bool interrupt)
{
    _s.phase = static_cast<uint8_t>(AtaPhase::DataOut);
    _s.bufferPos = 0;
    _s.bufferLen = length;
    _s.status = static_cast<uint8_t>((ReadyStatus() & ~(Status::BSY | Status::ERR)) | Status::DRQ);
    if (interrupt)
        _s.intrq = 1;
}

void AtaDevice::NotifyWrite()
{
    if (_onWrite)
        _onWrite();
}

/// endregion </Helpers for the command sets>
