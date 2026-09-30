#pragma once

/// @file ttdinputjournal.h
/// @brief TTD input event journal — captures keyboard matrix, Kempston Mouse
///        and automation-driven General Sound host-port mutations for
///        deterministic replay.
///
/// Per parent TDD §5 row #1 and §5.1:
///   "Host key events arrive asynchronously via MessageCenter and mutate the
///    matrix between instructions. Journal entries: (TTDTimePoint, key,
///    press/release) recorded when the matrix mutation is applied. On replay,
///    the TTD engine injects matrix changes at the recorded points instead
///    of live input (live input is suppressed during replay)."
///
/// This file owns the journal data structure only. It does NOT own:
///   - The capture call sites (live input reaches the journal through
///     TimeTravelManager::SubmitLiveInput / ApplyLiveInput).
///   - Applying an event to a device (ttdinputapply.h: ApplyInputEvent, shared
///     by live input and playback).
///   - Playback timing (TimeTravelManager::ServiceInput walks a cursor through
///     Events(), positioned with FirstIndexAtOrAfter).
///
/// Record format: TTDInputEvent is 17 bytes on 64-bit (8 + 4 + 1 + padding).
/// At ~10 keys/sec sustained typing, a 5-minute session is ~3 000 events =
/// ~50 KB. Negligible vs. the page store budget.
///
/// Thread model: Record() and ApplyInputEvent() run on the thread executing the machine
/// (the emulator loop, or the caller of a synchronous Emulator::Run*). Live
/// input from other threads (MessageCenter host keys/mouse, automation) is
/// queued by TimeTravelManager::SubmitLiveInput and applied + journaled there
/// at an instruction boundary, so a journal entry's time IS the moment the
/// program could first observe the change - which is what makes replay exact.
/// DropAfter / Clear run on the control thread under pause. No internal
/// locking is required.

#include <cstdint>
#include <cstddef>
#include <vector>

#include "ttdcheckpoint.h"  // TTDTimePoint

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
/// @brief What kind of input device mutation an event records.
///
/// One ordered journal for every device (Kempston Mouse design §6.2, option (a)
/// "discriminated union"): the ascending-time invariant and the replay merge stay
/// trivially correct with a single stream.
///
/// Adding an input device (e.g. a Kempston joystick fed from host controllers):
/// add a kind here and its case in ApplyInputEvent (ttdinputapply.h; a new device
/// is a field in TTDInputDevices), and route every live
/// source through TimeTravelManager::SubmitLiveInput - never mutate the device
/// directly. That one path gives it replay ownership (live input refused while
/// the journal drives the machine), marshalling onto the machine's thread and an
/// exact journal time. (A Sinclair joystick is keyboard keys: no new kind.)
enum class TTDInputKind : uint8_t
{
    Key = 0,            ///< keyboard matrix press / release (key, pressed)
    MouseMove,          ///< Kempston Mouse relative move (dx, dy; +y = up)
    MouseButtons,       ///< Kempston Mouse button mask (buttonMask, active-low)
    MouseWheel,         ///< Kempston Mouse wheel notches (wheelSteps)
    MouseCounters,      ///< Kempston Mouse absolute X/Y counter write (dx = x, dy = y)
    KeyboardReset,      ///< whole keyboard matrix released (DebugKeyboardManager::ReleaseAllKeys)

    // General Sound host-side stimuli from automation (the card's own Z80 is
    // stepped by these calls, so they must run on the machine's thread; a ZX
    // program's OUT reaches the card through the port decoder instead)
    GSCommand,          ///< OUT #BB (value)
    GSData,             ///< OUT #B3 (value)
    GSNmi,              ///< #33 bit 6: NMI to the card CPU
    GSResetCard,        ///< #33 bit 7: card reset, host mailbox kept
    GSReset,            ///< full card power-on reset, mailbox included

    PcKey,              ///< physical PC key press / release (key = PcKey, pressed) for the
                        ///< machine's PS/2 controller (ZX-Evo AVR); journaled only when one
                        ///< is attached. Its own event, not derived from Key: host Up is
                        ///< Caps Shift + 7 on the matrix but E0 75 on PS/2

    // Network (network adapters TDD §6): what the host network answered, as
    // the machine's virtual network sees it. The bytes (received data) live in
    // the journal's payload store, referenced by payloadOffset / payloadLength
    NetEvent,           ///< host network event for one virtual-network socket (netIndex -> TTDNetInput + payload)
    NetLinkReset        ///< every host connection of the virtual network is gone (seek / resume from the past)
};

/// Last valid kind: the file reader refuses anything above it
constexpr TTDInputKind kLastTTDInputKind = TTDInputKind::NetLinkReset;

struct TTDInputEvent
{
    TTDTimePoint time;        ///< When the mutation was applied (frame + tInFrame)
    TTDInputKind kind = TTDInputKind::Key;
    uint8_t      key = 0;     ///< Key: ZXKeysEnum value; PcKey: PcKey value (cast at the boundary)
    bool         pressed = false;  ///< Key / PcKey: true = press, false = release
    int16_t      dx = 0;      ///< MouseMove: delta X; MouseCounters: X value
    int16_t      dy = 0;      ///< MouseMove: delta Y; MouseCounters: Y value
    uint8_t      buttonMask = 0xFF;  ///< MouseButtons: active-low mask
    int8_t       wheelSteps = 0;     ///< MouseWheel: notches
    uint8_t      value = 0;          ///< GSCommand / GSData: the byte written

    /// NetEvent: 1-based index of its record in the journal's network table
    /// (TTDNetInput); 0 = none. Network fields live outside the event so the
    /// journal of a machine without a network adapter does not grow
    uint32_t     netIndex = 0;
};

/// @brief What a NetEvent carries (network adapters TDD §6): the virtual-network
/// socket, the event (NetEventType), its status (NetEventStatus), the peer
/// (IPv4 host byte order, port) and the received bytes in the payload store.
struct TTDNetInput
{
    uint16_t socket = 0;
    uint8_t  event = 0;
    uint8_t  status = 0;
    uint32_t addr = 0;
    uint16_t port = 0;
    uint32_t payloadOffset = 0;
    uint32_t payloadLength = 0;

    /// 1-based position in the journal's network table, set by the journal
    /// (0 = not journaled: TTD not recording). Lets a device name where its
    /// buffered bytes came from, so a checkpoint stores references, not bytes
    uint32_t journalIndex = 0;
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

    /// @brief Append a NetEvent: its network record goes to the network table,
    /// its bytes to the payload store, and the event points at the record.
    void Record(TTDInputEvent ev, TTDNetInput net, const uint8_t* payload, uint32_t length);

    /// @brief The network record of a journaled NetEvent (nullptr when none)
    const TTDNetInput* NetOf(const TTDInputEvent& ev) const;

    /// @brief The bytes of a network record (nullptr when it has none)
    const uint8_t* PayloadOf(const TTDNetInput& net) const;

    /// @brief Network record by its 1-based journal index (nullptr when out of range)
    const TTDNetInput* NetAt(uint32_t journalIndex) const
    {
        return (journalIndex == 0 || journalIndex > _net.size()) ? nullptr : &_net[journalIndex - 1];
    }

    /// @brief Network table and payload store (file writer, tests)
    inline const std::vector<TTDNetInput>& NetInputs() const { return _net; }
    inline const std::vector<uint8_t>& Payload() const { return _payload; }

    /// @brief Replace events, network table and payload store together (file
    /// reader). The caller validated every index and payload range.
    void Assign(std::vector<TTDInputEvent> events, std::vector<TTDNetInput> net, std::vector<uint8_t> payload);

    // -----------------------------------------------------------------------
    // Replay path (control thread; emulator paused between RunTStates batches)
    // -----------------------------------------------------------------------

    /// @brief Read-only access to the full event list (the playback cursor in
    /// TimeTravelManager::ServiceInput walks it; tests read it too).
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

    /// @brief Index of the first event with time >= `t` (Size() when none) -
    /// the replay cursor for a machine positioned at `t`.
    size_t FirstIndexAtOrAfter(const TTDTimePoint& t) const;

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

    /// Network records of NetEvents and their bytes, in journal order
    /// (append-only; DropAfter cuts both back to the last kept event)
    std::vector<TTDNetInput> _net;
    std::vector<uint8_t> _payload;
};

} // namespace ttd
