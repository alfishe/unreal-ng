#pragma once

#include <QMainWindow>
#include <QActionGroup>
#include <QLabel>

class QToolBar;
class ScreenWidget;
class StatusIndicator;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onStart();
    void onPause();
    void onRestart();
    void onToggleFullscreen(bool on);
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

    ScreenWidget *m_screen = nullptr;
    QToolBar     *m_toolBar = nullptr;

    int m_currentModeIndex = 2;  // Default to 352x288
    bool m_enforce1to1 = false;  // If true, window resizes to fit 1:1 on mode change

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
};
