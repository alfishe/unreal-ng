#include "emulator/io/network/ethernet/dp8390.h"

#include <cstring>

namespace
{
constexpr size_t kMinFrame = 60;        ///< without the CRC
constexpr size_t kMaxFrame = 1514;      ///< without the CRC
constexpr size_t kCrcBytes = 4;
constexpr size_t kHeaderBytes = 4;
}  // namespace

Dp8390::Dp8390(IBoard& board) : _board(board)
{
    Reset();
}

uint32_t Dp8390::Crc32(const uint8_t* data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return ~crc;
}

unsigned Dp8390::MulticastHashIndex(const uint8_t* mac)
{
    // The CRC register as the chip shifts the destination in (Linux ether_crc: the bit-reversed, not inverted
    // form of the IEEE CRC-32); its top 6 bits pick one of the 64 MAR bits
    const uint32_t crc = ~Crc32(mac, 6);
    uint32_t reversed = 0;
    for (int b = 0; b < 32; ++b)
        reversed |= ((crc >> b) & 1u) << (31 - b);
    return reversed >> 26;
}

void Dp8390::Reset()
{
    const bool before = InterruptActive();
    // RSTDRV / the reset port (DP8390 datasheet "Initialization"; RTL8019AS page 3 power-up CR = #21): stopped,
    // remote DMA aborted, RST set, interrupts masked, loopback off until the driver programs TCR
    _s.cr = kCrStp | kCrRdAbort;
    _s.isr = kIsrRst;
    _s.imr = 0;
    _s.dcr = 0;
    _s.tcr = 0;
    _s.rcr = 0;
    _s.rsr = 0;
    _s.tsr = 0;
    _s.ncr = 0;
    _s.dmaActive = 0;
    _s.remaining = 0;
    _s.txPending = 0;
    _s.txDoneAt = 0;
    _s.fifoLength = 0;
    _s.fifoRead = 0;
    NoteIrq(before);
}

void Dp8390::LoadState(const State& state)
{
    std::memcpy(&_s, &state, sizeof(_s));   // every byte, padding too: equal states give equal blobs
}

// ---------------------------------------------------------------------------
// Interrupts
// ---------------------------------------------------------------------------

void Dp8390::SetIsr(uint8_t bits)
{
    const bool before = InterruptActive();
    _s.isr |= bits;
    NoteIrq(before);
}

void Dp8390::NoteIrq(bool before)
{
    if (before != InterruptActive() && _irqChanged)
        _irqChanged();
}

void Dp8390::CountTally(int counter)
{
    if (_s.cntr[counter] < 0xFF)
        ++_s.cntr[counter];
    if (_s.cntr[counter] & 0x80)
        SetIsr(kIsrCnt);
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

void Dp8390::Advance(uint64_t now)
{
    if (_s.txPending && now >= _s.txDoneAt)
    {
        _s.txPending = 0;
        _s.cr = static_cast<uint8_t>(_s.cr & ~kCrTxp);
        _s.tsr = kTsrPtx;
        SetIsr(kIsrPtx);
    }
}

// ---------------------------------------------------------------------------
// Registers
// ---------------------------------------------------------------------------

uint8_t Dp8390::PeekRegister(uint8_t page, uint8_t offset) const
{
    offset &= 0x0F;
    if (offset == 0)
        return _s.cr;
    switch (page & 3)
    {
        case 0:
            switch (offset)
            {
                case 0x01: return static_cast<uint8_t>(_s.clda & 0xFF);
                case 0x02: return static_cast<uint8_t>(_s.clda >> 8);
                case 0x03: return _s.bnry;
                case 0x04: return _s.tsr;
                case 0x05: return _s.ncr;
                case 0x06: return _s.fifoLength ? _s.fifo[_s.fifoRead % _s.fifoLength] : 0;
                case 0x07: return _s.isr;
                case 0x08: return static_cast<uint8_t>(_s.crda & 0xFF);
                case 0x09: return static_cast<uint8_t>(_s.crda >> 8);
                case 0x0C: return _s.rsr;
                case 0x0D: return _s.cntr[0];
                case 0x0E: return _s.cntr[1];
                case 0x0F: return _s.cntr[2];
                default: return 0xFF;   // #0A / #0B: the variant's (the board answers the RTL8019AS ID)
            }
        case 1:
            if (offset <= 0x06)
                return _s.par[offset - 1];
            if (offset == 0x07)
                return _s.curr;
            return _s.mar[offset - 0x08];
        case 2:
            switch (offset)
            {
                case 0x01: return _s.pstart;
                case 0x02: return _s.pstop;
                case 0x03: return _s.bnry;    // remote next packet pointer (DP8390): where "send packet" starts
                case 0x04: return _s.tpsr;
                case 0x05: return _s.curr;    // local next packet pointer
                case 0x06: return static_cast<uint8_t>(_s.clda >> 8);
                case 0x07: return static_cast<uint8_t>(_s.clda & 0xFF);
                case 0x0C: return static_cast<uint8_t>(_s.rcr | 0xC0);
                case 0x0D: return static_cast<uint8_t>(_s.tcr | 0xE0);
                case 0x0E: return static_cast<uint8_t>(_s.dcr | 0x80);
                case 0x0F: return static_cast<uint8_t>(_s.imr | 0x80);
                default: return 0xFF;
            }
        default:
            return 0xFF;   // page 3 is the variant's
    }
}

uint8_t Dp8390::ReadRegisterAs(uint8_t page, uint8_t offset, uint64_t now)
{
    Advance(now);
    offset &= 0x0F;
    page &= 3;
    const uint8_t value = PeekRegister(page, offset);
    if (page == 0 && offset != 0)
    {
        if (offset >= 0x0D)
            _s.cntr[offset - 0x0D] = 0;   // tally counters clear on read
        else if (offset == 0x06 && _s.fifoLength)
            _s.fifoRead = static_cast<uint8_t>((_s.fifoRead + 1) % _s.fifoLength);
    }
    return value;
}

void Dp8390::WriteRegisterAs(uint8_t page, uint8_t offset, uint8_t value, uint64_t now)
{
    Advance(now);
    offset &= 0x0F;
    if (offset == 0)
    {
        // Command register: page select, remote DMA command, start / stop, transmit
        const bool wasStopped = (_s.cr & kCrStp) != 0;
        uint8_t cr = static_cast<uint8_t>((value & 0xFB) | (_s.cr & kCrTxp));
        if (value & kCrStp)
            cr = static_cast<uint8_t>(cr & ~kCrSta);
        else if (value & kCrSta)
            cr = static_cast<uint8_t>(cr & ~kCrStp);
        else
            cr = static_cast<uint8_t>((cr & ~(kCrSta | kCrStp)) | (_s.cr & (kCrSta | kCrStp)));
        _s.cr = cr;
        if (cr & kCrStp)
        {
            // Stopped: the receiver and transmitter halt, RST shows it
            SetIsr(kIsrRst);
        }
        else if (wasStopped && (cr & kCrSta))
        {
            const bool before = InterruptActive();
            _s.isr = static_cast<uint8_t>(_s.isr & ~kIsrRst);
            NoteIrq(before);
        }
        if (value & kCrRdMask)
            StartRemoteDma(static_cast<uint8_t>(value & kCrRdMask));
        if ((value & kCrTxp) && !(cr & kCrStp) && !_s.txPending)
            StartTransmit(now);
        return;
    }

    switch (page & 3)
    {
        case 0:
            switch (offset)
            {
                case 0x01: _s.pstart = value; break;
                case 0x02: _s.pstop = value; break;
                case 0x03: _s.bnry = value; break;
                case 0x04: _s.tpsr = value; break;
                case 0x05: _s.tbcr = static_cast<uint16_t>((_s.tbcr & 0xFF00) | value); break;
                case 0x06: _s.tbcr = static_cast<uint16_t>((_s.tbcr & 0x00FF) | (value << 8)); break;
                case 0x07:
                {
                    // Write 1 to clear; RST follows the stopped state, not a write
                    const bool before = InterruptActive();
                    _s.isr = static_cast<uint8_t>(_s.isr & ~(value & 0x7F));
                    NoteIrq(before);
                    break;
                }
                case 0x08: _s.rsar = static_cast<uint16_t>((_s.rsar & 0xFF00) | value); break;
                case 0x09: _s.rsar = static_cast<uint16_t>((_s.rsar & 0x00FF) | (value << 8)); break;
                case 0x0A: _s.rbcr = static_cast<uint16_t>((_s.rbcr & 0xFF00) | value); break;
                case 0x0B: _s.rbcr = static_cast<uint16_t>((_s.rbcr & 0x00FF) | (value << 8)); break;
                case 0x0C: _s.rcr = static_cast<uint8_t>(value & 0x3F); break;
                case 0x0D: _s.tcr = static_cast<uint8_t>(value & 0x1F); break;
                case 0x0E: _s.dcr = static_cast<uint8_t>(value & 0x7F); break;
                case 0x0F:
                {
                    const bool before = InterruptActive();
                    _s.imr = static_cast<uint8_t>(value & 0x7F);
                    NoteIrq(before);
                    break;
                }
                default: break;
            }
            break;
        case 1:
            if (offset <= 0x06)
                _s.par[offset - 1] = value;
            else if (offset == 0x07)
                _s.curr = value;
            else
                _s.mar[offset - 0x08] = value;
            break;
        case 2:
            // Diagnostic writes of the local DMA pointers (DP8390 page 2: CLDA, the next packet pointers)
            switch (offset)
            {
                case 0x01: _s.clda = static_cast<uint16_t>((_s.clda & 0xFF00) | value); break;
                case 0x02: _s.clda = static_cast<uint16_t>((_s.clda & 0x00FF) | (value << 8)); break;
                case 0x03: _s.bnry = value; break;
                case 0x05: _s.curr = value; break;
                default: break;
            }
            break;
        default:
            break;   // page 3: the variant's
    }
}

// ---------------------------------------------------------------------------
// Remote DMA
// ---------------------------------------------------------------------------

void Dp8390::StartRemoteDma(uint8_t rd)
{
    switch (rd)
    {
        case kCrRdRead:
        case kCrRdWrite:
            _s.crda = _s.rsar;
            _s.remaining = _s.rbcr;
            _s.dmaActive = rd == kCrRdRead ? 1 : 2;
            if (_s.remaining == 0)
            {
                _s.dmaActive = 0;
                SetIsr(kIsrRdc);
            }
            break;
        case kCrRdSend:
        {
            // "Send packet": read the frame BNRY points to - its header gives the count (header + frame + CRC)
            _s.crda = static_cast<uint16_t>(_s.bnry << 8);
            const uint16_t count = static_cast<uint16_t>(_board.BufferRead(static_cast<uint16_t>(_s.crda + 2)) |
                                                         (_board.BufferRead(static_cast<uint16_t>(_s.crda + 3)) << 8));
            _s.rsar = _s.crda;
            _s.rbcr = static_cast<uint16_t>(count + kHeaderBytes);
            _s.remaining = _s.rbcr;
            _s.dmaActive = 1;
            break;
        }
        default:
            // Abort / complete: the remote DMA stops where it is
            _s.dmaActive = 0;
            break;
    }
}

uint8_t Dp8390::DataPeek() const
{
    return _s.dmaActive == 1 ? _board.BufferRead(_s.crda) : 0xFF;
}

uint8_t Dp8390::DataRead(uint64_t now)
{
    Advance(now);
    if (_s.dmaActive != 1)
        return _board.BufferRead(_s.crda);   // no read in progress: what the local bus shows, nothing moves
    const uint8_t value = _board.BufferRead(_s.crda);
    _s.crda = static_cast<uint16_t>(_s.crda + 1);
    if (_s.pstop && _s.crda == static_cast<uint16_t>(_s.pstop << 8))
        _s.crda = static_cast<uint16_t>(_s.pstart << 8);   // the ring wraps for remote reads
    if (--_s.remaining == 0)
    {
        _s.dmaActive = 0;
        SetIsr(kIsrRdc);
    }
    return value;
}

void Dp8390::DataWrite(uint8_t value, uint64_t now)
{
    Advance(now);
    if (_s.dmaActive != 2)
        return;
    _board.BufferWrite(_s.crda, value);
    _s.crda = static_cast<uint16_t>(_s.crda + 1);
    if (--_s.remaining == 0)
    {
        _s.dmaActive = 0;
        SetIsr(kIsrRdc);
    }
}

// ---------------------------------------------------------------------------
// Transmit
// ---------------------------------------------------------------------------

void Dp8390::StartTransmit(uint64_t now)
{
    const size_t length = _s.tbcr > 1536 ? 1536 : _s.tbcr;
    std::vector<uint8_t> frame(length);
    const uint16_t start = static_cast<uint16_t>(_s.tpsr << 8);
    for (size_t i = 0; i < length; ++i)
        frame[i] = _board.BufferRead(static_cast<uint16_t>(start + i));
    _s.clda = static_cast<uint16_t>(start + length);
    _s.cr = static_cast<uint8_t>(_s.cr | kCrTxp);
    _s.txPending = 1;
    // The wire time of what goes out: the frame (a runt as it is: the chip does not pad), then the CRC
    _s.txDoneAt = now + WireTime(length + kCrcBytes);
    _s.tsr = 0;

    if (_s.tcr & kTcrLbMask)
    {
        Loopback(frame);   // never on the wire
        return;
    }
    ++_s.framesOut;
    _board.Transmit(frame.data(), frame.size());
}

void Dp8390::Loopback(const std::vector<uint8_t>& frame)
{
    // The datasheet's loopback: the receiver checks the frame (address filter, CRC) and reports it in RSR, the data
    // stays in the FIFO - nothing reaches the receive ring (network open question Q4; MAME rings it, the RTL kit's
    // NICLB accepts both). The FIFO keeps the last 8 bytes that went through: the frame's tail and its CRC
    std::vector<uint8_t> wire = frame;
    if (!(_s.tcr & kTcrCrc))
    {
        const uint32_t crc = Crc32(frame.data(), frame.size());
        for (int i = 0; i < 4; ++i)
            wire.push_back(static_cast<uint8_t>(crc >> (8 * i)));
    }
    const size_t keep = wire.size() < 8 ? wire.size() : 8;
    std::memcpy(_s.fifo, wire.data() + wire.size() - keep, keep);
    _s.fifoLength = static_cast<uint8_t>(keep);
    _s.fifoRead = 0;

    bool physicalMulticast = false;
    const bool accepted = AddressAccepted(frame.data(), frame.size(), physicalMulticast);
    _s.rsr = static_cast<uint8_t>((accepted ? kRsrPrx : 0) | (physicalMulticast ? kRsrPhy : 0));
}

// ---------------------------------------------------------------------------
// Receive
// ---------------------------------------------------------------------------

bool Dp8390::AddressAccepted(const uint8_t* frame, size_t length, bool& physicalMulticast) const
{
    physicalMulticast = false;
    if (length < 6)
        return false;
    static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (std::memcmp(frame, kBroadcast, 6) == 0)
    {
        physicalMulticast = true;
        return (_s.rcr & (kRcrAb | kRcrPro)) != 0;
    }
    if (frame[0] & 0x01)
    {
        physicalMulticast = true;
        if (_s.rcr & kRcrPro)
            return true;
        if (!(_s.rcr & kRcrAm))
            return false;
        const unsigned index = MulticastHashIndex(frame);
        return (_s.mar[index >> 3] >> (index & 7)) & 1;
    }
    if (_s.rcr & kRcrPro)
        return true;
    return std::memcmp(frame, _s.par, 6) == 0;
}

uint8_t Dp8390::NextRingPage(uint8_t page) const
{
    const uint8_t next = static_cast<uint8_t>(page + 1);
    return next >= _s.pstop ? _s.pstart : next;
}

uint16_t Dp8390::RingFreeBytes() const
{
    // The chip writes from CURR and may not enter the page BNRY names; every page it may fill counts
    if (_s.pstop <= _s.pstart)
        return 0;
    const unsigned ring = static_cast<unsigned>(_s.pstop - _s.pstart);
    unsigned pages = _s.bnry > _s.curr ? static_cast<unsigned>(_s.bnry - _s.curr)
                                       : static_cast<unsigned>(_s.bnry + ring - _s.curr);
    if (pages > ring)
        pages = ring;
    return static_cast<uint16_t>(pages * 256u);
}

bool Dp8390::CanAccept(const uint8_t* frame, size_t length) const
{
    if (!Running() || (_s.rcr & kRcrMon) || (_s.tcr & kTcrLbMask) || (_s.isr & kIsrOvw))
        return true;   // the frame is dropped (taken) - a stopped or monitoring chip stores nothing
    bool physicalMulticast = false;
    if (!AddressAccepted(frame, length, physicalMulticast))
        return true;
    const size_t stored = (length < kMinFrame ? kMinFrame : length) + kCrcBytes + kHeaderBytes;
    return stored <= RingFreeBytes();
}

bool Dp8390::Receive(const uint8_t* frame, size_t length, uint64_t now)
{
    Advance(now);
    if (length > kMaxFrame)
        length = kMaxFrame;
    if (!Running() || (_s.tcr & kTcrLbMask))
        return true;   // the receiver is off (stopped, or looped back onto itself): the wire's frame is lost
    bool physicalMulticast = false;
    if (!AddressAccepted(frame, length, physicalMulticast))
    {
        ++_s.framesFiltered;
        return true;
    }
    if (length < kMinFrame && !(_s.rcr & kRcrAr))
    {
        ++_s.framesFiltered;
        return true;   // a runt: the sender pads, the receiver only takes one with RCR.AR
    }
    if (_s.rcr & kRcrMon)
    {
        // Monitor mode: checked and counted, not stored
        _s.rsr = static_cast<uint8_t>(kRsrPrx | kRsrMpa | (physicalMulticast ? kRsrPhy : 0));
        CountTally(2);
        return true;
    }
    const size_t count = length + kCrcBytes;   // the header's byte count: the frame and its CRC
    if (!CanAccept(frame, length) || (_s.isr & kIsrOvw))
    {
        if (_s.isr & kIsrOvw)
        {
            ++_s.framesMissed;
            CountTally(2);
            return true;   // the receiver waits for the driver's overflow recovery: frames are lost meanwhile
        }
        ++_ringFullWaits;
        return false;      // a switch keeps it until the driver frees the ring
    }

    std::vector<uint8_t> stored(kHeaderBytes + count);
    std::memcpy(stored.data() + kHeaderBytes, frame, length);
    const uint32_t crc = Crc32(frame, length);
    for (size_t i = 0; i < kCrcBytes; ++i)
        stored[kHeaderBytes + length + i] = static_cast<uint8_t>(crc >> (8 * i));
    const size_t pages = (stored.size() + 255) / 256;
    uint8_t next = _s.curr;
    for (size_t p = 0; p < pages; ++p)
        next = NextRingPage(next);
    _s.rsr = static_cast<uint8_t>(kRsrPrx | (physicalMulticast ? kRsrPhy : 0));
    stored[0] = _s.rsr;
    stored[1] = next;
    stored[2] = static_cast<uint8_t>(count & 0xFF);
    stored[3] = static_cast<uint8_t>(count >> 8);

    // Local DMA into the ring, wrapping PSTOP -> PSTART
    uint16_t address = static_cast<uint16_t>(_s.curr << 8);
    for (uint8_t byte : stored)
    {
        _board.BufferWrite(address, byte);
        address = static_cast<uint16_t>(address + 1);
        if (address == static_cast<uint16_t>(_s.pstop << 8))
            address = static_cast<uint16_t>(_s.pstart << 8);
    }
    _s.clda = address;
    _s.curr = next;
    ++_s.framesIn;
    SetIsr(kIsrPrx);
    return true;
}
