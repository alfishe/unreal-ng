#include "stdafx.h"

#include "mousemanager.h"

#include <algorithm>

#include "debugger/debugmanager.h"
#include "debugger/mouse/debugmousemanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/mouse/imousesink.h"
#include "emulator/io/mouse/mouse.h"

namespace
{
constexpr uint8_t kAllButtons = 0x07;

/// Host input goes through the debug mouse manager when there is one (replay
/// guard, TTD journal, button sources); bare contexts (unit tests without a
/// DebugManager) apply directly
DebugMouseManager* HostInputFunnel(EmulatorContext* context)
{
    if (context && context->pDebugManager)
        return context->pDebugManager->GetMouseManager();
    return nullptr;
}
}  // namespace

MouseManager::MouseManager(EmulatorContext* context) : _context(context)
{
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observer = static_cast<Observer*>(this);
    messageCenter.AddObserver(MC_MOUSE_MOVE, observer, static_cast<ObserverCallbackMethod>(&MouseManager::OnMouseMove));
    messageCenter.AddObserver(MC_MOUSE_BUTTON, observer, static_cast<ObserverCallbackMethod>(&MouseManager::OnMouseButton));
    messageCenter.AddObserver(MC_MOUSE_WHEEL, observer, static_cast<ObserverCallbackMethod>(&MouseManager::OnMouseWheel));
}

MouseManager::~MouseManager()
{
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observer = static_cast<Observer*>(this);
    messageCenter.RemoveObserver(MC_MOUSE_MOVE, observer, static_cast<ObserverCallbackMethod>(&MouseManager::OnMouseMove));
    messageCenter.RemoveObserver(MC_MOUSE_BUTTON, observer, static_cast<ObserverCallbackMethod>(&MouseManager::OnMouseButton));
    messageCenter.RemoveObserver(MC_MOUSE_WHEEL, observer, static_cast<ObserverCallbackMethod>(&MouseManager::OnMouseWheel));
}

/// region <Devices>

void MouseManager::AddSink(IMouseSink* sink)
{
    if (!sink)
        return;
    std::lock_guard<std::mutex> lock(_mutex);
    if (std::find(_sinks.begin(), _sinks.end(), sink) == _sinks.end())
        _sinks.push_back(sink);
}

void MouseManager::RemoveSink(IMouseSink* sink)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _sinks.erase(std::remove(_sinks.begin(), _sinks.end(), sink), _sinks.end());
}

std::vector<IMouseSink*> MouseManager::Sinks() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _sinks;
}

bool MouseManager::HasMouseDevice() const
{
    // Out of the lock: a sink asks the port decoder (IsMouseWired)
    const std::vector<IMouseSink*> sinks = Sinks();
    return std::any_of(sinks.begin(), sinks.end(),
                       [](const IMouseSink* sink) { return sink->IsMouseWired() && sink->IsMouseFitted(); });
}

std::vector<MouseDeviceStatus> MouseManager::DescribeDevices() const
{
    std::vector<MouseDeviceStatus> devices;
    for (const IMouseSink* sink : Sinks())
        if (sink->IsMouseWired())
            devices.push_back(sink->DescribeMouse());
    return devices;
}

std::optional<MouseDeviceStatus> MouseManager::DescribeDefaultDevice() const
{
    for (const IMouseSink* sink : Sinks())
        if (sink->IsMouseWired() && sink->IsMouseFitted())
            return sink->DescribeMouse();
    return std::nullopt;
}

std::optional<MouseDeviceStatus> MouseManager::DescribeDevice(const std::string& id) const
{
    for (MouseDeviceStatus& device : DescribeDevices())
        if (device.id == id)
            return std::move(device);
    return std::nullopt;
}

bool MouseManager::HasUnreadMotion() const
{
    const std::vector<IMouseSink*> sinks = Sinks();
    return std::any_of(sinks.begin(), sinks.end(), [](const IMouseSink* sink) {
        return sink->IsMouseWired() && sink->IsMouseFitted() && sink->IsMouseInUse() && sink->HasUnreadMotion();
    });
}

int MouseManager::MotionStepLimit() const
{
    int limit = 127;
    for (const IMouseSink* sink : Sinks())
        if (sink->IsMouseWired() && sink->IsMouseFitted())
            limit = std::min(limit, std::max(1, sink->MotionStepLimit()));
    return limit;
}

bool MouseManager::IsMouseInUse() const
{
    // The sinks are copied out of the lock: a sink asks the port decoder, which may ask back
    std::vector<IMouseSink*> sinks;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        sinks = _sinks;
    }
    return std::any_of(sinks.begin(), sinks.end(), [](const IMouseSink* sink) { return sink->IsMouseInUse(); });
}

/// endregion </Devices>

/// region <Apply>

// The sinks are copied out of the lock: a sink may ask the manager something
// (HasMouseDevice) without deadlocking

void MouseManager::ApplyMotion(int dx, int dy)
{
    std::vector<IMouseSink*> sinks;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        sinks = _sinks;
    }
    for (IMouseSink* sink : sinks)
        sink->OnMouseMotion(dx, dy);
}

void MouseManager::ApplyButtons(uint8_t activeLowMask)
{
    std::vector<IMouseSink*> sinks;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        sinks = _sinks;
    }
    for (IMouseSink* sink : sinks)
        sink->OnMouseButtons(activeLowMask);
}

void MouseManager::ApplyWheel(int steps)
{
    std::vector<IMouseSink*> sinks;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        sinks = _sinks;
    }
    for (IMouseSink* sink : sinks)
        sink->OnMouseWheel(steps);
}

void MouseManager::ApplyCounters(uint8_t x, uint8_t y)
{
    std::vector<IMouseSink*> sinks;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        sinks = _sinks;
    }
    for (IMouseSink* sink : sinks)
        sink->OnMouseCounters(x, y);
}

/// endregion </Apply>

/// region <Buttons from two sources>

uint8_t MouseManager::ComposeButtons(ButtonSource source, uint8_t pressedBits)
{
    std::lock_guard<std::mutex> lock(_mutex);
    (source == ButtonSource::Host ? _hostPressed : _automationPressed) = static_cast<uint8_t>(pressedBits & kAllButtons);
    return static_cast<uint8_t>(0xFF & ~(_hostPressed | _automationPressed));
}

uint8_t MouseManager::PressedBits(ButtonSource source) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return source == ButtonSource::Host ? _hostPressed : _automationPressed;
}

void MouseManager::ClearButtonSources()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _hostPressed = 0;
    _automationPressed = 0;
}

/// endregion </Buttons>

/// region <Host input (message center)>

void MouseManager::SetHostInputGated(bool gated)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _hostInputGated = gated;
}

bool MouseManager::AcceptsHostEvents() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return !_hostInputGated;
}

namespace
{
/// The event for this emulator of the given kind, or nullptr (wrong payload,
/// wrong kind for the topic, or tagged for another emulator)
MouseEvent* EventFor(EmulatorContext* context, Message* message, MouseEventKind kind)
{
    if (!message || !message->obj)
        return nullptr;
    auto* event = dynamic_cast<MouseEvent*>(message->obj);
    if (!event || event->kind != kind)
        return nullptr;
    if (!event->targetId.empty() && context && context->pEmulator && event->targetId != context->pEmulator->GetId())
        return nullptr;
    return event;
}
}  // namespace

void MouseManager::OnMouseMove([[maybe_unused]] int id, Message* message)
{
    MouseEvent* event = AcceptsHostEvents() ? EventFor(_context, message, MouseEventKind::Move) : nullptr;
    if (!event)
        return;
    if (DebugMouseManager* funnel = HostInputFunnel(_context))
        funnel->ApplyHostMove(event->dx, event->dy);
    else
        ApplyMotion(event->dx, event->dy);
}

void MouseManager::OnMouseButton([[maybe_unused]] int id, Message* message)
{
    MouseEvent* event = AcceptsHostEvents() ? EventFor(_context, message, MouseEventKind::Buttons) : nullptr;
    if (!event)
        return;
    if (DebugMouseManager* funnel = HostInputFunnel(_context))
        funnel->ApplyHostButtons(event->buttonMask);
    else
        ApplyButtons(ComposeButtons(ButtonSource::Host, static_cast<uint8_t>(~event->buttonMask & kAllButtons)));
}

void MouseManager::OnMouseWheel([[maybe_unused]] int id, Message* message)
{
    MouseEvent* event = AcceptsHostEvents() ? EventFor(_context, message, MouseEventKind::Wheel) : nullptr;
    if (!event)
        return;
    if (DebugMouseManager* funnel = HostInputFunnel(_context))
        funnel->ApplyHostWheel(event->wheelSteps);
    else
        ApplyWheel(event->wheelSteps);
}

/// endregion </Host input>
