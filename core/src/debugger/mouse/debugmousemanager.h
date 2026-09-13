#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class EmulatorContext;
class Mouse;

/// Kempston Mouse button - the value is its bit in the active-low mask
enum class MouseButton : uint8_t
{
    Left = 0x01,
    Right = 0x02,
    Middle = 0x04
};

enum class MouseInjectStatus : uint8_t
{
    Ok,
    NoDevice,         // context has no Mouse
    InvalidArgument,  // out-of-range value or unknown name; message says which
    ReplayActive      // TTD replay in progress - live input refused
};

struct MouseInjectResult
{
    MouseInjectStatus status = MouseInjectStatus::Ok;
    std::string message;  // human-readable, reused by every front end
    std::string warning;  // set on success when the input cannot reach the guest (device absent)

    bool ok() const { return status == MouseInjectStatus::Ok; }
};

struct MouseStateSnapshot
{
    bool available = false;  // a Mouse device exists
    bool present = false;    // fitted (config + feature); absent = guest reads floating bus
    bool wheelEnabled = false;
    uint8_t x = 0;
    uint8_t y = 0;
    uint8_t buttonMask = 0xFF;  // active-low internal value
    uint8_t wheel = 0;          // 0..15
    uint8_t portButtons = 0xFF; // what IN #FADF returns from the device right now
    uint8_t portX = 0xFF;       // IN #FBDF
    uint8_t portY = 0xFF;       // IN #FFDF
    std::optional<MouseButton> pendingClickButton;
    uint16_t pendingClickFramesLeft = 0;
    bool journalSupported = true;  // TTD records mouse input (MouseMove/Buttons/Wheel/Counters)

    bool IsPressed(MouseButton button) const { return (buttonMask & static_cast<uint8_t>(button)) == 0; }
};

/// Single funnel for Kempston Mouse input (Kempston Mouse design §5.7, automation-interfaces §4.1).
///
/// Every source goes through here: automation (WebAPI, CLI, Lua, Python, MCP), the
/// desktop front end (via Mouse::OnMouse* on the MessageCenter thread) and tests.
/// That makes it the one place that refuses live input during TTD replay and that
/// journals input while TTD records - BEFORE the device is changed, same as
/// DebugKeyboardManager.
///
/// Automation methods validate ranges and return a status with a reason; the Apply*
/// host methods skip the automation limits (a fast host flick may exceed ±127 in one
/// event) but keep the replay guard and the journal.
///
/// Delivery is a direct call on the caller's thread (design Q1 option B): the change is
/// visible before the call returns, so pause -> inject -> run_frames is deterministic.
/// Mouse counters are atomic, so host and automation input compose without loss.
class DebugMouseManager
{
public:
    static constexpr uint16_t DEFAULT_CLICK_FRAMES = 2;  // same default as keyboard tap
    static constexpr int MAX_MOVE_PER_CALL = 127;        // a larger counter jump reads as a move the other way
    static constexpr int MAX_WHEEL_PER_CALL = 7;         // same ambiguity on the 4-bit wheel nibble
    static constexpr uint32_t MAX_CLICK_FRAMES = 65535;

    explicit DebugMouseManager(EmulatorContext* context);
    ~DebugMouseManager() = default;

    DebugMouseManager(const DebugMouseManager&) = delete;
    DebugMouseManager& operator=(const DebugMouseManager&) = delete;

    /// region <Automation - immediate, validated>
    MouseInjectResult Move(int dx, int dy);
    MouseInjectResult Wheel(int steps);
    MouseInjectResult PressButton(MouseButton button);
    MouseInjectResult ReleaseButton(MouseButton button);
    MouseInjectResult SetPressedButtons(uint8_t pressedBits);  // bit set = pressed (D0 L, D1 R, D2 M)
    MouseInjectResult ReleaseAllButtons();                     // also cancels a pending click
    MouseInjectResult SetCounters(int x, int y);               // debug: raw counter write, 0..255
    /// endregion </Automation - immediate, validated>

    /// region <Automation - timed>
    /// Press now, release after holdFrames emulated frames (released in OnFrame)
    MouseInjectResult Click(MouseButton button, uint32_t holdFrames = DEFAULT_CLICK_FRAMES);
    bool IsClickPending() const;
    void AbortClick();  // releases the held button now
    /// endregion </Automation - timed>

    /// region <Host input - replay guard + journal, no automation range limits>
    void ApplyHostMove(int dx, int dy);
    void ApplyHostButtons(uint8_t activeLowMask);
    void ApplyHostWheel(int steps);
    /// endregion </Host input>

    MouseStateSnapshot GetState() const;

    /// region <Names>
    static std::optional<MouseButton> ResolveButtonName(const std::string& name);  // left/l, right/r, middle/m
    static std::string GetButtonName(MouseButton button);
    static std::vector<std::string> GetAllButtonNames();
    /// endregion </Names>

    /// Frame pump (emulator thread, MainLoop::OnFrameEnd)
    void OnFrame();

private:
    Mouse* Device() const;
    MouseInjectResult Guard() const;  // NoDevice / ReplayActive
    MouseInjectResult Success(const std::string& message) const;

    // Journal + apply: shared by automation, host and timed paths
    void ApplyMove(Mouse& mouse, int dx, int dy);
    void ApplyButtons(Mouse& mouse, uint8_t activeLowMask);
    void ApplyWheel(Mouse& mouse, int steps);
    void ApplyCounters(Mouse& mouse, uint8_t x, uint8_t y);
    bool IsRecording() const;
    bool IsReplaying() const;

    void CancelPendingLocked();

    EmulatorContext* _context = nullptr;

    mutable std::mutex _mutex;  // pending click state: automation thread vs emulator thread
    std::optional<MouseButton> _pendingButton;
    uint16_t _pendingFrames = 0;
};
