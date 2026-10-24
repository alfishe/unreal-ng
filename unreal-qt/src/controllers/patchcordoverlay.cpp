#include "patchcordoverlay.h"

#include <QAbstractItemView>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kSpread = 7.0;  // px between the cords where they pass the picture
constexpr double kPi = 3.14159265358979323846;

/// An S-curve between two points: horizontal tangents at both ends
QPainterPath Curve(const QPointF& a, const QPointF& b)
{
    QPainterPath path(a);
    const double dx = std::max(12.0, std::abs(b.x() - a.x()) * 0.55);
    path.cubicTo(QPointF(a.x() + dx, a.y()), QPointF(b.x() - dx, b.y()), b);
    return path;
}
}  // namespace

PatchCordOverlay::PatchCordOverlay(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    _clock.start();
}

void PatchCordOverlay::setCords(const std::vector<Cord>& cords, const QRectF& passage)
{
    const bool live = std::any_of(cords.begin(), cords.end(), [](const Cord& cord) { return cord.live; });
    if (cords == _cords && passage == _passage && !live)
        return;
    _cords = cords;
    _passage = passage;
    update();
}

QPointF PatchCordOverlay::RowAnchor(QWidget* parent, QAbstractItemView* view, const QRect& row, bool right)
{
    QWidget* port = view->viewport();
    const int y = std::clamp(row.center().y(), 0, std::max(0, port->height() - 1));
    const QPoint edge = view->mapTo(parent, QPoint(right ? view->width() : 0, 0));
    return {static_cast<double>(edge.x()), static_cast<double>(port->mapTo(parent, QPoint(0, y)).y())};
}

std::vector<QPainterPath> PatchCordOverlay::pathsOf(size_t index) const
{
    if (index >= _cords.size())
        return {};
    const Cord& cord = _cords[index];
    if (_passage.isEmpty() || cord.from.x() >= _passage.left() || cord.to.x() <= _passage.right())
        return {Curve(cord.from, cord.to)};
    const double middle = _passage.center().y() +
                          (static_cast<double>(index) - (static_cast<double>(_cords.size()) - 1.0) / 2.0) * kSpread;
    const double y = std::clamp(middle, _passage.top() + 4.0, _passage.bottom() - 4.0);
    return {Curve(cord.from, QPointF(_passage.left(), y)), Curve(QPointF(_passage.right(), y), cord.to)};
}

void PatchCordOverlay::paintEvent(QPaintEvent*)
{
    if (_cords.empty())
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    // A slow breath, not a blink: 1.2 Hz between 55 % and 100 %
    const double phase = 0.5 + 0.5 * std::sin(2.0 * kPi * 1.2 * static_cast<double>(_clock.elapsed()) / 1000.0);
    // The quiet ones first: the selected device's cords lie on top
    for (bool selected : {false, true})
        for (size_t i = 0; i < _cords.size(); ++i)
        {
            const Cord& cord = _cords[i];
            if (cord.selected != selected)
                continue;
            QColor colour = cord.colour;
            double width = selected ? 2.0 : 1.5;
            double alpha = selected ? 0.9 : 0.6;
            if (cord.live)
            {
                alpha = 0.55 + 0.45 * phase;
                width += 0.8;
            }
            colour.setAlphaF(static_cast<float>(alpha));
            painter.setPen(QPen(colour, width, Qt::SolidLine, Qt::RoundCap));
            painter.setBrush(Qt::NoBrush);
            for (const QPainterPath& path : pathsOf(i))
                painter.drawPath(path);
            // The plugs: a dot at each end (the connector's row carries the tag: colour is never the only signal)
            painter.setPen(Qt::NoPen);
            painter.setBrush(colour);
            painter.drawEllipse(cord.from, 2.5, 2.5);
            painter.drawEllipse(cord.to, 2.5, 2.5);
        }
}
