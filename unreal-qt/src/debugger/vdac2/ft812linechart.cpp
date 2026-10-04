#include "ft812linechart.h"

#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>

QColor Ft812LineColor(Ft812LineBudget::LineClass cls)
{
    switch (cls)
    {
        case Ft812LineBudget::LineClass::Ok:
            return QColor(70, 170, 90);
        case Ft812LineBudget::LineClass::OverSoft:
            return QColor(230, 150, 40);
        case Ft812LineBudget::LineClass::OverHard:
            return QColor(220, 50, 50);
        case Ft812LineBudget::LineClass::NotMeasured:
        default:
            return QColor(128, 128, 128);
    }
}

/// region <Ft812LineChart>

Ft812LineChart::Ft812LineChart(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(120, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void Ft812LineChart::setView(Ft812LineBudget::View view)
{
    _view = std::move(view);
    update();
}

void Ft812LineChart::RowMapping(double& top, double& lineHeight) const
{
    const int count = static_cast<int>(_view.lines.size());
    top = 0;
    lineHeight = count > 0 ? static_cast<double>(height()) / count : 1.0;
    QRect picture;
    double firstRow = 0;
    double rows = 0;
    if (!_geometry || !_geometry(picture, firstRow, rows) || picture.height() <= 0 || rows <= 0)
        return;
    // The screen's scale: one framebuffer row of the picture per line
    lineHeight = picture.height() / rows;
    const double pictureTop = picture.top() - mapToGlobal(QPoint(0, 0)).y() - firstRow * lineHeight;
    // Level with the picture when that keeps every line inside the chart (docked
    // beside it); otherwise from the chart's top, still in the screen's scale
    const double span = lineHeight * count;
    top = (pictureTop >= -0.5 && pictureTop + span <= height() + 0.5) ? pictureTop : 0;
}

void Ft812LineChart::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const QColor background(28, 28, 30);
    const QColor text(200, 200, 200);
    painter.fillRect(rect(), background);
    const int count = static_cast<int>(_view.lines.size());
    if (count == 0 || _view.axisClocks == 0)
    {
        painter.setPen(text);
        painter.drawText(rect(), Qt::AlignCenter, tr("No line costs yet"));
        return;
    }

    double top = 0;
    double lineHeight = 1;
    RowMapping(top, lineHeight);
    const double w = width();
    auto xOf = [&](uint32_t clocks) { return w * clocks / _view.axisClocks; };

    // One horizontal bar per line. Where lines are thinner than a pixel, a pixel row
    // shows the worst of its lines, so an overloaded line never vanishes
    const double linesPerPixel = lineHeight < 1.0 ? 1.0 / lineHeight : 1.0;
    for (double first = 0; first < count; first += linesPerPixel)
    {
        const int from = static_cast<int>(first);
        const int to = std::min(count, std::max(from + 1, static_cast<int>(first + linesPerPixel)));
        const Ft812LineBudget::Line* worst = &_view.lines[from];
        for (int i = from + 1; i < to; ++i)
            if (_view.lines[i].clocks > worst->clocks)
                worst = &_view.lines[i];
        QColor color = Ft812LineColor(worst->cls);
        if (worst->previousFrame)
            color = QColor((color.red() + background.red()) / 2, (color.green() + background.green()) / 2,
                           (color.blue() + background.blue()) / 2);  // the previous frame, below the boundary
        const double y = top + from * lineHeight;
        const double h = std::max(1.0, (to - from) * lineHeight);
        painter.fillRect(QRectF(0, y, xOf(worst->clocks), h), color);
    }

    // The budgets, labeled at the top as in the line cost reference
    const double yEnd = top + count * lineHeight;
    painter.setPen(QPen(Ft812LineColor(Ft812LineBudget::LineClass::OverSoft), 1));
    painter.drawLine(QPointF(xOf(_view.softBudget), top), QPointF(xOf(_view.softBudget), yEnd));
    painter.setPen(QPen(Ft812LineColor(Ft812LineBudget::LineClass::OverHard), 1));
    painter.drawLine(QPointF(xOf(_view.hardBudget), top), QPointF(xOf(_view.hardBudget), yEnd));
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.8);
    painter.setFont(small);
    const double labelY = std::max(10.0, top - 3);
    painter.setPen(Ft812LineColor(Ft812LineBudget::LineClass::OverSoft));
    painter.drawText(QPointF(xOf(_view.softBudget) - 30, labelY), QString::number(_view.softBudget));
    painter.setPen(Ft812LineColor(Ft812LineBudget::LineClass::OverHard));
    painter.drawText(QPointF(xOf(_view.hardBudget) + 3, labelY), QString::number(_view.hardBudget));

    // The frame in flight ends here; below it, the previous frame
    if (_view.composite)
    {
        const double y = top + _view.boundary * lineHeight;
        painter.setPen(QPen(QColor(40, 160, 230), 2));
        painter.drawLine(QPointF(0, y), QPointF(w, y));
    }
}

void Ft812LineChart::mouseMoveEvent(QMouseEvent* event)
{
    const int count = static_cast<int>(_view.lines.size());
    if (count == 0)
        return;
    double top = 0;
    double lineHeight = 1;
    RowMapping(top, lineHeight);
    const int line = static_cast<int>((event->position().y() - top) / lineHeight);
    if (line < 0 || line >= count)
    {
        QToolTip::hideText();
        return;
    }
    const Ft812LineBudget::Line& l = _view.lines[line];
    QString text = l.cls == Ft812LineBudget::LineClass::NotMeasured
                       ? tr("Line %1: not measured").arg(line)
                       : tr("Line %1: %2 of %3 clocks (%4 %)")
                             .arg(line)
                             .arg(l.clocks)
                             .arg(_view.hardBudget)
                             .arg(_view.hardBudget ? l.clocks * 100 / _view.hardBudget : 0);
    if (l.previousFrame)
        text += tr("\n(previous frame: the frame in flight has not reached this line)");
    QToolTip::showText(event->globalPosition().toPoint(), text, this);
}

/// endregion
