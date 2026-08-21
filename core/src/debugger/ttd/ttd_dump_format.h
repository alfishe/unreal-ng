#pragma once

/// @file ttd_dump_format.h
/// @brief C++ constants and helpers for the .ttd binary format v1.
///
/// v1 is the first production release format with proper XOR-delta encoding:
///   - Codec-aware page store: 4 KB sub-pages
///   - Three encodings per slot: Full / XorPrev / Zero
///   - XOR-delta payloads written directly (not reconstructed full pages)
///   - zstd level 1 compression of every slot payload
///   - Per-slot CRC32C integrity check (4 bytes overhead per slot)
///   - I-frame / P-frame discriminator per checkpoint
///   - Write journal section for reverse-watchpoint fast-path scan
///
/// Mirrors the canonical Kaitai Struct schema in ttd.ksy. The schema is the
/// single source of truth — this header exists only to give the C++ writer
/// (TimeTravelManager::SerializeSession) symbolic names for magic / version /
/// sentinels without hand-typing integer literals.
///
/// Compatibility contract:
///   - kSchemaVersion here MUST equal meta.schema-version in ttd.ksy
///   - Both are bumped atomically in one commit on any breaking change
///
/// The .ksy is git-tagged `ttd-schema-vN` on every breaking change so
/// third-party parsers can pin to a known schema version.

#include <cstdint>

namespace ttd::dump {

/// 4-byte magic at the head of every .ttd file. ASCII "TTDD".
constexpr char kMagic[4] = {'T', 'T', 'D', 'D'};

/// Schema version. v1 = pre-release format with XOR-delta encoding.
/// MUST match `meta.schema-version` in ttd.ksy.
///
/// The format has not shipped, so it is still being amended in place rather
/// than versioned: header.model_ram_pages is u2 (it was u1 until a 4 MB
/// machine's 256 pages were found to truncate to 0). Sessions recorded before
/// that amendment do not parse and must be re-recorded.
constexpr uint16_t kSchemaVersion = 1;

/// Bit 0 of header.flags — set when the writer is little-endian (always 1
/// in v2+; we static_assert against little-endian in the writer).
constexpr uint16_t kFlagsLittleEndian = 0x0001;

/// Bit 1 of header.flags — set when the file includes a write-journal
/// section after the checkpoint table.
constexpr uint16_t kFlagsHasWriteJournal = 0x0002;

/// Bit 2 of header.flags — set when a reverse-search coverage index follows
/// the write journal.
///
/// The index is derived data: a session without it is complete and correct,
/// just slower to search (reverse queries fall back to replaying frames, which
/// is what they did before the index existed). Storing it costs 3-5 MB per hour
/// of recording against a session measured in gigabytes, and saves the ~32
/// seconds per ten minutes of history that rebuilding by replay would take.
constexpr uint16_t kFlagsHasCoverageIndex = 0x0004;

// ---------------------------------------------------------------------------
// Page slot encodings (Encoding enum in TTDCodecPageStore)
// ---------------------------------------------------------------------------

/// Slot stores a full, independent 4 KB page snapshot (zstd-1 compressed).
constexpr uint8_t kEncodingFull    = 0;
/// Slot stores an XOR delta against prev_slot's reconstructed bytes.
/// Payload is zstd-1 compressed XOR buffer.
constexpr uint8_t kEncodingXorPrev = 1;
/// Slot is all zeros. Payload is empty.
constexpr uint8_t kEncodingZero    = 2;

// ---------------------------------------------------------------------------
// Frame kinds (TTDFrameKind enum)
// ---------------------------------------------------------------------------

/// I-frame: every model RAM page is captured as Full snapshots.
/// Restore is O(1) per page.
constexpr uint8_t kFrameKindKeyFrame  = 0;
/// P-frame: only dirty pages are captured, encoded as Xor deltas against
/// the previous slot chain.
constexpr uint8_t kFrameKindDeltaFrame = 1;

// ---------------------------------------------------------------------------
// Sentinels
// ---------------------------------------------------------------------------

/// Sentinel slot index meaning "this sub-page was never written during the
/// session up to this checkpoint; live RAM content IS the historical content".
/// Matches TTDPageRef::kNeverTouched in ttd_checkpoint.h.
constexpr uint32_t kNeverTouchedSlot = 0xFFFFFFFFu;

// ---------------------------------------------------------------------------
// Sizes
// ---------------------------------------------------------------------------

/// 4 KB sub-page size used by the v2 codec.
/// Each 16 KB emulator RAM page is split into 4 × 4 KB sub-pages.
constexpr uint32_t kSubPageSize = 4096u;

/// Number of sub-pages per emulator RAM page (16 KB / 4 KB).
constexpr uint32_t kSubPagesPerEmuPage = 4u;

/// A reader that encounters a schema_version field higher than the maximum
/// it supports MUST refuse to parse and surface a clear error. Use this
/// constant in the error message ("file is schema vN, this reader supports
/// up to vM").
constexpr uint16_t kMaxSupportedSchemaVersion = kSchemaVersion;

} // namespace ttd::dump
