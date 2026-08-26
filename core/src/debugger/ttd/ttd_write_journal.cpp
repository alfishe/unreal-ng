/// @file ttd_write_journal.cpp
/// @brief TTD write journal — implementation.
///
/// Per parent TDD §9.3. See ttd_write_journal.h for the threading model and
/// design rationale.

#include "ttd_write_journal.h"

#include <algorithm>

#include "ttd_compression.h"

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

namespace
{

// ---------------------------------------------------------------------------
// Block-compressed journal section
// ---------------------------------------------------------------------------
//
// The journal used to be written verbatim: 12 bytes per record, uncompressed.
// On a real recording that is by far the largest thing in a .ttd - measured at
// 8.36 MB of a 9.37 MB file (89%) for a demo, against 648 KB for the whole
// compressed page store. At 2280 writes per frame it works out to 1.37 MB/s,
// i.e. ~4.9 GB per hour of recording.
//
// Records are stored in blocks, and each block is transposed into columns
// before compression. That matters more than the compression itself: the
// columns are individually near-constant (addresses cluster, values repeat,
// physPage rarely changes, globalT is monotonic and becomes small deltas),
// while an interleaved record stream mixes five unrelated distributions and
// gives the compressor nothing to work with. Measured on real journals:
// verbatim + zstd manages 3.4x, columnar + zstd reaches 13.5x.
//
// Random access into a block is deliberately NOT supported. Reverse search is
// served by the per-frame coverage index, which answers "which frames could
// possibly match" without touching the journal at all; the journal is only
// read to reconstruct the ring. The block directory still carries each block's
// globalT range so a time-bounded reload can skip blocks whole.

constexpr uint32_t kRecordsPerBlock = 2048;

/// Layout marker for the section. Bumping it invalidates old files, which is
/// acceptable while the format is pre-release.
constexpr uint32_t kJournalSectionMagic = 0x4A574C42;  // 'BLWJ'

struct BlockDirEntry
{
    uint64_t firstGlobalT;
    uint64_t lastGlobalT;
    uint32_t recordCount;
    uint32_t compressedSize;
    uint32_t rawSize;       ///< Exact decompressed size; codec::Decompress demands it
    uint32_t reserved;      ///< Keeps the entry 8-byte aligned
};

void AppendVarint(std::vector<uint8_t>& out, uint64_t value)
{
    while (value >= 0x80)
    {
        out.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<uint8_t>(value));
}

uint64_t ReadVarint(const uint8_t* data, size_t size, size_t& pos, bool& ok)
{
    uint64_t result = 0;
    uint32_t shift = 0;
    while (true)
    {
        if (pos >= size || shift > 63) { ok = false; return 0; }
        const uint8_t b = data[pos++];
        result |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0)
            break;
        shift += 7;
    }
    return result;
}

/// Transpose a run of records into columns. globalT is monotonic within the
/// journal, so it is stored as deltas from the block's first value.
std::vector<uint8_t> EncodeBlock(const TTDWriteRecord* recs, uint32_t count)
{
    std::vector<uint8_t> gt, addr, m1pc, value, page, io;
    gt.reserve(count * 2);
    addr.reserve(count * 2);
    m1pc.reserve(count * 2);
    value.reserve(count);
    page.reserve(count);
    io.reserve((count + 7) / 8);

    uint64_t prevT = count ? static_cast<uint64_t>(recs[0].globalT) : 0;
    uint8_t ioBits = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const TTDWriteRecord& r = recs[i];
        const uint64_t t = static_cast<uint64_t>(r.globalT);
        // Monotonic in practice; guard against a stray out-of-order record
        // rather than encoding a huge wrapped delta.
        AppendVarint(gt, t >= prevT ? (t - prevT) : 0);
        prevT = t;

        const uint16_t a = static_cast<uint16_t>(r.addr);
        addr.push_back(static_cast<uint8_t>(a & 0xFF));
        addr.push_back(static_cast<uint8_t>(a >> 8));
        m1pc.push_back(static_cast<uint8_t>(r.m1pc & 0xFF));
        m1pc.push_back(static_cast<uint8_t>(r.m1pc >> 8));
        value.push_back(r.value);
        page.push_back(r.physPage);

        ioBits |= static_cast<uint8_t>((r.isIo ? 1u : 0u) << (i & 7));
        if ((i & 7) == 7) { io.push_back(ioBits); ioBits = 0; }
    }
    if (count & 7)
        io.push_back(ioBits);

    // Column sizes first so the decoder can split the blob without scanning.
    std::vector<uint8_t> raw;
    raw.reserve(gt.size() + addr.size() + m1pc.size() + value.size() + page.size() + io.size() + 32);
    AppendVarint(raw, gt.size());
    AppendVarint(raw, count ? static_cast<uint64_t>(recs[0].globalT) : 0);
    raw.insert(raw.end(), gt.begin(), gt.end());
    raw.insert(raw.end(), addr.begin(), addr.end());
    raw.insert(raw.end(), m1pc.begin(), m1pc.end());
    raw.insert(raw.end(), value.begin(), value.end());
    raw.insert(raw.end(), page.begin(), page.end());
    raw.insert(raw.end(), io.begin(), io.end());
    return raw;
}

/// Inverse of EncodeBlock. Returns false on any inconsistency rather than
/// producing partially-decoded records.
bool DecodeBlock(const std::vector<uint8_t>& raw, uint32_t count,
                 std::vector<TTDWriteRecord>& out)
{
    out.clear();
    if (count == 0)
        return true;

    bool ok = true;
    size_t pos = 0;
    const uint64_t gtBytes = ReadVarint(raw.data(), raw.size(), pos, ok);
    const uint64_t baseT   = ReadVarint(raw.data(), raw.size(), pos, ok);
    if (!ok)
        return false;

    const size_t gtStart = pos;
    const size_t addrStart  = gtStart + static_cast<size_t>(gtBytes);
    const size_t m1pcStart  = addrStart + size_t(count) * 2;
    const size_t valueStart = m1pcStart + size_t(count) * 2;
    const size_t pageStart  = valueStart + count;
    const size_t ioStart    = pageStart + count;
    const size_t ioBytes    = (count + 7) / 8;
    if (ioStart + ioBytes > raw.size())
        return false;

    out.resize(count);
    size_t gtPos = gtStart;
    uint64_t t = baseT;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint64_t delta = ReadVarint(raw.data(), addrStart, gtPos, ok);
        if (!ok)
            return false;
        if (i != 0)
            t += delta;

        TTDWriteRecord& r = out[i];
        r.globalT = t & ((uint64_t{1} << 40) - 1);
        r.addr = static_cast<uint16_t>(raw[addrStart + i * 2] |
                                       (raw[addrStart + i * 2 + 1] << 8));
        r.isIo = (raw[ioStart + (i >> 3)] >> (i & 7)) & 1u;
        r.pad = 0;
        r.m1pc = static_cast<uint16_t>(raw[m1pcStart + i * 2] |
                                       (raw[m1pcStart + i * 2 + 1] << 8));
        r.value = raw[valueStart + i];
        r.physPage = raw[pageStart + i];
    }
    return true;
}

}  // namespace

bool TTDWriteJournal::Serialize(std::ostream& out) const
{
    const uint64_t live = Size();
    out.write(reinterpret_cast<const char*>(&live), sizeof(live));
    if (!out)
        return false;

    const uint32_t magic = kJournalSectionMagic;
    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));

    const uint32_t blockCount =
        static_cast<uint32_t>((live + kRecordsPerBlock - 1) / kRecordsPerBlock);
    out.write(reinterpret_cast<const char*>(&blockCount), sizeof(blockCount));
    if (!out)
        return false;
    if (live == 0)
        return true;

    // Compress first so the directory can carry each block's size, letting a
    // reader seek straight to the block it wants.
    std::vector<BlockDirEntry> dir;
    std::vector<std::vector<uint8_t>> payloads;
    dir.reserve(blockCount);
    payloads.reserve(blockCount);

    std::vector<TTDWriteRecord> scratch;
    scratch.reserve(kRecordsPerBlock);

    uint64_t seq = _seqTail;
    while (seq < _seqHead)
    {
        const uint32_t n = static_cast<uint32_t>(
            std::min<uint64_t>(kRecordsPerBlock, _seqHead - seq));

        scratch.clear();
        for (uint32_t i = 0; i < n; ++i)
            scratch.push_back(_ring[SeqToIdx(seq + i)]);

        const std::vector<uint8_t> rawBlock = EncodeBlock(scratch.data(), n);
        std::vector<uint8_t> comp = codec::Compress(rawBlock.data(), rawBlock.size());

        BlockDirEntry e{};
        e.firstGlobalT   = static_cast<uint64_t>(scratch.front().globalT);
        e.lastGlobalT    = static_cast<uint64_t>(scratch.back().globalT);
        e.recordCount    = n;
        e.compressedSize = static_cast<uint32_t>(comp.size());
        e.rawSize        = static_cast<uint32_t>(rawBlock.size());
        e.reserved       = 0;
        dir.push_back(e);
        payloads.push_back(std::move(comp));

        seq += n;
    }

    for (const BlockDirEntry& e : dir)
    {
        out.write(reinterpret_cast<const char*>(&e), sizeof(e));
        if (!out)
            return false;
    }
    for (const std::vector<uint8_t>& p : payloads)
    {
        out.write(reinterpret_cast<const char*>(p.data()),
                  static_cast<std::streamsize>(p.size()));
        if (!out)
            return false;
    }
    return true;
}

bool TTDWriteJournal::Deserialize(std::istream& in, uint64_t count)
{
    Clear();

    if (count > _ring.size())
        return false;

    uint32_t magic = 0;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (!in || magic != kJournalSectionMagic)
        return false;

    uint32_t blockCount = 0;
    in.read(reinterpret_cast<char*>(&blockCount), sizeof(blockCount));
    if (!in)
        return false;
    if (count == 0)
        return true;

    std::vector<BlockDirEntry> dir(blockCount);
    for (uint32_t i = 0; i < blockCount; ++i)
    {
        in.read(reinterpret_cast<char*>(&dir[i]), sizeof(BlockDirEntry));
        if (!in)
            return false;
    }

    uint64_t restored = 0;
    std::vector<uint8_t> comp;
    std::vector<uint8_t> rawBlock;
    std::vector<TTDWriteRecord> recs;

    for (uint32_t b = 0; b < blockCount; ++b)
    {
        comp.assign(dir[b].compressedSize, 0);
        in.read(reinterpret_cast<char*>(comp.data()),
                static_cast<std::streamsize>(comp.size()));
        if (!in)
            return false;

        rawBlock.assign(dir[b].rawSize, 0);
        if (!codec::Decompress(comp, dir[b].rawSize, rawBlock.data()))
            return false;

        if (!DecodeBlock(rawBlock, dir[b].recordCount, recs))
            return false;

        for (const TTDWriteRecord& r : recs)
            Append(r);
        restored += dir[b].recordCount;
    }

    return restored == count;
}

} // namespace ttd
