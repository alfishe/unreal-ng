// key-input.h - a terminal key event as the monitor's KeyPress (ui/actions.h).
//
// A terminal sends no modifiers with the F-keys, Tab and Home, and turns Ctrl+H/I/J/M into Backspace, Tab and Enter:
// those UnrealSpeccy keys (Ctrl+Tab, Alt+F4, Ctrl+J ...) are reached through the system menu (F10).
#pragma once

#include <ftxui/component/event.hpp>

#include "ui/actions.h"

namespace dbg {

KeyPress KeyFromTerminal(const ftxui::Event& e);

}  // namespace dbg
