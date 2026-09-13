#pragma once
#include "stdafx.h"

#include <atomic>

#include "3rdparty/message-center/messagecenter.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/platform.h"

class EmulatorContext;
class ModuleLogger;

extern const char* const MC_MOUSE_MOVE;
extern const char* const MC_MOUSE_BUTTON;
extern const char* const MC_MOUSE_WHEEL;

enum class MouseEventKind : uint8_t
{
    Move,
    Buttons,
    Wheel
};

/// Mouse input as the emulated machine sees it. Host-side concerns (pointer
/// capture, DPI, window upscale, sub-pixel carry, wheel notch accumulation)
/// are resolved by the front end before posting - the core only applies these
/// values to the device counters.
///
/// Built through the named factories: the payload kinds share integer-like
/// fields, and overloaded constructors silently picked the wrong kind for an
/// int literal (MouseEvent(0xFF, id) resolved to the wheel constructor).
class MouseEvent : public MessagePayload
{
public:
    MouseEventKind kind = MouseEventKind::Move;
    int dx = 0;                 // Move: emulated pixels, + = right
    int dy = 0;                 // Move: emulated pixels, + = up (screen Y is inverted by the front end)
    uint8_t buttonMask = 0xFF;  // Buttons: active-low, D0 = Left, D1 = Right, D2 = Middle
    int wheelSteps = 0;         // Wheel: whole notches, + = away from the user
    std::string targetId;       // Emulator id (UUID string); empty = every instance

    static MouseEvent* Move(int dx, int dy, const std::string& targetId = "")
    {
        MouseEvent* event = new MouseEvent(MouseEventKind::Move, targetId);
        event->dx = dx;
        event->dy = dy;
        return event;
    }

    static MouseEvent* Buttons(uint8_t buttonMask, const std::string& targetId = "")
    {
        MouseEvent* event = new MouseEvent(MouseEventKind::Buttons, targetId);
        event->buttonMask = buttonMask;
        return event;
    }

    static MouseEvent* Wheel(int steps, const std::string& targetId = "")
    {
        MouseEvent* event = new MouseEvent(MouseEventKind::Wheel, targetId);
        event->wheelSteps = steps;
        return event;
    }

    ~MouseEvent() override = default;

private:
    MouseEvent(MouseEventKind eventKind, const std::string& target) : MessagePayload(), kind(eventKind), targetId(target) {}
};

/// Kempston Mouse device: three read-only registers (buttons [+ wheel], X, Y).
///
/// State is written from more than one thread - host input arrives on the
/// MessageCenter worker, automation calls DebugMouseManager on its own thread,
/// the Z80 reads on the emulator thread - so every counter is atomic and the
/// relative updates are compare-and-swap loops (no lost moves).
///
/// "Fitting" properties (present, wheel) come from the machine config and are
/// not device state: Reset() keeps them and TTD does not save them.
class Mouse : public Observer, public ttd::TTDSerializable
{
public:
    /// Reset coordinates: two different non-zero values - software infers
    /// "no mouse" from equal axes (hardware-reference §7)
    static constexpr uint8_t RESET_X = 31;
    static constexpr uint8_t RESET_Y = 85;

    explicit Mouse(EmulatorContext* context);
    ~Mouse() override;

    /// Machine reset: counters, buttons and wheel back to power-on values
    void Reset();

    /// Reads mouse register based on register selection
    /// \param selectRegister 0 = buttons (+wheel), 1 = X axis, 2 = Y axis
    uint8_t ReadRegister(uint8_t selectRegister) const;

    void Move(int dx, int dy);
    void SetButtons(uint8_t mask);
    void SetWheel(int delta);
    void SetCounters(uint8_t x, uint8_t y);

    /// Fitted at all (config Mouse=KEMPSTON and the kempstonmouse feature).
    /// Absent: nothing drives the bus - decoders do not claim the ports.
    void SetPresent(bool present) { _present.store(present, std::memory_order_relaxed); }
    bool IsPresent() const { return _present.load(std::memory_order_relaxed); }

    /// Wheel-equipped interface (ZX Evo FPGA / Kempston with wheel): the upper
    /// nibble of the button register carries the wheel counter. Off (default)
    /// = classic Kempston mouse: D7-D3 read 1 as part of the #FF base, which is
    /// what software detection expects (ProfROM #08FB tests bits 5-3 == 1).
    void SetWheelEnabled(bool enabled) { _wheelEnabled.store(enabled, std::memory_order_relaxed); }
    bool IsWheelEnabled() const { return _wheelEnabled.load(std::memory_order_relaxed); }

    /// Apply the machine config (Mouse=, Wheel=) and the kempstonmouse feature to the fitting
    void ApplyConfiguration();

    uint8_t GetX() const { return _x.load(std::memory_order_relaxed); }
    uint8_t GetY() const { return _y.load(std::memory_order_relaxed); }
    uint8_t GetButtons() const { return _buttons.load(std::memory_order_relaxed); }
    uint8_t GetWheel() const { return _wheel.load(std::memory_order_relaxed); }

    // Observer callbacks for MessageCenter (host input from the desktop front end).
    // Routed through DebugMouseManager when present, so host input is replay-guarded
    // and TTD-journalled exactly like automation input.
    void OnMouseMove(int id, Message* message);
    void OnMouseButton(int id, Message* message);
    void OnMouseWheel(int id, Message* message);

    /// region <TTD (Kempston Mouse design §6.1)>
    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "KempstonMouse"; }
    ttd::PeripheralId TTDPeripheralId() const override { return ttd::PeripheralId::KempstonMouse; }
    uint64_t TTDHashState() const override;
    /// endregion </TTD>

private:
    MouseEvent* AcceptEvent(Message* message, MouseEventKind kind) const;

    EmulatorContext* _context = nullptr;
    ModuleLogger* _logger = nullptr;

    std::atomic<uint8_t> _x{RESET_X};
    std::atomic<uint8_t> _y{RESET_Y};
    std::atomic<uint8_t> _buttons{0xFF};  // Active-low: D0 = Left, D1 = Right, D2 = Middle
    std::atomic<uint8_t> _wheel{0x00};    // 4 bits, upper nibble of the button register when the wheel is enabled
    std::atomic<bool> _present{true};
    std::atomic<bool> _wheelEnabled{false};
};
