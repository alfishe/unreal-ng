#pragma once

#include <QScrollBar>
#include <QString>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QWidget>

/// Updates a debugger pane makes on every stop, done only when the value changes. A style sheet set again (even the
/// same one) repolishes the widget: its style, font and size hint are computed again and its layout is invalidated,
/// which relays out and repaints the panes around it (debugger repaint cost, gs-debugger design §7.3)
namespace widgetupdates
{
inline void SetStyleSheet(QWidget* widget, const QString& styleSheet)
{
    if (widget && widget->styleSheet() != styleSheet)
        widget->setStyleSheet(styleSheet);
}

inline void SetToolTip(QWidget* widget, const QString& toolTip)
{
    if (widget && widget->toolTip() != toolTip)
        widget->setToolTip(toolTip);
}

/// A scroll bar that repaints on a value change only when its handle moves: a view that follows PC through the 64K
/// moves its value by a few bytes per step, which leaves the handle on the same pixels. Transient (overlay) bars keep
/// Qt's behavior, their flash on a scroll is drawn through that repaint
class QuietScrollBar : public QScrollBar
{
public:
    using QScrollBar::QScrollBar;

protected:
    void sliderChange(SliderChange change) override
    {
        const QRect handle = handleRect();
        const bool atEnd = value() == minimum() || value() == maximum() || _paintedValue == minimum() ||
                           _paintedValue == maximum();  // the arrows' enabled look follows the ends
        const bool quiet = change == SliderValueChange && handle == _painted && !atEnd && isVisible() &&
                           !style()->styleHint(QStyle::SH_ScrollBar_Transient, nullptr, this) &&
                           !testAttribute(Qt::WA_UpdatesDisabled);
        if (quiet)
            setAttribute(Qt::WA_UpdatesDisabled, true);  // not setUpdatesEnabled: that repaints when switched back
        QScrollBar::sliderChange(change);
        if (quiet)
            setAttribute(Qt::WA_UpdatesDisabled, false);
    }

    void paintEvent(QPaintEvent* event) override
    {
        _painted = handleRect();
        _paintedValue = value();
        QScrollBar::paintEvent(event);
    }

private:
    QRect handleRect() const
    {
        QStyleOptionSlider option;
        initStyleOption(&option);
        return style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSlider, this);
    }

    QRect _painted;          ///< the handle as last painted
    int _paintedValue = 0;  ///< the value then
};
}  // namespace widgetupdates
