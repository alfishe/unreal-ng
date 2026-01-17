#pragma once

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QMutex>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QTimer>

#include "3rdparty/message-center/messagecenter.h"
#include "common/modulelogger.h"
#include "debugger/debuggerwindow.h"
#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatormanager.h"
#include "emulator/guiemulatorcontext.h"
#include "emulator/soundmanager.h"
#include "logviewer/logwindow.h"
#include "menumanager.h"
#include "ui_mainwindow.h"
#include "widgets/devicescreen.h"

#ifdef ENABLE_AUTOMATION
// Avoid name conflicts between Python and Qt "slot"
#undef slots
#include "automation/automation.h"
#define slots Q_SLOTS
#endif  // ENABLE_AUTOMATION

class DockingManager;

QT_BEGIN_NAMESPACE
namespace Ui
{
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow, public Observer
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    virtual ~MainWindow() override;

    // Disable object copy
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    // region <Slots>
private slots:
    void handleStartButton();
    void tryAdoptRemainingEmulator();
    void handleMessageScreenRefresh(int id, Message* message);
    void handleFileOpenRequest(int id, Message* message);
    void handleEmulatorStateChanged(int id, Message* message);
    void handleEmulatorInstanceDestroyed(int id, Message* message);
    void handleEmulatorInstanceCreated(int id, Message* message);
    void handleEmulatorSelectionChanged(int id, Message* message);
    void openFileDialog();
    void openSpecificFile(const QString& filepath);
    void resetEmulator();
    void handleFullScreenShortcut();

    // Menu action handlers
    void handleStartEmulator();
    void handlePauseEmulator();
    void handleResumeEmulator();
    void handleStopEmulator();
    void handleSpeedMultiplierChanged(int multiplier);
    void handleTurboModeToggled(bool enabled);
    void handleStepIn();
    void handleStepOver();
    void handleDebugModeToggled(bool enabled);
    void handleDebuggerToggled(bool visible);
    void handleLogWindowToggled(bool visible);
    void updateMenuStates();

    // Binding state handler
    void onBindingStateChanged(EmulatorStateEnum state);
    // endregion <Slots>

    // region <QWidget events override>
protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void changeEvent(QEvent* event) override;

    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    // endregion </QWidget events override>

protected:
    void updatePosition(QWidget* widget, QWidget* parent, float xscale, float yscale)
    {
        int w = parent->size().width();
        int h = parent->size().height();
        widget->move(
            QPoint(static_cast<int>(static_cast<float>(w) * xscale), static_cast<int>(static_cast<float>(h) * yscale)) -
            widget->rect().center());
    }

    void arrangeWindows();
    void adjust(QEvent* event, const QPoint& delta = QPoint{});

private:
    // Save the last directory path to settings
    void saveLastDirectory(const QString& path);

    // Clean up automation resources
    void cleanupAutomation();

    // Unsubscribe from all message bus events
    void unsubscribeFromMessageBus();

    // Subscribe/unsubscribe from per-emulator-instance events
    void subscribeToPerEmulatorEvents();
    void unsubscribeFromPerEmulatorEvents();

    // Bind audio callback to emulator (audio device runs continuously)
    void bindEmulatorAudio(std::shared_ptr<Emulator> emulator);

    /// @brief Adopt an emulator as the active emulator for this window.
    /// This is the SINGLE point of emulator binding. All emulator adoption
    /// (UI-triggered, automation-triggered, or selection-changed) must go through here.
    /// Handles: binding, audio, screen, debugger, menu, and UI state.
    /// @param emulator The emulator to adopt
    void adoptEmulator(std::shared_ptr<Emulator> emulator);

    /// @brief Unbind from the currently adopted emulator without destroying it.
    /// Used when switching to a different emulator - old emulator keeps running headless.
    void unbindFromEmulator();

    /// @brief Release and destroy the currently adopted emulator.
    /// This is the SINGLE point of emulator destruction. Use for stop, destroy, or close.
    /// Handles: unbinding, audio cleanup, screen detach, debugger reset, UI state, and emulator destruction.
    void releaseEmulator();

    // Platform-specific initialization methods
    void initializePlatformMacOS();
    void initializePlatformWindows();
    void initializePlatformLinux();

    // Platform-specific window state handling methods
    void handleWindowStateChangeMacOS(Qt::WindowStates oldState, Qt::WindowStates newState);
    void handleWindowStateChangeWindows(Qt::WindowStates oldState, Qt::WindowStates newState);
    void handleWindowStateChangeLinux(Qt::WindowStates oldState, Qt::WindowStates newState);

    void handleFullScreenShortcutMacOS();
    void handleFullScreenShortcutWindows();
    void handleFullScreenShortcutLinux();

private:
    Ui::MainWindow* ui = nullptr;
    DebuggerWindow* debuggerWindow = nullptr;
    LogWindow* logWindow = nullptr;
    DeviceScreen* deviceScreen = nullptr;
    QPushButton* startButton = nullptr;
    QMutex lockMutex;
    QMutex _audioMutex;              // Protects audio operations from race conditions
    bool _audioInitialized = false;  // Tracks if audio device is initialized

#ifdef ENABLE_AUTOMATION
    std::unique_ptr<Automation> _automation{nullptr};
#endif  // ENABLE_AUTOMATION

    EmulatorManager* _emulatorManager = nullptr;
    EmulatorBinding* m_binding = nullptr;  // Central state binding for UI
    AppSoundManager* _soundManager = nullptr;
    GUIEmulatorContext* _guiContext = nullptr;
    std::shared_ptr<Emulator> _emulator = nullptr;  // TODO: Remove after full binding migration
    uint32_t _lastFrameCount = 0;

    QPoint _lastCursorPos;
    QPalette _originalPalette;

    QShortcut* _fullScreenShortcut = nullptr;
    bool _inHandler = false;

    // Stores window geometry before going fullscreen / maximized
    QRect _normalGeometry;
    QRect _maximizedGeometry;
    Qt::WindowStates _preFullScreenState = Qt::WindowNoState;
    bool _isFullScreen = false;
    bool _inTransitionToFullScreen = false;

    // Last directory used for file operations
    QString _lastDirectory;

    DockingManager* _dockingManager = nullptr;
    MenuManager* _menuManager = nullptr;
};
