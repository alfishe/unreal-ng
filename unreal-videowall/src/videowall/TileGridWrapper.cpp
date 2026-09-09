#include "TileGridWrapper.h"
#include "TileGrid.h"
#include "TileGridGL.h"
#include "EmulatorTile.h"

#include <QDebug>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <emulatormanager.h>

static bool probeGPUAcceleration()
{
    QOpenGLContext testContext;
    if (!testContext.create())
        return false;

    QOffscreenSurface surface;
    surface.create();
    if (!testContext.makeCurrent(&surface))
        return false;

    testContext.doneCurrent();
    return true;
}

TileGridWrapper::TileGridWrapper(QWidget* parent)
    : TileGridWrapper(parent, probeGPUAcceleration())
{
}

TileGridWrapper::TileGridWrapper(QWidget* parent, bool useGPU)
    : QObject(parent)
{
    if (useGPU && probeGPUAcceleration())
    {
        try
        {
            _gpuGrid = new TileGridGL();
            _widget = QWidget::createWindowContainer(_gpuGrid, parent);
            _widget->setFocusPolicy(Qt::StrongFocus);
            _useGPU = true;
            qInfo() << "TileGridWrapper: Using GPU-accelerated rendering";
        }
        catch (...)
        {
            qWarning() << "TileGridWrapper: GPU grid creation failed";
            delete _gpuGrid;
            _gpuGrid = nullptr;
        }
    }

    if (!_useGPU)
    {
        _cpuGrid = new TileGrid(parent);
        _widget = _cpuGrid;
        qInfo() << "TileGridWrapper: Using software rendering";
    }

    setupConnections();
}

TileGridWrapper::~TileGridWrapper()
{
    delete _widget;
    _widget = nullptr;
    _cpuGrid = nullptr;
    _gpuGrid = nullptr;
}

bool TileGridWrapper::isGPUAvailable()
{
    return probeGPUAcceleration();
}

void TileGridWrapper::setupConnections()
{
    if (_useGPU && _gpuGrid)
    {
        connect(_gpuGrid, &TileGridGL::tileClicked, this, &TileGridWrapper::tileClicked);
        connect(_gpuGrid, &TileGridGL::fileDropped, this, &TileGridWrapper::fileDropped);
    }
    else if (_cpuGrid)
    {
        // CPU mode: EmulatorTile emits tileClicked(EmulatorTile*), we convert to index
        // This is handled in VideoWallWindow via the existing onTileClicked mechanism
    }
}

void TileGridWrapper::addEmulator(std::shared_ptr<Emulator> emulator)
{
    if (!emulator)
        return;

    if (_useGPU && _gpuGrid)
    {
        _gpuGrid->addEmulator(emulator);
    }
    else if (_cpuGrid)
    {
        EmulatorTile* tile = new EmulatorTile(emulator, _cpuGrid);
        _cpuGrid->addTile(tile);

        // Connect tile click signal for CPU mode
        connect(tile, &EmulatorTile::tileClicked, this, [this, tile](EmulatorTile*) {
            int index = 0;
            for (EmulatorTile* t : _cpuGrid->tiles())
            {
                if (t == tile)
                {
                    emit tileClicked(index);
                    return;
                }
                ++index;
            }
        });
    }
}

void TileGridWrapper::removeEmulator(const std::string& emulatorId)
{
    if (_useGPU && _gpuGrid)
    {
        _gpuGrid->removeEmulator(emulatorId);
    }
    else if (_cpuGrid)
    {
        for (EmulatorTile* tile : _cpuGrid->tiles())
        {
            if (tile && tile->emulator() && tile->emulator()->GetUUID().toString() == emulatorId)
            {
                _cpuGrid->removeTile(tile);
                return;
            }
        }
    }
}

void TileGridWrapper::removeLastEmulator()
{
    if (_useGPU && _gpuGrid)
    {
        auto ids = _gpuGrid->emulatorIds();
        if (!ids.empty())
            _gpuGrid->removeEmulator(ids.back());
    }
    else if (_cpuGrid)
    {
        auto& tiles = _cpuGrid->tiles();
        if (!tiles.empty())
            _cpuGrid->removeTile(tiles.back());
    }
}

void TileGridWrapper::clearAllEmulators()
{
    if (_useGPU && _gpuGrid)
        _gpuGrid->clearAllEmulators();
    else if (_cpuGrid)
        _cpuGrid->clearAllTiles();
}

int TileGridWrapper::emulatorCount() const
{
    if (_useGPU && _gpuGrid)
        return _gpuGrid->emulatorCount();
    else if (_cpuGrid)
        return static_cast<int>(_cpuGrid->tiles().size());
    return 0;
}

std::shared_ptr<Emulator> TileGridWrapper::emulatorAt(int index) const
{
    if (_useGPU && _gpuGrid)
    {
        return _gpuGrid->emulatorAt(index);
    }
    else if (_cpuGrid)
    {
        auto& tiles = _cpuGrid->tiles();
        if (index >= 0 && index < static_cast<int>(tiles.size()))
            return tiles[index]->emulator();
    }
    return nullptr;
}

std::vector<std::string> TileGridWrapper::emulatorIds() const
{
    if (_useGPU && _gpuGrid)
    {
        return _gpuGrid->emulatorIds();
    }
    else if (_cpuGrid)
    {
        std::vector<std::string> ids;
        for (EmulatorTile* tile : _cpuGrid->tiles())
        {
            if (tile && tile->emulator())
                ids.push_back(tile->emulator()->GetUUID().toString());
        }
        return ids;
    }
    return {};
}

void TileGridWrapper::setGridDimensions(int cols, int rows)
{
    _explicitCols = cols;
    _explicitRows = rows;

    if (_useGPU && _gpuGrid)
        _gpuGrid->setGridDimensions(cols, rows);
    else if (_cpuGrid)
        _cpuGrid->setGridDimensions(cols, rows);
}

void TileGridWrapper::setFullscreenMode(bool fullscreen)
{
    _isFullscreen = fullscreen;

    if (_useGPU && _gpuGrid)
        _gpuGrid->setFullscreenMode(fullscreen);
    else if (_cpuGrid)
        _cpuGrid->setFullscreenMode(fullscreen);
}

void TileGridWrapper::setSingleSyncMode(bool enable)
{
    _singleSyncMode = enable;

    if (_useGPU && _gpuGrid)
        _gpuGrid->setSingleSyncMode(enable);
    else if (_cpuGrid)
        _cpuGrid->setSingleSyncMode(enable);
}

void TileGridWrapper::setSyncEmulatorId(const std::string& emulatorId)
{
    _syncEmulatorId = emulatorId;

    if (_useGPU && _gpuGrid)
        _gpuGrid->setSyncEmulatorId(emulatorId);
    else if (_cpuGrid)
        _cpuGrid->setSyncEmulatorId(emulatorId);
}

void TileGridWrapper::repaintAllTiles()
{
    if (_useGPU && _gpuGrid)
        _gpuGrid->repaintAllTiles();
    else if (_cpuGrid)
        _cpuGrid->repaintAllTiles();
}

int TileGridWrapper::focusedIndex() const
{
    if (_useGPU && _gpuGrid)
        return _gpuGrid->focusedIndex();
    else if (_cpuGrid)
    {
        EmulatorTile* focused = _cpuGrid->focusedTile();
        if (!focused)
            return -1;

        int index = 0;
        for (EmulatorTile* tile : _cpuGrid->tiles())
        {
            if (tile == focused)
                return index;
            ++index;
        }
    }
    return -1;
}

void TileGridWrapper::setFocusedIndex(int index)
{
    if (_useGPU && _gpuGrid)
        _gpuGrid->setFocusedIndex(index);
    // CPU mode handles focus via Qt's focus system
}
