#pragma once

#include <QMainWindow>
#include <QTimer>
#include <QMutex>
#include <QEvent>
#include <QResizeEvent>
#include <QMoveEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSettings>
#include <QPushButton>

#include "3rdparty/message-center/messagecenter.h"
#include "common/modulelogger.h"
#include "debugger/debuggerwindow.h"
#include "emulator/emulator.h"
#include "emulator/guiemulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "logviewer/logwindow.h"
#include "widgets/devicescreen.h"
#include "emulator/soundmanager.h"

#ifdef ENABLE_AUTOMATION
    // Avoid name conflicts between Python and Qt "slot"
    #undef slots
    #include "automation/automation.h"
    #define slots Q_SLOTS
#endif // ENABLE_AUTOMATION


QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow, public Observer
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    virtual ~MainWindow() override;

    // Disable object copy
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

private slots:
    void handleStartButton();
    void handleMessageScreenRefresh(int id, Message* message);
    void handleFileOpenRequest(int id, Message* message);
    void openFileDialog();
    void openSpecificFile(const QString& filepath);
    void resetEmulator();
    void handleFullScreenShortcut();

    /// region <QWidget events override>
protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void changeEvent(QEvent* event) override;

    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
/// endregion </QWidget events override>

protected:
    void updatePosition(QWidget *widget, QWidget *parent, float xscale, float yscale)
    {
        int w = parent->size().width();
        int h = parent->size().height();
        widget->move(QPoint(static_cast<int>(static_cast<float>(w) * xscale), static_cast<int>(static_cast<float>(h) * yscale)) - widget->rect().center());
    }

    void arrangeWindows();
    void adjust(QEvent* event, const QPoint& delta = QPoint{});

private:
    // Save the last directory path to settings
    void saveLastDirectory(const QString& path);
    
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

    Ui::MainWindow* ui = nullptr;
    DebuggerWindow* debuggerWindow = nullptr;
    LogWindow* logWindow = nullptr;
    DeviceScreen* deviceScreen = nullptr;
    QPushButton* startButton = nullptr;
    QMutex lockMutex;

#ifdef ENABLE_AUTOMATION
    std::unique_ptr<Automation> _automation{nullptr};
#endif // ENABLE_AUTOMATION

    EmulatorManager* _emulatorManager = nullptr;
    AppSoundManager* _soundManager = nullptr;
    GUIEmulatorContext* _guiContext = nullptr;
    std::shared_ptr<Emulator> _emulator = nullptr;
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
};

