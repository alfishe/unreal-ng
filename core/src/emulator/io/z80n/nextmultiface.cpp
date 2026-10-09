#include "stdafx.h"

#include "nextmultiface.h"

#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/io/z80n/nextboard.h"

void NextMultiface::Reset()
{
    _nmiActive = false;
    _invisible = true;
    _mfEnable = false;
    Publish();
}

bool NextMultiface::Enabled() const { return (_board->Stored(0x83) & 0x02) != 0; }

unsigned NextMultiface::Type() const { return (_board->Stored(0x0A) >> 6) & 3; }

uint8_t NextMultiface::EnablePort() const
{
    const unsigned type = Type();
    return type & 2 ? 0x9F : (type & 1 ? 0xBF : 0x3F);
}

uint8_t NextMultiface::DisablePort() const
{
    const unsigned type = Type();
    return type & 2 ? 0x1F : (type & 1 ? 0x3F : 0xBF);
}

void NextMultiface::Publish() { _memory->SetMultifaceActive(_mfEnable); }

void NextMultiface::PressButton()
{
    if (_nmiActive)
        return;  // button_pulse = button and not nmi_active
    _nmiActive = true;
    _invisible = false;
}

void NextMultiface::OnRetn()
{
    _nmiActive = false;
    if (_mfEnable)
    {
        _mfEnable = false;
        Publish();
    }
}

bool NextMultiface::PortRead(uint8_t lowByte, uint16_t port, uint8_t& value)
{
    if (!Enabled())
        return false;
    const unsigned type = Type();
    const bool p3 = type == 0;
    const bool visible = !(_invisible && type != 3);  // invisible_eff: mode 48 is never invisible
    if (lowByte == DisablePort())
    {
        if (_mfEnable)
        {
            _mfEnable = false;
            Publish();
        }
        if (p3)
            _nmiActive = false;
        return false;
    }
    if (lowByte != EnablePort())
        return false;
    const bool wasIn = _mfEnable;
    _mfEnable = visible;
    if (_mfEnable != wasIn)
        Publish();
    if (!visible || type == 3)
        return false;
    const EmulatorState& state = *_state;
    if (p3)
    {
        switch ((port >> 12) & 0x0F)
        {
            case 0x1: value = static_cast<uint8_t>(state.p1FFD & 0x07);  // + the motor bit: not modelled (the +3 FDC is NR #D8's)
                      break;
            case 0x7: value = state.p7FFD; break;
            case 0xD: value = static_cast<uint8_t>(state.pDFFD & 0x0F); break;
            case 0xE: value = static_cast<uint8_t>(state.pEFF7 & 0x0C); break;
            default: value = static_cast<uint8_t>(state.pFE & 0x07); break;
        }
    }
    else
        value = static_cast<uint8_t>(((state.p7FFD >> 3) & 1) << 7 | 0x7F);
    return true;
}

void NextMultiface::PortWrite(uint8_t lowByte)
{
    if (!Enabled())
        return;
    const bool p3 = Type() == 0;
    if (lowByte == EnablePort())
    {
        _nmiActive = false;
        if (p3)
            _invisible = true;
    }
    else if (lowByte == DisablePort())
    {
        _nmiActive = false;
        if (!p3)
            _invisible = true;
    }
}

void NextMultiface::BeforeMachineM1(uint16_t address)
{
    if (address == 0x0066 && _nmiActive && !_mfEnable)
    {
        _mfEnable = true;  // the fetch of the NMI vector itself comes from the Multiface ROM
        Publish();
    }
}
