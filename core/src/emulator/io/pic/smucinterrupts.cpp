#include "stdafx.h"

#include "smucinterrupts.h"

#include "emulator/cpu/z80.h"
#include "emulator/io/rtc/ds12887.h"

void SmucInterrupts::Attach(Z80* cpu)
{
    if (cpu == _cpu)
        return;
    Detach();
    _cpu = cpu;
    if (_cpu)
        _cpu->Im2Chain().Add(this);
}

void SmucInterrupts::Detach()
{
    if (_cpu)
        _cpu->Im2Chain().Remove(this);
    _cpu = nullptr;
}

void SmucInterrupts::Write(uint16_t port, uint8_t value)
{
    SampleRtc();
    _pic.Write((port & 0x0100) != 0, value);
    Update();
}

uint8_t SmucInterrupts::Read(uint16_t port)
{
    SampleRtc();
    const uint8_t value = _pic.Read((port & 0x0100) != 0);
    Update();
    return value;
}

void SmucInterrupts::Update()
{
    if (_cpu)
        _cpu->Im2Chain().Update();
}

void SmucInterrupts::SampleRtc()
{
    _pic.SetInput(0, _rtc.IsIrqAsserted());
}

uint8_t SmucInterrupts::DaisyState(uint32_t t)
{
    (void)t;   // the clock reads the emulated time itself (PortDecoder::EmulatedMicroseconds)
    if (!(_pFFBA & kIntEnable))
        return 0;
    SampleRtc();
    return _pic.IntAsserted() ? kDaisyInt : 0;
}

bool SmucInterrupts::DaisyArmed() const
{
    if (!(_pFFBA & kIntEnable) || !_pic.Initialized())
        return false;
    const bool ir0Open = (_pic.GetState().imr & 0x01) == 0;
    return _pic.HasUnmaskedRequest() || (ir0Open && _rtc.IrqMayAssert());
}
