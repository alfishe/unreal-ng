#include "guiactions.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <utility>

#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/execution/automationgate.h"
#include "controllers/hostcontrollers.h"
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
}

GuiActions::~GuiActions()
{
    _alive->store(false);  // a result still on its way finds the window gone
}

const actions::ActionRegistry& GuiActions::Registry() const
{
    return actions::ActionRegistry::Instance();
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
