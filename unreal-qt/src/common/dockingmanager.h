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

    void addDockableWindow(QWidget* window, std::optional<Qt::Edge> initialEdge = std::nullopt);
    void removeDockableWindow(QWidget* window);
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

    MainWindow* _mainWindow;
    QMap<QWidget*, DockingInfo> _dockableWindows;
    QMap<QWidget*, PreFullscreenState> _preFullscreenState;
    QMap<QWidget*, QRect> _preFullscreenGeometries;
    int _snapDistance = 20;
    bool _isSnappingLocked = false;
}; 