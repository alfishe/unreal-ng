#include "stdafx.h"

#include "msserialmouse.h"

#include <algorithm>
#include <cstring>

MsSerialMouse::MsSerialMouse()
{
    SetBaseClock(3500000);
    Clear();
}

void MsSerialMouse::SetBaseClock(uint32_t baseHz)
{
    if (baseHz == 0)
        baseHz = 3500000;
    _charT = static_cast<uint32_t>((static_cast<uint64_t>(baseHz) * kBitsPerCharacter + kBaud / 2) / kBaud);
}

void MsSerialMouse::BuildPacket(int dx, int dy, bool left, bool right, uint8_t out[3])
{
    const uint8_t x = static_cast<uint8_t>(static_cast<int8_t>(dx));
    const uint8_t y = static_cast<uint8_t>(static_cast<int8_t>(dy));
    out[0] = static_cast<uint8_t>(0x40 | (left ? 0x20 : 0) | (right ? 0x10 : 0) | ((y >> 6) << 2) | (x >> 6));
    out[1] = static_cast<uint8_t>(x & 0x3F);
    out[2] = static_cast<uint8_t>(y & 0x3F);
}

void MsSerialMouse::Advance(uint64_t now)
{
    for (;;)
    {
        if (_state.sent < 3)
        {
            if (_state.nextByteAt > now)
                return;
            const uint8_t value = _state.packet[_state.sent++];
            const uint64_t at = _state.nextByteAt;
            _state.nextByteAt += _charT;
            if (_sink)
                _sink(value, at);
            continue;
        }

        // Idle: a new packet when the mouse moved or a button changed
        if (!_sampler)
            return;
        uint8_t x = 0, y = 0, buttons = 0xFF;
        _sampler(x, y, buttons);
        buttons |= 0xFC;  // two buttons
        if (!_state.synced)
        {
            _state.lastX = x;
            _state.lastY = y;
            _state.lastButtons = buttons;
            _state.synced = 1;
            return;
        }

        const int dx = std::clamp(static_cast<int>(static_cast<int8_t>(static_cast<uint8_t>(x - _state.lastX))), -127, 127);
        // Kempston Y grows upward, the serial mouse counts down
        const int up = std::clamp(static_cast<int>(static_cast<int8_t>(static_cast<uint8_t>(y - _state.lastY))), -127, 127);
        if (dx == 0 && up == 0 && buttons == _state.lastButtons)
            return;

        _state.lastX = static_cast<uint8_t>(_state.lastX + dx);
        _state.lastY = static_cast<uint8_t>(_state.lastY + up);
        _state.lastButtons = buttons;
        BuildPacket(dx, -up, (buttons & 0x01) == 0, (buttons & 0x02) == 0, _state.packet);
        _state.sent = 0;
        _packetsSent++;
        // The line was idle: the first character's frame starts now (the last one ended before)
        _state.nextByteAt = now + _charT;
    }
}

void MsSerialMouse::PendingMotion(int& dx, int& up) const
{
    dx = 0;
    up = 0;
    if (!_sampler || !_state.synced)
        return;
    uint8_t x = 0, y = 0, buttons = 0xFF;
    _sampler(x, y, buttons);
    dx = static_cast<int8_t>(static_cast<uint8_t>(x - _state.lastX));
    up = static_cast<int8_t>(static_cast<uint8_t>(y - _state.lastY));
}

void MsSerialMouse::Rebase(uint64_t now)
{
    if (_state.sent < 3)
        _state.nextByteAt = now + _charT;
}

void MsSerialMouse::Clear()
{
    std::memset(&_state, 0, sizeof(_state));
    _state.sent = 3;
    _packetsSent = 0;
}
