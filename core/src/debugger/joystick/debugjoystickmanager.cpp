#include "debugjoystickmanager.h"

#include <cctype>

#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdinputapply.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/joystick/joystick.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/slots/slotmanager.h"

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

namespace
{
/// The slot card that gives the machine a Kempston joystick, as a plug command (the machine's slot planner decides
/// where it goes and whether it needs the override)
std::string FitHint(EmulatorContext* context)
{
    if (!context || !context->pSlotManager)
        return {};
    return SlotControl::PlugHint(context->pSlotManager->Current(), "kempston");
}
}  // namespace

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
        result.warning = UnwiredWarning(FitHint(_context));
    return result;
}

std::string DebugJoystickManager::UnwiredWarning(const std::string& fit)
{
    std::string warning = "no Kempston joystick interface on this machine: the guest cannot see the buttons";
    if (!fit.empty())
        warning += "; " + fit;
    return warning;
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

/// The whole state of a pad connector of a machine with 12-button pads (the ZX Next), journaled as its own input kind
bool DebugJoystickManager::SubmitPad(unsigned connector, uint16_t state)
{
    ttd::TTDInputEvent ev;
    ev.kind = ttd::TTDInputKind::NextPad;
    ev.key = static_cast<uint8_t>(connector);
    ev.dx = static_cast<int16_t>(state);

    if (_context && _context->pTimeTravelHooks)
        return _context->pTimeTravelHooks->SubmitLiveInput(ev);
    ttd::TTDInputDevices devices;
    devices.joystick = Device();
    devices.portDecoder = _context ? _context->pPortDecoder : nullptr;
    return ttd::ApplyInputEvent(ev, devices);
}

/// endregion </Guards and journal>

/// region <Names>

namespace
{
/// The 12 buttons in PortDecoder::PadState order: bit 0 right, 1 left, 2 down, 3 up, 4 B (fire), 5 C, 6 A, 7 START, 8 Y, 9 Z, 10 X, 11 MODE
struct PadButton
{
    const char* name;
    const char* alias;  // a second spelling, or ""
    unsigned bit;
};
constexpr PadButton kPadButtons[] = {
    {"right", "", 0}, {"left", "", 1}, {"down", "", 2}, {"up", "", 3},   {"fire", "b", 4},  {"b5", "c", 5},
    {"b6", "a", 6},   {"b7", "start", 7}, {"y", "", 8}, {"z", "", 9},    {"x", "", 10},     {"mode", "", 11},
};
constexpr const char* kRightPrefix = "right.";
constexpr const char* kLeftPrefix = "left.";

std::string LowerCase(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

/// One name -> its bit (0..11 left, 16..27 right) or -1
int ButtonBit(const std::string& raw)
{
    std::string name = LowerCase(raw);
    unsigned shift = 0;
    if (name.rfind(kRightPrefix, 0) == 0)
    {
        shift = DebugJoystickManager::kRightShift;
        name = name.substr(std::string(kRightPrefix).size());
    }
    else if (name.rfind(kLeftPrefix, 0) == 0)
        name = name.substr(std::string(kLeftPrefix).size());
    for (const PadButton& b : kPadButtons)
        if (name == b.name || (*b.alias && name == b.alias))
            return static_cast<int>(b.bit + shift);
    return -1;
}
}  // namespace

uint32_t DebugJoystickManager::ResolveButtons(const std::string& names)
{
    uint32_t mask = 0;
    size_t position = 0;
    while (position <= names.size())
    {
        size_t end = names.find_first_of(",+", position);
        if (end == std::string::npos)
            end = names.size();
        const int bit = ButtonBit(names.substr(position, end - position));
        if (bit < 0)
            return 0;
        mask |= 1u << bit;
        position = end + 1;
    }
    return mask;
}

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

std::string DebugJoystickManager::DescribeMask(uint32_t mask)
{
    std::string text;
    for (const PadButton& b : kPadButtons)
    {
        if (mask & (1u << b.bit))
            text += (text.empty() ? "" : ",") + std::string(b.name);
        if (mask & (1u << (b.bit + kRightShift)))
            text += (text.empty() ? "" : ",") + std::string(kRightPrefix) + b.name;
    }
    return text.empty() ? std::string("none") : text;
}

bool DebugJoystickManager::ParseNames(const std::string& names, uint32_t& mask, JoystickInjectResult& error) const
{
    mask = ResolveButtons(names);
    if (mask == 0)
    {
        // The pads' names are part of the list only on a machine that has pads (the message of the others is the Kempston one)
        const bool pads = _context && _context->pPortDecoder && _context->pPortDecoder->HasPads();
        const std::string list = pads ? "(up, down, left, right, fire, b5, b6, b7; a pad adds a, c, start, x, y, z, mode and right.<button>)"
                                      : "(up, down, left, right, fire, b5, b6, b7)";
        error = Invalid(names.empty() ? "button name required " + list : "unknown joystick button '" + names + "' " + list);
        return false;
    }
    // Beyond the Kempston byte of the left connector the machine needs pads (the ZX Next)
    if ((mask & ~kLeftKempston) && !(_context && _context->pPortDecoder && _context->pPortDecoder->HasPads()))
    {
        error = Invalid("this machine has no pad buttons beyond the Kempston byte (a, c, start map to b6, b5, b7; x, y, z, mode and the right connector are the ZX Next's)");
        return false;
    }
    return true;
}

/// endregion </Names>

/// region <Automation - immediate>

/// Press (or release) the buttons of `mask`: the Kempston byte as the Joystick event, a pad connector as a NextPad event with its whole new state.
/// The caller holds _mutex
bool DebugJoystickManager::ApplyMaskLocked(uint32_t mask, bool press)
{
    Joystick& joystick = *Device();
    bool ok = true;
    const uint32_t leftExtras = mask & kLeftExtras, right = (mask & kRightAll) >> kRightShift;
    if (const uint8_t low = static_cast<uint8_t>(mask & kLeftKempston))
        ok = Submit(static_cast<uint8_t>(press ? (joystick.State() | low) : (joystick.State() & ~low)), joystick) && ok;
    PortDecoder* decoder = _context ? _context->pPortDecoder : nullptr;
    if (leftExtras && decoder)
    {
        const uint16_t now = decoder->PadState(0);
        ok = SubmitPad(0, static_cast<uint16_t>(press ? (now | leftExtras) : (now & ~leftExtras))) && ok;
    }
    if (right && decoder)
    {
        const uint16_t now = decoder->PadState(1);
        ok = SubmitPad(1, static_cast<uint16_t>(press ? (now | right) : (now & ~right))) && ok;
    }
    return ok;
}

JoystickInjectResult DebugJoystickManager::Press(const std::string& name)
{
    if (JoystickInjectResult guard = Guard(); !guard.ok())
        return guard;
    uint32_t mask = 0;
    JoystickInjectResult error;
    if (!ParseNames(name, mask, error))
        return error;
    return PressMask(mask);
}

JoystickInjectResult DebugJoystickManager::Release(const std::string& name)
{
    if (JoystickInjectResult guard = Guard(); !guard.ok())
        return guard;
    uint32_t mask = 0;
    JoystickInjectResult error;
    if (!ParseNames(name, mask, error))
        return error;
    return ReleaseMask(mask);
}

JoystickInjectResult DebugJoystickManager::PressMask(uint32_t mask)
{
    if (JoystickInjectResult guard = Guard(); !guard.ok())
        return guard;
    if (mask == 0)
        return Invalid("press requires a non-zero button mask");

    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_pendingMask & mask)
            CancelPendingLocked();  // the explicit call wins over a pending tap release
        ApplyMaskLocked(mask, true);
    }
    return Success("Joystick pressed: " + DescribeMask(mask));
}

JoystickInjectResult DebugJoystickManager::ReleaseMask(uint32_t mask)
{
    if (JoystickInjectResult guard = Guard(); !guard.ok())
        return guard;
    if (mask == 0)
        return Invalid("release requires a non-zero button mask");

    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_pendingMask & mask)
            CancelPendingLocked();
        ApplyMaskLocked(mask, false);
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

JoystickInjectResult DebugJoystickManager::SetNames(const std::string& names)
{
    if (JoystickInjectResult guard = Guard(); !guard.ok())
        return guard;
    uint32_t mask = 0;
    JoystickInjectResult error;
    if (!ParseNames(names, mask, error))
        return error;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        CancelPendingLocked();
        Submit(static_cast<uint8_t>(mask & kLeftKempston), *Device());
        if (PortDecoder* decoder = _context ? _context->pPortDecoder : nullptr; decoder && decoder->HasPads())
        {
            SubmitPad(0, static_cast<uint16_t>((decoder->PadState(0) & 0x00FF) | (mask & kLeftExtras)));
            SubmitPad(1, static_cast<uint16_t>((mask & kRightAll) >> kRightShift));
        }
    }
    return Success("Joystick state set: " + DescribeMask(mask));
}

JoystickInjectResult DebugJoystickManager::ReleaseAll()
{
    JoystickInjectResult result = SetState(0);
    if (result.ok() && _context && _context->pPortDecoder && _context->pPortDecoder->HasPads())
    {
        std::lock_guard<std::mutex> lock(_mutex);
        SubmitPad(0, 0);
        SubmitPad(1, 0);
    }
    return result;
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

    uint32_t mask = 0;
    JoystickInjectResult error;
    if (!ParseNames(name, mask, error))
        return error;

    if (holdFrames < 1 || holdFrames > MAX_TAP_FRAMES)
    {
        return Invalid("frames=" + std::to_string(holdFrames) + " out of range 1.." + std::to_string(MAX_TAP_FRAMES));
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);

        // A new tap replaces a pending one: release the old buttons first
        if (_pendingMask)
        {
            ApplyMaskLocked(_pendingMask, false);
            CancelPendingLocked();
        }

        ApplyMaskLocked(mask, true);
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
        ApplyMaskLocked(_pendingMask, false);
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
            ApplyMaskLocked(_pendingMask, false);
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
    if (!snapshot.wired)
        snapshot.fit = FitHint(_context);
    snapshot.state = joystick->State();
    snapshot.portValue = joystick->Read();
    snapshot.keys = joystick->BindingsSpec();
    for (unsigned bit = 0; bit < 8; bit++)
    {
        if (snapshot.state & (1u << bit))
            snapshot.buttons.push_back(Joystick::NameOfBit(static_cast<uint8_t>(1u << bit)));
    }
    if (_context->pPortDecoder && _context->pPortDecoder->HasPads())
    {
        snapshot.hasPads = true;
        snapshot.leftPad = _context->pPortDecoder->PadState(0);
        snapshot.rightPad = _context->pPortDecoder->PadState(1);
        for (const PadButton& b : kPadButtons)
        {
            if (b.bit >= 8 && (snapshot.leftPad & (1u << b.bit)))
                snapshot.leftExtraButtons.push_back(b.name);
            if (snapshot.rightPad & (1u << b.bit))
                snapshot.rightButtons.push_back(b.name);
        }
    }

    std::lock_guard<std::mutex> lock(_mutex);
    snapshot.pendingTapMask = _pendingMask;
    snapshot.pendingTapFramesLeft = _pendingMask ? _pendingFrames : 0;
    return snapshot;
}
