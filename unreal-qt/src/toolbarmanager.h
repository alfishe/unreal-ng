#pragma once

#include <QAction>
#include <QObject>
#include <QToolBar>
#include <memory>

class Emulator;
class MainWindow;
class MenuManager;

/// @brief ToolBarManager - the transport toolbar under the menu bar.
///
/// Mirrors the new-gui design: Start / Pause / Restart, Overscan / Full Screen,
/// and Recording. Overscan (Pentagon only, hidden for other models), Full Screen
/// and Recording reuse the MenuManager actions so their state stays in sync with
/// the menus; the transport buttons are toolbar-owned and updated from the active
/// emulator's state.
class ToolBarManager : public QObject
{
    Q_OBJECT

public:
    ToolBarManager(MainWindow* mainWindow, MenuManager* menuManager, QObject* parent = nullptr);

    /// Refresh enabled / checked states from the active emulator (queried directly, no duplication)
    void updateState(std::shared_ptr<Emulator> activeEmulator);

    // User preference (View -> Toolbar). Persisted in QSettings.
    void setVisibleByUser(bool visible);
    bool isVisibleByUser() const { return _visibleByUser; }
    void restoreSettings();
    void saveSettings() const;

    // Full screen hides the toolbar regardless of the preference; restore brings it back
    void hideForFullScreen();
    void restoreVisibility();

signals:
    void startOrResumeRequested();
    void pauseRequested();
    void restartRequested();

private:
    MainWindow* _mainWindow;
    MenuManager* _menuManager;
    QToolBar* _toolBar = nullptr;

    QAction* _startAction = nullptr;
    QAction* _pauseAction = nullptr;
    QAction* _restartAction = nullptr;

    bool _visibleByUser = true;
};
