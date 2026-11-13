#pragma once

/// @file handheldmode.h
/// @brief Handheld mode (handheld-menu design C-2): unreal-qt on a handheld without a keyboard or a mouse, driven by
/// its controls. Today the Steam Deck in Game Mode; Windows handhelds later go behind the same predicate.

namespace handheld
{

/// The handheld menu is available: Steam Deck Game Mode, or UNREAL_HANDHELD=1 (a desktop testing it). UNREAL_HANDHELD=0
/// turns it off even in Game Mode
bool IsHandheldMode();

}  // namespace handheld
