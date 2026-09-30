#include "emulator/io/serial/uart16550.h"

#include <cstring>

#include "emulator/io/serial/serialpeer.h"

// ZX-Evo BaseConf facts (AVR firmware rs232.c, FPGA zports.v / zwait.v; the
// research notes are in the network TDD §7.1):
//  - IIR always #01, IER stored but without effect: no interrupts
//  - OE clears only by an FCR RX reset; LSR bit 7 = RX FIFO half full (8+)
//  - THRE and TEMT both mean "TX FIFO empty"; RBR reads #00 when empty
//  - MSR: CTS live (+ DCTS), DSR and DCD always 1, RI 0
//  - MCR & #1F, only RTS drives a pin: no loopback, no auto flow control,
//    TX ignores CTS
//  - baud 115200 / divisor; divisor 0 = 345600; DLM bit 7 = the AVR's own
//    divisor: 691200 / (((DLM & #7F) << 8 | DLL) + 1)
//  - the USART starts 8N2 whatever LCR says, until the first LCR write
//  - a Z80 reset does not reset it (only the AVR's own hard reset)
//  - every access holds the Z80 on /WAIT while the AVR serves it

Uart16550::Params Uart16550::DefaultParams(Flavor flavor)
{
    Params p;
    p.flavor = flavor;
    if (flavor == Flavor::EvoAvr)
    {
        p.uartClockHz = 1843200;   // 115200 / divisor
        p.mcrMask = 0x1F;          // no AFE
        p.interrupts = false;
        // ~15 us of AVR service per access (5 SPI bytes at 5.53 MHz plus the
        // main loop; estimated, no measurement in the sources): 52 T at 3.5 MHz
        p.accessWaitT = 52;
    }
    return p;
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
    _lcr = 0;
    _mcr = 0;
    _lsr = kLsrThre | kLsrTemt;
    _scr = Evo() ? 0xFF : 0x00;
    _dll = 1;
    _dlm = 0;
    _rxCount = _rxHead = _txCount = _txHead = 0;
    _txShift = _rxShift = 0;
    _txBusy = _rxInFlight = false;
    _thrInterrupt = false;
    _lcrWritten = false;
    _txDoneAt = _rxArriveAt = 0;
    _msrLines = 0;
    _msr = 0;
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
            return 345600;                                  // the firmware's "256000" computes to this
        uint32_t ubrr = 0;
        if (_dlm & 0x80)
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
    if (!(_mcr & kMcrRts))
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
    const uint8_t depth = (Evo() || (_fcr & 0x01)) ? kFifoSize : 1;
    if (_rxCount >= depth)
    {
        // Overrun: the byte on the line is lost, the FIFO keeps its contents
        _lsr |= kLsrOe;
        ++_overruns;
        return;
    }
    _rx[(_rxHead + _rxCount) % kFifoSize] = byte;
    ++_rxCount;
    ++_bytesIn;
}

uint8_t Uart16550::PopRx()
{
    if (_rxCount == 0)
    {
        // Evo: the AVR answers #00; a 16550's RBR still holds the last byte
        return Evo() ? 0x00 : _rx[(_rxHead + kFifoSize - 1) % kFifoSize];
    }
    const uint8_t b = _rx[_rxHead];
    _rxHead = static_cast<uint8_t>((_rxHead + 1) % kFifoSize);
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
        _txHead = static_cast<uint8_t>((_txHead + 1) % kFifoSize);
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
            if (!_peer->HasByte() || !RtsAsserted())
                break;
            _rxShift = _peer->TakeByte();
            _rxInFlight = true;
            _rxArriveAt = rxStart + charT;
        }
    }
    UpdateModemStatus();
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
            value = static_cast<uint8_t>(_lsr & ~(kLsrDr | 0x80));
            if (_rxCount > 0)
                value |= kLsrDr;
            if (Evo() && _rxCount >= 8)
                value |= 0x80;   // Evo: RX FIFO half full
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
                const uint8_t depth = (Evo() || (_fcr & 0x01)) ? kFifoSize : 1;
                if (_txCount < depth)
                {
                    _tx[(_txHead + _txCount) % kFifoSize] = value;
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
                _peer->OnModemLines((_mcr & kMcrRts) != 0, (_mcr & kMcrDtr) != 0);
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
    v.lsr = static_cast<uint8_t>((_lsr & ~(kLsrDr | 0x80)) | (_rxCount ? kLsrDr : 0) |
                                 ((Evo() && _rxCount >= 8) ? 0x80 : 0));
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
    std::memcpy(out.rx, _rx.data(), kFifoSize);
    std::memcpy(out.tx, _tx.data(), kFifoSize);
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
    _rxCount = static_cast<uint8_t>(in.rxCount > kFifoSize ? kFifoSize : in.rxCount);
    _rxHead = static_cast<uint8_t>(in.rxHead % kFifoSize);
    _txCount = static_cast<uint8_t>(in.txCount > kFifoSize ? kFifoSize : in.txCount);
    _txHead = static_cast<uint8_t>(in.txHead % kFifoSize);
    _txShift = in.txShift;
    _txBusy = in.txBusy != 0;
    _rxShift = in.rxShift;
    _rxInFlight = in.rxInFlight != 0;
    _msrLines = in.msrLines;
    _thrInterrupt = in.thrInt != 0;
    _lcrWritten = in.lcrWritten != 0;
    std::memcpy(_rx.data(), in.rx, kFifoSize);
    std::memcpy(_tx.data(), in.tx, kFifoSize);
    _txDoneAt = in.txDoneAt;
    _rxArriveAt = in.rxArriveAt;
    _lastNow = in.lastNow;
    _bytesIn = in.bytesIn;
    _bytesOut = in.bytesOut;
    _overruns = in.overruns;
}
