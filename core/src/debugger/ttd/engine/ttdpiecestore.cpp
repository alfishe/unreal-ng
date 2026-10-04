#include "ttdpiecestore.h"

#include <cassert>
#include <cstring>

#include "debugger/ttd/ttdcompression.h"
#include "ttdregion.h"

namespace ttd
{

namespace
{
constexpr uint32_t kPiece = kTTDPieceSize;
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

TTDPieceId TTDPieceStore::StoreRanges(TTDPieceId previous, uint32_t depth, const uint8_t* diff, size_t size,
                                      uint32_t crc)
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

TTDPieceId TTDPieceStore::Intern(TTDPieceId previous, const uint8_t* previousBytes, const uint8_t* bytes)
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

    // The chain limit: a version that would reach depth K is stored Full
    const uint32_t depth = uint32_t(_versions[previous].depth) + 1;
    if (depth >= _params.chainLimit)
    {
        _work.forcedFull++;
        return StoreFull(bytes, crc);
    }

    // A few bytes changed: their runs as they are, nothing compressed
    const size_t rangesSize = RangesSize(diff);
    if (rangesSize <= _params.rangesLimit)
        return StoreRanges(previous, depth, diff, rangesSize, crc);

    // Encode once: the difference first; the full piece only when the
    // difference is larger than T, the smaller of the two kept
    std::vector<uint8_t> packedDiff = Compress(diff);
    if (rangesSize < packedDiff.size())
        return StoreRanges(previous, depth, diff, rangesSize, crc);
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
    _versions.clear();
    _versions.shrink_to_fit();
    _free.clear();
    _free.shrink_to_fit();
    _liveVersions = 0;
    _arena.Clear();
    _work = Work{};
}

}  // namespace ttd
