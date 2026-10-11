#pragma once

/// macOS-only application tweaks (implemented in macosplatform.mm; no-ops elsewhere)
namespace MacOSPlatform
{
/// Stop Cocoa from injecting its own "Enter Full Screen" item into the View menu.
/// The app has a single View -> Full Screen action with its own shortcut; Cocoa's
/// duplicate appeared disabled because the window does not opt into Cocoa full screen.
/// Must be called before the menu bar is created.
void disableAutomaticFullScreenMenuItem();

/// Makes the process a background one (no Dock icon, no menu bar, never "Not Responding"). For instances without
/// windows (-platform offscreen / minimal, the automation servers agents launch): macOS registers the bundle as a
/// regular app, and an app that never services the Cocoa event queue shows as hung in the Dock and Force Quit.
/// Call after the QApplication exists.
void runAsBackgroundProcess();
}
