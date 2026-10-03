#pragma once

/// @file ttdeventlog.h
/// @brief The engine's sparse event stream (D24): input, markers and facts,
/// one record each, ordered by (machine time, seq). Dense bus data (IN / OUT
/// values, vectors, DMA) lives in the bus journals, not here.
/// Design: docs/inprogress/2026-09-25-ttd-v2-migration/phase-3-replay-inputs-tdd.md §4.2

#include <cstdint>
#include <vector>

#include "debugger/ttd/engine/ttdpayloadstore.h"
#include "debugger/ttd/engine/ttdtime.h"

namespace ttd
{

struct TTDInputEvent;
struct TTDNetInput;

/// Stable: stored in files, appended, never reused. A v1 input kind keeps its
/// number; a v1 marker adds 0x0100
enum class TTDEventKind : uint16_t
{
    // 0x0000-0x00FF: input, the value of TTDInputKind (Key = 0 ... FrontPanelSwitch = 15)
    // 0x0100-0x01FF: markers, 0x0100 + TTDExternalEventKind
    TapeControl = 0x0100,
    MediaWrite = 0x0101,
    DebuggerEdit = 0x0102,
    HardwareReset = 0x0103,   ///< v1 files only: a reset ends the session (D39)
    OtherMarker = 0x01FF,
    // 0x0200-0x02FF: frame-boundary cuts (0x0203 reserved: device-set changes, D38)
    SnapshotLoad = 0x0200,
    MediaChange = 0x0201,
    ConfigChange = 0x0202,
    // 0x0300-0x03FF: facts the machine produces itself
    ClockChange = 0x0300,
    FrameLengthChange = 0x0301,
    ReplaySourceChange = 0x0302,
    InterruptFrame = 0x0303,
};

/// The input kinds share TTDInputKind's numbers
constexpr TTDEventKind InputEventKind(uint8_t inputKind) { return static_cast<TTDEventKind>(inputKind); }
constexpr bool IsInputKind(TTDEventKind kind) { return static_cast<uint16_t>(kind) < 0x0100; }

enum class TTDApplyPoint : uint8_t
{
    FrameBoundary,
    InstructionBoundary,
    OnAccess,
    None,   ///< a fact or a barrier: not applied
};

/// What an event means for a replay. A replay is sealed: the machine is cut
/// off from the outside world and fed only what the session recorded, until
/// the replay ends. Input drives the machine at its point; a Cut ends the
/// frame and is followed by a checkpoint; a Fact is produced by the emulation
/// and compared. Barrier exists only for records read from v1 files that lack
/// their data (an edit without its bytes, a v1 reset, an unclassified
/// marker): a seek stops before it
enum class TTDEventRole : uint8_t
{
    Input,
    Cut,
    Fact,
    Barrier,
};

/// The CPU whose instruction boundary applies an event (Phase 3, Step 3)
enum class TTDCpuId : uint16_t
{
    Main = 0,
    GeneralSoundZ80 = 1,
    NeoGSZ80 = 2,
    Atm2KeyboardMcs51 = 3,
    Vdac2Ft812 = 4,
};

struct TTDEvent
{
    TTDMachineTime machineTime = 0;
    uint32_t seq = 0;   ///< order among events at the same machine time (recording order)
    TTDEventKind kind = TTDEventKind::OtherMarker;
    TTDCpuId cpu = TTDCpuId::Main;
    uint8_t args[16] = {};   ///< kind-specific (an input event's fields, a network record)
    TTDPayloadRef payload;   ///< network bytes, edit bytes, a marker's reason
};

class TTDEventLog
{
public:
    explicit TTDEventLog(TTDPayloadStore& payloads) : _payloads(payloads) {}
    ~TTDEventLog() { Clear(); }
    TTDEventLog(const TTDEventLog&) = delete;
    TTDEventLog& operator=(const TTDEventLog&) = delete;

    /// Append an event; it takes over one reference of its payload. Refused
    /// (false, payload released) when it lies before the last event
    bool Append(TTDEvent ev);

    size_t Count() const { return _events.size(); }
    const TTDEvent& At(size_t index) const { return _events[index]; }
    const std::vector<TTDEvent>& Events() const { return _events; }

    /// Index of the first event at or after @p t (Count() when none)
    size_t CursorAt(TTDMachineTime t) const;
    /// The first barrier in (from, to], or null
    const TTDEvent* FirstBarrierIn(TTDMachineTime from, TTDMachineTime to) const;

    static TTDApplyPoint PointOf(TTDEventKind kind);
    /// The role of @p ev. Tape control is input (the deck is part of the
    /// machine); a media write is a fact (the replay writes again, into the
    /// held overlay, FR-20; what the CPU read from the medium comes from the
    /// bus journals); a debugger edit is input with its bytes. A barrier is
    /// only a v1 record without its data
    static TTDEventRole RoleOf(const TTDEvent& ev);

    /// Drop every event, releasing their payloads
    void Clear();

    /// Pack / unpack v1's records (kind numbers equal, fields in args)
    static TTDEvent FromInput(TTDMachineTime t, const TTDInputEvent& in);
    static void ToInput(const TTDEvent& ev, TTDInputEvent& out);
    static void PackNet(const TTDNetInput& net, TTDEvent& ev);
    static void UnpackNet(const TTDEvent& ev, TTDNetInput& net);

private:
    TTDPayloadStore& _payloads;
    std::vector<TTDEvent> _events;
};

}  // namespace ttd
