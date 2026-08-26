#pragma once
//
// ttd_format.h — Constants for the .ttd binary format (schema v3).
//
// Mirrors the canonical definitions in:
//   core/src/debugger/ttd/ttd_dump_format.h  (C++ writer)
//   core/src/debugger/ttd/ttd.ksy            (Kaitai Struct — source of truth)
//   tools/verification/ttd-analyzer/src/ttd_format.py  (Python reader)
//
// v3 is additive over v2: a new write-journal section after the checkpoint
// table (flag bit 1). v2 fields, slots, and checkpoints are unchanged, so a
// v2 reader can still parse v3 files if it ignores the journal.
//
// This PoC header has ZERO dependency on libcore.a — it copies the POD
// constants verbatim so the PoC is fully self-contained.


#include <cstdint>
#include <cstddef>

namespace ttd {

// Magic + versioning
inline constexpr char kMagic[4] = {'T', 'T', 'D', 'D'};
inline constexpr uint16_t kSchemaVersion     = 3;  // current writer
inline constexpr uint16_t kMinSupportedSchema = 2; // v2 readers can parse v3
inline constexpr uint16_t kMaxSupportedSchema = 3;

// Header flag bits
inline constexpr uint16_t kFlagsLittleEndian   = 0x0001; // always set in v2+
inline constexpr uint16_t kFlagsHasWriteJournal = 0x0002; // v3 additive section

// Page geometry
inline constexpr uint32_t kEmuPageSize        = 16384;  // 16 KB Z80 banking unit
inline constexpr uint32_t kSubPageSize        = 4096;   // 4 KB codec unit
inline constexpr uint32_t kSubPagesPerEmuPage = 4;

// Slot encodings (Encoding enum in TTDCodecPageStore)
inline constexpr uint8_t kEncodingFull     = 0;  // zstd-compressed 4 KB snapshot
inline constexpr uint8_t kEncodingXorPrev  = 1;  // zstd-compressed XOR against prev_slot
inline constexpr uint8_t kEncodingZero     = 2;  // all zeros, payload empty

// Frame kinds (TTDFrameKind enum)
inline constexpr uint8_t kFrameKindKeyFrame   = 0;  // I-frame
inline constexpr uint8_t kFrameKindDeltaFrame = 1;  // P-frame

// Sentinel slot index meaning "this sub-page was never written"
inline constexpr uint32_t kNeverTouchedSlot = 0xFFFFFFFFu;

// Screen rendering constants
inline constexpr int kScreenWidth   = 256;
inline constexpr int kScreenHeight  = 192;
inline constexpr int kPixelBytes    = 6144;   // 256*192/8
inline constexpr int kAttrBytes     = 768;    // 32*24
inline constexpr int kScreenBytes   = 6912;   // kPixelBytes + kAttrBytes
inline constexpr int kBorderDefaultPx = 32;

} // namespace ttd
