#pragma once

/// @file steamdeckgamemode.h
/// @brief unreal-qt as a non-Steam game in Steam Deck Game Mode (the gamescope session). gamescope shows the window on
/// the whole panel and has no desktop around it, so the window goes full screen without its chrome (menu, toolbars,
/// status bar) and stays so, and a Pentagon 128K starts at once (a window with no machine is a black screen there).
/// gamescope never confirms _NET_WM_STATE_FULLSCREEN, so MainWindow's own fullscreen styling (on the window manager's
/// state change) is not relied on: an event filter keeps the chrome hidden. Desktop Mode: nothing. Built on Linux only (unreal-qt/CMakeLists.txt).

class MainWindow;

namespace steamdeck
{

/// The gamescope session (XDG_CURRENT_DESKTOP=gamescope, set by SteamOS for Game Mode)
bool IsGameMode();

/// In Game Mode: full screen without chrome for the window's life; `startMachine`: a Pentagon 128K when no machine
/// runs (false when the command line brings its own file or script). Elsewhere nothing
void ApplyGameMode(MainWindow& window, bool startMachine);

}  // namespace steamdeck
