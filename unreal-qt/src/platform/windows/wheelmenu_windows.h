#pragma once

/// Windows: the system wheel (a Surface Dial) held for one top-level window while the emulator reads the Dial itself
/// (docs/inprogress/2026-10-17-controller-manager/surface-dial.md §4.1).
///
/// Windows feeds a Dial to its RadialController as well as to the HID interface the emulator reads (the Raw Input
/// backend opens it shared). Without a hold, a turn in the focused emulator window also runs the active built-in tool
/// (volume, scroll, zoom, undo) and a press and hold opens the wheel menu. A hold, made through the desktop interop
/// interfaces (IRadialControllerInterop / IRadialControllerConfigurationInterop, Windows 10 1607+; the menu
/// suppression 1703+):
///
/// - creates a RadialController for the window with one custom tool, selected, without automatic haptics (the
///   emulator plays the Dial's clicks and detents itself);
/// - clears the built-in tools for the window (SetDefaultMenuItems with none; the custom tool keeps the menu from
///   falling back to the defaults);
/// - suppresses the menu for the window (IsMenuSuppressed) with that controller active (ActiveControllerWhenMenuIsSuppressed),
///   so a press and hold opens nothing and the turns reach no built-in tool.
///
/// The settings are per window: another app in the foreground keeps the user's tools. Release puts them back.
/// The WinRT functions are loaded from combase.dll at run time and the few ABI interfaces are declared here (MinGW
/// has no Windows.UI.Input headers). No-ops returning nullptr on other platforms. Call on the GUI thread.
namespace WheelMenuWindows
{
/// Hold the wheel for top-level window @p hwnd (an HWND)
/// @return an opaque hold, or nullptr when the system cannot (no wheel support, an older Windows, not Windows)
void* Take(void* hwnd);

/// Give the wheel back to the system for the hold's window (nullptr is ignored): the menu, the user's built-in tools
void Release(void* hold);
}  // namespace WheelMenuWindows
