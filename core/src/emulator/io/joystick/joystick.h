#pragma once
#include "stdafx.h"

#include <array>
#include <atomic>
#include <string>

#include "debugger/ttd/ttdserializable.h"

class EmulatorContext;
class ModuleLogger;
enum class PcKey : uint8_t;

/// Kempston joystick device: one state byte, active high (a set bit = pressed).
///
/// D0 right, D1 left, D2 down, D3 up, D4 fire; D5..D7 are raw bits a Sega pad or an
/// extended interface drives (the ZX-Evo AVR sends 0 there for a switch joystick, and so
/// does an idle device). Idle value is 0x00: that is what the Evo board gives, not the
/// open-bus 1-padding of the classic interface (joystick TDD §1).
///
/// State is written from more than one thread - the machine thread applies journaled
/// events (PC keys, DebugJoystickManager), automation reads - so the byte and the binding
/// table are atomic. Fitting (present) and the key bindings come from the machine config
/// and are not device state: Reset() keeps them and TTD does not save them.
class Joystick : public ttd::TTDSerializable
{
public:
    static constexpr uint8_t kRight = 0x01;
    static constexpr uint8_t kLeft = 0x02;
    static constexpr uint8_t kDown = 0x04;
    static constexpr uint8_t kUp = 0x08;
    static constexpr uint8_t kFire = 0x10;

    /// Default [INPUT] JoystickKeys= (pckey names)
    static constexpr const char* kDefaultKeys = "up:kp8,down:kp2,left:kp4,right:kp6,fire:kp0";

    explicit Joystick(EmulatorContext* context);
    ~Joystick() override = default;

    /// Power-on: every button released. Fitting and bindings are kept
    void Reset();

    /// The value the port answers: the state, or 0x00 when the interface is not fitted
    uint8_t Read() const { return IsPresent() ? State() : 0x00; }

    /// Raw state write (atomic)
    void SetState(uint8_t state) { _state.store(state, std::memory_order_relaxed); }
    void Press(uint8_t mask) { _state.fetch_or(mask, std::memory_order_relaxed); }
    void Release(uint8_t mask) { _state.fetch_and(static_cast<uint8_t>(~mask), std::memory_order_relaxed); }
    uint8_t State() const { return _state.load(std::memory_order_relaxed); }

    /// Fitted at all ([INPUT] Joystick=KEMPSTON and the kempstonjoystick feature)
    void SetPresent(bool present) { _present.store(present, std::memory_order_relaxed); }
    bool IsPresent() const { return _present.load(std::memory_order_relaxed); }

    /// Apply [INPUT] Joystick=, JoystickKeys= and the kempstonjoystick feature
    void ApplyConfiguration();

    /// region <Host keys (decision J6)>
    /// Replace the binding table from a "button:key,..." list (button names up/down/left/right/fire/b5/b6/b7,
    /// keys as pckey::FromName, plus the short keypad spelling kp8 for kp_8). An empty list unbinds every key.
    /// Entries that do not parse are skipped.
    /// @return false when some entry was skipped (the rest is applied)
    bool SetBindings(const std::string& spec);

    /// The current table as a "button:key,..." list (bit order, only bound buttons)
    std::string BindingsSpec() const;

    /// True when the key is bound to a button and the device is fitted: the host key reaches the joystick
    bool WantsKey(PcKey key) const;

    /// One applied PC key event. Pressing a bound key presses its button, releasing releases it.
    /// @return true when the key is bound (the device is fitted)
    bool OnPcKey(PcKey key, bool pressed);

    /// The union of every bound button: what a "release all keys" gives back
    void ReleaseBoundKeys();
    /// endregion </Host keys>

    /// region <Names>
    /// up/down/left/right/fire/b5/b6/b7 (case-insensitive) -> mask; 0 when unknown
    static uint8_t MaskFromName(const std::string& name);
    /// One bit -> its name ("" for a mask that is not exactly one bit)
    static std::string NameOfBit(uint8_t mask);
    /// endregion </Names>

    /// region <TTD (joystick TDD §6)>
    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "KempstonJoystick"; }
    ttd::PeripheralId TTDPeripheralId() const override { return ttd::PeripheralId::KempstonJoystick; }
    uint64_t TTDHashState() const override;
    /// endregion </TTD>

private:
    EmulatorContext* _context = nullptr;
    ModuleLogger* _logger = nullptr;

    std::atomic<uint8_t> _state{0x00};
    std::atomic<bool> _present{true};
    /// PcKey bound to each state bit (PcKey::None = 0 = unbound)
    std::array<std::atomic<uint8_t>, 8> _boundKey{};
};
