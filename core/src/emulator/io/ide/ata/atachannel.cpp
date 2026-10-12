#include "stdafx.h"

#include "atachannel.h"

#include "emulator/bus/bustap.h"

using namespace ata;

void AtaChannel::SetUnit(int unit, std::unique_ptr<AtaDevice> device)
{
    if (unit < 0 || unit >= kUnits)
        return;
    _units[unit] = std::move(device);
}

AtaDevice* AtaChannel::Present(int unit) const
{
    AtaDevice* device = Unit(unit);
    return device && device->IsPresent() ? device : nullptr;
}

bool AtaChannel::AnyPresent() const
{
    return Present(0) || Present(1);
}

uint8_t AtaChannel::SelectedStatus() const
{
    AtaDevice* device = Present(_selected);
    return device ? device->State().status : 0;
}

void AtaChannel::CheckWrite(uint8_t reg)
{
    // Device control is the one register a host may write while BSY (the software reset)
    const uint8_t status = SelectedStatus();
    if (reg != Control && (status & Status::BSY))
        _tap->Finding(BusRule::AtaWriteWhileBusy, static_cast<uint8_t>(_selected), reg);
    else if (reg == StatusCommand && (status & Status::DRQ))
        _tap->Finding(BusRule::AtaCommandDropsData, static_cast<uint8_t>(_selected), reg);
}

uint8_t AtaChannel::ReadRegister(uint8_t reg)
{
    if (_tap && _tap->Active()) [[unlikely]]
    {
        const uint8_t value = ReadRegisterUntapped(reg);
        _tap->AtaRegister(static_cast<uint8_t>(_selected), reg, value, false);
        return value;
    }
    return ReadRegisterUntapped(reg);
}

uint8_t AtaChannel::ReadRegisterUntapped(uint8_t reg)
{
    AtaDevice* selected = Present(_selected);
    if (selected)
        return selected->ReadRegister(reg);

    AtaDevice* other = Present(_selected ^ 1);
    if (!other)
        return static_cast<uint8_t>(_emptyBus & 0xFF);  // nothing drives the bus
    // Device 1 absent: device 0 answers for it, with status 0 (ATA)
    if (reg == StatusCommand || reg == Control)
        return 0x00;
    return other->ReadRegister(reg);
}

void AtaChannel::WriteRegister(uint8_t reg, uint8_t value)
{
    if (_tap && _tap->Active()) [[unlikely]]
    {
        if (_tap->Checking())
            CheckWrite(reg);
        if (reg == StatusCommand)
            _tap->AtaCommand(static_cast<uint8_t>(_selected), value);
        else
            _tap->AtaRegister(static_cast<uint8_t>(_selected), reg, value, true);
    }
    if (reg == StatusCommand)
    {
        if (value == Command::ExecuteDiagnostic)
        {
            _selected = 0;  // the diagnostic clears the device register
            // Both units run it; the master reports (and interrupts) for the pair
            for (int unit = 0; unit < kUnits; unit++)
            {
                if (AtaDevice* device = Present(unit))
                    device->RunDiagnostic(unit == 0 || !Present(0));
            }
            return;
        }
        if (AtaDevice* selected = Present(_selected))
            selected->WriteRegister(reg, value);
        return;
    }

    if (reg == DeviceHead)
        _selected = (value & DeviceBits::DEV) ? 1 : 0;
    if (reg == Control && (value & DeviceControl::SRST))
        _selected = 0;  // a software reset selects the master again (device register cleared)
    for (auto& device : _units)
    {
        if (device)
            device->WriteRegister(reg, value);
    }
}

uint16_t AtaChannel::ReadData()
{
    AtaDevice* selected = Present(_selected);
    if (_tap && _tap->Active()) [[unlikely]]
    {
        if (selected && !(selected->State().status & Status::DRQ) && _tap->Checking())
            _tap->Finding(BusRule::AtaDataWithoutDrq, static_cast<uint8_t>(_selected));
        const uint16_t word = selected ? selected->ReadData() : _emptyBus;
        _tap->AtaData(static_cast<uint8_t>(_selected), word, false);
        return word;
    }
    return selected ? selected->ReadData() : _emptyBus;  // the other unit never drives data for it
}

void AtaChannel::WriteData(uint16_t word)
{
    AtaDevice* selected = Present(_selected);
    if (_tap && _tap->Active()) [[unlikely]]
    {
        if (selected && !(selected->State().status & Status::DRQ) && _tap->Checking())
            _tap->Finding(BusRule::AtaDataWithoutDrq, static_cast<uint8_t>(_selected), 1);
        _tap->AtaData(static_cast<uint8_t>(_selected), word, true);
    }
    if (selected)
        selected->WriteData(word);
}

bool AtaChannel::Intrq() const
{
    AtaDevice* selected = Present(_selected);
    return selected && selected->Intrq();
}

void AtaChannel::HardReset()
{
    _selected = 0;
    for (auto& device : _units)
    {
        if (device)
            device->HardReset();
    }
}
