#pragma once

#include <QWindow>

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

void install(QWindow* window, Delegate* delegate);
void uninstall(QWindow* window);

void enterFullscreen(QWindow* window);
void exitFullscreen(QWindow* window);
bool isFullscreen(QWindow* window);

// Hide/show title bar for cleaner fullscreen transitions
void hideTitleBar(QWindow* window);
void showTitleBar(QWindow* window);

} // namespace FullscreenHelper
