#include "widgets/journalspanslider.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QToolTip>
#include <algorithm>
#include <cstdlib>

namespace
{
constexpr int kMarkHalfWidth = 4;   ///< a mark's triangle: 9 px wide, its hit area too
}

void JournalSpanSlider::setJournalSpans(std::vector<std::pair<double, double>> spans)
{
    if (spans == _spans)
        return;
    _spans = std::move(spans);
    update();
}

void JournalSpanSlider::setBranchSpans(std::vector<BranchSpan> spans)
{
    if (spans == _branchSpans)
        return;
    _branchSpans = std::move(spans);
    update();
}

void JournalSpanSlider::setForkMarks(std::vector<ForkMark> marks)
{
    if (marks == _marks)
        return;
    _marks = std::move(marks);
    update();
}

QRect JournalSpanSlider::Groove() const
{
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    return style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
}

int JournalSpanSlider::markAt(int x) const
{
    const QRect groove = Groove();
    int best = -1;
    int bestDistance = kMarkHalfWidth + 1;
    for (size_t i = 0; i < _marks.size(); ++i)
    {
        const int mx = groove.left() + static_cast<int>(_marks[i].at * groove.width());
        const int d = std::abs(mx - x);
        if (d < bestDistance)
        {
            best = static_cast<int>(i);
            bestDistance = d;
        }
    }
    return best;
}

void JournalSpanSlider::paintEvent(QPaintEvent* event)
{
    QSlider::paintEvent(event);
    if (_spans.empty() && _branchSpans.empty() && _marks.empty())
        return;
    const QRect groove = Groove();
    QPainter painter(this);
    auto xOf = [&groove](double f) { return groove.left() + static_cast<int>(f * groove.width()); };

    const QColor band = palette().color(QPalette::Highlight);
    const int y = height() - 3;
    for (const auto& [from, to] : _spans)
        painter.fillRect(QRect(xOf(from), y, std::max(2, xOf(to) - xOf(from)), 3), band);

    // The lineage's parts along the top edge: each branch its tint
    for (const BranchSpan& s : _branchSpans)
        painter.fillRect(QRect(xOf(s.from), 0, std::max(2, xOf(s.to) - xOf(s.from)), 3), s.color);

    // Fork marks: a small triangle pointing down at the fork; a taller one where several branches leave
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor mark = palette().color(QPalette::Link);
    painter.setPen(Qt::NoPen);
    painter.setBrush(mark);
    for (const ForkMark& m : _marks)
    {
        const int x = xOf(m.at);
        const int h = m.count > 1 ? 8 : 6;
        const QPoint triangle[3] = {QPoint(x - kMarkHalfWidth, 0), QPoint(x + kMarkHalfWidth, 0), QPoint(x, h)};
        painter.drawPolygon(triangle, 3);
    }
}

bool JournalSpanSlider::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip)
    {
        auto* help = static_cast<QHelpEvent*>(event);
        const int i = markAt(help->pos().x());
        if (i >= 0)
        {
            QToolTip::showText(help->globalPos(), _marks[static_cast<size_t>(i)].tip, this);
            return true;
        }
    }
    return QSlider::event(event);
}

void JournalSpanSlider::mouseDoubleClickEvent(QMouseEvent* event)
{
    const int i = markAt(event->position().toPoint().x());
    if (i >= 0 && onMarkActivated)
    {
        onMarkActivated(i);
        return;
    }
    QSlider::mouseDoubleClickEvent(event);
}
