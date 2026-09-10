/// @file ttd_codec_page_store.cpp
/// @brief TTDCodecPageStore implementation — see header for design.
///
/// Per Phase 5 PoC: every dirty page goes through XOR-then-zstd-1 encoding
/// (Strategy A — measured 10 us encode, 678 B mean compressed per dirty
/// page on Binary Love I workload, 11.4x compression end-to-end).

#include "ttd_codec_page_store.h"

#include <cassert>
#include <cstring>
#include <algorithm>

namespace ttd
{

// ---------------------------------------------------------------------------
// Slot allocation
// ---------------------------------------------------------------------------

uint32_t TTDCodecPageStore::AllocateSlot()
{
    if (!_freeList.empty())
    {
        uint32_t idx = _freeList.back();
        _freeList.pop_back();
        return idx;
    }
    uint32_t idx = static_cast<uint32_t>(_slots.size());
    _slots.emplace_back();
    return idx;
}

// ---------------------------------------------------------------------------
// Intern family
// ---------------------------------------------------------------------------

uint32_t TTDCodecPageStore::InternFull(const uint8_t* pageData)
{
    assert(pageData != nullptr);

    // Early-out: all-zero page is stored as Zero encoding with empty payload.
    // This is rare in normal emulator state (most pages have at least a
    // stack frame or some screen content) but common immediately after
    // reset, so it's worth the fast path.
    if (codec::IsAllZero(pageData, kPageSize))
    {
        uint32_t idx = AllocateSlot();
        Slot& s = _slots[idx];
        s.encoding = Encoding::Zero;
        s.refcount = 1;
        s.prevSlot = 0;
        s.crc32c = codec::Crc32C(pageData, kPageSize);  // CRC of zero page
        s.payload.clear();
        _usedSlots++;
        return idx;
    }

    auto compressed = codec::Compress(pageData, kPageSize);
    assert(!compressed.empty());

    uint32_t idx = AllocateSlot();
    Slot& s = _slots[idx];
    s.encoding = Encoding::Full;
    s.refcount = 1;
    s.prevSlot = 0;
    s.crc32c = codec::Crc32C(pageData, kPageSize);
    s.payload = std::move(compressed);
    _usedSlots++;
    return idx;
}

uint32_t TTDCodecPageStore::InternXor(uint32_t prevSlot, const uint8_t* pageData)
{
    assert(prevSlot < _slots.size());
    assert(_slots[prevSlot].refcount > 0);
    assert(pageData != nullptr);

    // Decompress the prev slot into scratch (recursively, if it's also
    // an xor-prev). The recursive path is bounded by GetDeltaDepth.
    _prevScratch.assign(kPageSize, 0);
    if (!GetPageNoVerify(prevSlot, _prevScratch.data()))
    {
        // Prev slot is corrupt — fall back to full snapshot.
        return InternFull(pageData);
    }

    // Compute XOR buffer: cur ^ prev
    uint8_t xorBuf[kPageSize];
    codec::XorBuffers(pageData, _prevScratch.data(), xorBuf, kPageSize);

    // If XOR is all zeros, page hasn't actually changed. Shouldn't happen
    // (caller should have detected this via dirty tracker) but be defensive.
    if (codec::IsAllZero(xorBuf, kPageSize))
    {
        // Page is identical to prev; just AddRef prev and return.
        // This is correctness-safe and saves a slot allocation.
        _slots[prevSlot].refcount++;
        return prevSlot;
    }

    // Decide between xor-prev and full encodings.
    // Empirically xor-prev wins ~92% of the time on real workloads, but
    // for pages with no temporal correlation (random data), full can win.
    auto compressedXor = codec::Compress(xorBuf, kPageSize);
    auto compressedFull = codec::Compress(pageData, kPageSize);

    uint32_t idx = AllocateSlot();
    Slot& s = _slots[idx];
    s.refcount = 1;
    s.crc32c = codec::Crc32C(pageData, kPageSize);

    if (compressedXor.size() < compressedFull.size())
    {
        s.encoding = Encoding::XorPrev;
        s.payload = std::move(compressedXor);
        s.prevSlot = prevSlot;
        // Track the delta-chain reference: this XorPrev slot depends on
        // prevSlot for decoding (GetPage recursively decompresses prevSlot
        // and XORs with our payload). Without this AddRef, prevSlot could
        // be freed (refcount->0) while this XorPrev slot still references
        // it, producing use-after-free corruption on restore.
        //
        // Release() reciprocally decrements prevSlot's refcount when this
        // XorPrev slot's refcount reaches 0 (see Release below).
        _slots[prevSlot].refcount++;
    }
    else
    {
        // Full fallback: self-contained, no dependency on prevSlot.
        s.encoding = Encoding::Full;
        s.payload = std::move(compressedFull);
        s.prevSlot = 0;
    }
    _usedSlots++;
    return idx;
}

uint32_t TTDCodecPageStore::InternXorCached(uint32_t prevSlot, const uint8_t* pageData, const uint8_t* cachedPrev)
{
    assert(prevSlot < _slots.size());
    assert(_slots[prevSlot].refcount > 0);
    assert(pageData != nullptr);
    assert(cachedPrev != nullptr);

    // Compute XOR buffer using cached previous (no decompression needed!)
    uint8_t xorBuf[kPageSize];
    codec::XorBuffers(pageData, cachedPrev, xorBuf, kPageSize);

    // If XOR is all zeros, page hasn't changed
    if (codec::IsAllZero(xorBuf, kPageSize))
    {
        _slots[prevSlot].refcount++;
        return prevSlot;
    }

    // Decide between xor-prev and full encodings
    auto compressedXor = codec::Compress(xorBuf, kPageSize);
    auto compressedFull = codec::Compress(pageData, kPageSize);

    uint32_t idx = AllocateSlot();
    Slot& s = _slots[idx];
    s.refcount = 1;
    s.crc32c = codec::Crc32C(pageData, kPageSize);

    if (compressedXor.size() < compressedFull.size())
    {
        s.encoding = Encoding::XorPrev;
        s.payload = std::move(compressedXor);
        s.prevSlot = prevSlot;
        _slots[prevSlot].refcount++;
    }
    else
    {
        s.encoding = Encoding::Full;
        s.payload = std::move(compressedFull);
        s.prevSlot = 0;
    }
    _usedSlots++;
    return idx;
}

uint32_t TTDCodecPageStore::InternDirect(Encoding encoding, uint32_t prevSlot,
                                          uint32_t crc32c,
                                          const std::vector<uint8_t>& payload)
{
    const uint32_t idx = AllocateSlot();
    Slot& s = _slots[idx];
    s.encoding = encoding;
    s.refcount = 1;
    s.crc32c = crc32c;
    s.payload = payload;  // copy
    s.prevSlot = (encoding == Encoding::XorPrev) ? prevSlot : 0;

    // XorPrev slots hold a reference to their prevSlot (same as InternXor).
    if (encoding == Encoding::XorPrev)
    {
        assert(prevSlot < _slots.size() && _slots[prevSlot].refcount > 0);
        _slots[prevSlot].refcount++;
    }

    _usedSlots++;
    return idx;
}

void TTDCodecPageStore::AddRef(uint32_t idx)
{
    assert(idx < _slots.size());
    assert(_slots[idx].refcount > 0);
    _slots[idx].refcount++;
}

void TTDCodecPageStore::Release(uint32_t idx)
{
    assert(idx < _slots.size());
    assert(_slots[idx].refcount > 0);

    // Iteratively unwind the delta chain. When a XorPrev slot's refcount
    // hits zero we must also release the prevSlot it depends on (the AddRef
    // we did in InternXor). Iteration rather than recursion: delta chains
    // are bounded by kKeyFrameInterval (default 50) but a pathological
    // chain could nest deeper; iteration avoids stack-overflow risk.
    uint32_t cur = idx;
    while (true)
    {
        Slot& s = _slots[cur];
        if (--s.refcount > 0)
            return;  // Still referenced externally - done.

        // Refcount hit zero: free this slot.
        const Encoding enc = s.encoding;
        const uint32_t prev = s.prevSlot;
        _freeList.push_back(cur);
        _usedSlots--;

        // Only XorPrev slots hold a delta-chain ref to prevSlot.
        // Full/Zero slots set prevSlot=0 (or arbitrary) but don't depend
        // on it for decoding - releasing slot 0 spuriously here would
        // corrupt the store.
        if (enc != Encoding::XorPrev)
            return;

        assert(prev < _slots.size());
        assert(_slots[prev].refcount > 0);
        cur = prev;
    }
}

// ---------------------------------------------------------------------------
// Restore
// ---------------------------------------------------------------------------

bool TTDCodecPageStore::GetPageNoVerify(uint32_t idx, uint8_t* outBuf) const
{
    assert(idx < _slots.size());
    const Slot& s = _slots[idx];

    switch (s.encoding)
    {
        case Encoding::Zero:
            std::memset(outBuf, 0, kPageSize);
            return true;

        case Encoding::Full:
            if (s.payload.empty())
            {
                // Defensive: payload missing on a Full slot implies Zero.
                std::memset(outBuf, 0, kPageSize);
                return true;
            }
            return codec::Decompress(s.payload, kPageSize, outBuf);

        case Encoding::XorPrev:
        {
            // Recursively decompress prev into scratch, then XOR with our
            // payload decompressed into outBuf.
            if (s.prevSlot >= _slots.size()) return false;
            // The recursive call writes into _prevScratch (mutable member).
            _prevScratch.assign(kPageSize, 0);
            if (!GetPageNoVerify(s.prevSlot, _prevScratch.data()))
            {
                return false;
            }
            // Decompress this slot's delta into a local buffer.
            uint8_t deltaBuf[kPageSize];
            if (!codec::Decompress(s.payload, kPageSize, deltaBuf))
            {
                return false;
            }
            codec::XorBuffers(deltaBuf, _prevScratch.data(), outBuf, kPageSize);
            return true;
        }
    }
    return false;
}

bool TTDCodecPageStore::GetPage(uint32_t idx, uint8_t* outBuf) const
{
    if (!GetPageNoVerify(idx, outBuf))
    {
        return false;
    }
    // CRC32C check — guards against storage corruption (silent bit flip,
    // torn writes, partial captures after crash, etc.).
    const Slot& s = _slots[idx];
    uint32_t crc = codec::Crc32C(outBuf, kPageSize);
    if (crc != s.crc32c)
    {
        return false;
    }
    return true;
}

uint32_t TTDCodecPageStore::GetDeltaDepth(uint32_t idx) const
{
    assert(idx < _slots.size());
    uint32_t depth = 0;
    uint32_t cur = idx;
    // Walk the prev chain. Bounded by slot count; if we detect a cycle,
    // something is corrupt — return the current depth as a safety.
    for (uint32_t i = 0; i < _slots.size(); ++i)
    {
        if (_slots[cur].encoding != Encoding::XorPrev)
        {
            return depth;
        }
        cur = _slots[cur].prevSlot;
        ++depth;
        if (cur >= _slots.size()) break;
    }
    return depth;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

TTDCodecPageStore::Encoding TTDCodecPageStore::GetEncoding(uint32_t idx) const
{
    assert(idx < _slots.size());
    return _slots[idx].encoding;
}

uint32_t TTDCodecPageStore::GetRefCount(uint32_t idx) const
{
    assert(idx < _slots.size());
    return _slots[idx].refcount;
}

uint32_t TTDCodecPageStore::GetPrevSlot(uint32_t idx) const
{
    assert(idx < _slots.size());
    return _slots[idx].prevSlot;
}

bool TTDCodecPageStore::GetPayload(uint32_t idx, std::vector<uint8_t>& out) const
{
    if (idx >= _slots.size() || _slots[idx].refcount == 0)
        return false;
    const Slot& s = _slots[idx];
    out = s.payload;  // copy
    return true;
}

uint32_t TTDCodecPageStore::GetCrc32C(uint32_t idx) const
{
    assert(idx < _slots.size());
    return _slots[idx].crc32c;
}

size_t TTDCodecPageStore::GetUsedBytes() const
{
    size_t total = 0;
    for (const auto& s : _slots)
    {
        if (s.refcount > 0)
        {
            // Slot header: ~16 bytes (encoding + refcount + prevSlot + crc)
            total += 16 + s.payload.size();
        }
    }
    return total;
}

size_t TTDCodecPageStore::GetCapacityBytes() const
{
    // Slot struct vector capacity (heap-allocated vectors inside count
    // separately, but we use slot vector size for a fast conservative bound).
    return _slots.capacity() * sizeof(Slot);
}

size_t TTDCodecPageStore::GetLivePayloadBytes() const
{
    size_t total = 0;
    for (const auto& s : _slots)
    {
        if (s.refcount > 0)
        {
            total += s.payload.size();
        }
    }
    return total;
}

double TTDCodecPageStore::GetCompressionRatio() const
{
    if (_usedSlots == 0) return 1.0;
    size_t livePayload = GetLivePayloadBytes();
    if (livePayload == 0) return 1.0;
    double meanCompressed = static_cast<double>(livePayload) / _usedSlots;
    if (meanCompressed < 1.0) return static_cast<double>(kPageSize);
    return static_cast<double>(kPageSize) / meanCompressed;
}

void TTDCodecPageStore::Reset()
{
    _slots.clear();
    _slots.shrink_to_fit();
    _freeList.clear();
    _freeList.shrink_to_fit();
    _prevScratch.clear();
    _prevScratch.shrink_to_fit();
    _usedSlots = 0;
}

}  // namespace ttd
