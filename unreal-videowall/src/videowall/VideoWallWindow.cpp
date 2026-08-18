// MSVC: winsock2.h must be included before any header that includes windows.h
#ifdef _MSC_VER
#include <windows.h>
#include <winsock2.h>

#endif

#include <base/featuremanager.h>
#include <emulatormanager.h>
#include <platform.h>

#include "videowall/VideoWallWindow.h"
#include "videowall/VideowallRecorder.h"
#include "videowall/VideowallRecordingWidget.h"
#include "3rdparty/message-center/messagecenter.h"
#include "emulator/notifications.h"

#ifdef ENABLE_AUTOMATION
#include <automation/automation.h>
#endif

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMouseEvent>
#include <QScreen>
#include <QShortcut>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <future>
#include <unordered_set>
#include <vector>

#include "emulator/soundmanager.h"
#include "videowall/EmulatorTile.h"
#include "videowall/TileGrid.h"

VideoWallWindow::VideoWallWindow(QWidget* parent) : QMainWindow(parent)
{
    _emulatorManager = EmulatorManager::GetInstance();

    setupUI();
    createDefaultPresets();

    MessageCenter::DefaultMessageCenter().AddObserver(NC_VIDEOWALL_SINGLE_SYNC_MODE,
        [this](int id, Message* message) {
            if (message && message->obj) {
                handleSingleSyncModeMessage(message->obj);
            }
        });

#ifdef ENABLE_AUTOMATION
    // CRITICAL: Get Automation singleton reference immediately (not deferred) so it's guaranteed
    // to exist when destructor runs. This prevents race condition where destructor
    // tries to stop() an uninitialized or garbage pointer.
    _automation = &Automation::GetInstance();
#endif

    // CRITICAL: Defer starting automation until after the Qt event loop is running.
    // On macOS, starting automation threads before app.exec() causes race conditions with
    // CoreText font initialization, leading to SIGSEGV in TBaseFont::CopyOpticalSizeAxis().
    // This single-shot timer fires after the event loop starts, ensuring the application
    // is fully initialized before we spawn threads or access the menu bar.
    QTimer::singleShot(0, this, &VideoWallWindow::initializeAfterEventLoopStart);

    // Debug: Log initial window geometry at startup
    QTimer::singleShot(100, this, [this]() {
        qDebug() << "=== INITIAL WINDOW GEOMETRY AT STARTUP ===";
        qDebug() << "  geometry():" << geometry();
        qDebug() << "  frameGeometry():" << frameGeometry();
        qDebug() << "  windowState():" << windowState();
        qDebug() << "  screen():" << window()->screen()->name();
        qDebug() << "==========================================";
    });

    // Enable mouse tracking for auto-show menu bar in fullscreen
    setMouseTracking(true);
    centralWidget()->setMouseTracking(true);
    _tileGrid->installEventFilter(this);

    resize(800, 600);
    setWindowTitle(tr("Unreal Speccy Video Wall"));
}

VideoWallWindow::~VideoWallWindow()
{
    // Destructors must not throw - wrap everything in try-catch
    try
    {
        // CRITICAL: Stop automation FIRST, before any Qt objects are destroyed.
        // The automation threads may reference Qt objects, so we must join all threads
        // before the widget destruction begins.
#ifdef ENABLE_AUTOMATION
        if (_automation)
        {
            _automation->stop();
            _automation = nullptr;  // Don't delete - it's a singleton
        }
#endif

        // CRITICAL: Unbind audio BEFORE stopping sound manager.
        // This clears the emulator's audio callback to prevent race conditions
        // where the audio thread accesses emulator memory during destruction.
        if (_audioBoundTile)
        {
            unbindAudioFromTile();
        }

        // Clean up sound manager
        if (_soundManager)
        {
            _soundManager->stop();
            _soundManager->deinit();
            delete _soundManager;
            _soundManager = nullptr;
        }
    }
    catch (const std::exception& e)
    {
        qCritical() << "Exception in VideoWallWindow destructor:" << e.what();
    }
    catch (...)
    {
        qCritical() << "Unknown exception in VideoWallWindow destructor";
    }
}

void VideoWallWindow::setupUI()
{
    _tileGrid = new TileGrid(this);
    setCentralWidget(_tileGrid);

    // Set black background to hide gaps at screen edges
    _tileGrid->setAutoFillBackground(true);
    QPalette pal = _tileGrid->palette();
    pal.setColor(QPalette::Window, Qt::black);
    _tileGrid->setPalette(pal);
}

void VideoWallWindow::initializeAfterEventLoopStart()
{
    // This method is called via QTimer::singleShot(0, ...) from the constructor.
    // At this point, the Qt event loop is running and the window is fully initialized.
    // This eliminates race conditions with CoreText font access on macOS.

#ifdef ENABLE_AUTOMATION
    // Start automation modules (WebAPI, CLI, Python, Lua)
    // The Automation object was created in constructor, we only defer start().
    if (_automation)
    {
        _automation->start();
        qDebug() << "Automation modules started";
    }
#endif  // ENABLE_AUTOMATION

    // Initialize sound manager for audio binding to focused tile
    _soundManager = new AppSoundManager();
    if (_soundManager->init())
    {
        _soundManager->start();
        qDebug() << "Sound manager initialized and started";
    }
    else
    {
        qWarning() << "Failed to initialize sound manager";
    }

    // Attach tile grid widget to VideowallRecorder for 50Hz combined Qt buffer capture
    VideowallRecorder::instance().setTargetWidget(_tileGrid);

    // Now safe to create menus - the window is fully initialized
    createMenus();
}

void VideoWallWindow::createMenus()
{
    // File menu
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));

    QAction* exitAction = fileMenu->addAction(tr("E&xit"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    // Emulator menu
    QMenu* emulatorMenu = menuBar()->addMenu(tr("&Emulator"));

    QAction* addTileAction = emulatorMenu->addAction(tr("&Add Tile\tCtrl+N"));
    connect(addTileAction, &QAction::triggered, this, &VideoWallWindow::addEmulatorTile);

    QAction* clearAllAction = emulatorMenu->addAction(tr("&Clear All"));
    connect(clearAllAction, &QAction::triggered, this, &VideoWallWindow::clearAllTiles);

    // View menu
    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));

    QAction* framelessAction = viewMenu->addAction(tr("&Frameless Mode\tF10"));
    framelessAction->setCheckable(true);
    framelessAction->setChecked(false);
    connect(framelessAction, &QAction::triggered, this, &VideoWallWindow::toggleFramelessMode);

    QAction* fullscreenAction = viewMenu->addAction(tr("F&ullscreen Mode\tCtrl+F"));
    fullscreenAction->setCheckable(true);
    fullscreenAction->setChecked(false);
    connect(fullscreenAction, &QAction::triggered, this, &VideoWallWindow::toggleFullscreenMode);

    viewMenu->addSeparator();

    QAction* screenHQAction = viewMenu->addAction(tr("Toggle Screen &HQ"));
    screenHQAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_S));
    connect(screenHQAction, &QAction::triggered, this, &VideoWallWindow::toggleScreenHQForAllTiles);

#ifdef ENABLE_RECORDING
    // Tools menu
    QMenu* toolsMenu = menuBar()->addMenu(tr("&Tools"));

    QAction* recordAction = toolsMenu->addAction(tr("&Record Video / Audio...\tCtrl+R"));
    recordAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    connect(recordAction, &QAction::triggered, this, &VideoWallWindow::handleVideoRecordingRequested);
#endif

    // Platform-specific shortcut installation
#ifdef Q_OS_WIN
    setupShortcutsWindows();
#elif defined(Q_OS_MAC)
    setupShortcutsMacOS();
#else
    setupShortcutsLinux();
#endif
}

void VideoWallWindow::setupShortcutsWindows()
{
    // Windows hides the menu bar in fullscreen, so QAction shortcuts stop firing.
    // Use Qt::ApplicationShortcut to guarantee delivery regardless of focus or menu visibility.
    auto makeShortcut = [this](QKeySequence key) {
        auto* s = new QShortcut(key, this);
        s->setContext(Qt::ApplicationShortcut);
        return s;
    };

    connect(makeShortcut(QKeySequence(Qt::CTRL | Qt::Key_N)), &QShortcut::activated,
            this, &VideoWallWindow::addEmulatorTile);
    connect(makeShortcut(QKeySequence(Qt::CTRL | Qt::Key_R)), &QShortcut::activated,
            this, &VideoWallWindow::handleVideoRecordingRequested);
    connect(makeShortcut(QKeySequence(Qt::CTRL | Qt::Key_Backspace)), &QShortcut::activated,
            this, &VideoWallWindow::removeLastTile);
    connect(makeShortcut(QKeySequence(Qt::Key_F10)), &QShortcut::activated,
            this, &VideoWallWindow::toggleFramelessMode);
    connect(makeShortcut(QKeySequence(Qt::CTRL | Qt::Key_F)), &QShortcut::activated,
            this, &VideoWallWindow::toggleFullscreenMode);
    connect(makeShortcut(QKeySequence(Qt::Key_Escape)), &QShortcut::activated,
            this, [this]() { if (_isFullscreen) toggleFullscreenMode(); });
}

void VideoWallWindow::setupShortcutsMacOS()
{
    // macOS: Qt::CTRL maps to the Command (⌘) key, so Ctrl+X below becomes Cmd+X.
    // Qt::WindowShortcut is sufficient — macOS keeps the window active even in fullscreen.
    auto makeShortcut = [this](QKeySequence key) {
        auto* s = new QShortcut(key, this);
        s->setContext(Qt::WindowShortcut);
        return s;
    };

    connect(makeShortcut(QKeySequence(Qt::CTRL | Qt::Key_N)), &QShortcut::activated,
            this, &VideoWallWindow::addEmulatorTile);
    connect(makeShortcut(QKeySequence(Qt::CTRL | Qt::Key_R)), &QShortcut::activated,
            this, &VideoWallWindow::handleVideoRecordingRequested);
    connect(makeShortcut(QKeySequence(Qt::CTRL | Qt::Key_Backspace)), &QShortcut::activated,
            this, &VideoWallWindow::removeLastTile);
    connect(makeShortcut(QKeySequence(Qt::Key_F10)), &QShortcut::activated,
            this, &VideoWallWindow::toggleFramelessMode);
    connect(makeShortcut(QKeySequence(Qt::CTRL | Qt::Key_F)), &QShortcut::activated,
            this, &VideoWallWindow::toggleFullscreenMode);
    connect(makeShortcut(QKeySequence(Qt::Key_Escape)), &QShortcut::activated,
            this, [this]() { if (_isFullscreen) toggleFullscreenMode(); });
}

void VideoWallWindow::setupShortcutsLinux()
{
    // Linux: menu bar is hidden in fullscreen; use ApplicationShortcut for window-mode keys.
    auto makeShortcut = [this](QKeySequence key) {
        auto* s = new QShortcut(key, this);
        s->setContext(Qt::ApplicationShortcut);
        return s;
    };

    connect(makeShortcut(QKeySequence(Qt::CTRL | Qt::Key_N)), &QShortcut::activated,
            this, &VideoWallWindow::addEmulatorTile);
    connect(makeShortcut(QKeySequence(Qt::CTRL | Qt::Key_R)), &QShortcut::activated,
            this, &VideoWallWindow::handleVideoRecordingRequested);
    connect(makeShortcut(QKeySequence(Qt::CTRL | Qt::Key_Backspace)), &QShortcut::activated,
            this, &VideoWallWindow::removeLastTile);
    connect(makeShortcut(QKeySequence(Qt::Key_F10)), &QShortcut::activated,
            this, &VideoWallWindow::toggleFramelessMode);
    connect(makeShortcut(QKeySequence(Qt::CTRL | Qt::Key_F)), &QShortcut::activated,
            this, &VideoWallWindow::toggleFullscreenMode);
    connect(makeShortcut(QKeySequence(Qt::Key_Escape)), &QShortcut::activated,
            this, [this]() { if (_isFullscreen) toggleFullscreenMode(); });
}

void VideoWallWindow::handleVideoRecordingRequested()
{
    if (_recordingWidget)
    {
        _recordingWidget->close();
        return;
    }

    VideowallRecordingWidget* widget = new VideowallRecordingWidget(nullptr);
    widget->setAttribute(Qt::WA_DeleteOnClose);
    widget->setWindowFlags(Qt::Tool | Qt::Window);
    widget->show();
    widget->raise();
    widget->activateWindow();
    _recordingWidget = widget;
}

void VideoWallWindow::createDefaultPresets()
{
    // Presets are configuration snapshots that can be recalled
    // For now, presets are planned for future implementation
    // They would store: tile count, layout, loaded snapshots per tile
    qDebug() << "Default presets not yet implemented";
}

void VideoWallWindow::addEmulatorTile()
{
    if (_singleSyncMode && !_tileGrid->tiles().empty() && _tileGrid->tiles().front()->emulator())
    {
        auto primaryEmulator = _tileGrid->tiles().front()->emulator();
        EmulatorTile* tile = new EmulatorTile(primaryEmulator, this);
        tile->setSynchronousMode(true); // Ensure new tile doesn't run its own refresh loops
        connect(tile, &EmulatorTile::tileClicked, this, &VideoWallWindow::onTileClicked);
        _tileGrid->addTile(tile);
        return;
    }

    std::shared_ptr<Emulator> emulator = _emulatorManager->CreateEmulator("Pentagon", LoggerLevel::LogError);

    if (emulator)
    {
        emulator->DebugOff();

        // Disable sound for all videowall instances (saves ~17% CPU)
        // Sound can be enabled later for a focused/active tile
        auto* featureManager = emulator->GetFeatureManager();
        if (featureManager)
        {
            featureManager->setFeature(Features::kSoundGeneration, false);
            featureManager->setFeature(Features::kSoundHQ, false);

            // Enable screen HQ by default for better quality
            featureManager->setFeature(Features::kScreenHQ, _screenHQEnabled);
        }

        emulator->StartAsync();

        EmulatorTile* tile = new EmulatorTile(emulator, this);

        // Connect tile click signal for audio binding (only on user click, not Qt auto-focus)
        connect(tile, &EmulatorTile::tileClicked, this, &VideoWallWindow::onTileClicked);

        _tileGrid->addTile(tile);
    }
}

void VideoWallWindow::removeEmulatorTile(int index)
{
    const auto& tiles = _tileGrid->tiles();
    if (index < 0 || index >= static_cast<int>(tiles.size()))
    {
        qDebug() << "Invalid tile index:" << index;
        return;
    }

    EmulatorTile* tile = tiles[index];
    std::shared_ptr<Emulator> emulator = tile->emulator();

    // Unbind audio if removing the audio-bound tile
    if (tile == _audioBoundTile)
    {
        unbindAudioFromTile();
    }

    // Remove from grid
    _tileGrid->removeTile(tile);

    // Destroy emulator
    if (emulator)
    {
        _emulatorManager->RemoveEmulator(emulator->GetUUID());
    }

    // Rebind audio to another tile if available
    const auto& remainingTiles = _tileGrid->tiles();
    if (!_audioBoundTile && !remainingTiles.empty())
    {
        bindAudioToTile(remainingTiles.front());
    }

    qDebug() << "Removed tile at index" << index;
}

void VideoWallWindow::removeLastTile()
{
    const auto& tiles = _tileGrid->tiles();
    if (tiles.empty())
    {
        qDebug() << "No tiles to remove";
        return;
    }

    // Get last tile
    EmulatorTile* lastTile = tiles.back();
    std::shared_ptr<Emulator> emulator = lastTile->emulator();

    // Unbind audio if removing the audio-bound tile
    if (lastTile == _audioBoundTile)
    {
        unbindAudioFromTile();
    }

    // Remove from grid
    _tileGrid->removeTile(lastTile);

    // Destroy emulator
    _emulatorManager->RemoveEmulator(emulator->GetUUID());

    // Rebind audio to another tile if available
    const auto& remainingTiles = _tileGrid->tiles();
    if (!_audioBoundTile && !remainingTiles.empty())
    {
        bindAudioToTile(remainingTiles.front());
    }

    qDebug() << "Removed last tile:" << QString::fromStdString(emulator->GetUUID());
}

void VideoWallWindow::clearAllTiles()
{
    _tileGrid->clearAllTiles();
}

void VideoWallWindow::loadPreset(const QString& presetName)
{
    // Presets would load a saved configuration from disk
    // For now, this is planned for future implementation
    qDebug() << "loadPreset not yet implemented:" << presetName;
}

void VideoWallWindow::savePreset(const QString& presetName)
{
    // Presets would save current configuration to disk
    // For now, this is planned for future implementation
    qDebug() << "savePreset not yet implemented:" << presetName;
}

void VideoWallWindow::keyPressEvent(QKeyEvent* event)
{
    QMainWindow::keyPressEvent(event);
}

void VideoWallWindow::keyReleaseEvent(QKeyEvent* event)
{
    // Key events routed to focused tile via EmulatorTile::keyReleaseEvent
    // The tile with focus receives events directly from Qt
    QMainWindow::keyReleaseEvent(event);
}

void VideoWallWindow::mouseMoveEvent(QMouseEvent* event)
{
    // In fullscreen mode, show menu when mouse is near top of screen
    if (_isFullscreen && event->position().y() < MENU_TRIGGER_ZONE_HEIGHT)
    {
        menuBar()->show();

        // Reset auto-hide timer
        if (!_menuAutoHideTimer)
        {
            _menuAutoHideTimer = new QTimer(this);
            _menuAutoHideTimer->setSingleShot(true);
            connect(_menuAutoHideTimer, &QTimer::timeout, this, [this]() {
                // Only hide if still in fullscreen and mouse is not over menu
                if (_isFullscreen && !menuBar()->underMouse())
                {
                    menuBar()->hide();
                }
            });
        }
        _menuAutoHideTimer->stop();  // Reset timer while mouse is at top
    }
    else if (_isFullscreen && menuBar()->isVisible() && !menuBar()->underMouse())
    {
        // Mouse moved away from top - start auto-hide timer
        if (_menuAutoHideTimer)
        {
            _menuAutoHideTimer->start(MENU_AUTO_HIDE_DELAY_MS);
        }
    }

    QMainWindow::mouseMoveEvent(event);
}

void VideoWallWindow::closeEvent(QCloseEvent* event)
{
    // If closing while in fullscreen, exit fullscreen first to restore native OS window state
    if (_isFullscreen)
    {
        toggleFullscreenMode();
    }

    // Pre-stop and clear all emulator tiles before window teardown to prevent thread deadlocks
    if (_tileGrid)
    {
        _tileGrid->clearAllTiles();
    }

    QMainWindow::closeEvent(event);
}

bool VideoWallWindow::eventFilter(QObject* watched, QEvent* event)
{
    // Catch mouse moves from child widgets (tiles) to trigger menu show/hide
    if (event->type() == QEvent::MouseMove && _isFullscreen)
    {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        QPoint globalPos = mouseEvent->globalPosition().toPoint();
        QPoint localPos = mapFromGlobal(globalPos);

        if (localPos.y() < MENU_TRIGGER_ZONE_HEIGHT)
        {
            menuBar()->show();
            if (_menuAutoHideTimer)
            {
                _menuAutoHideTimer->stop();
            }
        }
        else if (menuBar()->isVisible() && !menuBar()->underMouse())
        {
            if (_menuAutoHideTimer && !_menuAutoHideTimer->isActive())
            {
                _menuAutoHideTimer->start(MENU_AUTO_HIDE_DELAY_MS);
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void VideoWallWindow::toggleFramelessMode()
{
    if (_windowMode == WindowMode::Frameless)
    {
        // Restore previous windowed state
        _windowMode = WindowMode::Windowed;
        // Exiting frameless - restore to windowed
        _windowMode = WindowMode::Windowed;

        // Restore window flags and frame
        setWindowFlags(Qt::Window);

        // Restore geometry BEFORE show
        if (_savedGeometry.isValid())
        {
            qDebug() << "Restoring geometry from frameless:" << _savedGeometry;
            setGeometry(_savedGeometry);

            // LOCK the geometry - prevent TileGrid from resizing window
            setFixedSize(_savedGeometry.size());
        }
        show();

        // Calculate optimal layout for RESTORED window size and apply it
        QTimer::singleShot(100, this, [this]() {
            // Unlock geometry first
            setMinimumSize(0, 0);
            setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

            // Apply auto-layout for restored window size
            calculateAndApplyOptimalLayout(_savedGeometry.size());
            qDebug() << "Frameless → windowed: auto-layout applied for" << _savedGeometry.size();
        });
    }
    else
    {
        // Entering frameless - save current geometry FIRST
        qDebug() << "BEFORE entering frameless:";
        qDebug() << "  Current windowState():" << windowState();
        qDebug() << "  geometry():" << geometry();
        qDebug() << "  SAVED _savedGeometry:" << geometry();

        _savedGeometry = geometry();

        // Enter frameless mode
        _windowMode = WindowMode::Frameless;
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

        QScreen* screen = window()->screen();
        showNormal();

        // Calculate layout after window state change
        QTimer::singleShot(100, this, [this, screen]() {
            calculateAndApplyOptimalLayout(screen->size());
            qDebug() << "Windowed → frameless:" << _tileGrid->tiles().size();
        });
    }
}

void VideoWallWindow::toggleFullscreenMode()
{
    // Platform-specific fullscreen handling (from unreal-qt)
#ifdef Q_OS_WIN
    toggleFullscreenWindows();
#elif defined(Q_OS_MAC)
    toggleFullscreenMacOS();
#elif defined(Q_OS_LINUX)
    toggleFullscreenLinux();
#endif
}

void VideoWallWindow::toggleFullscreenMacOS()
{
    if (windowState() & Qt::WindowFullScreen)
    {
        // Exiting fullscreen
        _windowMode = WindowMode::Windowed;
        _isFullscreen = false;

        // Note: Sound stays disabled - user explicitly disabled it for performance

        // Prevent horizontal transition from fullscreen to desktop (macOS)
        setWindowFlags(Qt::Window);

        // Restore geometry BEFORE show
        if (_savedGeometry.isValid())
        {
            qDebug() << "Restoring geometry:" << _savedGeometry;
            setGeometry(_savedGeometry);

            // LOCK the geometry - prevent TileGrid from resizing window
            setFixedSize(_savedGeometry.size());
        }
        else
        {
            qDebug() << "WARNING: _savedGeometry is invalid!";
        }
        showNormal();

        // Smart resize after window state change
        QTimer::singleShot(100, this, [this]() {
            // Unlock geometry first
            setMinimumSize(0, 0);
            setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

            // Intelligently resize grid (removes only excess tiles)
            resizeGridIntelligently(_savedGeometry.size());
            qDebug() << "Fullscreen → windowed: resized to" << _savedGeometry.size();
        });
    }
    else
    {
        // Entering fullscreen - save current geometry FIRST
        qDebug() << "BEFORE entering fullscreen:";
        qDebug() << "  Current windowState():" << windowState();
        qDebug() << "  geometry():" << geometry();
        qDebug() << "  frameGeometry():" << frameGeometry();
        qDebug() << "  screen():" << window()->screen()->name();

        _savedGeometry = geometry();
        qDebug() << "  SAVED _savedGeometry:" << _savedGeometry;

        _savedEmulatorIds.clear();
        for (const auto* tile : _tileGrid->tiles())
        {
            _savedEmulatorIds.push_back(tile->emulator()->GetUUID());
        }

        _windowMode = WindowMode::Fullscreen;
        _isFullscreen = true;

        // Note: Sound already disabled at emulator creation in addEmulatorTile()

        QScreen* screen = window()->screen();
        showFullScreen();

        // Smart resize after fullscreen transition
        QTimer::singleShot(100, this, [this, screen]() {
            resizeGridIntelligently(screen->size());
            qDebug() << "Windowed → fullscreen:" << _tileGrid->tiles().size() << "tiles";
        });
    }
}

void VideoWallWindow::toggleFullscreenWindows()
{
    if (windowState() & Qt::WindowFullScreen)
    {
        // Exiting fullscreen
        _windowMode = WindowMode::Windowed;
        _isFullscreen = false;

        // Note: Sound stays disabled - user explicitly disabled it for performance

        // Restore window flags (remove frameless)
        setWindowFlags(windowFlags() & ~Qt::FramelessWindowHint);

        // Show menu bar again
        menuBar()->show();

        // Show normal first
        showNormal();

        // Restore geometry
        if (_savedGeometry.isValid())
        {
            setGeometry(_savedGeometry);
        }

        // Stop any in-progress async batch creation before restoring saved state
        if (_batchTimer && _batchTimer->isActive())
        {
            _batchTimer->stop();
            _pendingEmulators = 0;
        }

        // Restore emulators after window state change
        QTimer::singleShot(100, this, [this]() {
            restoreSavedEmulators();
            qDebug() << "Fullscreen → windowed (restored" << _savedEmulatorIds.size() << "tiles)";
        });
    }
    else
    {
        // Entering fullscreen
        _savedGeometry = geometry();
        _savedEmulatorIds.clear();
        for (const auto* tile : _tileGrid->tiles())
        {
            _savedEmulatorIds.push_back(tile->emulator()->GetUUID());
        }

        _windowMode = WindowMode::Fullscreen;
        _isFullscreen = true;

        // Note: Sound already disabled at emulator creation in addEmulatorTile()

        // Add frameless hint for Windows fullscreen
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        setWindowState(Qt::WindowNoState);

        // Hide menu bar for cleaner fullscreen
        menuBar()->hide();

        QScreen* screen = window()->screen();
        showFullScreen();

        // Calculate layout after fullscreen transition
        QTimer::singleShot(100, this, [this, screen]() {
            calculateAndApplyOptimalLayout(screen->size());
            qDebug() << "Windowed → fullscreen:" << _tileGrid->tiles().size() << "tiles";
        });
    }
}

void VideoWallWindow::toggleFullscreenLinux()
{
    if (windowState() & Qt::WindowFullScreen)
    {
        // Exiting fullscreen
        _windowMode = WindowMode::Windowed;
        _isFullscreen = false;

        // Tell TileGrid we're exiting fullscreen so it can use size constraints again
        // But first clear the constraints so window can resize freely
        _tileGrid->setFullscreenMode(true);  // Keep constraints disabled temporarily
        _tileGrid->setMinimumSize(0, 0);
        _tileGrid->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

        // Note: Sound stays disabled - user explicitly disabled it for performance

        // Show the menu bar again
        menuBar()->show();

        showNormal();

        // Restore geometry and tiles after window state change is complete
        QRect savedGeom = _savedGeometry;  // Copy for lambda capture
        QTimer::singleShot(100, this, [this, savedGeom]() {
            // Clear size constraints again to allow resize
            _tileGrid->setMinimumSize(0, 0);
            _tileGrid->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

            if (savedGeom.isValid())
            {
                qDebug() << "Restoring geometry to:" << savedGeom;
                // Use resize + move instead of setGeometry for more reliable restoration
                resize(savedGeom.size());
                move(savedGeom.topLeft());
            }

            // Resize grid to fit window - keeps tiles that fit, disposes the rest
            resizeGridIntelligently(size());

            // Re-enable TileGrid size constraints now that we're done resizing
            _tileGrid->setFullscreenMode(false);

            qDebug() << "Fullscreen → windowed (" << _tileGrid->tiles().size() << "tiles)";
        });
    }
    else
    {
        // Entering fullscreen
        _savedGeometry = geometry();
        qDebug() << "Saved geometry before fullscreen:" << _savedGeometry;
        _savedEmulatorIds.clear();
        for (const auto* tile : _tileGrid->tiles())
        {
            _savedEmulatorIds.push_back(tile->emulator()->GetUUID());
        }

        _windowMode = WindowMode::Fullscreen;
        _isFullscreen = true;

        // Tell TileGrid we're in fullscreen so it skips setMinimumSize() calls
        _tileGrid->setFullscreenMode(true);

        // Note: Sound already disabled at emulator creation in addEmulatorTile()

        // CRITICAL: Clear size constraints BEFORE entering fullscreen
        // TileGrid's setMinimumSize() can prevent fullscreen on Linux window managers
        _tileGrid->setMinimumSize(0, 0);
        _tileGrid->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

        QScreen* screen = window()->screen();

        // Hide menu bar for cleaner fullscreen
        menuBar()->hide();

        showFullScreen();

        // Calculate layout after fullscreen transition is complete
        QTimer::singleShot(200, this, [this, screen]() {
            if (windowState() & Qt::WindowFullScreen)
            {
                calculateAndApplyOptimalLayout(screen->size());
                qDebug() << "Windowed → fullscreen:" << _tileGrid->tiles().size() << "tiles";
            }
            else
            {
                qDebug() << "Fullscreen transition failed - not in fullscreen state";
            }
        });
    }
}

void VideoWallWindow::calculateAndApplyOptimalLayout(QSize screenSize)
{
    clearAllTiles();

    // Calculate grid dynamically from screen size and configured tile size
    auto layout = TileLayoutManager::getFullscreenLayout(screenSize.width(), screenSize.height());
    int tilesWide = layout.cols;
    int tilesHigh = layout.rows;
    int total = layout.totalTiles;

    qDebug() << "Grid (dynamic):" << tilesWide << "×" << tilesHigh << "=" << total << "tiles"
             << "(" << TILE_WIDTH << "x" << TILE_HEIGHT << "px each) for" << screenSize;

    // Set explicit grid dimensions so TileGrid uses screen-based layout
    _tileGrid->setGridDimensions(tilesWide, tilesHigh);

    createEmulatorsAsync(total);
}

void VideoWallWindow::createEmulatorsAsync(int total)
{
    _pendingEmulators = total;

    if (!_batchTimer)
    {
        _batchTimer = new QTimer(this);
        connect(_batchTimer, &QTimer::timeout, this, &VideoWallWindow::createNextBatch);
    }

    qDebug() << "Starting async creation of" << total << "emulators in batches of" << BATCH_SIZE;
    _batchTimer->start(BATCH_DELAY_MS);
}

void VideoWallWindow::createNextBatch()
{
    if (_pendingEmulators <= 0)
    {
        _batchTimer->stop();
        qDebug() << "Async creation complete:" << _tileGrid->tiles().size() << "emulators created";
        return;
    }

    int batchSize = std::min(BATCH_SIZE, _pendingEmulators);
    qDebug() << "Creating batch of" << batchSize << "emulators (" << _pendingEmulators << "remaining)";

    for (int i = 0; i < batchSize; i++)
    {
        addEmulatorTile();
        _pendingEmulators--;
    }
}

void VideoWallWindow::resizeGridIntelligently(QSize screenSize)
{
    // Calculate grid dynamically from screen size and configured tile size
    auto layout = TileLayoutManager::getFullscreenLayout(screenSize.width(), screenSize.height());
    int tilesWide = layout.cols;
    int tilesHigh = layout.rows;
    int targetTotal = layout.totalTiles;
    int currentTotal = _tileGrid->tiles().size();

    qDebug() << "Smart resize: current" << currentTotal << "tiles, target" << targetTotal << "tiles for" << screenSize;

    // Set grid dimensions for proper layout
    _tileGrid->setGridDimensions(tilesWide, tilesHigh);

    // In single sync mode: do NOT touch the single emulator instance or unbind audio!
    // Just adjust the number of visual tile widgets matching the new layout size.
    if (_singleSyncMode)
    {
        if (targetTotal > currentTotal)
        {
            int tilesToAdd = targetTotal - currentTotal;
            for (int i = 0; i < tilesToAdd; i++)
            {
                addEmulatorTile();
            }
        }
        else if (targetTotal < currentTotal)
        {
            int tilesToRemove = currentTotal - targetTotal;
            for (int i = 0; i < tilesToRemove; i++)
            {
                const auto& tiles = _tileGrid->tiles();
                if (!tiles.empty())
                {
                    EmulatorTile* tile = tiles.back();
                    // Clear tile's emulator pointer so removeTile doesn't stop primaryEmulator
                    tile->setEmulator(nullptr);
                    _tileGrid->removeTile(tile, true);
                }
            }
            _tileGrid->updateLayout();
        }
        return;
    }

    if (targetTotal > currentTotal)
    {
        // Need MORE tiles - add them
        int tilesToAdd = targetTotal - currentTotal;
        qDebug() << "Adding" << tilesToAdd << "tiles to reach" << targetTotal;

        for (int i = 0; i < tilesToAdd; i++)
        {
            addEmulatorTile();
        }
    }
    else if (targetTotal < currentTotal)
    {
        // Need FEWER tiles - remove excess from the end
        int tilesToRemove = currentTotal - targetTotal;
        qDebug() << "Removing" << tilesToRemove << "excess tiles";

        auto tiles = _tileGrid->tiles();  // Get copy of vector

        // Pre-stop all excess emulator threads before releasing any.
        // On Windows, Release() of one emulator while another's thread is still
        // running can cause an access violation inside the running thread.
        for (int i = 0; i < tilesToRemove; i++)
        {
            int idx = static_cast<int>(tiles.size()) - 1 - i;
            if (idx >= 0)
            {
                EmulatorTile* tile = tiles[idx];
                if (tile && tile->emulator())
                {
                    _emulatorManager->StopEmulator(tile->emulator()->GetUUID());
                }
            }
        }

        for (int i = 0; i < tilesToRemove; i++)
        {
            // Remove from the end (newest tiles first)
            int lastIndex = tiles.size() - 1;
            if (lastIndex >= 0)
            {
                EmulatorTile* tile = tiles[lastIndex];
                if (tile)
                {
                    // If removing the audio-bound tile, unbind audio first to prevent
                    // dangling pointer and fix audio sync (empty ring buffer causes
                    // NC_AUDIO_BUFFER_HALF_FULL spam, making remaining emulators run too fast)
                    if (tile == _audioBoundTile)
                    {
                        unbindAudioFromTile();
                    }

                    // Get emulator ID before clearing the reference
                    std::string emulatorId;
                    if (tile->emulator())
                    {
                        emulatorId = tile->emulator()->GetUUID();
                    }

                    // CRITICAL: Clear tile's shared_ptr reference FIRST
                    tile->prepareForDeletion();

                    // Destroy emulator via manager (now safe - tile no longer holds reference)
                    if (!emulatorId.empty())
                    {
                        _emulatorManager->RemoveEmulator(emulatorId);
                    }

                    // Remove from grid (this deletes the tile)
                    _tileGrid->removeTile(tile, true);  // skipLayout during batch

                    // Update our copy
                    tiles = _tileGrid->tiles();

                    qDebug() << "Removed excess tile:" << QString::fromStdString(emulatorId);
                }
            }
        }

        // If audio was unbound (removed tile was audio-bound), rebind to first remaining tile
        // to restore proper audio pacing for all emulators
        if (!_audioBoundTile && !tiles.empty())
        {
            bindAudioToTile(tiles.front());
            qDebug() << "Rebound audio to first remaining tile after removal";
        }
    }
    else
    {
        // Same number of tiles - just update layout
        qDebug() << "Tile count unchanged, updating layout only";
        _tileGrid->updateLayout();
    }
}

void VideoWallWindow::restoreSavedEmulators()
{
    // If there were no saved emulators (went to fullscreen from empty window),
    // resize the grid to fit the restored window instead of removing all tiles
    if (_savedEmulatorIds.empty())
    {
        resizeGridIntelligently(size());
        qDebug() << "No saved emulators - resized grid to fit window:" << _tileGrid->tiles().size() << "tiles";
        return;
    }

    // Build set of saved UUIDs for fast lookup
    std::unordered_set<std::string> savedIds(_savedEmulatorIds.begin(), _savedEmulatorIds.end());

    // Get current tiles
    auto currentTiles = _tileGrid->tiles();

    // Find tiles to remove (those NOT in saved set)
    std::vector<EmulatorTile*> tilesToRemove;
    for (auto* tile : currentTiles)
    {
        if (!tile || !tile->emulator())
            continue;
        std::string uuid = tile->emulator()->GetUUID();
        if (savedIds.find(uuid) == savedIds.end())
        {
            tilesToRemove.push_back(tile);
        }
    }

    // Pre-stop all excess emulator threads before releasing any.
    // On Windows, Release() of one emulator while another's thread is still
    // running can cause an access violation inside the running thread.
    for (EmulatorTile* tile : tilesToRemove)
    {
        if (tile && tile->emulator())
        {
            _emulatorManager->StopEmulator(tile->emulator()->GetUUID());
        }
    }

    // Remove excessive tiles in reverse order (newest first)
    for (auto it = tilesToRemove.rbegin(); it != tilesToRemove.rend(); ++it)
    {
        EmulatorTile* tile = *it;
        if (!tile)
            continue;

        // Unbind audio if removing the audio-bound tile
        if (tile == _audioBoundTile)
        {
            unbindAudioFromTile();
        }

        // Get emulator ID before clearing the reference
        std::string uuid;
        if (tile->emulator())
        {
            uuid = tile->emulator()->GetUUID();
        }

        qDebug() << "Removing excessive emulator:" << QString::fromStdString(uuid);

        // CRITICAL: Clear tile's shared_ptr reference FIRST
        tile->prepareForDeletion();

        // Destroy emulator via manager (now safe - tile no longer holds reference)
        if (!uuid.empty())
        {
            _emulatorManager->RemoveEmulator(uuid);
        }

        // Remove from grid (this schedules tile for deletion via deleteLater)
        _tileGrid->removeTile(tile, true);  // skipLayout during batch
        // NOTE: Do NOT delete tile here - removeTile already calls deleteLater()
    }

    // If audio was unbound, rebind to first remaining tile
    const auto& remainingTiles = _tileGrid->tiles();
    if (!_audioBoundTile && !remainingTiles.empty())
    {
        bindAudioToTile(remainingTiles.front());
        qDebug() << "Rebound audio to first remaining tile after restore";
    }

    qDebug() << "Restored" << _savedEmulatorIds.size() << "emulators, removed" << tilesToRemove.size() << "excessive";
}

void VideoWallWindow::setSoundForAllTiles(bool enabled)
{
    // Set sound feature for ALL emulator instances in the grid
    // This is called when entering/exiting fullscreen or bulk toggling
    const auto& tiles = _tileGrid->tiles();
    int successCount = 0;

    for (auto* tile : tiles)
    {
        if (tile && tile->emulator())
        {
            auto* featureManager = tile->emulator()->GetFeatureManager();
            if (featureManager)
            {
                featureManager->setFeature(Features::kSoundGeneration, enabled);
                featureManager->setFeature(Features::kSoundHQ, enabled);
                successCount++;
            }
        }
    }

    qDebug() << "Sound and SoundHQ" << (enabled ? "enabled" : "disabled") << "for" << successCount << "/" << tiles.size()
             << "tiles";
}

void VideoWallWindow::toggleScreenHQForAllTiles()
{
    // Togglescreen HQ state
    _screenHQEnabled = !_screenHQEnabled;

    // Apply to all existing tiles
    const auto& tiles = _tileGrid->tiles();
    int successCount = 0;

    for (auto* tile : tiles)
    {
        if (tile && tile->emulator())
        {
            auto* featureManager = tile->emulator()->GetFeatureManager();
            if (featureManager)
            {
                featureManager->setFeature(Features::kScreenHQ, _screenHQEnabled);
                successCount++;
            }
        }
    }

    qDebug() << "Screen HQ" << (_screenHQEnabled ? "enabled" : "disabled") << "for" << successCount << "/"
             << tiles.size() << "tiles";
}

void VideoWallWindow::bindAudioToTile(EmulatorTile* tile)
{
    if (!_soundManager || !tile || !tile->emulator())
    {
        qWarning() << "VideoWallWindow::bindAudioToTile - Invalid parameters";
        return;
    }

    auto emulator = tile->emulator();

    // Unbind from previous tile if different
    if (_audioBoundTile && _audioBoundTile != tile && _audioBoundTile->emulator())
    {
        auto prevEmulator = _audioBoundTile->emulator();

        // Clear audio callback from previous emulator
        prevEmulator->ClearAudioCallback();

        // Disable sound generation and SoundHQ for previous tile on deselection (saves CPU)
        if (auto* fm = prevEmulator->GetFeatureManager())
        {
            fm->setFeature(Features::kSoundGeneration, false);
            fm->setFeature(Features::kSoundHQ, false);
        }

        qDebug() << "Audio unbound from tile:" << QString::fromStdString(prevEmulator->GetUUID());
    }

    // Enable sound generation AND SoundHQ for selected active tile
    if (auto* fm = emulator->GetFeatureManager())
    {
        fm->setFeature(Features::kSoundGeneration, true);
        fm->setFeature(Features::kSoundHQ, true);
    }

    // Bind audio callback to new emulator
    emulator->SetAudioCallback(_soundManager, &AppSoundManager::audioCallback, _soundManager->occupancyCell());
    emulator->SetAudioDeviceSampleRate(_soundManager->deviceSampleRate());
    _soundManager->setActiveContext(emulator->GetContext());

    _audioBoundTile = tile;
    
    // In non-singlesync mode, synchronize all rendering on this active emulator
    if (!_singleSyncMode)
    {
        _tileGrid->setSyncEmulatorId(emulator->GetUUID().toString());
    }
    
    qDebug() << "Audio bound to tile (Sound & SoundHQ enabled):" << QString::fromStdString(emulator->GetUUID().toString());
}

void VideoWallWindow::onTileClicked(EmulatorTile* tile)
{
    if (_singleSyncMode) return;

    // Toggle behavior: if clicking already-bound tile, unbind (mute / deselect)
    if (_audioBoundTile == tile)
    {
        unbindAudioFromTile();
        // Clear Qt focus so tile loses visual highlight (blue border)
        tile->clearFocus();
    }
    else
    {
        // Clear focus from previous tile if any
        if (_audioBoundTile)
        {
            _audioBoundTile->clearFocus();
        }
        bindAudioToTile(tile);
        // Set focus to show blue border on selected tile
        tile->setFocus();
    }
}

void VideoWallWindow::unbindAudioFromTile()
{
    // Clear pointer FIRST to prevent infinite recursion on secondary events
    EmulatorTile* tile = _audioBoundTile;
    _audioBoundTile = nullptr;

    if (_soundManager)
    {
        _soundManager->setActiveContext(nullptr);
    }

    if (!tile || !tile->emulator())
    {
        return;
    }

    auto emulator = tile->emulator();
    if (emulator)
    {
        // Clear audio callback
        emulator->ClearAudioCallback();

        // Disable sound generation and SoundHQ on deselection (saves CPU)
        if (auto* fm = emulator->GetFeatureManager())
        {
            fm->setFeature(Features::kSoundGeneration, false);
            fm->setFeature(Features::kSoundHQ, false);
        }

        qDebug() << "Audio unbound from tile (Sound & SoundHQ disabled):" << QString::fromStdString(emulator->GetUUID().toString());
    }

    // In non-singlesync mode, if we unbind, we lose the sync source.
    // We could fallback to the first tile if needed, but for now we'll just clear it.
    if (!_singleSyncMode)
    {
        _tileGrid->setSyncEmulatorId("");
    }
}
void VideoWallWindow::handleSingleSyncModeMessage(MessagePayload* payload)
{
    auto* syncPayload = dynamic_cast<VideowallSyncModePayload*>(payload);
    if (syncPayload)
    {
        // Use QMetaObject::invokeMethod to ensure this runs on the UI thread
        QMetaObject::invokeMethod(this, [this, enable = syncPayload->_enable, id = syncPayload->_emulatorId.toString()]() {
            setSingleEmulatorSyncMode(enable, id);
        }, Qt::QueuedConnection);
    }
}

void VideoWallWindow::setSingleEmulatorSyncMode(bool enable, const std::string& emulatorId)
{
    if (!_tileGrid || _tileGrid->tiles().empty()) return;
    
    _singleSyncMode = enable;

    if (enable)
    {
        // ALWAYS use tile (0, 0) as the single primary tile & emulator instance
        EmulatorTile* primaryTile = _tileGrid->tiles().front();
        if (!primaryTile || !primaryTile->emulator())
        {
            qWarning() << "VideoWallWindow: Cannot enable single sync mode, no valid emulator at tile (0,0)";
            _singleSyncMode = false;
            return;
        }

        auto primaryEmulator = primaryTile->emulator();
        std::string primaryId = primaryEmulator->GetUUID().toString();

        // Dispose all OTHER emulators in EmulatorManager (free CPU & memory)
        EmulatorManager* manager = EmulatorManager::GetInstance();
        if (manager)
        {
            auto allIds = manager->GetEmulatorIds();
            for (const auto& id : allIds)
            {
                if (id != primaryId)
                {
                    manager->StopEmulator(id);
                    manager->RemoveEmulator(id);
                }
            }
        }

        // Point ALL 48 tiles in the grid to the single primary emulator instance
        for (EmulatorTile* tile : _tileGrid->tiles())
        {
            if (tile)
            {
                tile->setEmulator(primaryEmulator);
                tile->setPrimarySyncTile(false);
                tile->setSynchronousMode(true);
            }
        }

        // Configure tile (0, 0) as the primary sync tile
        primaryTile->setPrimarySyncTile(true);
        primaryTile->setSynchronousMode(true);

        // Ensure primary emulator is running
        primaryEmulator->Resume();

        // Configure recording & bind HQ audio to tile (0, 0)
        VideowallRecorder::instance().setSynchronousMode(true);
        bindAudioToTile(primaryTile);

        // Configure TileGrid single sync state with primary emulator ID
        _tileGrid->setSingleSyncMode(true);
        _tileGrid->setSyncEmulatorId(primaryId);
    }
    else
    {
        for (EmulatorTile* tile : _tileGrid->tiles())
        {
            if (tile)
            {
                tile->setSynchronousMode(false);
            }
        }
        _tileGrid->setSingleSyncMode(false);
        VideowallRecorder::instance().setSynchronousMode(false);
    }
}
