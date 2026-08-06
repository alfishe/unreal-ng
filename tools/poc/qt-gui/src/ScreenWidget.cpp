#include "ScreenWidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QtMath>

namespace {
constexpr int kScreenW = 256;
constexpr int kScreenH = 192;
constexpr int kBorderW = 320;   // 256 + 2*32
constexpr int kBorderH = 240;   // 192 + 2*24
}

ScreenWidget::ScreenWidget(QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette p = palette();
    p.setColor(QPalette::Window, Qt::black);
    setPalette(p);
    setMinimumSize(kBorderW, kBorderH);

    // Placeholder framebuffer: the TR-DOS style boot screen.
    m_frame = QImage(kScreenW, kScreenH, QImage::Format_RGB32);
    m_frame.fill(Qt::black);
    QPainter g(&m_frame);
    QFont f(QStringLiteral("Monospace"), 8);
    f.setStyleHint(QFont::TypeWriter);
    g.setFont(f);
    g.setPen(QColor(0xd8, 0xd8, 0xd8));
    g.drawText(8, 18, QStringLiteral("unreal-ng 0.9.4"));
    g.setPen(QColor(0x00, 0xc0, 0xc0));
    g.drawText(8, 32, QStringLiteral("TR-DOS v5.04T"));
    g.setPen(QColor(0xc0, 0xc0, 0x00));
    g.drawText(8, 52, QStringLiteral("Loading \"manic\" CODE"));
}

void ScreenWidget::setViewMode(ViewMode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    update();
}

void ScreenWidget::setIntegerScaling(bool on)
{
    if (m_integer == on)
        return;
    m_integer = on;
    update();
}

void ScreenWidget::setBorderColor(const QColor &c)
{
    m_border = c;
    update();
}

void ScreenWidget::setFrame(const QImage &frame)
{
    m_frame = frame;
    update();
}

void ScreenWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);

    const bool withBorder = (m_mode == WithBorder);
    const QSize src = withBorder ? QSize(kBorderW, kBorderH) : QSize(kScreenW, kScreenH);

    qreal scale = qMin(qreal(width()) / src.width(), qreal(height()) / src.height());
    if (m_integer && scale >= 1.0)
        scale = qFloor(scale);
    if (scale <= 0)
        scale = 1;

    const QSize out(int(src.width() * scale), int(src.height() * scale));
    const QRect target(QPoint((width() - out.width()) / 2, (height() - out.height()) / 2), out);

    if (withBorder) {
        p.fillRect(target, m_border);
        const QRect inner(target.x() + int(32 * scale),
                          target.y() + int(24 * scale),
                          int(kScreenW * scale),
                          int(kScreenH * scale));
        p.drawImage(inner, m_frame);
    } else {
        p.drawImage(target, m_frame);
    }
}
