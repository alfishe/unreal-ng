#include "videowall/VideoWallWindow.h"

#include <emulatormanager.h>
#include <platform.h>

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QMenuBar>
#include <QScreen>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <future>
#include <unordered_set>
#include <vector>

#include "videowall/EmulatorTile.h"
#include "videowall/TileGrid.h"

VideoWallWindow::VideoWallWindow(QWidget* parent) : QMainWindow(parent)
{
    _emulatorManager = EmulatorManager::GetInstance();

    setupUI();
    createMenus();
    createDefaultPresets();

    // Debug: Log initial window geometry at startup
    QTimer::singleShot(100, this, [this]() {
        qDebug() << "=== INITIAL WINDOW GEOMETRY AT STARTUP ===";
        qDebug() << "  geometry():" << geometry();
        qDebug() << "  frameGeometry():" << frameGeometry();
        qDebug() << "  windowState():" << windowState();
        qDebug() << "  screen():" << window()->screen()->name();
        qDebug() << "==========================================";
    });

    resize(800, 600);
    setWindowTitle(tr("Unreal Speccy Video Wall"));
}

VideoWallWindow::~VideoWallWindow() {}

void VideoWallWindow::setupUI()
{
    _tileGrid = new TileGrid(this);
    setCentralWidget(_tileGrid);
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

    QAction* addTileAction = emulatorMenu->addAction(tr("&Add Tile"));
    addTileAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    connect(addTileAction, &QAction::triggered, this, &VideoWallWindow::addEmulatorTile);

    QAction* clearAllAction = emulatorMenu->addAction(tr("&Clear All"));
    connect(clearAllAction, &QAction::triggered, this, &VideoWallWindow::clearAllTiles);

    // View menu
    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));

    QAction* framelessAction = viewMenu->addAction(tr("&Frameless Mode"));
    framelessAction->setShortcut(QKeySequence(Qt::Key_F10));
    framelessAction->setCheckable(true);
    framelessAction->setChecked(false);
    connect(framelessAction, &QAction::triggered, this, &VideoWallWindow::toggleFramelessMode);

    QAction* fullscreenAction = viewMenu->addAction(tr("F&ullscreen Mode"));
    fullscreenAction->setShortcut(QKeySequence(Qt::Key_F11));
    fullscreenAction->setCheckable(true);
    fullscreenAction->setChecked(false);
    connect(fullscreenAction, &QAction::triggered, this, &VideoWallWindow::toggleFullscreenMode);
}

void VideoWallWindow::createDefaultPresets()
{
    // TODO: Implement preset creation in Phase 6
}

void VideoWallWindow::addEmulatorTile()
{
    std::shared_ptr<Emulator> emulator = _emulatorManager->CreateEmulator("Pentagon", LoggerLevel::LogError);

    if (emulator)
    {
        emulator->DebugOff();
        emulator->EnableTurboMode(false);  // Disable audio to prevent blocking
        emulator->StartAsync();

        EmulatorTile* tile = new EmulatorTile(emulator, this);
        _tileGrid->addTile(tile);
    }
}

void VideoWallWindow::removeEmulatorTile(int index)
{
    // TODO: Implement in Phase 5
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

    // Remove from grid
    _tileGrid->removeTile(lastTile);

    // Destroy emulator
    _emulatorManager->RemoveEmulator(emulator->GetUUID());

    qDebug() << "Removed last tile:" << QString::fromStdString(emulator->GetUUID());
}

void VideoWallWindow::clearAllTiles()
{
    _tileGrid->clearAllTiles();
}

void VideoWallWindow::loadPreset(const QString& presetName)
{
    // TODO: Implement in Phase 6
}

void VideoWallWindow::savePreset(const QString& presetName)
{
    // TODO: Implement in Phase 6
}

void VideoWallWindow::keyPressEvent(QKeyEvent* event)
{
    // Cmd+N: Add new emulator tile
    if (event->key() == Qt::Key_N && event->modifiers() & Qt::ControlModifier)
    {
        addEmulatorTile();
    }
    // Cmd+Backspace: Remove last tile
    else if (event->key() == Qt::Key_Backspace && event->modifiers() & Qt::ControlModifier)
    {
        removeLastTile();
    }
    // F10: Toggle frameless mode
    else if (event->key() == Qt::Key_F10)
    {
        toggleFramelessMode();
    }
    // Cmd+Shift+F: Toggle fullscreen mode
    else if (event->key() == Qt::Key_F && (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
    {
        toggleFullscreenMode();
    }
    else
    {
        QMainWindow::keyPressEvent(event);
    }
}

void VideoWallWindow::keyReleaseEvent(QKeyEvent* event)
{
    // TODO: Route to focused tile (Phase 5)
    QMainWindow::keyReleaseEvent(event);
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
            calculateAndApplyOptimalLayout(screen->availableSize());
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

        // Calculate optimal layout for RESTORED window size and apply it
        QTimer::singleShot(100, this, [this]() {
            // Unlock geometry first
            setMinimumSize(0, 0);
            setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

            // Apply auto-layout for restored window size
            calculateAndApplyOptimalLayout(_savedGeometry.size());
            qDebug() << "Fullscreen → windowed: auto-layout applied for" << _savedGeometry.size();
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

        QScreen* screen = window()->screen();
        showFullScreen();

        // Calculate layout after fullscreen transition
        QTimer::singleShot(100, this, [this, screen]() {
            calculateAndApplyOptimalLayout(screen->availableSize());
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

        // Restore window flags (remove frameless)
        setWindowFlags(windowFlags() & ~Qt::FramelessWindowHint);

        // Show normal first
        showNormal();

        // Restore geometry
        if (_savedGeometry.isValid())
        {
            setGeometry(_savedGeometry);
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

        // Add frameless hint for Windows fullscreen
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        setWindowState(Qt::WindowNoState);

        QScreen* screen = window()->screen();
        showFullScreen();

        // Calculate layout after fullscreen transition
        QTimer::singleShot(100, this, [this, screen]() {
            calculateAndApplyOptimalLayout(screen->availableSize());
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

        // Restore geometry and show
        if (_savedGeometry.isValid())
        {
            setGeometry(_savedGeometry);
        }
        showNormal();

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

        QScreen* screen = window()->screen();
        showFullScreen();

        // Calculate layout after fullscreen transition
        QTimer::singleShot(100, this, [this, screen]() {
            calculateAndApplyOptimalLayout(screen->availableSize());
            qDebug() << "Windowed → fullscreen:" << _tileGrid->tiles().size() << "tiles";
        });
    }
}

void VideoWallWindow::calculateAndApplyOptimalLayout(QSize screenSize)
{
    clearAllTiles();

    // Calculate grid using CEILING division - if ANY pixels left over, add another tile (will be clipped)
    int tilesWide = (screenSize.width() + 511) / 512;   // Ceiling division for 512px tiles
    int tilesHigh = (screenSize.height() + 383) / 384;  // Ceiling division for 384px tiles
    int total = tilesWide * tilesHigh;

    qDebug() << "Grid (2x scale, ceiling):" << tilesWide << "×" << tilesHigh << "=" << total << "emulators for"
             << screenSize;

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

void VideoWallWindow::restoreSavedEmulators()
{
    // Build set of saved UUIDs for fast lookup
    std::unordered_set<std::string> savedIds(_savedEmulatorIds.begin(), _savedEmulatorIds.end());

    // Get current tiles
    auto currentTiles = _tileGrid->tiles();

    // Find tiles to remove (those NOT in saved set)
    std::vector<EmulatorTile*> tilesToRemove;
    for (auto* tile : currentTiles)
    {
        std::string uuid = tile->emulator()->GetUUID();
        if (savedIds.find(uuid) == savedIds.end())
        {
            tilesToRemove.push_back(tile);
        }
    }

    // Remove excessive tiles in reverse order (newest first)
    for (auto it = tilesToRemove.rbegin(); it != tilesToRemove.rend(); ++it)
    {
        EmulatorTile* tile = *it;
        std::string uuid = tile->emulator()->GetUUID();

        qDebug() << "Removing excessive emulator:" << QString::fromStdString(uuid);

        // Remove from grid
        _tileGrid->removeTile(tile);

        // Destroy emulator via manager
        _emulatorManager->RemoveEmulator(uuid);

        // Delete tile widget
        delete tile;
    }

    qDebug() << "Restored" << _savedEmulatorIds.size() << "emulators, removed" << tilesToRemove.size() << "excessive";
}
