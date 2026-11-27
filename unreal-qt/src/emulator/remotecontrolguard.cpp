#include "remotecontrolguard.h"

#include <QAbstractButton>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QAction>
#include <QActionEvent>
#include <QChildEvent>
#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDateTime>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QToolTip>
#include <QHexView/qhexview.h>
#include <QMenu>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QShortcut>
#include <QToolButton>
#include <QWidget>
#include <algorithm>
#include <climits>
#include <utility>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

namespace
{

/// A control the user acts with (the views - scroll bars, tabs, item views, text - stay: looking is not acting)
bool IsControl(QObject* object)
{
    if (qobject_cast<QAction*>(object))
        return !qobject_cast<QAction*>(object)->menu();  // a menu stays openable; its items are locked
    if (qobject_cast<QShortcut*>(object))
        return true;
    if (qobject_cast<QScrollBar*>(object))
        return false;
    return qobject_cast<QAbstractButton*>(object) || qobject_cast<QAbstractSlider*>(object) ||
           qobject_cast<QComboBox*>(object) || qobject_cast<QAbstractSpinBox*>(object);
}

bool IsEnabled(QObject* object)
{
    if (auto* action = qobject_cast<QAction*>(object))
        return action->isEnabled();
    if (auto* shortcut = qobject_cast<QShortcut*>(object))
        return shortcut->isEnabled();
    if (auto* widget = qobject_cast<QWidget*>(object))
        return widget->isEnabled();
    return false;
}

void SetEnabled(QObject* object, bool enabled)
{
    if (auto* action = qobject_cast<QAction*>(object))
        action->setEnabled(enabled);
    else if (auto* shortcut = qobject_cast<QShortcut*>(object))
        shortcut->setEnabled(enabled);
    else if (auto* widget = qobject_cast<QWidget*>(object))
        widget->setEnabled(enabled);
}

/// An editor that can turn read-only, and whether it is now
bool ReadOnlyOf(QObject* object, bool& readOnly)
{
    if (auto* edit = qobject_cast<QLineEdit*>(object))
        return readOnly = edit->isReadOnly(), true;
    if (auto* edit = qobject_cast<QTextEdit*>(object))
        return readOnly = edit->isReadOnly(), true;
    if (auto* edit = qobject_cast<QPlainTextEdit*>(object))
        return readOnly = edit->isReadOnly(), true;
    if (auto* hex = qobject_cast<QHexView*>(object))
        return readOnly = hex->isReadOnly(), true;
    return false;
}

void SetReadOnly(QObject* object, bool readOnly)
{
    if (auto* edit = qobject_cast<QLineEdit*>(object))
        edit->setReadOnly(readOnly);
    else if (auto* edit = qobject_cast<QTextEdit*>(object))
        edit->setReadOnly(readOnly);
    else if (auto* edit = qobject_cast<QPlainTextEdit*>(object))
        edit->setReadOnly(readOnly);
    else if (auto* hex = qobject_cast<QHexView*>(object))
        hex->setReadOnly(readOnly);
}

/// Keys that only look: move, page, select, copy, find, leave a dialog
bool IsLookingKey(QKeyEvent* key)
{
    switch (key->key())
    {
        case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_Left: case Qt::Key_Right:
        case Qt::Key_PageUp: case Qt::Key_PageDown: case Qt::Key_Home: case Qt::Key_End:
        case Qt::Key_Tab: case Qt::Key_Backtab: case Qt::Key_Escape:
        case Qt::Key_Shift: case Qt::Key_Control: case Qt::Key_Alt: case Qt::Key_Meta:
            return true;
        default:
            break;
    }
    return key->matches(QKeySequence::Copy) || key->matches(QKeySequence::SelectAll) ||
           key->matches(QKeySequence::Find) || key->matches(QKeySequence::FindNext);
}

}  // namespace

bool RemoteControlGuard::Refuse(QWidget* where)
{
    if (!Locked())
        return false;
    QToolTip::showText(QCursor::pos(),
                       tr("Under remote control: an automation session controls this emulator. Take control back "
                          "first (toolbar or Run menu)"),
                       where);
    return true;
}

RemoteControlGuard::RemoteControlGuard(QObject* parent) : QObject(parent)
{
    _clock.setSingleShot(true);
    connect(&_clock, &QTimer::timeout, this, &RemoteControlGuard::refresh);
}

RemoteControlGuard::~RemoteControlGuard()
{
    _active = false;
    unlock();
}

void RemoteControlGuard::setEmulator(std::shared_ptr<Emulator> emulator)
{
    _emulator = emulator;
    refresh();
}

void RemoteControlGuard::exempt(QObject* control)
{
    if (control)
        _exempt.insert(control);
}

bool RemoteControlGuard::InputBlocked(const Emulator* emulator)
{
    EmulatorContext* context = emulator ? const_cast<Emulator*>(emulator)->GetContext() : nullptr;
    return context && context->IsRunControlClaimed();
}

namespace
{
QString SurfaceTitle(const QString& surface)
{
    static const QList<QPair<QString, QString>> titles{
        {"webapi", "WebAPI"}, {"mcp", "MCP"}, {"cli", "CLI"}, {"gdb", "GDB"},
        {"dezog", "DeZog"}, {"lua", "Lua"}, {"python", "Python"}};
    for (const auto& [key, title] : titles)
        if (surface == key)
            return title;
    return surface;
}
}  // namespace

QString RemoteControlGuard::name() const
{
    if (!_active)
        return {};
    const QString surface = SurfaceTitle(_surface);
    if (_client.isEmpty())
        return surface;
    if (_client.contains(surface, Qt::CaseInsensitive))
        return _client;
    return tr("%1 via %2").arg(_client, surface);
}

QString RemoteControlGuard::who() const
{
    if (!_active)
        return {};
    return _peer.isEmpty() ? tr("%1 (a local script)").arg(name()) : tr("%1 from %2").arg(name(), _peer);
}

QString RemoteControlGuard::brief() const
{
    if (!_active)
        return {};
    QString host = _peer;
    const int colon = host.lastIndexOf(QLatin1Char(':'));
    if (colon > 0)
        host = host.left(colon);
    host.remove(QLatin1Char('[')).remove(QLatin1Char(']'));
    const QString where = host.isEmpty() ? tr("local") : host;
    return tr("Remote control: %1 \u00B7 %2 \u00B7").arg(name(), where);
}

QString RemoteControlGuard::describe() const
{
    if (!_active)
        return tr("No automation session");
    const QString since = QDateTime::fromMSecsSinceEpoch(_sinceMs).toString(QStringLiteral("HH:mm:ss"));
    if (_implicit)
        return tr("Remote control by %1 (implicit), since %2").arg(who(), since);
    return tr("Remote control by %1, since %2").arg(who(), since);
}

void RemoteControlGuard::refresh()
{
    std::shared_ptr<Emulator> emulator = _emulator.lock();
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    EmulatorContext::RunControlState state;
    if (context)
        state = context->GetRunControlState();

    const bool active = state.claimed;
    const QString surface = QString::fromStdString(state.surfaceLabel);
    const QString client = QString::fromStdString(state.client);
    const QString peer = QString::fromStdString(state.peer);
    const bool changed = active != _active || surface != _surface || client != _client || peer != _peer ||
                         static_cast<qint64>(state.sinceMs) != _sinceMs ||
                         _hasCooldowns != !state.cooldowns.empty();
    _active = active;
    _surface = active ? surface : QString();
    _client = active ? client : QString();
    _peer = active ? peer : QString();
    _sinceMs = active ? static_cast<qint64>(state.sinceMs) : 0;
    _implicit = active && state.implicit;
    _hasCooldowns = !state.cooldowns.empty();

    if (!_active)
        unlock();
    else if (!_locked)
        lock();  // the session started: the one walk over every window
    else
        flushQueued();  // what changed since is queued by the filter; nothing walks the windows again
    armClock(context);

    if (changed)
        emit sessionChanged(_active);
}

void RemoteControlGuard::takeBack()
{
    std::shared_ptr<Emulator> emulator = _emulator.lock();
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    if (!context || !_active)
        return;
    const QString taker = who();
    const bool revoked = context->RevokeRunControl("gui");
    // The guard's own state first (unlocked, the session gone), then the news: the bar shows the take-back over a
    // state that no longer has the session
    refresh();
    if (revoked)
        emit controlTakenBack(taker);
}

void RemoteControlGuard::allowAgain()
{
    std::shared_ptr<Emulator> emulator = _emulator.lock();
    if (EmulatorContext* context = emulator ? emulator->GetContext() : nullptr)
        context->AllowRunControlRetake();
    refresh();
}

void RemoteControlGuard::armClock(const EmulatorContext* context)
{
    // An idle lease running out and a cooldown ending post nothing by themselves: the query at that moment does
    const std::optional<std::chrono::milliseconds> next =
        context != nullptr && (_active || _hasCooldowns) ? context->RunControlTimeToChange() : std::nullopt;
    if (next)
        _clock.start(static_cast<int>(std::min<long long>(next->count(), INT_MAX)));
    else
        _clock.stop();
}

void RemoteControlGuard::lock()
{
    s_locked.store(true, std::memory_order_relaxed);
    if (!_filtering && qApp)
    {
        qApp->installEventFilter(this);
        _filtering = true;
    }
    _locked = true;
    _queued.clear();  // all of it is looked at now
    const QScopedValueRollback<bool> pass(_inPass, true);
    // Every window: the main one, the debugger, its dialogs, the floating panels
    for (QWidget* window : QApplication::topLevelWidgets())
        lockTree(window);
}

void RemoteControlGuard::queue(QObject* object, bool tree)
{
    if (object == nullptr || object == this || _inPass)
        return;  // the guard's own changes are no news to it
    _queued.append({QPointer<QObject>(object), tree});
    if (_flushPosted)
        return;
    _flushPosted = true;
    QMetaObject::invokeMethod(this, &RemoteControlGuard::flushQueued, Qt::QueuedConnection);
}

void RemoteControlGuard::flushQueued()
{
    _flushPosted = false;
    const QList<std::pair<QPointer<QObject>, bool>> queued = std::exchange(_queued, {});
    if (!_active || !_locked)
        return;
    const QScopedValueRollback<bool> pass(_inPass, true);
    QSet<QObject*> done;
    for (const auto& [object, tree] : queued)
    {
        // Created, then already gone; or looked at here already (a child of an object added with its children)
        if (!object || done.contains(object.data()))
            continue;
        done.insert(object.data());
        lockObject(object.data());
        if (tree)
            for (QObject* child : object->findChildren<QObject*>())
                if (!done.contains(child))
                {
                    done.insert(child);
                    lockObject(child);
                }
    }
}

bool RemoteControlGuard::IsExempt(QObject* object) const
{
    for (QObject* o = object; o; o = o->parent())
    {
        if (_exempt.contains(o))
            return true;
        // A toolbar's button of an exempt action stays with it
        if (auto* button = qobject_cast<QToolButton*>(o))
            if (button->defaultAction() && _exempt.contains(button->defaultAction()))
                return true;
    }
    return false;
}

void RemoteControlGuard::lockTree(QWidget* root)
{
    lockObject(root);
    for (QObject* child : root->findChildren<QObject*>())
        lockObject(child);
}

void RemoteControlGuard::lockObject(QObject* object)
{
    ++_visited;
    if (IsExempt(object))
        return;
    bool readOnly = false;
    if (ReadOnlyOf(object, readOnly))
    {
        if (!readOnly)
        {
            SetReadOnly(object, true);
            _readOnlyByUs.insert(object, QPointer<QObject>(object));
        }
        return;
    }
    if (auto* view = qobject_cast<QAbstractItemView*>(object))
    {
        if (view->editTriggers() != QAbstractItemView::NoEditTriggers)
        {
            // The window's latest triggers come back (it set them again during the session)
            _editTriggersByUs.insert(view, {QPointer<QAbstractItemView>(view), view->editTriggers()});
            view->setEditTriggers(QAbstractItemView::NoEditTriggers);
        }
        return;
    }
    if (!IsControl(object) || !IsEnabled(object))
        return;
    SetEnabled(object, false);
    _lockedByUs.insert(object, QPointer<QObject>(object));
}

bool RemoteControlGuard::eventFilter(QObject* watched, QEvent* event)
{
    if (!_active)
        return false;
    // What may undo the lock or bring a new control: queued, locked at the next turn of the event loop (changing a
    // control inside its own notification is not done)
    switch (event->type())
    {
        case QEvent::EnabledChange:   // a window enabled a control again (its state refresh after a step or a stop)
        case QEvent::ReadOnlyChange:  // ... or an editor
            if (watched->isWidgetType())
                queue(watched, false);
            return false;
        case QEvent::ActionChanged:  // an action enabled again (sent to the action and to the widgets showing it)
        case QEvent::ActionAdded:    // an action put in a menu, a toolbar, a widget
            queue(static_cast<QActionEvent*>(event)->action(), false);
            return false;
        case QEvent::ChildAdded:  // an object created or re-parented: looked at once it is whole
            queue(static_cast<QChildEvent*>(event)->child(), true);
            return false;
        default:
            break;
    }
    if (!watched->isWidgetType())
        return false;
    switch (event->type())
    {
        case QEvent::Show:
            // A dialog or a context menu shown during the session is locked before the user can act in it
            if (static_cast<QWidget*>(watched)->isWindow())
            {
                const QScopedValueRollback<bool> pass(_inPass, true);
                lockTree(static_cast<QWidget*>(watched));
            }
            return false;
        case QEvent::MouseButtonDblClick:
        case QEvent::DragEnter:
        case QEvent::DragMove:
        case QEvent::Drop:
            if (IsExempt(watched))
                return false;
            event->ignore();
            return true;
        case QEvent::KeyPress:
        {
            auto* key = static_cast<QKeyEvent*>(event);
            if (IsExempt(watched) || IsLookingKey(key))
                return false;
            // In a menu Return / Space pick an item: only the exempt ones are enabled
            if (qobject_cast<QMenu*>(watched) &&
                (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Space))
                return false;
            return true;
        }
        default:
            return false;
    }
}

void RemoteControlGuard::unlock()
{
    s_locked.store(false, std::memory_order_relaxed);
    if (_filtering && qApp)
    {
        qApp->removeEventFilter(this);
        _filtering = false;
    }
    _locked = false;
    _queued.clear();
    const auto locked = std::exchange(_lockedByUs, {});
    for (const QPointer<QObject>& object : locked)
        if (object)
            SetEnabled(object, true);
    const auto readOnly = std::exchange(_readOnlyByUs, {});
    for (const QPointer<QObject>& object : readOnly)
        if (object)
            SetReadOnly(object, false);
    const auto triggers = std::exchange(_editTriggersByUs, {});
    for (const auto& [view, previous] : triggers)
        if (view)
            view->setEditTriggers(previous);
}

#include "moc_remotecontrolguard.cpp"
