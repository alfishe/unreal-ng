#pragma once

/// @file ttdbookmarks.h
/// @brief TTD bookmarks — advisory timeline annotations for agents and humans.
///
/// Per TD-4 (docs/inprogress/2026-09-14-automation-triage-gaps/
/// ttd-coverage-evaluation.md §TD-4):
///   "Implement the designed-but-missing bookmark as an *advisory*
///    annotation, explicitly not a replay barrier."
///
/// The reverse-search workflow that bookmarks serve: an agent seeks into
/// history to inspect a moment (the umt unpack entry, the title screen),
/// then wanders elsewhere via find-last / reverse-continue / step-back. The
/// only way back used to be re-running the reverse query that found the
/// moment in the first place. A bookmark is a named TTDTimePoint the agent
/// can return to by label.
///
/// What a bookmark is NOT (the barrier distinction, TDD §5.1):
///   - TTDExternalEventJournal stores *replay barriers*: nondeterminism the
///     engine cannot reproduce, so SeekTo stops at them. Bookmarks never
///     appear in that journal and never influence SeekTo — seeking across a
///     bookmark is exactly as legal as seeking across any other instant.
///     Concretely: a seek to or past a bookmark reports halt_reason
///     "target" / "external_event" / "out_of_range" — never "bookmark",
///     because no such halt reason exists.
///   - Bookmarks observe the timeline; they never modify it.
///
/// Storage: a plain std::vector kept sorted by time (like the external-event
/// journal). Bookmarks are rare — created on demand by a human or agent, not
/// per frame — so a linear scan is the right data structure.
///
/// Agent notes (docs/inprogress/2026-11-03-ttd-agent-bookmarks/design.md):
/// a bookmark also keeps the agent's note, tags, the moment's context
/// (registers, the code page and instruction at PC, taken once when it is
/// added) and the bytes of watched ranges. Note, tags and the label are
/// editable (Edit); the time, the context and the bytes are not. The .ttd
/// file keeps all of it (the bookmarks stream, version 2; version 1 holds
/// time and label only and still loads).
///
/// Label contract (labels are keys, not free text):
///   - non-empty, at most kMaxBookmarkLabelLength chars;
///   - unique within the session — Add rejects a duplicate instead of
///     silently truncating or shadowing, because seek/delete by label must
///     resolve to exactly one bookmark;
///   - rejected rather than truncated on over-length, for the same reason
///     (a truncated label would not round-trip through the .ttd file).
///
/// Thread model: mirrors TTDExternalEventJournal. All mutations and reads
/// are guarded by an internal mutex; Snapshot() returns a stable copy under
/// the lock so WebAPI/Lua/Python/CLI threads never iterate live storage.
///
/// Lifecycle (owned by TimeTravelController, same discipline as the other
/// journals):
///   - StartRecording / InvalidateSession — Clear() (a bookmark into wiped
///     history would dangle);
///   - ResumeRecordingFrom — DropAfter(from) (bookmarks in the discarded
///     future are dead; bookmarks exactly at `from` are kept);
///   - SerializeSession / DeserializeSession — persisted as the session
///     file's "bookmarks" holder stream (ttdcontrollersessionfile.cpp).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "ttdcheckpoint.h"  // TTDTimePoint

namespace ttd {

/// Maximum bookmark label length. Labels are identifiers resolved by
/// seek/delete, so the limit is enforced by rejection (not truncation).
/// 63 chars + NUL matches the external-event reason budget and the on-disk
/// u8 length prefix leaves headroom.
constexpr size_t kMaxBookmarkLabelLength = 63;

/// Agent notes (2026-11-03, docs/inprogress/2026-11-03-ttd-agent-bookmarks): bounds enforced by rejection, like
/// the label's, so a note round-trips through the .ttd file unchanged.
constexpr size_t kMaxBookmarkNoteLength = 4096;
constexpr size_t kMaxBookmarkTags = 8;
constexpr size_t kMaxBookmarkTagLength = 31;
constexpr size_t kMaxBookmarkWatches = 8;
constexpr size_t kMaxBookmarkWatchLength = 16;
constexpr size_t kMaxBookmarkInstructionLength = 63;

/// @brief The machine at the bookmark's moment, taken once when it is added (a seek to it would show the same).
struct TTDBookmarkContext
{
    bool valid = false;         ///< false: not taken (context=false, or the moment could not be reached)
    uint16_t pc = 0;
    uint16_t sp = 0;
    uint16_t af = 0;
    uint16_t bc = 0;
    uint16_t de = 0;
    uint16_t hl = 0;
    uint16_t ix = 0;
    uint16_t iy = 0;
    uint8_t iff1 = 0;
    uint8_t im = 0;
    uint8_t p7FFD = 0;          ///< The 128K paging latch (0 on a machine without one)
    uint16_t codePage = 0xFFFF; ///< The page PC runs from (Memory::TtdKeyOf), 0xFFFF unknown
    uint8_t codeKind = 0xFF;    ///< MemoryBankModeEnum of that page: 0 ROM, 1 RAM, 2 cache; 0xFF unknown
    std::string instruction;    ///< The instruction at PC, disassembled (<= kMaxBookmarkInstructionLength chars)
};

/// @brief A watched Z80 range: its bytes at the bookmark's moment.
struct TTDBookmarkWatch
{
    uint16_t address = 0;
    uint8_t length = 1;              ///< 1..kMaxBookmarkWatchLength
    std::vector<uint8_t> bytes;      ///< Read at the moment (empty: not read)
};

/// @brief A single named timeline position.
struct TTDBookmark
{
    TTDTimePoint time;   ///< When the bookmark points at.
    std::string  label;  ///< Unique, non-empty, <= kMaxBookmarkLabelLength chars.
    std::string  note;   ///< The agent's or human's note (<= kMaxBookmarkNoteLength chars), free text
    std::vector<std::string> tags;          ///< <= kMaxBookmarkTags, each 1..kMaxBookmarkTagLength of [A-Za-z0-9_.-]
    TTDBookmarkContext context;             ///< The machine at the moment
    std::vector<TTDBookmarkWatch> watches;  ///< <= kMaxBookmarkWatches

    bool HasTag(const std::string& tag) const;
};

/// @brief The bounds of a bookmark's fields (label, note, tags, watches); false and *err on the first violation.
bool ValidateBookmark(const TTDBookmark& bookmark, std::string* err);

/// @brief Sorted collection of TTDBookmarks with label-keyed lookup.
class TTDBookmarkJournal
{
public:
    TTDBookmarkJournal() = default;
    ~TTDBookmarkJournal() = default;

    TTDBookmarkJournal(const TTDBookmarkJournal&) = delete;
    TTDBookmarkJournal& operator=(const TTDBookmarkJournal&) = delete;

    /// @brief Insert a bookmark, keeping the time-sorted order.
    ///
    /// Validation happens here so every surface (WebAPI/MCP/CLI/Lua/Python)
    /// gets identical rules from the single source:
    ///   - label non-empty and <= kMaxBookmarkLabelLength chars;
    ///   - label not already present.
    /// On rejection returns false and fills *err (when non-null) with a
    /// caller-presentable reason.
    bool Add(const TTDBookmark& bookmark, std::string* err = nullptr);

    /// @brief Thread-safe copy of all bookmarks, sorted by time.
    std::vector<TTDBookmark> Snapshot() const;

    /// @brief Resolve a label. Returns false when no bookmark carries it.
    bool Find(const std::string& label, TTDBookmark& out) const;

    /// @brief Change the bookmark with this label under the lock: @p edit changes a copy, which replaces it when
    /// ValidateBookmark accepts it and a renamed label is still unique. False and *err otherwise (unknown label,
    /// a bound broken, a duplicate label); the journal is then untouched.
    bool Edit(const std::string& label, const std::function<void(TTDBookmark&)>& edit, std::string* err = nullptr);

    /// @brief Remove the bookmark with this label. Returns false when absent.
    bool Remove(const std::string& label);

    /// @brief Drop every bookmark with time strictly greater than `t`.
    /// Bookmarks exactly at `t` are kept (same semantics as the external-
    /// event journal's DropAfter). Used by ResumeRecordingFrom.
    void DropAfter(const TTDTimePoint& t);

    /// @brief Drop every bookmark before `t` (the history limit evicted that part).
    void DropBefore(const TTDTimePoint& t);

    /// @brief Remove all bookmarks. Used by StartRecording /
    /// InvalidateSession / DeserializeSession.
    void Clear();

    /// @brief Number of bookmarks. Thread-safe.
    size_t Size() const;

    /// @brief True iff Size() == 0. Thread-safe.
    bool IsEmpty() const;

private:
    mutable std::mutex _mutex;
    std::vector<TTDBookmark> _bookmarks;  // sorted by time (ascending)
};

} // namespace ttd
