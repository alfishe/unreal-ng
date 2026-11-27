#pragma once

#include <QAbstractItemView>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>
#include <atomic>
#include <memory>
#include <utility>

class Emulator;
class EmulatorContext;
class QShortcut;

/// @brief The GUI side of automation sessions (execution arbiter design §6a, X7c).
///
/// While an automation session (any surface's run-control claim: WebAPI, MCP, CLI, Python, Lua, GDB) holds the
/// active emulator, the user sees it and cannot interfere: every control of every window - actions, buttons,
/// sliders, combo and spin boxes, shortcuts - is disabled except the exempt ones (take back control, full screen,
/// exit), and the machine gets no keys or mouse from the GUI (InputBlocked). The user can take control back
/// (TakeBack): the session ends, its run is aborted, its client is told revoked_by_user and refused a re-take for
/// the cooldown unless the user allows it again (AllowAgain).
///
/// The lock cascades to every widget of every window, the debugger's included:
/// - controls (actions, shortcuts, buttons, sliders, combo / spin boxes) are disabled;
/// - editors (line / text edits, hex views) turn read-only, item views stop editing - scrolling, selecting and
///   copying stay: looking is not acting;
/// - an application event filter swallows what custom widgets act on: double clicks, keys other than navigation /
///   copy / find, drops;
/// - a window shown during the session (a dialog, a context menu) is locked as it shows;
/// - the places that change the machine's breakpoints, labels, ports, sectors check Locked() themselves (Refuse).
/// The guard gives back only what it changed, so the windows' own rules stay.
///
/// Cost: the whole walk over every window runs once, when a session starts; nothing walks the windows again while it
/// lasts, a step or a stop under the session costs the guard nothing. What changes later reaches it as events through
/// the application filter: a control a window enables again (its state refresh after a step: EnabledChange,
/// ActionChanged, ReadOnlyChange), an object created or re-parented (ChildAdded), an action added to a widget
/// (ActionAdded), a window shown. Those are queued and locked at the next turn of the event loop, before the user can
/// act. The session itself changes by NC_RUN_CONTROL_CHANGED (MainWindow calls refresh()); what changes with time alone
/// (an idle lease running out, a re-take cooldown ending) is a one-shot timer at RunControlTimeToChange.
class RemoteControlGuard : public QObject
{
    Q_OBJECT

public:
    explicit RemoteControlGuard(QObject* parent = nullptr);
    ~RemoteControlGuard() override;

    void setEmulator(std::shared_ptr<Emulator> emulator);

    /// Never locked: the take-back affordances themselves and whatever only changes the view
    void exempt(QObject* control);

    /// The session that holds the active emulator, if any
    bool active() const { return _active; }
    QString surface() const { return _surface; }
    QString client() const { return _client; }
    /// Where the client connects from ("192.168.1.5:51234"), empty for an in-process script
    QString peer() const { return _peer; }
    /// Who holds control, short: "DeZog (ZRCP)", "claude-code 2.1.0 via MCP", "demo.lua via Lua"
    QString name() const;
    /// ... and from where: "DeZog (ZRCP) from 192.168.1.5:51234", "demo.lua via Lua (a local script)"
    QString who() const;
    qint64 sinceMs() const { return _sinceMs; }
    /// The automation gate opened the session (the client drove the machine without taking control)
    bool implicit() const { return _implicit; }
    /// Clients the user took control back from that may not take it again yet
    bool hasCooldowns() const { return _hasCooldowns; }
    /// "Remote control by GDB from 192.168.1.5:51234, since 12:03:04"
    QString describe() const;
    /// The bar's line, short enough to fit: "Remote control: DeZog (ZRCP) · 127.0.0.1 ·" (the bar adds how long)
    QString brief() const;

    /// The machine takes no keys or mouse from the GUI: an automation session holds it
    static bool InputBlocked(const Emulator* emulator);
    /// An automation session holds the GUI's machine: the GUI changes nothing (breakpoints, labels, memory, ports)
    static bool Locked() { return s_locked.load(std::memory_order_relaxed); }
    /// For a place that changes the machine: true (refused, the user told why near `where`) while Locked()
    static bool Refuse(QWidget* where);

    /// The objects the lock looked at so far (the cost's measure: the tests count them)
    quint64 visited() const { return _visited; }

public slots:
    /// Re-read the session, lock or unlock, and announce a change (sessionChanged)
    void refresh();
    /// The user takes control back: the session ends, its client is told revoked_by_user
    void takeBack();
    /// The user lets the clients in the cooldown take control again
    void allowAgain();

signals:
    /// The session started, ended or changed hands (also after a take-back): the toolbar and the HUD follow
    void sessionChanged(bool active);
    /// The user took control back from `client` (surface): the toast
    void controlTakenBack(const QString& who);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void lock();
    void lockTree(QWidget* root);
    void lockObject(QObject* object);
    /// Lock @p object at the next turn of the event loop (@p tree: with its children, a widget added with them)
    void queue(QObject* object, bool tree);
    void flushQueued();
    /// The one-shot timer for what changes with time alone (an idle lease, a re-take cooldown)
    void armClock(const EmulatorContext* context);
    void unlock();
    bool IsExempt(QObject* object) const;

    static inline std::atomic<bool> s_locked{false};

    std::weak_ptr<Emulator> _emulator;
    bool _active = false;
    QString _surface;
    QString _client;
    QString _peer;
    bool _implicit = false;   // the automation gate opened the session: the client drove without taking control
    qint64 _sinceMs = 0;
    bool _hasCooldowns = false;

    QSet<QObject*> _exempt;
    // What the guard changed, by object: a control a window enabled again and the guard locked again is kept once
    QHash<QObject*, QPointer<QObject>> _lockedByUs;
    QHash<QObject*, QPointer<QObject>> _readOnlyByUs;
    QHash<QObject*, std::pair<QPointer<QAbstractItemView>, QAbstractItemView::EditTriggers>> _editTriggersByUs;
    bool _filtering = false;
    bool _locked = false;  // the session's walk over every window ran
    QList<std::pair<QPointer<QObject>, bool>> _queued;  // object, with its children
    bool _flushPosted = false;
    bool _inPass = false;  // the guard itself changes controls: their notifications are not queued
    quint64 _visited = 0;
    QTimer _clock{this};
};
