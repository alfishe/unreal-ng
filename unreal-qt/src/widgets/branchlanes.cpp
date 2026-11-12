#include "widgets/branchlanes.h"

#include <QFontMetrics>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <algorithm>
#include <map>

namespace
{
constexpr int kPad = 6;
constexpr int kBarHeight = 6;
constexpr int kCloseSize = 12;   ///< the cross's box at a lane's right end
}

BranchLanes::BranchLanes(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);   // the cross lights up under the mouse
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setContextMenuPolicy(Qt::CustomContextMenu);
}

QColor BranchLanes::ColorOf(uint32_t id)
{
    if (id == 0)
        return QColor(128, 128, 128);
    // Golden-angle hues: neighbours differ
    const int hue = static_cast<int>((id * 137u) % 360u);
    return QColor::fromHsv(hue, 150, 210);
}

void BranchLanes::setLanes(std::vector<Lane> lanes, uint64_t axisFrom, uint64_t axisTo, uint64_t current)
{
    // Tree order: depth-first from the roots, children by fork frame then id
    std::map<uint32_t, std::vector<const Lane*>> children;
    std::vector<const Lane*> roots;
    std::map<uint32_t, bool> present;
    for (const Lane& l : lanes)
        present[l.id] = true;
    for (const Lane& l : lanes)
        (present.count(l.parent) ? children[l.parent] : roots).push_back(&l);
    auto order = [](const Lane* a, const Lane* b) { return a->fork != b->fork ? a->fork < b->fork : a->id < b->id; };
    std::sort(roots.begin(), roots.end(), order);
    for (auto& [id, list] : children)
        std::sort(list.begin(), list.end(), order);
    std::vector<Lane> ordered;
    std::function<void(const Lane*, int)> walk = [&](const Lane* l, int depth) {
        Lane copy = *l;
        copy.depth = depth;
        ordered.push_back(copy);
        for (const Lane* c : children[l->id])
            walk(c, depth + 1);
    };
    for (const Lane* r : roots)
        walk(r, 0);

    const bool sameShape = ordered.size() == _lanes.size();
    _lanes = std::move(ordered);
    _from = axisFrom;
    _to = std::max(axisTo, axisFrom + 1);
    _current = current;
    if (_selected != kNone && std::none_of(_lanes.begin(), _lanes.end(), [this](const Lane& l) { return l.id == _selected; }))
        _selected = kNone;
    if (!sameShape)
        updateGeometry();
    update();
}

void BranchLanes::setSelected(uint32_t id)
{
    if (_selected == id)
        return;
    _selected = id;
    update();
}

QSize BranchLanes::sizeHint() const
{
    return QSize(400, static_cast<int>(_lanes.size()) * kRowHeight);
}

int BranchLanes::NameWidth() const
{
    const QFontMetrics fm(font());
    int w = fm.horizontalAdvance(QStringLiteral("resume 00"));
    for (const Lane& l : _lanes)
        w = std::max(w, fm.horizontalAdvance(l.name) + l.depth * 8 + fm.horizontalAdvance(QStringLiteral(" *")));
    return std::min(w + kPad, width() / 3);
}

int BranchLanes::XOf(uint64_t frame) const
{
    const int left = NameWidth() + kPad;
    const int right = width() - kPad - kCloseSize - kPad;
    const double f = (static_cast<double>(std::clamp(frame, _from, _to)) - static_cast<double>(_from)) /
                     static_cast<double>(_to - _from);
    return left + static_cast<int>(f * (right - left));
}

int BranchLanes::laneAt(int y) const
{
    const int row = y / kRowHeight;
    return row >= 0 && row < static_cast<int>(_lanes.size()) ? row : -1;
}

QRect BranchLanes::closeRect(int row) const
{
    if (row < 0 || row >= static_cast<int>(_lanes.size()) || _lanes[static_cast<size_t>(row)].parent == kNone)
        return {};
    return QRect(width() - kPad - kCloseSize, row * kRowHeight + (kRowHeight - kCloseSize) / 2, kCloseSize, kCloseSize);
}

bool BranchLanes::closable(int row) const
{
    return !closeRect(row).isEmpty();
}

void BranchLanes::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPalette pal = palette();
    const int nameWidth = NameWidth();
    std::map<uint32_t, int> rowOf;
    for (size_t i = 0; i < _lanes.size(); ++i)
        rowOf[_lanes[i].id] = static_cast<int>(i);

    for (size_t i = 0; i < _lanes.size(); ++i)
    {
        const Lane& l = _lanes[i];
        const int top = static_cast<int>(i) * kRowHeight;
        const QRect row(0, top, width(), kRowHeight);
        if (l.id == _selected)
            p.fillRect(row, pal.color(QPalette::Highlight).lighter(170));

        // The name, indented by depth; bold for the active branch, a star for a pinned one
        QFont f = font();
        f.setBold(l.active);
        p.setFont(f);
        p.setPen(l.onLineage ? pal.color(QPalette::WindowText) : pal.color(QPalette::PlaceholderText));
        const QString name = l.pinned ? l.name + QStringLiteral(" *") : l.name;
        p.drawText(QRect(kPad + l.depth * 8, top, nameWidth - l.depth * 8, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetrics(f).elidedText(name, Qt::ElideRight, nameWidth - l.depth * 8));

        // The bar from the fork to the end
        QColor c = ColorOf(l.id);
        if (!l.onLineage)
            c.setAlpha(110);
        const int x0 = XOf(l.fork);
        const int x1 = std::max(XOf(l.end), x0 + 2);
        const int yMid = top + kRowHeight / 2;
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(QRect(x0, yMid - kBarHeight / 2, x1 - x0, kBarHeight), 2, 2);

        // The connector up to the parent's lane at the fork
        if (auto it = rowOf.find(l.parent); it != rowOf.end())
        {
            const int parentMid = it->second * kRowHeight + kRowHeight / 2;
            p.setPen(QPen(c, 1));
            p.drawLine(x0, parentMid, x0, yMid);
        }

        // The cross (every branch but the trunk): red under the mouse
        if (const QRect box = closeRect(static_cast<int>(i)); !box.isEmpty())
        {
            const bool can = closable(static_cast<int>(i));
            const bool hot = can && _hoverClose == static_cast<int>(i);
            if (hot)
            {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(200, 60, 60));
                p.drawEllipse(box);
            }
            QColor ink = hot ? QColor(Qt::white) : pal.color(QPalette::WindowText);
            if (!can)
                ink = pal.color(QPalette::PlaceholderText);
            p.setPen(QPen(ink, 1.4));
            const QRectF x = QRectF(box).adjusted(3.5, 3.5, -3.5, -3.5);
            p.drawLine(x.topLeft(), x.bottomRight());
            p.drawLine(x.topRight(), x.bottomLeft());
        }
    }

    // The moment the machine stands on
    if (!_lanes.empty() && _current >= _from)
    {
        const int x = XOf(_current);
        p.setPen(QPen(pal.color(QPalette::Highlight), 1));
        p.drawLine(x, 0, x, static_cast<int>(_lanes.size()) * kRowHeight);
    }
}

void BranchLanes::mousePressEvent(QMouseEvent* event)
{
    const QPoint at = event->position().toPoint();
    const int row = laneAt(at.y());
    if (row < 0)
        return QWidget::mousePressEvent(event);
    if (event->button() == Qt::LeftButton && closeRect(row).contains(at))
    {
        if (closable(row) && onDeleteRequested)
            onDeleteRequested(_lanes[static_cast<size_t>(row)].id);
        return;
    }
    setSelected(_lanes[static_cast<size_t>(row)].id);
    if (onSelected)
        onSelected(_selected);
    QWidget::mousePressEvent(event);
}

void BranchLanes::mouseDoubleClickEvent(QMouseEvent* event)
{
    const int row = laneAt(event->position().toPoint().y());
    if (row < 0 || closeRect(row).contains(event->position().toPoint()))
        return QWidget::mouseDoubleClickEvent(event);
    setSelected(_lanes[static_cast<size_t>(row)].id);
    if (onActivated)
        onActivated(_selected);
}

void BranchLanes::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint at = event->position().toPoint();
    const int row = laneAt(at.y());
    const int hover = row >= 0 && closable(row) && closeRect(row).contains(at) ? row : -1;
    if (hover != _hoverClose)
    {
        _hoverClose = hover;
        setCursor(hover >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void BranchLanes::leaveEvent(QEvent* event)
{
    if (_hoverClose >= 0)
    {
        _hoverClose = -1;
        unsetCursor();
        update();
    }
    QWidget::leaveEvent(event);
}

bool BranchLanes::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip)
    {
        auto* help = static_cast<QHelpEvent*>(event);
        const int row = laneAt(help->pos().y());
        if (row >= 0 && closeRect(row).contains(help->pos()))
            QToolTip::showText(help->globalPos(), tr("Delete this branch"), this);
        else if (row >= 0)
            QToolTip::showText(help->globalPos(), _lanes[static_cast<size_t>(row)].tip, this);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}
