#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class EmulatorContext;
class Joystick;
namespace ttd
{
struct TTDInputEvent;
}

enum class JoystickInjectStatus : uint8_t
{
    Ok,
    NoDevice,         // context has no Joystick
    InvalidArgument,  // out-of-range value or unknown name; message says which
    ReplayActive      // TTD replay in progress - live input refused
};

/// Same shape as MouseInjectResult, so every front end reports both devices alike
struct JoystickInjectResult
{
    JoystickInjectStatus status = JoystickInjectStatus::Ok;
    std::string message;  // human-readable, reused by every front end
    std::string warning;  // set on success when the input cannot reach the guest (device absent / not decoded)

    bool ok() const { return status == JoystickInjectStatus::Ok; }
};

struct JoystickStateSnapshot
{
    bool available = false;  // a Joystick device exists
    bool present = false;    // fitted (config + feature); absent = the port reads 0x00
    bool wired = false;      // this machine's port decoder answers a Kempston joystick
    uint8_t state = 0;       // the device byte, active high
    uint8_t portValue = 0;   // what IN #1F returns from the device right now
    std::vector<std::string> buttons;  // names of the pressed bits (right, left, down, up, fire, b5, b6, b7)
    std::string keys;                  // host key bindings, "up:kp_8,..." (empty = no host keys)
    uint8_t pendingTapMask = 0;        // buttons a timed tap will release
    uint16_t pendingTapFramesLeft = 0;
};

/// Single funnel for automation input to the Kempston joystick (joystick TDD §4, §6).
///
/// Every state change goes through TimeTravelManager::SubmitLiveInput as a
/// TTDInputKind::Joystick event carrying the whole new state byte: refused while the journal
/// owns input (replay), journaled BEFORE the device changes while recording, applied on the
/// machine's thread. Host keys do not come here: a bound PC key is applied by Keyboard (the
/// same place the PS/2 sink gets it), journaled once as PcKey and replayed through the same call.
///
/// Delivery is a direct call on the caller's thread when the loop is not running, as the mouse.
class DebugJoystickManager
{
public:
    static constexpr uint16_t DEFAULT_TAP_FRAMES = 2;  // same default as the mouse click and the keyboard tap
    static constexpr uint32_t MAX_TAP_FRAMES = 65535;

    explicit DebugJoystickManager(EmulatorContext* context);
    ~DebugJoystickManager() = default;

    DebugJoystickManager(const DebugJoystickManager&) = delete;
    DebugJoystickManager& operator=(const DebugJoystickManager&) = delete;

    /// region <Automation - immediate, validated>
    /// `name` is one button name, or several joined by ',' or '+' ("up+fire")
    JoystickInjectResult Press(const std::string& name);
    JoystickInjectResult Release(const std::string& name);
    JoystickInjectResult PressMask(uint8_t mask);
    JoystickInjectResult ReleaseMask(uint8_t mask);
    JoystickInjectResult SetState(uint8_t state);  // the whole byte, D5..D7 included
    /// Surface entry: a raw integer from a JSON / Lua / Python / CLI value, range-checked here (0..255)
    JoystickInjectResult SetStateChecked(long long state);
    JoystickInjectResult ReleaseAll();             // also cancels a pending tap
    /// endregion </Automation - immediate, validated>

    /// region <Automation - timed>
    /// Press now, release after holdFrames emulated frames (released in OnFrame)
    JoystickInjectResult Tap(const std::string& name, uint32_t holdFrames = DEFAULT_TAP_FRAMES);
    /// Surface entry: frames as the raw integer the caller parsed (negative or beyond 32 bits is rejected, not wrapped)
    JoystickInjectResult TapChecked(const std::string& name, long long holdFrames);
    bool IsTapPending() const;
    void AbortTap();  // releases the held buttons now
    /// endregion </Automation - timed>

    JoystickStateSnapshot GetState() const;

    /// region <Names>
    /// One name or a ',' / '+' list -> mask; 0 when empty or any name is unknown
    static uint8_t ResolveButtonNames(const std::string& names);
    static std::vector<std::string> GetAllButtonNames();
    /// endregion </Names>

    /// Frame pump (emulator thread, MainLoop::CompleteFrame)
    void OnFrame();

private:
    Joystick* Device() const;
    JoystickInjectResult Guard() const;  // NoDevice / ReplayActive
    JoystickInjectResult Success(const std::string& message) const;
    JoystickInjectResult Invalid(const std::string& message) const;
    /// Mask of a name list, or an InvalidArgument result in `error`
    bool ParseNames(const std::string& names, uint8_t& mask, JoystickInjectResult& error) const;

    // Submit through the TTD live-input gateway (ownership + journal + apply on the machine's thread)
    bool Submit(uint8_t state, Joystick& joystick);
    bool IsReplaying() const;
    static std::string DescribeMask(uint8_t mask);

    void CancelPendingLocked();

    EmulatorContext* _context = nullptr;

    mutable std::mutex _mutex;  // pending tap state: automation thread vs emulator thread
    uint8_t _pendingMask = 0;
    uint16_t _pendingFrames = 0;
};
