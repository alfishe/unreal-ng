#include "statusindicator.h"

#include <QCursor>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include "tintedsvgicon.h"

StatusIndicator::StatusIndicator(const QString& iconName, const QString& name, QWidget* parent)
    : QWidget(parent), _icon(tintedSvgIcon(iconName)), _name(name)
{
    setCursor(Qt::ArrowCursor);
    setToolTip(name);
    _blink.setInterval(550);
    connect(&_blink, &QTimer::timeout, this, [this] {
        _phase = !_phase;
        update();
    });
}

void StatusIndicator::setActive(bool on)
{
    if (_active == on)
        return;
    _active = on;
    _phase = false;
    if (on && _blinking)
        _blink.start();
    else
        _blink.stop();
    update();
}

void StatusIndicator::setBlinking(bool blinking)
{
    _blinking = blinking;
    if (!_blinking)
    {
        _blink.stop();
        _phase = false;
        update();
    }
    else if (_active)
    {
        _blink.start();
    }
}

void StatusIndicator::setDetail(const QString& detail)
{
    setToolTip(detail.isEmpty() ? _name : QStringLiteral("%1 (%2)").arg(_name, detail));
}

void StatusIndicator::setLiveToolTip(const QString& text)
{
    if (toolTip() == text)
        return;
    setToolTip(text);
    if (QToolTip::isVisible() && underMouse())
        QToolTip::showText(QCursor::pos(), text, this);
}

void StatusIndicator::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    qreal opacity = 0.28;
    if (_active)
        opacity = _phase ? 0.35 : 1.0;
    p.setOpacity(opacity);

    const QSize s(16, 16);
    const QRect r(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
    _icon.paint(&p, r);  // TintedSvgIconEngine renders at the widget's device pixel ratio
}

void StatusIndicator::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        emit clicked();
}
