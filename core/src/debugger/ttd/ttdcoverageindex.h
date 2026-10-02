#pragma once

/// @file ttdcoverageindex.h
/// @brief Per-frame coverage sets that let reverse search skip frames.
///
/// Reverse search without an index is a backward walk that replays frames until
/// it finds the one it wants. Measured on a real demo, that means 111-215
/// replayed frames per query at 1.3 ms each, and it gets worse the longer the
/// session runs — the cost scales with history length, which is exactly the
/// wrong property for a debugger.
///
/// The fix is to record, per frame, WHICH addresses that frame touched. A query
/// then walks the coverage sets backwards — cheap, no emulation — and only
/// replays the first frame that could possibly match. Cost stops scaling with
/// history and starts scaling with how rare the target is.
///
/// Sizing, measured over 1200 frames of a real demo:
///
///   | set      | distinct/frame | sparse + zstd |
///   |----------|----------------|---------------|
///   | executed | 318            | 124 B         |
///   | written  | 75             | 47 B          |
///   | read     | 749            | 96 B          |
///
/// ~307 B/frame for all three, i.e. 54 MB/hour — less than the compressed write
/// journal. See docs/inprogress/2026-08-20-ttd-reverse-search-index/.
///
/// Why sparse sets rather than bitmaps
/// -----------------------------------
/// A flat bitmap over the Z80 address space compresses to a similar size, but
/// its SCRATCH buffer is the problem: keys here are physical (page, offset)
/// pairs, and on a 4 MB machine that space is 23 bits wide (256 pages plus the
/// "no page" bucket), so a flat bitmap needs 1 MB per frame and has to be
/// cleared every frame — on its own more
/// expensive than the entire 117 µs/frame capture. A sorted sparse set costs the
/// same ~47-124 bytes whether keys are 16 or 23 bits, because its cost follows
/// set cardinality, and cardinality does not depend on installed RAM. A 4 MB
/// clone indexes for the same price as a 128K machine.
///
/// Why keys are physical, not Z80 addresses
/// ----------------------------------------
/// On a banked machine one Z80 address names different bytes depending on what
/// is paged in. An index keyed by Z80 address would answer "frame 900 touched
/// 0xC000" without saying which page, producing candidate frames that cannot
/// match — the same defect the physPage search filter fixes.

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <vector>

#include "ttdphyspage.h"

namespace ttd
{

/// @brief What kind of access a coverage set records.
enum class TTDCoverageKind : uint8_t
{
    Executed = 0,  ///< Instruction fetches (M1). Serves reverse breakpoints.
    Written  = 1,  ///< Memory writes. Serves write watchpoints.
    Read     = 2,  ///< Memory reads. Serves read watchpoints, which have no
                   ///< journal at all and are replay-only without this.
    Count    = 3
};

/// @brief Physical address key: page field and offset within the page.
///
/// Packed as (pageField << 14) | offset. The page field is the RAM page
/// 0..255, or kCoverageNoPageField (256) for accesses with no RAM page - ROM
/// and cache - which all share that one bucket (an over-approximation: extra
/// replays, never a lost answer). 23 bits in total.
///
/// The bucket used to be page field 0xFF, i.e. the same key as RAM page 255
/// of a 4 MB machine, so ROM fetches answered page-255 queries and the other
/// way round. Sections written that way (version 1) are not loaded.
using TTDCoverageKey = uint32_t;

/// Page field of the shared "no RAM page" bucket (outside 0..255)
constexpr uint32_t kCoverageNoPageField = 0x100;

/// @brief The key's page field for a physical page (kPhysPageNone -> bucket).
inline uint32_t CoveragePageField(PhysPage physPage)
{
    return physPage == kPhysPageNone ? kCoverageNoPageField : (static_cast<uint32_t>(physPage) & 0xFFu);
}

/// @brief Build a coverage key from a physical page and Z80 address.
inline TTDCoverageKey MakeCoverageKey(PhysPage physPage, uint16_t z80Address)
{
    return (CoveragePageField(physPage) << 14) | (static_cast<TTDCoverageKey>(z80Address) & 0x3FFFu);
}

/// @brief Per-frame coverage sets for one session.
///
/// Lifecycle mirrors the timeline: Record*() during the frame, SealFrame() at
/// the frame boundary, Clear() when the session is invalidated.
///
/// Threading matches the rest of capture: Record*() and SealFrame() run on the
/// emulator thread; queries run on the control thread while the emulator is
/// paused. No locking, same discipline as the dirty tracker.
class TTDCoverageIndex
{
public:
    /// Frames per compressed block.
    ///
    /// zstd needs a few kilobytes to find anything; compressing a single frame
    /// of 100-500 bytes on its own wastes both ratio and a frame header. A
    /// block also matches how reverse search reads the index — it walks
    /// backwards frame by frame, so one decompression serves the next 64 steps.
    static constexpr uint32_t kFramesPerBlock = 64;

    /// One zstd-compressed run of up to kFramesPerBlock frames.
    ///
    /// Everything per-frame lives INSIDE the payload: each frame contributes a
    /// varint key count followed by its sorted delta-varint keys. Keeping the
    /// counts in the compressed stream rather than in a side table is what
    /// makes the index small — a 24-byte-per-frame table costs more than the
    /// compressed keys it describes (measured: 72 B/frame of table against
    /// 16.6 B/frame of data).
    ///
    /// The frames a block covers are [baseFrame, baseFrame + frameCount), which
    /// holds because SealFrame is called once per frame in order.
    struct Block
    {
        std::vector<uint8_t> compressed;
        uint32_t rawSize = 0;
        uint64_t baseFrame = 0;
        uint32_t frameCount = 0;
    };

    TTDCoverageIndex();

    /// @brief Record a touched key for the frame currently being accumulated.
    ///
    /// Inline on purpose. This runs on the instruction-fetch path — tens of
    /// thousands of calls per frame — and an out-of-line call through two
    /// pointers measured at ~200 us/frame all by itself, dwarfing the work it
    /// was guarding. Inlined, the repeat case is a key computation plus one
    /// bitmap test, and only genuinely new keys touch the vector.
    inline void Record(TTDCoverageKind kind, TTDCoverageKey key)
    {
        const size_t kindIdx = static_cast<size_t>(kind);
        if (kindIdx >= kKindCount)
            return;

        const TTDCoverageKey masked = key & (kKeySpace - 1);

        // L1-resident front filter. The authoritative structure is a 1 MB
        // membership bitmap per kind, and touching it on every instruction
        // fetch costs a cache miss each time — measured at ~175 us/frame, which
        // is more than the whole checkpoint capture. This 16 KB direct-mapped
        // table absorbs the repeats (a frame re-executes the same ~318
        // addresses constantly) and stays in L1.
        //
        // A collision is harmless: two keys sharing a slot merely fall through
        // to the bitmap, which is exact. The filter can produce extra work,
        // never a wrong answer.
        // Stored as key+1 so that 0 can mean "empty". Key 0 is a real address
        // (page 0, offset 0), and letting it share the empty marker made the
        // filter swallow it entirely — a false negative, which is the one
        // failure mode this structure must never have.
        const TTDCoverageKey tag = masked + 1;
        TTDCoverageKey& slot = _recent[kindIdx][masked & (kRecentSlots - 1)];
        if (slot == tag)
            return;
        slot = tag;

        if (TestAndSetSeen(kindIdx, masked))
            return;

        _pending[kindIdx].push_back(masked);
    }

    /// @brief Close the frame under accumulation and start a new one.
    /// Encodes each kind's set and appends it to the per-kind frame table.
    void SealFrame(uint64_t frame);

    /// @brief Drop everything. Called when the session is invalidated.
    void Clear();

    /// @brief Forget frames @p frame and later: a resume from the past
    /// discards that future, and a stale "this frame never touched X" would
    /// make reverse search skip a frame of the new history.
    ///
    /// Frames before @p frame stay indexed. With @p partialFirstFrame the
    /// resume point lies inside @p frame: the recording collects only the
    /// part after it, so that frame is sealed as a hole (not covered; queries
    /// replay it) instead of an incomplete set that would prove false
    /// absences.
    void DropFramesFrom(uint64_t frame, bool partialFirstFrame);

    /// @brief Most recent frame at or before @p beforeFrame whose @p kind set
    ///        contains @p key, or nullopt when no frame does.
    ///
    /// This is the whole point of the structure: it answers without replaying
    /// anything, so the caller replays exactly one candidate frame instead of
    /// scanning backwards through history.
    bool FindLastFrameTouching(TTDCoverageKind kind, TTDCoverageKey key,
                               uint64_t beforeFrame, uint64_t& outFrame) const;

    /// @brief Does the sealed set for @p frame contain @p key?
    bool FrameTouches(TTDCoverageKind kind, uint64_t frame, TTDCoverageKey key) const;

    /// @brief Retrieve all coverage keys recorded for a specific frame.
    /// @return false if the frame was not watched or materialized.
    bool GetFrameKeys(TTDCoverageKind kind, uint64_t frame,
                      std::vector<TTDCoverageKey>& outKeys) const;

    /// @brief Could @p frame contain an access in the given Z80 address range?
    ///
    /// Conservative by construction: returns true whenever it cannot prove
    /// otherwise, so a caller may waste a replay but can never lose a hit.
    ///
    /// @param offsetLow/offsetHigh  Offsets within a 16 KB page (addr & 0x3FFF).
    ///                              Callers that cannot express their range as a
    ///                              single non-wrapping offset interval must not
    ///                              use this.
    /// @param hasPage  When false, any physical page matches.
    /// @param page     RAM page 0..255, or kPhysPageNone for the ROM/no-page bucket.
    bool FrameMayContain(TTDCoverageKind kind, uint64_t frame,
                         uint16_t offsetLow, uint16_t offsetHigh,
                         bool hasPage, PhysPage page) const;

    /// @brief Was @p frame within the range this index observed?
    ///
    /// Pruning is only sound for frames the index actually watched. A session
    /// deserialized from disk carries a timeline but no coverage, and skipping
    /// its frames would silently drop results.
    bool CoversFrame(TTDCoverageKind kind, uint64_t frame) const;

    /// @brief Inclusive frame range this index observed for @p kind.
    /// @return false when nothing was ever sealed for that kind.
    bool CoveredRange(TTDCoverageKind kind, uint64_t& outFirst, uint64_t& outLast) const;

    /// @brief Number of sealed frames for a kind.
    size_t SealedFrameCount(TTDCoverageKind kind) const;

    /// @brief Total heap bytes held, for the session memory report.
    size_t HeapBytes() const;
    /// Part of HeapBytes() allocated but not holding data: unused capacity of
    /// the compressed blocks
    size_t CompressedSlackBytes() const;

    /// @brief Compressed bytes actually held for a kind (sealed blocks plus the
    /// block still being filled). This is the number that matters for sizing a
    /// session and for what persistence would have to write.
    size_t EncodedBytes(TTDCoverageKind kind) const;

    /// @brief Uncompressed size of the same data, for measuring the ratio.
    size_t RawEncodedBytes(TTDCoverageKind kind) const;

    /// @brief Flush the in-progress block so EncodedBytes reflects everything.
    /// Called at StopRecording; also needed before serializing.
    void FlushOpenBlocks();

    /// @brief Write the index to a stream.
    ///
    /// Blocks are already zstd-compressed in memory, so sealed ones are copied
    /// out verbatim — the on-disk shape is the in-memory shape. The block still
    /// accumulating is compressed into a temporary and written too, so the tail
    /// of a session is not lost; the index itself is left untouched, which is
    /// what lets this run from a const SerializeSession.
    /// @return false on stream failure.
    bool Serialize(std::ostream& out) const;

    /// @brief Read an index previously written by Serialize().
    /// Replaces any current contents. Returns false on stream failure or a
    /// malformed section, leaving the index empty rather than half-loaded —
    /// a partially-loaded index would prune frames it never observed.
    bool Deserialize(std::istream& in);

private:
    static constexpr size_t kKindCount = static_cast<size_t>(TTDCoverageKind::Count);
    static constexpr size_t kKeySpaceBits = 23;               ///< (256 pages + no-page bucket) x 16 KB
    static constexpr size_t kKeySpace = size_t{1} << kKeySpaceBits;
    static constexpr size_t kSeenWords = kKeySpace / 64;

    /// Direct-mapped repeat filter, sized to stay in L1 (4096 x 4 B = 16 KB).
    static constexpr size_t kRecentSlots = 4096;
    TTDCoverageKey _recent[kKindCount][kRecentSlots] = {};

    /// Distinct keys touched so far in the frame under accumulation, one
    /// bucket per kind.
    std::vector<TTDCoverageKey> _pending[kKindCount];

    /// Membership bitmap over the whole 23-bit key space, one per kind, used to
    /// drop repeat accesses before they reach _pending.
    ///
    /// This is not an optimisation, it is what makes the hot path viable. An
    /// instruction fetch happens tens of thousands of times per frame across
    /// only ~318 distinct addresses, so appending unconditionally and
    /// deduplicating at seal time cost +186 us/frame — almost doubling TTD's
    /// overhead. One load-and-test here drops the repeats at the source.
    ///
    /// It is allocated once (1 MB per kind) and never bulk-cleared: SealFrame
    /// clears exactly the bits listed in _pending, so per-frame work stays
    /// proportional to distinct keys rather than to the size of the address
    /// space. That is also why a 4 MB machine costs the same as a 128K one.
    std::vector<uint64_t> _seen[kKindCount];

    inline bool TestAndSetSeen(size_t kindIdx, TTDCoverageKey key)
    {
        const size_t word = key >> 6;
        const uint64_t mask = uint64_t{1} << (key & 63);
        uint64_t& slot = _seen[kindIdx][word];
        if ((slot & mask) != 0)
            return true;
        slot |= mask;
        return false;
    }

    /// Inclusive range of frames this index observed, per kind. Sealing a frame
    /// extends it even when the frame touched nothing, so "no entry" can be
    /// read as "touched nothing" rather than "not watched".
    uint64_t _firstCoveredFrame[kKindCount] = {};
    uint64_t _lastCoveredFrame[kKindCount] = {};
    bool     _hasCoverage[kKindCount] = {};

    /// Frames inside the covered range that hold no data (the partial frame of
    /// a mid-frame resume, DropFramesFrom). Ascending; rebuilt on load from the
    /// gaps between blocks, so the file format needs no field for them.
    std::vector<uint64_t> _holes[kKindCount];

    /// The next SealFrame for this frame records a hole instead of a set
    uint64_t _holeToSeal = UINT64_MAX;

    bool IsHole(size_t kindIdx, uint64_t frame) const;

    /// Byte length of the first @p frames frames of a raw block
    static size_t RawPrefixBytes(const std::vector<uint8_t>& raw, uint32_t frames);

    /// Forget the frame under accumulation (its keys and repeat filters)
    void DiscardPending(size_t kindIdx);

    /// Compressed blocks, in ascending frame order.
    std::vector<Block> _blocks[kKindCount];

    /// Bytes of the block currently being accumulated, before compression.
    std::vector<uint8_t> _openBlock[kKindCount];
    uint32_t             _openBlockFrames[kKindCount] = {};
    uint64_t             _openBlockBaseFrame[kKindCount] = {};

    /// Running totals so sizing questions do not require walking every block.
    size_t _compressedBytes[kKindCount] = {};
    size_t _rawBytes[kKindCount] = {};

    /// Decode scratch reused across queries (control thread only).
    mutable std::vector<TTDCoverageKey> _decodeScratch;

    /// Last block decompressed by a query, kept because reverse search walks
    /// consecutive frames and would otherwise decompress the same block once
    /// per frame.
    mutable std::vector<uint8_t> _blockCache;
    mutable size_t   _blockCacheKind = SIZE_MAX;
    mutable uint32_t _blockCacheIndex = UINT32_MAX;

    /// Byte offset of each frame within the cached block, built once when the
    /// block is decompressed so that per-frame access stays O(1) afterwards.
    mutable std::vector<uint32_t> _blockFrameOffsets;
    mutable std::vector<uint32_t> _blockFrameCounts;

    void EncodePending(size_t kindIdx, uint64_t frame);
    void CloseOpenBlock(size_t kindIdx);

    /// Decompress a block (or return the open one) and index its frames.
    /// Returns false when the block does not exist or fails to decompress.
    bool MaterializeBlock(size_t kindIdx, uint32_t blockIdx) const;

    /// Locate the block holding @p frame. Returns UINT32_MAX when none does.
    uint32_t FindBlockForFrame(size_t kindIdx, uint64_t frame) const;

    /// Decode one frame's key set out of the materialised block cache.
    void DecodeFrameFromCache(uint32_t frameIndexInBlock,
                              std::vector<TTDCoverageKey>& out) const;
};

}  // namespace ttd
