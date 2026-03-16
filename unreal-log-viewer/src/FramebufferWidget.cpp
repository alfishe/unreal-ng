#include "FramebufferWidget.h"

#include <QPainter>
#include <QVBoxLayout>

#include "emulator/monitoring/manifest.h"

using namespace monitoring;

FramebufferWidget::FramebufferWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    _infoLabel = new QLabel("No framebuffer attached", this);
    _infoLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(_infoLabel);

    // Stretch takes the paint area
    layout->addStretch(1);

    _refreshTimer = new QTimer(this);
    connect(_refreshTimer, &QTimer::timeout, this, &FramebufferWidget::onRefresh);
}

void FramebufferWidget::attachToShm(void* base, size_t size)
{
    _shmBase = base;
    _shmSize = size;
    _refreshTimer->start(REFRESH_MS);
}

void FramebufferWidget::detachFromShm()
{
    _refreshTimer->stop();
    _shmBase = nullptr;
    _shmSize = 0;
    _fifoActive = false;
    _image = QImage();
    _infoLabel->setText("No framebuffer attached");
    update();
}

void FramebufferWidget::onFrameReady()
{
    onRefresh();
}

void FramebufferWidget::setFifoActive(bool active)
{
    _fifoActive = active;
    if (_shmBase)
        _refreshTimer->start(active ? FALLBACK_MS : REFRESH_MS);
}

void FramebufferWidget::onRefresh()
{
    if (!_shmBase)
        return;

    auto* manifest = static_cast<ManifestHeader*>(_shmBase);

    for (uint16_t i = 0; i < manifest->section_count; i++)
    {
        auto* desc = getDescriptor(_shmBase, manifest, i);
        if (!desc || desc->type != SectionType::FRAMEBUFFER)
            continue;

        // Epoch-safe read
        uint64_t before = desc->epoch.load(std::memory_order_acquire);
        if (before == EPOCH_UPDATING)
            return;

        auto* data = static_cast<const uint8_t*>(getSectionData(_shmBase, desc));
        if (!data)
            return;

        auto* hdr = reinterpret_cast<const FramebufferSectionHeader*>(data);
        int w = hdr->width;
        int h = hdr->height;
        if (w <= 0 || h <= 0 || w > 1024 || h > 1024)
            return;

        const uint8_t* pixels = data + hdr->buffer_offset;
        uint32_t pixelBytes = static_cast<uint32_t>(w) * h * 4;
        if (hdr->buffer_offset + pixelBytes > desc->length)
            return;

        // Copy into QImage
        _image = QImage(w, h, QImage::Format_RGBA8888);
        memcpy(_image.bits(), pixels, pixelBytes);

        std::atomic_thread_fence(std::memory_order_acquire);
        uint64_t after = desc->epoch.load(std::memory_order_relaxed);
        if (before != after)
        {
            _image = QImage();
            return;  // Torn read, skip this frame
        }

        uint64_t frame = manifest->frame_counter.load(std::memory_order_relaxed);
        _infoLabel->setText(QString("%1x%2  frame %3").arg(w).arg(h).arg(frame));
        update();
        return;
    }

    _infoLabel->setText("FRAMEBUFFER section not found");
}

void FramebufferWidget::paintEvent(QPaintEvent*)
{
    if (_image.isNull())
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    // Calculate aspect-ratio-preserving rect (below info label)
    int labelH = _infoLabel->height() + 8;  // 8px gap between title and image
    QRect area(0, labelH, width(), height() - labelH);

    float scaleX = static_cast<float>(area.width()) / _image.width();
    float scaleY = static_cast<float>(area.height()) / _image.height();
    float scale = qMin(scaleX, scaleY);

    int dw = static_cast<int>(_image.width() * scale);
    int dh = static_cast<int>(_image.height() * scale);
    int dx = area.x() + (area.width() - dw) / 2;
    int dy = area.y();  // top-aligned, not centered

    painter.drawImage(QRect(dx, dy, dw, dh), _image);
}
