#include "guiactions.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <utility>

#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/execution/automationgate.h"
#include "controllers/hostcontrollers.h"
#include "profiles/json.h"
#include "router/analog.h"

namespace
{
QString Tr(const char* text)
{
    return QObject::tr(text);
}

/// A binding's parameters (its `curve` object, JSON) as the action's
StateNode ToNode(const controllers::JsonValue& value)
{
    switch (value.GetType())
    {
        case controllers::JsonValue::Type::Bool: return StateNode(value.AsBool());
        case controllers::JsonValue::Type::Number: return StateNode(value.AsNumber());
        case controllers::JsonValue::Type::String: return StateNode(value.AsString());
        case controllers::JsonValue::Type::Array:
        {
            StateNode node = StateNode::Array();
            for (const controllers::JsonValue& item : value.Items())
                node.push(ToNode(item));
            return node;
        }
        case controllers::JsonValue::Type::Object:
        {
            StateNode node = StateNode::Object();
            for (const auto& [key, member] : value.GetMembers())
                node[key] = ToNode(member);
            return node;
        }
        case controllers::JsonValue::Type::Null: break;
    }
    return {};
}

StateNode ParamsNode(const std::string& json)
{
    controllers::JsonValue value;
    std::string error;
    if (!controllers::ParseJson(json, value, error) || !value.IsObject())
        return StateNode::Object();
    return ToNode(value);
}

/// Run `fn` on the GUI thread while `alive` holds (the application object outlives every window)
void PostToGui(const std::shared_ptr<std::atomic<bool>>& alive, std::function<void()> fn)
{
    QCoreApplication* app = QCoreApplication::instance();
    if (app == nullptr)
        return;
    QMetaObject::invokeMethod(
        app,
        [alive, fn = std::move(fn)] {
            if (alive->load())
                fn();
        },
        Qt::QueuedConnection);
}
}  // namespace

/// ttd.scrub_back / ttd.scrub_forward (debug-and-ttd.md §3.2): each level is a speed on the scrub curve, both held
/// count against each other; a 20 ms timer turns the frames the speed covered into a ttd.jog_frames call of the core
/// registry (its actions worker seeks, the GUI thread never waits; jogs queued behind a slow seek merge). It stops on
/// the release, and at either end of the history or on a refusal, with the reason
class GuiActions::Scrub
{
public:
    Scrub(std::shared_ptr<std::atomic<bool>> alive, std::function<void(const QString&)> notice)
        : _alive(std::move(alive)), _notice(std::move(notice))
    {
        _timer.setInterval(20);
        QObject::connect(&_timer, &QTimer::timeout, [this] { Tick(); });
    }

    QString Set(bool forward, float level, const std::string& instanceId, const actions::ActionOrigin& origin)
    {
        (forward ? _forward : _back) = level;
        _instanceId = instanceId;
        _origin = origin;
        if (_back == 0 && _forward == 0)
        {
            Stop();
            return {};
        }
        if (!_timer.isActive())
        {
            _owed = 0;
            _clock.start();
            _timer.start();
        }
        return {};
    }

private:
    void Stop()
    {
        _timer.stop();
        _owed = 0;
    }

    void Tick()
    {
        const double seconds = static_cast<double>(_clock.restart()) / 1000.0;
        _owed += (controllers::ScrubSpeed(_forward) - controllers::ScrubSpeed(_back)) * seconds;
        const auto frames = static_cast<int64_t>(_owed);  // toward zero: the rest waits for the next tick
        if (frames == 0)
            return;
        _owed -= static_cast<double>(frames);
        actions::ActionValue value;
        value.kind = actions::ValueKind::Delta;
        value.delta = static_cast<double>(frames);
        value.rate = static_cast<double>(frames) / std::max(seconds, 0.001);
        auto alive = _alive;
        actions::ActionRegistry::Instance().Submit(
            "ttd.jog_frames", std::move(value), _instanceId, _origin, [this, alive](actions::ActionResult result) {
                if (result.ok)
                    return;
                // Held on: it starts again with the next change of the level
                const QString why = QString::fromStdString(result.message);
                PostToGui(alive, [this, why] {
                    Stop();
                    if (_notice)
                        _notice(Tr("Scrub stopped: %1").arg(why));
                });
            });
    }

    std::shared_ptr<std::atomic<bool>> _alive;
    std::function<void(const QString&)> _notice;
    QTimer _timer;
    QElapsedTimer _clock;
    float _back = 0;
    float _forward = 0;
    double _owed = 0;  ///< frames covered and not moved yet (back < 0)
    std::string _instanceId;
    actions::ActionOrigin _origin;
};

GuiActions::GuiActions(EmulatorBinding* binding, GuiActionHooks hooks, QObject* parent)
    : QObject(parent), _binding(binding), _hooks(std::move(hooks)), _alive(std::make_shared<std::atomic<bool>>(true))
{
    _scrub = std::make_shared<Scrub>(_alive, _hooks.notice);
    using actions::ValueKind;

    // 3.2 Time travel: the analog scrub (a trigger's level, `curve: scrub`; a button press is the full level)
    const QString ttd = Tr("Time travel");
    actions::ActionRules rewind;
    rewind.scope = actions::ActionRules::Fixed(automation_gate::ScopeRewind);
    // Weak and raw references only: an executor may be dropped on an actions worker, the objects stay on this thread
    std::weak_ptr<Scrub> scrub = _scrub;
    for (const bool forward : {false, true})
        Add(forward ? "ttd.scrub_forward" : "ttd.scrub_back", forward ? Tr("Scrub forward") : Tr("Scrub back"), ttd,
            ValueKind::Level,
            [scrub, forward](const std::string& instance, const actions::ActionValue& value,
                             const actions::ActionOrigin& origin) -> QString {
                std::shared_ptr<Scrub> live = scrub.lock();
                return live ? live->Set(forward, static_cast<float>(value.level), instance, origin) : QString();
            },
            rewind);

    // 3.3 Debugger UI
    const QString debug = Tr("Debugger");
    QObject* debugger = _hooks.debugger;
    std::function<void()> showDebugger = _hooks.showDebugger;
    Add("dbg.open", Tr("Open the debugger"), debug, ValueKind::Press,
        [showDebugger](const std::string&, const actions::ActionValue&, const actions::ActionOrigin&) -> QString {
            if (showDebugger)
                showDebugger();
            return {};
        });
    // Pane focus opens a closed debugger first; the cursor keys go to the pane in focus and repeat while held
    auto pane = [debugger, showDebugger](const char* name) -> GuiRun {
        return [debugger, showDebugger, name](const std::string&, const actions::ActionValue&,
                                              const actions::ActionOrigin&) -> QString {
            auto* window = qobject_cast<QWidget*>(debugger);
            if (window == nullptr)
                return Tr("the debugger is not available");
            if (!window->isVisible() && showDebugger)
                showDebugger();
            QMetaObject::invokeMethod(window, name);
            return {};
        };
    };
    auto key = [this, debugger](int code, bool repeats) -> GuiRun {
        auto* timer = new QTimer(this);  // the repeat; this object owns it
        auto send = [debugger, code] {
            if (debugger != nullptr)
                QMetaObject::invokeMethod(debugger, "paneKey", Q_ARG(int, code));
        };
        QObject::connect(timer, &QTimer::timeout, [timer, send] {
            timer->setInterval(60);
            send();
        });
        return [timer, send, repeats](const std::string&, const actions::ActionValue& value,
                                      const actions::ActionOrigin&) -> QString {
            timer->stop();
            if (!value.pressed)
                return {};
            send();
            if (repeats)
                timer->start(300);
            return {};
        };
    };
    actions::ActionRules noDebugger;
    noDebugger.unavailable = [debugger](Emulator&, const actions::ActionValue&) {
        return debugger == nullptr ? std::string("the debugger is not available") : std::string();
    };
    Add("dbg.focus_next_pane", Tr("Next debugger pane"), debug, ValueKind::Press, pane("focusNextPane"), noDebugger);
    Add("dbg.focus_prev_pane", Tr("Previous debugger pane"), debug, ValueKind::Press, pane("focusPrevPane"),
        noDebugger);
    Add("dbg.cursor_up", Tr("Cursor up in the pane"), debug, ValueKind::Press, key(Qt::Key_Up, true), noDebugger, true);
    Add("dbg.cursor_down", Tr("Cursor down in the pane"), debug, ValueKind::Press, key(Qt::Key_Down, true), noDebugger,
        true);
    Add("dbg.cursor_left", Tr("Cursor left in the pane"), debug, ValueKind::Press, key(Qt::Key_Left, true), noDebugger,
        true);
    Add("dbg.cursor_right", Tr("Cursor right in the pane"), debug, ValueKind::Press, key(Qt::Key_Right, true),
        noDebugger, true);
    Add("dbg.page_up", Tr("Page up in the pane"), debug, ValueKind::Press, key(Qt::Key_PageUp, true), noDebugger, true);
    Add("dbg.page_down", Tr("Page down in the pane"), debug, ValueKind::Press, key(Qt::Key_PageDown, true), noDebugger,
        true);
    Add("dbg.memory_follow", Tr("Memory follows the value under the cursor"), debug, ValueKind::Press,
        key(Qt::Key_Return, false), noDebugger, true);
    // An execution breakpoint at the disassembly cursor (dbg.breakpoint_toggle_pc, the core's, takes the PC)
    Add("dbg.breakpoint_toggle_cursor", Tr("Toggle a breakpoint at the cursor"), debug, ValueKind::Press,
        [debugger, showDebugger](const std::string&, const actions::ActionValue&,
                                 const actions::ActionOrigin&) -> QString {
            auto* window = qobject_cast<QWidget*>(debugger);
            if (window == nullptr)
                return Tr("the debugger is not available");
            if (!window->isVisible() && showDebugger)
                showDebugger();
            QMetaObject::invokeMethod(window, "toggleBreakpointAtCursor");
            return {};
        },
        noDebugger);

    // 3.4 Capture: the screenshot to the clipboard (the main window's)
    const QString machine = Tr("Capture and machine");
    std::function<void()> screenshot = _hooks.screenshot;
    Add("cap.screenshot", Tr("Screenshot"), machine, ValueKind::Press,
        [screenshot](const std::string&, const actions::ActionValue&, const actions::ActionOrigin&) -> QString {
            if (screenshot)
                screenshot();
            return {};
        });

    // The on-screen keyboard window (the main window's)
    std::function<void()> toggleKeyboard = _hooks.toggleKeyboard;
    Add("osd.keyboard", Tr("On-screen keyboard"), machine, ValueKind::Press,
        [toggleKeyboard](const std::string&, const actions::ActionValue&, const actions::ActionOrigin&) -> QString {
            if (toggleKeyboard)
                toggleKeyboard();
            return {};
        });

    // Stubs of the catalogue: a binding to one says why instead of doing nothing
    actions::ActionRules later;
    later.unavailable = [](Emulator&, const actions::ActionValue&) { return std::string("not available yet"); };
    later.unavailableError = "Not Implemented";
    for (const char* id : {"dbg.labels_toggle"})
        Add(id, QString::fromLatin1(id), debug, ValueKind::Press, nullptr, later);
    for (const char* id : {"osd.quick_menu"})
        Add(id, QString::fromLatin1(id), machine, ValueKind::Press, nullptr, later);
}

GuiActions::~GuiActions()
{
    _alive->store(false);
    _handles.clear();  // the registry forgets the GUI actions before their objects go
}

const actions::ActionRegistry& GuiActions::Registry() const
{
    return actions::ActionRegistry::Instance();
}

actions::Executor GuiActions::OnGui(GuiRun run)
{
    if (!run)
        return nullptr;
    auto alive = _alive;
    auto notice = _hooks.notice;
    return [alive, notice, run = std::move(run)](const actions::ActionCall& call) {
        PostToGui(alive, [run, notice, instance = call.instanceId, value = call.value, origin = call.origin] {
            const QString why = run(instance, value, origin);
            if (!why.isEmpty() && notice)
                notice(why);
        });
        StateNode body = StateNode::Object();
        body["queued"] = "gui";
        return actions::ActionResult::Success(std::move(body));
    };
}

void GuiActions::Add(const char* id, const QString& label, const QString& group, actions::ValueKind kind, GuiRun run,
                     actions::ActionRules rules, bool held)
{
    actions::ActionInfo info{id, label.toStdString(), group.toStdString(), kind, held, true, {}, {}, false};
    _handles.push_back(actions::ActionRegistry::Instance().Register(std::move(info), OnGui(std::move(run)),
                                                                    std::move(rules)));
}

void GuiActions::Notice(const QString& text)
{
    auto notice = _hooks.notice;
    PostToGui(_alive, [notice, text] {
        if (notice)
            notice(text);
    });
}

void GuiActions::Trigger(const HostActionEvent& event)
{
    const QString& action = event.action;
    const bool pressed = event.pressed;
    const float value = event.value;
    const QString& device = event.device;
    actions::ActionRegistry& registry = actions::ActionRegistry::Instance();
    const std::string id = action.toStdString();
    const std::optional<actions::ActionInfo> info = registry.Find(id);
    if (!info)
    {
        Notice(Tr("%1: unknown action").arg(action));
        return;
    }
    Emulator* emulator = _binding != nullptr ? _binding->emulator() : nullptr;
    if (emulator == nullptr)
    {
        if (pressed)
            Notice(Tr("%1: no machine").arg(QString::fromStdString(info->label)));
        return;
    }
    actions::ActionValue actionValue;
    actionValue.kind = info->kind;
    actionValue.pressed = pressed;
    actionValue.delta = pressed ? static_cast<double>(value) : 0.0;
    actionValue.level = pressed ? static_cast<double>(value) : 0.0;
    actionValue.rate = event.rate;
    if (!event.params.isEmpty())
        actionValue.params = ParamsNode(event.params.toStdString());
    actions::ActionOrigin origin;
    // The window keyboard is the GUI's own; every other device is a host device, by its id
    const bool keyboard = device == QLatin1String("keyboard") || device == QLatin1String("kbd:system");
    origin.origin = keyboard ? execution::Origin::Gui : execution::Origin::HostDevice;
    origin.identity = device.toStdString();
    origin.client = device.toStdString();
    const QString label = QString::fromStdString(info->label);
    auto alive = _alive;
    auto notice = _hooks.notice;
    auto marks = _hooks.marks;
    const std::string deviceId = device.toStdString();
    auto done = [alive, notice, marks, label, deviceId, keyboard](actions::ActionResult result) {
        // The device that moved feels the marks the seek crossed (control surfaces architecture.md §9)
        if (result.ok && marks && !keyboard)
            if (const StateNode* list = result.body.find("marks"); list != nullptr && list->isArray())
            {
                std::vector<std::string> kinds;
                for (const StateNode& mark : list->items)
                    if (const StateNode* kind = mark.find("kind"); kind != nullptr && kind->kind == StateNode::Kind::String)
                        kinds.push_back(kind->s);
                if (!kinds.empty())
                    PostToGui(alive, [marks, deviceId, kinds] { marks(deviceId, kinds); });
            }
        if (result.ok || !notice)
            return;
        const QString text = Tr("%1: %2").arg(label, QString::fromStdString(result.message));
        PostToGui(alive, [notice, text] { notice(text); });
    };
    // An encoder bound to a Press action: one press per step (architecture.md §5), at most 16 per turn
    if (event.turn && info->kind == actions::ValueKind::Press)
    {
        const int presses = std::min(16, static_cast<int>(std::lround(std::fabs(value))));
        for (int n = 0; n < presses; n++)
            for (const bool down : {true, false})
            {
                actions::ActionValue press = actionValue;
                press.pressed = down;
                registry.Submit(id, std::move(press), emulator->GetId(), origin, done);
            }
        return;
    }
    registry.Submit(id, std::move(actionValue), emulator->GetId(), std::move(origin), std::move(done));
}
