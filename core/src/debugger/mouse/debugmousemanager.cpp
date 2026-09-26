#include "debugmousemanager.h"

#include <algorithm>
#include <cctype>

#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/mouse/mouse.h"

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
    // recorded history (TimeTravelManager::OwnsInput)
    return _context && _context->pTimeTravelManager && _context->pTimeTravelManager->OwnsInput();
}

MouseInjectResult DebugMouseManager::Guard() const
{
    MouseInjectResult result;
    if (!Device())
    {
        result.status = MouseInjectStatus::NoDevice;
        result.message = "Mouse device not available";
    }
    else if (IsReplaying())
    {
        result.status = MouseInjectStatus::ReplayActive;
        result.message = "TTD replay in progress (recorded input drives the machine); live mouse input refused";
    }
    return result;
}

MouseInjectResult DebugMouseManager::Success(const std::string& message) const
{
    MouseInjectResult result;
    result.message = message;
    if (Mouse* mouse = Device(); mouse && !mouse->IsPresent())
        result.warning = "mouse not present: guest reads floating bus on the mouse ports";
    return result;
}

// Every mouse mutation goes through TimeTravelManager::SubmitLiveInput when TTD is
// present: refused while the journal owns input, applied on the machine's thread at an
// instruction boundary and journalled there while recording (same as keyboard)
bool DebugMouseManager::Submit(const ttd::TTDInputEvent& ev, Mouse& mouse)
{
    if (_context && _context->pTimeTravelManager)
        return _context->pTimeTravelManager->SubmitLiveInput(ev);
    return ttd::TTDInputJournal::Apply(ev, nullptr, &mouse);
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

    ApplyMove(*Device(), dx, dy);
    return Success("Mouse moved: dx=" + std::string(dx >= 0 ? "+" : "") + std::to_string(dx) +
                   " dy=" + std::string(dy >= 0 ? "+" : "") + std::to_string(dy));
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

    ApplyWheel(*Device(), steps);
    MouseInjectResult result = Success("Mouse wheel: " + std::string(steps > 0 ? "+" : "") + std::to_string(steps));
    if (Mouse* mouse = Device(); result.warning.empty() && mouse && !mouse->IsWheelEnabled())
        result.warning = "no wheel fitted ([INPUT] Wheel=NONE): the guest does not see the wheel counter";
    return result;
}

MouseInjectResult DebugMouseManager::PressButton(MouseButton button)
{
    if (MouseInjectResult guard = Guard(); !guard.ok())
        return guard;

    Mouse& mouse = *Device();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_pendingButton == button)
            CancelPendingLocked();  // the explicit call wins over a pending click release
        ApplyButtons(mouse, static_cast<uint8_t>(mouse.GetButtons() & ~static_cast<uint8_t>(button)));
    }
    return Success("Mouse button pressed: " + GetButtonName(button));
}

MouseInjectResult DebugMouseManager::ReleaseButton(MouseButton button)
{
    if (MouseInjectResult guard = Guard(); !guard.ok())
        return guard;

    Mouse& mouse = *Device();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_pendingButton == button)
            CancelPendingLocked();
        ApplyButtons(mouse, static_cast<uint8_t>(mouse.GetButtons() | static_cast<uint8_t>(button)));
    }
    return Success("Mouse button released: " + GetButtonName(button));
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

    Mouse& mouse = *Device();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        CancelPendingLocked();
        const uint8_t mask = static_cast<uint8_t>((mouse.GetButtons() | kAllButtons) & ~pressedBits);
        ApplyButtons(mouse, mask);
    }

    std::string names;
    for (MouseButton button : {MouseButton::Left, MouseButton::Right, MouseButton::Middle})
    {
        if (pressedBits & static_cast<uint8_t>(button))
            names += (names.empty() ? "" : ",") + GetButtonName(button);
    }
    return Success("Mouse buttons set: " + (names.empty() ? std::string("none") : names));
}

MouseInjectResult DebugMouseManager::ReleaseAllButtons()
{
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

    Mouse& mouse = *Device();
    {
        std::lock_guard<std::mutex> lock(_mutex);

        // A new click replaces a pending one: release the old button first
        if (_pendingButton.has_value())
        {
            ApplyButtons(mouse, static_cast<uint8_t>(mouse.GetButtons() | static_cast<uint8_t>(*_pendingButton)));
            CancelPendingLocked();
        }

        ApplyButtons(mouse, static_cast<uint8_t>(mouse.GetButtons() & ~static_cast<uint8_t>(button)));
        _pendingButton = button;
        _pendingFrames = static_cast<uint16_t>(holdFrames);
    }
    return Success("Mouse click: " + GetButtonName(button) + " for " + std::to_string(holdFrames) + " frames");
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
        ApplyButtons(*mouse, static_cast<uint8_t>(mouse->GetButtons() | static_cast<uint8_t>(*_pendingButton)));
    CancelPendingLocked();
}

void DebugMouseManager::OnFrame()
{
    Mouse* mouse = Device();
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_pendingButton.has_value())
        return;

    if (_pendingFrames > 0)
        _pendingFrames--;

    if (_pendingFrames == 0)
    {
        // Release through the journalled path (keyboard timed ops skipped the journal)
        if (mouse && !IsReplaying())
            ApplyButtons(*mouse, static_cast<uint8_t>(mouse->GetButtons() | static_cast<uint8_t>(*_pendingButton)));
        CancelPendingLocked();
    }
}

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

MouseStateSnapshot DebugMouseManager::GetState() const
{
    MouseStateSnapshot state;
    Mouse* mouse = Device();
    if (!mouse)
        return state;

    state.available = true;
    state.present = mouse->IsPresent();
    state.wheelEnabled = mouse->IsWheelEnabled();
    state.x = mouse->GetX();
    state.y = mouse->GetY();
    state.buttonMask = mouse->GetButtons();
    state.wheel = mouse->GetWheel();
    state.portButtons = mouse->ReadRegister(0);
    state.portX = mouse->ReadRegister(1);
    state.portY = mouse->ReadRegister(2);

    std::lock_guard<std::mutex> lock(_mutex);
    state.pendingClickButton = _pendingButton;
    state.pendingClickFramesLeft = _pendingButton.has_value() ? _pendingFrames : 0;
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
