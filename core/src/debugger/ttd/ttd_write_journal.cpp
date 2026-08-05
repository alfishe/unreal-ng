/// @file ttd_write_journal.cpp
/// @brief TTD write journal — implementation.
///
/// Per parent TDD §9.3. See ttd_write_journal.h for the threading model and
/// design rationale.

#include "ttd_write_journal.h"

#include <algorithm>
#include <cstring>

namespace ttd {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

/// Round `ringBytes` up to a power-of-two record count, then convert to
/// bytes. We always allocate at least 64 records (small tests + the
/// "empty session" degenerate case).
size_t RoundToPowerOfTwoRecords(size_t ringBytes)
{
    const size_t rawRecs = ringBytes / sizeof(TTDWriteRecord);
    size_t cap = 64;  // minimum
    while (cap < rawRecs && cap < (SIZE_MAX / 2))
        cap <<= 1;
    return cap;
}

} // namespace

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

TTDWriteJournal::TTDWriteJournal(size_t ringBytes, bool asyncAlloc)
{
    const size_t cap = RoundToPowerOfTwoRecords(ringBytes);
    _mask = cap - 1;
    _seqHead = 0;
    _seqTail = 0;

    if (asyncAlloc)
    {
        // Allocate on background thread to avoid blocking emulator
        _allocFuture = std::async(std::launch::async, [this, cap]() {
            _ring.resize(cap);
            _ready.store(true, std::memory_order_release);
        });
    }
    else
    {
        // Synchronous allocation (for tests and small buffers)
        _ring.resize(cap);
        _ready.store(true, std::memory_order_release);
    }
}

TTDWriteJournal::~TTDWriteJournal()
{
    // Ensure async allocation completes before destruction
    if (_allocFuture.valid())
        _allocFuture.wait();
}

void TTDWriteJournal::WaitReady()
{
    if (_allocFuture.valid())
        _allocFuture.wait();
}

// ---------------------------------------------------------------------------
// Capture path (emulator thread)
// ---------------------------------------------------------------------------

void TTDWriteJournal::Append(const TTDWriteRecord& rec)
{
    const size_t idx = SeqToIdx(_seqHead);
    _ring[idx] = rec;
    ++_seqHead;
    // If we've overflowed the ring, advance the tail to keep live count ==
    // capacity. (Capacity is fixed; only _seqTail moves to drop the oldest.)
    if (_seqHead - _seqTail > _ring.size())
        ++_seqTail;
}

// ---------------------------------------------------------------------------
// Query path (control thread; emulator paused)
// ---------------------------------------------------------------------------

std::optional<TTDWriteRecord> TTDWriteJournal::FindLast(
    uint64_t beforeT,
    const std::function<bool(const TTDWriteRecord&)>& pred) const
{
    if (IsEmpty())
        return std::nullopt;

    // Scan backward from the most recent append. _seqHead is exclusive;
    // the latest live record is at seq == _seqHead - 1.
    for (uint64_t seq = _seqHead; seq > _seqTail; --seq)
    {
        const TTDWriteRecord& rec = _ring[SeqToIdx(seq - 1)];
        if (rec.globalT > beforeT)
            continue;
        if (pred(rec))
            return rec;
    }
    return std::nullopt;
}

uint64_t TTDWriteJournal::OldestGlobalT() const
{
    if (IsEmpty())
        return 0;
    return _ring[SeqToIdx(_seqTail)].globalT;
}

uint64_t TTDWriteJournal::NewestGlobalT() const
{
    if (IsEmpty())
        return 0;
    return _ring[SeqToIdx(_seqHead - 1)].globalT;
}

// ---------------------------------------------------------------------------
// Lifecycle (control thread; emulator paused)
// ---------------------------------------------------------------------------

void TTDWriteJournal::DropAfter(uint64_t globalT)
{
    // Walk backward from the head, dropping every record with globalT >
    // globalT. Once we find one that's <= globalT, everything older is also
    // <= globalT (monotonic appends), so we can stop.
    while (_seqHead > _seqTail)
    {
        const size_t idx = SeqToIdx(_seqHead - 1);
        if (_ring[idx].globalT <= globalT)
            break;
        // Zero the slot for hygiene (helps debug memory dumps).
        _ring[idx] = TTDWriteRecord{};
        --_seqHead;
    }
}

void TTDWriteJournal::Clear()
{
    // Zero the live slots (don't shrink the vector — capacity is fixed
    // for the journal's lifetime).
    for (uint64_t seq = _seqTail; seq < _seqHead; ++seq)
        _ring[SeqToIdx(seq)] = TTDWriteRecord{};
    _seqHead = 0;
    _seqTail = 0;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

bool TTDWriteJournal::Serialize(std::ostream& out) const
{
    // Write the live count as u8 (little-endian).
    const uint64_t live = Size();
    out.write(reinterpret_cast<const char*>(&live), sizeof(live));
    if (!out)
        return false;

    // Walk live records from oldest to newest and dump each verbatim.
    for (uint64_t seq = _seqTail; seq < _seqHead; ++seq)
    {
        const TTDWriteRecord& rec = _ring[SeqToIdx(seq)];
        out.write(reinterpret_cast<const char*>(&rec), sizeof(rec));
        if (!out)
            return false;
    }
    return true;
}

bool TTDWriteJournal::Deserialize(std::istream& in, uint64_t count)
{
    // Reset and load.
    Clear();

    if (count > _ring.size())
    {
        // Caller asked for more records than our capacity — read nothing,
        // fail. The header should have validated this; this is defensive.
        return false;
    }

    for (uint64_t i = 0; i < count; ++i)
    {
        TTDWriteRecord rec{};
        in.read(reinterpret_cast<char*>(&rec), sizeof(rec));
        if (!in)
            return false;
        Append(rec);
    }
    return true;
}

} // namespace ttd
