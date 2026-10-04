#include "stdafx.h"

#include "usart8251.h"

#include <algorithm>

#include "emulator/io/serial/serialpeer.h"

Usart8251::Usart8251(uint32_t baseClockHz) : _baseHz(baseClockHz ? baseClockHz : 3500000)
{
}

void Usart8251::SetPeer(ISerialPeer* peer)
{
    _peer = peer;
    _toldLine = SerialLine{0, 0, 0, 0};
    if (!_peer)
        return;
    _toldRts = Rts();
    _toldDtr = Dtr();
    _peer->OnModemLines(_toldRts, _toldDtr);
    TellLine();
}

void Usart8251::SetState(const State& state)
{
    _state = state;
    // The peer follows the restored lines (it is restored on its own)
    _toldLine = SerialLine{0, 0, 0, 0};
    TellModemLines();
}

void Usart8251::InternalReset()
{
    // Back to the mode word; nothing on the lines, both buffers empty, DTR / RTS inactive
    const uint8_t latch = _state.boardLatch;
    const uint64_t lastNow = _state.lastNow;
    const uint64_t in = _state.bytesIn, out = _state.bytesOut, overruns = _state.overruns;
    _state = State();
    _state.boardLatch = latch;
    _state.lastNow = lastNow;
    _state.bytesIn = in;
    _state.bytesOut = out;
    _state.overruns = overruns;
    TellModemLines();
}

void Usart8251::Reset(uint64_t now)
{
    _state.lastNow = now;
    InternalReset();
}

uint32_t Usart8251::BaudFactor() const
{
    switch (_state.mode & 0x03)
    {
        case 2:
            return 16;
        case 3:
            return 64;
        default:
            return 1;
    }
}

uint8_t Usart8251::StopHalfBits() const
{
    switch (_state.mode >> 6)
    {
        case 2:
            return 3;
        case 3:
            return 4;
        default:
            return 2;   // 01 one stop bit; 00 is invalid on the chip, taken as one
    }
}

uint32_t Usart8251::FrameBits() const
{
    const uint32_t parity = (_state.mode & 0x10) ? 1u : 0u;
    return 1u + DataBits() + parity + (StopHalfBits() + 1u) / 2u;
}

uint32_t Usart8251::Baud() const
{
    if (!IsAsync() || !_clock)
        return 0;
    const ClockRate rate = _clock();
    if (!rate.hz || !rate.divisor)
        return 0;
    const uint64_t div = static_cast<uint64_t>(rate.divisor) * BaudFactor();
    return static_cast<uint32_t>((rate.hz + div / 2) / div);
}

uint64_t Usart8251::CharacterT() const
{
    if (!IsAsync() || !_clock)
        return 0;
    const ClockRate rate = _clock();
    if (!rate.hz || !rate.divisor)
        return 0;
    // Half bits: start + data + parity count two halves each, the stop bits 2 / 3 / 4
    const uint64_t parity = (_state.mode & 0x10) ? 1u : 0u;
    const uint64_t halfBits = 2u * (1u + DataBits() + parity) + StopHalfBits();
    const uint64_t num = halfBits * BaudFactor() * rate.divisor * _baseHz;
    const uint64_t den = 2u * static_cast<uint64_t>(rate.hz);
    return std::max<uint64_t>(1, (num + den / 2) / den);
}

SerialLine Usart8251::Line() const
{
    SerialLine line;
    line.baud = Baud();
    line.dataBits = DataBits();
    line.parity = (_state.mode & 0x10) ? ((_state.mode & 0x20) ? 'E' : 'O') : 'N';
    line.stopBits = StopHalfBits() > 2 ? 2 : 1;
    return line;
}

void Usart8251::TellLine()
{
    if (!_peer || !IsAsync())
        return;
    const SerialLine line = Line();
    if (line.baud == 0 || line == _toldLine)
        return;
    _toldLine = line;
    _peer->OnLineSettings(line);
}

void Usart8251::TellModemLines()
{
    if (!_peer || (Rts() == _toldRts && Dtr() == _toldDtr))
        return;
    _toldRts = Rts();
    _toldDtr = Dtr();
    _peer->OnModemLines(_toldRts, _toldDtr);
}

bool Usart8251::CtsIn() const
{
    if (!_peer)
        return false;   // an open RS-232 receiver input reads inactive
    return _peer->MirrorsModemLines() ? Rts() : _peer->Cts();
}

bool Usart8251::DsrIn() const
{
    if (!_peer)
        return false;
    return _peer->MirrorsModemLines() ? Dtr() : _peer->Dsr();
}

bool Usart8251::DcdIn() const
{
    if (!_peer)
        return false;
    return _peer->MirrorsModemLines() ? Dtr() : _peer->Dcd();
}

bool Usart8251::RiIn() const
{
    if (!_peer)
        return false;
    // The Profi's TESTCOM.COM plug wires RTS to CTS and RI (PLUSDOC comport.txt)
    return _peer->MirrorsModemLines() ? Rts() : _peer->Ri();
}

uint8_t Usart8251::Status() const
{
    uint8_t status = _state.errors;
    if (!_state.txFull)
        status |= kTxRdy;
    if (_state.rxReady)
        status |= kRxRdy;
    if (!_state.txFull && !_state.txBusy)
        status |= kTxEmpty;
    if (DsrIn())
        status |= kDsr;
    return status;
}

void Usart8251::StartTx(uint64_t at)
{
    // The buffer moves to the shifter while TxEN is set and CTS is asserted
    if (_state.txBusy || !_state.txFull || !(_state.command & kTxEn) || !CtsIn())
        return;
    const uint64_t charT = CharacterT();
    if (!charT)
        return;   // no clock: the character waits
    _state.txShift = _state.txBuffer;
    _state.txFull = 0;
    _state.txBusy = 1;
    _state.txDoneAt = at + charT;
}

void Usart8251::StartRx(uint64_t at)
{
    // The peer starts a character while RTS lets it (unless it ignores RTS) and the line is free
    if (_state.rxBusy || !_peer || !_peer->HasByte() || (_peer->HonorsRts() && !Rts()))
        return;
    const uint64_t charT = CharacterT();
    if (!charT)
        return;
    _state.rxShift = _peer->TakeByte();
    _state.rxBusy = 1;
    _state.rxDoneAt = at + charT;
}

void Usart8251::Advance(uint64_t now)
{
    if (now < _state.lastNow)
    {
        // The caller's clock restarted: the characters on the line keep their remaining time
        const uint64_t back = _state.lastNow - now;
        _state.txDoneAt = _state.txDoneAt > back ? _state.txDoneAt - back : 0;
        _state.rxDoneAt = _state.rxDoneAt > back ? _state.rxDoneAt - back : 0;
    }
    _state.lastNow = now;
    TellLine();

    // The events in time order: a character leaves the TX line (the next one starts behind it; a loopback echo
    // starts on the RX line at that instant), a character is complete on the RX line
    while (true)
    {
        const uint64_t txAt = _state.txBusy ? _state.txDoneAt : UINT64_MAX;
        const uint64_t rxAt = _state.rxBusy ? _state.rxDoneAt : UINT64_MAX;
        const uint64_t at = std::min(txAt, rxAt);
        if (at > now)
            break;
        if (txAt <= rxAt)
        {
            _state.txBusy = 0;
            ++_state.bytesOut;
            if (_peer)
                _peer->Transmit(static_cast<uint8_t>(_state.txShift & DataMask()));
            StartTx(at);
        }
        else
        {
            _state.rxBusy = 0;
            if (_state.command & kRxE)
            {
                if (_state.rxReady)
                {
                    _state.errors |= kOe;   // the previous byte was not read: it is lost
                    ++_state.overruns;
                }
                _state.rxData = static_cast<uint8_t>(_state.rxShift & DataMask());
                _state.rxReady = 1;
                ++_state.bytesIn;
            }
        }
        StartRx(at);
    }
    StartTx(now);
    StartRx(now);
}

void Usart8251::WriteControl(uint8_t value, uint64_t now)
{
    switch (static_cast<Expect>(_state.expect))
    {
        case Expect::Mode:
            _state.mode = value;
            // Synchronous (baud factor 00): one sync character (SCS, bit 7) or two follow
            _state.expect = static_cast<uint8_t>(IsAsync() ? Expect::Command : Expect::Sync1);
            TellLine();
            return;
        case Expect::Sync1:
            _state.sync1 = value;
            _state.expect = static_cast<uint8_t>((_state.mode & 0x80) ? Expect::Command : Expect::Sync2);
            return;
        case Expect::Sync2:
            _state.sync2 = value;
            _state.expect = static_cast<uint8_t>(Expect::Command);
            return;
        case Expect::Command:
        default:
            break;
    }
    if (value & kIr)
    {
        InternalReset();
        return;
    }
    if (value & kEr)
        _state.errors = 0;
    _state.command = static_cast<uint8_t>(value & ~(kIr | kEr));
    TellModemLines();
    // TxEN / RTS may have let a character start
    StartTx(now);
    StartRx(now);
}

void Usart8251::Write(uint8_t reg, uint8_t value, uint64_t now)
{
    Advance(now);
    if ((reg & 1) == kControl)
    {
        WriteControl(value, now);
        return;
    }
    // A byte written while the buffer is full replaces it
    _state.txBuffer = value;
    _state.txFull = 1;
    StartTx(now);
}

uint8_t Usart8251::Read(uint8_t reg, uint64_t now)
{
    Advance(now);
    if ((reg & 1) == kControl)
        return Status();
    _state.rxReady = 0;
    return _state.rxData;
}
