#pragma once

/// @file ttddumpformat.h
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
/// machine's 256 pages were found to truncate to 0); chipset_state shrank
/// to 120 bytes (from 168, or 184 with the Scorpion ProfROM fields) when
/// the extended / model-specific port latches
/// moved out to TTDPeripheralRegistry serializers. Per-frame checkpoints
/// moved to AFTER the new frame's start (MainLoop::CompleteFrame): a
/// checkpoint now carries the started frame's state (the General Sound
/// blobs gained their frame timeline in bytes 77..88) - a semantic change
/// older files would parse but restore one frame start off. Sessions recorded
/// before an amendment must be re-recorded.
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

/// Bit 3 of header.flags — set when an advisory bookmarks section follows
/// the coverage index (TD-4 agent bookmarks).
///
/// Bookmarks are named timeline annotations, not replay data: a file
/// without them is a complete session that simply carries no annotations.
/// They are advisory in both directions — SeekTo never stops on one, and
/// their absence costs nothing but the convenience of label-based return.
constexpr uint16_t kFlagsHasBookmarks = 0x0008;

/// Bit 4 of header.flags — set when the write journal holds every write of the
/// session: journaling ran from the recording start without a pause and the
/// ring never overwrote a record. Only then may a reader answer write/port
/// reverse queries from the journal; without the bit (older files included)
/// it falls back to replay, which is always correct. An attribute of the
/// journal section, not a section of its own.
constexpr uint16_t kFlagsWriteJournalComplete = 0x0010;

/// Bit 5 of header.flags — every in-frame position (checkpoints, markers,
/// input events, bookmarks, journal globalT) counts T-states at the model's
/// TOP CPU clock (EmulatorState::ttd_clock_units per base T-state; B4). Older
/// files counted at the clock running at the time, which repeats values
/// after a mid-frame hardware turbo switch; on models without a hardware
/// turbo both conventions are the same number, so only turbo models refuse
/// a file without the bit.
constexpr uint16_t kFlagsTopClockTime = 0x0020;

/// Bit 6 of header.flags — an input-journal section follows the bookmarks
/// (or the section before it): every keyboard, Kempston Mouse and General
/// Sound host stimulus the session applied, with the exact time it took
/// effect. Replay data, not an annotation: without it a loaded session
/// re-executes history inside a frame with no input and diverges from the
/// recording. The writer always sets the bit and writes the section, empty
/// or not, so "no input happened" and "the file predates the section" are
/// told apart. A section that fails to read fails the load.
///
/// Layout: u32 count, then per event (kInputEventRecordSize bytes)
/// u64 frame, u32 tInFrame, u8 kind (TTDInputKind), u8 key, u8 pressed,
/// i16 dx, i16 dy, u8 buttonMask, i8 wheelSteps, u8 value. Events are in
/// ascending time order.
constexpr uint16_t kFlagsHasInputJournal = 0x0040;

/// Bit 7 of header.flags — an external-event section follows the input
/// journal: the replay barriers of the session (tape control, disk and media
/// writes, debugger edits, hardware resets) with their exact time. Without
/// it a loaded session's seeks and reverse searches cross points that replay
/// cannot reproduce. Written always, like the input journal; a section that
/// fails to read fails the load.
///
/// Layout: u32 count, then per event u64 frame, u32 tInFrame, u8 kind
/// (TTDExternalEventKind; unknown values are kept - every marker is a
/// barrier whatever its kind), u8 reason_len (0..63), reason bytes (no
/// terminator). Events are in ascending time order.
constexpr uint16_t kFlagsHasExternalEvents = 0x0080;

/// Bit 8 of header.flags — the port journals follow the external events:
/// every IN result and every OUT of the main CPU, in execution order, each
/// with its time and PC, and each journal's position at every checkpoint.
/// With them a replay feeds the CPU the recorded IN values and needs no media
/// or host device, and "when did the program ..." questions are answered
/// without replay (ttd-port-read-journal.md). Written only when the session
/// holds the whole history's I/O (a configuration the first version isolates,
/// recorded without a gap); a reader without the section replays against the
/// live devices as before. A section that fails to read fails the load.
///
/// Layout: the IN journal, then the OUT journal, each: u64 record_count, u32
/// block_records (32768), u32 block_count, then per block u32 records, u64
/// base_frame (the first record's frame), u32 crc32c (of the raw block), u32
/// compressed_size and the zstd payload. The raw block is five columns of
/// `records` entries: u16 ports, u8 values, u16 PCs, u32 frame deltas (from
/// the previous record; 0 for the first) and u32 T-states (TTD time in the
/// frame: absolute for the first record and when the frame changed, else the
/// step from the previous record). Every block but the last holds
/// block_records records; records are in time order. Then u32 cursor_count
/// (= checkpoint count) and one u64 cursor per checkpoint, non-decreasing,
/// each at most record_count.
constexpr uint16_t kFlagsHasPortJournals = 0x0100;

/// Bit 9 of header.flags — the header's last 8 bytes (formerly reserved) hold
/// the peripheral mask: bit i set when peripheral id i (PeripheralId) has a
/// blob in the checkpoints - the devices fitted when the session was recorded.
/// A reader learns the recorded machine from the header alone (model, ROM
/// signature, devices: ttdfileinfo.h) and can provision a matching instance
/// before the load. Without the bit (older files) the bytes are zero and the
/// device set is found at the first checkpoint's blob ids.
constexpr uint16_t kFlagsHasPeripheralMask = 0x0200;

/// Bit 10 of header.flags — a network-input section follows the port journals
/// (network adapters TDD §6), written only when the session has NetEvents (a
/// session without a network adapter keeps the older layout byte for byte).
/// The input journal section keeps its fixed record size; NetEvents there
/// carry only the common fields, and this section holds the rest. Layout:
/// u32 count, then per NetEvent in journal order: u32 event_index (in the input journal), u16 socket,
/// u8 event, u8 status, u32 addr, u16 port, u32 payload_offset,
/// u32 payload_length; then u32 payload_size and payload_size bytes (the
/// payload store). Every event's payload range lies inside the store.
constexpr uint16_t kFlagsHasNetInputs = 0x0400;

/// Bytes per network-input record (the layout above)
constexpr uint32_t kNetInputRecordSize = 4 + 2 + 1 + 1 + 4 + 2 + 4 + 4;

/// Cap on the payload store: the bytes a session received from the network
constexpr uint32_t kMaxNetPayloadBytes = 1u << 30;

/// Bytes per input event on disk (the field-by-field layout above).
constexpr uint32_t kInputEventRecordSize = 8 + 4 + 1 + 1 + 1 + 2 + 2 + 1 + 1 + 1;

/// Sanity caps: a claimed count above these is corruption. At ~10 events a
/// second the input cap is days of typing; markers are rarer still.
constexpr uint32_t kMaxInputEvents    = 1u << 24;
constexpr uint32_t kMaxExternalEvents = 1u << 20;

/// Longest external-event reason stored (TTDExternalEvent::reason is 64 bytes
/// with the terminator).
constexpr uint8_t kMaxExternalEventReason = 63;

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
// Model-specific peripheral blobs
// ---------------------------------------------------------------------------

/// Upper bound on registry blobs stored per checkpoint. A machine registers one
/// serializer per model-specific subsystem, so the real count is 0-2 today; the
/// cap only exists so a corrupt length cannot drive an unbounded read loop.
constexpr uint16_t kMaxPeripheralBlobsPerCheckpoint = 64;

/// Upper bound on one stored device blob (after its compression). Most blobs
/// are tens of bytes; the largest today are the classic General Sound card and
/// the lightweight player, which still carry their memory (the classic card up
/// to 512 KB of RAM, which does not compress when it holds module data). NeoGS keeps its RAM and flash
/// out of v1 blobs - large memories wait for TTD v2 memory regions. The writer
/// refuses a larger blob and the reader treats one as corruption, so a file
/// that saves also loads.
constexpr uint32_t kMaxPeripheralBlobBytes = 16u << 20;

/// Sentinel rom_signature meaning "the writer could not read the ROM region"
/// (no Memory attached). A file carrying it skips the compatibility check
/// rather than failing every load.
constexpr uint64_t kRomSignatureUnknown = 0;

// ---------------------------------------------------------------------------
// Sentinels
// ---------------------------------------------------------------------------

/// Sentinel slot index meaning "this sub-page was never written during the
/// session up to this checkpoint; live RAM content IS the historical content".
/// Matches TTDPageRef::kNeverTouched in ttdcheckpoint.h.
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
