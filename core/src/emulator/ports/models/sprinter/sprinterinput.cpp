#include "stdafx.h"

#include "sprinterinput.h"

#include "3rdparty/z84c15/z84c15.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/mouse/mousemanager.h"
#include "emulator/ports/models/sprinter/sprinterpldstate.h"
#include "emulator/video/sprinter/sprinterintsource.h"

SprinterInput::SprinterInput(EmulatorContext* context, Z84Lib::Z84C15& chip, SprinterIntSource& intSource,
                             const SprinterPldState& pld)
    : _context(context), _chip(chip), _intSource(intSource), _pld(pld)
{
    const uint32_t baseHz = _context && _context->emulatorState.base_z80_frequency ? _context->emulatorState.base_z80_frequency : 3500000u;
    _keyboard.SetBaseClock(baseHz);
    _mouse.SetBaseClock(baseHz);
    _keyboard.SetClock([this]() { return Now(); });

    // The keyboard's clock and data reach SIO A; with ALL_MODE bits 0 and 3 the PLD raises an INT per byte
    _keyboard.SetByteSink([this](uint8_t value, [[maybe_unused]] uint64_t at) {
        if (!_chip.sio.Receive(0, value))
            _keyboardOverruns++;
        if (KeyboardIntEnabled())
            _intSource.LatchKeyboardInt();
    });

    _mouse.SetSampler([this](uint8_t& x, uint8_t& y, uint8_t& buttons) { SampleMouse(x, y, buttons); });
    _mouse.SetByteSink([this](uint8_t value, [[maybe_unused]] uint64_t at) { _chip.sio.Receive(1, value); });

    if (_context && _context->pMouseManager)
        _context->pMouseManager->AddSink(this);
}

SprinterInput::~SprinterInput()
{
    if (_context && _context->pMouseManager)
        _context->pMouseManager->RemoveSink(this);
}

void SprinterInput::SampleMouse(uint8_t& x, uint8_t& y, uint8_t& buttons) const
{
    x = _mouseX.load(std::memory_order_relaxed);
    y = _mouseY.load(std::memory_order_relaxed);
    buttons = _mouseButtons.load(std::memory_order_relaxed);
}

void SprinterInput::OnMouseMotion(int dx, int dy)
{
    // Compare-and-swap: a host move and an automation move must both land
    uint8_t old = _mouseX.load(std::memory_order_relaxed);
    while (!_mouseX.compare_exchange_weak(old, static_cast<uint8_t>(old + dx), std::memory_order_relaxed))
    {
    }
    old = _mouseY.load(std::memory_order_relaxed);
    while (!_mouseY.compare_exchange_weak(old, static_cast<uint8_t>(old + dy), std::memory_order_relaxed))
    {
    }
}

void SprinterInput::OnMouseButtons(uint8_t activeLowMask)
{
    _mouseButtons.store(activeLowMask, std::memory_order_relaxed);
}

void SprinterInput::OnMouseCounters(uint8_t x, uint8_t y)
{
    _mouseX.store(x, std::memory_order_relaxed);
    _mouseY.store(y, std::memory_order_relaxed);
}

SprinterInput::BoardMouse SprinterInput::GetBoardMouse() const
{
    BoardMouse mouse{};
    SampleMouse(mouse.x, mouse.y, mouse.buttons);
    return mouse;
}

void SprinterInput::SetBoardMouse(const BoardMouse& mouse)
{
    _mouseX.store(mouse.x, std::memory_order_relaxed);
    _mouseY.store(mouse.y, std::memory_order_relaxed);
    _mouseButtons.store(mouse.buttons, std::memory_order_relaxed);
}

uint8_t SprinterInput::ReadMouseView(uint16_t port) const
{
    // MAME sprinter.cpp case 0x58: #FADF buttons, #FBDF X, #FFDF Y (the Kempston address bits A8, A10)
    uint8_t x = 0, y = 0, buttons = 0xFF;
    SampleMouse(x, y, buttons);
    if ((port & 0x0100) == 0)
        return static_cast<uint8_t>(0xF8 | (buttons & 0x07));
    return (port & 0x0400) == 0 ? x : y;
}

uint64_t SprinterInput::Now() const
{
    if (!_context)
        return 0;
    const Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    const uint32_t multiplier = _context->emulatorState.current_z80_frequency_multiplier ? _context->emulatorState.current_z80_frequency_multiplier : 1u;
    return _context->emulatorState.t_states + (z80 ? z80->t / multiplier : 0);
}

void SprinterInput::OnPcKey(PcKey key, bool pressed)
{
    // The PLD watches the same stream: Ctrl + Alt + Del resets the CPU, a bare F12 flips the turbo switch.
    // Both act on the press, before the key's own bytes (the PLD decodes the make code)
    if (pressed && !_keyboard.IsHeld(key))
    {
        const bool ctrl = _keyboard.IsHeld(PcKey::LeftCtrl) || _keyboard.IsHeld(PcKey::RightCtrl);
        const bool alt = _keyboard.IsHeld(PcKey::LeftAlt) || _keyboard.IsHeld(PcKey::RightAlt);
        const bool shift = _keyboard.IsHeld(PcKey::LeftShift) || _keyboard.IsHeld(PcKey::RightShift);
        if ((key == PcKey::Delete || key == PcKey::KeypadDecimal) && ctrl && alt && _onReset)
            _onReset();
        else if (key == PcKey::Function12 && !ctrl && !alt && !shift && _onTurboSwitch)
            _onTurboSwitch();
    }

    _keyboard.OnPcKey(key, pressed);
    if (_onStepHookChange)
        _onStepHookChange();
}

void SprinterInput::ReleaseAllPcKeys()
{
    _keyboard.ReleaseAllPcKeys();
    if (_onStepHookChange)
        _onStepHookChange();
}

bool SprinterInput::KeyboardIntEnabled() const
{
    return (_pld.allMode & 0x09) == 0x09;
}

void SprinterInput::BeforeAllModeWrite()
{
    _keyboard.Advance(Now());
}

void SprinterInput::BeforeChipAccess(uint8_t lowByte)
{
    switch (lowByte)
    {
        case 0x18:
        case 0x19:
            _keyboard.Advance(Now());
            break;
        case 0x1A:
        case 0x1B:
            _mouse.Advance(Now());
            break;
        default:
            break;
    }
}

void SprinterInput::Advance()
{
    const uint64_t now = Now();
    _keyboard.Advance(now);
}

void SprinterInput::Rebase()
{
    const uint64_t now = Now();
    _keyboard.Rebase(now);
    _mouse.Rebase(now);
}

void SprinterInput::Clear()
{
    _keyboard.Clear();
    _mouse.Clear();
    _keyboardOverruns = 0;
}
