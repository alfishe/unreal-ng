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
/// - a chain of Xor versions has at most K - 1 links (default K = 50): a
///   version that would reach depth K is stored Full instead. There is no
///   global key frame.
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
        uint16_t chainLimit = 50;      ///< K: a version at depth K is stored Full
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

    const Params& GetParams() const { return _params; }

    /// Store the first version of a piece (no previous one)
    TTDPieceId InternFirst(const uint8_t* bytes);

    /// Store the next version of a piece whose current version is @p previous
    /// with content @p previousBytes. Returns @p previous (with one more
    /// reference) when the content did not change
    TTDPieceId Intern(TTDPieceId previous, const uint8_t* previousBytes, const uint8_t* bytes);

    void AddRef(TTDPieceId id) { _versions[id].refcount++; }
    /// Drop one reference; a version whose last reference goes is freed, and
    /// with it the reference it held on its base
    void Release(TTDPieceId id);

    /// Decode @p id into @p out (4 KB). False when a stored payload is damaged
    bool Decode(TTDPieceId id, uint8_t* out) const;

    Encoding EncodingOf(TTDPieceId id) const { return _versions[id].encoding; }
    uint16_t DepthOf(TTDPieceId id) const { return _versions[id].depth; }
    TTDPieceId BaseOf(TTDPieceId id) const { return _versions[id].base; }
    uint32_t RefCount(TTDPieceId id) const { return _versions[id].refcount; }
    uint32_t PayloadSize(TTDPieceId id) const { return _versions[id].payload.size; }

    size_t LiveVersions() const { return _liveVersions; }
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
    TTDPieceId StoreRanges(TTDPieceId previous, uint32_t depth, const uint8_t* diff, size_t size, uint32_t crc);
    static bool ApplyRanges(const uint8_t* payload, size_t size, uint8_t* out);
    struct Version
    {
        TTDArenaRef payload;
        TTDPieceId base = kNone;   ///< Xor only: the version this one is a difference from
        uint32_t crc32c = 0;       ///< of the raw 4 KB
        uint32_t refcount = 0;
        uint16_t depth = 0;        ///< 0 for Full and Zero, base depth + 1 for Xor
        Encoding encoding = Encoding::Full;
    };

    TTDPieceId Allocate();
    TTDPieceId StoreFull(const uint8_t* bytes, uint32_t crc);
    std::vector<uint8_t> Compress(const uint8_t* bytes);

    Params _params;
    std::vector<Version> _versions;
    std::vector<TTDPieceId> _free;
    size_t _liveVersions = 0;
    TTDArena _arena;
    mutable Work _work;
};

}  // namespace ttd
