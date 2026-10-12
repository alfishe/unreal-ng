#include "stdafx.h"

#include "nextuart.h"

#include <algorithm>

#include "emulator/io/serial/serialpeer.h"

namespace
{
constexpr uint64_t kSystemClockHz = 28000000;
constexpr uint8_t kSelectBit = 0x40, kPrescalerFollows = 0x10, kPrescalerMsbMask = 0x07, kHalfSelect = 0x80, kHalfMask = 0x7F;
constexpr unsigned kRxRegister = 0, kSelectRegister = 1, kFrameRegister = 2;  // 3 is the Tx / status register (the default)
constexpr unsigned kHalfBits = 7;
}  // namespace

void NextUart::ResetFifos(Channel& c)
{
    c.rxHead = c.rxCount = 0;
    c.txHead = c.txCount = 0;
    c.txBusy = c.rxBusy = false;
    c.errOverflow = c.errFraming = false;
}

void NextUart::Reset(bool hard)
{
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
        if (c.peer)
            c.peer->OnModemLines(true, true);
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

void NextUart::SetPeer(unsigned channel, ISerialPeer* peer)
{
    Channel& c = _ch[channel];
    c.peer = peer;
    c.toldValid = false;
    if (peer)
    {
        peer->OnModemLines(!c.rtsOut ? false : true, true);
        TellPeer(c);
    }
}

void NextUart::TellPeer(Channel& c)
{
    if (!c.peer)
        return;
    SerialLine line;
    line.baud = static_cast<uint32_t>(kSystemClockHz / Prescaler(c));
    line.dataBits = static_cast<uint8_t>(DataBits(c));
    line.parity = (c.frame & kFrameParity) ? ((c.frame & kFrameOdd) ? 'O' : 'E') : 'N';
    line.stopBits = (c.frame & kFrameTwoStops) ? 2 : 1;
    if (c.toldValid && line == c.told)
        return;
    c.told = line;
    c.toldValid = true;
    c.peer->OnLineSettings(line);
}

void NextUart::StartTx(Channel& c, uint64_t at)
{
    c.txByte = c.tx[c.txHead];
    c.txHead = static_cast<uint8_t>((c.txHead + 1) % kTxFifo);
    c.txCount--;
    c.txBusy = true;
    c.txDone = at + FrameClocks(c);
}

void NextUart::Step(Channel& c, uint64_t now)
{
    if (FifoReset(c))
    {
        // held in reset: the FIFOs and the shifters stay empty; a byte the peer offers is taken and lost
        ResetFifos(c);
        while (c.peer && c.peer->HasByte())
        {
            c.peer->TakeByte();
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
            if (c.peer)
                c.peer->Transmit(c.txByte);
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
        if (flow && c.peer && !c.peer->Cts())
            break;  // S_RTR: waits for the receiver; it goes on at the next look
        // the byte was queued at txReadyAt; the shifter was free from txFreeAt: the frame starts at the later of the two
        StartTx(c, std::min<uint64_t>(std::max(c.txFreeAt, c.txReadyAt), now));
    }
    // the receiver
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
        if (!c.peer || !c.peer->HasByte())
            break;
        const bool rtr = flow && c.rxCount >= kAlmostFull;
        if (rtr && c.peer->HonorsRts())
            break;
        c.rxByte = c.peer->TakeByte();
        c.rxBusy = true;
        // a byte the peer had waiting while the receiver was finishing the last one starts when that ended; one that came after the
        // receiver was idle starts at this look
        const uint64_t start = (c.rxFreeAt > c.lastLook && c.rxFreeAt <= now) ? c.rxFreeAt : now;
        c.rxDone = start + FrameClocks(c);
    }
    c.lastLook = now;
    // RTS: the peer is told when the line changes
    const bool rts = !(flow && c.rxCount >= kAlmostFull);
    if (rts != c.rtsOut)
    {
        c.rtsOut = rts;
        if (c.peer)
            c.peer->OnModemLines(rts, true);
    }
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
    if (c.errFraming)
        value |= kStatusFraming;
    if (c.rxCount && (c.rx[c.rxHead] & 0x100))
        value |= kStatusByteError;
    if (c.txCount == 0 && !c.txBusy)
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

NextUart::View NextUart::GetView(unsigned channel) const
{
    const Channel& c = _ch[channel];
    View v;
    v.selected = _selected == channel;
    v.prescaler = Prescaler(c);
    v.baud = static_cast<uint32_t>(kSystemClockHz / v.prescaler);
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
    v.hasPeer = c.peer != nullptr;
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
