#include "videowall/TileGrid.h"

#include <emulatormanager.h>
#include <emulator/emulatorcontext.h>
#include <emulator/notifications.h>
#include <emulator/video/screen.h>
#include <3rdparty/message-center/messagecenter.h>

#include <QPainter>
#include "SIMDBlend.h"
#include <QImage>
#include <QPaintEvent>
#include <QResizeEvent>
#include <algorithm>
#include <cstring>
#include "videowall/EmulatorTile.h"
#include "videowall/TileLayoutManager.h"
#include "videowall/VideowallRecorder.h"

TileGrid::TileGrid(QWidget* parent) : QWidget(parent) {}

TileGrid::~TileGrid()
{
    unsubscribeFromNotifications();
    clearAllTiles();
}

void TileGrid::addTile(EmulatorTile* tile)
{
    if (!tile)
        return;

    _tiles.push_back(tile);
    tile->setParent(this);
    
    // Inherit CRT effect settings
    tile->setCrtPhosphorEnabled(_crtPhosphorEnabled);
    tile->setCrtScanlinesEnabled(_crtScanlinesEnabled);
    
    tile->show();

    updateLayout();
}

void TileGrid::removeTile(EmulatorTile* tile, bool skipLayout)
{
    if (!tile)
        return;

    auto it = std::find(_tiles.begin(), _tiles.end(), tile);
    if (it != _tiles.end())
    {
        _tiles.erase(it);

        if (tile->emulator())
        {
            std::string emulatorId = tile->emulator()->GetUUID().toString();
            tile->prepareForDeletion();
            
            EmulatorManager* manager = EmulatorManager::GetInstance();
            if (manager && !emulatorId.empty())
            {
                manager->StopEmulator(emulatorId);
                manager->RemoveEmulator(emulatorId);
            }
        }
        else
        {
            tile->prepareForDeletion();
        }

        tile->deleteLater();
        
        // Skip layout during batch removal to prevent crashes
        if (!skipLayout)
        {
            updateLayout();
        }
    }
}

void TileGrid::clearAllTiles()
{
    EmulatorManager* manager = EmulatorManager::GetInstance();

    // PHASE 1: Pre-stop all emulator threads before releasing any.
    // This prevents race conditions where one emulator is being released while
    // another's thread is still running and potentially accessing shared resources.
    for (EmulatorTile* tile : _tiles)
    {
        if (tile && tile->emulator())
        {
            if (manager)
            {
                manager->StopEmulator(tile->emulator()->GetUUID());
            }
        }
    }

    // PHASE 2: Release emulators and delete tiles
    for (EmulatorTile* tile : _tiles)
    {
        if (tile)
        {
            // Get emulator ID before clearing the reference
            std::string emulatorId;
            if (tile->emulator())
            {
                emulatorId = tile->emulator()->GetUUID();
            }

            // Clear tile's reference to emulator before destroying it
            tile->prepareForDeletion();

            // Destroy the emulator instance via EmulatorManager
            if (!emulatorId.empty() && manager)
            {
                manager->RemoveEmulator(emulatorId);
            }

            // Now delete the tile widget (shared_ptr already cleared)
            tile->deleteLater();
        }
    }
    _tiles.clear();
    _focusedTile = nullptr;
}

void TileGrid::updateLayout()
{
    // Prevent re-entrant calls (e.g., from resizeEvent triggered by setMinimumSize)
    if (_inUpdateLayout)
    {
        return;
    }
    _inUpdateLayout = true;

    if (_tiles.empty())
    {
        _inUpdateLayout = false;
        return;
    }

    int cols, rows;

    // Use explicit dimensions if set, otherwise calculate from tile count
    if (_explicitCols > 0 && _explicitRows > 0)
    {
        cols = _explicitCols;
        rows = _explicitRows;
    }
    else
    {
        // Fallback to automatic calculation
        TileLayoutManager::GridLayout layout = TileLayoutManager::calculateLayout(_tiles.size());
        cols = layout.cols;
        rows = layout.rows;
    }

    // Position tiles in grid using configured tile size
    int x = 0;
    int y = 0;
    int col = 0;

    for (EmulatorTile* tile : _tiles)
    {
        if (!tile)  // Safety check
        {
            continue;
        }
        tile->setFixedSize(TILE_WIDTH, TILE_HEIGHT); // Restore fixed size constraint
        tile->move(x, y);
        tile->show(); // Ensure it's visible

        // Move to next column
        col++;
        x += TILE_WIDTH;

        // If we've filled a row, move to next row
        if (col >= cols)
        {
            col = 0;
            x = 0;
            y += TILE_HEIGHT;
        }
    }

    // Resize widget to fit grid (but NOT in fullscreen mode - size constraints break fullscreen on Linux)
    if (!_isFullscreen)
    {
        int windowWidth = cols * TILE_WIDTH;
        int windowHeight = rows * TILE_HEIGHT;
        resize(windowWidth, windowHeight);
    }

    _inUpdateLayout = false;
}

void TileGrid::setSingleSyncMode(bool enable)
{
    _singleSyncMode = enable;
    
    if (_singleSyncMode)
    {
        if (width() > 0 && height() > 0 && (_compositeImage.isNull() || width() != _compositeImage.width() || height() != _compositeImage.height()))
        {
            _compositeImage = QImage(width(), height(), QImage::Format_RGBA8888);
            _compositeImage.fill(Qt::black);
        }
    }
    updateLayout();
}

void TileGrid::setSyncEmulatorId(const std::string& emulatorId)
{
    _syncEmulatorId = emulatorId;
    
    unsubscribeFromNotifications();
    if (!_syncEmulatorId.empty())
    {
        subscribeToNotifications();
    }
}

void TileGrid::setCrtPhosphorEnabled(bool enable)
{
    _crtPhosphorEnabled = enable;
    if (!enable)
    {
        _phosphorHistory.clear();
    }
    
    // Propagate to all child tiles for non-singlesync mode
    for (auto* tile : _tiles)
    {
        if (tile) tile->setCrtPhosphorEnabled(enable);
    }
}

void TileGrid::setSmartPhosphorEnabled(bool enable)
{
    _smartPhosphorEnabled = enable;
    for (auto* tile : _tiles)
    {
        if (tile) tile->setSmartPhosphorEnabled(enable);
    }
}

void TileGrid::setPhosphorDepth(int depth)
{
    _phosphorDepth = std::max(2, std::min(8, depth)); // Clamp 2 to 8
    _phosphorHistory.clear();
    
    for (auto* tile : _tiles)
    {
        if (tile) tile->setPhosphorDepth(_phosphorDepth);
    }
}

void TileGrid::setCrtScanlinesEnabled(bool enable)
{
    _crtScanlinesEnabled = enable;
    
    // Propagate to all child tiles for non-singlesync mode
    for (auto* tile : _tiles)
    {
        if (tile) tile->setCrtScanlinesEnabled(enable);
    }
}

void TileGrid::subscribeToNotifications()
{
    _videoFrameObserverId = MessageCenter::DefaultMessageCenter().AddObserver(NC_VIDEO_FRAME_REFRESH,
        [this](int id, Message* message) {
            if (message && message->obj) {
                auto* payload = dynamic_cast<EmulatorFramePayload*>(message->obj);
                if (payload && payload->_emulatorId.toString() == _syncEmulatorId) {
                    // Drop frame if UI is still rendering the previous one (prevents event queue flooding)
                    bool expected = false;
                    if (_isRepaintPending.compare_exchange_strong(expected, true)) {

                        if (_singleSyncMode) {
                            // --- Perform SIMD / Multithreaded compositing on the emulator thread ---
                            compositeSingleSyncFrame();
                        }

                        QMetaObject::invokeMethod(this, [this]() {
                            // In single sync mode, repaintAllTiles just calls this->update() to draw the composite image
                            repaintAllTiles();
                            _isRepaintPending = false;
                        }, Qt::QueuedConnection);
                    }
                }
            }
        });
}

void TileGrid::unsubscribeFromNotifications()
{
    if (_videoFrameObserverId != 0)
    {
        MessageCenter::DefaultMessageCenter().RemoveObserverById(NC_VIDEO_FRAME_REFRESH, _videoFrameObserverId);
        _videoFrameObserverId = 0;
    }
}

void TileGrid::repaintAllTiles()
{
    if (_singleSyncMode)
    {
        // Draw the composite image directly to TileGrid
        update();
    }
    else
    {
        for (EmulatorTile* tile : _tiles)
        {
            if (tile && tile->isVisible())
            {
                tile->update();
            }
        }
    }

    if (VideowallRecorder::instance().isRecording())
    {
        VideowallRecorder::instance().captureVideoFrameSync();
    }
}

void TileGrid::setGridDimensions(int cols, int rows)
{
    _explicitCols = cols;
    _explicitRows = rows;
    updateLayout();
}

void TileGrid::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateLayout();
    
    // Resize the composite image if the window size changed
    if (_singleSyncMode && (width() != _compositeImage.width() || height() != _compositeImage.height()))
    {
        _compositeImage = QImage(width(), height(), QImage::Format_RGBA8888);
        _compositeImage.fill(Qt::black);
    }
}

void TileGrid::paintEvent(QPaintEvent* event)
{
    if (_singleSyncMode && !_compositeImage.isNull())
    {
        QPainter painter(this);
        // Use fast nearest neighbor when drawing the whole composite to the screen (if scaling is needed at all)
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(rect(), _compositeImage);
    }
    else
    {
        QWidget::paintEvent(event);
    }
}

void TileGrid::compositeSingleSyncFrame()
{
    if (_tiles.empty() || _compositeImage.isNull()) return;

    auto emulator = _tiles.front()->emulator();
    if (!emulator) return;

    // Get screen for tear-free copy from latched framebuffer
    EmulatorContext* ctx = emulator->GetContext();
    Screen* screen = ctx ? ctx->pScreen : nullptr;
    if (!screen) return;

    auto& desc = screen->GetFramebufferDescriptor();
    if (desc.width == 0 || desc.height == 0) return;

    // Allocate or resize backing buffer if needed
    if (_latchedFrame.width() != static_cast<int>(desc.width) ||
        _latchedFrame.height() != static_cast<int>(desc.height))
    {
        _latchedFrame = QImage(desc.width, desc.height, QImage::Format_RGBA8888);
    }

    // Copy from the frame-end latched snapshot (tear-free)
    if (!screen->CopyPresentedFramebuffer(_latchedFrame.bits(),
                                          static_cast<size_t>(_latchedFrame.sizeInBytes())))
    {
        return;
    }

    QImage& rawImage = _latchedFrame;
    
    // 2. Extract the 256x192 active area
    QImage activeArea = rawImage.copy(48, 48, 256, 192);

    // Apply CRT Effects on the 256x192 area (faster than post-scaling)
    // Note: Applying scanlines *before* scaling gives thicker retro scanlines
    if (_crtPhosphorEnabled)
    {
        applyPhosphorBlend(activeArea);
    }
    if (_crtScanlinesEnabled)
    {
        applyScanlines(activeArea);
    }

    // 3. Scale it *once* to the target tile size
    QImage scaledTile = activeArea.scaled(TILE_WIDTH, TILE_HEIGHT, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    
    if (scaledTile.isNull()) return;

    // 4. Blit to Composite Buffer
    // Every tile shows the identical image, so build the first band of tiles once,
    // then replicate that band down the grid with full-scanline copies.
    // (Serial on purpose: the whole composite is ~14 MB/frame of memcpy — spawning
    // a thread per tile costs far more than the copies themselves.)
    int cols = (_explicitCols > 0) ? _explicitCols : ((width() + TILE_WIDTH - 1) / TILE_WIDTH);
    int rows = (_explicitRows > 0) ? _explicitRows : ((height() + TILE_HEIGHT - 1) / TILE_HEIGHT);

    int tileByteWidth = TILE_WIDTH * 4;
    int compWidth = _compositeImage.width();
    int compHeight = _compositeImage.height();
    qsizetype compStride = _compositeImage.bytesPerLine();
    uchar* compBits = _compositeImage.bits();

    // First band: replicate the scaled tile across all columns, scanline by scanline
    int bandHeight = std::min(TILE_HEIGHT, compHeight);
    for (int y = 0; y < bandHeight; ++y)
    {
        uchar* destLine = compBits + y * compStride;
        const uchar* srcLine = scaledTile.constScanLine(y);
        for (int c = 0; c < cols; ++c)
        {
            int startX = c * TILE_WIDTH;
            if (startX >= compWidth) break;
            int bytesToCopy = std::min(tileByteWidth, (compWidth - startX) * 4);
            std::memcpy(destLine + startX * 4, srcLine, bytesToCopy);
        }
    }

    // Remaining rows: copy the first band wholesale, one full scanline at a time
    for (int r = 1; r < rows; ++r)
    {
        int startY = r * TILE_HEIGHT;
        for (int y = 0; y < bandHeight; ++y)
        {
            int destY = startY + y;
            if (destY >= compHeight) return;
            std::memcpy(compBits + destY * compStride, compBits + y * compStride, compStride);
        }
    }
}

void TileGrid::applyPhosphorBlend(QImage& current)
{
    if (current.format() != QImage::Format_RGBA8888 && current.format() != QImage::Format_ARGB32) {
        current = current.convertToFormat(QImage::Format_RGBA8888);
    }

    _phosphorHistory.push_front(current.copy());
    
    if (_phosphorHistory.size() > _phosphorDepth)
    {
        _phosphorHistory.pop_back();
    }
    
    if (_phosphorHistory.size() < _phosphorDepth)
    {
        return; // Wait until buffer fills up
    }

    int weightSum = (_phosphorDepth * (_phosphorDepth + 1)) / 2;
    std::vector<int> weights(_phosphorDepth);
    for (int i = 0; i < _phosphorDepth; ++i) {
        weights[i] = _phosphorDepth - i;
    }
    
    const int lumaThreshold = 25; // ~10% of 255
    
    // Create vector of const pointers to pass to SIMD
    std::vector<const QImage*> historyPtrs(_phosphorDepth);
    for (int i = 0; i < _phosphorDepth; ++i) {
        historyPtrs[i] = &_phosphorHistory[i];
    }

    videowall::simd::applySmartPhosphorBlend(
        current,
        historyPtrs,
        _phosphorDepth,
        lumaThreshold,
        weights,
        weightSum,
        _smartPhosphorEnabled
    );
}

void TileGrid::applyScanlines(QImage& current)
{
    if (current.format() != QImage::Format_RGBA8888 && current.format() != QImage::Format_ARGB32) {
        current = current.convertToFormat(QImage::Format_RGBA8888);
    }

    int width = current.width();
    int height = current.height();
    
    for (int y = 0; y < height; y++)
    {
        if (y % 2 != 0) // Darken every odd line
        {
            QRgb* curLine = reinterpret_cast<QRgb*>(current.scanLine(y));
            for (int x = 0; x < width; x++)
            {
                QRgb c = curLine[x];
                int r = (qRed(c) * 85) / 100;
                int g = (qGreen(c) * 85) / 100;
                int b = (qBlue(c) * 85) / 100;
                curLine[x] = qRgb(r, g, b);
            }
        }
    }
}
