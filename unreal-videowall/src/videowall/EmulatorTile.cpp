#include "videowall/EmulatorTile.h"

#include <emulator.h>
#include <platform.h>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPainter>
#include <QTimer>

EmulatorTile::EmulatorTile(std::shared_ptr<Emulator> emulator, QWidget* parent) : QWidget(parent), _emulator(emulator)
{
    setFixedSize(TILE_WIDTH, TILE_HEIGHT);
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
        // Draw placeholder for empty tile
        painter.fillRect(rect(), Qt::darkGray);
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, "No Emulator");
        return;
    }

    // Get full framebuffer (includes borders)
    QImage fullFramebuffer = convertFramebuffer();

    // Extract the central 256x192 screen area from the full framebuffer
    // For ZX48: framebuffer is 352x288, screen starts at offset (48, 48)
    QRectF sourceRect(48, 48, TILE_WIDTH, TILE_HEIGHT);  // Source region in full framebuffer
    QRectF targetRect(0, 0, TILE_WIDTH, TILE_HEIGHT);    // Target region in widget

    painter.drawImage(targetRect, fullFramebuffer, sourceRect);

    // Draw focus border if focused
    if (_hasTileFocus)
    {
        painter.setPen(QPen(Qt::cyan, 2));
        painter.drawRect(rect().adjusted(1, 1, -1, -1));
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
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
    }
}

void EmulatorTile::dropEvent(QDropEvent* event)
{
    // TODO: Phase 5 - Load snapshot/disk image
    const QMimeData* mimeData = event->mimeData();

    if (mimeData->hasUrls())
    {
        QList<QUrl> urls = mimeData->urls();
        if (!urls.isEmpty())
        {
            QString filePath = urls.first().toLocalFile();
            // TODO: Load file into emulator
        }
    }
}

void EmulatorTile::mousePressEvent(QMouseEvent* event)
{
    setFocus();
    QWidget::mousePressEvent(event);
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
