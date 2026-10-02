#include "emulator/io/serial/uart16550.h"

#include <cstring>

#include "emulator/io/serial/serialpeer.h"

// ZX-Evo AVR firmware facts (rs232.c of every NedoPC and TS-Labs release;
// research notes: reference-evo-com-port.md §2, §3, §9):
//  - IIR always #01, IER stored but without effect: no interrupts
//  - OE: set when the receive FIFO is full, cleared by an FCR RX reset (from
//    2013; before only by the AVR restarting), never by reading LSR
//  - THRE and TEMT both mean "TX FIFO empty" (TS 2016-04: THRE = not full,
//    TEMT = the USART's TXC); RBR reads #00 when empty; NedoPC 2023: LSR
//    bit 7 = 8 or more bytes waiting
//  - MSR: CTS live (+ DCTS), DSR and DCD always 1, RI 0
//  - MCR & #1F, only RTS drives a pin (inverted before 2011-09): no loopback,
//    no auto flow control, TX ignores CTS
//  - baud 115200 / divisor; divisor 0 = 345600 (TS 2016-04: 230400); DLM
//    bit 7 = the AVR's own divisor: 691200 / (((DLM & #7F) << 8 | DLL) + 1)
//  - the USART starts 8N2 whatever LCR says, until the first LCR write
//  - a Z80 reset does not reset it (only the AVR's own restart)
//  - every access holds the Z80 on /WAIT while the AVR serves it (AccessCycles)

Uart16550::Params Uart16550::DefaultParams(Flavor flavor)
{
    if (flavor == Flavor::EvoAvr)
        return EvoAvrParams(kLatestAvr);
    Params p;
    p.flavor = flavor;
    return p;
}

Uart16550::Params Uart16550::EvoAvrParams(AvrFirmware firmware)
{
    Params p;
    p.flavor = Flavor::EvoAvr;
    p.avr = firmware;
    p.uartClockHz = 1843200;   // 115200 / divisor
    p.mcrMask = 0x1F;          // no AFE
    p.interrupts = false;
    switch (firmware)
    {
        case AvrFirmware::Base2010:
            p.dataPath = false;
            p.divisorResets = false;
            p.rawUbrr = false;
            p.oeClearedByFcr = false;
            break;
        case AvrFirmware::Base2011Apr:
            p.divisorResets = false;
            p.rawUbrr = false;
            p.oeClearedByFcr = false;
            p.rtsInverted = true;
            break;
        case AvrFirmware::Base2011May:
            p.oeClearedByFcr = false;
            p.rtsInverted = true;
            break;
        case AvrFirmware::Base2011Sep:
            p.oeClearedByFcr = false;
            break;
        case AvrFirmware::Base2013:
            break;
        case AvrFirmware::Base2023:
            p.halfFullBit = true;
            break;
        case AvrFirmware::Ts2013:
            p.rxDepth = 256;
            p.txDepth = 256;
            break;
        case AvrFirmware::Ts2016Feb:
            break;
        case AvrFirmware::Ts2016Apr:
            p.rxDepth = 511;
            p.txDepth = 255;
            p.threNotFull = true;
            p.temtIsTxc = true;
            p.divisor0Baud = 230400;
            break;
    }
    return p;
}

namespace
{
struct AvrName
{
    Uart16550::AvrFirmware firmware;
    const char* name;
};
constexpr AvrName kAvrNames[] = {
    {Uart16550::AvrFirmware::Base2010, "BASE2010"},       {Uart16550::AvrFirmware::Base2011Apr, "BASE2011-04"},
    {Uart16550::AvrFirmware::Base2011May, "BASE2011-05"}, {Uart16550::AvrFirmware::Base2011Sep, "BASE2011-09"},
    {Uart16550::AvrFirmware::Base2013, "BASE2013"},       {Uart16550::AvrFirmware::Base2023, "BASE2023"},
    {Uart16550::AvrFirmware::Ts2013, "TS2013"},           {Uart16550::AvrFirmware::Ts2016Feb, "TS2016-02"},
    {Uart16550::AvrFirmware::Ts2016Apr, "TS2016-04"},
};

bool SameText(const char* a, const char* b)
{
    for (; *a && *b; ++a, ++b)
    {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x = static_cast<char>(x - 32);
        if (y >= 'a' && y <= 'z') y = static_cast<char>(y - 32);
        if (x != y)
            return false;
    }
    return *a == 0 && *b == 0;
}
}  // namespace

bool Uart16550::ParseAvrFirmware(const char* text, AvrFirmware& out)
{
    if (!text || !*text || SameText(text, "BASECONF") || SameText(text, "LATEST"))
    {
        out = kLatestAvr;
        return true;
    }
    if (SameText(text, "TS"))
    {
        out = AvrFirmware::Ts2016Apr;
        return true;
    }
    for (const AvrName& n : kAvrNames)
    {
        if (SameText(text, n.name))
        {
            out = n.firmware;
            return true;
        }
    }
    return false;
}

const char* Uart16550::AvrFirmwareName(AvrFirmware firmware)
{
    for (const AvrName& n : kAvrNames)
    {
        if (n.firmware == firmware)
            return n.name;
    }
    return "?";
}

uint16_t Uart16550::RxDepth() const
{
    if (Evo())
        return _params.rxDepth;
    return (_fcr & 0x01) ? kFifoSize : 1;
}

uint16_t Uart16550::TxDepth() const
{
    if (Evo())
        return _params.txDepth;
    return (_fcr & 0x01) ? kFifoSize : 1;
}

uint32_t Uart16550::AccessCycles(uint8_t reg, bool read, uint64_t now)
{
    if (!Evo() || !_params.avrClockHz)
        return 0;
    // The AVR's clock, absolute, from the base-clock T-states
    const uint64_t avrNow = now * _params.avrClockHz / _baseClockHz;
    const uint64_t elapsed = avrNow > _avrRelease ? avrNow - _avrRelease : 0;
    const uint32_t loop = _params.loopCycles ? _params.loopCycles : 1;
    // The main loop looks at the flag once per pass, and a pass starts when
    // the previous access is released: right behind it a whole pass is left,
    // long after it anywhere in one. The interrupt steals its cycles from the
    // loop on top (reference-evo-com-port.md §3)
    const uint32_t phase = loop - static_cast<uint32_t>(elapsed % loop);
    const bool dlab = (_lcr & 0x80) != 0;
    const uint32_t service = !read ? _params.serviceWrite
                                   : ((reg & 7) == kRbrThr && !dlab ? _params.serviceRbr : _params.serviceRead);
    const uint32_t total = _params.isrCycles + phase + service;
    _avrRelease = avrNow + total;
    return total;
}

uint8_t Uart16550::ReadStub(uint8_t reg) const
{
    // The 2010 register file: values as written, no transfer behind them
    const bool dlab = (_lcr & 0x80) != 0;
    switch (reg & 7)
    {
        case kRbrThr: return dlab ? _dll : 0x00;
        case kIer: return dlab ? _dlm : 0x01;   // reads the IIR constant
        case kIirFcr: return _stubIir;          // the last FCR value
        case kLcr: return _lcr;
        case kMcr: return _mcr;
        case kLsr: return _lsr;
        case kMsr: return _msr;
        default: return _scr;
    }
}

void Uart16550::WriteStub(uint8_t reg, uint8_t value)
{
    const bool dlab = (_lcr & 0x80) != 0;
    switch (reg & 7)
    {
        case kRbrThr:
            if (dlab)
                _dll = value;
            break;
        case kIer:
            if (dlab)
                _dlm = value;
            else
                _ier = static_cast<uint8_t>(value & 0x0F);
            break;
        case kIirFcr: _stubIir = value; break;
        case kLcr: _lcr = value; break;
        case kMcr: _mcr = static_cast<uint8_t>(value & 0x1F); break;
        case kLsr: _lsr = value; break;   // writable in the stub
        case kMsr: _msr = value; break;
        default: _scr = value; break;
    }
}

Uart16550::Uart16550(const Params& params, uint32_t baseClockHz)
    : _params(params), _baseClockHz(baseClockHz ? baseClockHz : 3500000)
{
    Reset();
}

void Uart16550::SetPeer(ISerialPeer* peer)
{
    _peer = peer;
    NotifyLine();
}

void Uart16550::Reset()
{
    _ier = 0;
    _fcr = Evo() ? 0x01 : 0x00;
    _txc = false;
    _stubIir = 0x00;
    _lcr = 0;
    _mcr = 0;
    _lsr = kLsrThre | kLsrTemt;
    _scr = Evo() ? 0xFF : 0x00;
    _dll = (Evo() && !_params.divisorResets) ? 0 : 1;
    _dlm = 0;
    _rxCount = _rxHead = _txCount = _txHead = 0;
    _txShift = _rxShift = 0;
    _txBusy = _rxInFlight = false;
    _thrInterrupt = false;
    _lcrWritten = false;
    _txDoneAt = _rxArriveAt = 0;
    _msrLines = 0;
    _msr = 0;
    if (Evo() && !_params.dataPath)
        return;   // the 2010 register file: MSR reads what was written (0)
    UpdateModemStatus();
    _msr &= 0xF0;   // no deltas after reset
    if (_peer)
    {
        _peer->OnModemLines(false, false);
        _peer->OnLineSettings(Line());
    }
}

void Uart16550::Rebase(uint64_t now)
{
    // Keep what is in flight at the same distance from "now" on the new clock
    auto move = [&](uint64_t at) { return at > _lastNow ? now + (at - _lastNow) : now; };
    _txDoneAt = move(_txDoneAt);
    _rxArriveAt = move(_rxArriveAt);
    _lastNow = now;
}

SerialLine Uart16550::Line() const
{
    SerialLine line;
    line.baud = Baud();
    if (Evo() && !_lcrWritten)
    {
        line.dataBits = 8;
        line.parity = 'N';
        line.stopBits = 2;
        return line;
    }
    line.dataBits = static_cast<uint8_t>(5 + (_lcr & 0x03));
    line.stopBits = (_lcr & 0x04) ? 2 : 1;
    if (!(_lcr & 0x08))
        line.parity = 'N';
    else if ((_lcr & 0x20) && !Evo())
        line.parity = (_lcr & 0x10) ? 'S' : 'M';   // stick parity: EPS 1 = space, 0 = mark
    else
        line.parity = (_lcr & 0x10) ? 'E' : 'O';
    return line;
}

void Uart16550::NotifyLine()
{
    if (_peer)
        _peer->OnLineSettings(Line());
}

uint32_t Uart16550::Baud() const
{
    const uint32_t divisor = (static_cast<uint32_t>(_dlm) << 8) | _dll;
    if (Evo())
    {
        if (divisor == 0)
            return _params.divisor0Baud;                    // the firmware's "256000" computes to 345600
        uint32_t ubrr = 0;
        if ((_dlm & 0x80) && _params.rawUbrr)
            ubrr = ((static_cast<uint32_t>(_dlm) & 0x7F) << 8) | _dll;   // the AVR's own divisor
        else
        {
            const uint32_t wanted = 115200u / divisor;
            ubrr = wanted ? (691200u / wanted) - 1u : 0xFFFu;
        }
        return 691200u / (ubrr + 1u);
    }
    return _params.uartClockHz / (16u * (divisor ? divisor : 65536u));
}

uint32_t Uart16550::FrameBits() const
{
    if (Evo() && !_lcrWritten)
        return 11;   // 8N2: the USART's power-on setup
    const uint32_t data = 5u + (_lcr & 0x03);
    const uint32_t stop = (_lcr & 0x04) ? 2u : 1u;   // 1.5 for 5 bits: counted as 2
    const uint32_t parity = (_lcr & 0x08) ? 1u : 0u;
    return 1u + data + parity + stop;
}

uint64_t Uart16550::CharacterT() const
{
    const uint64_t baud = Baud() ? Baud() : 1;
    const uint64_t t = (static_cast<uint64_t>(FrameBits()) * _baseClockHz + baud - 1) / baud;
    return t ? t : 1;
}

bool Uart16550::RtsAsserted() const
{
    // Before 2011-09 the AVR drove the RTS pin the other way round
    const bool bit = (_mcr & kMcrRts) != 0;
    if (Evo() && _params.rtsInverted ? bit : !bit)
        return false;
    // Auto-RTS (16C550 AFE with RTS): the chip drops RTS at the trigger level
    if (!Evo() && (_mcr & kMcrAfe) && (_fcr & 0x01) && _rxCount >= RxTriggerLevel())
        return false;
    return true;
}

bool Uart16550::CtsForTx() const
{
    // Auto-CTS (16C550 AFE): the transmitter waits for CTS. The Evo AVR and a
    // 16550 without AFE just send
    if (Evo() || !(_mcr & kMcrAfe) || (_mcr & kMcrLoop))
        return true;
    return _peer ? _peer->Cts() : false;
}

uint8_t Uart16550::RxTriggerLevel() const
{
    static constexpr uint8_t levels[4] = {1, 4, 8, 14};
    return levels[(_fcr >> 6) & 0x03];
}

void Uart16550::PushRx(uint8_t byte)
{
    if (_rxCount >= RxDepth())
    {
        // Overrun: the byte on the line is lost, the FIFO keeps its contents
        _lsr |= kLsrOe;
        ++_overruns;
        return;
    }
    _rx[(_rxHead + _rxCount) % kMaxRx] = byte;
    ++_rxCount;
    ++_bytesIn;
}

uint8_t Uart16550::PopRx()
{
    if (_rxCount == 0)
    {
        // Evo: the AVR answers #00; a 16550's RBR still holds the last byte
        return Evo() ? 0x00 : _rx[(_rxHead + kMaxRx - 1) % kMaxRx];
    }
    const uint8_t b = _rx[_rxHead];
    _rxHead = static_cast<uint16_t>((_rxHead + 1) % kMaxRx);
    --_rxCount;
    return b;
}

void Uart16550::UpdateModemStatus()
{
    uint8_t lines = 0;
    if (Evo())
    {
        // Only CTS is a pin; DSR and DCD read 1, RI 0
        lines = kMsrDsr | kMsrDcd;
        if (_peer && _peer->Cts())
            lines |= kMsrCts;
    }
    else if (_mcr & kMcrLoop)
    {
        // Loopback: RTS -> CTS, DTR -> DSR, OUT1 -> RI, OUT2 -> DCD
        if (_mcr & kMcrRts) lines |= kMsrCts;
        if (_mcr & kMcrDtr) lines |= kMsrDsr;
        if (_mcr & kMcrOut1) lines |= kMsrRi;
        if (_mcr & kMcrOut2) lines |= kMsrDcd;
    }
    else if (_params.ctsOnly)
    {
        lines = kMsrDsr | kMsrDcd;
        if (_peer && _peer->Cts())
            lines |= kMsrCts;
    }
    else if (_peer)
    {
        if (_peer->Cts()) lines |= kMsrCts;
        if (_peer->Dsr()) lines |= kMsrDsr;
        if (_peer->Ri()) lines |= kMsrRi;
        if (_peer->Dcd()) lines |= kMsrDcd;
    }
    const uint8_t changed = static_cast<uint8_t>(lines ^ _msrLines);
    uint8_t deltas = 0;
    if (changed & kMsrCts) deltas |= kMsrDcts;
    if (!Evo())
    {
        if (changed & kMsrDsr) deltas |= kMsrDdsr;
        if ((changed & kMsrRi) && !(lines & kMsrRi)) deltas |= kMsrTeri;   // trailing edge of RI
        if (changed & kMsrDcd) deltas |= kMsrDdcd;
    }
    _msrLines = lines;
    _msr = static_cast<uint8_t>(lines | (_msr & 0x0F) | deltas);
}

void Uart16550::Advance(uint64_t now)
{
    if (now < _lastNow)
        Rebase(now);      // the machine's clock restarted (a reset, a snapshot load)
    _lastNow = now;
    if (Evo() && !_params.dataPath)
        return;   // the 2010 register file moves no bytes
    const uint64_t charT = CharacterT();
    const bool loop = !Evo() && (_mcr & kMcrLoop) != 0;

    // Transmitter: a character ends, the next one starts right behind it
    // (back to back while the FIFO has bytes), or now after a pause
    uint64_t startAt = now;
    while (true)
    {
        if (_txBusy)
        {
            if (_txDoneAt > now)
                break;
            _txBusy = false;
            _txc = true;
            ++_bytesOut;
            if (loop)
                PushRx(_txShift);
            else if (_peer)
                _peer->Transmit(_txShift);
            startAt = _txDoneAt;
        }
        if (_txCount == 0 || !CtsForTx())
            break;
        _txShift = _tx[_txHead];
        _txHead = static_cast<uint16_t>((_txHead + 1) % kMaxTx);
        --_txCount;
        _txBusy = true;
        _txDoneAt = startAt + charT;
        if (_txCount == 0)
        {
            // Evo: THRE and TEMT both say "TX FIFO empty"; a 16550's TEMT
            // waits for the shifter
            _lsr |= kLsrThre;
            if (Evo())
                _lsr |= kLsrTemt;
            _thrInterrupt = true;
        }
    }
    if (!_txBusy && _txCount == 0)
        _lsr |= kLsrThre | kLsrTemt;

    // Receiver: the peer starts a character only while RTS is asserted (a
    // flow-controlled ESP samples CTS before each byte); a started character
    // arrives a character time later whatever RTS does meanwhile. NedoOS on
    // the Evo pulses RTS (MCR 2, then 0) and gets about one byte per pulse
    if (!loop && _peer)
    {
        uint64_t rxStart = now;
        while (true)
        {
            if (_rxInFlight)
            {
                if (_rxArriveAt > now)
                    break;
                _rxInFlight = false;
                PushRx(_rxShift);
                rxStart = _rxArriveAt;
            }
            if (!_peer->HasByte() || (_peer->HonorsRts() && !RtsAsserted()))
                break;
            _rxShift = _peer->TakeByte();
            _rxInFlight = true;
            _rxArriveAt = rxStart + charT;
        }
    }
    UpdateModemStatus();
}

uint8_t Uart16550::LsrValue() const
{
    uint8_t value = static_cast<uint8_t>(_lsr & ~(kLsrDr | kLsrHalfFull));
    if (_rxCount > 0)
        value |= kLsrDr;
    if (Evo() && _params.halfFullBit && _rxCount >= 8)
        value |= kLsrHalfFull;   // NedoPC 2023: 8 or more bytes waiting
    if (Evo() && _params.threNotFull)
    {
        // TS 2016-04: THRE = room in the TX ring; TEMT = the USART's TXC
        value = static_cast<uint8_t>(value & ~(kLsrThre | kLsrTemt));
        if (_txCount < _params.txDepth)
            value |= kLsrThre;
        if (_txc)
            value |= kLsrTemt;
    }
    return value;
}

uint8_t Uart16550::Iir() const
{
    if (!_params.interrupts)
        return 0x01;   // Evo: constant, FIFO bits too
    const uint8_t fifo = (_fcr & 0x01) ? 0xC0 : 0x00;
    if ((_ier & 0x04) && (_lsr & kLsrOe))
        return static_cast<uint8_t>(fifo | 0x06);
    if ((_ier & 0x01) && _rxCount > 0 && (!(_fcr & 0x01) || _rxCount >= RxTriggerLevel()))
        return static_cast<uint8_t>(fifo | 0x04);
    if ((_ier & 0x01) && _rxCount > 0)
        return static_cast<uint8_t>(fifo | 0x0C);   // character timeout (simplified: data below the trigger level)
    if ((_ier & 0x02) && _thrInterrupt)
        return static_cast<uint8_t>(fifo | 0x02);
    if ((_ier & 0x08) && (_msr & 0x0F))
        return static_cast<uint8_t>(fifo | 0x00);
    return static_cast<uint8_t>(fifo | 0x01);
}

bool Uart16550::InterruptActive() const
{
    return _params.interrupts && (_mcr & kMcrOut2) && !(Iir() & 0x01);
}

uint8_t Uart16550::Read(uint8_t reg, uint64_t now)
{
    if (Evo() && !_params.dataPath)
        return ReadStub(reg);
    Advance(now);
    const bool dlab = (_lcr & 0x80) != 0;
    uint8_t value = 0xFF;
    switch (reg & 0x07)
    {
        case kRbrThr:
            if (dlab)
                return _dll;
            value = PopRx();
            Advance(now);   // a freed FIFO slot may let auto-RTS through again
            return value;
        case kIer:
            return dlab ? _dlm : _ier;
        case kIirFcr:
            value = Iir();
            if ((value & 0x0F) == 0x02)
                _thrInterrupt = false;   // reading IIR with THRE as the source clears it
            return value;
        case kLcr:
            return _lcr;
        case kMcr:
            return _mcr;
        case kLsr:
            value = LsrValue();
            if (!Evo())
                _lsr &= static_cast<uint8_t>(~kLsrOe);   // 16550: OE clears on read (Evo: only by an FCR RX reset)
            return value;
        case kMsr:
            value = _msr;
            _msr &= 0xF0;   // deltas clear on read
            return value;
        default:
            return _scr;
    }
}

void Uart16550::Write(uint8_t reg, uint8_t value, uint64_t now)
{
    if (Evo() && !_params.dataPath)
        return WriteStub(reg, value);
    Advance(now);
    const bool dlab = (_lcr & 0x80) != 0;
    switch (reg & 0x07)
    {
        case kRbrThr:
            if (dlab)
            {
                _dll = value;
                NotifyLine();
                break;
            }
            {
                if (_txCount < TxDepth())
                {
                    _tx[(_txHead + _txCount) % kMaxTx] = value;
                    ++_txCount;
                }
                // A full FIFO drops the byte silently
                _lsr &= static_cast<uint8_t>(~(kLsrThre | kLsrTemt));
                _thrInterrupt = false;
            }
            break;
        case kIer:
            if (dlab)
            {
                _dlm = value;
                NotifyLine();
            }
            else
            {
                _ier = static_cast<uint8_t>(value & 0x0F);
                if (_params.interrupts && (_ier & 0x02) && (_lsr & kLsrThre))
                    _thrInterrupt = true;   // enabling THRE with the holding register empty raises it
            }
            break;
        case kIirFcr:
        {
            if (Evo())
            {
                // The AVR acts only with bit 0 set: bit 1 empties RX and clears
                // OE, bit 2 empties TX
                if (value & 0x01)
                {
                    if (value & 0x02)
                    {
                        _rxCount = _rxHead = 0;
                        if (_params.oeClearedByFcr)
                            _lsr &= static_cast<uint8_t>(~kLsrOe);
                    }
                    if (value & 0x04)
                    {
                        _txCount = _txHead = 0;
                        _lsr |= kLsrThre | kLsrTemt;
                    }
                    _fcr = static_cast<uint8_t>(value & 0xC9);
                }
                break;
            }
            const bool wasEnabled = (_fcr & 0x01) != 0;
            const bool enabled = (value & 0x01) != 0;
            if ((value & 0x02) || wasEnabled != enabled)
                _rxCount = _rxHead = 0;
            if ((value & 0x04) || wasEnabled != enabled)
            {
                _txCount = _txHead = 0;
                _lsr |= kLsrThre;
                if (!_txBusy)
                    _lsr |= kLsrTemt;
            }
            _fcr = static_cast<uint8_t>(value & 0xC9);
            break;
        }
        case kLcr:
            _lcr = value;
            _lcrWritten = true;
            NotifyLine();
            break;
        case kMcr:
            _mcr = static_cast<uint8_t>(value & _params.mcrMask);
            if (_peer && (Evo() || !(_mcr & kMcrLoop)))
                _peer->OnModemLines(RtsAsserted(), (_mcr & kMcrDtr) != 0);
            break;
        case kLsr:
        case kMsr:
            break;   // read-only
        default:
            _scr = value;
            break;
    }
    Advance(now);   // a new THR byte or RTS may start a transfer at once
}

Uart16550::View Uart16550::GetView() const
{
    View v;
    v.ier = _ier;
    v.iir = Iir();
    v.fcr = _fcr;
    v.lcr = _lcr;
    v.mcr = _mcr;
    v.lsr = (Evo() && !_params.dataPath) ? _lsr : LsrValue();
    v.msr = _msr;
    v.scr = _scr;
    v.divisor = static_cast<uint16_t>((_dlm << 8) | _dll);
    v.rxCount = _rxCount;
    v.txCount = _txCount;
    v.txBusy = _txBusy;
    v.bytesIn = _bytesIn;
    v.bytesOut = _bytesOut;
    v.overruns = _overruns;
    return v;
}

void Uart16550::SaveState(State& out) const
{
    std::memset(&out, 0, sizeof(out));
    out.ier = _ier;
    out.fcr = _fcr;
    out.lcr = _lcr;
    out.mcr = _mcr;
    out.lsr = _lsr;
    out.msr = _msr;
    out.scr = _scr;
    out.dll = _dll;
    out.dlm = _dlm;
    out.rxCount = _rxCount;
    out.rxHead = _rxHead;
    out.txCount = _txCount;
    out.txHead = _txHead;
    out.txShift = _txShift;
    out.txBusy = _txBusy ? 1 : 0;
    out.rxShift = _rxShift;
    out.rxInFlight = _rxInFlight ? 1 : 0;
    out.msrLines = _msrLines;
    out.thrInt = _thrInterrupt ? 1 : 0;
    out.lcrWritten = _lcrWritten ? 1 : 0;
    out.txc = _txc ? 1 : 0;
    out.stubIir = _stubIir;
    out.avrRelease = _avrRelease;
    std::memcpy(out.rx, _rx.data(), kMaxRx);
    std::memcpy(out.tx, _tx.data(), kMaxTx);
    out.txDoneAt = _txDoneAt;
    out.rxArriveAt = _rxArriveAt;
    out.lastNow = _lastNow;
    out.bytesIn = _bytesIn;
    out.bytesOut = _bytesOut;
    out.overruns = _overruns;
}

void Uart16550::LoadState(const State& in)
{
    _ier = in.ier;
    _fcr = in.fcr;
    _lcr = in.lcr;
    _mcr = in.mcr;
    _lsr = in.lsr;
    _msr = in.msr;
    _scr = in.scr;
    _dll = in.dll;
    _dlm = in.dlm;
    _rxCount = static_cast<uint16_t>(in.rxCount > kMaxRx ? kMaxRx : in.rxCount);
    _rxHead = static_cast<uint16_t>(in.rxHead % kMaxRx);
    _txCount = static_cast<uint16_t>(in.txCount > kMaxTx ? kMaxTx : in.txCount);
    _txHead = static_cast<uint16_t>(in.txHead % kMaxTx);
    _txc = in.txc != 0;
    _stubIir = in.stubIir;
    _avrRelease = in.avrRelease;
    _txShift = in.txShift;
    _txBusy = in.txBusy != 0;
    _rxShift = in.rxShift;
    _rxInFlight = in.rxInFlight != 0;
    _msrLines = in.msrLines;
    _thrInterrupt = in.thrInt != 0;
    _lcrWritten = in.lcrWritten != 0;
    std::memcpy(_rx.data(), in.rx, kMaxRx);
    std::memcpy(_tx.data(), in.tx, kMaxTx);
    _txDoneAt = in.txDoneAt;
    _rxArriveAt = in.rxArriveAt;
    _lastNow = in.lastNow;
    _bytesIn = in.bytesIn;
    _bytesOut = in.bytesOut;
    _overruns = in.overruns;
}
