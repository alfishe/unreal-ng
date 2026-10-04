#include "emulator/io/network/ethernet/etherlink3.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "emulator/io/network/ethernet/dp8390.h"

namespace
{
// Time in base T-states (3.5 MHz): 3.5 T per microsecond
constexpr uint64_t kAutoInitT = 1085;        ///< 310 us: the EEPROM is read after a reset (TR 7-2)
constexpr uint64_t kEepromReadT = 567;       ///< 162 us (TR 7-22)
constexpr uint64_t kEepromEnableT = 210;     ///< 60 us: erase / write enable or disable
constexpr uint64_t kEepromWriteT = 38500;    ///< 11 ms: write, erase, write all, erase all
constexpr uint64_t kLinkTestT = 168000;      ///< 48 ms: three link test pulses 16 ms apart (IEEE 802.3 14.2.1.7)
constexpr uint64_t kTxResetCipT = 21;        ///< 6 us: TX Reset while a packet leaves (TR 6-7)
constexpr uint16_t kThresholdDefault = 0x7FC;   ///< 2044: every threshold's power-up value on the 3C509B (TR 6-10)
constexpr uint16_t kThresholdLimit = 1792;      ///< above it a threshold is disabled
constexpr uint16_t kFifoReserve = 4;            ///< bytes of each FIFO never free
constexpr uint16_t kPacketOverhead = 4;         ///< per packet in a FIFO (status / header)
constexpr size_t kMinFrame = 60;
constexpr size_t kMaxFrame = 1514;
constexpr size_t kMaxReceive = 1792;

// The verified boards' EEPROM, words 00-37h (the Sprinter 3C509B Network Kit, tools/test-stage3-exe.js: read by its
// EL3EEP on a real Sprinter). Words 38-3F are past the Plug and Play resource data's end tag: zero (their XOR is zero
// in both dumps' vital checksum)
constexpr uint16_t kTpoWords[56] = {
    0x0020, 0xAF5D, 0x698B, 0x9550, 0xB434, 0x0041, 0x4A41, 0x6D50, 0x0010, 0x3000, 0x0020, 0xAF5D, 0x698B, 0x1310,
    0x0000, 0x3223, 0x2083, 0x0000, 0x0000, 0x0004, 0x0001, 0x0000, 0x0000, 0x0205, 0x6D50, 0x9550, 0x698B, 0xAF5D,
    0x0A5B, 0x1010, 0x1982, 0x3300, 0x6F43, 0x206D, 0x4333, 0x3035, 0x4239, 0x4520, 0x6874, 0x7265, 0x694C, 0x6B6E,
    0x4920, 0x4949, 0x5015, 0x506D, 0x0295, 0x411C, 0x80D0, 0x22F7, 0x9EA8, 0x0147, 0x0210, 0x03E0, 0x1010, 0x3779};
constexpr uint16_t kTpWords[56] = {
    0x0020, 0xAF4B, 0xAB97, 0x9050, 0xBE3D, 0x0041, 0x4741, 0x6D50, 0x0010, 0x3000, 0x0020, 0xAF4B, 0xAB97, 0x1310,
    0x0000, 0x3923, 0x2083, 0x0000, 0x0000, 0x0004, 0x0001, 0x0000, 0x0000, 0x4505, 0x6D50, 0x9050, 0xAB97, 0xAF4B,
    0x0ADF, 0x1010, 0x1982, 0x3300, 0x6F43, 0x206D, 0x4333, 0x3035, 0x4239, 0x4520, 0x6874, 0x7265, 0x694C, 0x6B6E,
    0x4920, 0x4949, 0x5015, 0x506D, 0x0290, 0x411C, 0x80D0, 0x22F7, 0x9EA8, 0x0147, 0x0210, 0x03E0, 0x1010, 0x3C79};

std::string Hex(unsigned value, int digits)
{
    char text[16];
    std::snprintf(text, sizeof(text), "#%0*X", digits, value);
    return text;
}

uint8_t XorBytes(uint16_t word)
{
    return static_cast<uint8_t>((word & 0xFF) ^ (word >> 8));
}

/// The ISA Plug and Play serial identifier checksum (the LFSR of the PnP specification, 64 bits LSB first)
uint8_t PnpChecksum(const uint8_t id[8])
{
    uint8_t lfsr = 0x6A;
    for (int i = 0; i < 8; ++i)
    {
        for (int bit = 0; bit < 8; ++bit)
        {
            const uint8_t b = static_cast<uint8_t>((id[i] >> bit) & 1);
            const uint8_t x = static_cast<uint8_t>(((lfsr >> 1) ^ lfsr ^ b) & 1);
            lfsr = static_cast<uint8_t>((lfsr >> 1) | (x << 7));
        }
    }
    return lfsr;
}

bool ValidIrq(int irq)
{
    return irq == 3 || irq == 5 || irq == 7 || irq == 9 || irq == 10 || irq == 11 || irq == 12 || irq == 15;
}

/// The statistic counters' widths (TR 6-24): 4, 4, 6, 6, then 8-bit ones, then the two 16-bit byte counts
constexpr uint16_t kStatMask[11] = {0x0F, 0x0F, 0x3F, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFFFF, 0xFFFF};
constexpr int kStatCarrierLost = 0, kStatRxOverruns = 5, kStatTxFrames = 6, kStatRxFrames = 7, kStatRxBytes = 9,
              kStatTxBytes = 10;

// --- state serialization: every field by value (no struct padding in the blob) ------------------------------------
struct Writer
{
    std::vector<uint8_t>& out;
    void operator()(uint8_t v) { out.push_back(v); }
    void operator()(uint16_t v)
    {
        out.push_back(static_cast<uint8_t>(v));
        out.push_back(static_cast<uint8_t>(v >> 8));
    }
    void operator()(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            out.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
    void operator()(uint64_t v)
    {
        for (int i = 0; i < 8; ++i)
            out.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
    template <size_t N>
    void operator()(uint8_t (&v)[N])
    {
        out.insert(out.end(), v, v + N);
    }
    template <size_t N>
    void operator()(uint16_t (&v)[N])
    {
        for (uint16_t& x : v)
            (*this)(x);
    }
};

struct Reader
{
    const uint8_t* src;
    size_t size;
    size_t at = 0;
    bool ok = true;
    bool Take(size_t n)
    {
        if (!ok || at + n > size)
        {
            ok = false;
            return false;
        }
        return true;
    }
    void operator()(uint8_t& v)
    {
        if (Take(1))
            v = src[at++];
    }
    void operator()(uint16_t& v)
    {
        if (Take(2))
        {
            v = static_cast<uint16_t>(src[at] | (src[at + 1] << 8));
            at += 2;
        }
    }
    void operator()(uint32_t& v)
    {
        if (!Take(4))
            return;
        v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<uint32_t>(src[at++]) << (8 * i);
    }
    void operator()(uint64_t& v)
    {
        if (!Take(8))
            return;
        v = 0;
        for (int i = 0; i < 8; ++i)
            v |= static_cast<uint64_t>(src[at++]) << (8 * i);
    }
    template <size_t N>
    void operator()(uint8_t (&v)[N])
    {
        if (Take(N))
        {
            std::memcpy(v, src + at, N);
            at += N;
        }
    }
    template <size_t N>
    void operator()(uint16_t (&v)[N])
    {
        for (uint16_t& x : v)
            (*this)(x);
    }
};
}  // namespace

// The fields of EtherLink3::State in blob order (one list for saving and loading)
#define EL3_STATE_FIELDS(V, s)                                                                                          \
    V(s.idState); V(s.tag); V(s.seqIndex); V(s.activated); V(s.idPort); V(s.autoInitUntil); V(s.eepromData);          \
    V(s.eepromCommand); V(s.eepromWriteEnable); V(s.eepromPending); V(s.eepromAddress); V(s.eepromBusyUntil);          \
    V(s.productId); V(s.addressConfig); V(s.resourceConfig); V(s.enable); V(s.internalConfig); V(s.romControl);        \
    V(s.window); V(s.writeLow); V(s.writeLowValid); V(s.readHigh); V(s.readHighOffset); V(s.intMask);                  \
    V(s.readZeroMask); V(s.latch); V(s.interruptRequested); V(s.txAvailable); V(s.updateStats); V(s.timerBase);       \
    V(s.cipUntil); V(s.rxFilter); V(s.rxEarly); V(s.txAvailThreshold); V(s.txStartThreshold); V(s.rxEnabled);         \
    V(s.txEnabled); V(s.statsEnabled); V(s.poweredDown); V(s.coax); V(s.txOverrun); V(s.rxUnderrun);                   \
    V(s.txResetNeeded); V(s.station); V(s.media); V(s.netDiag); V(s.ecStatus); V(s.linkBeatSince); V(s.cableSince);   \
    V(s.txInFlight); V(s.txStartedAt); V(s.txDoneAt); V(s.wireFreeAt); V(s.txStatus); V(s.txStatusCount);              \
    V(s.txStatusOverflow); V(s.preamble); V(s.preambleBytes); V(s.stats); V(s.statsPending); V(s.pendingTxReset)

const char* EtherLink3::VariantName(Variant v)
{
    return v == Variant::Tp ? "3C509B-TP" : "3C509B-TPO";
}

uint8_t EtherLink3::IdSequenceByte(int index)
{
    uint8_t v = 0xFF;
    for (int i = 0; i < index; ++i)
        v = static_cast<uint8_t>((v & 0x80) ? ((v << 1) ^ 0xCF) : (v << 1));
    return v;
}

uint16_t EtherLink3::PrimaryChecksum(const std::array<uint16_t, kEepromWords>& w)
{
    // TR 7-28: the high byte XORs both bytes of words 00-0E except 08, 09, 0D; the low byte those three
    uint8_t high = 0, low = 0;
    for (int i = 0; i < 15; ++i)
    {
        if (i == 0x08 || i == 0x09 || i == 0x0D)
            low ^= XorBytes(w[i]);
        else
            high ^= XorBytes(w[i]);
    }
    return static_cast<uint16_t>((high << 8) | low);
}

uint16_t EtherLink3::SecondaryChecksum(const std::array<uint16_t, kEepromWords>& w)
{
    // TR 7-29 with the lanes the real boards use (the kit's VALIDATE_SECONDARY): the vital high byte over words 10-12
    // and 18-3F, the configurable low byte over 13-16
    uint8_t high = 0, low = 0;
    for (int i = 0x10; i < kEepromWords; ++i)
    {
        if (i >= 0x13 && i <= 0x16)
            low ^= XorBytes(w[i]);
        else if (i != 0x17)
            high ^= XorBytes(w[i]);
    }
    return static_cast<uint16_t>((high << 8) | low);
}

std::array<uint16_t, EtherLink3::kEepromWords> EtherLink3::BuildEeprom(const Settings& settings)
{
    std::array<uint16_t, kEepromWords> w{};
    const uint16_t* source = settings.variant == Variant::Tp ? kTpWords : kTpoWords;
    std::copy(source, source + 56, w.begin());
    const auto& m = settings.mac;
    // The 3Com and the OEM node address: Address(2n) is the high byte of word n (TR 7-25, the kit's COPY_MAC)
    for (int i = 0; i < 3; ++i)
    {
        const uint16_t word = static_cast<uint16_t>((m[2 * i] << 8) | m[2 * i + 1]);
        w[i] = word;
        w[0x0A + i] = word;
    }
    // Address configuration: XCVR 00 (10BASE-T), no boot ROM, the I/O base; resource configuration: the IRQ
    w[0x08] = static_cast<uint16_t>((w[0x08] & ~0x1Fu) | (((settings.base - 0x200u) >> 4) & 0x1Fu));
    w[0x09] = static_cast<uint16_t>((w[0x09] & 0x0FFFu) | ((settings.irq & 0x0Fu) << 12));
    // The Plug and Play serial identifier (TR 7-30): vendor TCM509x, the low 4 bytes of the MAC as the serial number
    w[0x1A] = static_cast<uint16_t>((m[4] << 8) | m[5]);
    w[0x1B] = static_cast<uint16_t>((m[2] << 8) | m[3]);
    const uint8_t serial[8] = {static_cast<uint8_t>(w[0x18]), static_cast<uint8_t>(w[0x18] >> 8),
                               static_cast<uint8_t>(w[0x19]), static_cast<uint8_t>(w[0x19] >> 8),
                               static_cast<uint8_t>(w[0x1A]), static_cast<uint8_t>(w[0x1A] >> 8),
                               static_cast<uint8_t>(w[0x1B]), static_cast<uint8_t>(w[0x1B] >> 8)};
    w[0x1C] = static_cast<uint16_t>((w[0x1C] & 0xFF00) | PnpChecksum(serial));
    w[0x0F] = PrimaryChecksum(w);
    w[0x17] = SecondaryChecksum(w);
    return w;
}

EtherLink3::EtherLink3(const Settings& settings, std::function<uint64_t()> clock)
    : _settings(settings), _clock(std::move(clock))
{
    _eeprom = BuildEeprom(_settings);
    _s.cableSince = Now();
    PowerOnReset(Now());
}

// ---------------------------------------------------------------------------
// Resets
// ---------------------------------------------------------------------------

void EtherLink3::LoadFromEeprom()
{
    // TR 7-1: words 08, 09, 03 into the address / resource configuration and product ID, words 12h / 13h into the
    // internal configuration (RAM width reads 0 on the 3C509B)
    _s.productId = _eeprom[0x03];
    _s.addressConfig = _eeprom[0x08];
    _s.resourceConfig = _eeprom[0x09];
    _s.internalConfig = (static_cast<uint32_t>(_eeprom[0x13]) << 16 | _eeprom[0x12]) & ~0x8u;
}

void EtherLink3::PowerOnReset(uint64_t now)
{
    const uint64_t cable = _s.cableSince;
    const bool hadIrq = _s.latch != 0;
    _s = State();
    _s.cableSince = cable;
    _s.linkBeatSince = UINT64_MAX;
    _s.pendingTxReset = 0xFFFF;
    _s.rxEarly = _s.txAvailThreshold = _s.txStartThreshold = kThresholdDefault;
    _s.idState = static_cast<uint8_t>(IdState::AutoInit);
    _s.autoInitUntil = now + kAutoInitT;
    _s.timerBase = now;
    LoadFromEeprom();
    _tx.clear();
    _rx.clear();
    ++_counters.globalResets;
    Note(now, "power-on reset: EEPROM read (310 us), the card leaves its I/O base until the ID port activates it");
    if (hadIrq && _irqListener)
        _irqListener();
}

void EtherLink3::GlobalReset(uint16_t mask, uint64_t now)
{
    // TR 6-3: a set mask bit keeps that module out of the reset; all zero = the power-on reset
    if ((mask & 0x3F) == 0)
    {
        PowerOnReset(now);
        return;
    }
    if (!(mask & 0x01))
    {
        _s.media = 0;   // TPAUI: link beat and jabber enables
        _s.linkBeatSince = UINT64_MAX;
    }
    if (!(mask & 0x04))
    {
        // Network: the Ethernet controller, its status stack and statistics
        _s.rxEnabled = _s.txEnabled = 0;
        _s.rxFilter = 0;
        _s.txStatusCount = 0;
        _s.txStatusOverflow = 0;
        _s.txResetNeeded = 0;
        _s.netDiag = 0;
        _s.statsEnabled = 0;
        std::fill(std::begin(_s.stats), std::end(_s.stats), uint16_t(0));
        std::fill(std::begin(_s.statsPending), std::end(_s.statsPending), uint16_t(0));
        if (_s.txInFlight && !_tx.empty())
            _tx.pop_front();   // the packet on the wire is cut off
        _s.txInFlight = 0;
    }
    if (!(mask & 0x08))
    {
        _tx.clear();
        _rx.clear();
        _s.preambleBytes = 0;
        _s.txInFlight = 0;
        _s.rxEarly = _s.txAvailThreshold = _s.txStartThreshold = kThresholdDefault;
        _s.txOverrun = _s.rxUnderrun = 0;
    }
    if (!(mask & 0x20))
    {
        // Host: the bus interface - window 0, the interrupt bits (not their sources, TR 6-13)
        _s.window = 0;
        _s.latch = 0;
        _s.interruptRequested = 0;
        _s.txAvailable = 0;
    }
    if (!(mask & 0x10))
    {
        // AISM: the EEPROM is read again, the IDS returns to AUTOINIT (the card is no longer active)
        LoadFromEeprom();
        _s.activated = 0;
        _s.enable = 0;
        _s.tag = 0;
        _s.idPort = 0;
        _s.seqIndex = 0;
        _s.idState = static_cast<uint8_t>(IdState::AutoInit);
        _s.autoInitUntil = now + kAutoInitT;
    }
    Note(now, "Global Reset, mask " + Hex(mask & 0x3F, 2));
}

void EtherLink3::RxReset(uint16_t mask)
{
    if (!(mask & 0x04))
    {
        _s.rxEnabled = 0;
        _s.rxFilter = 0;
    }
    if (!(mask & 0x08))
    {
        _rx.clear();
        _s.rxFilter = 0;
        _s.rxEarly = kThresholdDefault;
        _s.rxUnderrun = 0;
    }
}

void EtherLink3::TxReset(uint16_t arg, uint64_t now)
{
    const bool network = !(arg & 0x04);
    const bool fifo = !(arg & 0x08);
    const bool immediate = (arg & 0x400) != 0;
    if (_s.txInFlight && network && !fifo && !immediate)
    {
        // Delayed until the packet on the wire is done (TR 6-7): Command in Progress meanwhile
        _s.pendingTxReset = arg;
        _s.cipUntil = _s.txDoneAt;
        return;
    }
    if (_s.txInFlight && (network || fifo))
    {
        // Cut off: the far end sees a bad CRC (nothing is delivered)
        if (!_tx.empty())
            _tx.pop_front();
        _s.txInFlight = 0;
        _s.wireFreeAt = now;
        if (!immediate)
            _s.cipUntil = now + kTxResetCipT;
    }
    if (network)
    {
        _s.txStatusCount = 0;
        _s.txStatusOverflow = 0;
        _s.txEnabled = 0;
        _s.txResetNeeded = 0;
    }
    if (fifo)
    {
        _tx.clear();
        _s.preambleBytes = 0;
        _s.txAvailThreshold = _s.txStartThreshold = kThresholdDefault;
        _s.txOverrun = 0;
    }
}

void EtherLink3::Reset()
{
    // ISA RESET DRV
    PowerOnReset(Now());
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

void EtherLink3::Advance(uint64_t now)
{
    if (_s.idState == static_cast<uint8_t>(IdState::AutoInit) && now >= _s.autoInitUntil)
    {
        _s.idState = static_cast<uint8_t>(IdState::IdWait);
        _s.seqIndex = 0;
    }
    FinishEeprom(now);
    for (int guard = 0; guard < 1024 && _s.txInFlight && !_tx.empty(); ++guard)
    {
        TxPacket& head = _tx.front();
        if (!head.complete)
        {
            // An early start: the wire needs data byte k at start + (8 + k) byte times (after the preamble)
            const uint64_t starve = _s.txStartedAt + ((8 + head.data.size()) * 28 + 9) / 10;
            if (now < starve)
                break;
            head.discard = true;
            _s.txInFlight = 0;
            _s.wireFreeAt = starve;
            _s.txEnabled = 0;
            _s.txResetNeeded = 1;
            ++_counters.txUnderruns;
            PushTxStatus(static_cast<uint8_t>(0x90 | (head.interruptOnSuccess ? 0x40 : 0)));
            Note(starve, "TX underrun: the host wrote " + std::to_string(head.data.size()) + " of " +
                             std::to_string(head.length) + " bytes when the wire needed more (TX Reset needed)");
            break;
        }
        if (now < _s.txDoneAt)
            break;
        FinishTransmit(_s.txDoneAt);
    }
    UpdateIrq(now);
}

// ---------------------------------------------------------------------------
// The ID port (TR 7-2..7-4)
// ---------------------------------------------------------------------------

void EtherLink3::IdWrite(uint16_t port, uint8_t value, uint64_t now)
{
    switch (static_cast<IdState>(_s.idState))
    {
        case IdState::AutoInit:
            return;
        case IdState::IdWait:
            if (value == 0)
            {
                _s.idPort = port;   // the last #1x0 written with 0 is the ID port; the sequence starts over
                _s.seqIndex = 0;
                return;
            }
            if (port != _s.idPort)
                return;
            if (value == IdSequenceByte(_s.seqIndex))
            {
                if (++_s.seqIndex == 255)
                {
                    _s.idState = static_cast<uint8_t>(IdState::IdCmd);
                    _s.seqIndex = 0;
                    ++_counters.idSequences;
                    Note(now, "ID sequence complete at " + Hex(port, 3) + ": ID_CMD");
                }
            }
            else
                _s.seqIndex = 0;
            return;
        case IdState::IdCmd:
            break;
    }
    if (port != _s.idPort)
        return;
    if (value < 0x80)
    {
        _s.idState = static_cast<uint8_t>(IdState::IdWait);
        _s.seqIndex = 0;
        return;
    }
    if (value < 0xC0)
    {
        StartEepromCommand(value, now);
        ++_counters.idEepromReads;
        return;
    }
    if (value < 0xD0)
    {
        Note(now, "ID Global Reset " + Hex(value, 2));
        PowerOnReset(now);
        return;
    }
    if (value < 0xD8)
    {
        const uint8_t tag = static_cast<uint8_t>(value & 7);
        if (tag != 0 && _s.tag != 0)
            return;   // a tagged card ignores D1-D7
        _s.tag = tag;
        return;
    }
    if (value < 0xE0)
    {
        if (_s.tag != (value & 7))
        {
            _s.idState = static_cast<uint8_t>(IdState::IdWait);
            _s.seqIndex = 0;
        }
        return;
    }
    if (value != 0xFF)
        _s.addressConfig = static_cast<uint16_t>((_s.addressConfig & ~0x1Fu) | (value & 0x1Fu));
    _s.activated = 1;
    _s.idState = static_cast<uint8_t>(IdState::IdWait);
    _s.seqIndex = 0;
    ++_counters.activations;
    Note(now, std::string("activated at ") + Hex(IoBase(), 3) + (value == 0xFF ? " (the EEPROM's base)" : ""));
}

uint8_t EtherLink3::IdRead(uint16_t port, uint64_t now, bool peek)
{
    if (static_cast<IdState>(_s.idState) != IdState::IdCmd || port != _s.idPort || _s.tag != 0)
        return 0xFF;   // the card does not drive the bus: the pull-ups
    uint16_t data = _s.eepromData;
    if (peek && _s.eepromPending == 1 && now >= _s.eepromBusyUntil)
        data = _eeprom[_s.eepromAddress];
    const uint8_t bit = static_cast<uint8_t>((data >> 15) & 1);
    if (!peek)
        _s.eepromData = static_cast<uint16_t>(_s.eepromData << 1);
    // Bit 15 of the EEPROM data on D0 through an open drain: a 1 leaves the line to the pull-up, like D1-D7
    return static_cast<uint8_t>(0xFE | bit);
}

void EtherLink3::StartEepromCommand(uint8_t command, uint64_t now)
{
    FinishEeprom(now);
    if (now < _s.eepromBusyUntil)
        return;   // busy: writes to the EEPROM command are disabled (TR 7-21)
    _s.eepromCommand = command;
    _s.eepromAddress = static_cast<uint8_t>(command & 0x3F);
    switch (command >> 6)
    {
        case 2:
            _s.eepromPending = 1;
            _s.eepromBusyUntil = now + kEepromReadT;
            return;
        case 1:
            _s.eepromPending = 2;
            _s.eepromBusyUntil = now + kEepromWriteT;
            return;
        case 3:
            _s.eepromPending = 3;
            _s.eepromBusyUntil = now + kEepromWriteT;
            return;
        default:
            break;
    }
    switch ((command >> 4) & 3)
    {
        case 3:
            _s.eepromWriteEnable = 1;
            _s.eepromPending = 6;
            _s.eepromBusyUntil = now + kEepromEnableT;
            break;
        case 0:
            _s.eepromWriteEnable = 0;
            _s.eepromPending = 6;
            _s.eepromBusyUntil = now + kEepromEnableT;
            break;
        case 2:
            _s.eepromPending = 4;
            _s.eepromBusyUntil = now + kEepromWriteT;
            break;
        default:
            _s.eepromPending = 5;
            _s.eepromBusyUntil = now + kEepromWriteT;
            break;
    }
}

void EtherLink3::FinishEeprom(uint64_t now)
{
    if (!_s.eepromPending || now < _s.eepromBusyUntil)
        return;
    const uint8_t a = _s.eepromAddress;
    switch (_s.eepromPending)
    {
        case 1:
            _s.eepromData = _eeprom[a];
            break;
        case 2:   // a write programs zeros only (TR 7-22)
            if (_s.eepromWriteEnable)
                _eeprom[a] = static_cast<uint16_t>(_eeprom[a] & _s.eepromData);
            break;
        case 3:
            if (_s.eepromWriteEnable)
                _eeprom[a] = 0xFFFF;
            break;
        case 4:
            if (_s.eepromWriteEnable)
                _eeprom.fill(0xFFFF);
            break;
        case 5:
            if (_s.eepromWriteEnable)
            {
                for (uint16_t& w : _eeprom)
                    w = static_cast<uint16_t>(w & _s.eepromData);
            }
            break;
        default:
            break;
    }
    if (_s.eepromPending >= 2 && _s.eepromPending <= 5)
        _s.eepromWriteEnable = 0;   // the hardware disables erase / write after each one
    _s.eepromPending = 0;
}

// ---------------------------------------------------------------------------
// The bus side
// ---------------------------------------------------------------------------

uint16_t EtherLink3::IoBase() const
{
    const unsigned field = _s.addressConfig & 0x1F;
    return field == 0x1F ? 0 : static_cast<uint16_t>(0x200 + field * 0x10);   // 1Fh: EISA slot addressing
}

bool EtherLink3::Decodes(uint32_t address, uint16_t& offset) const
{
    const uint32_t a = address & 0xFFFF;
    if (Deaf(Now()))
        return false;
    if ((a & 0xFF0F) == 0x0100)
    {
        offset = static_cast<uint16_t>(kIdPortOffset | ((a >> 4) & 0x0F));
        return true;
    }
    const uint16_t base = IoBase();
    if (_s.activated && base && (a & 0xFFF0) == base)
    {
        offset = static_cast<uint16_t>(a & 0x0F);
        return true;
    }
    return false;
}

std::string EtherLink3::DecodeNote() const
{
    return std::string("A15-A0 decoded, no mirrors: #9FBD A15-A14 = 0; the 16 registers answer only after activation "
                       "through the ID port (now: ") +
           (_s.activated ? "active at " + Hex(IoBase(), 3) : std::string("not active")) + ")";
}

bool EtherLink3::IoRange(uint32_t& first, uint32_t& last) const
{
    const uint16_t base = IoBase();
    if (!base)
        return false;
    first = base;
    last = base + 0x0Fu;
    return true;
}

std::vector<IIoBusDevice::AuxIoRange> EtherLink3::AuxIoRanges() const
{
    AuxIoRange id;
    id.name = "id_port";
    id.first = 0x100;
    id.last = 0x1F0;
    id.step = 0x10;
    std::string state;
    switch (static_cast<IdState>(_s.idState))
    {
        case IdState::AutoInit: state = "AUTOINIT (reading the EEPROM, deaf)"; break;
        case IdState::IdWait:
            state = _s.idPort ? "ID_WAIT at " + Hex(_s.idPort, 3) + ", sequence " + std::to_string(_s.seqIndex) + "/255"
                              : std::string("ID_WAIT, no ID port picked yet (a 0 written to any #1x0 picks it)");
            break;
        case IdState::IdCmd:
            state = "ID_CMD at " + Hex(_s.idPort, 3) + ", tag " + std::to_string(_s.tag) +
                    (_s.tag ? " (tagged: reads not answered)" : " (reads: EEPROM data bit 15 on D0)");
            break;
    }
    id.note = "writes to any of them watched (the ID sequence); " + state;
    return {id};
}

int EtherLink3::IrqLine() const
{
    const int irq = _s.resourceConfig >> 12;
    return ValidIrq(irq) ? irq : -1;
}

bool EtherLink3::IrqDriven() const
{
    // TR 7-13, 7-15, 7-20: the drivers need ENA, a valid IRQ, and are off while window 0 is selected; an 8-bit slot
    // has IRQ 2/9, 3-7 only (10, 11, 12, 15 are on the 16-bit connector)
    const int irq = IrqLine();
    return _s.enable && _s.window != 0 && (irq == 3 || irq == 5 || irq == 7 || irq == 9);
}

uint64_t EtherLink3::NextIrqEventAt() const
{
    if (!_s.txInFlight || _tx.empty())
        return UINT64_MAX;
    const TxPacket& head = _tx.front();
    if (!head.complete)
        return _s.txStartedAt + ((8 + head.data.size()) * 28 + 9) / 10;
    return _s.txDoneAt;
}

bool EtherLink3::IsByteRegister(uint8_t offset) const
{
    switch (_s.window)
    {
        case 1: return offset < 4 || offset == 0x0A || offset == 0x0B;
        case 2: return offset < 0x0E;
        case 3: return offset == 0x04 || offset == 0x05;
        case 6: return offset < 0x0A;
        default: return false;
    }
}

uint8_t EtherLink3::Read(uint16_t offset)
{
    const uint64_t now = Now();
    Advance(now);
    if (Deaf(now))
        return 0xFF;
    if (offset >= kIdPortOffset)
    {
        _s.readHighOffset = 0xFF;
        _s.writeLowValid = 0;
        return IdRead(static_cast<uint16_t>(0x100 | ((offset & 0x0F) << 4)), now, false);
    }
    const uint8_t reg = static_cast<uint8_t>(offset & 0x0F);
    _s.writeLowValid = 0;
    if (_s.poweredDown)
        return 0xFF;
    uint8_t value = 0;
    if (reg < 0x0E && IsByteRegister(reg))
    {
        _s.readHighOffset = 0xFF;
        bool isByte = true;
        value = ReadByteRegister(reg, now, false, isByte);
    }
    else if (!(reg & 1))
    {
        const uint16_t word = ReadWord(reg, now, false);
        _s.readHigh = static_cast<uint8_t>(word >> 8);
        _s.readHighOffset = static_cast<uint8_t>(reg + 1);
        value = static_cast<uint8_t>(word);
    }
    else if (_s.readHighOffset == reg)
    {
        _s.readHighOffset = 0xFF;
        value = _s.readHigh;
    }
    else
        value = static_cast<uint8_t>(ReadWord(static_cast<uint8_t>(reg - 1), now, false) >> 8);
    UpdateIrq(now);
    return value;
}

uint8_t EtherLink3::Peek(uint16_t offset) const
{
    auto* self = const_cast<EtherLink3*>(this);
    const uint64_t now = Now();
    if (Deaf(now))
        return 0xFF;
    if (offset >= kIdPortOffset)
        return self->IdRead(static_cast<uint16_t>(0x100 | ((offset & 0x0F) << 4)), now, true);
    const uint8_t reg = static_cast<uint8_t>(offset & 0x0F);
    if (_s.poweredDown)
        return 0xFF;
    if (reg < 0x0E && IsByteRegister(reg))
    {
        bool isByte = true;
        return self->ReadByteRegister(reg, now, true, isByte);
    }
    if (!(reg & 1))
        return static_cast<uint8_t>(self->ReadWord(reg, now, true));
    if (_s.readHighOffset == reg)
        return _s.readHigh;
    return static_cast<uint8_t>(self->ReadWord(static_cast<uint8_t>(reg - 1), now, true) >> 8);
}

uint8_t EtherLink3::ReadByteRegister(uint8_t offset, uint64_t now, bool peek, bool& isByte)
{
    isByte = true;
    switch (_s.window)
    {
        case 1:
            if (offset < 4)
                return RxPop(peek);
            if (offset == 0x0A)
            {
                // The latency timer: 10 MHz / 32 (3.2 us), from 0 at the interrupt's rise, stops at 255 (TR 6-22)
                const uint64_t ticks = now > _s.timerBase ? (now - _s.timerBase) * 10 / 112 : 0;
                return static_cast<uint8_t>(std::min<uint64_t>(ticks, 255));
            }
            if (offset == 0x0B)
                return _s.txStatusCount ? static_cast<uint8_t>(_s.txStatus[0] | (_s.txStatusOverflow ? 0x04 : 0)) : 0;
            break;
        case 2:
            return offset < 6 ? _s.station[offset] : 0;
        case 3:
            return offset == 0x05 ? static_cast<uint8_t>(_s.romControl & 3) : 0;
        case 6:
            return offset < 9 ? static_cast<uint8_t>(ReadStat(offset, peek)) : 0;   // 09: reserved
        default:
            break;
    }
    return 0;
}

uint16_t EtherLink3::ReadWord(uint8_t offset, uint64_t now, bool peek)
{
    if (offset == 0x0E)
    {
        uint16_t status = Status();
        if (now < _s.cipUntil)
            status |= 0x1000;
        return status;
    }
    switch (_s.window)
    {
        case 0:
            switch (offset)
            {
                case 0x00: return kManufacturerId;
                case 0x02: return _s.productId;
                case 0x04:
                {
                    // PORreg (TR 7-14): ISA, the AUI connector on the TP board, no BNC, normal mode, 10BASE-T, VCO
                    uint16_t v = 0x4F00;
                    if (_settings.variant == Variant::Tp)
                        v |= 0x2000;
                    return static_cast<uint16_t>(v | (_s.enable ? 1 : 0));
                }
                case 0x06: return _s.addressConfig;
                case 0x08: return _s.resourceConfig;
                case 0x0A:
                {
                    if (!peek)
                        FinishEeprom(now);
                    const bool busy = now < _s.eepromBusyUntil;
                    return static_cast<uint16_t>((busy ? 0x8000 : 0) | ((_s.tag & 7) << 8) | _s.eepromCommand);
                }
                case 0x0C:
                    if (!peek)
                        FinishEeprom(now);
                    else if (_s.eepromPending == 1 && now >= _s.eepromBusyUntil)
                        return _eeprom[_s.eepromAddress];
                    return _s.eepromData;
                default: return 0;
            }
        case 1:
            if (offset == 0x08)
                return RxStatus();
            if (offset == 0x0C)
                return static_cast<uint16_t>(TxFree() & ~3u);   // dword-truncated in window 1 (TR 6-21)
            return 0;
        case 3:
            switch (offset)
            {
                case 0x00: return static_cast<uint16_t>(_s.internalConfig);
                case 0x02: return static_cast<uint16_t>(_s.internalConfig >> 16);
                case 0x0A: return RxFree();
                case 0x0C: return TxFree();
                default: return 0;
            }
        case 4:
            switch (offset)
            {
                case 0x04: return FifoDiagnostic();
                case 0x06: return NetDiagnostic(now);
                case 0x08: return static_cast<uint16_t>(_s.ecStatus & 1);
                case 0x0A: return MediaStatus(now);
                default: return 0;
            }
        case 5:
            switch (offset)
            {
                case 0x00: return _s.txStartThreshold;
                case 0x02: return _s.txAvailThreshold;
                case 0x06: return _s.rxEarly;
                case 0x08: return static_cast<uint16_t>(_s.rxFilter & 0x0F);
                case 0x0A: return _s.intMask;
                case 0x0C: return _s.readZeroMask;
                default: return 0;
            }
        case 6:
            if (offset == 0x0A)
                return ReadStat(kStatRxBytes, peek);
            if (offset == 0x0C)
                return ReadStat(kStatTxBytes, peek);
            return 0;
        default:
            return 0;
    }
}

uint16_t EtherLink3::FifoDiagnostic() const
{
    // TR 6-30: RX underrun, RX overrun (the FIFO cannot take a minimum frame), TX overrun
    return static_cast<uint16_t>((_s.rxUnderrun ? 0x2000 : 0) | (size_t(RxFree()) < kMinFrame + kPacketOverhead ? 0x0800 : 0) |
                                 (_s.txOverrun ? 0x0400 : 0));
}

uint16_t EtherLink3::NetDiagnostic(uint64_t now) const
{
    // TR 6-28: the loopback bits, TX / RX enabled, transmitting, TX reset needed, statistics, ASIC revision 2
    const bool transmitting = _s.txInFlight && now >= _s.txStartedAt;
    return static_cast<uint16_t>((_s.netDiag & 0xF000) | (_s.txEnabled ? 0x0800 : 0) | (_s.rxEnabled ? 0x0400 : 0) |
                                 (transmitting ? 0x0200 : 0) | (_s.txResetNeeded ? 0x0100 : 0) |
                                 (_s.statsEnabled ? 0x0080 : 0) | (2 << 1));
}

uint16_t EtherLink3::MediaStatus(uint64_t now) const
{
    // TR 6-26: TP enabled (XCVR 00), bit 13 always 1, link beat detected, the writable bits, carrier while sending
    const bool tp = (_s.addressConfig >> 14) == 0;
    const bool linkBeat = (_s.media & 0x80) != 0;
    const bool link = linkBeat && tp && _link && now >= LinkUpAt();
    const bool carrier = _s.txInFlight && now >= _s.txStartedAt;
    return static_cast<uint16_t>((tp ? 0x8000 : 0) | 0x2000 | (link ? 0x0800 : 0) | (_s.media & 0x00CC) |
                                 (carrier ? 0x0020 : 0));
}

void EtherLink3::Write(uint16_t offset, uint8_t value)
{
    const uint64_t now = Now();
    Advance(now);
    if (Deaf(now))
        return;
    _s.readHighOffset = 0xFF;
    if (offset >= kIdPortOffset)
    {
        _s.writeLowValid = 0;
        IdWrite(static_cast<uint16_t>(0x100 | ((offset & 0x0F) << 4)), value, now);
        UpdateIrq(now);
        return;
    }
    const uint8_t reg = static_cast<uint8_t>(offset & 0x0F);
    if (reg < 0x0E && IsByteRegister(reg) && !_s.poweredDown)
    {
        _s.writeLowValid = 0;
        WriteByteRegister(reg, value, now);
        UpdateIrq(now);
        return;
    }
    if (!(reg & 1))
    {
        _s.writeLow = value;
        _s.writeLowValid = static_cast<uint8_t>(0x80 | reg);
        return;
    }
    const uint8_t low = _s.writeLowValid == (0x80 | (reg - 1)) ? _s.writeLow : 0;
    _s.writeLowValid = 0;
    const uint16_t word = static_cast<uint16_t>(low | (value << 8));
    if (_s.poweredDown)
    {
        // Power Down Full: the only legal access is Power Up (TR 6-11)
        if (reg == 0x0F && (word >> 11) == 0x1B)
            Command(word, now);
        return;
    }
    WriteWord(static_cast<uint8_t>(reg - 1), word, now);
    UpdateIrq(now);
}

bool EtherLink3::WriteByteRegister(uint8_t offset, uint8_t value, uint64_t now)
{
    switch (_s.window)
    {
        case 1:
            if (offset < 4)
            {
                TxPush(value, now);
                return true;
            }
            if (offset == 0x0B)
            {
                // Writing TX status pops the stack, if its top holds a completion (TR 6-18)
                if (_s.txStatusCount && (_s.txStatus[0] & 0x80))
                {
                    std::memmove(_s.txStatus, _s.txStatus + 1, sizeof(_s.txStatus) - 1);
                    --_s.txStatusCount;
                    _s.txStatus[30] = 0;
                    _s.txStatusOverflow = 0;
                    TryStartTransmit(now);
                }
                return true;
            }
            return true;
        case 2:
            if (offset < 6)
                _s.station[offset] = value;
            return true;
        case 3:
            if (offset == 0x05)
                _s.romControl = static_cast<uint8_t>(value & 3);
            return true;
        case 6:
            // A write adds to the counter while statistics are disabled (TR 6-23)
            if (offset < 9 && !_s.statsEnabled)
            {
                _s.stats[offset] = static_cast<uint16_t>((_s.stats[offset] + value) & kStatMask[offset]);
                UpdateStatsFlag();
            }
            return true;
        default:
            return false;
    }
}

void EtherLink3::WriteWord(uint8_t offset, uint16_t value, uint64_t now)
{
    if (offset == 0x0E)
    {
        Command(value, now);
        return;
    }
    switch (_s.window)
    {
        case 0:
            switch (offset)
            {
                case 0x04:
                    if (value & 0x04)
                    {
                        Note(now, "Configuration Control RST");
                        PowerOnReset(now);
                        return;
                    }
                    _s.enable = static_cast<uint8_t>(value & 1);
                    return;
                case 0x06: _s.addressConfig = value; return;
                case 0x08: _s.resourceConfig = value; return;
                case 0x0A: StartEepromCommand(static_cast<uint8_t>(value), now); return;
                case 0x0C:
                    FinishEeprom(now);
                    if (now >= _s.eepromBusyUntil)
                        _s.eepromData = value;
                    return;
                default: return;
            }
        case 3:
            if (offset == 0x00)
                _s.internalConfig = (_s.internalConfig & 0xFFFF0000u) | (value & ~0x8u);
            else if (offset == 0x02)
                _s.internalConfig = (_s.internalConfig & 0xFFFFu) | (static_cast<uint32_t>(value) << 16);
            return;
        case 4:
            switch (offset)
            {
                case 0x06: _s.netDiag = static_cast<uint16_t>(value & 0xF000); return;   // bit 0 (the LV test): ignored
                case 0x08: _s.ecStatus = static_cast<uint16_t>(value & 1); return;
                case 0x0A:
                {
                    const bool before = (_s.media & 0x80) != 0;
                    _s.media = static_cast<uint16_t>(value & 0x00CC);
                    const bool after = (_s.media & 0x80) != 0;
                    if (after && !before)
                        _s.linkBeatSince = now;
                    else if (!after)
                        _s.linkBeatSince = UINT64_MAX;
                    return;
                }
                default: return;
            }
        case 6:
            if ((offset == 0x0A || offset == 0x0C) && !_s.statsEnabled)
            {
                const int index = offset == 0x0A ? kStatRxBytes : kStatTxBytes;
                _s.stats[index] = static_cast<uint16_t>(_s.stats[index] + value);
                UpdateStatsFlag();
            }
            return;
        default:
            return;
    }
}

void EtherLink3::Command(uint16_t word, uint64_t now)
{
    ++_counters.commands;
    const uint16_t arg = static_cast<uint16_t>(word & 0x07FF);
    switch (word >> 11)
    {
        case 0: GlobalReset(arg, now); break;
        case 1: _s.window = static_cast<uint8_t>(arg & 7); break;
        case 2: _s.coax = 1; break;
        case 3: _s.rxEnabled = 0; break;
        case 4: _s.rxEnabled = 1; break;
        case 5: RxReset(arg); break;
        case 8:
            // RX Discard: the top packet (its remaining bytes, its status) goes
            if (!_rx.empty())
                _rx.pop_front();
            break;
        case 9:
            _s.txEnabled = 1;
            TryStartTransmit(now);
            break;
        case 10: _s.txEnabled = 0; break;
        case 11: TxReset(arg, now); break;
        case 12: _s.interruptRequested = 1; break;
        case 13:
            // Acknowledge Interrupt: the latch, TX Available (its threshold back to disabled), RX Early, Interrupt
            // Requested; the other causes follow the card's state (TR 6-7, 6-8)
            if (arg & 0x01)
                _s.latch = 0;
            if (arg & 0x08)
            {
                _s.txAvailable = 0;
                _s.txAvailThreshold = kThresholdDefault;
            }
            if (arg & 0x40)
                _s.interruptRequested = 0;
            break;
        case 14: _s.intMask = static_cast<uint16_t>(arg & 0xFE); break;
        case 15: _s.readZeroMask = static_cast<uint16_t>(arg & 0xFE); break;
        case 16: _s.rxFilter = static_cast<uint16_t>(arg & 0x0F); break;
        case 17: _s.rxEarly = static_cast<uint16_t>(arg & 0x7FC); break;
        case 18: _s.txAvailThreshold = static_cast<uint16_t>(arg & 0x7FC); break;
        case 19:
            _s.txStartThreshold = static_cast<uint16_t>(arg & 0x7FC);
            TryStartTransmit(now);
            break;
        case 21:
            _s.statsEnabled = 1;
            for (int i = 0; i < 11; ++i)
            {
                if (_s.statsPending[i])
                {
                    _s.stats[i] = static_cast<uint16_t>((_s.stats[i] + _s.statsPending[i]) & kStatMask[i]);
                    _s.statsPending[i] = 0;
                }
            }
            UpdateStatsFlag();
            break;
        case 22: _s.statsEnabled = 0; break;
        case 23: _s.coax = 0; break;
        case 24: break;   // Set TX Reclaim Threshold: the MCA 3C529's (TR 6-11)
        case 27:
            if (_s.poweredDown)
                Note(now, "Power Up");
            _s.poweredDown = 0;
            break;
        case 28:
            _s.poweredDown = 1;
            _s.coax = 0;
            Note(now, "Power Down Full");
            break;
        case 29: break;   // Power Auto: powers up again by itself on any activity
        default:
            Note(now, "unknown command " + Hex(word, 4) + " ignored");
            break;
    }
}

// ---------------------------------------------------------------------------
// The FIFOs
// ---------------------------------------------------------------------------

uint16_t EtherLink3::TxCapacity() const
{
    // Internal configuration (TR 7-23): RAM size bits 2-0 (000 8 KB, 010 32 KB), partition bits 17-16 TX:RX
    const uint32_t ram = (_s.internalConfig & 7) == 2 ? 32768u : 8192u;
    switch ((_s.internalConfig >> 16) & 3)
    {
        case 1: return static_cast<uint16_t>(ram / 4);
        case 2: return static_cast<uint16_t>(ram / 2);
        default: return static_cast<uint16_t>(ram * 3 / 8);
    }
}

uint16_t EtherLink3::RxCapacity() const
{
    const uint32_t ram = (_s.internalConfig & 7) == 2 ? 32768u : 8192u;
    return static_cast<uint16_t>(std::min<uint32_t>(ram - TxCapacity(), 0x7FFF));
}

uint16_t EtherLink3::TxFree() const
{
    int used = kFifoReserve;
    for (const TxPacket& p : _tx)
        used += p.complete ? p.Expected() + kPacketOverhead : static_cast<int>(p.data.size());
    const int free = static_cast<int>(TxCapacity()) - used;
    return static_cast<uint16_t>(std::max(free, 0));
}

uint16_t EtherLink3::RxFree() const
{
    int used = kFifoReserve;
    for (const RxPacket& p : _rx)
        used += static_cast<int>(((p.data.size() + 3) & ~size_t(3)) + kPacketOverhead);
    if (!_rx.empty())
        used -= std::min<int>(_rx.front().readPos, static_cast<int>((_rx.front().data.size() + 3) & ~size_t(3)));
    const int free = static_cast<int>(RxCapacity()) - used;
    return static_cast<uint16_t>(std::max(free, 0));
}

uint16_t EtherLink3::RxStatus() const
{
    if (_rx.empty())
        return 0x8000;   // incomplete / the FIFO is empty
    const RxPacket& top = _rx.front();
    const int remaining = static_cast<int>(top.data.size()) - static_cast<int>(top.readPos);
    uint16_t status = static_cast<uint16_t>(remaining & 0x7FF);   // counts down past 0 into the pad: -1 = 7FFh
    if (top.error)
        status |= static_cast<uint16_t>(0x4000 | ((top.error & 7) << 11));
    return status;
}

uint8_t EtherLink3::RxPop(bool peek)
{
    if (_rx.empty() || size_t(_rx.front().readPos) >= ((_rx.front().data.size() + 3) & ~size_t(3)))
    {
        // A read past the packet's pad: RX underrun, Adapter Failure (TR 6-30)
        if (!peek && !_s.rxUnderrun)
        {
            _s.rxUnderrun = 1;
            ++_counters.rxUnderruns;
            Note(Now(), "RX underrun: a read past the top packet's pad (RX Reset needed)");
        }
        return 0;
    }
    RxPacket& top = _rx.front();
    const uint8_t value = size_t(top.readPos) < top.data.size() ? top.data[top.readPos] : 0;
    if (!peek)
        ++top.readPos;
    return value;
}

void EtherLink3::TxPush(uint8_t value, uint64_t now)
{
    const bool assembling = !_tx.empty() && !_tx.back().complete;
    if (!assembling)
    {
        // The two preamble words: taken as they come, they use no FIFO room (TR 10-7)
        _s.preamble[_s.preambleBytes++] = value;
        if (_s.preambleBytes < 4)
            return;
        _s.preambleBytes = 0;
        TxPacket p;
        p.length = static_cast<uint16_t>((_s.preamble[0] | (_s.preamble[1] << 8)) & 0x7FF);
        p.interruptOnSuccess = (_s.preamble[1] & 0x80) != 0;
        p.noCrc = (_s.preamble[1] & 0x20) != 0;
        p.complete = p.Expected() == 0u;
        _tx.push_back(std::move(p));
        TryStartTransmit(now);
        return;
    }
    TxPacket& tail = _tx.back();
    if (!tail.discard && TxFree() == 0)
    {
        // TX overrun: more data than room - Adapter Failure, the transmitter stops (TR 6-30)
        if (!_s.txOverrun)
        {
            ++_counters.txOverruns;
            Note(now, "TX overrun: a write into a full TX FIFO (TX Reset needed)");
        }
        _s.txOverrun = 1;
        _s.txEnabled = 0;
        return;
    }
    tail.data.push_back(value);
    if (tail.data.size() < size_t(tail.Expected()))
    {
        if (!_s.txInFlight && !tail.discard && _tx.size() == 1)
            TryStartTransmit(now);   // past the TX start threshold: an early start
        return;
    }
    tail.complete = true;
    if (tail.discard)
    {
        // The rest of an underrun packet: nowhere to go
        if (_s.txInFlight && _tx.size() == 1)
            _s.txInFlight = 0;
        _tx.pop_back();
        TryStartTransmit(now);
        return;
    }
    if (_s.txInFlight && _tx.size() == 1)
    {
        // An early start: the packet now has its end, the wire time runs from the start
        const size_t wire = std::max<size_t>(tail.length, kMinFrame) + (tail.noCrc ? 0 : 4);
        _s.txDoneAt = _s.txStartedAt + Dp8390::WireTime(wire);
        return;
    }
    TryStartTransmit(now);
}

void EtherLink3::TryStartTransmit(uint64_t now)
{
    while (!_tx.empty() && _tx.front().discard && _tx.front().complete)
        _tx.pop_front();
    if (_s.txInFlight || !_s.txEnabled || _s.txStatusOverflow || _s.poweredDown || _tx.empty())
        return;
    const TxPacket& head = _tx.front();
    if (head.discard)
        return;
    const bool early = _s.txStartThreshold <= kThresholdLimit && head.data.size() > size_t(_s.txStartThreshold);
    if (!head.complete && !early)
        return;
    _s.txInFlight = 1;
    _s.txStartedAt = std::max(now, _s.wireFreeAt);
    const size_t wire = std::max<size_t>(head.length, kMinFrame) + (head.noCrc ? 0 : 4);
    _s.txDoneAt = head.complete ? _s.txStartedAt + Dp8390::WireTime(wire) : UINT64_MAX;
    if (_irqListener)
        _irqListener();   // NextIrqEventAt moved
}

void EtherLink3::PushTxStatus(uint8_t status)
{
    if (_s.txStatusCount < 31)
        _s.txStatus[_s.txStatusCount++] = status;
    if (_s.txStatusCount == 31)
        _s.txStatusOverflow = 1;   // the transmitter waits until the host pops one (TR 6-19)
    if (status & 0x3C)
        _s.txEnabled = 0;   // any error disables the transmitter (TR 4-2)
}

void EtherLink3::FinishTransmit(uint64_t at)
{
    TxPacket head = std::move(_tx.front());
    _tx.pop_front();
    _s.txInFlight = 0;
    _s.wireFreeAt = at;
    std::vector<uint8_t> frame(head.data.begin(), head.data.begin() + std::min<size_t>(head.length, head.data.size()));
    bool good = true;
    if (head.noCrc)
    {
        // The data carries its own CRC (TR 4-3): a wrong one makes every receiver drop the frame
        if (frame.size() >= 4)
        {
            const size_t body = frame.size() - 4;
            const uint32_t crc = Dp8390::Crc32(frame.data(), body);
            const uint32_t sent = static_cast<uint32_t>(frame[body] | (frame[body + 1] << 8) | (frame[body + 2] << 16) |
                                                        (static_cast<uint32_t>(frame[body + 3]) << 24));
            good = crc == sent;
            frame.resize(body);
        }
        else
            good = false;
    }
    else if (frame.size() < kMinFrame)
        frame.resize(kMinFrame, 0);   // the adapter pads to the minimum (TR 3-2)
    if (good)
        Deliver(frame);
    if (head.interruptOnSuccess)
        PushTxStatus(0xC0);
    CountStat(kStatTxFrames, 1);
    CountStat(kStatTxBytes, head.length);
    ++_counters.txFrames;
    if (_s.pendingTxReset != 0xFFFF)
    {
        const uint16_t arg = _s.pendingTxReset;
        _s.pendingTxReset = 0xFFFF;
        TxReset(static_cast<uint16_t>(arg | 0x400), at);
    }
    TryStartTransmit(at);
}

bool EtherLink3::LinkPass(uint64_t now) const
{
    // 10BASE-T: the cable, the TP transceiver selected (XCVR 00), and the link integrity test passed - or no link
    // test at all (link beat disabled: the MAU does not check)
    if (!_link || (_s.addressConfig >> 14) != 0)
        return false;
    return !(_s.media & 0x80) || now >= LinkUpAt();
}

uint64_t EtherLink3::LinkUpAt() const
{
    if (_s.linkBeatSince == UINT64_MAX)
        return UINT64_MAX;
    return std::max(_s.linkBeatSince, _s.cableSince) + kLinkTestT;
}

void EtherLink3::Deliver(const std::vector<uint8_t>& frame)
{
    const uint64_t now = Now();
    const uint16_t loop = static_cast<uint16_t>(_s.netDiag & 0xF000);
    // Internal loopbacks (FIFO, Ethernet controller, ENDEC) keep the frame off the wire; external loopback both
    if (!(loop & 0x7000))
    {
        if (LinkPass(now))
            _link->Transmit(*this, frame.data(), frame.size());
        else
        {
            ++_counters.txNoLink;
            CountStat(kStatCarrierLost, 1);
        }
    }
    if (loop)
    {
        const bool fifoLoop = (loop & 0x1000) != 0;
        if (_s.rxEnabled && (fifoLoop || Accepts(frame.data(), frame.size())))
        {
            RxPacket p;
            p.data = frame;
            if (size_t(RxFree()) >= ((p.data.size() + 3) & ~size_t(3)) + kPacketOverhead)
            {
                _rx.push_back(std::move(p));
                CountStat(kStatRxFrames, 1);
                CountStat(kStatRxBytes, static_cast<uint16_t>(frame.size()));
            }
            else
                CountStat(kStatRxOverruns, 1);
        }
    }
}

bool EtherLink3::Accepts(const uint8_t* frame, size_t length) const
{
    if (length < 6)
        return false;
    const uint16_t f = _s.rxFilter;
    if (f & 0x08)
        return true;   // promiscuous
    if (frame[0] & 1)
    {
        const bool broadcast = std::all_of(frame, frame + 6, [](uint8_t b) { return b == 0xFF; });
        return broadcast ? (f & 0x06) != 0 : (f & 0x02) != 0;   // group reception implies broadcast (TR 6-9)
    }
    return (f & 0x01) && std::memcmp(frame, _s.station, 6) == 0;
}

bool EtherLink3::Offer(const uint8_t* frame, size_t length)
{
    const uint64_t now = Now();
    Advance(now);
    if (!_s.rxEnabled || _s.poweredDown || !LinkPass(now) || !Accepts(frame, length))
    {
        ++_counters.rxFiltered;
        return true;
    }
    RxPacket p;
    p.data.assign(frame, frame + std::min(length, kMaxReceive));
    if (_s.media & 0x04)
    {
        // CRC strip disabled: the frame's CRC comes along (TR 6-27)
        const uint32_t crc = Dp8390::Crc32(frame, length);
        for (int i = 0; i < 4; ++i)
            p.data.push_back(static_cast<uint8_t>(crc >> (8 * i)));
    }
    const size_t limit = (_s.media & 0x04) ? kMaxFrame + 4 : kMaxFrame;
    if (p.data.size() < kMinFrame)
        p.error = 0x80 | 3;   // runt
    else if (p.data.size() > limit)
        p.error = 0x80 | 1;   // oversize
    const size_t need = ((p.data.size() + 3) & ~size_t(3)) + kPacketOverhead;
    if (size_t(RxFree()) < need)
    {
        ++_counters.rxFifoFullWaits;
        return false;   // the switch keeps it (network tdd §7.6)
    }
    const uint16_t bytes = static_cast<uint16_t>(p.data.size());
    const bool error = p.error != 0;
    _rx.push_back(std::move(p));
    ++_counters.rxFrames;
    if (!error)
    {
        CountStat(kStatRxFrames, 1);
        CountStat(kStatRxBytes, bytes);
    }
    UpdateIrq(now);
    return true;
}

void EtherLink3::SetLink(IEthernetLink* link)
{
    if (link && !_link)
        _s.cableSince = Now();   // a cable plugged in: the link test starts over
    _link = link;
}

void EtherLink3::StationMac(uint8_t out[6]) const
{
    for (int i = 0; i < 3; ++i)
    {
        out[2 * i] = static_cast<uint8_t>(_eeprom[i] >> 8);
        out[2 * i + 1] = static_cast<uint8_t>(_eeprom[i]);
    }
}

// ---------------------------------------------------------------------------
// Status, interrupts, statistics
// ---------------------------------------------------------------------------

uint8_t EtherLink3::RawStatusBits() const
{
    uint8_t bits = 0;
    if (_s.txOverrun || _s.rxUnderrun)
        bits |= 0x02;   // Adapter Failure
    if (_s.txStatusCount)
        bits |= 0x04;   // TX Complete
    if (_s.txAvailable)
        bits |= 0x08;
    if (!_rx.empty())
        bits |= 0x10;   // RX Complete: the top packet is whole (frames arrive whole)
    if (_s.interruptRequested)
        bits |= 0x40;
    if (_s.updateStats)
        bits |= 0x80;
    return bits;
}

uint16_t EtherLink3::Status() const
{
    // TR 6-13: window, then the causes through the read zero mask; the latch (bit 0) is not maskable
    return static_cast<uint16_t>((_s.window << 13) | (RawStatusBits() & _s.readZeroMask & 0xFE) | (_s.latch ? 1 : 0));
}

void EtherLink3::UpdateIrq(uint64_t now)
{
    // TX Available: the free room passed an enabled threshold (TR 6-10)
    if (_s.txAvailThreshold <= kThresholdLimit && TxFree() > _s.txAvailThreshold)
        _s.txAvailable = 1;
    const bool before = _s.latch != 0;
    if ((RawStatusBits() & _s.readZeroMask & _s.intMask & 0xFE) != 0)
        _s.latch = 1;
    if (_s.latch && !before)
        _s.timerBase = now;   // the timer restarts when the output goes active (TR 6-22)
    if (before != (_s.latch != 0) && _irqListener)
        _irqListener();
}

void EtherLink3::CountStat(int index, uint16_t amount)
{
    if (!_s.statsEnabled)
    {
        _s.statsPending[index] = amount;   // one update request latched per counter (TR 6-10)
        return;
    }
    _s.stats[index] = static_cast<uint16_t>((_s.stats[index] + amount) & kStatMask[index]);
    UpdateStatsFlag();
}

void EtherLink3::UpdateStatsFlag()
{
    // Update Statistics: a counter reached half its range; the 16-bit ones when their top three bits are set (TR 6-23)
    bool half = false;
    for (int i = 0; i < 9; ++i)
        half = half || (_s.stats[i] & ((kStatMask[i] + 1) >> 1)) != 0;
    half = half || (_s.stats[kStatRxBytes] & 0xE000) == 0xE000 || (_s.stats[kStatTxBytes] & 0xE000) == 0xE000;
    _s.updateStats = half ? 1 : 0;
}

uint16_t EtherLink3::ReadStat(int index, bool peek)
{
    const uint16_t value = _s.stats[index];
    if (!peek)
    {
        _s.stats[index] = 0;   // reading a statistic zeroes it
        UpdateStatsFlag();
    }
    return value;
}

std::string EtherLink3::IrqCause() const
{
    static const char* const kBits[8] = {"IL", "AF", "TC", "TA", "RC", "RE", "IR", "US"};
    const uint8_t pending = static_cast<uint8_t>(RawStatusBits() & _s.readZeroMask & _s.intMask & 0xFE);
    std::string active;
    for (int b = 1; b < 8; ++b)
    {
        if (pending & (1u << b))
            active += (active.empty() ? "" : "+") + std::string(kBits[b]);
    }
    std::string text = "status " + Hex(Status(), 4) + ", interrupt mask " + Hex(_s.intMask, 2) + ", read zero mask " +
                       Hex(_s.readZeroMask, 2) + (active.empty() ? ": nothing enabled is pending" : ": " + active) +
                       (_s.latch ? ", interrupt latch set" : "");
    const int irq = IrqLine();
    text += ", IRQ " + (irq >= 0 ? std::to_string(irq) : std::string("none (resource configuration disables the drivers)"));
    if (!_s.enable)
        text += ", ENA clear (the drivers are off)";
    else if (_s.window == 0)
        text += ", window 0 selected (the drivers are off)";
    else if (irq == 10 || irq == 11 || irq == 12 || irq == 15)
        text += " is on the 16-bit connector (not in an 8-bit slot)";
    return text;
}

// ---------------------------------------------------------------------------
// Reports
// ---------------------------------------------------------------------------

const char* EtherLink3::RegisterName(uint16_t offset, bool write) const
{
    if (offset >= kIdPortOffset)
        return "ID port";
    offset &= 0x0F;
    if (offset >= 0x0E)
        return write ? "command" : "status";
    static const char* const kRead[8][14] = {
        {"manufacturer ID", "manufacturer ID hi", "product ID", "product ID hi", "configuration control", "configuration control hi",
         "address configuration", "address configuration hi", "resource configuration", "resource configuration hi",
         "EEPROM command", "EEPROM command hi", "EEPROM data", "EEPROM data hi"},
        {"RX PIO data", "RX PIO data", "RX PIO data", "RX PIO data", "-", "-", "-", "-", "RX status", "RX status hi",
         "timer", "TX status", "TX free", "TX free hi"},
        {"station address 0", "station address 1", "station address 2", "station address 3", "station address 4",
         "station address 5", "-", "-", "-", "-", "-", "-", "-", "-"},
        {"internal configuration", "internal configuration", "internal configuration", "internal configuration", "-",
         "ROM control", "-", "-", "-", "-", "RX free", "RX free hi", "TX free", "TX free hi"},
        {"-", "-", "-", "-", "FIFO diagnostic", "FIFO diagnostic hi", "net diagnostic", "net diagnostic hi",
         "Ethernet controller status", "Ethernet controller status hi", "media type and status", "media type and status hi",
         "-", "-"},
        {"TX start threshold", "TX start threshold hi", "TX available threshold", "TX available threshold hi", "-", "-",
         "RX early threshold", "RX early threshold hi", "RX filter", "RX filter hi", "interrupt mask", "interrupt mask hi",
         "read zero mask", "read zero mask hi"},
        {"carrier lost", "no SQE", "multiple collisions", "one collision", "late collisions", "RX overruns",
         "TX frames OK", "RX frames OK", "TX deferrals", "-", "RX bytes OK", "RX bytes OK hi", "TX bytes OK", "TX bytes OK hi"},
        {"-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-"}};
    if (write)
    {
        if (_s.window == 1 && offset < 4)
            return "TX PIO data";
        if (_s.window == 1 && offset == 0x0B)
            return "TX status (pop)";
    }
    return kRead[_s.window & 7][offset];
}

void EtherLink3::Note(uint64_t now, std::string text)
{
    _events.push_back({now, std::move(text)});
    while (_events.size() > kEventCount)
        _events.pop_front();
}

void EtherLink3::Describe(StateNode& out) const
{
    const uint64_t now = Now();
    char mac[24];
    uint8_t m[6];
    StationMac(m);
    std::snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    char station[24];
    std::snprintf(station, sizeof(station), "%02X:%02X:%02X:%02X:%02X:%02X", _s.station[0], _s.station[1],
                  _s.station[2], _s.station[3], _s.station[4], _s.station[5]);
    const uint16_t base = IoBase();
    const int irq = IrqLine();
    const char* idState = _s.idState == 0 ? "AUTOINIT" : _s.idState == 1 ? "ID_WAIT" : "ID_CMD";
    const bool linkBeat = (_s.media & 0x80) != 0;
    std::string link = !_link ? "no cable"
                       : (_s.addressConfig >> 14) != 0 ? "the TP transceiver is not selected (XCVR)"
                       : !linkBeat ? "link beat disabled (no link test)"
                       : now >= LinkUpAt() ? "link pass"
                                           : "link test running";
    out["chip"] = VariantName(_settings.variant);
    out["product_id"] = Hex(_s.productId, 4);
    out["base"] = base ? Hex(base, 3) : std::string("EISA slot addressing");
    out["eeprom_base"] = Hex(0x200 + (_eeprom[0x08] & 0x1F) * 0x10, 3);
    out["id_port"] = _s.idPort ? Hex(_s.idPort, 3) : std::string("none");
    out["ids"] = std::string(idState) + (_s.idState == 1 && _s.idPort ? ", sequence " + std::to_string(_s.seqIndex) + "/255"
                                                                        : std::string()) +
                 ", tag " + std::to_string(_s.tag);
    out["activated"] = _s.activated != 0;
    out["irq"] = irq >= 0 ? StateNode(irq) : StateNode("none");
    out["irq_level"] = Irq();
    out["irq_driven"] = IrqDriven();
    out["mac"] = std::string(mac);
    out["station_address"] = std::string(station);
    out["port_key"] = _settings.key;
    out["link"] = _link ? "ethernet-gateway" : "none";
    out["link_state"] = link;
    out["window"] = static_cast<int>(_s.window);
    out["powered_down"] = _s.poweredDown != 0;

    StateNode r = StateNode::Object();
    r["status"] = Hex(Status() | (now < _s.cipUntil ? 0x1000 : 0), 4);
    r["configuration_control"] = Hex(0x4F00 | (_settings.variant == Variant::Tp ? 0x2000 : 0) | (_s.enable ? 1 : 0), 4);
    r["address_configuration"] = Hex(_s.addressConfig, 4);
    r["resource_configuration"] = Hex(_s.resourceConfig, 4);
    r["internal_configuration"] = Hex(_s.internalConfig, 8);
    r["interrupt_mask"] = Hex(_s.intMask, 2);
    r["read_zero_mask"] = Hex(_s.readZeroMask, 2);
    r["rx_filter"] = Hex(_s.rxFilter, 1);
    r["rx_early_threshold"] = static_cast<int>(_s.rxEarly);
    r["tx_available_threshold"] = static_cast<int>(_s.txAvailThreshold);
    r["tx_start_threshold"] = static_cast<int>(_s.txStartThreshold);
    r["rx_enabled"] = _s.rxEnabled != 0;
    r["tx_enabled"] = _s.txEnabled != 0;
    r["statistics_enabled"] = _s.statsEnabled != 0;
    r["media"] = Hex(MediaStatus(now), 4);
    r["net_diagnostic"] = Hex(NetDiagnostic(now), 4);
    r["fifo_diagnostic"] = Hex(FifoDiagnostic(), 4);
    r["eeprom_command"] = Hex(static_cast<unsigned>((now < _s.eepromBusyUntil ? 0x8000 : 0) | ((_s.tag & 7) << 8) | _s.eepromCommand), 4);
    r["eeprom_data"] = Hex(_s.eepromData, 4);
    out["registers"] = r;

    StateNode f = StateNode::Object();
    size_t txBytes = 0;
    for (const TxPacket& p : _tx)
        txBytes += p.data.size();
    size_t rxBytes = 0;
    for (const RxPacket& p : _rx)
        rxBytes += p.data.size();
    f["tx_capacity"] = static_cast<int>(TxCapacity());
    f["tx_free"] = static_cast<int>(TxFree());
    f["tx_packets"] = static_cast<uint64_t>(_tx.size());
    f["tx_bytes"] = static_cast<uint64_t>(txBytes);
    f["tx_on_wire"] = _s.txInFlight != 0;
    f["rx_capacity"] = static_cast<int>(RxCapacity());
    f["rx_free"] = static_cast<int>(RxFree());
    f["rx_packets"] = static_cast<uint64_t>(_rx.size());
    f["rx_bytes"] = static_cast<uint64_t>(rxBytes);
    f["rx_status"] = Hex(RxStatus(), 4);
    StateNode stack = StateNode::Array();
    for (int i = 0; i < _s.txStatusCount; ++i)
        stack.push(Hex(_s.txStatus[i], 2));
    f["tx_status_stack"] = stack;
    out["fifo"] = f;

    StateNode e = StateNode::Object();
    const bool primary = PrimaryChecksum(_eeprom) == _eeprom[0x0F];
    const bool secondary = SecondaryChecksum(_eeprom) == _eeprom[0x17];
    e["product_id"] = Hex(_eeprom[0x03], 4);
    e["manufacturer_id"] = Hex(_eeprom[0x07], 4);
    e["address_configuration"] = Hex(_eeprom[0x08], 4);
    e["resource_configuration"] = Hex(_eeprom[0x09], 4);
    e["checksums"] = std::string(primary ? "primary OK" : "primary BAD") + ", " + (secondary ? "secondary OK" : "secondary BAD");
    e["busy"] = now < _s.eepromBusyUntil;
    StateNode words = StateNode::Array();
    for (uint16_t w : _eeprom)
        words.push(Hex(w, 4));
    e["words"] = words;
    out["eeprom"] = e;

    StateNode st = StateNode::Object();
    static const char* const kStatNames[11] = {"carrier_lost", "no_sqe", "multiple_collisions", "one_collision",
                                               "late_collisions", "rx_overruns", "tx_frames_ok", "rx_frames_ok",
                                               "tx_deferrals", "rx_bytes_ok", "tx_bytes_ok"};
    for (int i = 0; i < 11; ++i)
        st[kStatNames[i]] = static_cast<int>(_s.stats[i]);
    out["statistics"] = st;

    StateNode c = StateNode::Object();
    c["tx_frames"] = _counters.txFrames;
    c["rx_frames"] = _counters.rxFrames;
    c["rx_filtered"] = _counters.rxFiltered;
    c["rx_fifo_full_waits"] = _counters.rxFifoFullWaits;
    c["tx_no_link"] = _counters.txNoLink;
    c["tx_underruns"] = _counters.txUnderruns;
    c["tx_overruns"] = _counters.txOverruns;
    c["rx_underruns"] = _counters.rxUnderruns;
    c["id_sequences"] = _counters.idSequences;
    c["id_eeprom_reads"] = _counters.idEepromReads;
    c["activations"] = _counters.activations;
    c["power_on_resets"] = _counters.globalResets;
    c["commands"] = _counters.commands;
    out["counters"] = c;

    StateNode ev = StateNode::Array();
    for (const Event& event : _events)
    {
        StateNode one = StateNode::Object();
        one["t"] = event.time;
        one["text"] = event.text;
        ev.push(one);
    }
    out["events"] = ev;

    out["summary"] = std::string(VariantName(_settings.variant)) + ", ID port " +
                     (_s.idPort ? Hex(_s.idPort, 3) : std::string("none")) + " " + idState +
                     (_s.activated ? ", active at " + Hex(base, 3) : std::string(", not active")) + ", window " +
                     std::to_string(_s.window) + ", status " + Hex(Status(), 4) + ", RX " + (_s.rxEnabled ? "on" : "off") +
                     " filter " + Hex(_s.rxFilter, 1) + ", TX " + (_s.txEnabled ? "on" : "off") + "; TX FIFO " +
                     std::to_string(_tx.size()) + " packet(s), " + std::to_string(TxFree()) + " free; RX FIFO " +
                     std::to_string(_rx.size()) + " packet(s), " + std::to_string(RxFree()) + " free; " + link;
}

// ---------------------------------------------------------------------------
// TTD
// ---------------------------------------------------------------------------

size_t EtherLink3::CardStateBound() const
{
    // The fields, the EEPROM, and both FIFOs at the largest RAM (32 KB) with a header per 4 bytes at worst
    return 1024 + kEepromWords * 2 + 2 * 32768 + 2 * (32768 / 4) * 16;
}

void EtherLink3::SaveCardState(std::vector<uint8_t>& out) const
{
    out.clear();
    Writer w{out};
    w(kStateVersion);
    w(static_cast<uint8_t>(_settings.variant));
    State s = _s;
    EL3_STATE_FIELDS(w, s);
    for (uint16_t word : _eeprom)
        w(word);
    w(static_cast<uint16_t>(_tx.size()));
    for (const TxPacket& p : _tx)
    {
        w(p.length);
        w(static_cast<uint8_t>((p.interruptOnSuccess ? 1 : 0) | (p.noCrc ? 2 : 0) | (p.complete ? 4 : 0) | (p.discard ? 8 : 0)));
        w(static_cast<uint16_t>(p.data.size()));
        out.insert(out.end(), p.data.begin(), p.data.end());
    }
    w(static_cast<uint16_t>(_rx.size()));
    for (const RxPacket& p : _rx)
    {
        w(p.readPos);
        w(p.error);
        w(static_cast<uint16_t>(p.data.size()));
        out.insert(out.end(), p.data.begin(), p.data.end());
    }
}

bool EtherLink3::LoadCardState(const uint8_t* src, size_t size)
{
    Reader r{src, size};
    uint8_t version = 0, variant = 0;
    r(version);
    r(variant);
    if (!r.ok || version != kStateVersion || variant != static_cast<uint8_t>(_settings.variant))
        return false;
    State s;
    EL3_STATE_FIELDS(r, s);
    std::array<uint16_t, kEepromWords> eeprom{};
    for (uint16_t& word : eeprom)
        r(word);
    std::deque<TxPacket> tx;
    uint16_t count = 0;
    r(count);
    for (uint16_t i = 0; i < count && r.ok; ++i)
    {
        TxPacket p;
        uint8_t flags = 0;
        uint16_t length = 0;
        r(p.length);
        r(flags);
        r(length);
        if (!r.Take(length))
            break;
        p.data.assign(src + r.at, src + r.at + length);
        r.at += length;
        p.interruptOnSuccess = (flags & 1) != 0;
        p.noCrc = (flags & 2) != 0;
        p.complete = (flags & 4) != 0;
        p.discard = (flags & 8) != 0;
        tx.push_back(std::move(p));
    }
    std::deque<RxPacket> rx;
    count = 0;
    r(count);
    for (uint16_t i = 0; i < count && r.ok; ++i)
    {
        RxPacket p;
        uint16_t length = 0;
        r(p.readPos);
        r(p.error);
        r(length);
        if (!r.Take(length))
            break;
        p.data.assign(src + r.at, src + r.at + length);
        r.at += length;
        rx.push_back(std::move(p));
    }
    if (!r.ok)
        return false;
    _s = s;
    _eeprom = eeprom;
    _tx = std::move(tx);
    _rx = std::move(rx);
    return true;
}
