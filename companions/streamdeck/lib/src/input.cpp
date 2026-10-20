#include "sdb/input.h"

#include "sdb/format.h"
#include "sdb/protocol.h"

namespace sdb
{

std::string ToString(EventType t)
{
    switch (t)
    {
        case EventType::KeyDown:
            return "key_down";
        case EventType::KeyUp:
            return "key_up";
        case EventType::TouchPointDown:
            return "touchpoint_down";
        case EventType::TouchPointUp:
            return "touchpoint_up";
        case EventType::DialDown:
            return "dial_down";
        case EventType::DialUp:
            return "dial_up";
        case EventType::DialTurn:
            return "dial_turn";
        case EventType::LcdTap:
            return "lcd_tap";
        case EventType::LcdLongPress:
            return "lcd_long_press";
        case EventType::LcdSwipe:
            return "lcd_swipe";
    }
    return "?";
}

std::string Describe(const Event& e)
{
    switch (e.type)
    {
        case EventType::DialTurn:
            return sdb::Format("{} #{} {:+d}", ToString(e.type), e.index, e.delta);
        case EventType::LcdTap:
        case EventType::LcdLongPress:
            return sdb::Format("{} raw=({},{})", ToString(e.type), e.x, e.y);
        case EventType::LcdSwipe:
            return sdb::Format("{} raw=({},{})->({},{})", ToString(e.type), e.x, e.y, e.x2, e.y2);
        default:
            return sdb::Format("{} #{}", ToString(e.type), e.index);
    }
}

InputParser::InputParser(const Model& m) : _m(m), _buttons(m.TotalButtons(), false), _dials(m.dials, false) {}

namespace
{
int Le16(std::span<const std::uint8_t> b, size_t o)
{
    return b[o] | (b[o + 1] << 8);
}
}  // namespace

std::vector<Event> InputParser::Feed(std::span<const std::uint8_t> b)
{
    std::vector<Event> out;
    if (b.size() < 2 || b[0] == 0)
        return out;

    auto diffButtons = [&](std::vector<bool> now) {
        for (size_t i = 0; i < now.size() && i < _buttons.size(); ++i)
        {
            if (now[i] == _buttons[i])
                continue;
            const bool isTouch = static_cast<int>(i) >= _m.keys;
            Event e;
            e.index = isTouch ? static_cast<int>(i) - _m.keys : static_cast<int>(i);
            e.type = isTouch ? (now[i] ? EventType::TouchPointDown : EventType::TouchPointUp)
                             : (now[i] ? EventType::KeyDown : EventType::KeyUp);
            out.push_back(e);
        }
        _buttons = std::move(now);
    };
    auto readKeys = [&](size_t offset, size_t count) {
        std::vector<bool> now(count, false);
        for (size_t i = 0; i < count && offset + i < b.size(); ++i)
            now[i] = b[offset + i] != 0;
        return now;
    };

    if (_m.dials > 0)
    {  // Plus / Plus XL
        switch (b[1])
        {
            case 0x00:
                diffButtons(readKeys(4, _m.keys));
                break;
            case 0x02:
                if (b.size() >= 10)
                {
                    Event e;
                    e.x = Le16(b, 6);
                    e.y = Le16(b, 8);
                    switch (b[4])
                    {
                        case 1:
                            e.type = EventType::LcdTap;
                            break;
                        case 2:
                            e.type = EventType::LcdLongPress;
                            break;
                        case 3:
                            if (b.size() < 14)
                                return out;
                            e.type = EventType::LcdSwipe;
                            e.x2 = Le16(b, 10);
                            e.y2 = Le16(b, 12);
                            break;
                        default:
                            return out;
                    }
                    out.push_back(e);
                }
                break;
            case 0x03:
                if (b.size() >= static_cast<size_t>(5 + _m.dials))
                {
                    if (b[4] == 0)
                    {
                        for (int i = 0; i < _m.dials; ++i)
                        {
                            const bool now = b[5 + i] != 0;
                            if (now != _dials[i])
                            {
                                out.push_back({now ? EventType::DialDown : EventType::DialUp, i});
                                _dials[i] = now;
                            }
                        }
                    }
                    else if (b[4] == 1)
                    {
                        for (int i = 0; i < _m.dials; ++i)
                        {
                            const int d = static_cast<std::int8_t>(b[5 + i]);
                            if (d != 0)
                            {
                                Event e{EventType::DialTurn, i};
                                e.delta = d;
                                out.push_back(e);
                            }
                        }
                    }
                }
                break;
            default:
                break;
        }
        return out;
    }

    if (_m.pid == 0x0060)
    {  // Original v1: mirrored columns, keys from byte 1
        std::vector<bool> now(_m.keys, false);
        for (int i = 0; i < _m.keys; ++i)
        {
            const size_t src = 1 + static_cast<size_t>(proto::FlipKeyOriginal(_m, i));
            now[i] = src < b.size() && b[src] != 0;
        }
        diffButtons(std::move(now));
    }
    else if (_m.family == Family::V1)
    {
        diffButtons(readKeys(1, _m.keys));
    }
    else
    {
        diffButtons(readKeys(4, _m.TotalButtons()));
    }
    return out;
}

}  // namespace sdb
