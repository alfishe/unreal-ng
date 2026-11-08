#include "guiactions.h"

#include <QCoreApplication>
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
#include "debugger/metadatasettings.h"
#include "emulator/actions/actionparams.h"
#include "emulator/actions/catalogue.h"
#include "emulator/actions/steppingactions.h"
#include "profiles/json.h"
#include "guicontrol/host.h"
#include "guicontrol/interfaces.h"

#include <QJsonDocument>
#include <QJsonObject>

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

GuiActions::GuiActions(EmulatorBinding* binding, GuiActionHooks hooks, QObject* parent)
    : QObject(parent), _binding(binding), _hooks(std::move(hooks)), _alive(std::make_shared<std::atomic<bool>>(true))
{
    // 3.3 Debugger UI
    QObject* debugger = _hooks.debugger;
    std::function<void()> showDebugger = _hooks.showDebugger;
    Add("dbg.open",
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
    Add("dbg.focus_next_pane", pane("focusNextPane"), noDebugger);
    Add("dbg.focus_prev_pane", pane("focusPrevPane"), noDebugger);
    Add("dbg.cursor_up", key(Qt::Key_Up, true), noDebugger);
    Add("dbg.cursor_down", key(Qt::Key_Down, true), noDebugger);
    Add("dbg.cursor_left", key(Qt::Key_Left, true), noDebugger);
    Add("dbg.cursor_right", key(Qt::Key_Right, true), noDebugger);
    Add("dbg.page_up", key(Qt::Key_PageUp, true), noDebugger);
    Add("dbg.page_down", key(Qt::Key_PageDown, true), noDebugger);
    Add("dbg.memory_follow", key(Qt::Key_Return, false), noDebugger);
    // An execution breakpoint at the disassembly cursor (dbg.breakpoint_toggle_pc, the core's, takes the PC)
    Add("dbg.breakpoint_toggle_cursor",
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

    // The disassembly (Stream Deck D6.4, D6.5): follow PC, labels on / off, byte / command scroll, go to an address
    Add("dbg.follow_pc", pane("followPc"), noDebugger);
    // The labels and the metadata switches (Debug > Metadata): global, kept in the settings, every debugger follows
    for (const MetadataSettings::Switch which :
         {MetadataSettings::Switch::Labels, MetadataSettings::Switch::Metadata, MetadataSettings::Switch::Regions,
          MetadataSettings::Switch::Colours, MetadataSettings::Switch::Notes})
    {
        actions::ActionRules rules;
        rules.state = [which](Emulator&, const actions::ActionValue&) -> std::optional<bool> {
            return MetadataSettings::Instance().IsOn(which);
        };
        Add(MetadataSettings::ActionId(which),
            [which](const std::string&, const actions::ActionValue&, const actions::ActionOrigin&) -> QString {
                MetadataSettings::Instance().Toggle(which);
                return {};
            },
            std::move(rules));
    }
    Add("dbg.scroll_mode_toggle", pane("toggleScrollMode"), noDebugger);
    Add("dbg.goto",
        [debugger, showDebugger](const std::string&, const actions::ActionValue& value,
                                 const actions::ActionOrigin&) -> QString {
            uint16_t address = 0;
            std::string error;
            if (!actions::AddressParam(value.params, "addr", address, error))
                return QString::fromStdString(error);
            auto* window = qobject_cast<QWidget*>(debugger);
            if (window == nullptr)
                return Tr("the debugger is not available");
            if (!window->isVisible() && showDebugger)
                showDebugger();
            QMetaObject::invokeMethod(window, "goToDisassemblyAddress", Q_ARG(int, static_cast<int>(address)));
            return {};
        },
        noDebugger);

    // 3.4 Capture: the screenshot to the clipboard (the main window's)
    std::function<void()> screenshot = _hooks.screenshot;
    Add("cap.screenshot",
        [screenshot](const std::string&, const actions::ActionValue&, const actions::ActionOrigin&) -> QString {
            if (screenshot)
                screenshot();
            return {};
        });

    // The on-screen keyboard window (the main window's)
    std::function<void()> toggleKeyboard = _hooks.toggleKeyboard;
    Add("osd.keyboard",
        [toggleKeyboard](const std::string&, const actions::ActionValue&, const actions::ActionOrigin&) -> QString {
            if (toggleKeyboard)
                toggleKeyboard();
            return {};
        });

    // The Controllers window, and its learn wizard for a device the database does not know (ui-ux.md §3.5)
    std::function<void()> showControllers = _hooks.showControllers;
    Add("ctl.open",
        [showControllers](const std::string&, const actions::ActionValue&, const actions::ActionOrigin&) -> QString {
            if (showControllers)
                showControllers();
            return {};
        });
    std::function<QString(const std::string&)> learn = _hooks.learn;
    Add("ctl.learn",
        [learn](const std::string&, const actions::ActionValue& value, const actions::ActionOrigin&) -> QString {
            const std::optional<std::string> device = actions::TextParam(value, "device");
            if (!device || device->empty())
                return Tr("ctl.learn needs params.device: a host device id (controllers devices lists them)");
            return learn ? learn(*device) : Tr("the Controllers window is not available");
        },
        {});

    // Stubs of the catalogue: a binding to one says why instead of doing nothing
    actions::ActionRules later;
    later.unavailable = [](Emulator&, const actions::ActionValue&) { return std::string("not available yet"); };
    later.unavailableError = "Not Implemented";
    for (const char* id : {"osd.quick_menu"})
        Add(id, nullptr, later);
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

void GuiActions::Add(const char* id, GuiRun run, actions::ActionRules rules)
{
    const actions::GuiActionDeclaration* declaration = actions::GuiActionDeclarationOf(id);
    Q_ASSERT_X(declaration != nullptr, "GuiActions::Add", "a GUI action the core does not declare (catalogue.cpp)");
    if (declaration == nullptr)
        return;
    actions::ActionInfo info{id,
                             Tr(declaration->label).toStdString(),
                             Tr(declaration->group).toStdString(),
                             declaration->kind,
                             declaration->held,
                             true,
                             declaration->params,
                             {},
                             false};
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

void GuiActions::SetGuiHost(guicontrol::Host* host)
{
    _guiHost = host;
}

bool GuiActions::TriggerGui(const HostActionEvent& event)
{
    const guicontrol::CommandSpec* command = guicontrol::FindCommand(event.action);
    if (command == nullptr)
        return false;
    const QString method = event.action;
    // The binding's parameters are the command's; `target` among them names the node (default: the interface on the
    // device's machine, design §4.5: the focused view of that interface showing it, else the focused one)
    QJsonObject params = QJsonDocument::fromJson(event.params.toUtf8()).object();
    QString target = params.take(QStringLiteral("target")).toString();
    if (target.isEmpty())
    {
        const QString interfaceName = guicontrol::InterfaceOf(method);
        const guicontrol::InterfaceSpec* spec = guicontrol::FindInterface(interfaceName);
        if (spec != nullptr && spec->level == guicontrol::Level::View && !event.instance.isEmpty())
            target = interfaceName + '@' + event.instance;
        else
            target = interfaceName;
    }
    const QString key = event.device + '|' + method + '|' + target;
    if (auto repeat = _guiRepeats.find(key); repeat != _guiRepeats.end())
        repeat->second->stop();
    if (!event.pressed && !event.turn)
        return true;
    if (!_guiHost)
    {
        Notice(Tr("%1: the GUI control plane is off").arg(method));
        return true;
    }
    const QString client = event.device;
    auto invoke = [this, method, target, params, client]() {
        if (!_guiHost)
            return;
        const QPointer<GuiActions> self(this);
        _guiHost->Invoke(method, target, params, client, [self, method](guicontrol::Result result) {
            if (self && !result.ok)
                self->Notice(Tr("%1: %2").arg(method, result.message));
        });
    };
    // An encoder's turn: once per step, at most 16 (as a Press action of the core)
    if (event.turn)
    {
        const int steps = std::min(16, static_cast<int>(std::lround(std::fabs(event.value))));
        for (int n = 0; n < steps; ++n)
            invoke();
        return true;
    }
    invoke();
    if (command->repeats)
    {
        QTimer*& timer = _guiRepeats[key];
        if (timer == nullptr)
            timer = new QTimer(this);
        timer->disconnect();
        connect(timer, &QTimer::timeout, this, [timer, invoke]() {
            timer->setInterval(60);
            invoke();
        });
        timer->start(300);
    }
    return true;
}

void GuiActions::Trigger(const HostActionEvent& event)
{
    if (TriggerGui(event))
        return;
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
    // The device's own instance (pinned to one, or the followed one); the window's when the router names none
    Emulator* emulator = _binding != nullptr ? _binding->emulator() : nullptr;
    std::string instance = event.instance.toStdString();
    if (instance.empty() && emulator != nullptr)
        instance = emulator->GetId();
    if (instance.empty())
    {
        if (pressed)
            Notice(Tr("%1: no machine").arg(QString::fromStdString(info->label)));
        return;
    }
    // The GUI's actions (the debugger's panes, the screenshot, the windows) act on this window's machine only
    if (info->gui && (emulator == nullptr || instance != emulator->GetId()))
    {
        if (pressed)
            Notice(Tr("%1: the device drives another machine than this window's")
                       .arg(QString::fromStdString(info->label)));
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
                registry.Submit(id, std::move(press), instance, origin, done);
            }
        return;
    }
    registry.Submit(id, std::move(actionValue), instance, std::move(origin), std::move(done));
}
