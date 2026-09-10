#pragma once

/// @file ttd_external_events.h
/// @brief TTD external-event journal — replay barriers for sources of
///        nondeterminism that aren't covered by an input journal in v1.
///
/// Per parent TDD §5.1 (External-Event Markers):
///   "Any nondeterministic event that is not covered by a journal gets an
///    ExternalEvent marker on the timeline instead of silently corrupting
///    replay: tape control commands, debugger-initiated state edits, disk
///    writes (Phase 1), anything added later before its journal exists."
///
/// Rules (TDD §5.1):
///   - A marker either invalidates the session (4.2) or, where the past
///     remains valid, becomes a replay barrier.
///   - SeekTo refuses to cross a barrier silently: it stops at the marker's
///     time and surfaces the marker (kind + reason) to the caller.
///   - Markers are visible on the timeline widget so the user understands
///     why history "ends" where it does at the replay level.
///   - This keeps TTD honest: it never pretends to reproduce what it
///     cannot. Journals (input today, disk/tape later) progressively
///     convert marker classes into replayable events.
///
/// This file owns the marker data structure. It does NOT own:
///   - The capture call sites (those will live in Tape, BetaDisk, etc.
///     and call Record() via the TimeTravelManager facade).
///   - The seek-time barrier logic (that lives in TimeTravelManager::
///     SeekTo, which calls FirstMarkerInInterval() to decide whether
///     replay would cross a marker).
///
/// Record format: TTDExternalEvent is 80 bytes on 64-bit
/// (8 + 4 + 1 + padding + 64-byte inline reason). At ~1 marker/sec
/// sustained (heavy tape use), a 5-minute session is ~300 events =
/// ~24 KB. Negligible vs. the page store budget.
///
/// Thread model: Record() is callable from any thread — emulator thread
/// (tape/disk hooks), WebAPI IO thread (DirectWriteToZ80Memory →
/// RecordExternalEvent), Lua thread (poke), or the control thread.
/// All mutations (Record, DropAfter, Clear) and read access (Size,
/// FirstMarkerInInterval, SnapshotEvents) are guarded by an internal
/// mutex. Callers that hold a reference from Events() must not iterate
/// across a Record/DropAfter/Clear call; use SnapshotEvents() instead,
/// which returns a stable copy under the lock.
///
/// Background: the WebAPI /memory/write handler calls MarkDirty +
/// RecordExternalEvent from Drogon's IO thread. Multiple parallel
/// requests therefore race on push_back — the mutex prevents heap
/// corruption from reallocation happening mid-call.

#include <cstdint>
#include <cstddef>
#include <mutex>
#include <vector>

#include "ttd_checkpoint.h"  // TTDTimePoint

namespace ttd {

/// @brief Classification of external events.
///
/// Stored alongside each event so the UI / caller can distinguish marker
/// sources without parsing the reason string. New values append to this
/// enum; existing values are not renumbered (they appear in automation
/// JSON per parent TDD §10.4).
enum class TTDExternalEventKind : uint8_t
{
    TapeControl    = 0,  ///< Tape play/stop/rewind/etc. (TapeManager API)
    DiskWrite      = 1,  ///< TR-DOS / +D / BetaDisk write command
    DebuggerEdit   = 2,  ///< User changed a register / memory via debugger
    HardwareReset  = 3,  ///< Emulator::Reset() — state teleport (replay barrier)
    Other          = 255 ///< Unclassified (future-proof extension point)
};

/// @brief A single external-event marker on the timeline.
///
/// Markers are nondeterminism barriers: replay cannot reproduce them, so
/// SeekTo stops at the marker instead of crossing it silently. The marker
/// captures *what* happened and *when*, so the UI can show "Tape play at
/// frame 1234 — replay ends here" rather than a cryptic seek failure.
///
/// `reason` is a short human-readable string stored inline (no heap alloc
/// per event). 63 chars + NUL terminator fits "Tape play pressed",
/// "WD1793 write command 0xF8", etc. Longer strings truncate at 63 chars.
struct TTDExternalEvent
{
    TTDTimePoint time;                                  ///< When the event occurred.
    TTDExternalEventKind kind = TTDExternalEventKind::Other;
    char reason[64] = {};                               ///< Truncated short description.
};

/// @brief Append-only journal of TTDExternalEvents, queryable by interval.
///
/// Storage is a plain std::vector — events are rare (compared to per-t-
/// state work) and the linear scan over a few hundred markers is cheap.
///
/// Ordering invariant: the journal is kept sorted by `time` (ascending).
/// Record() enforces this by trusting the caller — events arrive in
/// emulator-time order, which is monotonic. A non-monotonic insert is
/// logged and dropped at the call site (TimeTravelManager::RecordExternalEvent).
///
/// Resume-from-past truncation (Item 5) calls DropAfter() to clip the
/// journal to a new end position, matching how the timeline and input
/// journal are clipped.
class TTDExternalEventJournal
{
public:
    TTDExternalEventJournal() = default;
    ~TTDExternalEventJournal() = default;

    TTDExternalEventJournal(const TTDExternalEventJournal&) = delete;
    TTDExternalEventJournal& operator=(const TTDExternalEventJournal&) = delete;

    // -----------------------------------------------------------------------
    // Capture path (emulator thread or control thread under pause)
    // -----------------------------------------------------------------------

    /// @brief Append a marker to the journal.
    ///
    /// Caller (TimeTravelManager::RecordExternalEvent) is responsible for
    /// monotonicity checking — this method just appends. Idempotent in the
    /// sense that recording the same event twice produces two entries; the
    /// caller's monotonicity guard rejects the second one before it gets here.
    void Record(TTDExternalEvent ev);

    // -----------------------------------------------------------------------
    // Seek path (control thread; emulator paused between RunTStates batches)
    // -----------------------------------------------------------------------

    /// @brief Read-only access to the full marker list. UNSAFE for
    /// cross-thread iteration — caller must hold no concurrent
    /// Record/DropAfter/Clear. Prefer SnapshotEvents() when reading
    /// from a non-emulator thread.
    inline const std::vector<TTDExternalEvent>& Events() const { return _events; }

    /// @brief Thread-safe snapshot of the marker list. Returns a copy
    /// taken under the mutex, so callers can iterate safely even while
    /// another thread records new markers. Use from any non-emulator
    /// thread (WebAPI, Lua, Python, UI).
    inline std::vector<TTDExternalEvent> SnapshotEvents() const
    {
        std::lock_guard<std::mutex> lk(_mutex);
        return _events;
    }

    /// @brief Number of markers currently in the journal. Thread-safe.
    inline size_t Size() const
    {
        std::lock_guard<std::mutex> lk(_mutex);
        return _events.size();
    }

    /// @brief True iff Size() == 0. Thread-safe.
    inline bool IsEmpty() const
    {
        std::lock_guard<std::mutex> lk(_mutex);
        return _events.empty();
    }

    /// @brief Find the first marker strictly inside the half-open interval
    /// `(from, to]`.
    ///
    /// Used by SeekTo (Item 6) to decide whether intra-frame replay from
    /// a restored checkpoint would cross a marker. If the result is
    /// non-empty, SeekTo stops at the marker instead of advancing to `to`.
    ///
    /// "Strictly inside" means `from < m.time <= to`. Markers at `from`
    /// itself are NOT considered crossing — they happened at or before the
    /// restore point, so their effect is already in the checkpoint.
    ///
    /// Returns nullptr when no marker falls in the interval.
    const TTDExternalEvent* FirstMarkerInInterval(const TTDTimePoint& from,
                                                  const TTDTimePoint& to) const;

    // -----------------------------------------------------------------------
    // Lifecycle (control thread; emulator paused)
    // -----------------------------------------------------------------------

    /// @brief Drop every marker with time strictly greater than `t`.
    ///
    /// Used by Resume-from-past (Item 5): when the user resumes from a
    /// Detached position T, history > T is discarded, and so are markers
    /// recorded after T. Markers exactly at T are kept.
    void DropAfter(const TTDTimePoint& t);

    /// @brief Drop all markers. Called by InvalidateSession and StartRecording.
    void Clear();

private:
    mutable std::mutex _mutex;
    std::vector<TTDExternalEvent> _events;
};

/// @brief Stable string identifier for a TTDExternalEventKind.
/// Values ("tape_control" / "disk_write" / "debugger_edit" / "other") are
/// part of the automation contract per parent TDD §10.4.
const char* TTDExternalEventKindToString(TTDExternalEventKind kind);

} // namespace ttd
