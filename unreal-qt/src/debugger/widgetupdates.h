#pragma once

#include <QString>
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
}  // namespace widgetupdates
