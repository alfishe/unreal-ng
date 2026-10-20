// Stateful parser: devices report full button state, we emit edges.  docs/05-input-events.md
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "sdb/models.h"

namespace sdb
{

enum class EventType : std::uint8_t
{
    KeyDown,
    KeyUp,
    TouchPointDown,
    TouchPointUp,
    DialDown,
    DialUp,
    DialTurn,
    LcdTap,
    LcdLongPress,
    LcdSwipe
};

// Where an input report came from: a USB device, or Stream Deck Studio (clicked on screen).
enum class InputSource : std::uint8_t
{
    Usb,
    Emulator
};
[[nodiscard]] inline const char* ToString(InputSource s)
{
    return s == InputSource::Emulator ? "emu" : "usb";
}

struct Event
{
    EventType type{};
    int index = 0;         // key / touch point / dial
    int delta = 0;         // DialTurn: signed ticks
    int x = 0, y = 0;      // Lcd*: raw device coordinates as reported
    int x2 = 0, y2 = 0;    // LcdSwipe end
    std::int64_t tUs = 0;  // host monotonic timestamp, filled by the reader
    InputSource source = InputSource::Usb;
};

[[nodiscard]] std::string ToString(EventType);
[[nodiscard]] std::string Describe(const Event&);

class InputParser
{
public:
    explicit InputParser(const Model& m);
    // `buf` is exactly what hid_read returned (byte 0 = report ID); empty/zero first byte => no events.
    [[nodiscard]] std::vector<Event> Feed(std::span<const std::uint8_t> buf);

private:
    const Model& _m;
    std::vector<bool> _buttons, _dials;
};

}  // namespace sdb
