#include "portclaimtable.h"

#include "emulator/ports/portdecoder.h"   // PortDevice

#include <algorithm>
#include <bit>
#include <cstring>

namespace slots
{

namespace
{

bool Covers(const ClaimEntry& entry, uint16_t port)
{
    return (port & entry.mask) == entry.match;
}

bool Covers(const PortClaim& claim, uint16_t port)
{
    return (port & claim.mask) == (claim.match & claim.mask);
}

bool HasDir(Dir set, Dir dir)
{
    return (static_cast<uint8_t>(set) & static_cast<uint8_t>(dir)) != 0;
}

/// Whether the entry drives IORQGE on this cycle
bool DrivesIorqge(const ClaimEntry& entry, bool isRead)
{
    return entry.iorqge == Iorqge::Yes || (entry.iorqge == Iorqge::ReadsOnly && isRead);
}

/// The documented port of a claim: PortClaim::port, else the match with every undecoded line high
uint16_t DocumentedPort(const PortClaim& claim)
{
    if (claim.port != 0)
        return claim.port;
    return static_cast<uint16_t>((claim.match & claim.mask) | static_cast<uint16_t>(~claim.mask));
}

/// Sets the bit of every port (H << 8 | L) the mask / match covers, for the low bytes in `lows`
void SetBits(std::array<uint8_t, PortClaimTable::kBitmapBytes>& bits, uint16_t mask, uint16_t match)
{
    const uint8_t maskLow = static_cast<uint8_t>(mask);
    const uint8_t matchLow = static_cast<uint8_t>(match & mask);
    const uint8_t maskHigh = static_cast<uint8_t>(mask >> 8);
    const uint8_t matchHigh = static_cast<uint8_t>((match & mask) >> 8);
    for (unsigned low = 0; low < 256; low++)
    {
        if ((low & maskLow) != matchLow)
            continue;
        for (unsigned high = 0; high < 256; high++)
        {
            if ((high & maskHigh) != matchHigh)
                continue;
            const unsigned port = (high << 8) | low;
            bits[port >> 3] = static_cast<uint8_t>(bits[port >> 3] | (1u << (port & 7)));
        }
    }
}

} // namespace

ClaimEntry MakeClaimEntry(const PortClaim& claim, uint8_t slot, PortDevice* owner, CycleDetection detection)
{
    ClaimEntry entry;
    entry.mask = claim.mask;
    entry.match = static_cast<uint16_t>(claim.match & claim.mask);
    entry.dir = claim.dir;
    entry.iorqge = claim.iorqge;
    entry.gate = claim.gate;
    entry.lockedOnRomFetch = claim.romLock;
    entry.detection = detection;
    entry.slot = slot;
    entry.owner = owner;
    return entry;
}

PortClaimTable::PortClaimTable()
{
    Build();
    _buildCount = 0;
}

/// region <Control path>

void PortClaimTable::Configure(Arbitration arbitration, ReadRule readRule, std::span<const PortClaim> boardPorts)
{
    _arbitration = arbitration;
    _readRule = readRule;
    _boardPorts.assign(boardPorts.begin(), boardPorts.end());
}

void PortClaimTable::Configure(const BusDef& bus)
{
    Configure(bus.arbitration, bus.readRule, bus.boardPorts);
}

void PortClaimTable::AddBuiltIn(const char* id, std::span<const PortClaim> claims)
{
    BuiltInState state;
    state.id = id;
    state.claims = claims;
    _builtIns.push_back(state);
}

void PortClaimTable::Add(const ClaimEntry& entry)
{
    ClaimEntry stored = entry;
    stored.match = static_cast<uint16_t>(entry.match & entry.mask);
    stored.sequence = _nextSequence++;
    _entries.push_back(stored);
}

bool PortClaimTable::Remove(uint16_t mask, uint16_t match, const PortDevice* owner)
{
    const uint16_t normalized = static_cast<uint16_t>(match & mask);
    const size_t before = _entries.size();
    std::erase_if(_entries, [&](const ClaimEntry& e)
    {
        return e.mask == mask && e.match == normalized && e.owner == owner;
    });
    return _entries.size() != before;
}

bool PortClaimTable::RemoveOwner(const PortDevice* owner)
{
    const size_t before = _entries.size();
    std::erase_if(_entries, [&](const ClaimEntry& e) { return e.owner == owner; });
    return _entries.size() != before;
}

bool PortClaimTable::Contains(uint16_t mask, uint16_t match) const
{
    const uint16_t normalized = static_cast<uint16_t>(match & mask);
    return std::any_of(_entries.begin(), _entries.end(), [&](const ClaimEntry& e)
    {
        return e.mask == mask && e.match == normalized;
    });
}

void PortClaimTable::Clear()
{
    _entries.clear();
    _builtIns.clear();
}

void PortClaimTable::Build()
{
    _buildCount++;

    std::vector<ClaimEntry> ordered = _entries;
    std::stable_sort(ordered.begin(), ordered.end(), [](const ClaimEntry& a, const ClaimEntry& b)
    {
        return a.slot != b.slot ? a.slot < b.slot : a.sequence < b.sequence;
    });

    // Buckets: every claim is copied into the bucket of each low byte it covers
    size_t total = 0;
    for (const ClaimEntry& e : ordered)
    {
        const unsigned freeLowBits = 8u - static_cast<unsigned>(std::popcount(static_cast<uint8_t>(e.mask)));
        total += size_t{ 1 } << freeLowBits;
    }
    if (_scanTrap)
        total += 256;
    std::vector<ClaimEntry> bucket;
    bucket.reserve(total);
    ClaimEntry trap;
    trap.owner = _scanTrap;   // mask 0: covers every port (test hook, see SetScanTrapForTests)
    for (unsigned low = 0; low < 256; low++)
    {
        _bucketBegin[low] = static_cast<uint32_t>(bucket.size());
        if (_scanTrap)
            bucket.push_back(trap);
        for (const ClaimEntry& e : ordered)
        {
            if ((low & e.mask & 0xFFu) == (e.match & 0xFFu))
                bucket.push_back(e);
        }
    }
    _bucketBegin[256] = static_cast<uint32_t>(bucket.size());
    _bucket.swap(bucket);
    for (unsigned low = 0; low < 256; low++)
    {
        const bool alone = _bucketBegin[low + 1] - _bucketBegin[low] == 1;
        _whole[low] = alone && (_bucket[_bucketBegin[low]].mask & 0xFF00) == 0 ? &_bucket[_bucketBegin[low]] : nullptr;
    }

    _claimed.fill(0);
    for (const ClaimEntry& e : _entries)
        SetBits(_claimed, e.mask, e.match);

    _boardBits.fill(0);
    if (_arbitration == Arbitration::BoardWins)
    {
        for (const PortClaim& claim : _boardPorts)
            SetBits(_boardBits, claim.mask, claim.match);
    }

    // Shadowing (CardWins only): a card's IORQGE claim covers a documented port of the built-in
    for (BuiltInState& builtIn : _builtIns)
    {
        builtIn.shadowed = false;
        builtIn.shadowedBySlot = 0;
        if (_arbitration != Arbitration::CardWins)
            continue;
        for (const PortClaim& claim : builtIn.claims)
        {
            const uint16_t port = DocumentedPort(claim);
            for (const ClaimEntry& e : ordered)
            {
                if (e.iorqge == Iorqge::No || !Covers(e, port))
                    continue;
                const bool overlap = e.iorqge == Iorqge::ReadsOnly ? HasDir(claim.dir, Dir::In)
                                                                   : HasDir(claim.dir, e.dir);
                if (!overlap)
                    continue;
                if (!builtIn.shadowed || e.slot < builtIn.shadowedBySlot)
                {
                    builtIn.shadowed = true;
                    builtIn.shadowedBySlot = e.slot;
                }
            }
        }
    }
}

/// endregion </Control path>

/// region <Access path>

bool PortClaimTable::GateOpen(Gate gate) const
{
    if (gate == Gate::Always)
        return true;
    const bool dos = _signals && _signals->DosActive();
    return gate == Gate::DosOnly ? dos : !dos;
}

bool PortClaimTable::IsBoardPort(uint16_t port, Dir dir) const
{
    if (_arbitration != Arbitration::BoardWins || !((_boardBits[port >> 3] >> (port & 7)) & 1u))
        return false;
    for (const PortClaim& claim : _boardPorts)
    {
        if (Covers(claim, port) && HasDir(claim.dir, dir) && GateOpen(claim.gate))
            return true;
    }
    return false;
}

bool PortClaimTable::Sees(const ClaimEntry& entry, uint16_t port, bool isRead, bool boardPort) const
{
    if (!Covers(entry, port) || !HasDir(entry.dir, isRead ? Dir::In : Dir::Out))
        return false;
    if (!GateOpen(entry.gate))
        return false;
    // Step 1: on a BoardWins bus the board masks /IORQ to the slots for its own ports; only a card that detects a
    // cycle by RD / WR alone still sees it
    if (boardPort && entry.detection == CycleDetection::Iorq)
        return false;
    // ROM-fetch lock: read only when an entry needs it
    if (entry.lockedOnRomFetch && _signals && _signals->LastM1Address() < 0x4000)
        return false;
    return true;
}

BoardCycle PortClaimTable::WriteCards(uint16_t port, uint8_t value) const
{
    const bool boardPort = IsBoardPort(port, Dir::Out);
    BoardCycle board = BoardCycle::Full;
    const PortDevice* lastOwner = nullptr;
    const uint8_t low = static_cast<uint8_t>(port);
    for (uint32_t i = _bucketBegin[low], end = _bucketBegin[low + 1u]; i < end; i++)
    {
        const ClaimEntry& e = _bucket[i];
        if (!Sees(e, port, false, boardPort))
            continue;
        // One device gets one write, however many of its claims cover the port
        if (e.owner != lastOwner && e.owner)
            e.owner->portDeviceOutMethod(port, value);
        lastOwner = e.owner;
        // Step 2: IORQGE hides the cycle from later slots; step 3: and from the board by the arbitration
        if (DrivesIorqge(e, false))
        {
            if (_arbitration == Arbitration::CardWins)
                board = BoardCycle::Hidden;
            else if (_arbitration == Arbitration::UlaOnly)
                board = BoardCycle::UlaSilenced;
            break;
        }
    }
    return board;
}

PortClaimTable::CardReads PortClaimTable::ReadCards(uint16_t port) const
{
    CardReads reads;
    const bool boardPort = IsBoardPort(port, Dir::In);
    const PortDevice* lastOwner = nullptr;
    const uint8_t low = static_cast<uint8_t>(port);
    for (uint32_t i = _bucketBegin[low], end = _bucketBegin[low + 1u]; i < end; i++)
    {
        const ClaimEntry& e = _bucket[i];
        if (!Sees(e, port, true, boardPort))
            continue;
        if (e.owner != lastOwner && e.owner)
        {
            bool drives = true;
            const uint8_t v = e.owner->portDeviceReadCycle(port, drives);
            if (drives)
            {
                if (reads.count == 0)
                    reads.firstValue = v;
                reads.value = static_cast<uint8_t>(reads.value & v);
                reads.count++;
            }
        }
        lastOwner = e.owner;
        if (DrivesIorqge(e, true))
        {
            if (_arbitration == Arbitration::CardWins)
                reads.board = BoardCycle::Hidden;
            else if (_arbitration == Arbitration::UlaOnly)
                reads.board = BoardCycle::UlaSilenced;
            break;
        }
    }
    return reads;
}

ReadResult PortClaimTable::Combine(const CardReads& cards, bool boardDrives, uint8_t boardValue) const
{
    ReadResult result;
    result.drivers = static_cast<uint8_t>(cards.count + (boardDrives ? 1 : 0));
    result.nobody = result.drivers == 0;
    result.busFight = result.drivers > 1;
    if (result.nobody)
        return result;

    if (cards.count == 0)
    {
        result.value = boardValue;
        return result;
    }

    switch (_readRule)
    {
        case ReadRule::CardOverUla:
            // The board sits behind series resistors: the cards win; several cards still fight among themselves
            result.value = cards.value;
            break;
        case ReadRule::SlotOrder:
            // The board first, then the slots in order (modeling choice, reported as a bus fight)
            result.value = boardDrives ? boardValue : cards.firstValue;
            break;
        case ReadRule::WiredAnd:
        case ReadRule::Count:
        default:
            result.value = static_cast<uint8_t>(cards.value & (boardDrives ? boardValue : 0xFF));
            break;
    }
    return result;
}

/// endregion </Access path>

/// region <Reports>

const PortClaimTable::BuiltInState* PortClaimTable::FindBuiltIn(const char* id) const
{
    for (const BuiltInState& builtIn : _builtIns)
    {
        if (std::strcmp(builtIn.id, id) == 0)
            return &builtIn;
    }
    return nullptr;
}

void PortClaimTable::SetBuiltInRemoved(const char* id)
{
    for (BuiltInState& builtIn : _builtIns)
    {
        if (std::strcmp(builtIn.id, id) == 0)
            builtIn.removed = true;
    }
}

bool PortClaimTable::IsRemovedBuiltInRead(uint16_t port) const
{
    for (const BuiltInState& builtIn : _builtIns)
    {
        if (!builtIn.removed)
            continue;
        for (const PortClaim& claim : builtIn.claims)
        {
            if (Covers(claim, port) && HasDir(claim.dir, Dir::In))
                return true;
        }
    }
    return false;
}

/// endregion </Reports>

} // namespace slots
