#pragma once

#include <QString>

#include <cstdint>
#include <string>

#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/notifications.h"
#include "emulator/sound/chips/gs/gsdebughooks.h"

/// A breakpoint hit as NC_EXECUTION_BREAKPOINT carries it (copied off the message centre's thread)
struct BreakpointHit
{
    std::string emulatorId;
    uint16_t id = BRK_INVALID;
    uint16_t address = 0;
    BreakpointHitKind kind = BreakpointHitKind::Execute;
    std::string cpu = "main";
    uint16_t cpuPc = 0;
};

/// The stop line for a hit of `breakpoint`: which breakpoint, on what access, the condition that held, the hit count
/// (rich text with the links "edit" and "list")
QString BreakpointStopLine(const BreakpointDescriptor& breakpoint, const BreakpointHit& hit);

/// An event breakpoint's kind (BRK_EVENT) in plain words, its value filter in them: "the card takes a command",
/// "the host sends command #30", "the card accepts an interrupt, value #05 & #0F" (`mask` 0: any value). `past`: as a
/// stop tells it ("the card took command #0F")
QString EventBreakpointWords(GSCardEvent kind, uint8_t value, uint8_t mask, bool past = false);
/// The group an event kind is listed under: "Card", "Protocol, card side", "Protocol, host side", "Machine"
QString EventBreakpointGroup(GSCardEvent kind);
