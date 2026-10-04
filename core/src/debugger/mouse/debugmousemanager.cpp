#include "debugmousemanager.h"

#include <algorithm>
#include <cctype>

#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdinputapply.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/io/mouse/mousemanager.h"
#include "emulator/ports/portdecoder.h"

namespace
{
std::string RangeError(const char* name, long long value, int min, int max, const char* hint = "")
{
    return std::string(name) + "=" + std::to_string(value) + " out of range " + std::to_string(min) + ".." +
           std::to_string(max) + hint;
}

constexpr const char* kSplitHint = "; split into several moves with run_frames between them";

constexpr uint8_t kAllButtons = 0x07;
}  // namespace

DebugMouseManager::DebugMouseManager(EmulatorContext* context) : _context(context) {}

/// region <Guards and journal>

Mouse* DebugMouseManager::Device() const
{
    // Read on every call: independent of whether DebugManager or Core::Init runs first
    return _context ? _context->pMouse : nullptr;
}

bool DebugMouseManager::IsReplaying() const
{
    // The TTD journal owns input: a replay, or the machine re-executing
    // recorded history (TimeTravelManager::OwnsInput), or an RZX playback
    if (_context && _context->rzxPlayer)
        return true;
    return _context && _context->pTimeTravelManager && _context->pTimeTravelManager->OwnsInput();
}

MouseInjectResult DebugMouseManager::Guard() const
{
    MouseInjectResult result;
    Mouse* mouse = Device();
    if (!mouse)
    {
        result.status = MouseInjectStatus::NoDevice;
        result.message = "Mouse device not available";
    }
    else if (IsReplaying())
    {
        result.status = MouseInjectStatus::ReplayActive;
        result.message = "TTD replay in progress (recorded input drives the machine); live mouse input refused";
    }
    // No device the machine's ports read is fitted: the input could reach nothing (design 2026-10-03 §5).
    // A bare context (unit tests without a manager) asks the Kempston interface
    else if (_context && _context->pMouseManager ? !_context->pMouseManager->HasMouseDevice() : !mouse->IsPresent())
    {
        result.status = MouseInjectStatus::NoMouseFitted;
        result.message = "no mouse fitted on this machine: a program cannot read mouse input "
                         "([INPUT] Mouse=NONE or feature kempstonmouse off; the machine has no mouse of its own)";
    }
    return result;
}

MouseInjectResult DebugMouseManager::Success(const std::string& message) const
{
    MouseInjectResult result;
    result.message = message;
    return result;
}

MouseInjectResult DebugMouseManager::CheckDevice(const std::string& deviceId) const
{
    MouseInjectResult result;
    if (deviceId.empty())
        return result;
    std::vector<MouseDeviceStatus> devices;
    if (_context && _context->pMouseManager)
        devices = _context->pMouseManager->DescribeDevices();
    else if (Mouse* mouse = Device())
        devices.push_back(mouse->DescribeMouse());
    std::string known;
    for (const MouseDeviceStatus& device : devices)
    {
        if (device.id == deviceId)
            return result;
        known += (known.empty() ? "" : ", ") + device.id;
    }
    result.status = MouseInjectStatus::InvalidArgument;
    result.message = "Unknown mouse device '" + deviceId + "'. This machine has: " + (known.empty() ? "none" : known);
    return result;
}

// Every mouse mutation goes through TimeTravelManager::SubmitLiveInput when TTD is
// present: refused while the journal owns input, applied on the machine's thread at an
// instruction boundary and journalled there while recording (same as keyboard)
bool DebugMouseManager::Submit(const ttd::TTDInputEvent& ev, Mouse& mouse)
{
    if (_context && _context->pTimeTravelManager)
        return _context->pTimeTravelManager->SubmitLiveInput(ev);
    ttd::TTDInputDevices devices;
    devices.mouse = &mouse;
    devices.mouseManager = _context ? _context->pMouseManager : nullptr;
    return ttd::ApplyInputEvent(ev, devices);
}

// Automation's held buttons live in the mouse manager next to the host's, so
// one source never overwrites the other (mousemanager.h). A bare context
// (unit tests without a manager) reads them back from the device

uint8_t DebugMouseManager::AutomationPressed() const
{
    if (_context && _context->pMouseManager)
        return _context->pMouseManager->PressedBits(MouseManager::ButtonSource::Automation);
    const Mouse* mouse = Device();
    return mouse ? static_cast<uint8_t>(~mouse->GetButtons() & kAllButtons) : 0;
}

void DebugMouseManager::SubmitAutomationButtons(Mouse& mouse, uint8_t pressedBits)
{
    pressedBits &= kAllButtons;
    const uint8_t mask = _context && _context->pMouseManager
                             ? _context->pMouseManager->ComposeButtons(MouseManager::ButtonSource::Automation, pressedBits)
                             : static_cast<uint8_t>((mouse.GetButtons() | kAllButtons) & ~pressedBits);
    ApplyButtons(mouse, mask);
}

void DebugMouseManager::ApplyMove(Mouse& mouse, int dx, int dy)
{
    ttd::TTDInputEvent ev;
    ev.kind = ttd::TTDInputKind::MouseMove;
    ev.dx = static_cast<int16_t>(dx);
    ev.dy = static_cast<int16_t>(dy);
    Submit(ev, mouse);
}

void DebugMouseManager::ApplyButtons(Mouse& mouse, uint8_t activeLowMask)
{
    ttd::TTDInputEvent ev;
    ev.kind = ttd::TTDInputKind::MouseButtons;
    ev.buttonMask = activeLowMask;
    Submit(ev, mouse);
}

void DebugMouseManager::ApplyWheel(Mouse& mouse, int steps)
{
    ttd::TTDInputEvent ev;
    ev.kind = ttd::TTDInputKind::MouseWheel;
    ev.wheelSteps = static_cast<int8_t>(steps);
    Submit(ev, mouse);
}

void DebugMouseManager::ApplyCounters(Mouse& mouse, uint8_t x, uint8_t y)
{
    ttd::TTDInputEvent ev;
    ev.kind = ttd::TTDInputKind::MouseCounters;
    ev.dx = x;
    ev.dy = y;
    Submit(ev, mouse);
}

/// endregion </Guards and journal>

/// region <Automation - immediate>

MouseInjectResult DebugMouseManager::Move(int dx, int dy)
{
    if (MouseInjectResult guard = Guard(); !guard.ok())
        return guard;

    MouseInjectResult error;
    error.status = MouseInjectStatus::InvalidArgument;
    if (dx < -MAX_MOVE_PER_CALL || dx > MAX_MOVE_PER_CALL)
    {
        error.message = RangeError("dx", dx, -MAX_MOVE_PER_CALL, MAX_MOVE_PER_CALL, kSplitHint);
        return error;
    }
    if (dy < -MAX_MOVE_PER_CALL || dy > MAX_MOVE_PER_CALL)
    {
        error.message = RangeError("dy", dy, -MAX_MOVE_PER_CALL, MAX_MOVE_PER_CALL, kSplitHint);
        return error;
    }
    if (dx == 0 && dy == 0)
    {
        error.message = "move requires a non-zero dx or dy";
        return error;
    }

    MouseInjectResult result = Success("Mouse moved: dx=" + std::string(dx >= 0 ? "+" : "") + std::to_string(dx) +
                                       " dy=" + std::string(dy >= 0 ? "+" : "") + std::to_string(dy));
    std::lock_guard<std::mutex> lock(_mutex);
    if (!EnqueueIfBusyLocked({QueuedOp::Kind::Motion, dx, dy, 0, 0}, result))
        ApplyMove(*Device(), dx, dy);
    return result;
}

MouseInjectResult DebugMouseManager::Glide(int dx, int dy)
{
    if (MouseInjectResult guard = Guard(); !guard.ok())
        return guard;

    MouseInjectResult error;
    error.status = MouseInjectStatus::InvalidArgument;
    if (dx < -MAX_GLIDE_PER_CALL || dx > MAX_GLIDE_PER_CALL)
    {
        error.message = RangeError("dx", dx, -MAX_GLIDE_PER_CALL, MAX_GLIDE_PER_CALL);
        return error;
    }
    if (dy < -MAX_GLIDE_PER_CALL || dy > MAX_GLIDE_PER_CALL)
    {
        error.message = RangeError("dy", dy, -MAX_GLIDE_PER_CALL, MAX_GLIDE_PER_CALL);
        return error;
    }
    if (dx == 0 && dy == 0)
    {
        error.message = "glide requires a non-zero dx or dy";
        return error;
    }

    MouseInjectResult result = Success("Mouse glide: dx=" + std::string(dx >= 0 ? "+" : "") + std::to_string(dx) +
                                       " dy=" + std::string(dy >= 0 ? "+" : "") + std::to_string(dy));
    std::lock_guard<std::mutex> lock(_mutex);
    QueuedOp op{QueuedOp::Kind::Motion, dx, dy, 0, 0};
    if (EnqueueIfBusyLocked(op, result))
        return result;
    // Idle: the first step now, the rest at frame ends
    if (!ApplyMotionStepLocked(*Device(), op))
    {
        _queue.push_back(op);
        _waitFrames = 0;
        result.queued = true;
    }
    return result;
}

bool DebugMouseManager::IsBusy() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return !_queue.empty();
}

void DebugMouseManager::CancelQueue()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _queue.clear();
    _waitFrames = 0;
}

MouseInjectResult DebugMouseManager::Wheel(int steps)
{
    if (MouseInjectResult guard = Guard(); !guard.ok())
        return guard;

    if (steps == 0 || steps < -MAX_WHEEL_PER_CALL || steps > MAX_WHEEL_PER_CALL)
    {
        MouseInjectResult error;
        error.status = MouseInjectStatus::InvalidArgument;
        error.message = steps == 0 ? "wheel requires non-zero steps"
                                   : RangeError("steps", steps, -MAX_WHEEL_PER_CALL, MAX_WHEEL_PER_CALL);
        return error;
    }

    MouseInjectResult result = Success("Mouse wheel: " + std::string(steps > 0 ? "+" : "") + std::to_string(steps));
    bool wheel = false;
    if (_context && _context->pMouseManager)
    {
        for (const MouseDeviceStatus& device : _context->pMouseManager->DescribeDevices())
            wheel |= device.fitted && device.wheel;
    }
    else if (Mouse* mouse = Device())
        wheel = mouse->IsWheelEnabled();
    if (!wheel)
        result.warning = "no wheel fitted ([INPUT] Wheel=NONE, or a mouse without one): the guest does not see the wheel";

    std::lock_guard<std::mutex> lock(_mutex);
    if (!EnqueueIfBusyLocked({QueuedOp::Kind::Wheel, steps, 0, 0, 0}, result))
        ApplyWheel(*Device(), steps);
    return result;
}

MouseInjectResult DebugMouseManager::PressButton(MouseButton button)
{
    if (MouseInjectResult guard = Guard(); !guard.ok())
        return guard;

    MouseInjectResult result = Success("Mouse button pressed: " + GetButtonName(button));
    Mouse& mouse = *Device();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (EnqueueIfBusyLocked({QueuedOp::Kind::Press, 0, 0, static_cast<uint8_t>(button), 0}, result))
            return result;
        if (_pendingButton == button)
            CancelPendingLocked();  // the explicit call wins over a pending click release
        SubmitAutomationButtons(mouse, static_cast<uint8_t>(AutomationPressed() | static_cast<uint8_t>(button)));
    }
    return result;
}

MouseInjectResult DebugMouseManager::ReleaseButton(MouseButton button)
{
    if (MouseInjectResult guard = Guard(); !guard.ok())
        return guard;

    MouseInjectResult result = Success("Mouse button released: " + GetButtonName(button));
    Mouse& mouse = *Device();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (EnqueueIfBusyLocked({QueuedOp::Kind::Release, 0, 0, static_cast<uint8_t>(button), 0}, result))
            return result;
        if (_pendingButton == button)
            CancelPendingLocked();
        SubmitAutomationButtons(mouse, static_cast<uint8_t>(AutomationPressed() & ~static_cast<uint8_t>(button)));
    }
    return result;
}

MouseInjectResult DebugMouseManager::SetPressedButtons(uint8_t pressedBits)
{
    if (MouseInjectResult guard = Guard(); !guard.ok())
        return guard;

    if (pressedBits & ~kAllButtons)
    {
        MouseInjectResult error;
        error.status = MouseInjectStatus::InvalidArgument;
        error.message = "pressed bits must be within 0x07 (D0 left, D1 right, D2 middle)";
        return error;
    }

    std::string names;
    for (MouseButton button : {MouseButton::Left, MouseButton::Right, MouseButton::Middle})
    {
        if (pressedBits & static_cast<uint8_t>(button))
            names += (names.empty() ? "" : ",") + GetButtonName(button);
    }
    MouseInjectResult result = Success("Mouse buttons set: " + (names.empty() ? std::string("none") : names));

    Mouse& mouse = *Device();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (EnqueueIfBusyLocked({QueuedOp::Kind::SetButtons, 0, 0, pressedBits, 0}, result))
            return result;
        CancelPendingLocked();
        SubmitAutomationButtons(mouse, pressedBits);
    }
    return result;
}

MouseInjectResult DebugMouseManager::ReleaseAllButtons()
{
    // Everything stops: the queued input is dropped, then the buttons are released now
    CancelQueue();
    return SetPressedButtons(0);
}

MouseInjectResult DebugMouseManager::SetCounters(int x, int y)
{
    if (MouseInjectResult guard = Guard(); !guard.ok())
        return guard;

    MouseInjectResult error;
    error.status = MouseInjectStatus::InvalidArgument;
    if (x < 0 || x > 255)
    {
        error.message = RangeError("x", x, 0, 255);
        return error;
    }
    if (y < 0 || y > 255)
    {
        error.message = RangeError("y", y, 0, 255);
        return error;
    }

    ApplyCounters(*Device(), static_cast<uint8_t>(x), static_cast<uint8_t>(y));
    return Success("Mouse counters set: X=" + std::to_string(x) + " Y=" + std::to_string(y));
}

/// endregion </Automation - immediate>

/// region <Automation - timed>

void DebugMouseManager::CancelPendingLocked()
{
    _pendingButton.reset();
    _pendingFrames = 0;
}

MouseInjectResult DebugMouseManager::Click(MouseButton button, uint32_t holdFrames)
{
    if (MouseInjectResult guard = Guard(); !guard.ok())
        return guard;

    if (holdFrames < 1 || holdFrames > MAX_CLICK_FRAMES)
    {
        MouseInjectResult error;
        error.status = MouseInjectStatus::InvalidArgument;
        error.message = RangeError("frames", holdFrames, 1, static_cast<int>(MAX_CLICK_FRAMES));
        return error;
    }

    MouseInjectResult result = Success("Mouse click: " + GetButtonName(button) + " for " + std::to_string(holdFrames) + " frames");
    Mouse& mouse = *Device();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (EnqueueIfBusyLocked({QueuedOp::Kind::Click, 0, 0, static_cast<uint8_t>(button), holdFrames}, result))
            return result;

        // A new click replaces a pending one: release the old button first
        if (_pendingButton.has_value())
        {
            SubmitAutomationButtons(mouse, static_cast<uint8_t>(AutomationPressed() & ~static_cast<uint8_t>(*_pendingButton)));
            CancelPendingLocked();
        }

        SubmitAutomationButtons(mouse, static_cast<uint8_t>(AutomationPressed() | static_cast<uint8_t>(button)));
        _pendingButton = button;
        _pendingFrames = static_cast<uint16_t>(holdFrames);
    }
    return result;
}

bool DebugMouseManager::IsClickPending() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _pendingButton.has_value();
}

void DebugMouseManager::AbortClick()
{
    Mouse* mouse = Device();
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_pendingButton.has_value())
        return;
    if (mouse && !IsReplaying())
        SubmitAutomationButtons(*mouse, static_cast<uint8_t>(AutomationPressed() & ~static_cast<uint8_t>(*_pendingButton)));
    CancelPendingLocked();
}

void DebugMouseManager::OnFrame()
{
    Mouse* mouse = Device();
    std::lock_guard<std::mutex> lock(_mutex);
    if (_pendingButton.has_value())
    {
        if (_pendingFrames > 0)
            _pendingFrames--;

        if (_pendingFrames == 0)
        {
            // Release through the journalled path (keyboard timed ops skipped the journal)
            if (mouse && !IsReplaying())
                SubmitAutomationButtons(*mouse, static_cast<uint8_t>(AutomationPressed() & ~static_cast<uint8_t>(*_pendingButton)));
            CancelPendingLocked();
        }
        return;  // the queue waits for the click to end (the release is this frame's input)
    }
    ProcessQueueLocked(mouse);
}

/// region <Queue behind a glide>

bool DebugMouseManager::EnqueueIfBusyLocked(const QueuedOp& op, MouseInjectResult& result)
{
    if (_queue.empty())
        return false;
    _queue.push_back(op);
    result.queued = true;
    result.message += " (queued behind a glide)";
    return true;
}

int DebugMouseManager::StepLimit() const
{
    return _context && _context->pMouseManager ? _context->pMouseManager->MotionStepLimit() : MAX_MOVE_PER_CALL;
}

bool DebugMouseManager::HasUnreadMotion() const
{
    if (_context && _context->pMouseManager)
        return _context->pMouseManager->HasUnreadMotion();
    const Mouse* mouse = Device();
    return mouse && mouse->IsMouseInUse() && mouse->HasUnreadMotion();
}

bool DebugMouseManager::ApplyMotionStepLocked(Mouse& mouse, QueuedOp& op)
{
    const int limit = StepLimit();
    const int stepX = std::clamp(op.dx, -limit, limit);
    const int stepY = std::clamp(op.dy, -limit, limit);
    ApplyMove(mouse, stepX, stepY);
    op.dx -= stepX;
    op.dy -= stepY;
    return op.dx == 0 && op.dy == 0;
}

void DebugMouseManager::ApplyQueuedLocked(Mouse& mouse, QueuedOp& op)
{
    const MouseButton button = static_cast<MouseButton>(op.bits);
    switch (op.kind)
    {
        case QueuedOp::Kind::Motion:
            break;  // stepped by the caller
        case QueuedOp::Kind::Press:
            SubmitAutomationButtons(mouse, static_cast<uint8_t>(AutomationPressed() | op.bits));
            break;
        case QueuedOp::Kind::Release:
            SubmitAutomationButtons(mouse, static_cast<uint8_t>(AutomationPressed() & ~op.bits));
            break;
        case QueuedOp::Kind::SetButtons:
            SubmitAutomationButtons(mouse, op.bits);
            break;
        case QueuedOp::Kind::Wheel:
            ApplyWheel(mouse, op.dx);
            break;
        case QueuedOp::Kind::Click:
            SubmitAutomationButtons(mouse, static_cast<uint8_t>(AutomationPressed() | op.bits));
            _pendingButton = button;
            _pendingFrames = static_cast<uint16_t>(op.frames);
            break;
    }
}

void DebugMouseManager::ProcessQueueLocked(Mouse* mouse)
{
    if (_queue.empty())
        return;
    // Recorded input drives the machine (or the device went away): queued live input is void
    if (!mouse || IsReplaying())
    {
        _queue.clear();
        _waitFrames = 0;
        return;
    }

    // Whatever comes next waits until the program has taken the last motion, so neither a counter
    // jumps by more than one step between reads nor a click lands before the pointer arrived
    if (HasUnreadMotion() && _waitFrames < GLIDE_WAIT_FRAMES)
    {
        _waitFrames++;
        return;
    }
    _waitFrames = 0;

    // One item per frame: a press and its release never meet in the same frame
    QueuedOp& op = _queue.front();
    if (op.kind == QueuedOp::Kind::Motion)
    {
        if (ApplyMotionStepLocked(*mouse, op))
            _queue.pop_front();
        return;
    }
    QueuedOp item = op;
    _queue.pop_front();
    ApplyQueuedLocked(*mouse, item);
}

/// endregion </Queue behind a glide>

/// endregion </Automation - timed>

/// region <Host input>

void DebugMouseManager::ApplyHostMove(int dx, int dy)
{
    Mouse* mouse = Device();
    if (!mouse || IsReplaying())
        return;
    ApplyMove(*mouse, dx, dy);
}

void DebugMouseManager::ApplyHostButtons(uint8_t activeLowMask)
{
    Mouse* mouse = Device();
    if (!mouse || IsReplaying())
        return;
    // The host's buttons joined with automation's (mousemanager.h)
    if (_context && _context->pMouseManager)
        activeLowMask = _context->pMouseManager->ComposeButtons(MouseManager::ButtonSource::Host,
                                                                static_cast<uint8_t>(~activeLowMask & kAllButtons));
    ApplyButtons(*mouse, activeLowMask);
}

void DebugMouseManager::ApplyHostWheel(int steps)
{
    Mouse* mouse = Device();
    if (!mouse || IsReplaying())
        return;
    ApplyWheel(*mouse, steps);
}

/// endregion </Host input>

MouseStateSnapshot DebugMouseManager::GetState(const std::string& deviceId) const
{
    MouseStateSnapshot state;
    Mouse* mouse = Device();
    if (!mouse)
        return state;

    // The machine's mouse devices (the manager); a bare context has the Kempston interface alone
    if (_context && _context->pMouseManager)
    {
        MouseManager& manager = *_context->pMouseManager;
        state.devices = manager.DescribeDevices();
        state.mouseFitted = manager.HasMouseDevice();
        state.device = deviceId.empty() ? manager.DescribeDefaultDevice() : manager.DescribeDevice(deviceId);
    }
    else
    {
        state.devices.push_back(mouse->DescribeMouse());
        state.mouseFitted = mouse->IsPresent();
        if (deviceId.empty() ? state.mouseFitted : deviceId == state.devices.front().id)
            state.device = state.devices.front();
    }

    state.available = true;
    state.present = mouse->IsPresent();
    state.wheelEnabled = mouse->IsWheelEnabled();
    state.x = mouse->GetX();
    state.y = mouse->GetY();
    state.buttonMask = mouse->GetButtons();
    state.wheel = mouse->GetWheel();
    // What the machine's ports return: the Kempston device, or the machine's own
    // mouse (ZX-Evo AVR PS/2 mouse, Sprinter board mouse)
    const PortDecoder* decoder = _context ? _context->pPortDecoder : nullptr;
    uint8_t* ports[3] = {&state.portButtons, &state.portX, &state.portY};
    for (uint8_t reg = 0; reg < 3; reg++)
        if (!decoder || !decoder->PeekMouseRegister(reg, *ports[reg]))
            *ports[reg] = mouse->PeekRegister(reg);

    std::lock_guard<std::mutex> lock(_mutex);
    state.pendingClickButton = _pendingButton;
    state.pendingClickFramesLeft = _pendingButton.has_value() ? _pendingFrames : 0;
    state.queuedOps = _queue.size();
    for (const QueuedOp& op : _queue)
    {
        if (op.kind != QueuedOp::Kind::Motion)
            continue;
        state.glideRemainingDx += op.dx;
        state.glideRemainingDy += op.dy;
    }
    return state;
}

/// region <Names>

std::optional<MouseButton> DebugMouseManager::ResolveButtonName(const std::string& name)
{
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });

    if (lower == "left" || lower == "l")
        return MouseButton::Left;
    if (lower == "right" || lower == "r")
        return MouseButton::Right;
    if (lower == "middle" || lower == "m")
        return MouseButton::Middle;
    return std::nullopt;
}

std::string DebugMouseManager::GetButtonName(MouseButton button)
{
    switch (button)
    {
        case MouseButton::Left:
            return "left";
        case MouseButton::Right:
            return "right";
        case MouseButton::Middle:
            return "middle";
    }
    return "unknown";
}

std::vector<std::string> DebugMouseManager::GetAllButtonNames()
{
    return {"left", "right", "middle"};
}

/// endregion </Names>
