#pragma once

/// @file closeprompt.h
/// @brief Qt-free logic of the question the main window asks before it closes an emulator whose media have unsaved
/// changes (D-8): which media need the user (writes.save: ask, and media whose changes would be lost) and what
/// closing does with the others. The core decides by policy; the window only asks where the policy leaves it to the
/// user. Design: docs/inprogress/2026-10-05-media-multisource/flatten-strategies.md §3.

#include <string>
#include <vector>

#include "emulator/media/mediamanager.h"

struct UnsavedMediumLine
{
    std::string slot;
    std::string source;
    std::string changes;    ///< "12 sectors", for people
    std::string onRelease;  ///< SlotInfo::onRelease
    bool asks = false;      ///< the user decides: ask, lost
    std::string fate;       ///< what closing does with the changes, for people
};

struct ClosePrompt
{
    std::vector<UnsavedMediumLine> media;  ///< every medium with unsaved changes, the ones that ask first
    bool ask = false;                      ///< some medium needs the user before the close goes on
};

/// The question for `Unsaved()` of the emulator that closes
ClosePrompt ClosePromptFor(const std::vector<SlotInfo>& unsaved);

/// What closing does with a medium's changes (SlotInfo::onRelease), for people
std::string ReleaseFateText(const std::string& onRelease);
