#include "ttdcoverageindex.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstring>
#include <istream>
#include <iterator>
#include <ostream>

#include "ttdcompression.h"

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
        _seenWords[k].assign(kSeenSummaryWords, 0);
    }
}

void TTDCoverageIndex::SealFrame(uint64_t frame)
{
    if (frame == _holeToSeal)
    {
        // The partial frame of a mid-frame resume: its set would be missing
        // the accesses before the resume point. No entry; the next frame
        // starts a new block, and the gap marks this one as not covered
        _holeToSeal = UINT64_MAX;
        for (size_t k = 0; k < kKindCount; ++k)
        {
            DiscardPending(k);
            CloseOpenBlock(k);
            if (_hasCoverage[k] && frame > _lastCoveredFrame[k])
                _holes[k].push_back(frame);
        }
        return;
    }

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

    return frame >= _firstCoveredFrame[kindIdx] && frame <= _lastCoveredFrame[kindIdx] && !IsHole(kindIdx, frame);
}

bool TTDCoverageIndex::IsHole(size_t kindIdx, uint64_t frame) const
{
    const std::vector<uint64_t>& holes = _holes[kindIdx];
    return std::binary_search(holes.begin(), holes.end(), frame);
}

void TTDCoverageIndex::DiscardPending(size_t kindIdx)
{
    std::vector<uint64_t>& seen = _seen[kindIdx];
    std::vector<uint64_t>& words = _seenWords[kindIdx];
    for (TTDCoverageKey key : _pending[kindIdx])
    {
        const TTDCoverageKey masked = key & (kKeySpace - 1);
        seen[masked >> 6] = 0;   // every key of the word is in the list
        words[masked >> 12] = 0;
    }
    _pending[kindIdx].clear();
    std::fill(std::begin(_recent[kindIdx]), std::end(_recent[kindIdx]), TTDCoverageKey{0});
}

size_t TTDCoverageIndex::RawPrefixBytes(const std::vector<uint8_t>& raw, uint32_t frames)
{
    size_t pos = 0;
    for (uint32_t f = 0; f < frames && pos < raw.size(); ++f)
    {
        const uint32_t count = ReadVarint(raw.data(), pos);
        for (uint32_t i = 0; i < count; ++i)
            (void)ReadVarint(raw.data(), pos);
    }
    return pos;
}

void TTDCoverageIndex::DropFramesFrom(uint64_t frame, bool partialFirstFrame)
{
    // The query cache may hold a block that is about to change
    _blockCache.clear();
    _blockCacheKind = SIZE_MAX;
    _blockCacheIndex = UINT32_MAX;
    _rowsCacheIndex = UINT32_MAX;
    _blockFrameOffsets.clear();
    _blockFrameCounts.clear();

    for (size_t k = 0; k < kKindCount; ++k)
    {
        // The frame under accumulation belongs to the discarded future
        DiscardPending(k);

        std::vector<uint64_t>& holes = _holes[k];
        holes.erase(std::lower_bound(holes.begin(), holes.end(), frame), holes.end());

        if (!_hasCoverage[k])
            continue;
        if (frame <= _firstCoveredFrame[k])
        {
            // Nothing before the cut was indexed for this kind
            _blocks[k].clear();
            _openBlock[k].clear();
            ResetOpenSummary(k);
            _openBlockFrames[k] = 0;
            _openBlockBaseFrame[k] = 0;
            _compressedBytes[k] = 0;
            _rawBytes[k] = 0;
            _hasCoverage[k] = false;
            _firstCoveredFrame[k] = 0;
            _lastCoveredFrame[k] = 0;
            holes.clear();
            continue;
        }

        // The block still being filled
        if (_openBlockFrames[k] > 0)
        {
            const uint64_t base = _openBlockBaseFrame[k];
            if (base >= frame)
            {
                _openBlock[k].clear();
                _openBlockFrames[k] = 0;
                ResetOpenSummary(k);
            }
            else if (base + _openBlockFrames[k] > frame)
            {
                const uint32_t keep = static_cast<uint32_t>(frame - base);
                _openBlock[k].resize(RawPrefixBytes(_openBlock[k], keep));
                _openBlockFrames[k] = keep;
                ResetOpenSummary(k);   // its summary held the dropped frames: built by a query instead
            }
        }

        // Sealed blocks: whole ones past the cut go, a straddling one is cut
        std::vector<Block>& blocks = _blocks[k];
        while (!blocks.empty() && blocks.back().baseFrame >= frame)
        {
            _rawBytes[k] -= blocks.back().rawSize;
            _compressedBytes[k] -= blocks.back().compressed.size();
            blocks.pop_back();
        }
        if (!blocks.empty())
        {
            Block& b = blocks.back();
            if (b.baseFrame + b.frameCount > frame)
            {
                std::vector<uint8_t> raw(b.rawSize);
                if (!codec::Decompress(b.compressed, b.rawSize, raw.data()))
                {
                    // Cannot cut it: forget it, and the frames it held are
                    // no longer covered
                    _rawBytes[k] -= b.rawSize;
                    _compressedBytes[k] -= b.compressed.size();
                    const uint64_t lost = b.baseFrame;
                    blocks.pop_back();
                    if (lost <= _firstCoveredFrame[k])
                    {
                        _hasCoverage[k] = false;
                        continue;
                    }
                    frame = lost;
                }
                else
                {
                    const uint32_t keep = static_cast<uint32_t>(frame - b.baseFrame);
                    raw.resize(RawPrefixBytes(raw, keep));
                    _rawBytes[k] -= b.rawSize;
                    _compressedBytes[k] -= b.compressed.size();
                    b.compressed = codec::Compress(raw.data(), raw.size());
                    b.rawSize = static_cast<uint32_t>(raw.size());
                    b.frameCount = keep;
                    b.unionKeys.clear();
                    b.unionRawSize = 0;
                    b.frameKeyCounts.clear();
                    b.pieceMask.clear();
                    b.unionSpans.clear();
                    b.unionCount = 0;
                    b.frameRows.clear();
                    b.frameRowsRawSize = 0;
                    b.summarized = false;
                    _rawBytes[k] += b.rawSize;
                    _compressedBytes[k] += b.compressed.size();
                }
            }
        }

        if (_lastCoveredFrame[k] >= frame)
            _lastCoveredFrame[k] = frame - 1;

        // The next frame must start a fresh open block after the kept frames:
        // close a kept partial open block so a later hole or seal cannot
        // append out of order
        if (partialFirstFrame)
            CloseOpenBlock(k);
    }

    _holeToSeal = partialFirstFrame ? frame : UINT64_MAX;
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

const std::vector<TTDCoverageKey>* TTDCoverageIndex::DecodeWatchedFrame(TTDCoverageKind kind, uint64_t frame) const
{
    const size_t kindIdx = static_cast<size_t>(kind);
    if (kindIdx >= kKindCount)
        return nullptr;  // Unknown kind — never claim to have proved absence.

    if (!CoversFrame(kind, frame))
        return nullptr;  // Not watched; the caller must not skip this frame.

    const uint32_t blockIdx = FindBlockForFrame(kindIdx, frame);
    if (blockIdx == UINT32_MAX)
        return nullptr;
    if (!MaterializeBlock(kindIdx, blockIdx))
        return nullptr;  // Decompression failed — fall back to replaying.

    const uint64_t base = (blockIdx < _blocks[kindIdx].size())
                              ? _blocks[kindIdx][blockIdx].baseFrame
                              : _openBlockBaseFrame[kindIdx];
    DecodeFrameFromCache(static_cast<uint32_t>(frame - base), _decodeScratch);
    return &_decodeScratch;
}

bool TTDCoverageIndex::FrameMayContain(TTDCoverageKind kind, uint64_t frame,
                                       uint16_t offsetLow, uint16_t offsetHigh,
                                       bool hasPage, PhysPage page) const
{
    const std::vector<TTDCoverageKey>* keys = DecodeWatchedFrame(kind, frame);
    if (!keys)
        return true;

    for (TTDCoverageKey key : *keys)
    {
        const uint16_t offset = static_cast<uint16_t>(key & 0x3FFF);
        if (offset < offsetLow || offset > offsetHigh)
            continue;
        if (hasPage && (key >> 14) != CoveragePageField(page))
            continue;
        return true;
    }

    return false;  // Watched, decoded, nothing in range.
}

bool TTDCoverageIndex::FrameMayContain(TTDCoverageKind kind, uint64_t frame, const TTDCoverageOffsetSet& set) const
{
    const std::vector<TTDCoverageKey>* keys = DecodeWatchedFrame(kind, frame);
    if (!keys)
        return true;

    for (TTDCoverageKey key : *keys)
    {
        if (set.hasPage && (key >> 14) != set.pageField)
            continue;
        if (set.Contains(key & 0x3FFFu))
            return true;
    }
    return false;  // Watched, decoded, no offset of the set
}

void TTDCoverageOffsetSet::AddRange(uint16_t low, uint16_t high)
{
    low &= 0x3FFF;
    high &= 0x3FFF;
    for (uint32_t offset = low; offset <= high;)
    {
        const uint32_t word = offset >> 6;
        const uint32_t first = offset & 63;
        const uint32_t last = std::min<uint32_t>(63, first + (high - offset));
        const uint64_t mask = (last == 63 ? ~uint64_t{0} : ((uint64_t{1} << (last + 1)) - 1)) & ~((uint64_t{1} << first) - 1);
        bits[word] |= mask;
        offset += last - first + 1;
    }
}

bool TTDCoverageOffsetSet::Empty() const
{
    for (const uint64_t w : bits)
        if (w != 0)
            return false;
    return true;
}

void TTDCoverageIndex::EncodePending(size_t kindIdx, uint64_t frame)
{
    std::vector<TTDCoverageKey>& pending = _pending[kindIdx];

    // An empty frame still gets a slot — a zero count. Skipping it entirely
    // would break the "block covers [baseFrame, baseFrame+frameCount)" mapping
    // that replaces the per-frame table.

    // Reset the repeat filter for the next frame. Without this a key touched in
    // frame N would be skipped in frame N+1 and the set would be incomplete —
    // a false negative, the one failure mode that actually loses answers.
    std::fill(std::begin(_recent[kindIdx]), std::end(_recent[kindIdx]), TTDCoverageKey{0});

    // The delta encoding below needs the keys sorted (the bitmap already deduplicated them). The membership bits
    // are released for exactly the words this frame touched, so the next frame starts clean without touching the
    // rest of the bitmap
    std::vector<uint64_t>& seen = _seen[kindIdx];
    std::vector<uint64_t>& words = _seenWords[kindIdx];
    if (pending.size() < kSortedWalkMinKeys)
    {
        std::sort(pending.begin(), pending.end());
        for (TTDCoverageKey key : pending)
        {
            seen[key >> 6] = 0;   // every key of the word is in the list
            words[key >> 12] = 0;
        }
    }
    else
    {
        // The bitmap in address order is the set sorted: the summary names the words holding a bit
        size_t at = 0;
        for (size_t w = 0; w < kSeenSummaryWords; ++w)
        {
            for (uint64_t summary = words[w]; summary != 0; summary &= summary - 1)
            {
                const size_t word = (w << 6) + static_cast<size_t>(std::countr_zero(summary));
                for (uint64_t bits = seen[word]; bits != 0; bits &= bits - 1)
                    pending[at++] = static_cast<TTDCoverageKey>((word << 6) + static_cast<size_t>(std::countr_zero(bits)));
                seen[word] = 0;
            }
            words[w] = 0;
        }
        assert(at == pending.size());
    }

    // The block summary: the keys new to the block with this frame, the frame's count
    if (_openBlockFrames[kindIdx] == 0)
    {
        ResetOpenSummary(kindIdx);
        _blockSummaryValid[kindIdx] = true;
    }
    if (_blockSummaryValid[kindIdx])
    {
        // The keys are sorted: one leaf lookup per 4 KB the frame reached
        SparseBits& blockSeen = _blockSeen[kindIdx];
        std::vector<uint32_t>& blockUnion = _blockUnion[kindIdx];
        const uint32_t inBlock = static_cast<uint32_t>(_blockCounts[kindIdx].size());
        uint32_t piece = UINT32_MAX;
        uint64_t* leaf = nullptr;
        for (const TTDCoverageKey key : pending)
        {
            const TTDCoverageKey masked = key & (kKeySpace - 1);
            if ((masked >> 12) != piece)
            {
                piece = masked >> 12;
                leaf = blockSeen.Leaf(piece, true);
            }
            uint64_t& word = leaf[(masked >> 6) & 63];
            const uint64_t bit = uint64_t{1} << (masked & 63);
            if (word & bit)
                continue;
            word |= bit;
            blockUnion.push_back(masked << 8 | inBlock);
        }
        _blockCounts[kindIdx].push_back(static_cast<uint32_t>(pending.size()));
        if (kindIdx == static_cast<size_t>(TTDCoverageKind::Written))
            AppendFrameRows(_blockRows, pending.data(), pending.size());
    }

    std::vector<uint8_t>& pool = _openBlock[kindIdx];

    // A query may have cached a copy of the open block; it is about to grow,
    // and a stale copy would answer "not touched" for the frames appended now
    if (_blockCacheKind == kindIdx && _blockCacheIndex >= _blocks[kindIdx].size())
    {
        _blockCacheKind = SIZE_MAX;
        _blockCacheIndex = UINT32_MAX;
        _rowsCacheIndex = UINT32_MAX;
    }

    if (_openBlockFrames[kindIdx] == 0)
        _openBlockBaseFrame[kindIdx] = frame;

    // Frame layout inside the block: key count, then sorted delta varints.
    AppendVarint(pool, static_cast<uint32_t>(pending.size()));
    if (_blockSummaryValid[kindIdx])
        _blockOffsets[kindIdx].push_back(static_cast<uint32_t>(pool.size()));

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
        ResetOpenSummary(kindIdx);
        return;
    }

    Block block;
    block.rawSize = static_cast<uint32_t>(raw.size());
    block.baseFrame = _openBlockBaseFrame[kindIdx];
    block.frameCount = _openBlockFrames[kindIdx];
    block.compressed = codec::Compress(raw.data(), raw.size());
    SealSummary(kindIdx, block, raw);

    _rawBytes[kindIdx] += block.rawSize;
    _compressedBytes[kindIdx] += block.compressed.size();

    _blocks[kindIdx].push_back(std::move(block));

    raw.clear();
    _openBlockFrames[kindIdx] = 0;
}

void TTDCoverageIndex::ResetOpenSummary(size_t kindIdx)
{
    _blockSeen[kindIdx].Reset();
    _blockUnion[kindIdx].clear();
    _blockCounts[kindIdx].clear();
    _blockOffsets[kindIdx].clear();
    if (kindIdx == static_cast<size_t>(TTDCoverageKind::Written))
        _blockRows.clear();
    _blockSummaryValid[kindIdx] = false;
}

void TTDCoverageIndex::AppendFrameRows(std::vector<uint8_t>& out, const TTDCoverageKey* keys, size_t count)
{
    // The count leads the frame: the rows are written after a placeholder of the widest count, then moved up
    const size_t at = out.size();
    uint32_t rows = 0;
    out.resize(at + 5);
    uint32_t prev = 0;
    for (size_t i = 0, prevRow = SIZE_MAX; i < count; ++i)
        if (const size_t row = (keys[i] & (kKeySpace - 1)) >> kRowBits; row != prevRow)
        {
            AppendVarint(out, static_cast<uint32_t>(row) - prev);
            prev = static_cast<uint32_t>(row);
            prevRow = row;
            ++rows;
        }
    uint8_t head[5];
    size_t headSize = 0;
    for (uint32_t v = rows; ; v >>= 7)
    {
        head[headSize++] = static_cast<uint8_t>((v & 0x7F) | (v > 0x7F ? 0x80 : 0));
        if (v <= 0x7F)
            break;
    }
    std::memcpy(out.data() + at, head, headSize);
    std::memmove(out.data() + at + headSize, out.data() + at + 5, out.size() - at - 5);
    out.resize(out.size() - (5 - headSize));
}

int TTDCoverageIndex::FrameWroteIn(uint64_t frame, TTDCoverageKey lo, TTDCoverageKey hi) const
{
    constexpr size_t kindIdx = static_cast<size_t>(TTDCoverageKind::Written);
    if (lo >= hi)
        return 0;
    const uint32_t b = FindBlockForFrame(kindIdx, frame);
    if (b >= _blocks[kindIdx].size() || !SummarizeBlock(kindIdx, b))
        return -1;
    const Block& block = _blocks[kindIdx][b];
    if (block.frameRows.empty() && block.frameRowsRawSize != 0)
        return -1;
    if (_rowsCacheIndex != b)
    {
        _rowsCacheIndex = UINT32_MAX;
        _rowsCache.resize(block.frameRowsRawSize);
        if (block.frameRowsRawSize &&
            !codec::Decompress(block.frameRows, block.frameRowsRawSize, _rowsCache.data()))
            return -1;
        _rowsOffsets.clear();
        size_t pos = 0;
        for (uint32_t f = 0; f < block.frameCount && pos < _rowsCache.size(); ++f)
        {
            _rowsOffsets.push_back(static_cast<uint32_t>(pos));
            const uint32_t rows = ReadVarint(_rowsCache.data(), pos);
            for (uint32_t i = 0; i < rows; ++i)
                ReadVarint(_rowsCache.data(), pos);
        }
        if (_rowsOffsets.size() != block.frameCount)
            return -1;
        _rowsCacheIndex = b;
    }
    // Rows the range holds whole answer yes; a row it cuts may hold only keys outside it
    const uint32_t firstRow = lo >> kRowBits;
    const uint32_t lastRow = (hi - 1) >> kRowBits;
    size_t pos = _rowsOffsets[static_cast<size_t>(frame - block.baseFrame)];
    const uint32_t rows = ReadVarint(_rowsCache.data(), pos);
    uint32_t row = 0;
    int answer = 0;
    for (uint32_t i = 0; i < rows; ++i)
    {
        row += ReadVarint(_rowsCache.data(), pos);
        if (row < firstRow)
            continue;
        if (row > lastRow)
            break;
        const TTDCoverageKey rowLo = row << kRowBits;
        const TTDCoverageKey rowHi = rowLo + (TTDCoverageKey{1} << kRowBits);
        if (rowLo >= lo && rowHi <= hi)
            return 1;
        answer = -1;
    }
    return answer;
}

namespace
{
/// Sort packed (key << 8 | frame) values below 2^31: three passes of 11 bits. std::sort of a block's few thousand
/// keys was most of a seal's summary
void RadixSort31(std::vector<uint32_t>& values, std::vector<uint32_t>& scratch)
{
    constexpr uint32_t kBits = 11;
    constexpr uint32_t kBuckets = 1u << kBits;
    if (values.size() < 64)
    {
        std::sort(values.begin(), values.end());
        return;
    }
    scratch.resize(values.size());
    uint32_t counts[kBuckets];
    for (uint32_t shift = 0; shift < 33; shift += kBits)
    {
        std::fill(std::begin(counts), std::end(counts), 0u);
        for (const uint32_t v : values)
            ++counts[(v >> shift) & (kBuckets - 1)];
        uint32_t sum = 0;
        for (uint32_t& c : counts)
        {
            const uint32_t n = c;
            c = sum;
            sum += n;
        }
        for (const uint32_t v : values)
            scratch[counts[(v >> shift) & (kBuckets - 1)]++] = v;
        values.swap(scratch);
    }
}
}  // namespace

void TTDCoverageIndex::SealSummary(size_t kindIdx, Block& block, const std::vector<uint8_t>& raw)
{
    std::vector<uint32_t>& firsts = _blockUnion[kindIdx];
    const std::vector<uint32_t>& counts = _blockCounts[kindIdx];
    const std::vector<uint32_t>& offsets = _blockOffsets[kindIdx];
    if (!_blockSummaryValid[kindIdx] || counts.size() != block.frameCount || offsets.size() != block.frameCount)
    {
        ResetOpenSummary(kindIdx);   // a cut inside the block: the first query builds it
        return;
    }
    // Each key's last frame: the frames walked back, a key's bit cleared where it is met first
    SparseBits& blockSeen = _blockSeen[kindIdx];
    _sealLast.clear();
    for (uint32_t f = block.frameCount; f-- > 0;)
    {
        size_t pos = offsets[f];
        TTDCoverageKey key = 0;
        uint32_t piece = UINT32_MAX;
        uint64_t* leaf = nullptr;
        for (uint32_t i = 0; i < counts[f]; ++i)
        {
            key += ReadVarint(raw.data(), pos);
            const TTDCoverageKey masked = key & (kKeySpace - 1);
            if ((masked >> 12) != piece)
            {
                piece = masked >> 12;
                leaf = blockSeen.Leaf(piece, false);
            }
            if (!leaf)
                continue;
            uint64_t& word = leaf[(masked >> 6) & 63];
            const uint64_t bit = uint64_t{1} << (masked & 63);
            if (word & bit)
            {
                word &= ~bit;
                _sealLast.push_back(masked << 8 | f);
            }
        }
    }
    RadixSort31(firsts, _sealSort);
    RadixSort31(_sealLast, _sealSort);
    if (_sealLast.size() != firsts.size())
    {
        ResetOpenSummary(kindIdx);
        return;
    }
    BuildSummary(block, firsts, _sealLast, _sealRaw);
    block.frameKeyCounts = counts;
    if (kindIdx == static_cast<size_t>(TTDCoverageKind::Written))
    {
        block.frameRows = codec::Compress(_blockRows.data(), _blockRows.size());
        block.frameRowsRawSize = static_cast<uint32_t>(_blockRows.size());
    }
    block.summarized = true;
    ResetOpenSummary(kindIdx);
}

void TTDCoverageIndex::BuildSummary(const Block& block, const std::vector<uint32_t>& firsts,
                                    const std::vector<uint32_t>& lasts, std::vector<uint8_t>& scratch)
{
    assert(firsts.size() == lasts.size());
    scratch.clear();
    block.pieceMask.assign(kSummaryPieceWords, 0);
    std::vector<uint8_t> spans;
    spans.reserve(firsts.size() * 2);
    TTDCoverageKey prev = 0;
    for (size_t i = 0; i < firsts.size(); ++i)
    {
        const TTDCoverageKey key = firsts[i] >> 8;
        assert((lasts[i] >> 8) == key);
        const size_t piece = key >> kSummaryPieceBits;
        block.pieceMask[piece >> 6] |= uint64_t{1} << (piece & 63);
        AppendVarint(scratch, key - prev);
        prev = key;
        spans.push_back(static_cast<uint8_t>(firsts[i]));
        spans.push_back(static_cast<uint8_t>(lasts[i]));
    }
    block.unionKeys = codec::Compress(scratch.data(), scratch.size());
    block.unionKeys.shrink_to_fit();
    block.unionRawSize = static_cast<uint32_t>(scratch.size());
    block.unionSpans = codec::Compress(spans.data(), spans.size());
    block.unionSpans.shrink_to_fit();
    block.unionCount = static_cast<uint32_t>(firsts.size());
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
            _rowsCacheIndex = UINT32_MAX;
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
    // Blocks are sealed (and loaded) in frame order: the last block starting at or before the frame
    const std::vector<Block>& blocks = _blocks[kindIdx];
    auto after = std::upper_bound(blocks.begin(), blocks.end(), frame,
                                  [](uint64_t f, const Block& b) { return f < b.baseFrame; });
    if (after != blocks.begin())
    {
        const Block& b = *std::prev(after);
        if (frame < b.baseFrame + b.frameCount)
            return static_cast<uint32_t>(std::prev(after) - blocks.begin());
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
    _rowsCacheIndex = UINT32_MAX;
    _blockFrameOffsets.clear();
    _blockFrameCounts.clear();
    _unionSeen = {};
    _unionScratch = {};
    _unionRaw = {};
    _sealRaw = {};
    _sealLast = {};
    _sealSort = {};
    _unionLast = {};
    _spansRaw = {};
    _blockRows = {};
    _rowsCache = {};
    _rowsOffsets = {};

    for (size_t k = 0; k < kKindCount; ++k)
    {
        _pending[k].clear();
        _seen[k].assign(kSeenWords, 0);
        _seenWords[k].assign(kSeenSummaryWords, 0);
        std::fill(std::begin(_recent[k]), std::end(_recent[k]), TTDCoverageKey{0});
        _blocks[k].clear();
        _openBlock[k].clear();
        ResetOpenSummary(k);
        _blockSeen[k] = {};
        _blockUnion[k] = {};
        _openBlockFrames[k] = 0;
        _openBlockBaseFrame[k] = 0;
        _compressedBytes[k] = 0;
        _rawBytes[k] = 0;
        _hasCoverage[k] = false;
        _firstCoveredFrame[k] = 0;
        _lastCoveredFrame[k] = 0;
        _holes[k].clear();

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
        // A hole holds no data: the index cannot say it did not touch the key
        if (IsHole(kindIdx, frame))
            return false;
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

bool TTDCoverageIndex::GetFrameKeys(TTDCoverageKind kind, uint64_t frame,
                                    std::vector<TTDCoverageKey>& outKeys) const
{
    outKeys.clear();
    const size_t kindIdx = static_cast<size_t>(kind);
    if (kindIdx >= kKindCount || !CoversFrame(kind, frame))
        return false;

    const uint32_t blockIdx = FindBlockForFrame(kindIdx, frame);
    if (blockIdx == UINT32_MAX || !MaterializeBlock(kindIdx, blockIdx))
        return false;

    const uint64_t base = (blockIdx < _blocks[kindIdx].size())
                              ? _blocks[kindIdx][blockIdx].baseFrame
                              : _openBlockBaseFrame[kindIdx];
    DecodeFrameFromCache(static_cast<uint32_t>(frame - base), outKeys);
    return true;
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

uint32_t TTDCoverageIndex::FirstBlockReaching(size_t kindIdx, uint64_t first) const
{
    const std::vector<Block>& blocks = _blocks[kindIdx];
    const auto at = std::partition_point(blocks.begin(), blocks.end(),
                                         [&](const Block& b) { return b.baseFrame + b.frameCount <= first; });
    return static_cast<uint32_t>(at - blocks.begin());
}

bool TTDCoverageIndex::SummarizeBlock(size_t kindIdx, uint32_t blockIdx) const
{
    const Block& block = _blocks[kindIdx][blockIdx];
    if (block.summarized)
        return true;
    if (!MaterializeBlock(kindIdx, blockIdx))
        return false;
    if (_unionSeen.empty())
        _unionSeen.assign(kSeenWords, 0);
    block.frameKeyCounts.assign(_blockFrameCounts.begin(), _blockFrameCounts.end());
    _unionScratch.clear();
    const bool rows = kindIdx == static_cast<size_t>(TTDCoverageKind::Written);
    std::vector<uint8_t> frameRows;
    std::vector<TTDCoverageKey> frameKeys;
    for (uint32_t f = 0; f < _blockFrameOffsets.size(); ++f)
    {
        size_t pos = _blockFrameOffsets[f];
        TTDCoverageKey key = 0;
        if (rows)
        {
            frameKeys.clear();
            size_t at = pos;
            TTDCoverageKey k = 0;
            for (uint32_t i = 0; i < _blockFrameCounts[f]; ++i)
                frameKeys.push_back(k += ReadVarint(_blockCache.data(), at));
            AppendFrameRows(frameRows, frameKeys.data(), frameKeys.size());
        }
        for (uint32_t i = 0; i < _blockFrameCounts[f]; ++i)
        {
            key += ReadVarint(_blockCache.data(), pos);
            const TTDCoverageKey masked = key & (kKeySpace - 1);
            uint64_t& word = _unionSeen[masked >> 6];
            const uint64_t bit = uint64_t{1} << (masked & 63);
            if (word & bit)
                continue;
            word |= bit;
            _unionScratch.push_back(masked << 8 | f);
        }
    }
    // Each key's last frame: the frames walked back, a key's bit cleared where it is met first (all clear after)
    _unionLast.clear();
    for (uint32_t f = static_cast<uint32_t>(_blockFrameOffsets.size()); f-- > 0;)
    {
        size_t pos = _blockFrameOffsets[f];
        TTDCoverageKey key = 0;
        for (uint32_t i = 0; i < _blockFrameCounts[f]; ++i)
        {
            key += ReadVarint(_blockCache.data(), pos);
            const TTDCoverageKey masked = key & (kKeySpace - 1);
            uint64_t& word = _unionSeen[masked >> 6];
            const uint64_t bit = uint64_t{1} << (masked & 63);
            if (word & bit)
            {
                word &= ~bit;
                _unionLast.push_back(masked << 8 | f);
            }
        }
    }
    std::vector<uint32_t> sortScratch;
    RadixSort31(_unionScratch, sortScratch);
    RadixSort31(_unionLast, sortScratch);
    BuildSummary(block, _unionScratch, _unionLast, _unionRaw);
    if (rows)
    {
        block.frameRows = codec::Compress(frameRows.data(), frameRows.size());
        block.frameRows.shrink_to_fit();
        block.frameRowsRawSize = static_cast<uint32_t>(frameRows.size());
    }
    block.summarized = true;
    return true;
}

uint32_t TTDCoverageIndex::CountDistinctKeys(TTDCoverageKind kind, uint64_t first, uint64_t last,
                                             std::vector<uint32_t>& stamp, uint32_t mark) const
{
    const size_t kindIdx = static_cast<size_t>(kind);
    if (kindIdx >= kKindCount || !_hasCoverage[kindIdx] || first > last)
        return 0;
    uint32_t distinct = 0;
    auto count = [&](TTDCoverageKey key) {
        if (key < stamp.size() && stamp[key] != mark)
        {
            stamp[key] = mark;
            ++distinct;
        }
    };
    // Frames one by one: a block the range cuts, the open block, a block that would not summarise
    auto frames = [&](uint32_t blockIdx, uint64_t base, uint32_t frameCount) {
        const uint64_t from = std::max(first, base);
        const uint64_t to = std::min(last, base + frameCount - 1);
        if (from > to || !MaterializeBlock(kindIdx, blockIdx))
            return;
        for (uint64_t frame = from; frame <= to; ++frame)
        {
            const uint32_t f = static_cast<uint32_t>(frame - base);
            if (f >= _blockFrameOffsets.size())
                break;
            size_t pos = _blockFrameOffsets[f];
            TTDCoverageKey key = 0;
            for (uint32_t i = 0; i < _blockFrameCounts[f]; ++i)
            {
                key += ReadVarint(_blockCache.data(), pos);
                count(key);
            }
        }
    };
    const std::vector<Block>& blocks = _blocks[kindIdx];
    for (uint32_t b = FirstBlockReaching(kindIdx, first); b < blocks.size() && blocks[b].baseFrame <= last; ++b)
    {
        const Block& block = blocks[b];
        if (block.frameCount == 0)
            continue;
        const bool holdsStart = block.baseFrame >= first;
        const bool holdsEnd = block.baseFrame + block.frameCount - 1 <= last;
        if ((!holdsStart && !holdsEnd) || !SummarizeBlock(kindIdx, b))
        {
            frames(b, block.baseFrame, block.frameCount);
            continue;
        }
        if (!holdsStart || !holdsEnd)
        {
            // Cut at one side: the union's keys first seen up to the range's end, or last seen from its start
            _spansRaw.resize(size_t{block.unionCount} * 2);
            _unionRaw.resize(block.unionRawSize);
            if ((block.unionRawSize && !codec::Decompress(block.unionKeys, block.unionRawSize, _unionRaw.data())) ||
                (block.unionCount && !codec::Decompress(block.unionSpans, _spansRaw.size(), _spansRaw.data())))
            {
                frames(b, block.baseFrame, block.frameCount);
                continue;
            }
            const uint32_t edge = static_cast<uint32_t>((holdsStart ? last : first) - block.baseFrame);
            size_t pos = 0;
            TTDCoverageKey key = 0;
            for (uint32_t i = 0; i < block.unionCount && pos < _unionRaw.size(); ++i)
            {
                key += ReadVarint(_unionRaw.data(), pos);
                if (holdsStart ? _spansRaw[2 * i] <= edge : _spansRaw[2 * i + 1] >= edge)
                    count(key);
            }
            continue;
        }
        _unionRaw.resize(block.unionRawSize);
        if (block.unionRawSize && !codec::Decompress(block.unionKeys, block.unionRawSize, _unionRaw.data()))
        {
            frames(b, block.baseFrame, block.frameCount);
            continue;
        }
        size_t pos = 0;
        TTDCoverageKey key = 0;
        while (pos < _unionRaw.size())
        {
            key += ReadVarint(_unionRaw.data(), pos);
            count(key);
        }
    }
    if (_openBlockFrames[kindIdx] > 0)
        frames(static_cast<uint32_t>(blocks.size()), _openBlockBaseFrame[kindIdx], _openBlockFrames[kindIdx]);
    return distinct;
}

bool TTDCoverageIndex::BlockMayHaveKeyIn(TTDCoverageKind kind, uint64_t frame, TTDCoverageKey lo,
                                         TTDCoverageKey hi) const
{
    const size_t kindIdx = static_cast<size_t>(kind);
    if (kindIdx >= kKindCount || lo >= hi)
        return lo < hi;
    const uint32_t b = FindBlockForFrame(kindIdx, frame);
    if (b >= _blocks[kindIdx].size() || !SummarizeBlock(kindIdx, b))
        return true;
    const std::vector<uint64_t>& mask = _blocks[kindIdx][b].pieceMask;
    const size_t last = std::min<size_t>((static_cast<size_t>(hi) - 1) >> kSummaryPieceBits, kSummaryPieceWords * 64 - 1);
    for (size_t piece = static_cast<size_t>(lo) >> kSummaryPieceBits; piece <= last; ++piece)
        if (mask[piece >> 6] & (uint64_t{1} << (piece & 63)))
            return true;
    return false;
}

void TTDCoverageIndex::FrameKeyCounts(TTDCoverageKind kind, uint64_t first, uint64_t last, uint32_t* out) const
{
    const size_t kindIdx = static_cast<size_t>(kind);
    if (kindIdx >= kKindCount || !_hasCoverage[kindIdx] || first > last)
        return;
    auto copy = [&](uint64_t base, const uint32_t* counts, size_t n) {
        const uint64_t from = std::max(first, base);
        const uint64_t to = std::min<uint64_t>(last, base + n - 1);
        for (uint64_t frame = from; n > 0 && frame <= to; ++frame)
            out[frame - first] = counts[frame - base];
    };
    const std::vector<Block>& blocks = _blocks[kindIdx];
    for (uint32_t b = FirstBlockReaching(kindIdx, first); b < blocks.size() && blocks[b].baseFrame <= last; ++b)
        if (SummarizeBlock(kindIdx, b))
            copy(blocks[b].baseFrame, blocks[b].frameKeyCounts.data(), blocks[b].frameKeyCounts.size());
    if (_openBlockFrames[kindIdx] > 0 && MaterializeBlock(kindIdx, static_cast<uint32_t>(blocks.size())))
        copy(_openBlockBaseFrame[kindIdx], _blockFrameCounts.data(), _blockFrameCounts.size());
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

size_t TTDCoverageIndex::CompressedSlackBytes() const
{
    size_t total = 0;
    for (size_t k = 0; k < kKindCount; ++k)
        for (const Block& b : _blocks[k])
            total += b.compressed.capacity() - b.compressed.size();
    return total;
}

size_t TTDCoverageIndex::HeapBytes() const
{
    size_t total = 0;
    for (size_t k = 0; k < kKindCount; ++k)
    {
        total += _pending[k].capacity() * sizeof(TTDCoverageKey);
        total += _seen[k].capacity() * sizeof(uint64_t);
        total += _seenWords[k].capacity() * sizeof(uint64_t);
        total += _openBlock[k].capacity();
        total += _blockSeen[k].HeapBytes() +
                 (_blockUnion[k].capacity() + _blockCounts[k].capacity() + _blockOffsets[k].capacity()) * sizeof(uint32_t);
        for (const Block& b : _blocks[k])
            total += b.compressed.capacity() + sizeof(Block) + b.unionKeys.capacity() +
                     b.frameKeyCounts.capacity() * sizeof(uint32_t) + b.pieceMask.capacity() * sizeof(uint64_t) +
                     b.frameRows.capacity() + b.unionSpans.capacity();
    }
    total += _decodeScratch.capacity() * sizeof(TTDCoverageKey);
    total += _unionSeen.capacity() * sizeof(uint64_t) + _unionScratch.capacity() * sizeof(TTDCoverageKey) +
             _unionRaw.capacity() + _sealRaw.capacity() + _blockRows.capacity() + _spansRaw.capacity() +
             (_unionLast.capacity() + _sealLast.capacity() + _sealSort.capacity()) * sizeof(uint32_t) + _rowsCache.capacity() +
             _rowsOffsets.capacity() * sizeof(uint32_t);
    total += _blockCache.capacity();
    return total;
}



namespace
{

/// Section marker, so a truncated or misplaced read fails loudly instead of
/// producing a plausible-looking empty index.
constexpr uint32_t kCoverageSectionMagic = 0x56435654;  // 'TVCV'
/// Version 2: the "no RAM page" bucket moved from page field 0xFF (which
/// collided with RAM page 255) to kCoverageNoPageField. Version 1 sections are
/// parsed so the stream stays aligned, then dropped - their page-255 keys are
/// ambiguous, and reverse queries fall back to replay instead.
constexpr uint16_t kCoverageSectionVersion = 2;
constexpr uint16_t kCoverageSectionVersionV1 = 1;

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

uint64_t TTDCoverageIndex::RepeatFrom() const
{
    uint64_t from = UINT64_MAX;
    for (size_t k = 0; k < kKindCount; ++k)
    {
        if (_openBlockFrames[k] > 0 && !_openBlock[k].empty())
            from = std::min(from, _openBlockBaseFrame[k]);
        else if (!_blocks[k].empty())
            from = std::min<uint64_t>(from, _blocks[k].back().baseFrame + _blocks[k].back().frameCount);
    }
    return from == UINT64_MAX ? 0 : from;
}

void TTDCoverageIndex::AppendLater(TTDCoverageIndex&& later, uint64_t fromFrame)
{
    _blockCache.clear();
    _blockCacheKind = SIZE_MAX;
    _blockCacheIndex = UINT32_MAX;
    _rowsCacheIndex = UINT32_MAX;
    for (size_t k = 0; k < kKindCount; ++k)
    {
        std::vector<Block>& mine = _blocks[k];
        std::vector<Block>& theirs = later._blocks[k];
        const uint64_t cut = theirs.empty() ? fromFrame : std::min(fromFrame, theirs.front().baseFrame);
        while (!mine.empty() && mine.back().baseFrame + mine.back().frameCount > cut)
            mine.pop_back();
        for (Block& b : theirs)
            mine.push_back(std::move(b));
        theirs.clear();
    }
    RebuildFromBlocks();
}

void TTDCoverageIndex::RebuildFromBlocks()
{
    for (size_t k = 0; k < kKindCount; ++k)
    {
        _rawBytes[k] = 0;
        _compressedBytes[k] = 0;
        _hasCoverage[k] = false;
        _firstCoveredFrame[k] = 0;
        _lastCoveredFrame[k] = 0;
        _holes[k].clear();
        const std::vector<Block>& blocks = _blocks[k];
        for (size_t i = 0; i < blocks.size(); ++i)
        {
            const Block& b = blocks[i];
            _rawBytes[k] += b.rawSize;
            _compressedBytes[k] += b.compressed.size();
            // The covered range from the blocks themselves rather than stored: a stored range could disagree with
            // the blocks and would then authorise pruning frames the index does not hold
            const uint64_t lastFrame = b.baseFrame + (b.frameCount ? b.frameCount - 1 : 0);
            if (!_hasCoverage[k])
            {
                _hasCoverage[k] = true;
                _firstCoveredFrame[k] = b.baseFrame;
                _lastCoveredFrame[k] = lastFrame;
            }
            else
            {
                _firstCoveredFrame[k] = std::min(_firstCoveredFrame[k], b.baseFrame);
                _lastCoveredFrame[k] = std::max(_lastCoveredFrame[k], lastFrame);
            }
            // Holes (DropFramesFrom, a dropped block of a join) are the gaps between consecutive blocks
            if (i > 0)
                for (uint64_t f = blocks[i - 1].baseFrame + blocks[i - 1].frameCount; f < b.baseFrame; ++f)
                    _holes[k].push_back(f);
        }
    }
}

bool TTDCoverageIndex::Serialize(std::ostream& out, uint64_t fromFrame) const
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

        // A part: the blocks that reach fromFrame (in frame order, so the first of them is found by its end)
        const auto firstBlock = std::partition_point(_blocks[k].begin(), _blocks[k].end(), [fromFrame](const Block& b) {
            return b.baseFrame + b.frameCount <= fromFrame;
        });
        const uint32_t blockCount =
            static_cast<uint32_t>(_blocks[k].end() - firstBlock) + (hasPending ? 1u : 0u);
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

        for (auto it = firstBlock; it != _blocks[k].end(); ++it)
            if (!writeBlock(*it)) return false;

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
    if (!ReadPod(in, version) ||
        (version != kCoverageSectionVersion && version != kCoverageSectionVersionV1))
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

            // Sizes come from the file: a block spans at most kFramesPerBlock
            // frames, the payload is read as it arrives, and the raw size must be
            // the one the zstd frame declares - so a corrupt field can neither
            // request gigabytes here nor at the block's first decompression
            if (b.frameCount == 0 || b.frameCount > kFramesPerBlock || b.rawSize < b.frameCount ||
                !codec::ReadExact(in, compSize, b.compressed) ||
                codec::DeclaredContentSize(b.compressed) != b.rawSize)
            {
                Clear();
                return false;
            }

            _blocks[k].push_back(std::move(b));
        }
    }

    if (version == kCoverageSectionVersionV1)
    {
        // Consumed, but not usable: see kCoverageSectionVersion.
        Clear();
        return false;
    }

    RebuildFromBlocks();
    return true;
}

}  // namespace ttd
