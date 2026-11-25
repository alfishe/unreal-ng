#include "tabscrollarrows.h"

#include <QEvent>
#include <QObject>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTabBar>
#include <QToolButton>
#include <QWheelEvent>

#include <algorithm>
#include <cstdlib>

namespace
{
bool IsHorizontal(const QTabBar* bar)
{
    const QTabBar::Shape shape = bar->shape();
    return shape == QTabBar::RoundedNorth || shape == QTabBar::RoundedSouth || shape == QTabBar::TriangularNorth ||
           shape == QTabBar::TriangularSouth;
}

bool IsScrollButton(const QWidget* widget)
{
    return qobject_cast<const QToolButton*>(widget) && widget->parentWidget() &&
           qobject_cast<const QTabBar*>(widget->parentWidget());
}

// QTabBar sizes its scroll buttons by PM_TabBarScrollButtonWidth, asks the style where they go and lays the tabs out
// between them
class EndScrollArrowsStyle : public QProxyStyle
{
public:
    explicit EndScrollArrowsStyle(QStyle* base) : QProxyStyle(base) {}

    int pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const override
    {
        const int value = QProxyStyle::pixelMetric(metric, option, widget);
        if (metric == PM_TabBarScrollButtonWidth && qobject_cast<const QTabBar*>(widget))
            return value * 3 / 2;
        return value;
    }

    // A tool button draws its arrow over the whole button: on the wider button it is kept at two thirds of the style's
    // button width, centered
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget) const override
    {
        const bool arrow = element == PE_IndicatorArrowLeft || element == PE_IndicatorArrowRight ||
                           element == PE_IndicatorArrowUp || element == PE_IndicatorArrowDown;
        if (!arrow || !IsScrollButton(widget))
            return QProxyStyle::drawPrimitive(element, option, painter, widget);
        const int side = QProxyStyle::pixelMetric(PM_TabBarScrollButtonWidth, option, widget->parentWidget()) * 2 / 3;
        QRect square(0, 0, std::min(side, option->rect.width()), std::min(side, option->rect.height()));
        square.moveCenter(option->rect.center());
        if (const auto* tool = qstyleoption_cast<const QStyleOptionToolButton*>(option))
        {
            QStyleOptionToolButton shrunk(*tool);
            shrunk.rect = square;
            return QProxyStyle::drawPrimitive(element, &shrunk, painter, widget);
        }
        QStyleOption shrunk(*option);
        shrunk.rect = square;
        QProxyStyle::drawPrimitive(element, &shrunk, painter, widget);
    }

    QRect subElementRect(SubElement element, const QStyleOption* option, const QWidget* widget) const override
    {
        const auto* bar = qobject_cast<const QTabBar*>(widget);
        const bool arrow = element == SE_TabBarScrollLeftButton || element == SE_TabBarScrollRightButton;
        if (!bar || !IsHorizontal(bar) || !arrow)
            return QProxyStyle::subElementRect(element, option, widget);
        const QRect rect = option->rect;
        const int width = pixelMetric(PM_TabBarScrollButtonWidth, option, widget);
        const bool atLeft = (element == SE_TabBarScrollLeftButton) != (option->direction == Qt::RightToLeft);
        return QRect(atLeft ? rect.left() : rect.right() - width + 1, rect.top(), width, rect.height());
    }
};

class WheelSwitchesTabs : public QObject
{
public:
    explicit WheelSwitchesTabs(QTabBar* bar) : QObject(bar), _bar(bar) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched != _bar || event->type() != QEvent::Wheel)
            return false;
        const QPoint angle = static_cast<QWheelEvent*>(event)->angleDelta();
        _pending += std::abs(angle.y()) >= std::abs(angle.x()) ? angle.y() : angle.x();
        while (_pending <= -Notch || _pending >= Notch)
        {
            const int step = _pending < 0 ? 1 : -1;  // down / right: the next tab
            _pending += step * Notch;
            Switch(step);
        }
        event->accept();
        return true;
    }

private:
    static constexpr int Notch = 120;

    void Switch(int step)
    {
        for (int index = _bar->currentIndex() + step; index >= 0 && index < _bar->count(); index += step)
            if (_bar->isTabVisible(index) && _bar->isTabEnabled(index))
            {
                _bar->setCurrentIndex(index);
                return;
            }
    }

    QTabBar* _bar;
    int _pending = 0;
};
}  // namespace

void UseEndScrollArrows(QTabBar* bar, const char* baseStyle)
{
    bar->setElideMode(Qt::ElideNone);
    bar->setUsesScrollButtons(true);
    // No base: the proxy follows the application style; a named base belongs to the proxy
    auto* style = new EndScrollArrowsStyle(baseStyle ? QStyleFactory::create(QString::fromLatin1(baseStyle)) : nullptr);
    style->setParent(bar);
    bar->setStyle(style);
    // A widget's style does not reach its children: the bar's own scroll buttons get it too
    for (QToolButton* button : bar->findChildren<QToolButton*>(Qt::FindDirectChildrenOnly))
        button->setStyle(style);
}

void UseWheelToSwitchTabs(QTabBar* bar)
{
    bar->installEventFilter(new WheelSwitchesTabs(bar));
}
