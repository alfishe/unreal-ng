#include "stdafx.h"

#include "evoavrmouse.h"

#include <algorithm>
#include <cstdlib>

EvoAvrMouse::EvoAvrMouse(std::function<uint8_t()> resolution, std::function<void(uint8_t)> setResolution)
    : _resolution(std::move(resolution)), _setResolution(std::move(setResolution))
{
}

void EvoAvrMouse::ResetRegisters(bool found)
{
    // zx.c zx_mouse_reset
    _x.store(found ? kFoundX : 0xFF, std::memory_order_relaxed);
    _y.store(found ? kFoundY : 0xFF, std::memory_order_relaxed);
    _buttons.store(0xFF, std::memory_order_relaxed);
}

void EvoAvrMouse::SetConnected(bool connected)
{
    const uint8_t value = connected ? 1 : 0;
    if (_connected.exchange(value, std::memory_order_relaxed) != value)
        ResetRegisters(connected);
}

uint8_t EvoAvrMouse::ReadRegister(uint8_t selectRegister) const
{
    switch (selectRegister)
    {
        case 0:
            return _buttons.load(std::memory_order_relaxed);
        case 1:
            return _x.load(std::memory_order_relaxed);
        case 2:
            return _y.load(std::memory_order_relaxed);
        default:
            return 0xFF;
    }
}

uint8_t EvoAvrMouse::Resolution() const
{
    return static_cast<uint8_t>((_resolution ? _resolution() : 0) & 0x03);
}

void EvoAvrMouse::OnKeypadKey(uint8_t scancode)
{
    // ps2.c ps2mouse_set_resolution: only while left and right are both held
    if (!IsConnected() || (_buttons.load(std::memory_order_relaxed) & 0x03) != 0 || !_setResolution)
        return;

    uint8_t resolution = Resolution();
    switch (scancode)
    {
        case 0x7C:  // keypad '*': the default
            resolution = 0;
            break;
        case 0x79:  // keypad '+'
            resolution = static_cast<uint8_t>(std::min(resolution + 1, 3));
            break;
        case 0x7B:  // keypad '-'
            resolution = static_cast<uint8_t>(resolution > 0 ? resolution - 1 : 0);
            break;
        default:
            return;
    }
    _setResolution(resolution);
}

void EvoAvrMouse::OnMouseMotion(int dx, int dy)
{
    if (!IsConnected())
        return;

    // Counts at the mouse's resolution; the AVR adds each packet's 8-bit movement
    // bytes to its registers, so the sum lands modulo 256 however it is split
    const int scale = 1 << Resolution();
    const int countsX = dx * scale;
    const int countsY = dy * scale;
    _x.store(static_cast<uint8_t>(_x.load(std::memory_order_relaxed) + countsX), std::memory_order_relaxed);
    _y.store(static_cast<uint8_t>(_y.load(std::memory_order_relaxed) + countsY), std::memory_order_relaxed);
}

void EvoAvrMouse::OnMouseButtons(uint8_t activeLowMask)
{
    if (!IsConnected())
        return;

    // Packet byte 1 has bit 3 set and L / R / M active high; the AVR stores (b ^ 7) & #0F
    const uint8_t pressed = static_cast<uint8_t>(~activeLowMask & 0x07);
    const uint8_t low = static_cast<uint8_t>((0x08 | pressed) ^ 0x07);
    const uint8_t old = _buttons.load(std::memory_order_relaxed);
    _buttons.store(static_cast<uint8_t>((old & 0xF0) | low), std::memory_order_relaxed);
}

void EvoAvrMouse::OnMouseWheel(int steps)
{
    if (!IsConnected())
        return;

    // Z is negative when the wheel rolls away from the user; the AVR adds it to the high nibble
    const int z = -steps;
    const uint8_t old = _buttons.load(std::memory_order_relaxed);
    _buttons.store(static_cast<uint8_t>(old + ((z << 4) & 0xF0)), std::memory_order_relaxed);
}

void EvoAvrMouse::OnMouseCounters(uint8_t x, uint8_t y)
{
    // Debug write (automation `counters`): the registers as given
    _x.store(x, std::memory_order_relaxed);
    _y.store(y, std::memory_order_relaxed);
}

EvoAvrMouse::State EvoAvrMouse::GetState() const
{
    return State{_x.load(std::memory_order_relaxed), _y.load(std::memory_order_relaxed),
                 _buttons.load(std::memory_order_relaxed), _connected.load(std::memory_order_relaxed)};
}

void EvoAvrMouse::SetState(const State& state)
{
    _x.store(state.x, std::memory_order_relaxed);
    _y.store(state.y, std::memory_order_relaxed);
    _buttons.store(state.buttons, std::memory_order_relaxed);
    _connected.store(state.connected ? 1 : 0, std::memory_order_relaxed);
}
