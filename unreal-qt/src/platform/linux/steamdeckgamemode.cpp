#include "steamdeckgamemode.h"

#include <QEvent>
#include <QMenuBar>
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
