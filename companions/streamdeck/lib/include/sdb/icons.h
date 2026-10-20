// Procedural icons for emulator status (no image assets needed). All scale to any square size.
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "sdb/draw.h"

namespace sdb
{

enum class Icon
{
    Tape,
    Disk,
    Play,
    Pause,
    Stop,
    Record,
    Reset,
    Speaker,
    Mute,
    Rewind,
    Forward,
    StepBack,
    Flag,
    Bug,
    Gear,
    Check,
    Cross,
    Warn,
    Camera,
    Chip,
    StepInto,
    StepOver,
    StepOut,
    Eye,
    Memory,
    Regs,
    Disasm,
    Stack  // debugger
};

[[nodiscard]] std::optional<Icon> IconByName(const std::string& name);
[[nodiscard]] std::vector<std::string> IconNames();

// Draws into the square (x, y, size, size). `fg` is the main colour, `accent` the secondary one.
// `frame` animates moving parts (tape reels spin, disk activity LED blinks).
void DrawIcon(Rgb& im, Icon icon, int x, int y, int size, draw::Color fg, draw::Color accent, int frame = 0);

}  // namespace sdb
