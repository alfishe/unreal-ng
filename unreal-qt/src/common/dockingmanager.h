#pragma once

#include <QObject>
#include <QMainWindow>
#include <QWidget>
#include <QList>
#include <QPoint>
#include <optional>
#include <QMap>

class MainWindow;

class DockingManager : public QObject
{
    Q_OBJECT

public:
    explicit DockingManager(MainWindow* mainWindow, QObject* parent = nullptr);
    virtual ~DockingManager() = default;

    void addDockableWindow(QWidget* window, std::optional<Qt::Edge> initialEdge = std::nullopt,
                            bool useNativeChildWindow = false);
    void removeDockableWindow(QWidget* window);
    /// Snap `window` to `edge` now, `offset` pixels along it from the main window's
    /// top (left / right edge) or left (top / bottom edge); attaches it natively
    /// when it was added with useNativeChildWindow
    void dockAt(QWidget* window, Qt::Edge edge, int offset);
    /// The window follows the main window (snapped to an edge)
    bool isDocked(QWidget* window) const;
    void updateDockedWindows();
    void moveDockedWindows(const QPoint& delta);
    void onEnterFullscreen();
    void onExitFullscreen();
    void setSnappingLocked(bool locked);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct DockingInfo
    {
        std::optional<Qt::Edge> snappedEdge = std::nullopt;
        QPoint offset;
        bool isBeingSetByManager = false;
        /// Glue snapped windows to the main window at the OS level where possible
        /// (platform/childwindow.h) instead of chasing the main window's move
        /// events, which lags behind on macOS while dragging.
        bool useNativeChildWindow = false;
        bool nativeAttached = false;
    };

    struct PreFullscreenState
    {
        QRect geometry;
        std::optional<Qt::Edge> snappedEdge;
    };

    void handleWindowMove(QWidget* window);
    void handleMouseRelease(QWidget* window);
    void snapWindow(QWidget* window, Qt::Edge edge);
    void unsnapWindow(QWidget* window);
    void updateWindowPosition(QWidget* window, DockingInfo& info);
    bool isCloseToEdge(QWidget* window, Qt::Edge& edge) const;
    void attachNative(QWidget* window, DockingInfo& info);
    void detachNative(QWidget* window, DockingInfo& info);

    MainWindow* _mainWindow;
    QMap<QWidget*, DockingInfo> _dockableWindows;
    QMap<QWidget*, PreFullscreenState> _preFullscreenState;
    QMap<QWidget*, QRect> _preFullscreenGeometries;
    int _snapDistance = 20;
    bool _isSnappingLocked = false;
}; 