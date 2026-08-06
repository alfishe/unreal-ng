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

private slots:
    void onStart();
    void onPause();
    void onRestart();
    void onToggleFullscreen(bool on);
    void onToggleRecord(bool on);
    void onMachineChanged(QAction *a);
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

    ScreenWidget *m_screen = nullptr;
    QToolBar     *m_toolBar = nullptr;

    // File
    QAction *m_actOpen = nullptr;
    QAction *m_actSaveState = nullptr;
    QAction *m_actLoadState = nullptr;
    QAction *m_actQuit = nullptr;
    // View
    QAction *m_actToolBar = nullptr;
    QAction *m_actStatusBar = nullptr;
    QAction *m_actBorder = nullptr;
    QAction *m_actInteger = nullptr;
    QAction *m_actFullscreen = nullptr;
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
