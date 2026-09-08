#include "toolbarmanager.h"

#include <QMainWindow>
#include <QSettings>

#include "emulator/emulator.h"
#include "mainwindow.h"
#include "menumanager.h"
#include "widgets/tintedsvgicon.h"

namespace
{
constexpr const char* kSettingsKey = "View/ToolBarVisible";
}

ToolBarManager::ToolBarManager(MainWindow* mainWindow, MenuManager* menuManager, QObject* parent)
    : QObject(parent), _mainWindow(mainWindow), _menuManager(menuManager)
{
    _toolBar = _mainWindow->addToolBar(tr("Transport"));
    _toolBar->setObjectName(QStringLiteral("transportToolBar"));
    _toolBar->setMovable(false);
    _toolBar->setFloatable(false);
    _toolBar->setIconSize(QSize(16, 16));
    _toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    // ---- Transport -------------------------------------------------------
    _startAction = new QAction(tintedSvgIcon(QStringLiteral("start")), tr("Start"), this);
    _startAction->setCheckable(true);
    _startAction->setToolTip(tr("Start / resume emulator"));
    connect(_startAction, &QAction::triggered, this, &ToolBarManager::startOrResumeRequested);

    _pauseAction = new QAction(tintedSvgIcon(QStringLiteral("pause")), tr("Pause"), this);
    _pauseAction->setCheckable(true);
    _pauseAction->setToolTip(tr("Pause emulator"));
    connect(_pauseAction, &QAction::triggered, this, &ToolBarManager::pauseRequested);

    _restartAction = new QAction(tintedSvgIcon(QStringLiteral("restart")), tr("Restart"), this);
    _restartAction->setToolTip(tr("Reset machine (starts the emulator if it is not running)"));
    connect(_restartAction, &QAction::triggered, this, &ToolBarManager::restartRequested);

    // ---- View ------------------------------------------------------------
    // Pentagon overscan on/off (384x304 vs. the standard symmetric 352x288). The menu
    // action is hidden by updateMenuStates() for non-Pentagon models, which hides
    // this button as well.
    QAction* overscan = _menuManager->overscanAction();
    overscan->setIcon(tintedSvgIcon(QStringLiteral("videomode")));
    overscan->setIconVisibleInMenu(false);
    overscan->setToolTip(tr("Pentagon overscan mode (384x304)"));

    QAction* fullScreen = _menuManager->fullScreenAction();
    fullScreen->setIcon(tintedSvgIcon(QStringLiteral("fullscreen")));
    fullScreen->setIconVisibleInMenu(false);

    _toolBar->addAction(_startAction);
    _toolBar->addAction(_pauseAction);
    _toolBar->addAction(_restartAction);
    _toolBar->addSeparator();
    _toolBar->addAction(overscan);
    _toolBar->addAction(fullScreen);

#ifdef ENABLE_RECORDING
    QAction* record = _menuManager->videoRecordingAction();
    record->setIcon(tintedSvgIcon(QStringLiteral("record"), /*tint=*/false));
    record->setIconVisibleInMenu(false);
    _toolBar->addSeparator();
    _toolBar->addAction(record);
#endif

    // View -> Toolbar
    connect(_menuManager, &MenuManager::toolBarToggled, this, &ToolBarManager::setVisibleByUser);

    updateState(nullptr);
}

void ToolBarManager::updateState(std::shared_ptr<Emulator> activeEmulator)
{
    const bool exists = (activeEmulator != nullptr);
    const bool running = exists && activeEmulator->IsRunning();
    const bool paused = exists && activeEmulator->IsPaused();
    const bool active = running && !paused;

    _startAction->setChecked(active);
    _startAction->setEnabled(!active);
    _pauseAction->setChecked(paused);
    _pauseAction->setEnabled(active);
    _restartAction->setEnabled(true);
}

void ToolBarManager::setVisibleByUser(bool visible)
{
    _visibleByUser = visible;
    _toolBar->setVisible(visible);
    _menuManager->setToolBarChecked(visible);
}

void ToolBarManager::restoreSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
    setVisibleByUser(settings.value(QLatin1String(kSettingsKey), true).toBool());
}

void ToolBarManager::saveSettings() const
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
    settings.setValue(QLatin1String(kSettingsKey), _visibleByUser);
}

void ToolBarManager::hideForFullScreen()
{
    _toolBar->hide();
}

void ToolBarManager::restoreVisibility()
{
    _toolBar->setVisible(_visibleByUser);
}
