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
/// Lifecycle (owned by TimeTravelManager, same discipline as the other
/// journals):
///   - StartRecording / InvalidateSession — Clear() (a bookmark into wiped
///     history would dangle);
///   - ResumeRecordingFrom — DropAfter(from) (bookmarks in the discarded
///     future are dead; bookmarks exactly at `from` are kept);
///   - SerializeSession / DeserializeSession — persisted as a flag-gated
///     section (ttd::dump::kFlagsHasBookmarks), schema-additive like the
///     coverage index.

#include <cstddef>
#include <cstdint>
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

/// @brief A single named timeline position.
struct TTDBookmark
{
    TTDTimePoint time;   ///< When the bookmark points at.
    std::string  label;  ///< Unique, non-empty, <= kMaxBookmarkLabelLength chars.
};

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

    /// @brief Remove the bookmark with this label. Returns false when absent.
    bool Remove(const std::string& label);

    /// @brief Drop every bookmark with time strictly greater than `t`.
    /// Bookmarks exactly at `t` are kept (same semantics as the external-
    /// event journal's DropAfter). Used by ResumeRecordingFrom.
    void DropAfter(const TTDTimePoint& t);

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
