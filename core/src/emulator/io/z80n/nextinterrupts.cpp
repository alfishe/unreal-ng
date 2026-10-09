#include "stdafx.h"

#include "nextinterrupts.h"

#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"

NextInterruptSource::NextInterruptSource(EmulatorContext* context) : _context(context) {}

void NextInterruptSource::SetGeometry(const NextTiming& timing)
{
    _timing = timing;
}

void NextInterruptSource::Reset()
{
    _control = 0;
    if (onStacklessNmi)
        onStacklessNmi(false);
    _nmiReturn[0] = _nmiReturn[1] = 0;
    _enable0 = 0x81;  // the ULA interrupt and the expansion bus INT
    _enableCtc = _enableUart = 0;
    _dma[0] = _dma[1] = _dma[2] = 0;
    _lineControl = _lineValue = 0;
    _pending = _inService = 0;
    _lastT = 0;
}

/// region <NextREG>

bool NextInterruptSource::WriteNr(uint8_t reg, uint8_t value)
{
    switch (reg)
    {
        case kRegLineControl:
            _lineControl = value & 0x07;
            return true;
        case kRegLineValue:
            _lineValue = value;
            return true;
        case kRegControl:
            _control = value & 0xE9;  // vector 7:5, stackless NMI 3, mode 0
            if (onStacklessNmi)
                onStacklessNmi((_control & 0x08) != 0);
            if (!HardwareMode())
                _pending = _inService = 0;
            return true;
        case kRegNmiReturnLow:
        case kRegNmiReturnHigh:
            _nmiReturn[reg - kRegNmiReturnLow] = value;
            return true;
        case kRegEnable0:
            _enable0 = value & 0x83;
            return true;
        case kRegEnableCtc:
            _enableCtc = value;
            return true;
        case kRegEnableUart:
            _enableUart = value;
            return true;
        case kRegStatus0:
            // write 1 clears: bit 1 line, bit 0 ULA
            _pending &= static_cast<uint16_t>(~(((value & 2) ? 1u << kLine : 0u) | ((value & 1) ? 1u << kUla : 0u)));
            return true;
        case kRegStatusCtc:
            _pending &= static_cast<uint16_t>(~(static_cast<unsigned>(value) << kCtc0));
            return true;
        case kRegStatusUart:
            _pending &= static_cast<uint16_t>(~(((value & 0x01) ? 1u << kUart0Rx : 0u) | ((value & 0x04) ? 1u << kUart1Rx : 0u) |
                                                ((value & 0x10) ? 1u << kUart0Tx : 0u) | ((value & 0x40) ? 1u << kUart1Tx : 0u)));
            return true;
        case kRegDma0:
        case kRegDma0 + 1:
        case kRegDma2:
            _dma[reg - kRegDma0] = value;
            return true;
        default:
            return false;
    }
}

bool NextInterruptSource::ReadNr(uint8_t reg, uint8_t& value) const
{
    switch (reg)
    {
        case kRegLineControl:
            value = _lineControl;
            return true;
        case kRegLineValue:
            value = _lineValue;
            return true;
        case kRegControl:
            value = _control;
            return true;
        case kRegNmiReturnLow:
        case kRegNmiReturnHigh:
            value = _nmiReturn[reg - kRegNmiReturnLow];
            return true;
        case kRegEnable0:
            value = _enable0;
            return true;
        case kRegEnableCtc:
            value = _enableCtc;
            return true;
        case kRegEnableUart:
            value = _enableUart;
            return true;
        case kRegStatus0:
            value = static_cast<uint8_t>((((_pending >> kLine) & 1) << 1) | ((_pending >> kUla) & 1));
            return true;
        case kRegStatusCtc:
            value = static_cast<uint8_t>(_pending >> kCtc0);
            return true;
        case kRegStatusUart:
            value = static_cast<uint8_t>((((_pending >> kUart0Rx) & 1) << 0) | (((_pending >> kUart1Rx) & 1) << 2) |
                                         (((_pending >> kUart0Tx) & 1) << 4) | (((_pending >> kUart1Tx) & 1) << 6));
            return true;
        case kRegDma0:
        case kRegDma0 + 1:
        case kRegDma2:
            value = _dma[reg - kRegDma0];
            return true;
        default:
            return false;
    }
}

/// endregion

/// region <Sources>

/// Requests of the highest-priority source in service and everything below it wait for its RETI
uint16_t NextInterruptSource::Blocked() const
{
    if (!_inService)
        return 0;
    const uint16_t lowest = static_cast<uint16_t>(_inService & -_inService);
    return static_cast<uint16_t>(~(lowest - 1));
}

bool NextInterruptSource::Enabled(Source source) const
{
    switch (source)
    {
        case kUla:
            return (_enable0 & 0x01) != 0 && (_lineControl & 0x04) == 0;
        case kLine:
            return (_enable0 & 0x02) != 0 || (!HardwareMode() && (_lineControl & 0x02) != 0);
        case kUart0Rx:
            return (_enableUart & 0x03) != 0;
        case kUart1Rx:
            return (_enableUart & 0x0C) != 0;
        case kUart0Tx:
            return (_enableUart & 0x10) != 0;
        case kUart1Tx:
            return (_enableUart & 0x40) != 0;
        default:
            return source >= kCtc0 && source < kCtc0 + 8 && (_enableCtc & (1u << (source - kCtc0))) != 0;
    }
}

void NextInterruptSource::Raise(Source source)
{
    if (HardwareMode() && Enabled(source))
        _pending |= static_cast<uint16_t>(1u << source);
}

uint32_t NextInterruptSource::BaseT(uint32_t t) const
{
    const uint32_t multiplier = _context->emulatorState.current_z80_frequency_multiplier ? _context->emulatorState.current_z80_frequency_multiplier : 1u;
    return t / multiplier;
}

/// The T-state (base, from the frame start) where the line counter (copper vertical counter cvc of zxula_timing.vhd) is 0:
/// it counts from the paper's first line, steps at the ULA's pixel counter zero (11 pixels before the paper's first pixel)
/// and starts at the copper offset NR #64. The frame's (vc 0, hc 0) is placed so that the ULA interrupt sits at its
/// (line, pixel clock)
uint32_t NextInterruptSource::CounterOrigin() const
{
    const uint32_t intOffset = _timing.intLine * _timing.line + _timing.intHc / 2;
    const uint32_t vcZero = (_timing.intStart + _timing.frame - intOffset % _timing.frame) % _timing.frame;
    const uint32_t cvcZero = _timing.minVactive * _timing.line + (_timing.minHactive - 11) / 2;
    const uint32_t offset = (_counterOffset % (_timing.frame / _timing.line)) * _timing.line;
    return (vcZero + cvcZero + _timing.frame - offset) % _timing.frame;
}

uint32_t NextInterruptSource::LineStartT() const
{
    const uint32_t lines = _timing.frame / _timing.line;
    const uint32_t target = (static_cast<uint32_t>(_lineControl & 1) << 8) | _lineValue;
    const uint32_t counterLine = target == 0 ? lines - 1 : (target - 1) % lines;
    return (CounterOrigin() + counterLine * _timing.line) % _timing.frame;
}

uint16_t NextInterruptSource::CurrentLine() const
{
    const uint32_t baseT = BaseT(_context->pCore->GetZ80()->t) % _timing.frame;
    return static_cast<uint16_t>(((baseT + _timing.frame - CounterOrigin()) % _timing.frame) / _timing.line);
}

bool NextInterruptSource::PulseAt(uint32_t baseT, uint32_t start) const
{
    const uint32_t length = _timing.intLength;
    const uint32_t end = start + length;
    if (end <= _timing.frame)
        return baseT >= start && baseT < end;
    return baseT >= start || baseT < end - _timing.frame;  // wraps the frame end
}

/// Hardware mode: a source whose event position was crossed since the last call latches its request
void NextInterruptSource::Latch(uint32_t baseT)
{
    auto crossed = [&](uint32_t pos) {
        if (baseT >= _lastT)
            return pos > _lastT && pos <= baseT;
        return pos > _lastT || pos <= baseT;  // the frame rolled over between the calls
    };
    if (baseT == _lastT)
        return;
    if (crossed(_timing.intStart) && Enabled(kUla))
        _pending |= 1u << kUla;
    if ((_lineControl & 0x02) && crossed(LineStartT()) && Enabled(kLine))
        _pending |= 1u << kLine;
    _lastT = baseT;
}

/// endregion

/// region <IInterruptSource>

bool NextInterruptSource::IsIntAsserted(uint32_t t)
{
    if (_poller && HardwareMode())
        _poller();
    const uint32_t baseT = BaseT(t);
    if (!HardwareMode())
    {
        if (Enabled(kUla) && PulseAt(baseT, _timing.intStart))
            return true;
        return (_lineControl & 0x02) != 0 && PulseAt(baseT, LineStartT());
    }
    Latch(baseT);
    // the highest-priority request that no source in service outranks
    const uint16_t blocked = Blocked();
    return (_pending & ~blocked) != 0;
}

uint8_t NextInterruptSource::AcknowledgeInterrupt(uint32_t t)
{
    if (!HardwareMode())
        return 0xFF;
    Latch(BaseT(t));
    const uint16_t blocked = Blocked();
    const uint16_t ready = _pending & ~blocked;
    if (!ready)
        return VectorOf(kUla);  // a spurious acknowledge reads the ULA's vector
    unsigned source = 0;
    while (!(ready & (1u << source)))
        source++;
    _pending &= static_cast<uint16_t>(~(1u << source));
    _inService |= static_cast<uint16_t>(1u << source);
    return VectorOf(static_cast<Source>(source));
}

void NextInterruptSource::OnReti()
{
    if (!HardwareMode() || !_inService)
        return;
    _inService &= static_cast<uint16_t>(_inService - 1);  // the highest-priority source in service is done (lowest bit)
}

/// endregion
