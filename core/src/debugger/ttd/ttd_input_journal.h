#pragma once

/// @file ttd_input_journal.h
/// @brief TTD input event journal — captures keyboard matrix mutations for
///        deterministic replay.
///
/// Per parent TDD §5 row #1 and §5.1:
///   "Host key events arrive asynchronously via MessageCenter and mutate the
///    matrix between instructions. Journal entries: (TTDTimePoint, key,
///    press/release) recorded when the matrix mutation is applied. On replay,
///    the TTD engine injects matrix changes at the recorded points instead
///    of live input (live input is suppressed during replay)."
///
/// This file owns the journal data structure. It does NOT own:
///   - The capture call sites (those live in DebugKeyboardManager and call
///     Record() via the TimeTravelManager facade).
///   - The replay-time injection coordination (that lives in the seek engine,
///     Item 4 — see InjectDueEvents / PeekNextEventTimeOnOrAfter).
///
/// Record format: TTDInputEvent is 17 bytes on 64-bit (8 + 4 + 1 + padding).
/// At ~10 keys/sec sustained typing, a 5-minute session is ~3 000 events =
/// ~50 KB. Negligible vs. the page store budget.
///
/// Thread model: Record() runs on the host input thread (whoever calls
/// DebugKeyboardManager::PressKey). Read access (EventsUpTo, Size,
/// PeekNextEventTimeOnOrAfter) runs on the control thread during seek.
/// DropAfter / Clear run on the control thread under pause. The existing
/// MessageCenter delivery already serializes host key events onto a single
/// thread, so no internal locking is required — matching the page store's
/// threading model.

#include <cstdint>
#include <cstddef>
#include <vector>

#include "ttd_checkpoint.h"  // TTDTimePoint

// Forward declaration — full Keyboard type pulled in by the .cpp only.
class Keyboard;

namespace ttd {

/// @brief A single keyboard matrix mutation captured for replay.
///
/// `key` is the ZXKeysEnum value (uint8_t). We deliberately use uint8_t
/// here rather than the actual enum type to avoid pulling keyboard.h into
/// every translation unit that includes this header. Callers cast at the
/// boundary.
///
/// `pressed` is true for press events, false for release events.
///
/// Ordering invariant: the journal is kept sorted by `time` (ascending).
/// Record() enforces this by trusting the caller — host events arrive in
/// wall-clock order, which under the emulator's pause/pacing maps to
/// monotonic TTDTimePoint order. A non-monotonic insert is logged and
/// dropped at the call site (TimeTravelManager::RecordInputEvent) rather
/// than silently corrupting the journal.
struct TTDInputEvent
{
    TTDTimePoint time;        ///< When the mutation was applied (frame + tInFrame)
    uint8_t      key = 0;     ///< ZXKeysEnum value (cast at the boundary)
    bool         pressed = false;  ///< true = press, false = release
};

/// @brief Append-only journal of TTDInputEvents, queryable by TTDTimePoint.
///
/// Storage is a plain std::vector — the record format is small enough and
/// events are rare enough (relative to per-t-state work) that a flat
/// sorted vector with linear/binary search beats a tree or ring buffer.
///
/// Resume-from-past truncation (Item 5) calls DropAfter() to clip the
/// journal to a new end position.
class TTDInputJournal
{
public:
    TTDInputJournal() = default;
    ~TTDInputJournal() = default;

    TTDInputJournal(const TTDInputJournal&) = delete;
    TTDInputJournal& operator=(const TTDInputJournal&) = delete;

    // -----------------------------------------------------------------------
    // Capture path (host input thread; emulator paused or running)
    // -----------------------------------------------------------------------

    /// @brief Append an event to the journal.
    ///
    /// Caller (TimeTravelManager::RecordInputEvent) is responsible for
    /// monotonicity checking — this method just appends. Idempotent in the
    /// sense that recording the same event twice produces two entries; the
    /// caller's monotonicity guard rejects the second one before it gets here.
    void Record(TTDInputEvent ev);

    // -----------------------------------------------------------------------
    // Replay path (control thread; emulator paused between RunTStates batches)
    // -----------------------------------------------------------------------

    /// @brief Read-only access to the full event list. Used by tests and by
    /// the future seek engine when it needs to iterate manually.
    inline const std::vector<TTDInputEvent>& Events() const { return _events; }

    /// @brief Number of events currently in the journal.
    inline size_t Size() const { return _events.size(); }

    /// @brief True iff Size() == 0.
    inline bool IsEmpty() const { return _events.empty(); }

    /// @brief Get the time of the first event at-or-after `from`.
    ///
    /// Returns a default-constructed (frame=0, tInFrame=0) TTDTimePoint when
    /// no event matches — caller should treat that as "no pending event".
    /// (frame=0 is a valid time but only matches the session's first
    /// instruction; the seek engine never asks "anything due after the
    /// session start?", so the ambiguity is harmless in practice. A more
    /// defensive return type would be std::optional<TTDTimePoint>; we keep
    /// the plain struct for now to match the rest of the TTD API.)
    TTDTimePoint PeekNextEventTimeOnOrAfter(const TTDTimePoint& from) const;

    /// @brief Inject every event with time == `now` into the live keyboard.
    ///
    /// "Equal to now" (not "<=") is the precise replay semantics: each
    /// event is injected exactly once, at the moment the emulated clock
    /// reaches its recorded TTDTimePoint. The seek engine calls this after
    /// every RunTStates step that crosses an event boundary, passing the
    /// exact TTDTimePoint of the step's end.
    ///
    /// Returns the number of events injected (for diagnostics).
    ///
    /// Defined out-of-line in the .cpp so the Keyboard type stays forward-
    /// declared in this header.
    size_t InjectDueEvents(Keyboard& keyboard, const TTDTimePoint& now);

    // -----------------------------------------------------------------------
    // Lifecycle (control thread; emulator paused)
    // -----------------------------------------------------------------------

    /// @brief Drop every event with time strictly greater than `t`.
    ///
    /// Used by Resume-from-past (Item 5): when the user resumes from a
    /// Detached position T, history > T is discarded, and so are input
    /// events recorded after T. Events exactly at T are kept (they're part
    /// of the past).
    void DropAfter(const TTDTimePoint& t);

    /// @brief Drop all events. Called by InvalidateSession and StartRecording.
    void Clear();

private:
    std::vector<TTDInputEvent> _events;
};

} // namespace ttd
