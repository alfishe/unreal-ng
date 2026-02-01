#include "dockingmanager.h"

#include <QApplication>
#include <QEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QScreen>

#include "mainwindow.h"

DockingManager::DockingManager(MainWindow* mainWindow, QObject* parent) : QObject(parent), _mainWindow(mainWindow) {}

void DockingManager::addDockableWindow(QWidget* window, std::optional<Qt::Edge> initialEdge)
{
    if (!window || _dockableWindows.contains(window))
        return;

    DockingInfo info;
    if (initialEdge.has_value())
    {
        info.snappedEdge = initialEdge;
    }
    _dockableWindows.insert(window, info);
    window->installEventFilter(this);

    if (initialEdge.has_value())
    {
        // Calculate initial offset based on main window's current position
        info.offset = window->pos() - _mainWindow->pos();
        if (*initialEdge == Qt::LeftEdge || *initialEdge == Qt::RightEdge)
        {
            info.offset.setX(0);
        }
        else
        {
            info.offset.setY(0);
        }
        _dockableWindows.insert(window, info);  // Re-insert with updated info

        updateWindowPosition(window, _dockableWindows[window]);
    }
}

void DockingManager::removeDockableWindow(QWidget* window)
{
    if (window)
    {
        window->removeEventFilter(this);
        _dockableWindows.remove(window);
    }
}

bool DockingManager::eventFilter(QObject* watched, QEvent* event)
{
    QWidget* window = qobject_cast<QWidget*>(watched);
    if (window && _dockableWindows.contains(window))
    {
        if (event->type() == QEvent::Move)
        {
            auto& info = _dockableWindows[window];
            if (!info.isBeingSetByManager && !_isSnappingLocked)
            {
                Qt::Edge edge;
                if (isCloseToEdge(window, edge))
                {
                    if (!info.snappedEdge.has_value() || info.snappedEdge.value() != edge)
                    {
                        snapWindow(window, edge);
                    }
                }
                else
                {
                    if (info.snappedEdge.has_value())
                    {
                        unsnapWindow(window);
                    }
                }
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void DockingManager::updateDockedWindows()
{
    if (!_mainWindow)
        return;

    for (auto it = _dockableWindows.begin(); it != _dockableWindows.end(); ++it)
    {
        if (it.value().snappedEdge.has_value())
        {
            updateWindowPosition(it.key(), it.value());
        }
    }
}

void DockingManager::moveDockedWindows(const QPoint& delta)
{
    if (!_mainWindow)
        return;

    for (auto it = _dockableWindows.begin(); it != _dockableWindows.end(); ++it)
    {
        if (it.value().snappedEdge.has_value())
        {
            QWidget* window = it.key();
            DockingInfo& info = it.value();

            QScopedValueRollback<bool> guard(info.isBeingSetByManager, true);
            window->move(window->pos() + delta);
        }
    }
}

void DockingManager::snapWindow(QWidget* window, Qt::Edge edge)
{
    if (!_dockableWindows.contains(window))
        return;
    auto& info = _dockableWindows[window];

    info.snappedEdge = edge;

    // Calculate and store the offset relative to the main window's top-left corner.
    info.offset = window->pos() - _mainWindow->pos();

    // The offset is constrained based on the edge.
    // For left/right edges, we only care about the Y offset.
    // For top/bottom edges, we only care about the X offset.
    if (edge == Qt::LeftEdge || edge == Qt::RightEdge)
    {
        info.offset.setX(0);
    }
    else
    {
        info.offset.setY(0);
    }

    // Immediately update position to snap it cleanly.
    updateWindowPosition(window, info);
}

void DockingManager::unsnapWindow(QWidget* window)
{
    if (_dockableWindows.contains(window))
    {
        _dockableWindows[window].snappedEdge = std::nullopt;
    }
}

void DockingManager::updateWindowPosition(QWidget* window, DockingInfo& info)
{
    if (!info.snappedEdge.has_value())
        return;

    QRect mainRect = _mainWindow->geometry();
    QPoint newPos;

    // Use a guard to prevent feedback loops in the event filter
    QScopedValueRollback<bool> guard(info.isBeingSetByManager, true);

    switch (info.snappedEdge.value())
    {
        case Qt::LeftEdge:
            newPos.setX(mainRect.left() - window->width());
            newPos.setY(mainRect.top() + info.offset.y());
            break;
        case Qt::RightEdge:
            newPos.setX(mainRect.right() + 1);
            newPos.setY(mainRect.top() + info.offset.y());
            break;
        case Qt::TopEdge:
            newPos.setX(mainRect.left() + info.offset.x());
            newPos.setY(mainRect.top() - window->height());
            break;
        case Qt::BottomEdge:
            newPos.setX(mainRect.left() + info.offset.x());
            newPos.setY(mainRect.bottom() + 1);
            break;
    }

    if (window->pos() != newPos)
    {
        window->move(newPos);
    }
}

bool DockingManager::isCloseToEdge(QWidget* window, Qt::Edge& edge) const
{
    // A window can only be snapped if it's on the same screen as the main window.
    if (window->screen() != _mainWindow->screen())
    {
        return false;
    }

    const int snapThreshold = 20;
    const QRect& windowRect = window->geometry();
    const QRect& mainRect = _mainWindow->geometry();

    // Check vertical proximity for left/right edges
    bool yOverlap = (windowRect.top() < mainRect.bottom() && windowRect.bottom() > mainRect.top());

    // Check horizontal proximity for top/bottom edges
    bool xOverlap = (windowRect.left() < mainRect.right() && windowRect.right() > mainRect.left());

    // Check left edge of main window
    if (yOverlap && qAbs(windowRect.right() - mainRect.left()) < snapThreshold)
    {
        edge = Qt::LeftEdge;
        return true;
    }
    // Check right edge of main window
    if (yOverlap && qAbs(windowRect.left() - (mainRect.right() + 1)) < snapThreshold)
    {
        edge = Qt::RightEdge;
        return true;
    }
    // Check top edge of main window
    if (xOverlap && qAbs(windowRect.bottom() - mainRect.top()) < snapThreshold)
    {
        edge = Qt::TopEdge;
        return true;
    }
    // Check bottom edge of main window
    if (xOverlap && qAbs(windowRect.top() - (mainRect.bottom() + 1)) < snapThreshold)
    {
        edge = Qt::BottomEdge;
        return true;
    }

    return false;
}

void DockingManager::onEnterFullscreen()
{
    _preFullscreenState.clear();

    // Get the main window's screen to compare against.
    QScreen* mainScreen = _mainWindow->screen();

    // 1. Process windows on the same screen as the main window.
    // Windows on other screens will be ignored and left as they are.
    for (auto it = _dockableWindows.begin(); it != _dockableWindows.end(); ++it)
    {
        QWidget* window = it.key();

        // If the window is on a different screen, leave it alone.
        if (window->screen() != mainScreen)
        {
            continue;
        }

        // For windows on the same screen, save their state and hide them.
        DockingInfo& info = it.value();
        PreFullscreenState state;
        state.snappedEdge = info.snappedEdge;
        if (window->isVisible())
        {
            state.geometry = window->geometry();
        }
        _preFullscreenState.insert(window, state);

        window->hide();
    }

    // 2. Find a target screen for any snapped windows we just hid.
    auto screens = QApplication::screens();
    if (screens.count() <= 1)
    {
        return;  // No other screen to move to.
    }

    QScreen* targetScreen = nullptr;
    for (QScreen* screen : screens)
    {
        if (screen != mainScreen)
        {
            targetScreen = screen;
            break;
        }
    }

    if (!targetScreen)
    {
        return;  // Should be rare, but possible.
    }

    // 3. Move and show windows that were both visible and snapped.
    QPoint cascadePos = targetScreen->availableGeometry().topLeft();
    for (auto it = _preFullscreenState.begin(); it != _preFullscreenState.end(); ++it)
    {
        QWidget* window = it.key();
        const PreFullscreenState& savedState = it.value();

        // Check if the window was visible AND it was snapped.
        if (savedState.geometry.isValid() && savedState.snappedEdge.has_value())
        {
            window->move(cascadePos);
            cascadePos.ry() += window->frameGeometry().height() + 5;
            window->show();
        }
    }
}

void DockingManager::onExitFullscreen()
{
    for (auto it = _preFullscreenState.begin(); it != _preFullscreenState.end(); ++it)
    {
        QWidget* window = it.key();
        const PreFullscreenState& savedState = it.value();

        if (savedState.geometry.isValid())
        {
            window->setGeometry(savedState.geometry);

            // Restore snap state
            if (_dockableWindows.contains(window))
            {
                _dockableWindows[window].snappedEdge = savedState.snappedEdge;
            }

            // Show window but immediately lower it to preserve z-order
            // (main window should stay on top)
            window->show();
            window->lower();
        }
        else
        {
            window->hide();
        }
    }
    _preFullscreenState.clear();
}

void DockingManager::setSnappingLocked(bool locked)
{
    _isSnappingLocked = locked;
}
