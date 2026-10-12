#pragma once

/// @file remotecontrolrow.h
/// @brief The remote control bar's content (automation sessions in the GUI, execution arbiter X7c): one row with the
/// icon, the line ("Remote control: who · where · time") and the buttons, all on one vertical axis, and the bar's
/// style sheet. ToolBarManager owns the bar; the row is built here so that a test can build the same one.

#include <QList>
#include <QPair>
#include <QString>

class QAction;
class QLabel;
class QToolBar;
class QToolButton;
class QWidget;

struct RemoteControlRow
{
    QWidget* row = nullptr;
    QLabel* icon = nullptr;
    QLabel* label = nullptr;
    QList<QPair<QToolButton*, QAction*>> buttons;  // one per action, in order
};

/// Builds the row into `bar` (added to it): the line in the system's fixed-pitch font, 14 % above the bar's text,
/// the buttons in the bar's font
RemoteControlRow BuildRemoteControlRow(QToolBar* bar, const QList<QAction*>& actions);

/// The bar's style sheet: the accent (the system's selection colour) while a session holds the machine, the window's
/// colour after a take-back
QString RemoteControlBarStyle(bool active);
