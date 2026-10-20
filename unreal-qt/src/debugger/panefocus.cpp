#include "panefocus.h"

#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QMetaObject>
#include <QPainter>
#include <QPalette>

#include <algorithm>
#include <cmath>
#include <limits>

/// A frame drawn over the window around a pane or a cell, transparent for the mouse
class PaneFocus::Frame : public QWidget
{
public:
    Frame(QWidget* window, double width) : QWidget(window), _width(width)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        hide();
    }

    /// Around @p target in window coordinates, or hidden for nullptr
    void Around(QWidget* target)
    {
        if (target == nullptr || !target->isVisible())
        {
            hide();
            return;
        }
        const int margin = static_cast<int>(std::ceil(_width));
        const QRect rect(target->mapTo(parentWidget(), QPoint(0, 0)), target->size());
        setGeometry(rect.adjusted(-margin, -margin, margin, margin));
        show();
        raise();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette().color(QPalette::Highlight), _width));
        painter.setBrush(Qt::NoBrush);
        const double half = _width / 2;
        painter.drawRoundedRect(QRectF(rect()).adjusted(half, half, -half, -half), 4, 4);
    }

private:
    double _width;
};

PaneFocus::PaneFocus(QWidget* window, std::vector<Pane> panes)
    : QObject(window), _window(window), _panes(std::move(panes)), _cell(_panes.size(), 0)
{
    _paneFrame = new Frame(window, 2.0);
    _cellFrame = new Frame(window, 1.5);
    _window->installEventFilter(this);
    for (Pane& pane : _panes)
    {
        // A pane of cells has no focusable child: it takes the focus itself and its keys go through the filter
        if (!pane.cells.empty())
            pane.widget->setFocusPolicy(Qt::StrongFocus);
        for (QWidget* widget = pane.widget; widget != nullptr && widget != _window; widget = widget->parentWidget())
            widget->installEventFilter(this);
    }
    connect(qApp, &QApplication::focusChanged, this, [this] { Place(); });
}

void PaneFocus::Move(int delta)
{
    if (_panes.empty())
        return;
    const int count = static_cast<int>(_panes.size());
    const int from = Focused() >= 0 ? Focused() : _pane - (delta > 0 ? 1 : 0);
    _pane = ((from + delta) % count + count) % count;
    if (QWidget* receiver = Receiver(_panes[_pane]))
        receiver->setFocus(Qt::OtherFocusReason);
    Place();
}

void PaneFocus::Key(int key)
{
    if (_panes.empty())
        return;
    if (Focused() < 0)
        Move(0);
    Pane& pane = _panes[_pane];
    if (!pane.cells.empty())
    {
        CellKey(pane, static_cast<size_t>(_pane), key);
        return;
    }
    QWidget* receiver = Receiver(pane);
    if (receiver == nullptr)
        return;
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QCoreApplication::sendEvent(receiver, &press);
    QCoreApplication::sendEvent(receiver, &release);
}

bool PaneFocus::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type())
    {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
            Place();
            break;
        case QEvent::KeyPress:
            for (size_t i = 0; i < _panes.size(); ++i)
                if (watched == _panes[i].widget && !_panes[i].cells.empty())
                {
                    auto* key = static_cast<QKeyEvent*>(event);
                    if (key->modifiers() == Qt::NoModifier && CellKey(_panes[i], i, key->key()))
                        return true;
                }
            break;
        default:
            break;
    }
    return QObject::eventFilter(watched, event);
}

int PaneFocus::Focused() const
{
    QWidget* focus = _window->focusWidget();
    for (size_t i = 0; focus != nullptr && i < _panes.size(); ++i)
        if (focus == _panes[i].widget || _panes[i].widget->isAncestorOf(focus))
            return static_cast<int>(i);
    return -1;
}

QWidget* PaneFocus::Receiver(const Pane& pane) const
{
    if (pane.widget->focusPolicy() != Qt::NoFocus)
        return pane.widget;
    for (QWidget* child : pane.widget->findChildren<QWidget*>())
        if ((child->focusPolicy() & Qt::StrongFocus) == Qt::StrongFocus && child->isVisible())
            return child;
    return pane.widget;
}

/// Arrows move the cursor to the nearest visible cell that way, Enter double-clicks it; false for other keys
bool PaneFocus::CellKey(Pane& pane, size_t index, int key)
{
    size_t& cell = _cell[index];
    if (cell >= pane.cells.size())
        cell = 0;
    if (key == Qt::Key_Return || key == Qt::Key_Enter)
    {
        QMetaObject::invokeMethod(pane.cells[cell], "doubleClicked");
        return true;
    }
    QPointF direction;
    switch (key)
    {
        case Qt::Key_Up:
        case Qt::Key_PageUp:
            direction = {0, -1};
            break;
        case Qt::Key_Down:
        case Qt::Key_PageDown:
            direction = {0, 1};
            break;
        case Qt::Key_Left:
            direction = {-1, 0};
            break;
        case Qt::Key_Right:
            direction = {1, 0};
            break;
        default:
            return false;
    }
    auto centre = [this](QWidget* widget) {
        return QPointF(widget->mapTo(_window, widget->rect().center()));
    };
    const QPointF from = centre(pane.cells[cell]);
    double best = std::numeric_limits<double>::max();
    for (size_t i = 0; i < pane.cells.size(); ++i)
    {
        if (i == cell || !pane.cells[i]->isVisible())
            continue;
        const QPointF offset = centre(pane.cells[i]) - from;
        const double along = offset.x() * direction.x() + offset.y() * direction.y();
        const double across = std::abs(offset.x() * direction.y() - offset.y() * direction.x());
        if (along <= 0.5)
            continue;
        const double score = along + 2 * across;  // the cell straight that way before a nearer one aside
        if (score < best)
        {
            best = score;
            cell = i;
        }
    }
    Place();
    return true;
}

/// The pane frame around the pane in focus, the cell frame around its cursor
void PaneFocus::Place()
{
    const int focused = Focused();
    if (focused >= 0)
        _pane = focused;
    _paneFrame->Around(focused >= 0 ? _panes[focused].widget : nullptr);
    QWidget* cell = nullptr;
    if (focused >= 0 && !_panes[focused].cells.empty())
        cell = _panes[focused].cells[std::min(_cell[focused], _panes[focused].cells.size() - 1)];
    _cellFrame->Around(cell);
}
