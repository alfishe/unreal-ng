#pragma once

/// macOS-only application tweaks (implemented in macosplatform.mm; no-ops elsewhere)
namespace MacOSPlatform
{
/// Stop Cocoa from injecting its own "Enter Full Screen" item into the View menu.
/// The app has a single View -> Full Screen action with its own shortcut; Cocoa's
/// duplicate appeared disabled because the window does not opt into Cocoa full screen.
/// Must be called before the menu bar is created.
void disableAutomaticFullScreenMenuItem();
}
