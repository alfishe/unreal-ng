#include "videowall/VideoWallWindow.h"

#include <emulatormanager.h>

#include <QAction>
#include <QKeyEvent>
#include <QMenuBar>

#include "videowall/EmulatorTile.h"
#include "videowall/TileGrid.h"

VideoWallWindow::VideoWallWindow(QWidget* parent) : QMainWindow(parent)
{
    // Get emulator manager singleton
    _emulatorManager = EmulatorManager::GetInstance();

    setupUI();
    createMenus();
    createDefaultPresets();

    setWindowTitle("Unreal Video Wall");
    resize(800, 600);
}

VideoWallWindow::~VideoWallWindow()
{
    clearAllTiles();
}

void VideoWallWindow::setupUI()
{
    // Create tile grid as central widget
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
}

void VideoWallWindow::createDefaultPresets()
{
    // TODO: Implement preset creation in Phase 6
}

void VideoWallWindow::addEmulatorTile()
{
    if (!_emulatorManager)
    {
        qWarning() << "EmulatorManager not available";
        return;
    }

    // Generate unique symbolic ID for this emulator
    int tileIndex = _tileGrid->tiles().size();
    QString symbolicId = QString("tile_%1").arg(tileIndex);

    // Create emulator with error logging enabled to diagnose issues
    auto emulator = _emulatorManager->CreateEmulator(symbolicId.toStdString(), LoggerLevel::LogError);

    if (!emulator)
    {
        qWarning() << "Failed to create emulator for tile" << tileIndex;
        return;
    }

    // Turn off debugging for minimal overhead
    emulator->DebugOff();

    // Create tile widget for this emulator
    EmulatorTile* tile = new EmulatorTile(emulator, this);
    _tileGrid->addTile(tile);

    // Start the emulator
    _emulatorManager->StartEmulatorAsync(emulator->GetId());

    qDebug() << "Created emulator tile" << tileIndex << "with ID:" << QString::fromStdString(emulator->GetId());
}

void VideoWallWindow::removeEmulatorTile(int index)
{
    // TODO: Implement in Phase 2
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
    // TODO: Route to focused tile (Phase 5)
    QMainWindow::keyPressEvent(event);
}

void VideoWallWindow::keyReleaseEvent(QKeyEvent* event)
{
    // TODO: Route to focused tile (Phase 5)
    QMainWindow::keyReleaseEvent(event);
}
