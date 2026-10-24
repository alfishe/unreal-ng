#pragma once

/// @file controllersmenu.h
/// @brief Machine > Controllers (docs/inprogress/2026-10-17-controller-manager/ui-ux.md §8): a quick plug per
/// connector (the ControllerControl `plug` / `unplug` verbs every plane has), Swap joystick ports (the `player` verb's
/// choice for the first two pads, or the one pad moved on) and Reset bindings to default (the choices made by hand
/// go). Filled each time the menu opens, so it always shows the machine as it is.

class HostControllers;
class QMenu;

namespace controllersmenu
{
/// Clear @p menu and fill it for the bound machine
void Fill(QMenu* menu, HostControllers* host);
}  // namespace controllersmenu
