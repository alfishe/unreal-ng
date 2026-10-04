#pragma once

/// @file portclaimtable.h
/// @brief The port claim table of the ZX-bus slots (docs/inprogress/2026-10-03-zx-bus-slots/architecture.md §4.2-4.3):
/// which fitted devices claim which 16-bit ports, and how one I/O cycle on a claimed port is resolved between the
/// cards in the slots and the board's own decoder (IORQGE, bus arbitration, cycle detection, ROM-fetch lock, read
/// rule).
///
/// The table is built on the control path (machine start, a device registering or leaving) and only read on the
/// access path: a port no device claims costs one bit test (IsClaimed), a claimed port one scan of the bucket of its
/// low byte. Nothing on the access path allocates.
///
/// PortDecoder keeps three role instances (SL-3): the full-decode observers (raw port, before the board decode), the
/// self-decoding devices (raw port, after it) and the exact peripheral port map (decoded port), served through the
/// lookups FirstMatch / FindByMask / ForEachMatch; the full cycle resolution (Write / Read) is used by machines from
/// SL-4 on, when the cards declare their claims.

// Qt defines `slots` and `signals` as macros; this header reaches Qt translation units through portdecoder.h
#pragma push_macro("slots")
#pragma push_macro("signals")
#undef slots
#undef signals

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "slottypes.h"
#include "slotvocabulary.h"

class PortDevice;

namespace slots
{

/// One claim of a fitted device as the table stores it (architecture.md §4.2)
struct ClaimEntry
{
    uint16_t mask = 0;                  ///< claims port p when (p & mask) == match
    uint16_t match = 0;
    Dir dir = Dir::InOut;
    Iorqge iorqge = Iorqge::No;         ///< the device pulls IORQGE on this claim
    Gate gate = Gate::Always;           ///< DOS state the claim is live in
    bool lockedOnRomFetch = false;      ///< ignored while the last opcode fetch came from #0000-#3FFF
    CycleDetection detection = CycleDetection::Iorq;
    uint8_t slot = 0;                   ///< slot order: lower slots see a cycle first; entries of one device share it
    PortDevice* owner = nullptr;

    /// The table's internal registration order (ties within a slot); set by Add
    uint32_t sequence = 0;
};

/// The table entry for a reference-data claim of a device in slot `slot`
ClaimEntry MakeClaimEntry(const PortClaim& claim, uint8_t slot, PortDevice* owner,
                          CycleDetection detection = CycleDetection::Iorq);

/// What the board's own decoder gets of one cycle (architecture.md §4.3 step 3)
enum class BoardCycle : uint8_t
{
    Full,           ///< the board decodes the cycle as if no card were fitted
    UlaSilenced,    ///< UlaOnly bus: IORQGE silenced the ULA's #FE decode, the rest of the board decodes
    Hidden,         ///< CardWins bus: a visible card drove IORQGE, the board gets no cycle
};

/// The outcome of one read cycle (architecture.md §4.3 step 5)
struct ReadResult
{
    uint8_t value = 0xFF;   ///< the byte on the data bus (0xFF when nobody drove it)
    uint8_t drivers = 0;    ///< devices that drove the bus (the board counts as one)
    bool nobody = true;     ///< nobody drove the bus: the machine applies its floating-bus rule
    bool busFight = false;  ///< more than one driver: the value is the bus's read rule (a modeling choice, reported)
};

/// Bus signals the table consults only when an entry needs them (ROM-fetch lock, DOS-gated claims)
class IClaimSignals
{
public:
    virtual ~IClaimSignals() = default;
    /// The address of the last opcode fetch (the IN / OUT instruction's own M1)
    virtual uint16_t LastM1Address() const = 0;
    /// DOS mode: the TR-DOS ROM is paged / the Beta-128 shadow ports are open
    virtual bool DosActive() const = 0;
};

class PortClaimTable
{
public:
    /// Ports are 16 bits, one bit each
    static constexpr size_t kBitmapBytes = 65536 / 8;

    PortClaimTable();

    /// region <Control path: configuration and build>

    /// The bus the claims sit on: its arbitration, read rule and (BoardWins) the board ports it hides from the slots
    void Configure(Arbitration arbitration, ReadRule readRule, std::span<const PortClaim> boardPorts = {});
    void Configure(const BusDef& bus);

    /// A built-in device of the machine, for the shadow report (architecture.md §4.3 "Shadowing"). `id` must outlive
    /// the table (reference data strings)
    void AddBuiltIn(const char* id, std::span<const PortClaim> claims);

    /// Where the ROM-fetch lock and DOS-gated claims read their signals; nullptr: never locked, never DOS
    void BindSignals(const IClaimSignals* signals) { _signals = signals; }

    /// Adds a claim (match is normalized to the mask). Takes effect at the next Build()
    void Add(const ClaimEntry& entry);
    /// Removes every claim (mask, match) of `owner`; true when one was removed. Takes effect at the next Build()
    bool Remove(uint16_t mask, uint16_t match, const PortDevice* owner);
    /// Removes every claim of `owner`. Takes effect at the next Build()
    bool RemoveOwner(const PortDevice* owner);
    /// Whether a claim with exactly this mask and match is registered (any owner)
    bool Contains(uint16_t mask, uint16_t match) const;
    /// Drops every claim and built-in; the bus configuration stays
    void Clear();

    /// Computes the bitmaps, the buckets and the shadow flags from the claims. Control path only: allocates
    void Build();

    /// endregion </Control path: configuration and build>

    /// region <Access path>

    /// Whether any claim covers the port: the whole cost of an access to a port no device claims
    bool IsClaimed(uint16_t port) const
    {
        return (_claimed[port >> 3] >> (port & 7)) & 1u;
    }

    /// Legacy observer lookup (full-decode observers of PortDecoder): the first claim in slot order covering the
    /// port, whatever its direction, gate or IORQGE; nullptr when none. Inline: the claimed-port path of every IN /
    /// OUT on a machine with an observer card
    const ClaimEntry* FirstMatch(uint16_t port) const
    {
        const uint8_t low = static_cast<uint8_t>(port);
        if (const ClaimEntry* only = _whole[low])
            return only;
        const ClaimEntry* entry = _bucket.data() + _bucketBegin[low];
        const ClaimEntry* const end = _bucket.data() + _bucketBegin[low + 1u];
        for (; entry != end; ++entry)
        {
            if ((port & entry->mask) == entry->match)
                return entry;
        }
        return nullptr;
    }

    /// The first claim in slot order with exactly this mask that covers the port; nullptr when none
    const ClaimEntry* FindByMask(uint16_t port, uint16_t mask) const
    {
        const uint8_t low = static_cast<uint8_t>(port);
        if (const ClaimEntry* only = _whole[low])
            return only->mask == mask ? only : nullptr;
        const ClaimEntry* entry = _bucket.data() + _bucketBegin[low];
        const ClaimEntry* const end = _bucket.data() + _bucketBegin[low + 1u];
        for (; entry != end; ++entry)
        {
            if (entry->mask == mask && (port & entry->mask) == entry->match)
                return entry;
        }
        return nullptr;
    }

    /// Every claim covering the port, in slot order (registration order within a slot): `bool f(const ClaimEntry&)`
    /// returns true to stop. Returns whether f stopped. Claimed ports only (call IsClaimed first)
    template <typename F>
    bool ForEachMatch(uint16_t port, F&& f) const
    {
        const uint8_t low = static_cast<uint8_t>(port);
        const ClaimEntry* entry = _bucket.data() + _bucketBegin[low];
        const ClaimEntry* const end = _bucket.data() + _bucketBegin[low + 1u];
        for (; entry != end; ++entry)
        {
            if ((port & entry->mask) == entry->match && f(*entry))
                return true;
        }
        return false;
    }

    /// A write cycle: every card that sees it gets the value; `board(BoardCycle)` is called unless the board is
    /// hidden (architecture.md §4.3 steps 1-4)
    template <typename BoardOut>
    void Write(uint16_t port, uint8_t value, BoardOut&& board) const
    {
        BoardCycle cycle = BoardCycle::Full;
        if (IsClaimed(port)) [[unlikely]]
            cycle = WriteCards(port, value);
        if (cycle != BoardCycle::Hidden)
            board(cycle);
    }

    /// A read cycle: `bool board(BoardCycle, uint8_t& value)` returns true when the board drove the bus (called unless
    /// the board is hidden). The drivers combine by the bus's read rule (architecture.md §4.3 step 5)
    template <typename BoardIn>
    ReadResult Read(uint16_t port, BoardIn&& board) const
    {
        CardReads cards;
        if (IsClaimed(port)) [[unlikely]]
            cards = ReadCards(port);
        uint8_t boardValue = 0xFF;
        const bool boardDrives = cards.board != BoardCycle::Hidden && board(cards.board, boardValue);
        return Combine(cards, boardDrives, boardValue);
    }

    /// endregion </Access path>

    /// region <Reports and instrumentation>

    struct BuiltInState
    {
        const char* id = "";
        std::span<const PortClaim> claims{};
        bool shadowed = false;      ///< a card's IORQGE claim covers one of its documented ports (CardWins buses)
        uint8_t shadowedBySlot = 0; ///< the first such card's slot
    };

    /// The built-in with this id; nullptr when none
    const BuiltInState* FindBuiltIn(const char* id) const;
    const std::vector<BuiltInState>& BuiltIns() const { return _builtIns; }

    /// The claims as added, in registration order
    const std::vector<ClaimEntry>& Entries() const { return _entries; }

    Arbitration GetArbitration() const { return _arbitration; }
    ReadRule GetReadRule() const { return _readRule; }

    /// Whether the port is one the board hides from Iorq cards (BoardWins), for this direction and DOS state
    bool IsBoardPort(uint16_t port, Dir dir) const;

    /// How many times Build() ran
    uint32_t BuildCount() const { return _buildCount; }
    /// Test hook (control path; costs the access path nothing): from the next Build() on, every bucket starts with a
    /// claim of `trap` covering every port, while the claimed-port bitmap stays as the real claims set it. Any bucket
    /// scan then reaches the trap device, so a test sees whether an access scanned the table at all ("an unclaimed
    /// port never scans"). nullptr removes it
    void SetScanTrapForTests(PortDevice* trap) { _scanTrap = trap; }
    /// The bucket storage (its address must not change between builds: no allocation on the access path)
    const ClaimEntry* BucketStorage() const { return _bucket.data(); }

    /// endregion </Reports and instrumentation>

private:
    struct CardReads
    {
        BoardCycle board = BoardCycle::Full;
        uint8_t value = 0xFF;       ///< the cards' combined value
        uint8_t firstValue = 0xFF;  ///< the first card's (slot order) value
        uint8_t count = 0;          ///< cards that drove the bus
    };

    BoardCycle WriteCards(uint16_t port, uint8_t value) const;
    CardReads ReadCards(uint16_t port) const;
    ReadResult Combine(const CardReads& cards, bool boardDrives, uint8_t boardValue) const;

    /// Whether the entry sees this cycle by its own rules (direction, gate, ROM-fetch lock, board-port masking)
    bool Sees(const ClaimEntry& entry, uint16_t port, bool isRead, bool boardPort) const;
    bool GateOpen(Gate gate) const;

    std::array<uint8_t, kBitmapBytes> _claimed{};       ///< one bit per port: some claim covers it
    std::array<uint8_t, kBitmapBytes> _boardBits{};     ///< one bit per port: some board port covers it (BoardWins)
    std::array<uint32_t, 257> _bucketBegin{};           ///< claims of low byte L: _bucket[_bucketBegin[L] .. [L+1])
    std::vector<ClaimEntry> _bucket;                    ///< per-low-byte copies, sorted by (slot, sequence)
    /// The bucket's only claim when it is alone and covers every high byte (a low-byte decode, the common case):
    /// a claimed port of that low byte matches it without a compare. nullptr otherwise
    std::array<const ClaimEntry*, 256> _whole{};

    std::vector<ClaimEntry> _entries;
    std::vector<PortClaim> _boardPorts;
    std::vector<BuiltInState> _builtIns;

    Arbitration _arbitration = Arbitration::None;
    ReadRule _readRule = ReadRule::WiredAnd;
    const IClaimSignals* _signals = nullptr;
    uint32_t _nextSequence = 0;
    uint32_t _buildCount = 0;
    PortDevice* _scanTrap = nullptr;
};

} // namespace slots

#pragma pop_macro("signals")
#pragma pop_macro("slots")
