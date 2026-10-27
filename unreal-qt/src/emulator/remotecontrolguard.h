#pragma once

#include <QAbstractItemView>
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
/// The guard gives back only what it changed, so the windows' own rules stay. A window that re-enables a control
/// (its state refresh) is locked again by the next pass (a short timer while locked, which also notices an idle
/// session's lease running out).
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
    QList<QPointer<QObject>> _lockedByUs;
    QList<QPointer<QObject>> _readOnlyByUs;
    QList<std::pair<QPointer<QAbstractItemView>, QAbstractItemView::EditTriggers>> _editTriggersByUs;
    bool _filtering = false;
    QTimer _relock;
};
