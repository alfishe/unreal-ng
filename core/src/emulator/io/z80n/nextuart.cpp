#include "stdafx.h"

#include "nextuart.h"

#include <algorithm>
#include <bit>

#include "emulator/io/serial/serialpeer.h"

namespace
{
constexpr uint8_t kSelectBit = 0x40, kPrescalerFollows = 0x10, kPrescalerMsbMask = 0x07, kHalfSelect = 0x80, kHalfMask = 0x7F;
constexpr unsigned kRxRegister = 0, kSelectRegister = 1, kFrameRegister = 2;  // 3 is the Tx / status register (the default)
constexpr unsigned kHalfBits = 7;
}  // namespace

void NextUart::ResetFifos(Channel& c)
{
    _pollClean = false;
    c.rxHead = c.rxCount = 0;
    c.txHead = c.txCount = 0;
    c.txBusy = c.rxBusy = false;
    c.errOverflow = c.errFraming = false;
    // the receiver is reset to its pause state (uart_rx.vhd S_PAUSE): a frame or a break in progress is dropped
    c.rxLow = c.lowFramed = c.breakIn = false;
}

void NextUart::Reset(bool hard)
{
    _pollClean = false;
    for (Channel& c : _ch)
    {
        ResetFifos(c);
        if (hard)
        {
            c.prescalerMsb = 0;
            c.prescalerLsb = kDefaultPrescaler;
            c.frame = kDefaultFrame;
        }
        c.rtsOut = true;
        c.rxLevel = c.txLevel = false;
        c.sent = c.received = c.lost = 0;
        c.toldValid = false;
        c.rxFreeAt = c.txFreeAt = c.lastLook = 0;
        c.lowPaused = false;
        if (c.Live() && WireOf(c) == c.peer)
            c.peer->OnModemLines(true, true);
        TellBreak(c);
    }
    _selected = 0;
    _now = 0;
}

uint64_t NextUart::FrameClocks(const Channel& c) const
{
    const unsigned bits = 1 + DataBits(c) + ((c.frame & kFrameParity) ? 1u : 0u) + ((c.frame & kFrameTwoStops) ? 2u : 1u);
    return static_cast<uint64_t>(bits) * Prescaler(c);
}

uint64_t NextUart::FrameClocks(unsigned channel) const
{
    return FrameClocks(_ch[channel]);
}

uint64_t NextUart::StopSampleClocks(const Channel& c) const
{
    // uart_rx.vhd: S_IDLE loads half a bit, S_START ends there; every data bit, the parity bit and the first stop bit take one bit
    // time more, the stop bit being sampled at the end of its timer
    const uint64_t bit = Prescaler(c);
    const unsigned frameBits = DataBits(c) + ((c.frame & kFrameParity) ? 1u : 0u) + 1u;
    return bit / 2 + frameBits * bit;
}

uint64_t NextUart::BreakClocks(const Channel& c) const
{
    // the error state shifts the line into a register it set to ones, one bit time per shift: eight zeros
    return StopSampleClocks(c) + 8ull * Prescaler(c);
}

void NextUart::SetPeer(unsigned channel, ISerialPeer* peer)
{
    Channel& c = _ch[channel];
    c.peer = peer;
    c.toldValid = false;
    c.breakTold = false;
    if (!peer)
        return;
    if (_joyOn && _joyChannel == channel)
    {
        peer->OnModemLines(false, true);  // the joystick connector has the channel: the module's ready-to-receive pin is forced not ready
    }
    else if (c.Live())
    {
        peer->OnModemLines(c.rtsOut, true);
        TellPeer(c);
        TellBreak(c);
    }
}

void NextUart::SetJoystickPeer(ISerialPeer* peer)
{
    if (_joyPeer == peer)
        return;
    _joyPeer = peer;
    if (_joyOn)
    {
        Channel& c = _ch[_joyChannel];
        c.toldValid = false;
        c.breakTold = false;
        c.rxLow = c.breakIn = c.lowFramed = false;
        if (peer)
        {
            TellPeer(c);
            TellBreak(c);
        }
    }
}

void NextUart::SetJoystickRoute(bool on, unsigned channel)
{
    channel = channel ? 1u : 0u;
    if (on == _joyOn && (!on || channel == _joyChannel))
        return;
    const bool wasOn = _joyOn;
    const unsigned was = _joyChannel;
    _joyOn = on;
    _joyChannel = channel;
    // zxnext.vhd 3340-3350: a channel the connector takes has its module cut off (Tx idle, RTR not ready, no data); the module
    // returns when the connector lets go. The line state of the new other end starts from scratch
    auto rewire = [this](unsigned ch, bool toJoystick) {
        Channel& c = _ch[ch];
        c.toldValid = false;
        c.breakTold = false;
        c.rxLow = c.breakIn = c.lowFramed = c.lowPaused = false;
        if (toJoystick)
        {
            if (c.peer)
            {
                c.peer->OnModemLines(false, true);
                c.peer->OnBreak(false);
            }
            if (_joyPeer)
            {
                TellPeer(c);
                TellBreak(c);
            }
        }
        else if (c.Live())
        {
            c.peer->OnModemLines(c.rtsOut, true);
            TellPeer(c);
            TellBreak(c);
        }
    };
    if (wasOn)
        rewire(was, false);
    if (on)
        rewire(channel, true);
}

void NextUart::TellBreak(Channel& c)
{
    const bool held = (c.frame & kFrameBreak) != 0;
    if (c.breakTold == held)
        return;
    c.breakTold = held;
    if (ISerialPeer* peer = WireOf(c))
        peer->OnBreak(held);
}

void NextUart::TellPeer(Channel& c)
{
    ISerialPeer* const peer = WireOf(c);
    if (!peer)
        return;
    SerialLine line;
    // CLK_28 / prescaler is never exactly a standard rate (243 gives 115226): the line is told the nearest standard one
    // within 3 %, which is what a module's own tolerance accepts
    static const uint32_t kStandard[] = {300,   600,    1200,   2400,   4800,    9600,    14400,   19200,   28800,   31250,   38400,  57600,
                                         76800, 115200, 230400, 250000, 460800,  500000,  921600,  1000000, 1500000, 2000000, 3000000};
    const uint32_t raw = static_cast<uint32_t>(_systemClockHz / Prescaler(c));
    line.baud = raw;
    for (uint32_t standard : kStandard)
    {
        const uint32_t diff = raw > standard ? raw - standard : standard - raw;
        if (diff * 100 <= standard * 3)
        {
            line.baud = standard;
            break;
        }
    }
    line.dataBits = static_cast<uint8_t>(DataBits(c));
    line.parity = (c.frame & kFrameParity) ? ((c.frame & kFrameOdd) ? 'O' : 'E') : 'N';
    line.stopBits = (c.frame & kFrameTwoStops) ? 2 : 1;
    if (c.toldValid && line == c.told)
        return;
    c.told = line;
    c.toldValid = true;
    peer->OnLineSettings(line);
}

void NextUart::StartTx(Channel& c, uint64_t at)
{
    c.txByte = c.tx[c.txHead];
    c.txHead = static_cast<uint8_t>((c.txHead + 1) % kTxFifo);
    c.txCount--;
    c.txBusy = true;
    c.txDone = at + FrameClocks(c);
}

void NextUart::StepLine(Channel& c, ISerialPeer* peer, uint64_t now)
{
    const bool low = peer && peer->BreakHeld();
    if (c.lowPaused)
    {
        if (!low)
            c.lowPaused = false;  // S_PAUSE: the receiver went on when the line rose
        return;
    }
    if (low && !c.rxLow)
    {
        c.rxLow = true;
        c.lowSince = now;
        c.lowFramed = false;
    }
    if (!c.rxLow)
        return;
    const uint64_t elapsed = now > c.lowSince ? now - c.lowSince : 0;
    const uint64_t stop = StopSampleClocks(c);
    if (low)
    {
        if (!c.lowFramed && elapsed >= stop)
        {
            c.lowFramed = true;
            c.errFraming = true;  // o_err_framing (the byte is thrown away); sticky until the status read
        }
        if (elapsed >= BreakClocks(c))
            c.breakIn = true;  // o_err_break: held while the line stays low
        return;
    }
    // the line rose
    c.rxLow = false;
    c.breakIn = false;
    const bool counted = c.lowFramed;   // the frame error was counted when the stop bit's sample point passed
    c.lowFramed = false;
    if (counted)
        return;
    if (elapsed >= stop)
    {
        c.errFraming = true;  // first seen at the release (the line was not looked at in between)
        return;
    }
    const uint64_t bit = Prescaler(c);
    if (elapsed < bit / 2)
        return;  // a pulse shorter than half a bit: S_START went back to idle
    // a short low pulse is a frame with zero bits in it: the data bits sampled while the line was low are 0, the rest 1
    const unsigned n = DataBits(c);
    unsigned zeros = 0;
    for (unsigned j = 1; j <= n; j++)
        if (bit / 2 + j * bit < elapsed)
            zeros++;
    const unsigned mask = (1u << n) - 1u;
    const uint8_t value = static_cast<uint8_t>((0xFFu << zeros) & mask);
    if (c.frame & kFrameParity)
    {
        const bool parityBitHigh = !(bit / 2 + (n + 1) * bit < elapsed);
        const bool odd = (c.frame & kFrameOdd) != 0;
        const bool expected = odd != ((std::popcount(static_cast<unsigned>(value)) & 1) != 0);
        if (parityBitHigh != expected)
        {
            c.errFraming = true;  // o_err_parity: counted with the framing errors
            return;
        }
    }
    if (!c.rxBusy)
    {
        c.rxByte = value;
        c.rxBusy = true;
        c.rxDone = c.lowSince + stop;
    }
}

void NextUart::Step(Channel& c, uint64_t now)
{
    _pollClean = false;
    ISerialPeer* const peer = WireOf(c);
    const bool viaJoystick = peer && peer == _joyPeer;
    // pins not connected: what the module sends reaches nobody (the connector's device, when it has the channel, is another matter)
    while (!c.connected && c.peer && !(_joyOn && _joyChannel == IndexOf(c)) && c.peer->HasByte())
    {
        c.peer->TakeByte();
        c.lost++;
    }
    if (FifoReset(c))
    {
        // held in reset: the FIFOs and the shifters stay empty; a byte the peer offers is taken and lost
        ResetFifos(c);
        c.lowPaused = peer && peer->BreakHeld();  // the receiver waits for a held line to rise
        while (peer && !peer->BreakHeld() && peer->HasByte())
        {
            peer->TakeByte();
            c.lost++;
        }
        c.lastLook = now;
        return;
    }
    const bool flow = (c.frame & kFrameFlow) != 0;
    // the transmitter
    for (;;)
    {
        if (c.txBusy)
        {
            if (c.txDone > now)
                break;
            if (peer)
                peer->Transmit(c.txByte);
            else
                c.lost++;
            c.sent++;
            c.txBusy = false;
            c.txFreeAt = c.txDone;
        }
        if (c.frame & kFrameBreak)
            break;  // the line is held low: nothing is sent
        if (c.txCount == 0)
            break;
        if (flow && peer && !peer->Cts())
            break;  // S_RTR: waits for the receiver; it goes on at the next look
        // the byte was queued at txReadyAt; the shifter was free from txFreeAt: the frame starts at the later of the two
        StartTx(c, std::min<uint64_t>(std::max(c.txFreeAt, c.txReadyAt), now));
    }
    // the receiver
    StepLine(c, peer, now);
    for (;;)
    {
        if (c.rxBusy)
        {
            if (c.rxDone > now)
                break;
            const uint16_t entry = static_cast<uint16_t>(c.rxByte | ((c.errOverflow || c.errFraming) ? 0x100 : 0));
            if (!RxFull(c))
            {
                c.rx[(c.rxHead + c.rxCount) % kRxFifo] = entry;
                c.rxCount++;
                c.received++;
            }
            else
            {
                c.errOverflow = true;
                c.lost++;
            }
            c.rxBusy = false;
            c.rxFreeAt = c.rxDone;
        }
        if (!peer || c.rxLow || c.lowPaused || peer->BreakHeld() || !peer->HasByte())
            break;
        // the connector has no ready-to-receive pin: a joystick peer cannot be held off
        const bool rtr = flow && c.rxCount >= kAlmostFull;
        if (rtr && !viaJoystick && peer->HonorsRts())
            break;
        c.rxByte = peer->TakeByte();
        c.rxBusy = true;
        // a byte the peer had waiting while the receiver was finishing the last one starts when that ended; one that came after the
        // receiver was idle starts at this look
        const uint64_t start = (c.rxFreeAt > c.lastLook && c.rxFreeAt <= now) ? c.rxFreeAt : now;
        c.rxDone = start + FrameClocks(c);
    }
    c.lastLook = now;
    // RTS: the peer is told when the line changes (the connector has no pin for it, a module cut off by it sees RTR forced)
    const bool rts = !(flow && c.rxCount >= kAlmostFull);
    if (rts != c.rtsOut)
    {
        c.rtsOut = rts;
        if (peer && !viaJoystick)
            peer->OnModemLines(rts, true);
    }
}

bool NextUart::Quiet() const
{
    for (unsigned i = 0; i < kChannels; i++)
    {
        const Channel& c = _ch[i];
        // everything Step acts on, in its order: the reset, the transmitter, the line, the receiver, RTS
        if (FifoReset(c) || c.txBusy || c.txCount || c.rxBusy || c.rxLow || c.lowPaused || c.breakIn)
            return false;
        if (c.rtsOut != !((c.frame & kFrameFlow) && c.rxCount >= kAlmostFull))
            return false;
        const bool joystick = _joyOn && _joyChannel == i;
        if (!c.connected && c.peer && !joystick && c.peer->HasByte())
            return false;  // a module cut off from the pins: its bytes are taken and lost
        if (const ISerialPeer* peer = Wire(i))
            if (peer->BreakHeld() || peer->HasByte())
                return false;
    }
    return true;
}

void NextUart::Look(uint64_t now)
{
    if (now < _now)
        now = _now;  // the clock does not go back
    _now = now;
    for (Channel& c : _ch)
        c.lastLook = now;
}

void NextUart::Advance(uint64_t now)
{
    if (now < _now)
        now = _now;  // the clock does not go back
    _now = now;
    for (Channel& c : _ch)
        Step(c, now);
}

uint8_t NextUart::Status(const Channel& c) const
{
    uint8_t value = 0;
    if (c.breakIn)
        value |= kStatusBreak;
    if (c.errFraming)
        value |= kStatusFraming;
    if (c.rxCount && (c.rx[c.rxHead] & 0x100))
        value |= kStatusByteError;
    // uart.vhd: Tx empty = FIFO empty and not busy; the transmitter's o_busy also holds while the frame's break or reset bit is set
    if (c.txCount == 0 && !c.txBusy && !(c.frame & (kFrameBreak | kFrameReset)))
        value |= kStatusTxEmpty;
    if (c.rxCount >= kNearFull)
        value |= kStatusNearFull;
    if (c.errOverflow)
        value |= kStatusOverflow;
    if (c.txCount >= kTxFifo)
        value |= kStatusTxFull;
    if (c.rxCount)
        value |= kStatusRxAvail;
    return value;
}

uint8_t NextUart::Read(uint16_t port, uint64_t now)
{
    Advance(now);
    _pollClean = false;
    Channel& c = _ch[_selected];
    switch ((port >> 8) & 3)
    {
        case kRxRegister:
        {
            if (!c.rxCount)
                return 0;
            const uint8_t value = static_cast<uint8_t>(c.rx[c.rxHead] & 0xFF);
            c.rxHead = static_cast<uint16_t>((c.rxHead + 1) % kRxFifo);
            c.rxCount--;
            Step(c, now);  // a byte waiting for room, RTS
            return value;
        }
        case kSelectRegister:
            return static_cast<uint8_t>((_selected ? kSelectBit : 0) | c.prescalerMsb);
        case kFrameRegister:
            return c.frame;
        default:
        {
            const uint8_t status = Status(c);
            c.errOverflow = c.errFraming = false;  // the falling edge of the read clears the sticky bits
            return status;
        }
    }
}

void NextUart::Write(uint16_t port, uint8_t value, uint64_t now)
{
    Advance(now);
    _pollClean = false;
    Channel& c = _ch[_selected];
    switch ((port >> 8) & 3)
    {
        case kRxRegister:
            if (value & kHalfSelect)
                c.prescalerLsb = static_cast<uint16_t>((c.prescalerLsb & 0x7F) | ((value & kHalfMask) << kHalfBits));
            else
                c.prescalerLsb = static_cast<uint16_t>((c.prescalerLsb & ~0x7F) | (value & kHalfMask));
            TellPeer(c);
            break;
        case kSelectRegister:
            _selected = (value & kSelectBit) ? 1 : 0;
            if (value & kPrescalerFollows)
                _ch[_selected].prescalerMsb = value & kPrescalerMsbMask;
            TellPeer(_ch[_selected]);
            break;
        case kFrameRegister:
            c.frame = value;
            if (value & kFrameReset)
                ResetFifos(c);
            TellPeer(c);
            TellBreak(c);
            break;
        default:
            if (!FifoReset(c) && c.txCount < kTxFifo)
            {
                c.tx[(c.txHead + c.txCount) % kTxFifo] = value;
                c.txCount++;
                c.txReadyAt = now;
            }
            break;
    }
    Step(c, now);
}

void NextUart::PollInterrupts(uint8_t enable)
{
    // The per-instruction call of a UART nothing happened to: the levels are what the last look stored, no edge
    if (_pollClean && enable == _polledEnable)
        return;
    _pollClean = true;
    _polledEnable = enable;
    for (unsigned i = 0; i < kChannels; i++)
    {
        Channel& c = _ch[i];
        const uint8_t bits = static_cast<uint8_t>((enable >> (i ? 4 : 0)) & 7);
        const bool rxLevel = c.rxCount >= kNearFull || (c.rxCount && !(bits & 2));
        const bool txLevel = c.txCount == 0;
        const bool rxRise = rxLevel && !c.rxLevel, txRise = txLevel && !c.txLevel;
        c.rxLevel = rxLevel;
        c.txLevel = txLevel;
        if (rxRise && onInterrupt)
            onInterrupt(i, false);
        if (txRise && onInterrupt)
            onInterrupt(i, true);
    }
}

void NextUart::SetSystemClock(uint32_t hz)
{
    if (hz == _systemClockHz)
        return;
    _systemClockHz = hz;
    for (Channel& c : _ch)
        TellPeer(c);  // the same prescaler is another rate now
}

NextUart::View NextUart::GetView(unsigned channel) const
{
    const Channel& c = _ch[channel];
    View v;
    v.selected = _selected == channel;
    v.prescaler = Prescaler(c);
    v.baud = static_cast<uint32_t>(_systemClockHz / v.prescaler);
    v.frame = c.frame;
    v.dataBits = DataBits(c);
    v.parity = (c.frame & kFrameParity) ? ((c.frame & kFrameOdd) ? 'O' : 'E') : 'N';
    v.stopBits = (c.frame & kFrameTwoStops) ? 2 : 1;
    v.hardwareFlow = (c.frame & kFrameFlow) != 0;
    v.rtsAsserted = c.rtsOut;
    v.breakOut = (c.frame & kFrameBreak) != 0;
    v.rxCount = c.rxCount;
    v.txCount = c.txCount;
    v.txBusy = c.txBusy;
    v.rxBusy = c.rxBusy;
    v.overflow = c.errOverflow;
    v.framingError = c.errFraming;
    v.status = Status(c);
    v.sent = c.sent;
    v.received = c.received;
    v.lost = c.lost;
    v.hasPeer = Wire(channel) != nullptr;
    v.breakIn = c.breakIn;
    v.rxLineLow = c.rxLow;
    v.joystick = _joyOn && _joyChannel == channel;
    return v;
}

unsigned NextUart::PeekRx(unsigned channel, uint16_t* out, unsigned max) const
{
    const Channel& c = _ch[channel];
    const unsigned n = std::min<unsigned>(c.rxCount, max);
    for (unsigned i = 0; i < n; i++)
        out[i] = c.rx[(c.rxHead + i) % kRxFifo];
    return n;
}

unsigned NextUart::PeekTx(unsigned channel, uint8_t* out, unsigned max) const
{
    const Channel& c = _ch[channel];
    const unsigned n = std::min<unsigned>(c.txCount, max);
    for (unsigned i = 0; i < n; i++)
        out[i] = c.tx[(c.txHead + i) % kTxFifo];
    return n;
}
