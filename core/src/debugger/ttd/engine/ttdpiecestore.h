#pragma once

/// @file ttdpiecestore.h
/// @brief The engine's piece store: every content change of a 4 KB piece stored once.
///
/// A piece version is stored as Full (a compressed copy), Xor (the compressed
/// difference from the piece's previous version) or Zero (no payload). Rules
/// (measured on real recordings, POC 011 experiments E1 and E2):
/// - an unchanged piece adds a reference to its version, never a new one;
/// - the difference is compressed first; the full piece is compressed too only
///   when the difference came out larger than T bytes (default 128), and the
///   smaller is kept;
/// - a chain of difference versions has at most K - 1 versions (default K =
///   50): the version that would be the K-th is stored Full instead. There is
///   no global key frame;
/// - every S-th version of a chain (default S = 7) is a difference from the
///   version S before it (its anchor) instead of from the previous one, so a
///   decode applies at most (K - 1) / S + S - 1 differences (12) instead of
///   K - 1 (49): a seek decodes every piece's chain (P1.S10 seek format).
/// A version records the version it depends on (its base), so eviction and
/// damage ranges can follow dependencies (engine decision D5). The store can
/// be shared by several sessions (D22).
/// Design: docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md §4.3.

#include <cstdint>
#include <vector>

#include "ttdarena.h"

namespace ttd
{

using TTDPieceId = uint32_t;

class TTDPieceStore
{
public:
    static constexpr TTDPieceId kNone = 0xFFFFFFFFu;

    enum class Encoding : uint8_t
    {
        Full = 0,
        Xor = 1,      ///< the zstd-compressed XOR with the base version
        Zero = 2,
        Ranges = 3,   ///< the XOR's non-zero runs, uncompressed: (u16 offset, u8 length, bytes) each
    };

    /// A version that stores a difference from a base (Xor or Ranges)
    static bool IsDifference(Encoding e) { return e == Encoding::Xor || e == Encoding::Ranges; }

    struct Params
    {
        uint16_t chainLimit = 50;      ///< K: the K-th version of a chain is stored Full
        /// S: every S-th version of a chain builds on the one S before it (Intern's anchor); 0 or 1: each version
        /// builds on the previous one. Chains of fewer than S versions are the same either way
        uint16_t skipStride = DefaultSkipStride();
        uint32_t fullThreshold = 128;  ///< T: compress the full piece only when the XOR is larger
        /// A difference whose runs take at most this many bytes is stored as
        /// Ranges at once, without compressing anything (a few bytes changed
        /// in a piece: a compressed XOR would cost its fixed overhead)
        uint32_t rangesLimit = 64;
    };

    /// Counted work since ResetWork() (deterministic; the benchmark's capture work)
    struct Work
    {
        uint64_t compressCalls = 0;
        uint64_t compressInputBytes = 0;
        uint64_t linksDecoded = 0;     ///< difference versions applied while decoding
        uint64_t versionsStored = 0;
        uint64_t forcedFull = 0;       ///< versions stored Full because the chain reached K
    };

    TTDPieceStore() = default;
    explicit TTDPieceStore(const Params& params) : _params(params) {}

    /// Params::skipStride of the stores made from now on (7 unless set: a measurement's other side)
    static uint16_t DefaultSkipStride();
    static void SetDefaultSkipStride(uint16_t stride);

    const Params& GetParams() const { return _params; }

    /// Store the first version of a piece (no previous one)
    TTDPieceId InternFirst(const uint8_t* bytes);

    /// Store the next version of a piece whose current version is @p previous
    /// with content @p previousBytes. Returns @p previous (with one more
    /// reference) when the content did not change
    /// @p anchor, @p anchorBytes: the chain's last anchor point at or before @p previous (IsAnchorPoint) and its
    /// content, when the caller keeps them: the version that is the next anchor point is stored as a difference
    /// from it, or is @p anchor itself (one more reference) when the piece is back at its content. Without them
    /// every version builds on the previous one
    TTDPieceId Intern(TTDPieceId previous, const uint8_t* previousBytes, const uint8_t* bytes,
                      TTDPieceId anchor = kNone, const uint8_t* anchorBytes = nullptr);
    /// @p id starts a chain or is the S-th, 2S-th ... version of one: the version S after it builds on it, so a
    /// caller that passes anchors to Intern keeps its content from here on
    bool IsAnchorPoint(TTDPieceId id) const
    {
        return _params.skipStride > 1 && _versions[id].run % _params.skipStride == 0;
    }

    /// A version as stored elsewhere (a session file, Phase 4): its encoding,
    /// its base (a difference only: one reference is taken on it), depth, the
    /// CRC of the raw piece and the payload, kept as it is. Returns its id with
    /// one reference; kNone when the fields do not fit together
    TTDPieceId Import(Encoding encoding, TTDPieceId base, uint16_t depth, uint32_t crc, const uint8_t* payload,
                      size_t size);
    /// The stored payload of @p id (null for a Zero version)
    const uint8_t* PayloadData(TTDPieceId id) const
    {
        return _versions[id].payload.size ? _arena.Data(_versions[id].payload) : nullptr;
    }
    uint32_t CrcOf(TTDPieceId id) const { return _versions[id].crc32c; }

    void AddRef(TTDPieceId id) { _versions[id].refcount++; }
    /// Drop one reference; a version whose last reference goes is freed, and
    /// with it the reference it held on its base
    void Release(TTDPieceId id);

    /// Decode @p id into @p out (4 KB). False when a stored payload is damaged
    bool Decode(TTDPieceId id, uint8_t* out) const;
    /// Decode @p id into @p inout, which holds version @p have: when one of the two is a difference chain's descendant
    /// of the other, only the differences between them are applied (an XOR difference turns either way) - a restore
    /// one checkpoint from the last applies one difference, not the chain. False when they share no chain or that
    /// is not cheaper than Decode (@p inout untouched), or when a payload is damaged (@p inout undefined)
    bool DecodeFrom(TTDPieceId id, TTDPieceId have, uint8_t* inout, bool& damaged) const;

    Encoding EncodingOf(TTDPieceId id) const { return _versions[id].encoding; }
    uint16_t DepthOf(TTDPieceId id) const { return _versions[id].depth; }
    /// The version's place in its chain: 0 for a Full or Zero version, the versions since it for a difference
    uint8_t RunOf(TTDPieceId id) const { return _versions[id].run; }
    TTDPieceId BaseOf(TTDPieceId id) const { return _versions[id].base; }
    uint32_t RefCount(TTDPieceId id) const { return _versions[id].refcount; }
    uint32_t PayloadSize(TTDPieceId id) const { return _versions[id].payload.size; }

    size_t LiveVersions() const { return _liveVersions; }
    /// Changes whenever an id may stand for other content than before (a version freed, the store cleared): a
    /// holder that keeps decoded pieces by id keeps them while it stays the same
    uint64_t ReuseGeneration() const { return _reuseGeneration; }

    /// Tests only: flip one bit of @p id's stored payload (damage, as a bad
    /// disk sector would). False for a version without payload (Zero)
    bool FlipBitForTesting(TTDPieceId id, uint32_t byte)
    {
        Version& v = _versions[id];
        if (v.payload.size == 0)
            return false;
        _arena.MutableData(v.payload)[byte % v.payload.size] ^= 0x10;
        ++_reuseGeneration;   // the version's content changed under its id
        return true;
    }
    size_t PayloadBytes() const { return _arena.LiveBytes(); }
    /// Heap: version table, free list, arena chunks, decode scratch
    size_t HeapBytes() const;
    size_t VersionTableBytes() const;
    size_t ArenaBytes() const { return _arena.HeapBytes(); }

    const Work& GetWork() const { return _work; }
    void ResetWork() { _work = Work{}; }

    void Clear();

private:
    /// Ranges encoding (Encoding::Ranges)
    static size_t RangesSize(const uint8_t* diff);
    TTDPieceId StoreRanges(TTDPieceId base, uint32_t depth, uint32_t run, const uint8_t* diff, size_t size,
                           uint32_t crc);
    static bool ApplyRanges(const uint8_t* payload, size_t size, uint8_t* out);
    struct Version
    {
        TTDArenaRef payload;
        TTDPieceId base = kNone;   ///< Xor only: the version this one is a difference from
        uint32_t crc32c = 0;       ///< of the raw 4 KB
        uint32_t refcount = 0;
        uint16_t depth = 0;        ///< 0 for Full and Zero, base depth + 1 for a difference
        Encoding encoding = Encoding::Full;
        /// The versions since the chain's Full or Zero start (saturating at 255; an imported version: its depth).
        /// Equal to depth while every version builds on the previous one
        uint8_t run = 0;
    };

    TTDPieceId Allocate();
    TTDPieceId StoreFull(const uint8_t* bytes, uint32_t crc);
    std::vector<uint8_t> Compress(const uint8_t* bytes);

    Params _params;
    std::vector<Version> _versions;
    std::vector<TTDPieceId> _free;
    size_t _liveVersions = 0;
    uint64_t _reuseGeneration = 0;   ///< ReuseGeneration
    TTDArena _arena;
    mutable Work _work;
};

}  // namespace ttd
