#pragma once

/// @file handheldcontrol.h
/// @brief The handheld menu on the GUI control plane (`handheld.*`, docs/inprogress/2026-11-12-handheld-menu): open,
/// close and drive it as the pad does, and its `handheld_menu` state. Declared in
/// unrealqtinterface.cpp.

namespace guicontrol
{
class Host;
}

class HandheldMenu;

/// Register the `handheld.*` commands on @p host for @p menu (nullptr: not in handheld mode, every command says so)
void RegisterHandheldControl(guicontrol::Host& host, HandheldMenu* menu);
