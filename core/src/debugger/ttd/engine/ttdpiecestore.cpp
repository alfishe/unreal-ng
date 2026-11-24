#include "ttdpiecestore.h"

#include <atomic>
#include <cassert>
#include <cstring>

#include "debugger/ttd/ttdcompression.h"
#include "ttdregion.h"

namespace ttd
{

namespace
{
constexpr uint32_t kPiece = kTTDPieceSize;
std::atomic<uint16_t> g_defaultSkipStride{7};
uint8_t RunByte(uint32_t run)
{
    return static_cast<uint8_t>(run < 255 ? run : 255);
}
}  // namespace

uint16_t TTDPieceStore::DefaultSkipStride()
{
    return g_defaultSkipStride.load(std::memory_order_relaxed);
}

void TTDPieceStore::SetDefaultSkipStride(uint16_t stride)
{
    g_defaultSkipStride.store(stride, std::memory_order_relaxed);
}

TTDPieceId TTDPieceStore::Allocate()
{
    TTDPieceId id;
    if (!_free.empty())
    {
        id = _free.back();
        _free.pop_back();
        _versions[id] = Version{};
    }
    else
    {
        id = static_cast<TTDPieceId>(_versions.size());
        _versions.emplace_back();
    }
    _versions[id].refcount = 1;
    _liveVersions++;
    _work.versionsStored++;
    return id;
}

std::vector<uint8_t> TTDPieceStore::Compress(const uint8_t* bytes)
{
    _work.compressCalls++;
    _work.compressInputBytes += kPiece;
    return codec::Compress(bytes, kPiece);
}

TTDPieceId TTDPieceStore::StoreFull(const uint8_t* bytes, uint32_t crc)
{
    const TTDPieceId id = Allocate();
    Version& v = _versions[id];
    v.crc32c = crc;
    if (codec::IsAllZero(bytes, kPiece))
    {
        v.encoding = Encoding::Zero;
        return id;
    }
    const std::vector<uint8_t> packed = Compress(bytes);
    v.encoding = Encoding::Full;
    v.payload = _arena.Store(packed.data(), static_cast<uint32_t>(packed.size()));
    return id;
}

TTDPieceId TTDPieceStore::Import(Encoding encoding, TTDPieceId base, uint16_t depth, uint32_t crc,
                                  const uint8_t* payload, size_t size)
{
    const bool difference = IsDifference(encoding);
    if (difference != (base != kNone) || (difference && (base >= _versions.size() || _versions[base].refcount == 0)) ||
        (encoding == Encoding::Zero) != (size == 0) || size > UINT32_MAX ||
        static_cast<uint8_t>(encoding) > static_cast<uint8_t>(Encoding::Ranges))
        return kNone;
    const TTDPieceId id = Allocate();
    Version& v = _versions[id];
    v.encoding = encoding;
    v.crc32c = crc;
    v.depth = difference ? depth : 0;
    v.run = RunByte(v.depth);   // not stored: at least the depth
    v.base = base;
    if (size)
        v.payload = _arena.Store(payload, static_cast<uint32_t>(size));
    if (difference)
        AddRef(base);
    return id;
}

TTDPieceId TTDPieceStore::InternFirst(const uint8_t* bytes)
{
    return StoreFull(bytes, codec::Crc32C(bytes, kPiece));
}

namespace
{
/// The non-zero runs of a difference: [start, end) each, runs closer than
/// 4 zero bytes merged (a 3-byte header costs more than the gap), split at
/// 255 bytes (the length is one byte). A changed piece's difference is mostly
/// zero: zeros are skipped 8 bytes at a time (byte by byte this scan cost more
/// than compressing the difference). SIMD-CANDIDATE(ttd-ranges-scan): the
/// zero skip and the run walk are a vector compare-to-zero and a mask scan
template <typename F>
void ForEachRun(const uint8_t* diff, size_t size, F&& f)
{
    size_t i = 0;
    while (i < size)
    {
        if (diff[i] == 0)
        {
            ++i;
            while (i + 8 <= size)
            {
                uint64_t word;
                std::memcpy(&word, diff + i, 8);
                if (word != 0)
                    break;
                i += 8;
            }
            continue;
        }
        size_t end = i + 1;
        size_t j = end;
        while (j < size && j - i < 255)
        {
            if (diff[j] != 0)
                end = ++j;
            else if (j - end < 3)
                ++j;
            else
                break;
        }
        f(i, end);
        i = end;
    }
}
}  // namespace

size_t TTDPieceStore::RangesSize(const uint8_t* diff)
{
    size_t size = 0;
    ForEachRun(diff, kPiece, [&](size_t start, size_t end) { size += 3 + (end - start); });
    return size;
}

TTDPieceId TTDPieceStore::StoreRanges(TTDPieceId previous, uint32_t depth, uint32_t run, const uint8_t* diff,
                                      size_t size, uint32_t crc)
{
    std::vector<uint8_t> payload;
    payload.reserve(size);
    ForEachRun(diff, kPiece, [&](size_t start, size_t end) {
        payload.push_back(static_cast<uint8_t>(start));
        payload.push_back(static_cast<uint8_t>(start >> 8));
        payload.push_back(static_cast<uint8_t>(end - start));
        payload.insert(payload.end(), diff + start, diff + end);
    });
    const TTDPieceId id = Allocate();
    Version& v = _versions[id];
    v.crc32c = crc;
    v.encoding = Encoding::Ranges;
    v.depth = static_cast<uint16_t>(depth);
    v.run = RunByte(run);
    v.base = previous;
    v.payload = _arena.Store(payload.data(), static_cast<uint32_t>(payload.size()));
    AddRef(previous);   // a difference depends on its base
    return id;
}

bool TTDPieceStore::ApplyRanges(const uint8_t* payload, size_t size, uint8_t* out)
{
    size_t at = 0;
    while (at < size)
    {
        if (size - at < 3)
            return false;
        const size_t start = payload[at] | (size_t(payload[at + 1]) << 8);
        const size_t length = payload[at + 2];
        at += 3;
        if (length == 0 || start + length > kPiece || size - at < length)
            return false;
        for (size_t k = 0; k < length; ++k)
            out[start + k] ^= payload[at + k];
        at += length;
    }
    return true;
}

TTDPieceId TTDPieceStore::Intern(TTDPieceId previous, const uint8_t* previousBytes, const uint8_t* bytes,
                                 TTDPieceId anchor, const uint8_t* anchorBytes)
{
    if (previous == kNone)
        return InternFirst(bytes);
    assert(previous < _versions.size() && _versions[previous].refcount > 0);

    uint8_t diff[kPiece];
    codec::XorBuffers(bytes, previousBytes, diff, kPiece);
    if (codec::IsAllZero(diff, kPiece))
    {
        AddRef(previous);
        return previous;
    }

    const uint32_t crc = codec::Crc32C(bytes, kPiece);
    if (codec::IsAllZero(bytes, kPiece))
        return StoreFull(bytes, crc);   // a Zero version

    // The chain limit: the K-th version of a chain is stored Full
    const uint32_t run = uint32_t(_versions[previous].run) + 1;
    uint32_t depth = uint32_t(_versions[previous].depth) + 1;
    if (depth >= _params.chainLimit || run >= _params.chainLimit)
    {
        _work.forcedFull++;
        return StoreFull(bytes, crc);
    }

    // An anchor point (the S-th, 2S-th ... version): a difference from the anchor S versions back, so a decode
    // skips the versions between. The anchor is checked to be that ancestor (the caller's id may be stale). A
    // piece back at its anchor's content takes the anchor's version again: no empty difference is stored, and the
    // chain goes on from the anchor (a value toggling with the stride's period)
    if (_params.skipStride > 1 && run % _params.skipStride == 0 && anchor != kNone && anchorBytes != nullptr)
    {
        TTDPieceId ancestor = previous;
        for (uint32_t k = 1; k < _params.skipStride && ancestor != anchor && IsDifference(_versions[ancestor].encoding); ++k)
            ancestor = _versions[ancestor].base;
        if (ancestor == anchor && uint32_t(_versions[anchor].run) + _params.skipStride == run)
        {
            uint8_t skip[kPiece];
            codec::XorBuffers(bytes, anchorBytes, skip, kPiece);
            if (codec::IsAllZero(skip, kPiece))
            {
                AddRef(anchor);
                return anchor;
            }
            std::memcpy(diff, skip, kPiece);
            previous = anchor;
            depth = uint32_t(_versions[anchor].depth) + 1;
        }
    }

    // A few bytes changed: their runs as they are, nothing compressed
    const size_t rangesSize = RangesSize(diff);
    if (rangesSize <= _params.rangesLimit)
        return StoreRanges(previous, depth, run, diff, rangesSize, crc);

    // Encode once: the difference first; the full piece only when the
    // difference is larger than T, the smaller of the two kept
    std::vector<uint8_t> packedDiff = Compress(diff);
    if (rangesSize < packedDiff.size())
        return StoreRanges(previous, depth, run, diff, rangesSize, crc);
    if (packedDiff.size() > _params.fullThreshold)
    {
        std::vector<uint8_t> packedFull = Compress(bytes);
        if (packedFull.size() < packedDiff.size())
        {
            const TTDPieceId id = Allocate();
            Version& v = _versions[id];
            v.crc32c = crc;
            v.encoding = Encoding::Full;
            v.payload = _arena.Store(packedFull.data(), static_cast<uint32_t>(packedFull.size()));
            return id;
        }
    }

    const TTDPieceId id = Allocate();
    Version& v = _versions[id];
    v.crc32c = crc;
    v.encoding = Encoding::Xor;
    v.depth = static_cast<uint16_t>(depth);
    v.run = RunByte(run);
    v.base = previous;
    v.payload = _arena.Store(packedDiff.data(), static_cast<uint32_t>(packedDiff.size()));
    AddRef(previous);   // a difference depends on its base
    return id;
}

void TTDPieceStore::Release(TTDPieceId id)
{
    // Iterative: freeing a version releases its base, which may free in turn
    while (id != kNone)
    {
        assert(id < _versions.size() && _versions[id].refcount > 0);
        Version& v = _versions[id];
        if (--v.refcount > 0)
            return;
        const TTDPieceId base = IsDifference(v.encoding) ? v.base : kNone;
        _arena.Release(v.payload);
        v = Version{};
        _free.push_back(id);
        _liveVersions--;
        ++_reuseGeneration;
        id = base;
    }
}

bool TTDPieceStore::Decode(TTDPieceId id, uint8_t* out) const
{
    assert(id < _versions.size() && _versions[id].refcount > 0);

    // Walk to the chain's start (a Full or Zero version), then apply the
    // differences forward
    TTDPieceId chain[1024];
    uint32_t links = 0;
    TTDPieceId cur = id;
    while (IsDifference(_versions[cur].encoding))
    {
        if (links == sizeof(chain) / sizeof(chain[0]))
            return false;
        chain[links++] = cur;
        cur = _versions[cur].base;
    }

    const Version& start = _versions[cur];
    if (start.encoding == Encoding::Zero)
        std::memset(out, 0, kPiece);
    else
    {
        if (!codec::Decompress(_arena.Data(start.payload), start.payload.size, kPiece, out))
            return false;
    }

    uint8_t diff[kPiece];
    for (uint32_t i = links; i-- > 0;)
    {
        const Version& v = _versions[chain[i]];
        if (v.encoding == Encoding::Ranges)
        {
            if (!ApplyRanges(_arena.Data(v.payload), v.payload.size, out))
                return false;
        }
        else
        {
            if (!codec::Decompress(_arena.Data(v.payload), v.payload.size, kPiece, diff))
                return false;
            codec::XorBuffers(out, diff, out, kPiece);
        }
        _work.linksDecoded++;
    }
    return codec::Crc32C(out, kPiece) == _versions[id].crc32c;
}

bool TTDPieceStore::DecodeFrom(TTDPieceId id, TTDPieceId have, uint8_t* inout, bool& damaged) const
{
    damaged = false;
    if (have == kNone || have >= _versions.size() || _versions[have].refcount == 0)
        return false;
    if (id == have)
        return true;
    // Which one descends from the other: walk each back to its chain's start looking for the other
    auto distance = [this](TTDPieceId from, TTDPieceId to, uint32_t& depth) {
        uint32_t steps = 0;
        for (TTDPieceId cur = from; IsDifference(_versions[cur].encoding); cur = _versions[cur].base)
        {
            ++steps;
            if (_versions[cur].base == to)
            {
                depth = steps;
                return true;
            }
        }
        depth = steps;   // from's own chain: Decode applies this many
        return false;
    };
    uint32_t up = 0, down = 0;
    const bool idAbove = distance(id, have, up);      // id descends from have: up differences of id's line
    const bool haveAbove = !idAbove && distance(have, id, down);   // have descends from id
    if (!idAbove && !haveAbove)
        return false;
    uint32_t full = 0;
    if (haveAbove)
    {
        distance(id, kNone, full);
        if (down > full)   // the chain from its start is shorter
            return false;
    }
    // The differences between them, each applied once (XOR: the same step either way)
    TTDPieceId chain[1024];
    uint32_t links = 0;
    for (TTDPieceId cur = idAbove ? id : have; cur != (idAbove ? have : id); cur = _versions[cur].base)
    {
        if (links == sizeof(chain) / sizeof(chain[0]))
            return false;
        chain[links++] = cur;
    }
    uint8_t diff[kPiece];
    for (uint32_t i = links; i-- > 0;)
    {
        const Version& v = _versions[chain[i]];
        bool ok = true;
        if (v.encoding == Encoding::Ranges)
            ok = ApplyRanges(_arena.Data(v.payload), v.payload.size, inout);
        else
        {
            ok = codec::Decompress(_arena.Data(v.payload), v.payload.size, kPiece, diff);
            if (ok)
                codec::XorBuffers(inout, diff, inout, kPiece);
        }
        if (!ok)
        {
            damaged = true;
            return false;
        }
        _work.linksDecoded++;
    }
    if (codec::Crc32C(inout, kPiece) != _versions[id].crc32c)
    {
        damaged = true;
        return false;
    }
    return true;
}

size_t TTDPieceStore::VersionTableBytes() const
{
    return _versions.capacity() * sizeof(Version) + _free.capacity() * sizeof(TTDPieceId);
}

size_t TTDPieceStore::HeapBytes() const
{
    return VersionTableBytes() + _arena.HeapBytes();
}

void TTDPieceStore::Clear()
{
    ++_reuseGeneration;
    _versions.clear();
    _versions.shrink_to_fit();
    _free.clear();
    _free.shrink_to_fit();
    _liveVersions = 0;
    _arena.Clear();
    _work = Work{};
}

}  // namespace ttd
