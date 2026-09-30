#pragma once

/// Glue a small window (a popover) to its main window at the OS level, so it
/// moves, minimizes and stacks with that window by itself instead of chasing
/// its move events.
///
/// macOS: NSWindow child windows (platform/macos/childwindow_macos.mm) - the
/// window server moves the child with its parent while it is dragged, no lag.
/// Other platforms: not available (Attach returns false); the caller keeps
/// following the main window's move events. An owned tool window already
/// stays above its owner on Windows and on X11 / Wayland.
///
/// Both widgets must be top-level windows with native handles; call on the
/// GUI thread.
class QWidget;

namespace ChildWindow
{
/// Attach `child` above `parent`; false when the platform cannot
bool Attach(QWidget* parent, QWidget* child);

/// Undo Attach (nothing when not attached)
void Detach(QWidget* parent, QWidget* child);
}  // namespace ChildWindow
