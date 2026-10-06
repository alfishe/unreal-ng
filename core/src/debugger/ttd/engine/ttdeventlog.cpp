#include "debugger/ttd/engine/ttdeventlog.h"

#include <algorithm>
#include <cstring>

#include "debugger/ttd/ttdinputjournal.h"

namespace ttd
{

namespace
{
template <typename T>
void Put(uint8_t* args, size_t at, T v)
{
    std::memcpy(args + at, &v, sizeof(T));
}
template <typename T>
T Get(const uint8_t* args, size_t at)
{
    T v;
    std::memcpy(&v, args + at, sizeof(T));
    return v;
}
}  // namespace

bool TTDEventLog::Append(TTDEvent ev)
{
    if (!_events.empty() && ev.machineTime < _events.back().machineTime)
    {
        _payloads.Release(ev.payload);
        return false;
    }
    ev.seq = (!_events.empty() && _events.back().machineTime == ev.machineTime) ? _events.back().seq + 1 : 0;
    _events.push_back(ev);
    return true;
}

void TTDEventLog::DropBefore(TTDMachineTime t)
{
    const size_t n = CursorAt(t);
    for (size_t i = 0; i < n; ++i)
        _payloads.Release(_events[i].payload);
    _events.erase(_events.begin(), _events.begin() + static_cast<std::ptrdiff_t>(n));
}

void TTDEventLog::DropAfter(TTDMachineTime t)
{
    const auto it = std::upper_bound(_events.begin(), _events.end(), t,
                                     [](TTDMachineTime v, const TTDEvent& e) { return v < e.machineTime; });
    for (auto e = it; e != _events.end(); ++e)
        _payloads.Release(e->payload);
    _events.erase(it, _events.end());
}

size_t TTDEventLog::CursorAt(TTDMachineTime t) const
{
    auto it = std::lower_bound(_events.begin(), _events.end(), t,
                               [](const TTDEvent& e, TTDMachineTime v) { return e.machineTime < v; });
    return static_cast<size_t>(it - _events.begin());
}

bool TTDEventLog::HasCutAt(TTDMachineTime t) const
{
    for (size_t i = CursorAt(t); i < _events.size() && _events[i].machineTime == t; ++i)
        if (RoleOf(_events[i]) == TTDEventRole::Cut)
            return true;
    return false;
}

const TTDEvent* TTDEventLog::FirstBarrierIn(TTDMachineTime from, TTDMachineTime to) const
{
    for (size_t i = CursorAt(from); i < _events.size() && _events[i].machineTime <= to; ++i)
        if (_events[i].machineTime > from && RoleOf(_events[i]) == TTDEventRole::Barrier)
            return &_events[i];
    return nullptr;
}

TTDApplyPoint TTDEventLog::PointOf(TTDEventKind kind)
{
    if (IsInputKind(kind) || kind == TTDEventKind::DebuggerEdit || kind == TTDEventKind::TapeControl ||
        kind == TTDEventKind::ReplaySourceChange)
        return TTDApplyPoint::InstructionBoundary;
    switch (kind)
    {
        case TTDEventKind::SnapshotLoad:
        case TTDEventKind::MediaChange:
        case TTDEventKind::ConfigChange:
            return TTDApplyPoint::FrameBoundary;
        default:
            return TTDApplyPoint::None;
    }
}

TTDEventRole TTDEventLog::RoleOf(const TTDEvent& ev)
{
    if (IsInputKind(ev.kind))
        return TTDEventRole::Input;
    switch (ev.kind)
    {
        case TTDEventKind::TapeControl:
            return TTDEventRole::Input;
        case TTDEventKind::MediaWrite:
            return TTDEventRole::Fact;
        case TTDEventKind::DebuggerEdit:
            // A v1 edit has only its reason, not the bytes
            return ev.payload && ev.args[0] == kEditCarriesData ? TTDEventRole::Input : TTDEventRole::Barrier;
        case TTDEventKind::SnapshotLoad:
        case TTDEventKind::MediaChange:
        case TTDEventKind::ConfigChange:
            return TTDEventRole::Cut;
        case TTDEventKind::ClockChange:
        case TTDEventKind::FrameLengthChange:
        case TTDEventKind::InterruptFrame:
        case TTDEventKind::ReplaySourceChange:
            return TTDEventRole::Fact;
        default:   // v1 records without their data: a reset (D39), an unclassified marker
            return TTDEventRole::Barrier;
    }
}

void TTDEventLog::Clear()
{
    for (const TTDEvent& e : _events)
        _payloads.Release(e.payload);
    _events.clear();
}

// args layout of an input event: key u8 @0, pressed u8 @1, dx i16 @2, dy i16 @4,
// buttonMask u8 @6, wheelSteps i8 @7, value u8 @8, netIndex u32 @12 (v1's
// network table index; the engine's network fields are packed by PackNet)
TTDEvent TTDEventLog::FromInput(TTDMachineTime t, const TTDInputEvent& in)
{
    TTDEvent ev;
    ev.machineTime = t;
    ev.kind = InputEventKind(static_cast<uint8_t>(in.kind));
    Put<uint8_t>(ev.args, 0, in.key);
    Put<uint8_t>(ev.args, 1, in.pressed ? 1 : 0);
    Put<int16_t>(ev.args, 2, in.dx);
    Put<int16_t>(ev.args, 4, in.dy);
    Put<uint8_t>(ev.args, 6, in.buttonMask);
    Put<int8_t>(ev.args, 7, in.wheelSteps);
    Put<uint8_t>(ev.args, 8, in.value);
    return ev;
}

void TTDEventLog::ToInput(const TTDEvent& ev, TTDInputEvent& out)
{
    out.kind = static_cast<TTDInputKind>(static_cast<uint16_t>(ev.kind));
    out.key = Get<uint8_t>(ev.args, 0);
    out.pressed = Get<uint8_t>(ev.args, 1) != 0;
    out.dx = Get<int16_t>(ev.args, 2);
    out.dy = Get<int16_t>(ev.args, 4);
    out.buttonMask = Get<uint8_t>(ev.args, 6);
    out.wheelSteps = Get<int8_t>(ev.args, 7);
    out.value = Get<uint8_t>(ev.args, 8);
}

// A network event: socket u16 @0, event u8 @2, status u8 @3, addr u32 @4, port u16 @8;
// the received bytes are the event's payload
void TTDEventLog::PackNet(const TTDNetInput& net, TTDEvent& ev)
{
    Put<uint16_t>(ev.args, 0, net.socket);
    Put<uint8_t>(ev.args, 2, net.event);
    Put<uint8_t>(ev.args, 3, net.status);
    Put<uint32_t>(ev.args, 4, net.addr);
    Put<uint16_t>(ev.args, 8, net.port);
}

void TTDEventLog::UnpackNet(const TTDEvent& ev, TTDNetInput& net)
{
    net.socket = Get<uint16_t>(ev.args, 0);
    net.event = Get<uint8_t>(ev.args, 2);
    net.status = Get<uint8_t>(ev.args, 3);
    net.addr = Get<uint32_t>(ev.args, 4);
    net.port = Get<uint16_t>(ev.args, 8);
}

}  // namespace ttd
