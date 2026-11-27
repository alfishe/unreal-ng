#pragma once

#include <QEvent>
#include <QObject>
#include <QPalette>
#include <QWidget>

/// An information window (Video Synchronization, MIDI Activity, network traffic, a controller's test plots) looks
/// the same whether it is the active window or not: the platform's inactive colors (macOS: Highlight turns gray,
/// accents fade) are the active ones. Readouts watched beside the emulator window are never the active window, and
/// there they went gray: the "Steady" pill, the frames graph, the lit keys.
/// KeepActiveColors(widget) sets the widget's inactive color group to its active one, and again on each palette change
/// (a theme switch). Children under a style sheet take their palette from the style, not from the window: what such a
/// window paints itself names the group (palette().color(QPalette::Active, ...))
namespace activecolors
{
inline bool Copy(QWidget* widget)
{
    QPalette palette = widget->palette();
    bool changed = false;
    for (int role = 0; role < QPalette::NColorRoles; role++)
    {
        const auto r = static_cast<QPalette::ColorRole>(role);
        const QBrush& active = palette.brush(QPalette::Active, r);
        if (palette.brush(QPalette::Inactive, r) != active)
        {
            palette.setBrush(QPalette::Inactive, r, active);
            changed = true;
        }
    }
    if (changed)
        widget->setPalette(palette);  // the palette change it sends finds the groups equal and stops
    return changed;
}

class Keeper final : public QObject
{
public:
    explicit Keeper(QWidget* widget) : QObject(widget) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::PaletteChange)
            if (auto* widget = qobject_cast<QWidget*>(watched))
                Copy(widget);
        return QObject::eventFilter(watched, event);
    }
};
}  // namespace activecolors

inline void KeepActiveColors(QWidget* widget)
{
    activecolors::Copy(widget);
    widget->installEventFilter(new activecolors::Keeper(widget));
}
