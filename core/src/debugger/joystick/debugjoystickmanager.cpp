#include "debugjoystickmanager.h"

#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdinputapply.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/joystick/joystick.h"
#include "emulator/ports/portdecoder.h"

/// region <Guards and journal>

DebugJoystickManager::DebugJoystickManager(EmulatorContext* context) : _context(context) {}

Joystick* DebugJoystickManager::Device() const
{
    // Read on every call: independent of whether DebugManager or Core::Init runs first
    return _context ? _context->pJoystick : nullptr;
}

bool DebugJoystickManager::IsReplaying() const
{
    // The TTD journal owns input: a replay, or the machine re-executing recorded history
    // (TimeTravelManager::OwnsInput), or an RZX playback
    if (_context && _context->rzxPlayer)
        return true;
    return _context && _context->pTimeTravelHooks && _context->pTimeTravelHooks->OwnsInput();
}

JoystickInjectResult DebugJoystickManager::Guard() const
{
    JoystickInjectResult result;
    if (!Device())
    {
        result.status = JoystickInjectStatus::NoDevice;
        result.message = "Joystick device not available";
    }
    else if (IsReplaying())
    {
        result.status = JoystickInjectStatus::ReplayActive;
        result.message = "TTD replay in progress (recorded input drives the machine); live joystick input refused";
    }
    return result;
}

JoystickInjectResult DebugJoystickManager::Success(const std::string& message) const
{
    JoystickInjectResult result;
    result.message = message;
    if (Joystick* joystick = Device(); joystick && !joystick->IsPresent())
        result.warning = "joystick not present: the guest reads 0x00 on the joystick port";
    else if (_context && _context->pPortDecoder && !_context->pPortDecoder->HasKempstonJoystick())
        result.warning = "this machine does not decode a Kempston joystick port: the guest cannot see the buttons";
    return result;
}

JoystickInjectResult DebugJoystickManager::Invalid(const std::string& message) const
{
    JoystickInjectResult result;
    result.status = JoystickInjectStatus::InvalidArgument;
    result.message = message;
    return result;
}

// Every joystick mutation goes through TimeTravelManager::SubmitLiveInput when TTD is present:
// refused while the journal owns input, applied on the machine's thread at an instruction boundary
// and journaled there while recording (same as the mouse and the keyboard)
bool DebugJoystickManager::Submit(uint8_t state, Joystick& joystick)
{
    ttd::TTDInputEvent ev;
    ev.kind = ttd::TTDInputKind::Joystick;
    ev.buttonMask = state;

    if (_context && _context->pTimeTravelHooks)
        return _context->pTimeTravelHooks->SubmitLiveInput(ev);
    ttd::TTDInputDevices devices;
    devices.joystick = &joystick;
    return ttd::ApplyInputEvent(ev, devices);
}

/// endregion </Guards and journal>

/// region <Names>

uint8_t DebugJoystickManager::ResolveButtonNames(const std::string& names)
{
    uint8_t mask = 0;
    size_t position = 0;
    while (position <= names.size())
    {
        size_t end = names.find_first_of(",+", position);
        if (end == std::string::npos)
            end = names.size();
        const uint8_t one = Joystick::MaskFromName(names.substr(position, end - position));
        if (one == 0)
            return 0;
        mask |= one;
        position = end + 1;
    }
    return mask;
}

std::vector<std::string> DebugJoystickManager::GetAllButtonNames()
{
    return {"up", "down", "left", "right", "fire", "b5", "b6", "b7"};
}

std::string DebugJoystickManager::DescribeMask(uint8_t mask)
{
    std::string text;
    for (unsigned bit = 0; bit < 8; bit++)
    {
        if (mask & (1u << bit))
            text += (text.empty() ? "" : ",") + Joystick::NameOfBit(static_cast<uint8_t>(1u << bit));
    }
    return text.empty() ? std::string("none") : text;
}

bool DebugJoystickManager::ParseNames(const std::string& names, uint8_t& mask, JoystickInjectResult& error) const
{
    mask = ResolveButtonNames(names);
    if (mask != 0)
        return true;

    error = Invalid(names.empty() ? std::string("button name required (up, down, left, right, fire, b5, b6, b7)")
                                  : "unknown joystick button '" + names +
                                        "' (up, down, left, right, fire, b5, b6, b7)");
    return false;
}

/// endregion </Names>

/// region <Automation - immediate>

JoystickInjectResult DebugJoystickManager::Press(const std::string& name)
{
    if (JoystickInjectResult guard = Guard(); !guard.ok())
        return guard;
    uint8_t mask = 0;
    JoystickInjectResult error;
    if (!ParseNames(name, mask, error))
        return error;
    return PressMask(mask);
}

JoystickInjectResult DebugJoystickManager::Release(const std::string& name)
{
    if (JoystickInjectResult guard = Guard(); !guard.ok())
        return guard;
    uint8_t mask = 0;
    JoystickInjectResult error;
    if (!ParseNames(name, mask, error))
        return error;
    return ReleaseMask(mask);
}

JoystickInjectResult DebugJoystickManager::PressMask(uint8_t mask)
{
    if (JoystickInjectResult guard = Guard(); !guard.ok())
        return guard;
    if (mask == 0)
        return Invalid("press requires a non-zero button mask");

    Joystick& joystick = *Device();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_pendingMask & mask)
            CancelPendingLocked();  // the explicit call wins over a pending tap release
        Submit(static_cast<uint8_t>(joystick.State() | mask), joystick);
    }
    return Success("Joystick pressed: " + DescribeMask(mask));
}

JoystickInjectResult DebugJoystickManager::ReleaseMask(uint8_t mask)
{
    if (JoystickInjectResult guard = Guard(); !guard.ok())
        return guard;
    if (mask == 0)
        return Invalid("release requires a non-zero button mask");

    Joystick& joystick = *Device();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_pendingMask & mask)
            CancelPendingLocked();
        Submit(static_cast<uint8_t>(joystick.State() & ~mask), joystick);
    }
    return Success("Joystick released: " + DescribeMask(mask));
}

JoystickInjectResult DebugJoystickManager::SetState(uint8_t state)
{
    if (JoystickInjectResult guard = Guard(); !guard.ok())
        return guard;

    Joystick& joystick = *Device();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        CancelPendingLocked();
        Submit(state, joystick);
    }
    return Success("Joystick state set: " + DescribeMask(state));
}

JoystickInjectResult DebugJoystickManager::SetStateChecked(long long state)
{
    if (JoystickInjectResult guard = Guard(); !guard.ok())
        return guard;
    if (state < 0 || state > 255)
        return Invalid("state=" + std::to_string(state) + " out of range 0..255");
    return SetState(static_cast<uint8_t>(state));
}

JoystickInjectResult DebugJoystickManager::ReleaseAll()
{
    return SetState(0);
}

/// endregion </Automation - immediate>

/// region <Automation - timed>

void DebugJoystickManager::CancelPendingLocked()
{
    _pendingMask = 0;
    _pendingFrames = 0;
}

JoystickInjectResult DebugJoystickManager::Tap(const std::string& name, uint32_t holdFrames)
{
    if (JoystickInjectResult guard = Guard(); !guard.ok())
        return guard;

    uint8_t mask = 0;
    JoystickInjectResult error;
    if (!ParseNames(name, mask, error))
        return error;

    if (holdFrames < 1 || holdFrames > MAX_TAP_FRAMES)
    {
        return Invalid("frames=" + std::to_string(holdFrames) + " out of range 1.." + std::to_string(MAX_TAP_FRAMES));
    }

    Joystick& joystick = *Device();
    {
        std::lock_guard<std::mutex> lock(_mutex);

        // A new tap replaces a pending one: release the old buttons first
        uint8_t state = joystick.State();
        if (_pendingMask)
        {
            state = static_cast<uint8_t>(state & ~_pendingMask);
            CancelPendingLocked();
        }

        Submit(static_cast<uint8_t>(state | mask), joystick);
        _pendingMask = mask;
        _pendingFrames = static_cast<uint16_t>(holdFrames);
    }
    return Success("Joystick tap: " + DescribeMask(mask) + " for " + std::to_string(holdFrames) + " frames");
}

JoystickInjectResult DebugJoystickManager::TapChecked(const std::string& name, long long holdFrames)
{
    if (JoystickInjectResult guard = Guard(); !guard.ok())
        return guard;
    if (holdFrames < 1 || holdFrames > static_cast<long long>(MAX_TAP_FRAMES))
        return Invalid("frames=" + std::to_string(holdFrames) + " out of range 1.." + std::to_string(MAX_TAP_FRAMES));
    return Tap(name, static_cast<uint32_t>(holdFrames));
}

bool DebugJoystickManager::IsTapPending() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _pendingMask != 0;
}

void DebugJoystickManager::AbortTap()
{
    Joystick* joystick = Device();
    std::lock_guard<std::mutex> lock(_mutex);
    if (_pendingMask == 0)
        return;
    if (joystick && !IsReplaying())
        Submit(static_cast<uint8_t>(joystick->State() & ~_pendingMask), *joystick);
    CancelPendingLocked();
}

void DebugJoystickManager::OnFrame()
{
    Joystick* joystick = Device();
    std::lock_guard<std::mutex> lock(_mutex);
    if (_pendingMask == 0)
        return;

    if (_pendingFrames > 0)
        _pendingFrames--;

    if (_pendingFrames == 0)
    {
        // Release through the journaled path
        if (joystick && !IsReplaying())
            Submit(static_cast<uint8_t>(joystick->State() & ~_pendingMask), *joystick);
        CancelPendingLocked();
    }
}

/// endregion </Automation - timed>

JoystickStateSnapshot DebugJoystickManager::GetState() const
{
    JoystickStateSnapshot snapshot;
    Joystick* joystick = Device();
    if (!joystick)
        return snapshot;

    snapshot.available = true;
    snapshot.present = joystick->IsPresent();
    snapshot.wired = !_context->pPortDecoder || _context->pPortDecoder->HasKempstonJoystick();
    snapshot.state = joystick->State();
    snapshot.portValue = joystick->Read();
    snapshot.keys = joystick->BindingsSpec();
    for (unsigned bit = 0; bit < 8; bit++)
    {
        if (snapshot.state & (1u << bit))
            snapshot.buttons.push_back(Joystick::NameOfBit(static_cast<uint8_t>(1u << bit)));
    }

    std::lock_guard<std::mutex> lock(_mutex);
    snapshot.pendingTapMask = _pendingMask;
    snapshot.pendingTapFramesLeft = _pendingMask ? _pendingFrames : 0;
    return snapshot;
}
