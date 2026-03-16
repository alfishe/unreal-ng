#include "SparklineWidget.h"

#include <QPainter>
#include <algorithm>
#include <limits>

static const QColor SPARKLINE_COLOR(140, 140, 140);

SparklineWidget::SparklineWidget(int capacity, QWidget* parent)
    : QWidget(parent)
    , _capacity(capacity)
    , _buffer(capacity, 0.0f)
{
    setFixedHeight(18);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void SparklineWidget::addValue(float v)
{
    _buffer[_head] = v;
    _head = (_head + 1) % _capacity;
    if (_count < _capacity)
        _count++;
    _dirty = true;
    update();
}

void SparklineWidget::clear()
{
    std::fill(_buffer.begin(), _buffer.end(), 0.0f);
    _head = 0;
    _count = 0;
    _dirty = true;
    update();
}

void SparklineWidget::paintEvent(QPaintEvent*)
{
    if (_count < 2)
        return;

    if (_dirty)
    {
        _minVal = std::numeric_limits<float>::max();
        _maxVal = std::numeric_limits<float>::lowest();
        int start = (_head - _count + _capacity) % _capacity;
        for (int i = 0; i < _count; i++)
        {
            float v = _buffer[(start + i) % _capacity];
            _minVal = std::min(_minVal, v);
            _maxVal = std::max(_maxVal, v);
        }
        _dirty = false;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    float range = _maxVal - _minVal;
    if (range < 1e-6f)
        range = 1.0f;

    int w = width();
    int h = height();
    float marginY = 1.0f;
    float marginL = 10.0f;  // ~1 char gap from indicator
    float drawW = w - marginL;
    float drawH = h - 2 * marginY;

    int start = (_head - _count + _capacity) % _capacity;

    painter.setPen(QPen(SPARKLINE_COLOR, 1.0));

    QPointF prev;
    for (int i = 0; i < _count; i++)
    {
        float v = _buffer[(start + i) % _capacity];
        float x = marginL + static_cast<float>(i) / (_count - 1) * (drawW - 1);
        float y = marginY + drawH * (1.0f - (v - _minVal) / range);
        QPointF pt(x, y);

        if (i > 0)
            painter.drawLine(prev, pt);
        prev = pt;
    }
}
