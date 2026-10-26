#include "stdafx.h"

#include "nextxilinx.h"

namespace
{
// UG480 transfer functions, the 12-bit result in bits 15:4: temperature = code * 503.975 / 4096 - 273.15, supply = code / 4096 * 3 V
constexpr uint16_t kTemperature = 2586 << 4;  // 45.0 C
constexpr uint16_t kVccInt = 1365 << 4;       // 1.0 V
constexpr uint16_t kVccAux = 2458 << 4;       // 1.8 V
constexpr uint16_t kVccBram = 1365 << 4;      // 1.0 V
constexpr uint16_t kVrefP = 1707 << 4;        // 1.25 V
}  // namespace

void NextXilinx::PowerOn()
{
    for (uint16_t& r : _control)
        r = 0;
    _control[0x41 - 0x40] = 0x00F0;  // INIT_41: the default sequence, the alarms off; INIT_40 / INIT_42 / INIT_48-5C are 0
    _dnaOut = 0;
    _dnaShifted = 0;
    _drpWrite = false;
    _drpAddress = 0;
    _d0 = _d1 = 0;
    Reset();
}

void NextXilinx::Reset()
{
    _select = true;
    _dnaEnabled = false;
    _adcEnabled = false;
}

bool NextXilinx::DnaBit() const
{
    // DOUT: the id's most significant bit first, then the DIN the core ties to 0
    return _dnaShifted < kDnaBits && ((kDna >> (kDnaBits - 1 - _dnaShifted)) & 1);
}

uint8_t NextXilinx::ReadCommand() const
{
    if (_select)
        return static_cast<uint8_t>(0x80 | (_adcEnabled ? 2 : 0) | (_dnaEnabled ? 1 : 0));
    if (_dnaEnabled)
        return static_cast<uint8_t>(_dnaOut >> 7);
    if (_adcEnabled)
        return 0x03;  // '0' & BUSY & "0000" & EOC & EOS
    return 0;
}

void NextXilinx::AfterProgramRead()
{
    if (!_select && _dnaEnabled)
    {
        _dnaOut = static_cast<uint8_t>((_dnaOut << 1) | (DnaBit() ? 1 : 0));
        if (_dnaShifted < kDnaBits)
            _dnaShifted++;
    }
}

void NextXilinx::WriteCommand(uint8_t value)
{
    // The load and the XADC's reset / convert start look at the select mode the write finds, not the one it sets
    if (!_select && !(value & 0x80))
    {
        if (_dnaEnabled)
        {
            _dnaOut = kDnaLength;
            _dnaShifted = 0;
        }
        // _adcEnabled: bit 6 resets the XADC, bit 0 starts a conversion; the fixed die reads the same either way
    }
    _select = (value & 0x80) != 0;
    if ((value & 0xC0) == 0xC0)
    {
        _dnaEnabled = (value & 1) && !(value & 2);
        _adcEnabled = (value & 2) && !(value & 1);
    }
}

void NextXilinx::WriteAddress(uint8_t value)
{
    _drpWrite = (value & 0x80) != 0;
    _drpAddress = static_cast<uint8_t>(value & 0x7F);
    if (_drpWrite)
    {
        if (_drpAddress >= 0x40)
            _control[_drpAddress - 0x40] = static_cast<uint16_t>((_d1 << 8) | _d0);
        return;
    }
    const uint16_t data = DrpRead(_drpAddress);
    _d0 = static_cast<uint8_t>(data);
    _d1 = static_cast<uint8_t>(data >> 8);
}

uint16_t NextXilinx::DrpRead(uint8_t address) const
{
    if (address >= 0x40)
        return _control[(address - 0x40) & 0x3F];
    switch (address)
    {
        case 0x00: case 0x20: case 0x24:  // the temperature, its maximum and minimum
            return kTemperature;
        case 0x01: case 0x21: case 0x25:
            return kVccInt;
        case 0x02: case 0x22: case 0x26:
            return kVccAux;
        case 0x06: case 0x23: case 0x27:
            return kVccBram;
        case 0x04:
            return kVrefP;
        default:
            return 0;  // VP / VN, VREFN, the offsets and gains, the auxiliary inputs, the flags
    }
}
