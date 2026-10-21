#pragma once

/// @file surfacetargets.h
/// @brief The main window as a view of the core's TargetState (Stream Deck integration targeting.md): it registers
/// the window, reports the instance its binding shows as the view's active instance, and moves the view to the front
/// of the focus order when one of the application's windows gets the focus (the main window or one of its tool
/// windows: they all show the same instance). Focus moving to another program changes nothing (rule 2).
///
/// It also gives the GUI the surface sessions (a Stream Deck, a REST surface) with their targets, and pins them: the
/// status bar's surface icon ("Pin here" / "Unpin", T-2) and the surfaces table of the controllers window.
/// changed() is emitted on the GUI thread after every NC_SURFACE_TARGET.

#include <QObject>
#include <QString>
#include <vector>

#include "emulator/actions/targetstate.h"

class EmulatorBinding;
class QWindow;

class SurfaceTargets : public QObject
{
    Q_OBJECT

public:
    SurfaceTargets(EmulatorBinding* binding, QString name, QObject* parent = nullptr);
    ~SurfaceTargets() override;

    /// This window's view id ("v1")
    QString viewId() const;
    /// Every surface session with its target now
    std::vector<actions::SurfaceSessionInfo> sessions() const;
    /// The sessions whose target is this view now (following it or pinned to it)
    std::vector<actions::SurfaceSessionInfo> sessionsHere() const;
    /// Every view in focus order (the surfaces table offers them as pins)
    std::vector<actions::ViewInfo> views() const;

    /// Pin a session to this view, or to a pin of its choice; "" on success, else why not
    QString pinHere(const QString& session);
    QString pin(const QString& session, const actions::Pin& pin);
    QString unpin(const QString& session);

    /// The application got the focus inside this host: the view moves to the front (tests call it directly)
    void activated();

signals:
    void changed();

private:
    void onBound();
    void onUnbound();
    void onFocusWindowChanged(QWindow* window);

    EmulatorBinding* _binding = nullptr;
    actions::ViewHandle _view;
    uint64_t _observer = 0;
};
