#include "widgets/journalspanslider.h"

#include <QPainter>
#include <QStyle>
#include <QStyleOptionSlider>
#include <algorithm>

void JournalSpanSlider::setJournalSpans(std::vector<std::pair<double, double>> spans)
{
    if (spans == _spans)
        return;
    _spans = std::move(spans);
    update();
}

void JournalSpanSlider::paintEvent(QPaintEvent* event)
{
    QSlider::paintEvent(event);
    if (_spans.empty())
        return;
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    QPainter painter(this);
    const QColor band = palette().color(QPalette::Highlight);
    const int y = height() - 3;
    for (const auto& [from, to] : _spans)
    {
        const int x0 = groove.left() + static_cast<int>(from * groove.width());
        const int x1 = groove.left() + static_cast<int>(to * groove.width());
        painter.fillRect(QRect(x0, y, std::max(2, x1 - x0), 3), band);
    }
}

