#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "emulator/io/mouse/mousedevicestatus.h"

class EmulatorContext;
class Mouse;
namespace ttd
{
struct TTDInputEvent;
}

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
    ReplayActive,     // TTD replay in progress - live input refused
    NoMouseFitted     // the machine has no mouse a program can read ([INPUT] Mouse=NONE, feature off); message says why
};

struct MouseInjectResult
{
    MouseInjectStatus status = MouseInjectStatus::Ok;
    std::string message;  // human-readable, reused by every front end
    std::string warning;  // set on success when part of the input cannot reach the guest (no wheel fitted)
    bool queued = false;  // accepted behind a glide still in progress: applied in order at frame ends

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

    /// The machine's mouse (docs/inprogress/2026-10-03-mouse-api-routing/design.md): `device` is the one
    /// reported (the first fitted one, or the one asked for); `devices` every device the ports read
    bool mouseFitted = false;
    std::optional<MouseDeviceStatus> device;
    std::vector<MouseDeviceStatus> devices;
    /// Automation input waiting behind a glide
    size_t queuedOps = 0;
    int glideRemainingDx = 0;
    int glideRemainingDy = 0;

    bool IsPressed(MouseButton button) const { return (buttonMask & static_cast<uint8_t>(button)) == 0; }
};

/// Single funnel for Kempston Mouse input (Kempston Mouse design §5.7, automation-interfaces §4.1).
///
/// Every source goes through here: automation (WebAPI, CLI, Lua, Python, MCP), the
/// desktop front end (via MouseManager::OnMouse* on the MessageCenter thread) and tests.
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
/// Mouse counters are atomic, so host and automation input compose without loss; the
/// held buttons of each source are kept apart in the emulator's MouseManager and the
/// machine gets their union (mouse-manager design §3.1).
class DebugMouseManager
{
public:
    static constexpr uint16_t DEFAULT_CLICK_FRAMES = 2;  // same default as keyboard tap
    static constexpr int MAX_MOVE_PER_CALL = 127;        // a larger counter jump reads as a move the other way
    static constexpr int MAX_WHEEL_PER_CALL = 7;         // same ambiguity on the 4-bit wheel nibble
    static constexpr uint32_t MAX_CLICK_FRAMES = 65535;
    static constexpr int MAX_GLIDE_PER_CALL = 4096;      // a glide's total per axis (stepped, see Glide)
    static constexpr uint16_t GLIDE_WAIT_FRAMES = 10;    // a step waits at most this long for the program's read

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

    /// region <Automation - glide (design 2026-10-03 §4)>
    /// A move of up to MAX_GLIDE_PER_CALL per axis in steps a program can follow: the first step now,
    /// one per frame after it, each once the program has read the last (IMouseSink::HasUnreadMotion,
    /// at most GLIDE_WAIT_FRAMES). A step is at most MouseManager::MotionStepLimit (127 for 8-bit
    /// counters). Input sent while a glide is in progress queues behind it and is applied in order,
    /// one item per frame: a click after a glide lands where the glide ended.
    /// Worked example (Kempston, a program polling every frame): Glide(300, 0) -> X += 127 now,
    /// +127 at the end of the next frame, +46 at the end of the frame after
    MouseInjectResult Glide(int dx, int dy);
    /// The glide and the input queued behind it are still in progress
    bool IsBusy() const;
    /// Drop the queued input (the motion already applied stays)
    void CancelQueue();
    /// endregion

    /// The device the surfaces report: "" = the default (the first fitted one). InvalidArgument for an id
    /// the machine does not have (the message lists the ones it has)
    MouseInjectResult CheckDevice(const std::string& deviceId) const;

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

    MouseStateSnapshot GetState(const std::string& deviceId = "") const;

    /// region <Names>
    static std::optional<MouseButton> ResolveButtonName(const std::string& name);  // left/l, right/r, middle/m
    static std::string GetButtonName(MouseButton button);
    static std::vector<std::string> GetAllButtonNames();
    /// endregion </Names>

    /// Frame pump (emulator thread, MainLoop::CompleteFrame)
    void OnFrame();

private:
    Mouse* Device() const;
    MouseInjectResult Guard() const;  // NoDevice / ReplayActive / NoMouseFitted
    MouseInjectResult Success(const std::string& message) const;

    // Submit through the TTD live-input gateway (ownership + journal + apply on the
    // machine's thread): shared by automation, host and timed paths
    bool Submit(const ttd::TTDInputEvent& ev, Mouse& mouse);
    void ApplyMove(Mouse& mouse, int dx, int dy);
    void ApplyButtons(Mouse& mouse, uint8_t activeLowMask);
    void ApplyWheel(Mouse& mouse, int steps);
    void ApplyCounters(Mouse& mouse, uint8_t x, uint8_t y);
    /// Automation's held buttons (bit set = held) and their submission joined with the host's
    uint8_t AutomationPressed() const;
    void SubmitAutomationButtons(Mouse& mouse, uint8_t pressedBits);
    bool IsReplaying() const;

    void CancelPendingLocked();

    /// region <Queue behind a glide>
    struct QueuedOp
    {
        enum class Kind : uint8_t
        {
            Motion,
            Press,
            Release,
            SetButtons,
            Wheel,
            Click,
        };
        Kind kind = Kind::Motion;
        int dx = 0;
        int dy = 0;
        uint8_t bits = 0;  // SetButtons: pressed bits; Press / Release / Click: the button
        uint32_t frames = 0;  // Click: hold
    };
    /// Queue `op` when input is waiting (returns true, result marked queued)
    bool EnqueueIfBusyLocked(const QueuedOp& op, MouseInjectResult& result);
    /// One step of a motion op: at most the step limit per axis; returns true when the op is done
    bool ApplyMotionStepLocked(Mouse& mouse, QueuedOp& op);
    void ApplyQueuedLocked(Mouse& mouse, QueuedOp& op);
    void ProcessQueueLocked(Mouse* mouse);
    int StepLimit() const;
    bool HasUnreadMotion() const;
    /// endregion

    EmulatorContext* _context = nullptr;

    mutable std::mutex _mutex;  // pending click state: automation thread vs emulator thread
    std::optional<MouseButton> _pendingButton;
    uint16_t _pendingFrames = 0;
    std::deque<QueuedOp> _queue;
    uint16_t _waitFrames = 0;
};
