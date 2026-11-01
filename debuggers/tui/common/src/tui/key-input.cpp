// key-input.cpp - a terminal key event as the monitor's KeyPress.
#include "tui/key-input.h"

#include <string>

namespace dbg {

KeyPress KeyFromTerminal(const ftxui::Event& e) {
    using ftxui::Event;
    struct Named {
        const Event& event;
        Key key;
        unsigned mods;
    };
    const Named named[] = {
        {Event::Escape, Key::Esc, 0},     {Event::Tab, Key::Tab, 0},         {Event::TabReverse, Key::Tab, kModShift},
        {Event::Return, Key::Enter, 0},   {Event::Backspace, Key::Backspace, 0}, {Event::Home, Key::Home, 0},
        {Event::End, Key::End, 0},        {Event::ArrowUp, Key::Up, 0},      {Event::ArrowDown, Key::Down, 0},
        {Event::ArrowLeft, Key::Left, 0}, {Event::ArrowRight, Key::Right, 0}, {Event::PageUp, Key::PgUp, 0},
        {Event::PageDown, Key::PgDn, 0},  {Event::F1, Key::F1, 0},           {Event::F2, Key::F2, 0},
        {Event::F3, Key::F3, 0},          {Event::F4, Key::F4, 0},           {Event::F5, Key::F5, 0},
        {Event::F6, Key::F6, 0},          {Event::F7, Key::F7, 0},           {Event::F8, Key::F8, 0},
        {Event::F9, Key::F9, 0},          {Event::F10, Key::F10, 0},         {Event::F11, Key::F11, 0},
        {Event::F12, Key::F12, 0},
    };
    KeyPress k;
    for (const Named& n : named) {
        if (e == n.event) {
            k.key = n.key;
            k.mods = n.mods;
            return k;
        }
    }
    const std::string& in = e.input();
    const auto setChar = [&k](char c, unsigned mods) {
        if (c == ' ' && mods == 0) {
            k.key = Key::Space;
            return;
        }
        k.key = Key::Char;
        k.mods = mods;
        if (c >= 'A' && c <= 'Z') {
            k.ch = static_cast<char>(c - 'A' + 'a');
            k.mods |= kModShift;
        } else {
            k.ch = c;
        }
    };
    if (in.size() == 1) {
        const unsigned char c = static_cast<unsigned char>(in[0]);
        if (c >= 0x20 && c < 0x7F) setChar(static_cast<char>(c), 0);
        else if (c >= 0x01 && c <= 0x1A) setChar(static_cast<char>('a' + c - 1), kModCtrl);  // Ctrl+letter
        return k;
    }
    // Alt+key: ESC and the key
    if (in.size() == 2 && in[0] == '\x1b') {
        const unsigned char c = static_cast<unsigned char>(in[1]);
        if (c >= 0x20 && c < 0x7F) setChar(static_cast<char>(c), kModAlt);
    }
    return k;
}

}  // namespace dbg
