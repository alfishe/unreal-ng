#include "videowall/EmulatorTile.h"

#include <emulator.h>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPainter>

EmulatorTile::EmulatorTile(std::shared_ptr<Emulator> emulator, QWidget* parent) : QWidget(parent), _emulator(emulator)
{
    setFixedSize(TILE_WIDTH, TILE_HEIGHT);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);

    if (_emulator)
    {
        subscribeToNotifications();
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
        // Draw placeholder
        painter.fillRect(rect(), Qt::darkGray);
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, "No Emulator");
        return;
    }

    // TODO: Phase 3 - Convert framebuffer to QImage and render
    QImage image = convertFramebuffer();
    painter.drawImage(0, 0, image);

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

void EmulatorTile::handleVideoFrameRefresh(int id, void* message)
{
    // TODO: Phase 3 - Implement frame refresh handling
    update();
}

void EmulatorTile::subscribeToNotifications()
{
    // TODO: Phase 2 - Subscribe to NC_VIDEO_FRAME_REFRESH
}

void EmulatorTile::unsubscribeFromNotifications()
{
    // TODO: Phase 2 - Unsubscribe from notifications
}

QImage EmulatorTile::convertFramebuffer()
{
    // TODO: Phase 3 - Convert emulator framebuffer to QImage
    // Placeholder: return black image
    QImage image(TILE_WIDTH, TILE_HEIGHT, QImage::Format_RGB888);
    image.fill(Qt::black);
    return image;
}
