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
};

// Install custom fullscreen delegate with NSWindowDelegate for custom animations
void install(QWindow* window, Delegate* delegate);
void uninstall(QWindow* window);

// Set callbacks for Qt UI hide/show during custom fullscreen animation
void setCallbacks(QWindow* window, std::function<void()> hideQtUI, std::function<void()> showQtUI);

// Enter/exit native fullscreen (uses custom animation via NSWindowDelegate)
void enterFullscreen(QWindow* window);
void exitFullscreen(QWindow* window);
bool isFullscreen(QWindow* window);

// Hide/show title bar for cleaner fullscreen transitions
void hideTitleBar(QWindow* window);
void showTitleBar(QWindow* window);

// Get the fullscreen target size for the window's screen
QSize fullscreenSize(QWindow* window);

// Flush all pending Core Animation changes
void flushGraphics();

} // namespace FullscreenHelper
