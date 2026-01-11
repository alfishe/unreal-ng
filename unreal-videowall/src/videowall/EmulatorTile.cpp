#include "videowall/EmulatorTile.h"

#include <QDragLeaveEvent>
#include <QMimeData>
#include <QPainter>
#include <QTimer>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator.h"
#include "emulator/io/keyboard/keyboard.h"
#include "keyboard/keyboardmanager.h"

EmulatorTile::EmulatorTile(std::shared_ptr<Emulator> emulator, QWidget* parent) : QWidget(parent), _emulator(emulator)
{
    setFixedSize(TILE_WIDTH, TILE_HEIGHT);
    // Cache emulator UUID
    _emulatorId = _emulator ? _emulator->GetUUID() : "";
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);

    if (_emulator)
    {
        subscribeToNotifications();

        // Set up 50Hz refresh timer (20ms = 50 FPS)
        _refreshTimer = new QTimer(this);
        connect(_refreshTimer, &QTimer::timeout, this, &EmulatorTile::handleVideoFrameRefresh);
        _refreshTimer->start(20);  // 50Hz
    }
}

EmulatorTile::~EmulatorTile()
{
    unsubscribeFromNotifications();
}

void EmulatorTile::paintEvent(QPaintEvent* event)
{
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
        // Extract central 256x192 screen from 352x288 framebuffer
        QRectF sourceRect(48, 48, 256, 192);

        // Scale 2x to fill 512x384 tile
        QRectF targetRect(0, 0, TILE_WIDTH, TILE_HEIGHT);

        // Use nearest-neighbor scaling (default)  // Nearest-neighbor for crisp pixels
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
    else if (_hasTileFocus)
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
    setFocus();
    QWidget::mousePressEvent(event);
}

void EmulatorTile::keyPressEvent(QKeyEvent* event)
{
    // Let application shortcuts propagate to window
    // Fullscreen: Cmd+Shift+F on macOS, Ctrl+Shift+F on Windows/Linux
    bool isFullscreenShortcut = (event->key() == Qt::Key_F && (event->modifiers() & Qt::ControlModifier) &&
                                 (event->modifiers() & Qt::ShiftModifier));
    // Frameless: F10
    bool isFramelessShortcut = (event->key() == Qt::Key_F10);

    if (isFullscreenShortcut || isFramelessShortcut)
    {
        QWidget::keyPressEvent(event);  // Propagate to parent
        return;
    }

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

void EmulatorTile::handleVideoFrameRefresh()
{
    // TODO: Phase 3 - will implement Observer-based notifications
    update();
}

void EmulatorTile::subscribeToNotifications()
{
    // TODO: Phase 3 - will subscribe to NC_VIDEO_FRAME_REFRESH
}

void EmulatorTile::unsubscribeFromNotifications()
{
    // TODO: Phase 3 - will unsubscribe from notifications
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

        // Create QImage from the full framebuffer data with proper stride
        // Qt will handle extracting the correct region when we use devicePixelsRect in paintEvent
        image = QImage(static_cast<const unsigned char*>(framebufferDesc.memoryBuffer), framebufferDesc.width,
                       framebufferDesc.height, stride, QImage::Format_RGBA8888);
    }

    return image;
}
