#pragma once

#include <QWindow>
#include <functional>

class QWidget;

namespace FullscreenHelper {

// Callback interface - implement in your window class
class Delegate {
public:
    virtual ~Delegate() = default;
    virtual void willEnterFullscreen() = 0;
    virtual void didEnterFullscreen() = 0;
    virtual void willExitFullscreen() = 0;
    virtual void didExitFullscreen() = 0;
    // The window has already teleported to its destination; animate the layer
    // from/to the given rect (in the coordinates of the window during the zoom).
    virtual void zoomStarted(double duration, int layerW, int layerH,
                             int fromX, int fromY, int fromW, int fromH,
                             bool reverse) = 0;
    // Whichever comes first: our timer or AppKit finishing the transition.
    virtual void zoomFinished() = 0;
};

// Install custom fullscreen delegate with NSWindowDelegate for custom animations
void install(QWindow* window, Delegate* delegate);
void uninstall(QWindow* window);

// Set callbacks for Qt UI hide/show and screen zoom during custom fullscreen animation
// screenZoom receives (targetX, targetY, targetWidth, targetHeight, duration) for smooth animation
void setCallbacks(QWindow* window,
                  std::function<void()> hideQtUI,
                  std::function<void()> showQtUI,
                  std::function<void(int, int, int, int, double)> screenZoomIn = nullptr,
                  std::function<void(int, int, int, int, double)> screenZoomOut = nullptr);

// Enter/exit native fullscreen (uses custom animation via NSWindowDelegate)
void enterFullscreen(QWindow* window);
void exitFullscreen(QWindow* window);
bool isFullscreen(QWindow* window);

// Hide/show title bar for cleaner fullscreen transitions
void hideTitleBar(QWindow* window);
void showTitleBar(QWindow* window);

// Fast non-animated window chrome hide/restore (titlebar buttons + title +
// black background; styleMask untouched). Hide BEFORE toggleFullScreen so the
// system's animation snapshot contains no chrome; restore in didExitFullscreen.
void hideWindowChrome(QWindow* window);
void restoreWindowChrome(QWindow* window);

// Get the fullscreen target size for the window's screen
QSize fullscreenSize(QWindow* window);

// Flush all pending Core Animation changes
void flushGraphics();

// Restore NSWindow firstResponder to the Qt content view. AppKit fullscreen
// transitions can leave firstResponder pointing at a transient chrome view
// (or nil), after which Qt receives no keyboard events at all — Qt-level
// setFocus() cannot fix that. Call after every fullscreen transition.
void ensureKeyboardFocus(QWindow* window);

} // namespace FullscreenHelper
