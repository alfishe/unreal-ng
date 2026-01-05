#include "mainwindow.h"

#include <stdio.h>
#include <webapi/src/automation-webapi.h>

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QMimeData>
#include <QScopedValueRollback>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>

#include "3rdparty/message-center/eventqueue.h"
#include "common/dockingmanager.h"
#include "common/modulelogger.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/filemanager.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/soundmanager.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    // Load the last used directory from settings
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
    _lastDirectory = settings.value("LastFileDirectory", QCoreApplication::applicationDirPath()).toString();
    qDebug() << "Loading last directory from settings:" << _lastDirectory;

#ifdef ENABLE_AUTOMATION
    _automation = std::make_unique<Automation>();
#endif  // ENABLE_AUTOMATION

    // Intercept all keyboard and mouse events
    // qApp->installEventFilter(this);

    // Instantiate all child widgets (UI form auto-generated)
    ui->setupUi(this);
    startButton = ui->startEmulator;

    // Store original palette
    _originalPalette = palette();

    // Register fullscreen on/off shortcut
    _fullScreenShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), this);
    _fullScreenShortcut->setKey(Qt::CTRL | Qt::Key_F);
    _fullScreenShortcut->setContext(Qt::ApplicationShortcut);
    connect(_fullScreenShortcut, &QShortcut::activated, this, &MainWindow::handleFullScreenShortcut);

    // Put emulator screen into resizable content frame
    QFrame* contentFrame = ui->contentFrame;
    deviceScreen = new DeviceScreen(contentFrame);

    QHBoxLayout* layout = new QHBoxLayout;
    layout->addWidget(deviceScreen, Qt::AlignHCenter);
    contentFrame->setLayout(layout);

    /*
        QSizePolicy dp;
        dp.setHorizontalPolicy(QSizePolicy::Expanding);
        dp.setVerticalPolicy(QSizePolicy::Expanding);
        deviceScreen->setSizePolicy(dp);
    */

    // Connect button release signal to appropriate event handling slot
    connect(startButton, SIGNAL(released()), this, SLOT(handleStartButton()));

    // Create bridge between GUI and emulator
    _emulatorManager = EmulatorManager::GetInstance();

    // Init audio subsystem
    _soundManager = new AppSoundManager();
    _soundManager->init();

    // Instantiate Logger window
    logWindow = new LogWindow();

    // Instantiate debugger window
    debuggerWindow = new DebuggerWindow();
    debuggerWindow->reset();
    debuggerWindow->show();

    _dockingManager = new DockingManager(this);
    _dockingManager->addDockableWindow(debuggerWindow, Qt::LeftEdge);
    _dockingManager->addDockableWindow(logWindow, Qt::RightEdge);

    // Create and configure menu system
    _menuManager = new MenuManager(this, ui->menubar, this);
    
    // Connect menu signals to handlers
    connect(_menuManager, &MenuManager::openFileRequested, this, &MainWindow::openFileDialog);
    connect(_menuManager, &MenuManager::openSnapshotRequested, this, &MainWindow::openFileDialog);
    connect(_menuManager, &MenuManager::openTapeRequested, this, &MainWindow::openFileDialog);
    connect(_menuManager, &MenuManager::openDiskRequested, this, &MainWindow::openFileDialog);
    connect(_menuManager, &MenuManager::startRequested, this, &MainWindow::handleStartEmulator);
    connect(_menuManager, &MenuManager::pauseRequested, this, &MainWindow::handlePauseEmulator);
    connect(_menuManager, &MenuManager::resumeRequested, this, &MainWindow::handleResumeEmulator);
    connect(_menuManager, &MenuManager::stopRequested, this, &MainWindow::handleStopEmulator);
    connect(_menuManager, &MenuManager::resetRequested, this, &MainWindow::resetEmulator);
    connect(_menuManager, &MenuManager::speedMultiplierChanged, this, &MainWindow::handleSpeedMultiplierChanged);
    connect(_menuManager, &MenuManager::turboModeToggled, this, &MainWindow::handleTurboModeToggled);
    connect(_menuManager, &MenuManager::stepInRequested, this, &MainWindow::handleStepIn);
    connect(_menuManager, &MenuManager::stepOverRequested, this, &MainWindow::handleStepOver);
    connect(_menuManager, &MenuManager::debugModeToggled, this, &MainWindow::handleDebugModeToggled);
    connect(_menuManager, &MenuManager::debuggerToggled, this, &MainWindow::handleDebuggerToggled);
    connect(_menuManager, &MenuManager::logWindowToggled, this, &MainWindow::handleLogWindowToggled);
    connect(_menuManager, &MenuManager::fullScreenToggled, this, &MainWindow::handleFullScreenShortcut);

    // Bring application windows to foreground
    debuggerWindow->raise();
    this->raise();

    // Enable Drag'n'Drop
    setAcceptDrops(true);

    // Enable event filter to passthrough keyboard events to DeviceScreen
    contentFrame->installEventFilter(this);
    this->installEventFilter(this);

    // Store original window geometry before going fullscreen / maximized
    _normalGeometry = normalGeometry();

    // Initialize platform-specific settings
#ifdef Q_OS_MAC
    initializePlatformMacOS();
#elif defined(Q_OS_WIN)
    initializePlatformWindows();
#else
    initializePlatformLinux();
#endif

    // Subscribe to global emulator instance lifecycle events (so UI can detect CLI/WebAPI-started instances)
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);

    ObserverCallbackMethod createCallback =
        static_cast<ObserverCallbackMethod>(&MainWindow::handleEmulatorInstanceCreated);
    messageCenter.AddObserver(NC_EMULATOR_INSTANCE_CREATED, observerInstance, createCallback);

    ObserverCallbackMethod destroyCallback =
        static_cast<ObserverCallbackMethod>(&MainWindow::handleEmulatorInstanceDestroyed);
    messageCenter.AddObserver(NC_EMULATOR_INSTANCE_DESTROYED, observerInstance, destroyCallback);

    qDebug() << "MainWindow: Subscribed to global instance lifecycle events";

    // Check if there's already a running emulator instance (started before UI)
    QTimer::singleShot(100, this, [this]() {
        auto emulatorIds = _emulatorManager->GetEmulatorIds();
        if (!emulatorIds.empty() && !_emulator)
        {
            // Adopt the first running emulator
            auto emulator = _emulatorManager->GetEmulator(emulatorIds[0]);
            if (emulator)
            {
                qDebug() << "MainWindow: Found existing emulator instance, binding to it...";
                // Trigger the bind via the normal handler
                SimpleTextPayload payload(emulatorIds[0]);
                Message msg(0, &payload, false);  // topic id 0, don't cleanup payload (it's on stack)
                handleEmulatorInstanceCreated(0, &msg);
            }
        }
    });

#ifdef ENABLE_AUTOMATION

    // Delay automation modules start, so the main thread is not blocked, and automation is fully initialized
    // Otherwise race conditions
    // TODO: implement proper waiting until automation is fully initialized
    QTimer::singleShot(300, this, [this]() {
        if (_automation)
        {
            _automation->start();
        }
    });

#endif

    /// region <Debug>

    /// endregion </Debug>
}

void MainWindow::cleanupAutomation()
{
#ifdef ENABLE_AUTOMATION
    if (_automation)
    {
        _automation->stop();
        _automation.reset();
        qDebug() << "Automation cleanup complete";
    }
#endif
}

MainWindow::~MainWindow()
{
    setAcceptDrops(false);

    // Clean up automation resources first
    cleanupAutomation();

    // Deinit audio subsystem
    if (_soundManager)
    {
        _soundManager->deinit();
        delete _soundManager;
        _soundManager = nullptr;
    }

    if (debuggerWindow != nullptr)
    {
        _dockingManager->removeDockableWindow(debuggerWindow);
        debuggerWindow->hide();
        delete debuggerWindow;
    }

    if (logWindow != nullptr)
    {
        _dockingManager->removeDockableWindow(logWindow);
        logWindow->hide();
        delete logWindow;
    }

    if (deviceScreen != nullptr)
        delete deviceScreen;

    if (_guiContext)
        delete _guiContext;

    if (_dockingManager)
        delete _dockingManager;

    delete ui;
}

/// region <Protected members>

/// region <QWidget events override>

/// QWidget event called after windows is shown
void MainWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // Center device screen within content frame
    updatePosition(deviceScreen, ui->contentFrame, 0.5, 0.5);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Clean up automation resources
    cleanupAutomation();

    event->accept();
    qDebug() << "QCloseEvent : Closing application";

    // Unsubscribe from message bus events
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);
    ObserverCallbackMethod callback = static_cast<ObserverCallbackMethod>(&MainWindow::handleMessageScreenRefresh);
    messageCenter.RemoveObserver(NC_VIDEO_FRAME_REFRESH, observerInstance, callback);

    ObserverCallbackMethod stateCallback =
        static_cast<ObserverCallbackMethod>(&MainWindow::handleEmulatorStateChanged);
    messageCenter.RemoveObserver(NC_EMULATOR_STATE_CHANGE, observerInstance, stateCallback);

    ObserverCallbackMethod destroyCallback =
        static_cast<ObserverCallbackMethod>(&MainWindow::handleEmulatorInstanceDestroyed);
    messageCenter.RemoveObserver(NC_EMULATOR_INSTANCE_DESTROYED, observerInstance, destroyCallback);

    ObserverCallbackMethod createCallback =
        static_cast<ObserverCallbackMethod>(&MainWindow::handleEmulatorInstanceCreated);
    messageCenter.RemoveObserver(NC_EMULATOR_INSTANCE_CREATED, observerInstance, createCallback);

    // Close debugger
    if (debuggerWindow)
    {
        _dockingManager->removeDockableWindow(debuggerWindow);
        debuggerWindow->hide();
        delete debuggerWindow;
        debuggerWindow = nullptr;
    }

    // Close LogViewer
    if (logWindow)
    {
        _dockingManager->removeDockableWindow(logWindow);
        logWindow->hide();
        delete logWindow;
        logWindow = nullptr;
    }

    // Shutdown emulator
    if (deviceScreen)
    {
        deviceScreen->detach();
    }

    // Remove emulator from manager before destroying it
    if (_emulator)
    {
        std::string emulatorId = _emulator->GetId();
        _emulatorManager->RemoveEmulator(emulatorId);
        _emulator = nullptr;
    }

    qDebug() << "QCloseEvent : Emulator shutdown complete";
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    // deviceScreen->move(this->rect().center() - deviceScreen->rect().center());

    // Keep widget center-aligned. Alignment policy is not working good
    updatePosition(deviceScreen, ui->contentFrame, 0.5, 0.5);

    // Update normal geometry
    _normalGeometry = normalGeometry();

    // Notify docked child windows
    _dockingManager->updateDockedWindows();

    QWidget::resizeEvent(event);
}

void MainWindow::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);

    // Notify docked child windows
    if (_dockingManager)
        _dockingManager->updateDockedWindows();

    // Update normal geometry
    _normalGeometry = normalGeometry();
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange)
    {
        QWindowStateChangeEvent* stateEvent = static_cast<QWindowStateChangeEvent*>(event);
        Qt::WindowStates oldState = stateEvent->oldState();
        Qt::WindowStates newState = windowState();

        // Debug output for state transitions
        auto stateToString = [](Qt::WindowStates state) -> QString {
            QStringList states;
            if (state == Qt::WindowNoState)
                states << "NoState";
            if (state & Qt::WindowMinimized)
                states << "Minimized";
            if (state & Qt::WindowMaximized)
                states << "Maximized";
            if (state & Qt::WindowFullScreen)
                states << "FullScreen";
            if (state & Qt::WindowActive)
                states << "Active";
            return states.join(" | ");
        };

        qDebug().noquote() << "Window state changed from:" << stateToString(oldState)
                           << "to:" << stateToString(newState);

#ifdef Q_OS_MAC
        handleWindowStateChangeMacOS(oldState, newState);
#elif defined(Q_OS_WIN)
        handleWindowStateChangeWindows(oldState, newState);
#else
        handleWindowStateChangeLinux(oldState, newState);
#endif

        update();
        event->accept();
    }

    QWidget::changeEvent(event);
}

void MainWindow::handleWindowStateChangeMacOS(Qt::WindowStates oldState, Qt::WindowStates newState)
{
    // Prevent recursive calls
    QScopedValueRollback<bool> guard(_inHandler, true);

#ifdef _DEBUG
    auto stateToString = [](Qt::WindowStates state) -> QString {
        QStringList states;
        if (state == Qt::WindowNoState)
            states << "NoState";
        if (state & Qt::WindowMinimized)
            states << "Minimized";
        if (state & Qt::WindowMaximized)
            states << "Maximized";
        if (state & Qt::WindowFullScreen)
            states << "FullScreen";
        if (state & Qt::WindowActive)
            states << "Active";
        return states.isEmpty() ? "Unknown" : states.join("|");
    };

    // qDebug() << "Window state change - Old:" << stateToString(oldState) << "New:" << stateToString(newState);
#endif

    // Handle maximize state (triggered by green button or double-click)
    if (newState & Qt::WindowMaximized && !_isFullScreen)
    {
        qDebug() << "Maximizing window (macOS)";

        _isFullScreen = false;

        // Ensure we're not in fullscreen mode
        if (windowFlags() & Qt::FramelessWindowHint)
        {
            qDebug() << "Clearing frameless window hint";
            setWindowFlags(windowFlags() & ~Qt::FramelessWindowHint);
        }

        // Restore normal palette if needed
        if (palette() != _originalPalette)
        {
            setPalette(_originalPalette);
        }

        // Show maximized
        showMaximized();
    }
    // Handle fullscreen state (only from menu/shortcut, not green button)
    else if (newState & Qt::WindowFullScreen)
    {
        qDebug() << "Entering fullscreen (macOS)";

        hide();
        _isFullScreen = true;

        // Store previous geometry if we're not already in fullscreen
        if (!(oldState & Qt::WindowFullScreen))
        {
            _normalGeometry = (oldState & Qt::WindowMaximized) ? _normalGeometry : geometry();
            qDebug() << "Stored normal geometry for fullscreen:" << _normalGeometry;
        }

        // Apply fullscreen palette: black background around emulated system screen
        QPalette palette;
        palette.setColor(QPalette::Window, Qt::black);
        setPalette(palette);

        // Hide all control elements
        statusBar()->hide();
        startButton->hide();

        // Set frameless window hint for fullscreen
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        showFullScreen();
    }
    // Handle restore to normal state
    else if (newState == Qt::WindowNoState)
    {
        qDebug() << "Restoring to normal state (macOS)";

        _isFullScreen = false;

        // Hide window before applying changes to its style
        // hide();

        // Restore normal styling
        setPalette(_originalPalette);

        // Show controls
        statusBar()->show();
        startButton->show();

        // Clear frameless window hint if set
        if (windowFlags() & Qt::FramelessWindowHint)
        {
            qDebug() << "Clearing frameless window hint during restore";
            setWindowFlags(windowFlags() & ~Qt::FramelessWindowHint);
        }

        // Restore all window flags
        initializePlatformMacOS();

        showNormal();

        // Restore to previous geometry if available
        if (_normalGeometry.isValid())
        {
            qDebug() << "Restoring to normal geometry:" << _normalGeometry;

            //            QTimer::singleShot(0, this, [this, geom = _normalGeometry]() {
            //                // Ensure we are not in a recursive handler call (should be false)
            //                // and that the window is still in a normal state.
            //                if (this->windowState() == Qt::WindowNoState) // Check state again
            //                {
            //                    qDebug().noquote() << QString("Deferred: Applying stored normal geometry");
            //                    this->setGeometry(geom);
            //                }
            //                else
            //                {
            //                    qDebug().noquote() << QString("Deferred: Window state is not Qt::WindowNoState or
            //                    recursive call suspected. Skipping setGeometry.");
            //                }
            //            });

            setGeometry(_normalGeometry);
        }
        else
        {
            qDebug() << "No stored normal geometry available, using default";
        }

        if (!isVisible())
        {
            qDebug() << "Window is not visible after showNormal/flag changes, explicitly calling show().";
            show();
        }
    }

    // Ensure the window is properly updated
    activateWindow();
    raise();
}

void MainWindow::handleWindowStateChangeWindows(Qt::WindowStates oldState, Qt::WindowStates newState)
{
    // Handle maximize state
    if (newState & Qt::WindowMaximized &&
        !(newState & Qt::WindowFullScreen))  // Check if it's maximize and NOT fullscreen
    {
        // This is a standard maximize (e.g., user clicked maximize button), not our managed fullscreen.
        if (!_isFullScreen)  // Only if we are not in a managed fullscreen state
        {
            qDebug() << "Maximized (Windows) - standard maximize";
            if (oldState == Qt::WindowNoState)
            {  // If maximizing from a normal (non-maximized, non-fullscreen) state
                _normalGeometry = normalGeometry();  // Store pre-maximize geometry
                qDebug() << "Stored geometry from Normal state for Maximize:" << _normalGeometry;
            }
        }
        else
        {
            // If we get here while _isFullScreen is true, it means we're in an inconsistent state
            // This can happen if the user uses OS controls to maximize while in fullscreen
            _isFullScreen = false;
            setPalette(_originalPalette);
            statusBar()->show();
            startButton->show();
        }
    }
    else if (newState & Qt::WindowFullScreen)  // Handle fullscreen state
    {
        qDebug() << "FullScreen (Windows)";
        _isFullScreen = true;

        // Store geometry to restore to when exiting fullscreen.
        if (!(oldState & Qt::WindowFullScreen))
        {
            if (oldState == Qt::WindowNoState)
            {
                _normalGeometry = this->geometry();
                qDebug() << "Stored geometry from Normal state for FullScreen:" << _normalGeometry;
            }
            else if (oldState & Qt::WindowMaximized)
            {
                qDebug() << "Transitioning to FullScreen from Maximized, _normalGeometry is:" << _normalGeometry;
            }
        }
        // Apply fullscreen styling
        QPalette palette;
        palette.setColor(QPalette::Window, Qt::black);
        setPalette(palette);
        statusBar()->hide();
        startButton->hide();
        // Do NOT call setWindowFlags or showFullScreen() here! Already handled in shortcut.
    }
    // Handle restore state
    else if (newState == Qt::WindowNoState)
    {
        qDebug() << "Restored (Windows)";

        if (_isFullScreen)
        {
            // We are exiting our managed fullscreen state
            qDebug() << "Exiting managed fullscreen to normal state";
            _isFullScreen = false;

            // Restore normal styling
            setPalette(_originalPalette);
            statusBar()->show();
            startButton->show();

            setWindowFlags(windowFlags() & ~Qt::FramelessWindowHint);  // Restore frame

            if (_normalGeometry.isValid())
            {
                qDebug() << "Restoring geometry to:" << _normalGeometry;
                setGeometry(_normalGeometry);  // Restore geometry
            }
            else
            {
                qDebug() << "No valid _normalGeometry to restore for exiting fullscreen.";
            }
            // Do not call showNormal() here, already done in shortcut
        }
        else
        {
            // Transition to WindowNoState, but not from our managed fullscreen.
            // E.g., user un-maximizes a window that was never in our managed fullscreen.
            qDebug() << "Restored (Windows) - standard unmaximize or other normal transition";

            // Ensure frameless hint is off if it was somehow set
            if (windowFlags() & Qt::FramelessWindowHint)
            {
                setWindowFlags(windowFlags() & ~Qt::FramelessWindowHint);
            }

            // If it was a standard maximize (oldState was Maximized and we are not in _isFullScreen), _normalGeometry
            // should have been set when it was maximized from a normal state.
            if (oldState & Qt::WindowMaximized)
            {
                if (_normalGeometry.isValid())
                {
                    qDebug() << "Restoring geometry from standard Maximize:" << _normalGeometry;
                    setGeometry(_normalGeometry);
                }
                else
                {
                    qDebug() << "No valid _normalGeometry to restore for standard unmaximize.";
                }
            }
        }
    }
}

void MainWindow::handleWindowStateChangeLinux(Qt::WindowStates oldState, Qt::WindowStates newState)
{
    // Handle maximize state
    if (newState & Qt::WindowMaximized && !(newState & Qt::WindowFullScreen))
    {
        qDebug() << "Maximized (Linux)";

        // Ensure we're not in fullscreen mode
        _isFullScreen = false;

        showMaximized();
    }
    // Handle fullscreen state
    else if (newState & Qt::WindowFullScreen)
    {
        qDebug() << "FullScreen (Linux)";

        hide();
        _isFullScreen = true;

        // Store geometry if coming from normal state
        if (oldState == Qt::WindowNoState)
        {
            _normalGeometry = normalGeometry();
        }

        // Apply fullscreen styling
        QPalette palette;
        palette.setColor(QPalette::Window, Qt::black);
        setPalette(palette);
        statusBar()->hide();
        startButton->hide();

        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        showFullScreen();
    }
    // Handle restore state
    else if (newState == Qt::WindowNoState)
    {
        qDebug() << "Restored (Linux)";

        // Skip intermediate restore events
        if (oldState & Qt::WindowMaximized && !_isFullScreen)
        {
            return;
        }

        hide();
        _isFullScreen = false;

        // Restore normal styling
        setPalette(_originalPalette);
        statusBar()->show();
        startButton->show();

        // Restore window flags and geometry
        setWindowFlags(windowFlags() & ~Qt::FramelessWindowHint);

        // Restore to previous geometry
        if (_normalGeometry.isValid())
        {
            setGeometry(_normalGeometry);
        }

        showNormal();
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    // Highlight drop area when mouse enters the window
    ui->contentFrame->setStyleSheet("border: 1px solid red;");

    // Allow dropping files into window
    event->acceptProposedAction();
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent* event)
{
    // Remove drop area highlight when cursor left the window area
    ui->contentFrame->setStyleSheet("border: none;");
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QMimeData* mimeData = event->mimeData();

    if (mimeData->hasUrls())
    {
        QStringList pathList;
        QList<QUrl> urlList = mimeData->urls();

        // extract the local paths of the files
        for (int i = 0; i < urlList.size() && i < 32; i++)
        {
            pathList.append(urlList.at(i).toLocalFile());
        }

        qDebug() << pathList.size() << "files dropped";
        qDebug() << pathList.join(",");

        // Process the dropped file
        QString filepath = pathList.first();
        std::string file = filepath.toStdString();

        // Save the directory for next time
        QFileInfo fileInfo(filepath);
        QString ext = fileInfo.suffix();

        // Save directory to settings
        saveLastDirectory(filepath);

        // Determine file type
        SupportedFileCategoriesEnum category = FileManager::determineFileCategoryByExtension(filepath);

        // Auto-start emulator if not running (except for symbol files which don't need it)
        if (!_emulator && category != FileSymbol && category != FileUnknown)
        {
            qDebug() << "Auto-starting emulator for dropped file";
            handleStartButton();  // Create and start new emulator instance
        }

        // Now proceed with file loading
        switch (category)
        {
            case FileROM:
                break;
            case FileSnapshot:
                if (_emulator)
                    _emulator->LoadSnapshot(file);
                break;
            case FileTape:
                if (_emulator)
                    _emulator->LoadTape(file);
                break;
            case FileDisk:
                if (_emulator)
                    _emulator->LoadDisk(file);
                break;
            case FileSymbol:
                if (_emulator && _emulator->GetDebugManager())
                {
                    _emulator->GetDebugManager()->GetLabelManager()->LoadLabels(file);
                }
                break;
            default:
                break;
        };
    }

    // Remove drop area highlight
    ui->contentFrame->setStyleSheet("border: none;");
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    event->accept();

    qDebug() << "MainWindow : keyPressEvent , key : " << event->text();
}

void MainWindow::mousePressEvent(QMouseEvent* event)
{
    event->accept();

    qDebug() << "MainWindow : mousePressEvent";
}
/// endregion </QWidget events override>

void MainWindow::initializePlatformMacOS()
{
    // Set window flags for macOS
    Qt::WindowFlags flags = windowFlags();
    flags |= Qt::WindowMinimizeButtonHint;
    flags |= Qt::WindowMaximizeButtonHint;
    flags |= Qt::WindowCloseButtonHint;
    flags |= Qt::WindowSystemMenuHint;
    flags |= Qt::WindowTitleHint;
    flags |= Qt::WindowCloseButtonHint;
    flags |= Qt::CustomizeWindowHint;

    // Explicitly disable the fullscreen button
    flags &= ~Qt::WindowFullscreenButtonHint;

    setWindowFlags(flags);

    // Store the original palette for later restoration
    _originalPalette = palette();

    // Store normal geometry
    _normalGeometry = normalGeometry();

    qDebug() << "macOS window initialized with flags:" << flags;
}

void MainWindow::initializePlatformWindows()
{
    // Windows-specific initialization
    // Set window flags for Windows behavior
    this->setWindowFlag(Qt::WindowMaximizeButtonHint);
}

void MainWindow::initializePlatformLinux()
{
    // Linux-specific initialization
    // Set window flags for Linux behavior
    this->setWindowFlag(Qt::WindowMaximizeButtonHint);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    // Handle main window dragging state for docking manager
    if (watched == this)
    {
        if (event->type() == QEvent::NonClientAreaMouseButtonPress)
        {
            if (_dockingManager)
                _dockingManager->setSnappingLocked(true);
        }
        else if (event->type() == QEvent::NonClientAreaMouseButtonRelease)
        {
            if (_dockingManager)
                _dockingManager->setSnappingLocked(false);
        }
    }

    switch (event->type())
    {
        case QEvent::KeyPress:
        {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
            QString keyName = QKeySequence(keyEvent->key()).toString();
            QString hexScanCode = QString("0x%1").arg(keyEvent->nativeScanCode(), 4, 16, QLatin1Char('0'));
            QString hexVirtualKey = QString("0x%1").arg(keyEvent->nativeVirtualKey(), 4, 16, QLatin1Char('0'));

            qDebug() << "MainWindow : eventFilter - keyPress, scan: " << hexScanCode << "virt: " << hexVirtualKey
                     << " key: " << keyName << " " << keyEvent->text();

            /*
            if (keyEvent->key() == Qt::Key_F1)
            {
                   if( QWidget* w = QApplication::widgetAt( QCursor::pos() ) )
                   {
                           if( w == XXX )
                           {
                                  // show help
                                  return;
                            }
                   }
            }
            */

            deviceScreen->handleExternalKeyPress(keyEvent);
        }
        break;
        case QEvent::KeyRelease:
        {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
            QString keyName = QKeySequence(keyEvent->key()).toString();
            QString hexScanCode = QString("0x%1").arg(keyEvent->nativeScanCode(), 4, 16, QLatin1Char('0'));
            QString hexVirtualKey = QString("0x%1").arg(keyEvent->nativeVirtualKey(), 4, 16, QLatin1Char('0'));

            qDebug() << "MainWindow : eventFilter - keyRelease, scan: " << hexScanCode << "virt: " << hexVirtualKey
                     << " key: " << keyName << " " << keyEvent->text();

            deviceScreen->handleExternalKeyRelease(keyEvent);
        }
        break;
        case QEvent::Move:
            _lastCursorPos = QCursor::pos();
            break;
        case QEvent::Resize:
        case QEvent::Show:
            _dockingManager->updateDockedWindows();
            break;

        case QEvent::NonClientAreaMouseButtonPress:
            _lastCursorPos = QCursor::pos();
            break;
        case QEvent::NonClientAreaMouseButtonRelease:
            _dockingManager->updateDockedWindows();
            break;
        case QEvent::NonClientAreaMouseMove:
        {
            if (watched == this && static_cast<QMouseEvent*>(event)->buttons() == Qt::LeftButton)
            {
                QPoint currentPos = QCursor::pos();
                QPoint delta = currentPos - _lastCursorPos;
                _dockingManager->moveDockedWindows(delta);

                _lastCursorPos = currentPos;
            }
        }
        break;
        default:
            break;
    }

    // return false;

    // Forward the key event to QShortcut
    QApplication::sendEvent(_fullScreenShortcut, event);

    return QMainWindow::eventFilter(watched, event);
}

/// endregion </Protected members>

/// region <Slots>

void MainWindow::handleStartButton()
{
    // This is the "smart" handler for the UI Start/Stop button
    // It handles multiple states:
    // - If emulator is NULL -> Create and start new instance
    // - If emulator is PAUSED -> Resume (convenience feature)
    // - If emulator is RUNNING -> Stop and destroy instance
    //
    // Note: Menu actions use specific handlers (handleStartEmulator, handleResumeEmulator)
    // that don't have this "smart" behavior - they do exactly what they say.
    
    // Lock will be removed after method exit
    QMutexLocker ml(&lockMutex);

    // Check if emulator is paused - if so, resume it (convenience for button)
    if (_emulator && _emulator->IsPaused())
    {
        std::string emulatorId = _emulator->GetId();
        _emulatorManager->ResumeEmulator(emulatorId);  // Use EmulatorManager for lifecycle
        updateMenuStates();
        return;
    }

    if (_emulator == nullptr)
    {
        startButton->setEnabled(false);

        // Clear log
        logWindow->reset();

        size_t size = sizeof(AutomationWebAPI);
        size_t sizeAutomation = sizeof(Automation);
        EmulatorManager* test = EmulatorManager::GetInstance();

        // Create a new emulator instance
        _emulator = _emulatorManager->CreateEmulator("test", LoggerLevel::LogInfo);

        // Initialize emulator instance
        if (_emulator)
        {
            _lastFrameCount = 0;

            _emulator->DebugOff();

            /// region <Setup logging>
            // Redirect all module logger output to LogWindow
            if (true)
            {
                ModuleLogger& logger = *_emulator->GetLogger();
                logger.SetLoggingLevel(LoggerLevel::LogInfo);

                // Mute frequently firing events
                logger.TurnOffLoggingForModule(PlatformModulesEnum::MODULE_Z80,
                                               PlatformZ80SubmodulesEnum::SUBMODULE_Z80_M1);
                logger.TurnOffLoggingForModule(PlatformModulesEnum::MODULE_IO,
                                               PlatformIOSubmodulesEnum::SUBMODULE_IO_GENERIC);
                logger.TurnOffLoggingForModule(PlatformModulesEnum::MODULE_IO,
                                               PlatformIOSubmodulesEnum::SUBMODULE_IO_IN);
                logger.TurnOffLoggingForModule(PlatformModulesEnum::MODULE_IO,
                                               PlatformIOSubmodulesEnum::SUBMODULE_IO_OUT);
                logger.TurnOffLoggingForModule(PlatformModulesEnum::MODULE_MEMORY,
                                               PlatformMemorySubmodulesEnum::SUBMODULE_MEM_ROM);
                logger.TurnOffLoggingForModule(PlatformModulesEnum::MODULE_CORE,
                                               PlatformCoreSubmodulesEnum::SUBMODULE_CORE_MAINLOOP);

                logger.TurnOffLoggingForAll();
                // logger.TurnOnLoggingForModule(MODULE_IO, SUBMODULE_IO_TAPE);
                // logger.TurnOnLoggingForModule(MODULE_IO, SUBMODULE_IO_IN);
                // logger.TurnOnLoggingForModule(MODULE_IO, SUBMODULE_IO_OUT);
                logger.TurnOnLoggingForModule(MODULE_DISK, SUBMODULE_DISK_FDC);

                std::string dumpSettings = logger.DumpSettings();
                qDebug("%s", dumpSettings.c_str());

                // Mute I/O outs to frequently used ports
                PortDecoder& portDecoder = *_emulator->GetContext()->pPortDecoder;
                portDecoder.MuteLoggingForPort(0x00FE);
                portDecoder.MuteLoggingForPort(0x7FFD);
                portDecoder.MuteLoggingForPort(0xFFFD);
                portDecoder.MuteLoggingForPort(0xBFFD);

                if (false)
                {
                    // Redirect all module logger output to the LogWindow
                    ModuleLoggerObserver* loggerObserverInstance = static_cast<ModuleLoggerObserver*>(logWindow);
                    ModuleObserverObserverCallbackMethod loggerCallback =
                        static_cast<ModuleObserverObserverCallbackMethod>(&LogWindow::Out);
                    logger.SetLoggerOut(loggerObserverInstance, loggerCallback);
                    logWindow->reset();
                    logWindow->show();
                }
            }

            /// endregion </Setup logging>

            /// region <Setup breakpoints>
            BreakpointManager& breakpointManager = *_emulator->GetBreakpointManager();
            // breakpointManager.AddExecutionBreakpoint(0x05ED);   // LD_SAMPLE in SOS48
            // breakpointManager.AddExecutionBreakpoint(0x05FA);   // LD_SAMPLE - pilot edge detected
            // breakpointManager.AddExecutionBreakpoint(0x0562);   // LD_BYTES - first IN A,($FE)
            // breakpointManager.AddExecutionBreakpoint(0x04D8);   // SA_LEADER - 5 seconds of pilot during SAVE
            /// endregion </Setup breakpoints>

            // Reinitialize and attach emulator audio (audio device can only be bound to one emulator at a time)
            if (_soundManager)
            {
                qDebug() << "MainWindow::handleStartButton() - Reinitializing audio device";
                _soundManager->deinit();
                _soundManager->init();

                qDebug() << "MainWindow::handleStartButton() - Setting audio callback for UI-started emulator";
                _emulator->SetAudioCallback(_soundManager, &AppSoundManager::audioCallback);
            }

            // Attach emulator framebuffer to GUI
            FramebufferDescriptor framebufferDesc = _emulator->GetFramebuffer();
            this->deviceScreen->init(framebufferDesc.width, framebufferDesc.height, framebufferDesc.memoryBuffer);

            /*
            int scale = 8;
            int width = framebufferDesc.width * scale;
            int height = framebufferDesc.height * scale;
            this->deviceScreen->resize(width, height);
            this->deviceScreen->setMinimumSize(width, height);
            this->adjustSize();
             */

            // Subscribe to video frame refresh events
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            Observer* observerInstance = static_cast<Observer*>(this);

            // Subscribe to video frame refresh events
            ObserverCallbackMethod callback =
                static_cast<ObserverCallbackMethod>(&MainWindow::handleMessageScreenRefresh);
            messageCenter.AddObserver(NC_VIDEO_FRAME_REFRESH, observerInstance, callback);

            // Subscribe to file open request events
            ObserverCallbackMethod fileOpenCallback =
                static_cast<ObserverCallbackMethod>(&MainWindow::handleFileOpenRequest);
            messageCenter.AddObserver(NC_FILE_OPEN_REQUEST, observerInstance, fileOpenCallback);

            // Subscribe to emulator state change events
            ObserverCallbackMethod stateCallback =
                static_cast<ObserverCallbackMethod>(&MainWindow::handleEmulatorStateChanged);
            messageCenter.AddObserver(NC_EMULATOR_STATE_CHANGE, observerInstance, stateCallback);

            // Subscribe to emulator instance destruction events
            ObserverCallbackMethod destroyCallback =
                static_cast<ObserverCallbackMethod>(&MainWindow::handleEmulatorInstanceDestroyed);
            messageCenter.AddObserver(NC_EMULATOR_INSTANCE_DESTROYED, observerInstance, destroyCallback);

            // Subscribe to emulator instance creation events
            ObserverCallbackMethod createCallback =
                static_cast<ObserverCallbackMethod>(&MainWindow::handleEmulatorInstanceCreated);
            messageCenter.AddObserver(NC_EMULATOR_INSTANCE_CREATED, observerInstance, createCallback);

            // Notify debugger about new emulator instance
            // Debugger will subscribe to required event messages from emulator core (like execution state changes)
            debuggerWindow->setEmulator(_emulator.get());

            // Enable audio output
            if (_soundManager)
            {
                qDebug() << "MainWindow::handleStartButton() - Starting audio for UI-started emulator";
                _soundManager->start();
            }

            // Pass active emulator reference to menu manager
            if (_menuManager)
            {
                _menuManager->setActiveEmulator(_emulator);
            }

            // Start in async own thread - use EmulatorManager for lifecycle
            std::string emulatorId = _emulator->GetId();
            _emulatorManager->StartEmulatorAsync(emulatorId);

            fflush(stdout);
            fflush(stderr);
            startButton->setText("Stop");
            startButton->setEnabled(true);
            updateMenuStates();

            // DEBUG: Reset ZX-Spectrum each 2 seconds
            // QTimer *timer = new QTimer(this);
            // connect(timer, SIGNAL(timeout()), this, SLOT(resetEmulator()));
            // timer->start(2000);
        }
        else
        {
            _emulator = nullptr;
        }
    }
    else
    {
        startButton->setEnabled(false);

        // Disable audio output
        _soundManager->stop();

        if (_emulator)
        {
            // Stop emulator instance - use EmulatorManager for lifecycle
            std::string emulatorId = _emulator->GetId();
            _emulatorManager->StopEmulator(emulatorId);
            updateMenuStates();

    // Unsubscribe from message bus events
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);

    ObserverCallbackMethod callback =
        static_cast<ObserverCallbackMethod>(&MainWindow::handleMessageScreenRefresh);
    messageCenter.RemoveObserver(NC_VIDEO_FRAME_REFRESH, observerInstance, callback);

    ObserverCallbackMethod stateCallback =
        static_cast<ObserverCallbackMethod>(&MainWindow::handleEmulatorStateChanged);
    messageCenter.RemoveObserver(NC_EMULATOR_STATE_CHANGE, observerInstance, stateCallback);

    ObserverCallbackMethod destroyCallback =
        static_cast<ObserverCallbackMethod>(&MainWindow::handleEmulatorInstanceDestroyed);
    messageCenter.RemoveObserver(NC_EMULATOR_INSTANCE_DESTROYED, observerInstance, destroyCallback);

    ObserverCallbackMethod createCallback =
        static_cast<ObserverCallbackMethod>(&MainWindow::handleEmulatorInstanceCreated);
    messageCenter.RemoveObserver(NC_EMULATOR_INSTANCE_CREATED, observerInstance, createCallback);

            // Detach framebuffer
            deviceScreen->detach();

            // Remove emulator from manager
            _emulatorManager->RemoveEmulator(_emulator->GetId());
            _emulator = nullptr;
            
            // Update menu states - no active emulator now
            updateMenuStates();
        }

        _lastFrameCount = 0;

        fflush(stdout);
        fflush(stderr);
        startButton->setText("Start");
        startButton->setEnabled(true);
    }
}

void MainWindow::handleFullScreenShortcut()
{
#ifdef Q_OS_WIN
    handleFullScreenShortcutWindows();
#elif defined(Q_OS_MAC)
    handleFullScreenShortcutMacOS();
#elif defined(Q_OS_LINUX)
    handleFullScreenShortcutLinux();
#endif
}

void MainWindow::handleFullScreenShortcutWindows()
{
    if (windowState() & Qt::WindowFullScreen)
    {
        if (_dockingManager) _dockingManager->setSnappingLocked(true);

        // Always restore palette and frame before leaving fullscreen
        setPalette(_originalPalette);
        setWindowFlags(windowFlags() & ~Qt::FramelessWindowHint);
        statusBar()->show();
        startButton->show();

        // First show normal to ensure clean state
        showNormal();
        
        if (_preFullScreenState & Qt::WindowMaximized)
        {
            // Restore to maximized state
            showMaximized();
        }
        else if (_normalGeometry.isValid())
        {
            // Restore to normal state with saved geometry
            setGeometry(_normalGeometry);
        }

        // Defer child window restoration and unlock until the event queue has processed the main window changes.
        QTimer::singleShot(100, this, [this]() {
            if (_dockingManager) {
                _dockingManager->onExitFullscreen();

                QTimer::singleShot(100, this, [this](){
                    if (_dockingManager) _dockingManager->setSnappingLocked(false);
                });
            }
        });
    }
    else
    {
        if (_dockingManager) _dockingManager->setSnappingLocked(true);
        if (_dockingManager) _dockingManager->onEnterFullscreen();

        // Store state and geometry before entering fullscreen
        bool wasMaximized = (windowState() & Qt::WindowMaximized) && !(windowFlags() & Qt::FramelessWindowHint);
        _preFullScreenState = wasMaximized ? Qt::WindowMaximized : Qt::WindowNoState;

        // Always store the current geometry
        if (wasMaximized)
        {
            // If maximized, store both the current (maximized) state and the normal geometry
            _maximizedGeometry = geometry();
            qDebug() << "Storing maximized geometry:" << _maximizedGeometry;

            // Store the normal geometry if we have it, otherwise use current geometry as fallback
            if (!_normalGeometry.isValid())
            {
                _normalGeometry = normalGeometry();
                qDebug() << "Using normal geometry from window:" << _normalGeometry;
            }
        }
        else
        {
            // If normal, just store the normal geometry
            _normalGeometry = geometry();
            qDebug() << "Storing normal geometry:" << _normalGeometry;
        }
        // Set palette and hide UI for fullscreen
        QPalette palette;
        palette.setColor(QPalette::Window, Qt::black);
        setPalette(palette);
        statusBar()->hide();
        startButton->hide();
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        setWindowState(Qt::WindowNoState);
        showFullScreen();

        QTimer::singleShot(100, this, [this](){
            if (_dockingManager) _dockingManager->setSnappingLocked(false);
        });
    }
}

void MainWindow::handleFullScreenShortcutMacOS()
{
    if (windowState() & Qt::WindowFullScreen)
    {
        if (_dockingManager) _dockingManager->setSnappingLocked(true);

        setWindowFlags(Qt::Window);  // Prevent horizontal transition from full screen to system desktop
        // Restore previous state and geometry
        if (_preFullScreenState & Qt::WindowMaximized)
        {
            if (_maximizedGeometry.isValid())
                setGeometry(_maximizedGeometry);
            showMaximized();
        }
        else
        {
            if (_normalGeometry.isValid())
                setGeometry(_normalGeometry);
            showNormal();
        }

        // Defer child window restoration and unlock until the event queue has processed the main window changes.
        QTimer::singleShot(100, this, [this]() {
            if (_dockingManager) {
                _dockingManager->onExitFullscreen();

                QTimer::singleShot(100, this, [this](){
                    if (_dockingManager) _dockingManager->setSnappingLocked(false);
                });
            }
        });
    }
    else
    {
        if (_dockingManager) _dockingManager->setSnappingLocked(true);
        if (_dockingManager) _dockingManager->onEnterFullscreen();

        // Store state and geometry before entering fullscreen
        if (windowState() & Qt::WindowMaximized)
        {
            _preFullScreenState = Qt::WindowMaximized;
            _maximizedGeometry = geometry();
        }
        else
        {
            _preFullScreenState = Qt::WindowNoState;
            _normalGeometry = geometry();
        }
        showFullScreen();
        QTimer::singleShot(100, this, [this](){
            if (_dockingManager) _dockingManager->setSnappingLocked(false);
        });
    }
}

void MainWindow::handleFullScreenShortcutLinux()
{
    if (windowState() & Qt::WindowFullScreen)
    {
        if (_dockingManager) _dockingManager->setSnappingLocked(true);
        
        // Restore previous state and geometry
        if (_preFullScreenState & Qt::WindowMaximized)
        {
            if (_maximizedGeometry.isValid())
                setGeometry(_maximizedGeometry);
            showMaximized();
        }
        else
        {
            if (_normalGeometry.isValid())
                setGeometry(_normalGeometry);
            showNormal();
        }

        // Defer child window restoration and unlock until the event queue has processed the main window changes.
        QTimer::singleShot(100, this, [this]() {
            if (_dockingManager) {
                _dockingManager->onExitFullscreen();

                QTimer::singleShot(100, this, [this](){
                    if (_dockingManager) _dockingManager->setSnappingLocked(false);
                });
            }
        });
    }
    else
    {
        if (_dockingManager) _dockingManager->setSnappingLocked(true);
        if (_dockingManager) _dockingManager->onEnterFullscreen();

        // Store state and geometry before entering fullscreen
        if (windowState() & Qt::WindowMaximized)
        {
            _preFullScreenState = Qt::WindowMaximized;
            _maximizedGeometry = geometry();
        }
        else
        {
            _preFullScreenState = Qt::WindowNoState;
            _normalGeometry = geometry();
        }
        showFullScreen();
        QTimer::singleShot(100, this, [this](){
            if (_dockingManager) _dockingManager->setSnappingLocked(false);
        });
    }
}

void MainWindow::handleMessageScreenRefresh(int id, Message* message)
{
    if (deviceScreen)
    {
        // Invoke deviceScreen->refresh() in main thread
        QMetaObject::invokeMethod(deviceScreen, "refresh", Qt::QueuedConnection);

        if (message && message->obj)
        {
            SimpleNumberPayload* payload = (SimpleNumberPayload*)message->obj;
            uint32_t frameCount = payload->_payloadNumber;

#ifdef _DEBUG
            if (frameCount - _lastFrameCount > 1)
            {
                qDebug() << QString::asprintf("Frame(s) skipped from:%d till: %d", _lastFrameCount, frameCount);
            }
#endif

            _lastFrameCount = frameCount;
        }
    }
}

void MainWindow::handleFileOpenRequest(int id, Message* message)
{
    if (!_emulator)
    {
        qDebug() << "File open request received but no emulator is running";
        return;
    }

    // Check if a filepath was provided in the message payload
    if (message && message->obj)
    {
        // The message payload should be a SimpleTextPayload containing the filepath
        SimpleTextPayload* payload = static_cast<SimpleTextPayload*>(message->obj);
        QString filepath = QString::fromStdString(payload->_payloadText);

        // Use Qt's signal/slot mechanism to handle the file opening on the main thread
        QMetaObject::invokeMethod(this, "openSpecificFile", Qt::QueuedConnection, Q_ARG(QString, filepath));
    }
    else
    {
        // No filepath provided, show a file open dialog on the main thread
        QMetaObject::invokeMethod(this, "openFileDialog", Qt::QueuedConnection);
    }
}

void MainWindow::openSpecificFile(const QString& filepath)
{
    // Check if the file exists
    QFileInfo fileInfo(filepath);
    if (fileInfo.exists() && fileInfo.isFile())
    {
        // Save directory to settings
        saveLastDirectory(filepath);

        // Process the file based on its extension
        QString filepathCopy = filepath;  // Create a non-const copy for the method call
        SupportedFileCategoriesEnum category = FileManager::determineFileCategoryByExtension(filepathCopy);
        std::string file = filepath.toStdString();

        switch (category)
        {
            case FileROM:
                break;
            case FileSnapshot:
                _emulator->LoadSnapshot(file);
                break;
            case FileTape:
                _emulator->LoadTape(file);
                break;
            case FileDisk:
                _emulator->LoadDisk(file);
                break;
            case FileSymbol:
                if (_emulator && _emulator->GetDebugManager())
                {
                    _emulator->GetDebugManager()->GetLabelManager()->LoadLabels(file);
                }
                break;
            default:
                qDebug() << "Unsupported file type:" << filepath;
                break;
        };
    }
    else
    {
        qDebug() << "File does not exist or is not a regular file:" << filepath;
        // If the specified file doesn't exist, fall back to the file dialog
        openFileDialog();
    }
}

void MainWindow::openFileDialog()
{
    // Show a file open dialog using the last directory
    QString filePath = QFileDialog::getOpenFileName(
        this, tr("Open File"), _lastDirectory,
        tr("All Supported Files (*.sna *.z80 *.tap *.tzx *.trd *.scl *.fdi *.td0 *.udi);;Snapshots (*.sna "
           "*.z80);;Tapes (*.tap *.tzx);;Disks (*.trd *.scl *.fdi *.td0 *.udi);;All Files (*)"));

    if (!filePath.isEmpty())
    {
        // Save directory to settings
        saveLastDirectory(filePath);

        // Process the selected file
        QString filePathCopy = filePath;  // Create a non-const copy for the method call
        SupportedFileCategoriesEnum category = FileManager::determineFileCategoryByExtension(filePathCopy);
        std::string file = filePath.toStdString();

        switch (category)
        {
            case FileROM:
                break;
            case FileSnapshot:
                _emulator->LoadSnapshot(file);
                break;
            case FileTape:
                _emulator->LoadTape(file);
                break;
            case FileDisk:
                _emulator->LoadDisk(file);
                break;
            case FileSymbol:
                if (_emulator && _emulator->GetDebugManager())
                {
                    _emulator->GetDebugManager()->GetLabelManager()->LoadLabels(file);
                }
                break;
            default:
                qDebug() << "Unsupported file type:" << filePath;
                break;
        };
    }
}

void MainWindow::saveLastDirectory(const QString& path)
{
    if (path.isEmpty())
        return;

    QFileInfo fileInfo(path);
    QString dirPath;

    if (fileInfo.isFile())
        dirPath = fileInfo.absolutePath();
    else if (fileInfo.isDir())
        dirPath = path;
    else
        return;

    // Only update if it's different
    if (_lastDirectory != dirPath)
    {
        _lastDirectory = dirPath;

        // Save to settings with explicit organization and application name
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
        settings.setValue("LastFileDirectory", _lastDirectory);
        settings.sync();  // Force immediate write to disk

        qDebug() << "Saved last directory to settings:" << _lastDirectory;
    }
}

void MainWindow::resetEmulator()
{
    if (_emulator)
    {
        // Reset handles pause/resume internally to avoid race conditions
        _emulator->Reset();
        _lastFrameCount = 0;
        
        // Update menu states
        updateMenuStates();
    }
}

/// region <Menu action handlers>

void MainWindow::handleStartEmulator()
{
    // Menu "Start" action should ONLY start a new emulator instance
    // It should not resume a paused emulator (that's what Resume is for)
    if (_emulator == nullptr)
    {
        handleStartButton();
    }
    else
    {
        qDebug() << "Emulator already running. Use Stop or Resume instead.";
    }
}

void MainWindow::handlePauseEmulator()
{
    if (_emulator && _emulator->IsRunning() && !_emulator->IsPaused())
    {
        std::string emulatorId = _emulator->GetId();
        _emulatorManager->PauseEmulator(emulatorId);  // Use EmulatorManager for lifecycle
        updateMenuStates();
        qDebug() << "Emulator paused";
    }
}

void MainWindow::handleResumeEmulator()
{
    if (_emulator && _emulator->IsPaused())
    {
        std::string emulatorId = _emulator->GetId();
        _emulatorManager->ResumeEmulator(emulatorId);  // Use EmulatorManager for lifecycle
        updateMenuStates();
        qDebug() << "Emulator resumed";
    }
}

void MainWindow::handleStopEmulator()
{
    if (_emulator)
    {
        // Full cleanup like the main button
        startButton->setEnabled(false);

        // Stop and deinit audio (must be reinitialized for next emulator)
        _soundManager->stop();
        _soundManager->deinit();

        // Stop emulator instance - use EmulatorManager for lifecycle
        std::string emulatorId = _emulator->GetId();
        _emulatorManager->StopEmulator(emulatorId);

        // Unsubscribe from message bus events
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        Observer* observerInstance = static_cast<Observer*>(this);
        ObserverCallbackMethod callback =
            static_cast<ObserverCallbackMethod>(&MainWindow::handleMessageScreenRefresh);
        messageCenter.RemoveObserver(NC_VIDEO_FRAME_REFRESH, observerInstance, callback);

        ObserverCallbackMethod stateCallback =
            static_cast<ObserverCallbackMethod>(&MainWindow::handleEmulatorStateChanged);
        messageCenter.RemoveObserver(NC_EMULATOR_STATE_CHANGE, observerInstance, stateCallback);

        ObserverCallbackMethod destroyCallback =
            static_cast<ObserverCallbackMethod>(&MainWindow::handleEmulatorInstanceDestroyed);
        messageCenter.RemoveObserver(NC_EMULATOR_INSTANCE_DESTROYED, observerInstance, destroyCallback);

        ObserverCallbackMethod createCallback =
            static_cast<ObserverCallbackMethod>(&MainWindow::handleEmulatorInstanceCreated);
        messageCenter.RemoveObserver(NC_EMULATOR_INSTANCE_CREATED, observerInstance, createCallback);

        // Detach framebuffer
        deviceScreen->detach();

        // Remove emulator from manager
        _emulatorManager->RemoveEmulator(_emulator->GetId());
        _emulator = nullptr;

        _lastFrameCount = 0;

        // Update UI
        startButton->setText("Start");
        startButton->setEnabled(true);
        updateMenuStates();

        qDebug() << "Emulator stopped and cleaned up";
    }
}

void MainWindow::handleSpeedMultiplierChanged(int multiplier)
{
    if (_emulator)
    {
        Core* core = _emulator->GetContext()->pCore;
        if (core)
        {
            core->SetSpeedMultiplier(static_cast<uint8_t>(multiplier));
            qDebug() << "Speed multiplier set to" << multiplier << "x";
        }
    }
}

void MainWindow::handleTurboModeToggled(bool enabled)
{
    if (_emulator)
    {
        Core* core = _emulator->GetContext()->pCore;
        if (core)
        {
            if (enabled)
            {
                core->EnableTurboMode(false);  // No audio in turbo mode
                qDebug() << "Turbo mode enabled";
            }
            else
            {
                core->DisableTurboMode();
                qDebug() << "Turbo mode disabled";
            }
        }
    }
}

void MainWindow::handleStepIn()
{
    if (_emulator)
    {
        _emulator->RunSingleCPUCycle(false);
        qDebug() << "Step in executed";
    }
}

void MainWindow::handleStepOver()
{
    if (_emulator)
    {
        _emulator->StepOver();
        qDebug() << "Step over executed";
    }
}

void MainWindow::handleDebugModeToggled(bool enabled)
{
    if (_emulator)
    {
        if (enabled)
        {
            _emulator->DebugOn();
            qDebug() << "Debug mode enabled";
        }
        else
        {
            _emulator->DebugOff();
            qDebug() << "Debug mode disabled";
        }
    }
}

void MainWindow::handleDebuggerToggled(bool visible)
{
    if (debuggerWindow)
    {
        debuggerWindow->setVisible(visible);
    }
}

void MainWindow::handleLogWindowToggled(bool visible)
{
    if (logWindow)
    {
        logWindow->setVisible(visible);
    }
}

void MainWindow::updateMenuStates()
{
    if (_menuManager)
    {
        // Menu will query emulator directly - no state duplication!
        _menuManager->updateMenuStates(_emulator);
    }
}

/// endregion </Menu action handlers>

/// region <Message handlers>

void MainWindow::handleEmulatorStateChanged(int id, Message* message)
{
    Q_UNUSED(id);
    Q_UNUSED(message);

    // Update UI state to reflect emulator state changes
    QMetaObject::invokeMethod(this, "updateMenuStates", Qt::QueuedConnection);
}

void MainWindow::handleEmulatorInstanceDestroyed(int id, Message* message)
{
    Q_UNUSED(id);

    // Handle emulator instance destruction (from CLI, WebAPI, or UI)
    if (message && message->obj)
    {
        SimpleTextPayload* payload = dynamic_cast<SimpleTextPayload*>(message->obj);
        if (payload)
        {
            std::string destroyedId = payload->_payloadText;

            // Check if this was our active emulator - BEFORE queuing to main thread
            // to avoid race condition where _emulator might be cleared by the time lambda runs
            bool wasOurEmulator = (_emulator && _emulator->GetId() == destroyedId);

            if (wasOurEmulator)
            {
                // Update UI on main thread
                QMetaObject::invokeMethod(this, [this, destroyedId]() {
                    // Double-check _emulator still exists and matches
                    // (it might have been cleared by stop button handler already)
                    if (!_emulator || _emulator->GetId() != destroyedId)
                    {
                        qDebug() << "MainWindow: Emulator" << QString::fromStdString(destroyedId)
                                 << "already cleaned up by UI";
                        return;
                    }

                    // Stop and deinit audio (must be reinitialized for next emulator)
                    if (_soundManager)
                    {
                        qDebug() << "MainWindow: Stopping and deinitializing audio device";
                        _soundManager->stop();
                        _soundManager->deinit();
                    }

                    // Unsubscribe from MessageCenter events
                    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
                    Observer* observerInstance = static_cast<Observer*>(this);

                    ObserverCallbackMethod screenRefreshCallback =
                        static_cast<ObserverCallbackMethod>(&MainWindow::handleMessageScreenRefresh);
                    messageCenter.RemoveObserver(NC_VIDEO_FRAME_REFRESH, observerInstance, screenRefreshCallback);

                    ObserverCallbackMethod stateChangeCallback =
                        static_cast<ObserverCallbackMethod>(&MainWindow::handleEmulatorStateChanged);
                    messageCenter.RemoveObserver(NC_EMULATOR_STATE_CHANGE, observerInstance, stateChangeCallback);

                    // Detach framebuffer
                    deviceScreen->detach();

                    // Clear emulator reference from menu manager
                    if (_menuManager)
                    {
                        _menuManager->setActiveEmulator(nullptr);
                    }

                    // Clear emulator reference from debugger
                    if (debuggerWindow)
                    {
                        debuggerWindow->setEmulator(nullptr);
                    }

                    // Clear emulator reference
                    _emulator.reset();

                    // Update UI state
                    startButton->setText("Start");
                    startButton->setEnabled(true);
                    updateMenuStates();

                    qDebug() << "MainWindow: Active emulator instance" << QString::fromStdString(destroyedId)
                             << "destroyed externally - fully unbound from UI";
                }, Qt::QueuedConnection);
            }
        }
    }
}

void MainWindow::handleEmulatorInstanceCreated(int id, Message* message)
{
    Q_UNUSED(id);

    // Handle emulator instance creation
    if (message && message->obj)
    {
        SimpleTextPayload* payload = dynamic_cast<SimpleTextPayload*>(message->obj);
        if (payload)
        {
            std::string createdId = payload->_payloadText;

            // If we don't have an active emulator, adopt this new one (e.g., started from CLI or WebAPI)
            if (!_emulator)
            {
                auto* emulatorManager = EmulatorManager::GetInstance();
                auto emulator = emulatorManager->GetEmulator(createdId);

                if (emulator)
                {
                    // Update UI on main thread
                    QMetaObject::invokeMethod(this, [this, emulator]() {
                        // Set as our active emulator
                        _emulator = emulator;

                        // Reinitialize and bind audio callback (audio device can only be bound to one emulator at a time)
                        if (_soundManager)
                        {
                            qDebug() << "MainWindow::handleEmulatorInstanceCreated() - Reinitializing audio device";
                            _soundManager->deinit();
                            _soundManager->init();

                            qDebug() << "MainWindow::handleEmulatorInstanceCreated() - Binding audio to emulator" << QString::fromStdString(_emulator->GetId());
                            _emulator->SetAudioCallback(_soundManager, &AppSoundManager::audioCallback);
                            _soundManager->start();
                        }

                        // Subscribe to emulator state change events
                        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
                        Observer* observerInstance = static_cast<Observer*>(this);

                        ObserverCallbackMethod screenRefreshCallback =
                            static_cast<ObserverCallbackMethod>(&MainWindow::handleMessageScreenRefresh);
                        messageCenter.AddObserver(NC_VIDEO_FRAME_REFRESH, observerInstance, screenRefreshCallback);

                        ObserverCallbackMethod stateChangeCallback =
                            static_cast<ObserverCallbackMethod>(&MainWindow::handleEmulatorStateChanged);
                        messageCenter.AddObserver(NC_EMULATOR_STATE_CHANGE, observerInstance, stateChangeCallback);

                        // Initialize device screen with emulator's screen (if emulator is running or just initialized)
                        auto* context = _emulator->GetContext();
                        if (context && context->pScreen)
                        {
                            try {
                                // Get screen dimensions and framebuffer
                                auto& framebufferDesc = context->pScreen->GetFramebufferDescriptor();
                                deviceScreen->init(framebufferDesc.width, framebufferDesc.height, framebufferDesc.memoryBuffer);
                            } catch (const std::exception& e) {
                                qWarning() << "Failed to initialize device screen:" << e.what();
                            }
                        }

                        // Pass emulator reference to debugger
                        if (debuggerWindow)
                        {
                            debuggerWindow->setEmulator(_emulator.get());
                        }

                        // Pass active emulator reference to menu manager
                        if (_menuManager)
                        {
                            _menuManager->setActiveEmulator(_emulator);
                        }

                        // Update UI state
                        if (emulator->IsRunning())
                        {
                            startButton->setText("Stop");
                        }
                        else
                        {
                            startButton->setText("Start");
                        }
                        startButton->setEnabled(true);
                        updateMenuStates();

                        qDebug() << "MainWindow: Bound to emulator instance" << QString::fromStdString(emulator->GetId())
                                 << "(started externally from CLI/WebAPI)";
                    }, Qt::QueuedConnection);
                }
            }
        }
    }
}

/// endregion </Message handlers>

/// endregion </Slots>
