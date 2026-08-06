#include "ScreenWidget.h"

#include <QPainter>
#include <QPaintEvent>

static const VideoModeInfo s_videoModes[] = {
    // Standard ZX Spectrum
    { VideoMode::ZX_256x192,    256, 192, "256x192 (ZX inner)", ":/patterns/zx_256x192.png" },
    { VideoMode::ZX_320x240,    320, 240, "320x240 (ZX partial)", ":/patterns/zx_320x240.png" },
    { VideoMode::ZX_352x288,    352, 288, "352x288 (ZX PAL)", ":/patterns/zx_352x288.png" },
    { VideoMode::ZX_384x304,    384, 304, "384x304 (ZX extended)", ":/patterns/zx_384x304.png" },
    // ATM Turbo / Profi
    { VideoMode::ATM_320x200,   320, 200, "320x200 (ATM)", ":/patterns/atm_320x200.png" },
    { VideoMode::ATM_640x200,   640, 200, "640x200 (ATM hi-res)", ":/patterns/atm_640x200.png" },
    { VideoMode::Profi_512x240, 512, 240, "512x240 (Profi)", ":/patterns/profi_512x240.png" },
    // ZX Evolution / TS-Conf
    { VideoMode::Evo_360x288,   360, 288, "360x288 (Evo)", ":/patterns/evo_360x288.png" },
    { VideoMode::TSConf_640x400, 640, 400, "640x400 (TS-Conf)", ":/patterns/tsconf_640x400.png" },
    { VideoMode::TSConf_640x480, 640, 480, "640x480 (TS-Conf VGA)", ":/patterns/tsconf_640x480.png" },
    // ZX Next
    { VideoMode::Next_320x256,  320, 256, "320x256 (Next)", ":/patterns/next_320x256.png" },
    { VideoMode::Next_640x256,  640, 256, "640x256 (Next hi-res)", ":/patterns/next_640x256.png" },
    // Generic hi-res
    { VideoMode::HiRes_512x384, 512, 384, "512x384 (2x)", ":/patterns/hires_512x384.png" },
    { VideoMode::HiRes_640x512, 640, 512, "640x512 (max)", ":/patterns/hires_640x512.png" },
};

const VideoModeInfo* ScreenWidget::videoModes()
{
    return s_videoModes;
}

int ScreenWidget::videoModeCount()
{
    return sizeof(s_videoModes) / sizeof(s_videoModes[0]);
}

ScreenWidget::ScreenWidget(QWidget* parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette p = palette();
    p.setColor(QPalette::Window, Qt::black);
    setPalette(p);

    setMinimumSize(m_baseWidth, m_baseHeight);
    setFocusPolicy(Qt::StrongFocus);
}

ScreenWidget::~ScreenWidget()
{
    detachFramebuffer();
}

void ScreenWidget::attachFramebuffer(uint16_t width, uint16_t height, void* buffer)
{
    detachFramebuffer();

    if (!buffer || width == 0 || height == 0)
        return;

    m_baseWidth = width;
    m_baseHeight = height;
    m_aspectRatio = static_cast<float>(width) / static_cast<float>(height);

    m_image = new QImage(
        static_cast<const uchar*>(buffer),
        width,
        height,
        width * 4,
        QImage::Format_RGBA8888
    );

    setMinimumSize(width, height);
    update();
}

void ScreenWidget::loadTestPattern(int modeIndex)
{
    detachFramebuffer();

    int count = videoModeCount();
    if (modeIndex < 0 || modeIndex >= count)
        modeIndex = 2;

    const auto& mode = s_videoModes[modeIndex];

    m_ownedImage = QImage(QString::fromLatin1(mode.patternFile));
    if (m_ownedImage.isNull())
    {
        // Fallback: create solid color image
        m_ownedImage = QImage(mode.width, mode.height, QImage::Format_RGBA8888);
        m_ownedImage.fill(QColor(32, 32, 96));
    }

    // Convert to RGBA8888 if needed
    if (m_ownedImage.format() != QImage::Format_RGBA8888)
    {
        m_ownedImage = m_ownedImage.convertToFormat(QImage::Format_RGBA8888);
    }

    m_baseWidth = mode.width;
    m_baseHeight = mode.height;
    m_aspectRatio = static_cast<float>(mode.width) / static_cast<float>(mode.height);

    m_image = &m_ownedImage;

    setMinimumSize(mode.width, mode.height);
    update();
}

void ScreenWidget::detachFramebuffer()
{
    if (m_image && m_image != &m_ownedImage)
    {
        delete m_image;
    }
    m_image = nullptr;
    m_ownedImage = QImage();
    update();
}

void ScreenWidget::refresh()
{
    update();
}

QRect ScreenWidget::scaledRect(const QSize& containerSize) const
{
    int destW, destH;
    if (static_cast<float>(containerSize.width()) / containerSize.height() > m_aspectRatio)
    {
        destH = containerSize.height();
        destW = static_cast<int>(destH * m_aspectRatio);
    }
    else
    {
        destW = containerSize.width();
        destH = static_cast<int>(destW / m_aspectRatio);
    }

    int x = (containerSize.width() - destW) / 2;
    int y = (containerSize.height() - destH) / 2;

    return QRect(x, y, destW, destH);
}

void ScreenWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if (!m_image || m_image->isNull())
        return;

#if QT_VERSION >= QT_VERSION_CHECK(5, 13, 0)
    painter.setRenderHint(QPainter::LosslessImageRendering);
#endif
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const int widgetW = width();
    const int widgetH = height();

    int destW, destH;
    if (static_cast<float>(widgetW) / widgetH > m_aspectRatio)
    {
        destH = widgetH;
        destW = static_cast<int>(widgetH * m_aspectRatio);
    }
    else
    {
        destW = widgetW;
        destH = static_cast<int>(widgetW / m_aspectRatio);
    }

    const int x = (widgetW - destW) / 2;
    const int y = (widgetH - destH) / 2;

    painter.drawImage(QRect(x, y, destW, destH), *m_image);
}
