#include "steamdeckgamemode.h"

#include <QEvent>
#include <QMenuBar>
#include <QScreen>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include "mainwindow.h"

namespace steamdeck
{
namespace
{

/// Keeps the window full screen and its chrome hidden: the fullscreen shortcut and MainWindow's state handling would
/// bring the menu and toolbars back
class GameModeFilter : public QObject
{
public:
    explicit GameModeFilter(MainWindow& window) : QObject(&window), _window(window) {}

    void Apply()
    {
        if (!_window.isFullScreen())
            _window.showFullScreen();
        _window.menuBar()->hide();
        _window.statusBar()->hide();
        for (QToolBar* bar : _window.findChildren<QToolBar*>())
            bar->hide();
        // gamescope does not act on the full screen state of an XWayland window (no _NET_WM_STATE appears): the window
        // keeps the size it was created with and gamescope fits THAT into the screen by its shape, bars around it.
        // The window takes the screen's size itself, so the picture is fitted into the whole screen
        if (const QScreen* screen = _window.screen())
        {
            const QRect area = screen->geometry();
            if (_window.geometry() != area)
                _window.setGeometry(area);
        }
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == &_window && (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Show))
            QTimer::singleShot(0, this, [this]() { Apply(); });  // after MainWindow's own handling of the change
        return QObject::eventFilter(watched, event);
    }

private:
    MainWindow& _window;
};

}  // namespace

bool IsGameMode()
{
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP") == QLatin1String("gamescope");
}

void ApplyGameMode(MainWindow& window, bool startMachine)
{
    if (!IsGameMode())
        return;
    auto* filter = new GameModeFilter(window);
    window.installEventFilter(filter);
    QTimer::singleShot(0, filter, [filter]() { filter->Apply(); });
    if (startMachine)
        QTimer::singleShot(0, &window, [&window]() { window.startEmulatorIfNone("Pentagon", 128); });
}

}  // namespace steamdeck
