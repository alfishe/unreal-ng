#pragma once

/// @file mousemanager.h
/// @brief One mouse input path per emulator: every source goes in, every mouse
/// device of the machine gets it (docs/inprogress/2026-10-02-mouse-manager/design.md §3.1).
///
/// Sources and threads:
///   - the desktop front end posts MC_MOUSE_* to the message center; this
///     manager is the emulator's only subscriber (worker thread) and hands the
///     input to DebugMouseManager's host methods;
///   - automation (WebAPI, MCP, CLI, Lua, Python) calls DebugMouseManager.
/// DebugMouseManager journals the input and submits it through
/// TimeTravelManager; ttd::ApplyInputEvent then calls Apply* here, on the
/// emulator thread, and the manager fans the input out to its sinks. Replayed
/// input takes the same Apply* path.
///
/// Buttons have two sources that must not overwrite each other: the host's
/// physical buttons (an absolute set) and automation's press / release / click.
/// Each source keeps its own set of held buttons here; what the machine gets is
/// their union (ComposeButtons), so an automation press survives a host button
/// change and the other way round.
///
/// Worked example: automation presses left (automation {L}); the user then
/// presses and releases the right button on the host (host {R}, then {}). The
/// machine sees L, L+R, L: left stays held until automation releases it.

#include <cstdint>
#include <mutex>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"

class EmulatorContext;
class IMouseSink;

class MouseManager : public Observer
{
public:
    enum class ButtonSource : uint8_t
    {
        Host,
        Automation,
    };

    explicit MouseManager(EmulatorContext* context);
    ~MouseManager();

    MouseManager(const MouseManager&) = delete;
    MouseManager& operator=(const MouseManager&) = delete;

    /// region <Devices>
    /// A mouse device of the machine. The device unregisters before it goes away
    void AddSink(IMouseSink* sink);
    void RemoveSink(IMouseSink* sink);
    /// Some device of the machine can be read: capturing the host mouse makes sense
    bool HasMouseDevice() const;
    /// endregion </Devices>

    /// region <Apply: emulator thread (ttd::ApplyInputEvent), one input to every device>
    void ApplyMotion(int dx, int dy);
    void ApplyButtons(uint8_t activeLowMask);
    void ApplyWheel(int steps);
    void ApplyCounters(uint8_t x, uint8_t y);
    /// endregion </Apply>

    /// region <Buttons from two sources>
    /// Store `pressedBits` (bit set = held: D0 left, D1 right, D2 middle) as the
    /// buttons `source` holds; returns the active-low mask of both sources together
    uint8_t ComposeButtons(ButtonSource source, uint8_t pressedBits);
    /// The buttons `source` holds (bit set = held)
    uint8_t PressedBits(ButtonSource source) const;
    /// Forget both sources (the machine's buttons were set from elsewhere: TTD seek, replay)
    void ClearButtonSources();
    /// endregion </Buttons>

    /// Ignore the front end's MC_MOUSE_* events: a ZX-Poly member gets its
    /// mouse input from its group, at frame boundaries
    void SetHostInputGated(bool gated);

    // Message center callbacks (the desktop front end)
    void OnMouseMove(int id, Message* message);
    void OnMouseButton(int id, Message* message);
    void OnMouseWheel(int id, Message* message);

private:
    bool AcceptsHostEvents() const;

    EmulatorContext* _context = nullptr;

    mutable std::mutex _mutex;  // sinks and button sources: emulator, worker and automation threads
    std::vector<IMouseSink*> _sinks;
    uint8_t _hostPressed = 0;
    uint8_t _automationPressed = 0;
    bool _hostInputGated = false;
};
