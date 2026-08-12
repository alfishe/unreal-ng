#include "videowall/EmulatorTile.h"
#include "videowall/TileGrid.h"

#include <QDragLeaveEvent>
#include <QMimeData>
#include "videowall/VideowallRecorder.h"
#include <QPainter>
#include <QResizeEvent>
#include <QMouseEvent>
#include "SIMDBlend.h"
#include <QTimer>
#include <emulator/notifications.h>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator.h"
#include "emulator/io/keyboard/keyboard.h"
#include "keyboard/keyboardmanager.h"

EmulatorTile::EmulatorTile(std::shared_ptr<Emulator> emulator, QWidget* parent) : QWidget(parent), _emulator(emulator)
{
    setFixedSize(TILE_WIDTH, TILE_HEIGHT);
    // Cache emulator UUID
    _emulatorId = _emulator ? _emulator->GetUUID().toString() : "";
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);

    if (_emulator)
    {
    }
}

EmulatorTile::~EmulatorTile()
{
    // Stop all timers to prevent callbacks during destruction
    if (_blinkTimer)
    {
        _blinkTimer->stop();
    }
}

void EmulatorTile::prepareForDeletion()
{
    // Stop all timers IMMEDIATELY to prevent callbacks during pending deletion
    // Stop all timers IMMEDIATELY to prevent callbacks during pending deletion
    if (_blinkTimer)
    {
        _blinkTimer->stop();
        _blinkTimer->deleteLater();
        _blinkTimer = nullptr;
    }
    
    // Clear emulator reference to prevent any further access
    _emulator.reset();
    
    // Disable updates to prevent paint events
    setUpdatesEnabled(false);
    hide();
}

void EmulatorTile::paintEvent(QPaintEvent* event)
{
    if (_isSynchronousMode) return;

    QPainter painter(this);

    if (!_emulator)
    {
        painter.fillRect(rect(), Qt::black);
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, "No Emulator");
        return;
    }

    QImage image = convertFramebuffer();
    if (!image.isNull())
    {
        // PERFORMANCE: Disable smooth transformation (bilinear) - use fast nearest-neighbor
        // This reduces Qt scaling overhead by ~90% (qt_blend_argb32_on_argb32_neon)
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

        // Extract central 256x192 screen from 352x288 framebuffer, scale to widget size
        QRectF sourceRect(48, 48, 256, 192);
        QRectF targetRect(rect());
        painter.drawImage(targetRect, image, sourceRect);
    }
    else
    {
        painter.fillRect(rect(), Qt::black);
    }

    // Draw visual feedback borders
    if (_isBlinkingSuccess)
    {
        // Bright green blink on successful load
        QPen pen(QColor(0, 255, 0), 6);
        painter.setPen(pen);
        painter.drawRect(rect().adjusted(2, 2, -4, -4));
    }
    else if (_isBlinkingFailure)
    {
        // Bright red blink on failed load
        QPen pen(QColor(255, 0, 0), 6);
        painter.setPen(pen);
        painter.drawRect(rect().adjusted(2, 2, -4, -4));
    }
    else if (_isDragHovering)
    {
        // Thick blue border when dragging file over tile
        QPen pen(QColor(80, 120, 255), 5);
        painter.setPen(pen);
        painter.drawRect(rect().adjusted(2, 2, -4, -4));
    }
    else if (_hasTileFocus && !_isSynchronousMode)
    {
        // Lighter blue border when tile has keyboard focus
        QPen pen(QColor(120, 160, 255), 2);
        painter.setPen(pen);
        painter.drawRect(rect().adjusted(1, 1, -2, -2));
    }
}

void EmulatorTile::focusInEvent(QFocusEvent* event)
{
    _hasTileFocus = true;
    // NOTE: Do NOT emit signal here - this triggers on Qt auto-focus (window transitions)
    // Audio binding should only happen from user clicks (mousePressEvent)
    update();
    QWidget::focusInEvent(event);
}

void EmulatorTile::focusOutEvent(QFocusEvent* event)
{
    _hasTileFocus = false;
    update();
    QWidget::focusOutEvent(event);
}

void EmulatorTile::dragEnterEvent(QDragEnterEvent* event)
{
    if (!_emulator)
    {
        event->ignore();
        return;
    }

    // Accept only file drops
    if (!event->mimeData()->hasUrls())
    {
        event->ignore();
        return;
    }

    // Validate file type
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty())
    {
        event->ignore();
        return;
    }

    QString filePath = urls.first().toLocalFile();
    QString ext = filePath.right(4).toLower();

    // Accept snapshots and disk images
    if (ext == ".sna" || ext == ".z80" || ext == ".scl" || ext == ".trd" || ext == ".tap" || ext == ".tzx")
    {
        _isDragHovering = true;  // Enable visual feedback
        event->acceptProposedAction();
        update();  // Trigger repaint for visual feedback
    }
    else
    {
        event->ignore();
    }
}

void EmulatorTile::dragLeaveEvent(QDragLeaveEvent* event)
{
    _isDragHovering = false;  // Disable visual feedback
    update();                 // Trigger repaint to remove border
    QWidget::dragLeaveEvent(event);
}

void EmulatorTile::dropEvent(QDropEvent* event)
{
    if (!_emulator)
    {
        event->ignore();
        return;
    }

    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty())
    {
        event->ignore();
        return;
    }

    QString filePath = urls.first().toLocalFile();
    QString ext = filePath.right(4).toLower();

    bool loadSuccess = false;

    // Load snapshot files
    if (ext == ".sna" || ext == ".z80")
    {
        loadSuccess = _emulator->LoadSnapshot(filePath.toStdString());
        qDebug() << (loadSuccess ? "Loaded" : "Failed to load") << "snapshot:" << filePath;
    }
    // Load disk images
    else if (ext == ".scl" || ext == ".trd")
    {
        loadSuccess = _emulator->LoadDisk(filePath.toStdString());
        qDebug() << (loadSuccess ? "Loaded" : "Failed to load") << "disk:" << filePath;
    }
    // Load tape files
    else if (ext == ".tap" || ext == ".tzx")
    {
        loadSuccess = _emulator->LoadTape(filePath.toStdString());
        qDebug() << (loadSuccess ? "Loaded" : "Failed to load") << "tape:" << filePath;
    }

    // Visual feedback: blink border (green=success, red=failure)
    _isDragHovering = false;  // Clear hover state
    if (loadSuccess)
    {
        _isBlinkingSuccess = true;
    }
    else
    {
        _isBlinkingFailure = true;
    }

    // Stop blink after 300ms
    QTimer::singleShot(300, this, [this]() {
        _isBlinkingSuccess = false;
        _isBlinkingFailure = false;
        update();
    });

    event->acceptProposedAction();
    update();  // Repaint with blink effect
}

void EmulatorTile::mousePressEvent(QMouseEvent* event)
{
    // Emit click signal - VideoWallWindow handles focus and audio binding
    emit tileClicked(this);
    QWidget::mousePressEvent(event);
}

void EmulatorTile::keyPressEvent(QKeyEvent* event)
{
    event->accept();

    // Don't react on auto-repeat
    if (!event->isAutoRepeat() && _emulator)
    {
        quint8 zxKey = KeyboardManager::mapQtKeyToEmulatorKeyWithModifiers(event->key(), event->modifiers());

        // Skip unknown keys
        if (zxKey != 0)
        {
            // Create event WITH target emulator ID for selective routing
            std::string targetId = _emulator->GetUUID();
            KeyboardEvent* keyEvent = new KeyboardEvent(static_cast<uint8_t>(zxKey), KEY_PRESSED, targetId);

            // Send valid key combinations to emulator instance
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            messageCenter.Post(MC_KEY_PRESSED, keyEvent);

            qDebug() << "EmulatorTile: Key pressed for emulator:" << QString::fromStdString(targetId);
        }
    }
}

void EmulatorTile::keyReleaseEvent(QKeyEvent* event)
{
    event->accept();

    // Don't react on auto-repeat
    if (!event->isAutoRepeat() && _emulator)
    {
        quint8 zxKey = KeyboardManager::mapQtKeyToEmulatorKeyWithModifiers(event->key(), event->modifiers());

        // Skip unknown keys
        if (zxKey != 0)
        {
            // Create event WITH target emulator ID for selective routing
            std::string targetId = _emulator->GetUUID();
            KeyboardEvent* keyEvent = new KeyboardEvent(static_cast<uint8_t>(zxKey), KEY_RELEASED, targetId);

            // Send valid key combinations to emulator instance
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            messageCenter.Post(MC_KEY_RELEASED, keyEvent);

            qDebug() << "EmulatorTile: Key released for emulator:" << QString::fromStdString(targetId);
        }
    }
}

void EmulatorTile::setEmulator(std::shared_ptr<Emulator> emulator)
{
    _emulator = emulator;
    _emulatorId = _emulator ? _emulator->GetUUID().toString() : "";
    update();
}

void EmulatorTile::setSynchronousMode(bool enable)
{
    if (_isSynchronousMode == enable) return;
    _isSynchronousMode = enable;
}

void EmulatorTile::setCrtPhosphorEnabled(bool enable)
{
    _crtPhosphorEnabled = enable;
    if (!enable)
    {
        _phosphorHistory.clear();
    }
}

void EmulatorTile::setSmartPhosphorEnabled(bool enable)
{
    _smartPhosphorEnabled = enable;
}

void EmulatorTile::setPhosphorDepth(int depth)
{
    _phosphorDepth = depth;
    _phosphorHistory.clear();
}

void EmulatorTile::setCrtScanlinesEnabled(bool enable)
{
    _crtScanlinesEnabled = enable;
}



QImage EmulatorTile::convertFramebuffer()
{
    // TODO: OPTIMIZATION OPPORTUNITIES (defer to Phase 6 or when scaling to 100+ tiles)
    // 1. Async framebuffer copy: Use std::async to parallel-copy from multiple emulators
    // 2. Local buffer: memcpy to tile-local buffer for thread safety and no tearing
    // 3. MessageCenter notifications: Replace QTimer with NC_VIDEO_FRAME_REFRESH events
    // 4. Batch updates: Coordinate all tiles to repaint together in single window update
    // Current: Zero-copy direct read (fast, simple, works well for current scale)

    // Default: black image if no emulator
    QImage image(TILE_WIDTH, TILE_HEIGHT, QImage::Format_RGBA8888);
    image.fill(Qt::black);

    if (!_emulator)
    {
        return image;
    }

    // Get framebuffer from emulator
    FramebufferDescriptor framebufferDesc = _emulator->GetFramebuffer();

    if (framebufferDesc.memoryBuffer && framebufferDesc.width > 0 && framebufferDesc.height > 0)
    {
        // The framebuffer is larger than 256x192 - it includes borders
        // For ZX48: framebuffer is 352x288, screen is 256x192 at offset (48, 48)
        // We need to use the correct stride (bytes per line in the full framebuffer)

        int bytesPerPixel = 4;  // RGBA8888
        int stride = framebufferDesc.width * bytesPerPixel;

        QImage rawImage(static_cast<const unsigned char*>(framebufferDesc.memoryBuffer), framebufferDesc.width, framebufferDesc.height, stride, QImage::Format_RGBA8888);
        QImage activeArea = rawImage.copy(48, 48, 256, 192);

        if (_crtPhosphorEnabled)
        {
            applyPhosphorBlend(activeArea);
        }
        if (_crtScanlinesEnabled)
        {
            applyScanlines(activeArea);
        }

        // Scale to tile size using fast nearest-neighbor scaling
        return activeArea.scaled(TILE_WIDTH, TILE_HEIGHT, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }

    return image;
}

void EmulatorTile::applyPhosphorBlend(QImage& current)
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

void EmulatorTile::applyScanlines(QImage& current)
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
