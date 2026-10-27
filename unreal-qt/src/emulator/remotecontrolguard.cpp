#include "remotecontrolguard.h"

#include <QAbstractButton>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QAction>
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
#include <QScrollBar>
#include <QShortcut>
#include <QToolButton>
#include <QWidget>
#include <utility>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

namespace
{

/// The relock / lease poll while a session holds the machine (or a cooldown runs)
constexpr int kRelockIntervalMs = 250;

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
    _relock.setInterval(kRelockIntervalMs);
    connect(&_relock, &QTimer::timeout, this, &RemoteControlGuard::refresh);
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

    if (_active)
        lock();  // also locks what was enabled since the last pass
    else
        unlock();

    if (_active || _hasCooldowns)
    {
        if (!_relock.isActive())
            _relock.start();
    }
    else
        _relock.stop();

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

void RemoteControlGuard::lock()
{
    s_locked.store(true, std::memory_order_relaxed);
    if (!_filtering && qApp)
    {
        qApp->installEventFilter(this);
        _filtering = true;
    }
    // Every window: the main one, the debugger, its dialogs, the floating panels
    for (QWidget* window : QApplication::topLevelWidgets())
        lockTree(window);
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
    QList<QObject*> candidates;
    candidates.append(root);
    for (QObject* child : root->findChildren<QObject*>())
        candidates.append(child);
    for (QObject* object : candidates)
    {
        if (IsExempt(object))
            continue;
        bool readOnly = false;
        if (ReadOnlyOf(object, readOnly))
        {
            if (!readOnly)
            {
                SetReadOnly(object, true);
                _readOnlyByUs.append(QPointer<QObject>(object));
            }
            continue;
        }
        if (auto* view = qobject_cast<QAbstractItemView*>(object))
        {
            if (view->editTriggers() != QAbstractItemView::NoEditTriggers)
            {
                _editTriggersByUs.append({QPointer<QAbstractItemView>(view), view->editTriggers()});
                view->setEditTriggers(QAbstractItemView::NoEditTriggers);
            }
            continue;
        }
        if (!IsControl(object) || !IsEnabled(object))
            continue;
        SetEnabled(object, false);
        _lockedByUs.append(QPointer<QObject>(object));
    }
}

bool RemoteControlGuard::eventFilter(QObject* watched, QEvent* event)
{
    if (!_active || !watched->isWidgetType())
        return false;
    switch (event->type())
    {
        case QEvent::Show:
            // A dialog or a context menu shown during the session is locked before the user can act in it
            if (static_cast<QWidget*>(watched)->isWindow())
                lockTree(static_cast<QWidget*>(watched));
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
    const QList<QPointer<QObject>> locked = std::exchange(_lockedByUs, {});
    for (const QPointer<QObject>& object : locked)
        if (object)
            SetEnabled(object, true);
    const QList<QPointer<QObject>> readOnly = std::exchange(_readOnlyByUs, {});
    for (const QPointer<QObject>& object : readOnly)
        if (object)
            SetReadOnly(object, false);
    const auto triggers = std::exchange(_editTriggersByUs, {});
    for (const auto& [view, previous] : triggers)
        if (view)
            view->setEditTriggers(previous);
}
