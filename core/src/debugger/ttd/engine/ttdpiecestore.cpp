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

TTDPieceId TTDPieceStore::InternFirst(const uint8_t* bytes)
{
    return StoreFull(bytes, codec::Crc32C(bytes, kPiece));
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

    // Encode once: the difference first; the full piece only when the
    // difference is larger than T, the smaller of the two kept
    std::vector<uint8_t> packedDiff = Compress(diff);
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
        const TTDPieceId base = v.encoding == Encoding::Xor ? v.base : kNone;
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
    while (_versions[cur].encoding == Encoding::Xor)
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
        if (!codec::Decompress(_arena.Data(v.payload), v.payload.size, kPiece, diff))
            return false;
        codec::XorBuffers(out, diff, out, kPiece);
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
