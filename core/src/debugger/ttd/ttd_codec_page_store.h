#pragma once

/// @file ttd_codec_page_store.h
/// @brief Codec-aware COW page store for TTD checkpoint RAM capture.
///
/// Replaces the legacy uncompressed 16 KB TTDPageStore with a compressed
/// 4 KB store that supports both full-page snapshots and XOR-delta snapshots.
///
/// Per Phase 5 PoC results (docs/inprogress/2026-07-19-time-travel/
/// phase-5-codec-poc-results.md):
///   - 4 KB granularity (was 16 KB) — 4x fewer dirty pages on idle workload
///   - zstd level 1 compression — 2.66x ratio at 43 us/page
///   - XOR-against-prev-slot delta encoding — 92% size win on BLI workload
///   - CRC32C per slot — 4 byte overhead per slot, hardware-accelerated
///
/// Slot layout (on-the-heap, after Intern):
///   - encoding:    0=full, 1=xor-prev, 2=zero (page all zeros)
///   - refcount:    uint32_t — shared via AddRef when not dirty
///   - prevSlot:    uint32_t — for xor-prev: where to apply the delta
///   - rawSize:     uint32_t — always kPageSize (4096); sanity check
///   - compSize:    uint32_t — compressed payload bytes
///   - crc32c:      uint32_t — CRC32C of the ORIGINAL raw page (not XOR)
///   - payload:     uint8_t[compSize] — zstd-1 compressed bytes
///
/// Thread model: single-threaded (emulator thread for capture; control
/// thread for restore). No internal locking. Caller must pause the
/// emulator before calling restore-side methods.

#include <cstdint>
#include <cstddef>
#include <vector>

#include "ttd_compression.h"

namespace ttd {

/// @brief Codec-aware compressed page store.
class TTDCodecPageStore
{
public:
    /// Size of one page slot. Changed from 16 KB to 4 KB in v2 — see PoC
    /// results doc for the empirical justification (92.9% of dirty 16K
    /// pages have only one dirty 4K sub-page, so 4K granularity saves 4x).
    static constexpr size_t kPageSize = 4096;

    /// Encoding mode stored per slot.
    enum class Encoding : uint8_t {
        Full    = 0,   ///< Independent snapshot (I-frame page)
        XorPrev = 1,   ///< XOR delta against prevSlot's decompressed bytes
        Zero    = 2,   ///< Page is all zeros (no payload)
    };

    TTDCodecPageStore() = default;
    ~TTDCodecPageStore() = default;

    TTDCodecPageStore(const TTDCodecPageStore&) = delete;
    TTDCodecPageStore& operator=(const TTDCodecPageStore&) = delete;

    // -------------------------------------------------------------------
    // Intern family (capture path)
    // -------------------------------------------------------------------

    /// @brief Store a 4 KB page as a full (I-frame) snapshot.
    ///
    /// Compresses the page with zstd-1 and stores it along with its CRC32C.
    /// The result is a self-contained slot — restore can decompress it
    /// without consulting any other slot.
    ///
    /// @param pageData  4 KB of raw page data. Must not be null.
    /// @return Slot index. Use AddRef/Release for refcount management.
    uint32_t InternFull(const uint8_t* pageData);

    /// @brief Store a 4 KB page as an XOR delta against an existing slot.
    ///
    /// Computes: xorBuf = pageData XOR Decompress(prevSlot)
    /// Compresses xorBuf with zstd-1. If the compressed XOR is larger than
    /// the page compressed directly (rare for non-correlated data), falls
    /// back to InternFull and stores as Encoding::Full instead — the caller
    /// never sees the fallback. If xorBuf is all zeros, stores as
    /// Encoding::Zero with empty payload.
    ///
    /// @param prevSlot  Slot index of the previous version of this page.
    ///                  Must be a live slot (refcount > 0).
    /// @param pageData  4 KB of new page data. Must not be null.
    /// @return Slot index.
    uint32_t InternXor(uint32_t prevSlot, const uint8_t* pageData);

    /// @brief Increment refcount of a slot.
    /// Used when a new checkpoint shares a clean page with the previous one.
    void AddRef(uint32_t idx);

    /// @brief Decrement refcount; free the slot when it reaches zero.
    void Release(uint32_t idx);

    // -------------------------------------------------------------------
    // Restore path
    // -------------------------------------------------------------------

    /// @brief Decompress slot `idx` into `outBuf`.
    ///
    /// Handles all three encodings:
    ///   - Full:   decompress payload directly into outBuf
    ///   - XorPrev: decompress payload, XOR with recursively-decompressed prev
    ///   - Zero:   memset(outBuf, 0, kPageSize)
    ///
    /// Verifies CRC32C after reconstruction; on mismatch returns false and
    /// leaves outBuf in an unspecified state.
    ///
    /// @param idx     Slot index.
    /// @param outBuf  Destination buffer, at least kPageSize bytes.
    /// @return true on success (CRC matches); false on CRC mismatch or error.
    bool GetPage(uint32_t idx, uint8_t* outBuf) const;

    /// @brief Recursive depth of an xor-prev chain (for diagnostics).
    /// A Full slot has depth 0; an XorPrev slot has depth(prevSlot) + 1.
    /// Useful to detect pathological chains that would slow restore.
    uint32_t GetDeltaDepth(uint32_t idx) const;

    // -------------------------------------------------------------------
    // Diagnostics
    // -------------------------------------------------------------------

    Encoding GetEncoding(uint32_t idx) const;
    uint32_t GetRefCount(uint32_t idx) const;
    uint32_t GetPrevSlot(uint32_t idx) const;

    inline uint32_t GetCapacity() const { return static_cast<uint32_t>(_slots.size()); }
    inline uint32_t GetUsedSlots() const { return _usedSlots; }
    inline uint32_t GetFreeSlotCount() const { return static_cast<uint32_t>(_freeList.size()); }

    /// Total bytes currently held in slot payloads + headers. Used by the
    /// capture orchestrator for budget enforcement (default 64 MB).
    size_t GetUsedBytes() const;

    /// Bytes allocated for slot capacity (capacity × sizeof(Slot) rounded).
    /// Smaller than the old uncompressed store because payloads are compressed.
    size_t GetCapacityBytes() const;

    /// Total payload bytes across all live slots (excludes Slot header).
    /// Used by telemetry to compute compression ratio.
    size_t GetLivePayloadBytes() const;

    /// Compression ratio: averageRawBytes / averageCompressedBytes.
    /// Returns 1.0 if no live slots; otherwise kPageSize / mean(payload).
    double GetCompressionRatio() const;

    /// Clear all storage. Used by session reset.
    void Reset();

private:
    /// One stored page slot. Variable-size via payload vector — the legacy
    /// fixed-flat-vector trick doesn't apply when payloads compress to
    /// different sizes.
    struct Slot {
        Encoding  encoding = Encoding::Full;
        uint32_t  refcount = 0;
        uint32_t  prevSlot = 0;          ///< Only valid when encoding == XorPrev
        uint32_t  crc32c = 0;            ///< Of the ORIGINAL raw page
        std::vector<uint8_t> payload;    ///< zstd-1 compressed (or empty if Zero)
    };

    std::vector<Slot>    _slots;
    std::vector<uint32_t> _freeList;
    uint32_t _usedSlots = 0;

    /// Scratch buffer for recursive decompression in GetPage(XorPrev).
    /// Marked mutable so GetPage can stay const.
    mutable std::vector<uint8_t> _prevScratch;

    /// Allocate a free slot index (grows _slots if needed).
    uint32_t AllocateSlot();

    /// Internal helper: decompress slot `idx` into `outBuf` without CRC
    /// verification (used internally by the XOR-restore recursive path).
    bool GetPageNoVerify(uint32_t idx, uint8_t* outBuf) const;
};

}  // namespace ttd
