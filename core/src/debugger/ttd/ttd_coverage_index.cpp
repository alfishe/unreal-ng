#include "ttd_coverage_index.h"

#include <algorithm>
#include <istream>
#include <ostream>

#include "ttd_compression.h"

namespace ttd
{

namespace
{

/// Typical distinct keys per frame, measured on a real demo: 318 executed, 749
/// read, 75 written. Reserve for the largest so the hot path never reallocates
/// after the first frame.
constexpr size_t kPendingReserve = 1024;

void AppendVarint(std::vector<uint8_t>& out, uint32_t value)
{
    while (value >= 0x80)
    {
        out.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<uint8_t>(value));
}

uint32_t ReadVarint(const uint8_t* data, size_t& pos)
{
    uint32_t result = 0;
    uint32_t shift = 0;
    while (true)
    {
        const uint8_t b = data[pos++];
        result |= static_cast<uint32_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0)
            break;
        shift += 7;
    }
    return result;
}

}  // namespace

TTDCoverageIndex::TTDCoverageIndex()
{
    for (size_t k = 0; k < kKindCount; ++k)
    {
        _pending[k].reserve(kPendingReserve);
        _seen[k].assign(kSeenWords, 0);
    }
}

void TTDCoverageIndex::SealFrame(uint64_t frame)
{
    for (size_t k = 0; k < kKindCount; ++k)
    {
        // Record that this frame was watched BEFORE encoding. A frame that
        // touched nothing produces no entry, and the covered range is what
        // tells a query that the absence is real rather than a gap in the
        // index.
        if (!_hasCoverage[k])
        {
            _hasCoverage[k] = true;
            _firstCoveredFrame[k] = frame;
            _lastCoveredFrame[k] = frame;
        }
        else
        {
            if (frame < _firstCoveredFrame[k]) _firstCoveredFrame[k] = frame;
            if (frame > _lastCoveredFrame[k])  _lastCoveredFrame[k] = frame;
        }

        EncodePending(k, frame);
    }
}

bool TTDCoverageIndex::CoversFrame(TTDCoverageKind kind, uint64_t frame) const
{
    const size_t kindIdx = static_cast<size_t>(kind);
    if (kindIdx >= kKindCount || !_hasCoverage[kindIdx])
        return false;

    return frame >= _firstCoveredFrame[kindIdx] && frame <= _lastCoveredFrame[kindIdx];
}

bool TTDCoverageIndex::CoveredRange(TTDCoverageKind kind, uint64_t& outFirst,
                                    uint64_t& outLast) const
{
    const size_t kindIdx = static_cast<size_t>(kind);
    if (kindIdx >= kKindCount || !_hasCoverage[kindIdx])
        return false;

    outFirst = _firstCoveredFrame[kindIdx];
    outLast  = _lastCoveredFrame[kindIdx];
    return true;
}

bool TTDCoverageIndex::FrameMayContain(TTDCoverageKind kind, uint64_t frame,
                                       uint16_t offsetLow, uint16_t offsetHigh,
                                       bool hasPage, uint8_t page) const
{
    const size_t kindIdx = static_cast<size_t>(kind);
    if (kindIdx >= kKindCount)
        return true;  // Unknown kind — never claim to have proved absence.

    if (!CoversFrame(kind, frame))
        return true;  // Not watched; the caller must not skip this frame.

    const uint32_t blockIdx = FindBlockForFrame(kindIdx, frame);
    if (blockIdx == UINT32_MAX)
        return true;
    if (!MaterializeBlock(kindIdx, blockIdx))
        return true;  // Decompression failed — fall back to replaying.

    const uint64_t base = (blockIdx < _blocks[kindIdx].size())
                              ? _blocks[kindIdx][blockIdx].baseFrame
                              : _openBlockBaseFrame[kindIdx];
    DecodeFrameFromCache(static_cast<uint32_t>(frame - base), _decodeScratch);

    for (TTDCoverageKey key : _decodeScratch)
    {
        const uint16_t offset = static_cast<uint16_t>(key & 0x3FFF);
        if (offset < offsetLow || offset > offsetHigh)
            continue;
        if (hasPage && static_cast<uint8_t>(key >> 14) != page)
            continue;
        return true;
    }

    return false;  // Watched, decoded, nothing in range.
}

void TTDCoverageIndex::EncodePending(size_t kindIdx, uint64_t frame)
{
    std::vector<TTDCoverageKey>& pending = _pending[kindIdx];

    // An empty frame still gets a slot — a zero count. Skipping it entirely
    // would break the "block covers [baseFrame, baseFrame+frameCount)" mapping
    // that replaces the per-frame table.

    // Release the membership bits for exactly the keys this frame touched, so
    // the next frame starts clean without touching the rest of the bitmap.
    std::vector<uint64_t>& seen = _seen[kindIdx];
    for (TTDCoverageKey key : pending)
    {
        const TTDCoverageKey masked = key & (kKeySpace - 1);
        seen[masked >> 6] &= ~(uint64_t{1} << (masked & 63));
    }

    // Reset the repeat filter for the next frame. Without this a key touched in
    // frame N would be skipped in frame N+1 and the set would be incomplete —
    // a false negative, the one failure mode that actually loses answers.
    std::fill(std::begin(_recent[kindIdx]), std::end(_recent[kindIdx]), TTDCoverageKey{0});

    // Already deduplicated by the membership bitmap; sorting is what the delta
    // encoding below needs.
    std::sort(pending.begin(), pending.end());

    std::vector<uint8_t>& pool = _openBlock[kindIdx];

    if (_openBlockFrames[kindIdx] == 0)
        _openBlockBaseFrame[kindIdx] = frame;

    // Frame layout inside the block: key count, then sorted delta varints.
    AppendVarint(pool, static_cast<uint32_t>(pending.size()));

    TTDCoverageKey prev = 0;
    for (TTDCoverageKey key : pending)
    {
        AppendVarint(pool, key - prev);
        prev = key;
    }

    pending.clear();  // Keeps the reserved capacity for the next frame.

    if (++_openBlockFrames[kindIdx] >= kFramesPerBlock)
        CloseOpenBlock(kindIdx);
}

void TTDCoverageIndex::CloseOpenBlock(size_t kindIdx)
{
    std::vector<uint8_t>& raw = _openBlock[kindIdx];
    if (raw.empty())
    {
        _openBlockFrames[kindIdx] = 0;
        return;
    }

    Block block;
    block.rawSize = static_cast<uint32_t>(raw.size());
    block.baseFrame = _openBlockBaseFrame[kindIdx];
    block.frameCount = _openBlockFrames[kindIdx];
    block.compressed = codec::Compress(raw.data(), raw.size());

    _rawBytes[kindIdx] += block.rawSize;
    _compressedBytes[kindIdx] += block.compressed.size();

    _blocks[kindIdx].push_back(std::move(block));

    raw.clear();
    _openBlockFrames[kindIdx] = 0;
}

void TTDCoverageIndex::FlushOpenBlocks()
{
    for (size_t k = 0; k < kKindCount; ++k)
        CloseOpenBlock(k);
}

bool TTDCoverageIndex::MaterializeBlock(size_t kindIdx, uint32_t blockIdx) const
{
    if (_blockCacheKind == kindIdx && _blockCacheIndex == blockIdx)
        return true;

    const bool isOpenBlock = (blockIdx >= _blocks[kindIdx].size());
    uint32_t frameCount = 0;

    if (isOpenBlock)
    {
        _blockCache = _openBlock[kindIdx];
        frameCount = _openBlockFrames[kindIdx];
    }
    else
    {
        const Block& block = _blocks[kindIdx][blockIdx];
        _blockCache.assign(block.rawSize, 0);
        if (!codec::Decompress(block.compressed, block.rawSize, _blockCache.data()))
        {
            _blockCacheKind = SIZE_MAX;
            _blockCacheIndex = UINT32_MAX;
            return false;
        }
        frameCount = block.frameCount;
    }

    // Walk the block once to note where each frame starts. Done here rather
    // than per query so a backward scan over consecutive frames pays for it
    // once per block.
    _blockFrameOffsets.clear();
    _blockFrameCounts.clear();
    _blockFrameOffsets.reserve(frameCount);
    _blockFrameCounts.reserve(frameCount);

    size_t pos = 0;
    for (uint32_t f = 0; f < frameCount && pos < _blockCache.size(); ++f)
    {
        const uint32_t count = ReadVarint(_blockCache.data(), pos);
        _blockFrameOffsets.push_back(static_cast<uint32_t>(pos));
        _blockFrameCounts.push_back(count);

        TTDCoverageKey ignored = 0;
        for (uint32_t i = 0; i < count; ++i)
            ignored += ReadVarint(_blockCache.data(), pos);
        (void)ignored;
    }

    _blockCacheKind = kindIdx;
    _blockCacheIndex = blockIdx;
    return true;
}

uint32_t TTDCoverageIndex::FindBlockForFrame(size_t kindIdx, uint64_t frame) const
{
    const std::vector<Block>& blocks = _blocks[kindIdx];
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        if (frame >= blocks[i].baseFrame &&
            frame < blocks[i].baseFrame + blocks[i].frameCount)
        {
            return static_cast<uint32_t>(i);
        }
    }

    // Might be in the block still being accumulated.
    if (_openBlockFrames[kindIdx] > 0 &&
        frame >= _openBlockBaseFrame[kindIdx] &&
        frame < _openBlockBaseFrame[kindIdx] + _openBlockFrames[kindIdx])
    {
        return static_cast<uint32_t>(blocks.size());
    }

    return UINT32_MAX;
}

void TTDCoverageIndex::DecodeFrameFromCache(uint32_t frameIndexInBlock,
                                            std::vector<TTDCoverageKey>& out) const
{
    out.clear();
    if (frameIndexInBlock >= _blockFrameOffsets.size())
        return;

    size_t pos = _blockFrameOffsets[frameIndexInBlock];
    const uint32_t count = _blockFrameCounts[frameIndexInBlock];
    out.reserve(count);

    TTDCoverageKey prev = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        prev += ReadVarint(_blockCache.data(), pos);
        out.push_back(prev);
    }
}

void TTDCoverageIndex::Clear()
{
    // Invalidate the query cache too, or a stale decompressed block would be
    // served for a session it does not belong to.
    _blockCache.clear();
    _blockCacheKind = SIZE_MAX;
    _blockCacheIndex = UINT32_MAX;
    _blockFrameOffsets.clear();
    _blockFrameCounts.clear();

    for (size_t k = 0; k < kKindCount; ++k)
    {
        _pending[k].clear();
        _seen[k].assign(kSeenWords, 0);
        std::fill(std::begin(_recent[k]), std::end(_recent[k]), TTDCoverageKey{0});
        _blocks[k].clear();
        _openBlock[k].clear();
        _openBlockFrames[k] = 0;
        _openBlockBaseFrame[k] = 0;
        _compressedBytes[k] = 0;
        _rawBytes[k] = 0;
        _hasCoverage[k] = false;
        _firstCoveredFrame[k] = 0;
        _lastCoveredFrame[k] = 0;

    }
}


bool TTDCoverageIndex::FindLastFrameTouching(TTDCoverageKind kind, TTDCoverageKey key,
                                             uint64_t beforeFrame, uint64_t& outFrame) const
{
    const size_t kindIdx = static_cast<size_t>(kind);
    if (kindIdx >= kKindCount || !_hasCoverage[kindIdx])
        return false;

    const uint64_t start = std::min(beforeFrame, _lastCoveredFrame[kindIdx]);
    if (start < _firstCoveredFrame[kindIdx])
        return false;

    for (uint64_t frame = start + 1; frame-- > _firstCoveredFrame[kindIdx];)
    {
        if (FrameTouches(kind, frame, key))
        {
            outFrame = frame;
            return true;
        }
        if (frame == 0)
            break;
    }

    return false;
}

bool TTDCoverageIndex::FrameTouches(TTDCoverageKind kind, uint64_t frame,
                                    TTDCoverageKey key) const
{
    const size_t kindIdx = static_cast<size_t>(kind);
    if (kindIdx >= kKindCount)
        return false;

    const uint32_t blockIdx = FindBlockForFrame(kindIdx, frame);
    if (blockIdx == UINT32_MAX || !MaterializeBlock(kindIdx, blockIdx))
        return false;

    const uint64_t base = (blockIdx < _blocks[kindIdx].size())
                              ? _blocks[kindIdx][blockIdx].baseFrame
                              : _openBlockBaseFrame[kindIdx];
    DecodeFrameFromCache(static_cast<uint32_t>(frame - base), _decodeScratch);

    return std::binary_search(_decodeScratch.begin(), _decodeScratch.end(), key);
}

size_t TTDCoverageIndex::SealedFrameCount(TTDCoverageKind kind) const
{
    const size_t kindIdx = static_cast<size_t>(kind);
    if (kindIdx >= kKindCount)
        return 0;

    size_t total = _openBlockFrames[kindIdx];
    for (const Block& b : _blocks[kindIdx])
        total += b.frameCount;
    return total;
}

size_t TTDCoverageIndex::EncodedBytes(TTDCoverageKind kind) const
{
    const size_t kindIdx = static_cast<size_t>(kind);
    if (kindIdx >= kKindCount)
        return 0;

    // Sealed blocks are compressed; the open one is not yet.
    return _compressedBytes[kindIdx] + _openBlock[kindIdx].size();
}

size_t TTDCoverageIndex::RawEncodedBytes(TTDCoverageKind kind) const
{
    const size_t kindIdx = static_cast<size_t>(kind);
    if (kindIdx >= kKindCount)
        return 0;

    return _rawBytes[kindIdx] + _openBlock[kindIdx].size();
}

size_t TTDCoverageIndex::HeapBytes() const
{
    size_t total = 0;
    for (size_t k = 0; k < kKindCount; ++k)
    {
        total += _pending[k].capacity() * sizeof(TTDCoverageKey);
        total += _seen[k].capacity() * sizeof(uint64_t);
        total += _openBlock[k].capacity();
        for (const Block& b : _blocks[k])
            total += b.compressed.capacity() + sizeof(Block);
    }
    total += _decodeScratch.capacity() * sizeof(TTDCoverageKey);
    total += _blockCache.capacity();
    return total;
}



namespace
{

/// Section marker, so a truncated or misplaced read fails loudly instead of
/// producing a plausible-looking empty index.
constexpr uint32_t kCoverageSectionMagic = 0x56435654;  // 'TVCV'
constexpr uint16_t kCoverageSectionVersion = 1;

template <typename T>
bool WritePod(std::ostream& out, const T& value)
{
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return static_cast<bool>(out);
}

template <typename T>
bool ReadPod(std::istream& in, T& value)
{
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(in);
}

}  // namespace

bool TTDCoverageIndex::Serialize(std::ostream& out) const
{
    if (!WritePod(out, kCoverageSectionMagic)) return false;
    if (!WritePod(out, kCoverageSectionVersion)) return false;

    const uint16_t kindCount = static_cast<uint16_t>(kKindCount);
    if (!WritePod(out, kindCount)) return false;

    for (size_t k = 0; k < kKindCount; ++k)
    {
        // The block still being accumulated is compressed on the fly rather
        // than sealed in place: serialization must not mutate the live index,
        // and a session serialized mid-recording would otherwise lose its most
        // recent frames.
        Block pending;
        const bool hasPending = _openBlockFrames[k] > 0 && !_openBlock[k].empty();
        if (hasPending)
        {
            pending.rawSize = static_cast<uint32_t>(_openBlock[k].size());
            pending.baseFrame = _openBlockBaseFrame[k];
            pending.frameCount = _openBlockFrames[k];
            pending.compressed = codec::Compress(_openBlock[k].data(), _openBlock[k].size());
        }

        const uint32_t blockCount =
            static_cast<uint32_t>(_blocks[k].size()) + (hasPending ? 1u : 0u);
        if (!WritePod(out, blockCount)) return false;

        auto writeBlock = [&out](const Block& b) -> bool
        {
            if (!WritePod(out, b.baseFrame)) return false;
            if (!WritePod(out, b.frameCount)) return false;
            if (!WritePod(out, b.rawSize)) return false;

            const uint32_t compSize = static_cast<uint32_t>(b.compressed.size());
            if (!WritePod(out, compSize)) return false;

            if (compSize != 0)
            {
                out.write(reinterpret_cast<const char*>(b.compressed.data()), compSize);
                if (!out) return false;
            }
            return true;
        };

        for (const Block& b : _blocks[k])
            if (!writeBlock(b)) return false;

        if (hasPending && !writeBlock(pending)) return false;
    }

    return true;
}

bool TTDCoverageIndex::Deserialize(std::istream& in)
{
    Clear();

    uint32_t magic = 0;
    if (!ReadPod(in, magic) || magic != kCoverageSectionMagic)
        return false;

    uint16_t version = 0;
    if (!ReadPod(in, version) || version != kCoverageSectionVersion)
        return false;

    uint16_t kindCount = 0;
    if (!ReadPod(in, kindCount) || kindCount != static_cast<uint16_t>(kKindCount))
        return false;

    for (size_t k = 0; k < kKindCount; ++k)
    {
        uint32_t blockCount = 0;
        if (!ReadPod(in, blockCount)) { Clear(); return false; }

        for (uint32_t i = 0; i < blockCount; ++i)
        {
            Block b;
            uint32_t compSize = 0;
            if (!ReadPod(in, b.baseFrame) || !ReadPod(in, b.frameCount) ||
                !ReadPod(in, b.rawSize)  || !ReadPod(in, compSize))
            {
                Clear();
                return false;
            }

            b.compressed.assign(compSize, 0);
            if (compSize != 0)
            {
                in.read(reinterpret_cast<char*>(b.compressed.data()), compSize);
                if (!in) { Clear(); return false; }
            }

            _rawBytes[k] += b.rawSize;
            _compressedBytes[k] += compSize;

            // Rebuild the covered range from the blocks themselves rather than
            // storing it: a stored range could disagree with the blocks and
            // would then authorise pruning frames the index does not hold.
            const uint64_t firstFrame = b.baseFrame;
            const uint64_t lastFrame = b.baseFrame + (b.frameCount ? b.frameCount - 1 : 0);
            if (!_hasCoverage[k])
            {
                _hasCoverage[k] = true;
                _firstCoveredFrame[k] = firstFrame;
                _lastCoveredFrame[k] = lastFrame;
            }
            else
            {
                if (firstFrame < _firstCoveredFrame[k]) _firstCoveredFrame[k] = firstFrame;
                if (lastFrame > _lastCoveredFrame[k])   _lastCoveredFrame[k] = lastFrame;
            }

            _blocks[k].push_back(std::move(b));
        }
    }

    return true;
}

}  // namespace ttd
