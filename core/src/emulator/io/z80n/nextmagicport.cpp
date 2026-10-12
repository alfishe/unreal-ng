#include "stdafx.h"

#include "nextmagicport.h"

#include <algorithm>
#include <cstdio>

const char* NextMagicPortModeName(NextMagicPortMode mode)
{
    switch (mode)
    {
        case NextMagicPortMode::Hex: return "hex";
        case NextMagicPortMode::Dec: return "dec";
        case NextMagicPortMode::Ascii: return "ascii";
        case NextMagicPortMode::Line: return "line";
    }
    return "hex";
}

bool NextMagicPortModeFromString(const std::string& text, NextMagicPortMode& mode)
{
    for (NextMagicPortMode m : {NextMagicPortMode::Hex, NextMagicPortMode::Dec, NextMagicPortMode::Ascii, NextMagicPortMode::Line})
        if (text == NextMagicPortModeName(m))
        {
            mode = m;
            return true;
        }
    return false;
}

void NextMagicPort::Configure(bool enabled, uint16_t port, NextMagicPortMode mode)
{
    if (port != _port || mode != _mode)
        _line.clear();  // a half line of another port or mode is not part of this one
    _enabled = enabled;
    _port = port;
    _mode = mode;
}

void NextMagicPort::Clear()
{
    _head = 0;
    _count = 0;
    _evicted = 0;
    _bytes = 0;
    _line.clear();
}

void NextMagicPort::Push(std::string text, uint32_t frame, uint32_t t, uint16_t pc)
{
    NextMagicPortItem& slot = _ring[_head];
    slot.seq = ++_seq;
    slot.frame = frame;
    slot.t = t;
    slot.pc = pc;
    slot.text = std::move(text);
    _head = (_head + 1) % _ring.size();
    if (_count < _ring.size())
        _count++;
    else
        _evicted++;
}

bool NextMagicPort::Write(uint16_t port, uint8_t value, uint32_t frame, uint32_t t, uint16_t pc)
{
    if (!_enabled || port != _port)
        return false;
    _bytes++;
    char buffer[8];
    switch (_mode)
    {
        case NextMagicPortMode::Hex:
            std::snprintf(buffer, sizeof(buffer), "%02X", value);
            Push(buffer, frame, t, pc);
            break;
        case NextMagicPortMode::Dec:
            std::snprintf(buffer, sizeof(buffer), "%u", value);
            Push(buffer, frame, t, pc);
            break;
        case NextMagicPortMode::Ascii:
            if (value >= 0x20 && value < 0x7F)
                Push(std::string(1, static_cast<char>(value)), frame, t, pc);
            else
            {
                std::snprintf(buffer, sizeof(buffer), "\\x%02X", value);
                Push(buffer, frame, t, pc);
            }
            break;
        case NextMagicPortMode::Line:
            if (value == '\r' || value == '\n')
            {
                if (!_line.empty())
                {
                    Push(std::move(_line), frame, t, pc);
                    _line.clear();
                }
            }
            else
            {
                _line.push_back(static_cast<char>(value));
                if (_line.size() >= kMaxLineLength)
                {
                    Push(std::move(_line), frame, t, pc);
                    _line.clear();
                }
            }
            break;
    }
    return true;
}

std::vector<NextMagicPortItem> NextMagicPort::Query(uint64_t since, size_t limit) const
{
    std::vector<NextMagicPortItem> out;
    const size_t first = (_head + _ring.size() - _count) % _ring.size();
    for (size_t i = 0; i < _count; i++)
    {
        const NextMagicPortItem& item = _ring[(first + i) % _ring.size()];
        if (item.seq > since)
            out.push_back(item);
    }
    if (limit > 0 && out.size() > limit)
        out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(limit));
    return out;
}
