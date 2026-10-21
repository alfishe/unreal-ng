#pragma once

/// @file surfacestatusicon.h
/// @brief The status bar's surface icon (Stream Deck integration targeting.md §6, T-2): lit while a control surface
/// (a Stream Deck, a REST surface) drives this window's instance, its tooltip names them; a click opens a menu with
/// every surface and "Pin here" / "Unpin". Hidden while no surface is connected.

#include <QPointer>

#include "widgets/statusindicator.h"

class QMenu;
class SurfaceTargets;

class SurfaceStatusIcon : public StatusIndicator
{
    Q_OBJECT

public:
    SurfaceStatusIcon(SurfaceTargets* targets, QWidget* parent = nullptr);

    /// Rebuild from TargetState (on every change; tests call it directly)
    void refresh();
    /// The menu a click opens, filled now (tests)
    QMenu* buildMenu(QWidget* parent);

private:
    void showMenu();

    QPointer<SurfaceTargets> _targets;
};
