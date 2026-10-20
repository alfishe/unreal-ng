#include "remotecontrolrow.h"

#include <QAction>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

#include "widgets/inkcentre.h"

RemoteControlRow BuildRemoteControlRow(QToolBar* bar, const QList<QAction*>& actions)
{
    RemoteControlRow r;
    // One row widget with its own layout: the icon, the line and the buttons share one vertical centre (a
    // toolbar's own layout places each item by itself)
    r.row = new QWidget(bar);
    r.row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* layout = new QHBoxLayout(r.row);
    layout->setContentsMargins(6, 0, 4, 0);
    layout->setSpacing(6);
    // The line in the system's fixed-pitch font (every platform has one): the ticking time keeps its width; 14 %
    // above the toolbar's text, read at a glance
    const QFont buttonFont = bar->font();  // the buttons keep the toolbar's own font
    QFont lineFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    lineFont.setPointSizeF(buttonFont.pointSizeF() * 1.14);
    r.icon = new QLabel(r.row);
    r.label = new QLabel(r.row);
    r.label->setTextFormat(Qt::PlainText);
    r.label->setFont(lineFont);
    // Centred on the letters' ink, not on the fixed-pitch font's line box: on the axis of the icon and the buttons
    r.label->setContentsMargins(InkCentringMargins(lineFont, QStringLiteral("REMOTE CONTROL: 00:00:00")));
    // The line takes the row's whole height and centres its text itself: a shorter label placed by the layout
    // lands half a pixel high whenever the heights differ by an odd number (the layout rounds down)
    r.label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    r.label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(r.icon, 0, Qt::AlignVCenter);
    layout->addWidget(r.label);
    layout->addStretch(1);
    for (QAction* action : actions)
    {
        auto* button = new QToolButton(r.row);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setFont(buttonFont);
        // The caption on its ink too (Segoe UI's line box puts the capitals a pixel low): the bar style's vertical
        // padding (2 px) plus the centring margins; the button's own sheet wins over the bar's
        const QMargins caption = InkCentringMargins(buttonFont, action->text().toUpper());
        if (!caption.isNull())
            button->setStyleSheet(QStringLiteral("QToolButton { padding-top: %1px; padding-bottom: %2px; }")
                                      .arg(2 + caption.top())
                                      .arg(2 + caption.bottom()));
        layout->addWidget(button, 0, Qt::AlignVCenter);
        r.buttons.append({button, action});
    }
    bar->addWidget(r.row);
    return r;
}

QString RemoteControlBarStyle(bool active)
{
    const QString buttons = QStringLiteral(
        "QToolButton { border: 1px solid palette(mid); border-radius: 4px; background: palette(button); "
        "color: palette(button-text); padding: 2px 12px; margin: 3px 4px; }");
    if (active)
        return QStringLiteral("QToolBar#remoteControlBar { background: palette(highlight); border: none; padding: 2px; } "
                              "QLabel { color: palette(highlighted-text); } ") +
               buttons;
    return QStringLiteral("QToolBar#remoteControlBar { background: palette(window); "
                          "border-bottom: 1px solid palette(mid); padding: 2px; } ") +
           buttons;
}
