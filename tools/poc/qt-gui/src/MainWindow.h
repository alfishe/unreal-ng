#pragma once

#include <QMainWindow>
#include <QActionGroup>
#include <QLabel>
#include <QEvent>

#ifdef Q_OS_MACOS
#include "platform/macos/FullscreenHelper.h"
class MetalScreenWidget;
#endif

class QFrame;
class QToolBar;
class ScreenWidget;
class StatusIndicator;
class EmulatorWidget;
class AppSoundManager;
class AudioSettingsDialog;

class MainWindow : public QMainWindow
#ifdef Q_OS_MACOS
    , public FullscreenHelper::Delegate
#endif
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

#ifdef Q_OS_MACOS
    // FullscreenHelper::Delegate
    void willEnterFullscreen() override;
    void didEnterFullscreen() override;
    void willExitFullscreen() override;
    void didExitFullscreen() override;
#endif

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onStart();
    void onPause();
    void onRestart();
    void onToggleFullscreen();
    void onToggleRecord(bool on);
    void onToggle1to1(bool on);
    void onMachineChanged(QAction *a);
    void onVideoModeChanged(QAction *a);
    void onNextVideoMode();
    void onEjectAll();

private:
    void buildActions();
    void buildMenus();
    void buildToolBar();
    void buildStatusBar();
    void restoreLayout();
    void saveLayout();
    void closeEvent(QCloseEvent *e) override;
    void flash(const QString &message);
    void refreshTitle();

    void setVideoMode(int modeIndex);
    void adjustWindowToFitScreen();
    void loadFile(const QString &filePath);

    // Platform-specific fullscreen handlers
#ifdef Q_OS_WIN
    void toggleFullscreenWindows();
    void handleWindowStateChangeWindows(Qt::WindowStates oldState, Qt::WindowStates newState);
#endif
#ifdef Q_OS_MACOS
    void toggleFullscreenMacOS();
    void handleWindowStateChangeMacOS(Qt::WindowStates oldState, Qt::WindowStates newState);
#endif
#ifdef Q_OS_LINUX
    void toggleFullscreenLinux();
    void handleWindowStateChangeLinux(Qt::WindowStates oldState, Qt::WindowStates newState);
#endif

    void applyFullscreenStyle();
    void restoreNormalStyle();

    QFrame *m_contentFrame = nullptr;
#ifdef Q_OS_MACOS
    MetalScreenWidget *m_screen = nullptr;
#else
    ScreenWidget *m_screen = nullptr;
#endif
    QToolBar     *m_toolBar = nullptr;

    int m_currentModeIndex = 2;  // Default to 352x288
    bool m_enforce1to1 = false;  // If true, window resizes to fit 1:1 on mode change

    // Fullscreen state
    enum class FullscreenState { Normal, EnteringFullscreen, Fullscreen, ExitingFullscreen };
    FullscreenState m_fullscreenState = FullscreenState::Normal;
    Qt::WindowStates m_preFullscreenState = Qt::WindowNoState;
    QRect m_normalGeometry;
    QRect m_maximizedGeometry;
    QPalette m_normalPalette;

    // File
    QAction *m_actOpen = nullptr;
    QAction *m_actSaveState = nullptr;
    QAction *m_actLoadState = nullptr;
    QAction *m_actQuit = nullptr;
    // View
    QAction *m_actToolBar = nullptr;
    QAction *m_actStatusBar = nullptr;
    QAction *m_actFullscreen = nullptr;
    QAction *m_actNextVideoMode = nullptr;
    QAction *m_act1to1 = nullptr;
    QActionGroup *m_videoModes = nullptr;
    // Machine
    QActionGroup *m_machines = nullptr;
    QAction *m_actRestart = nullptr;
    // Media
    QAction *m_actTape = nullptr;
    QAction *m_actDisk = nullptr;
    QAction *m_actHdd = nullptr;
    QAction *m_actEject = nullptr;
    // Audio
    QAction *m_actSound = nullptr;
    QAction *m_actAy = nullptr;
    QAction *m_actBeeper = nullptr;
    QAction *m_actAudioSettings = nullptr;
    // Transport
    QAction *m_actStart = nullptr;
    QAction *m_actPause = nullptr;
    QAction *m_actRecord = nullptr;
    // Debug / Help
    QAction *m_actDebugger = nullptr;
    QAction *m_actAbout = nullptr;

    StatusIndicator *m_indTape = nullptr;
    StatusIndicator *m_indDisk = nullptr;
    StatusIndicator *m_indHdd = nullptr;
    StatusIndicator *m_indSound = nullptr;
    QLabel          *m_fps = nullptr;

    QString m_machine = QStringLiteral("Pentagon 128");
    QString m_media   = QStringLiteral("manic.trd");

    // Emulator integration
    EmulatorWidget *m_emulator = nullptr;
    AppSoundManager *m_soundManager = nullptr;

    // Dialogs
    AudioSettingsDialog *m_audioSettingsDialog = nullptr;
};
