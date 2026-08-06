#include "StatusIndicator.h"

#include <QPainter>
#include <QMouseEvent>
#include <QToolTip>

StatusIndicator::StatusIndicator(const QString &iconPath, const QString &name, QWidget *parent)
    : QWidget(parent)
    , m_icon(iconPath)
    , m_name(name)
{
    setCursor(Qt::ArrowCursor);
    setToolTip(name);
    m_blink.setInterval(550);
    connect(&m_blink, &QTimer::timeout, this, [this] { m_phase = !m_phase; update(); });
}

void StatusIndicator::setActive(bool on)
{
    if (m_active == on)
        return;
    m_active = on;
    m_phase = false;
    if (on)
        m_blink.start();
    else
        m_blink.stop();
    update();
    emit toggled(on);
}

void StatusIndicator::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    qreal opacity = 0.28;
    if (m_active)
        opacity = m_phase ? 0.35 : 1.0;
    p.setOpacity(opacity);

    const QSize s(16, 16);
    const QRect r(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);

    QPixmap pm = m_icon.pixmap(s * devicePixelRatioF());
    pm.setDevicePixelRatio(devicePixelRatioF());

    // Tint the monochrome SVG with the current text colour so it works
    // in both light and dark platform themes.
    QPixmap tinted(pm.size());
    tinted.setDevicePixelRatio(pm.devicePixelRatio());
    tinted.fill(Qt::transparent);
    {
        QPainter tp(&tinted);
        tp.drawPixmap(0, 0, pm);
        tp.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tp.fillRect(tinted.rect(), palette().color(QPalette::WindowText));
    }
    p.drawPixmap(r, tinted);
}

void StatusIndicator::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        setActive(!m_active);
}
