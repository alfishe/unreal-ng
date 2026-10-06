#include "slotcontrol.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/cpu/core.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/io/network/networkspec.h"
#include "debugger/ttd/timetravelhooks.h"
#include "emulator/slots/card.h"
#include "emulator/slots/slotconfig.h"
#include "emulator/slots/slotmatrix.h"
#include "emulator/slots/slotplanner.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/state/devicestate.h"
#include "emulator/state/statenodejson.h"

using namespace slots;

namespace
{

const SlotPlanner& Planner()
{
    static const SlotPlanner planner;
    return planner;
}

const char* FitName(Fit fit)
{
    switch (fit)
    {
        case Fit::Real:
            return "real";
        case Fit::Adapter:
            return "adapter";
        case Fit::Unrealistic:
            return "unrealistic";
    }
    return "real";
}

const char* OpName(SlotRequest::Op op)
{
    switch (op)
    {
        case SlotRequest::Op::Plug:
            return "plug";
        case SlotRequest::Op::Remove:
            return "remove";
        case SlotRequest::Op::SetOptions:
            return "set";
    }
    return "plug";
}

const char* DispositionName(MediaDisposition disposition)
{
    switch (disposition)
    {
        case MediaDisposition::Save:
            return "save";
        case MediaDisposition::Discard:
            return "discard";
        case MediaDisposition::None:
            break;
    }
    return "none";
}

std::string RuleName(Rule rule)
{
    if (rule == Rule::Request)
    {
        return "request";
    }
    return "D" + std::to_string(static_cast<int>(rule) - static_cast<int>(Rule::D1) + 1);
}

std::string PortText(uint16_t port)
{
    return FormatPort(port);
}

StateNode Strings(const std::vector<std::string>& values)
{
    StateNode array = StateNode::Array();
    for (const std::string& value : values)
    {
        array.push(value);
    }
    return array;
}

StateNode PortsNode(const std::vector<uint16_t>& ports)
{
    StateNode array = StateNode::Array();
    for (uint16_t port : ports)
    {
        array.push(PortText(port));
    }
    return array;
}

StateNode Functions(const std::vector<Function>& functions)
{
    StateNode array = StateNode::Array();
    for (Function function : functions)
    {
        array.push(Describe(function).id);
    }
    return array;
}

StateNode Signals(SignalSet signals)
{
    StateNode array = StateNode::Array();
    for (int i = 0; i < kBusSignalCount; i++)
    {
        if (signals & (1u << i))
        {
            array.push(Describe(static_cast<BusSignal>(1u << i)).id);
        }
    }
    return array;
}

std::string OptionsOf(const std::string& cardId, const CardOptions& options)
{
    const CardDef* card = Planner().FindCard(cardId);
    return card != nullptr ? FormatCardOptions(*card, options) : std::string();
}

/// The resolved instance, or nullptr with the reply filled
std::shared_ptr<Emulator> FindEmulator(const SlotControlRequest& request, SlotControlReply& reply)
{
    EmulatorManager& manager = *EmulatorManager::GetInstance();
    const std::string id = request.emulatorId.empty() ? manager.GetSelectedEmulatorId() : request.emulatorId;
    std::shared_ptr<Emulator> emulator = id.empty() ? nullptr : manager.GetEmulator(id);
    if (!emulator || emulator->GetContext() == nullptr)
    {
        reply.status = "no-machine";
        reply.httpStatus = 404;
        reply.message = id.empty() ? "no emulator selected" : "no emulator '" + id + "'";
    }
    return emulator;
}

void BadRequest(SlotControlReply& reply, std::string message)
{
    reply.status = "bad-request";
    reply.httpStatus = 400;
    reply.message = std::move(message);
}

/// The card options of a request; false with the reply filled
bool ParseOptions(const CardDef& card, const std::string& text, CardOptions& out, SlotControlReply& reply)
{
    std::string error;
    if (!ParseCardOptions(card, text, out, &error))
    {
        BadRequest(reply, "options '" + text + "' of " + card.id + ": " + error);
        return false;
    }
    return true;
}

/// gs verb: the personality names of every surface (gs, gs-lw, neogs; the GS switch's z80 / lle, lw / lightweight,
/// ngs / neogs)
bool GsKindOf(std::string name, GSTypeKind& kind)
{
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name == "gs")
    {
        kind = GSTypeKind::Z80;
        return true;
    }
    if (name == "gs-lw")
    {
        kind = GSTypeKind::LW;
        return true;
    }
    return gsParsePersonality(name, kind) && kind != GSTypeKind::NONE;
}

StateNode MediaValue(const MediaTransferReport& media)
{
    StateNode node = StateNode::Object();
    node["attached"] = Strings(media.attached);
    node["detached"] = Strings(media.detached);
    node["closed"] = Strings(media.closed);
    node["lines"] = Strings(media.lines);
    return node;
}

StateNode StrandedValue(const std::vector<SlotInfo>& stranded)
{
    StateNode array = StateNode::Array();
    for (const SlotInfo& info : stranded)
    {
        StateNode medium = StateNode::Object();
        medium["slot"] = info.descriptor.id;
        medium["source"] = info.source;
        medium["changes"] = info.changes;
        array.push(std::move(medium));
    }
    return array;
}

/// Runs a change through SlotChange and fills the reply 1:1 from its result. `changes`: several requests planned into
/// one restart (`change` then carries the flags); `afterRestart`: called with the restarted machine before it is started
void RunChange(const SlotControlRequest& request, const std::string& emulatorId, const SlotRequest& change,
               SlotControlReply& reply, const std::vector<SlotRequest>& changes = {},
               const std::function<void(Emulator&)>& afterRestart = {})
{
    SlotChangeRequest run;
    run.emulatorId = emulatorId;
    run.change = change;
    run.changes = changes;
    run.beforeRelease = request.beforeRelease;
    SlotChangeResult result = SlotChange::Run(run);
    if (result.Applied() && result.emulator && afterRestart)
    {
        afterRestart(*result.emulator);
    }

    reply.status = SlotChange::StatusName(result.status);
    reply.message = result.message;
    switch (result.status)
    {
        case SlotChangeStatus::Applied:
        case SlotChangeStatus::DryRun:
            reply.httpStatus = 200;
            break;
        case SlotChangeStatus::Refused:
        case SlotChangeStatus::Recording:
            reply.httpStatus = 409;
            break;
        case SlotChangeStatus::NoMachine:
            reply.httpStatus = 404;
            break;
        case SlotChangeStatus::Failed:
            reply.httpStatus = result.stranded.empty() ? 500 : 409;
            break;
    }

    StateNode& body = reply.body;
    body["op"] = OpName(change.op);
    body["slot"] = change.slot;
    if (change.op == SlotRequest::Op::Plug)
    {
        body["card"] = change.card;
    }
    body["replaceIfIncompatible"] = change.replaceIfIncompatible;
    body["dryRun"] = change.dryRun;
    body["mediaDisposition"] = DispositionName(change.mediaDisposition);
    body["plan"] = SlotControl::PlanValue(result.plan);

    StateNode restart = StateNode::Object();
    restart["restarted"] = result.Applied();
    restart["previousEmulatorId"] = result.previousEmulatorId.empty() ? emulatorId : result.previousEmulatorId;
    restart["emulatorId"] = result.emulator ? result.emulator->GetId() : emulatorId;
    restart["wasRunning"] = result.wasRunning;
    bool started = false;
    if (result.Applied() && result.emulator && result.wasRunning && request.startWhenRunning)
    {
        started = EmulatorManager::GetInstance()->StartEmulatorAsync(result.emulator->GetId());
    }
    restart["started"] = started;
    if (result.Applied())
    {
        restart["note"] = "the machine was restarted with the new slot set: a new emulator id, the machine state is "
                          "lost, the media followed";
    }
    body["restart"] = std::move(restart);
    body["emulatorId"] = result.emulator ? result.emulator->GetId() : emulatorId;
    body["media"] = MediaValue(result.media);
    body["stranded"] = StrandedValue(result.stranded);
    reply.emulator = result.emulator;
}

/// network verb (owner decision Q11): the settings checked first; a card change of the ZX-bus cards becomes the slot
/// changes of one restart and the other keys go to the restarted machine; without one the settings apply in place
void Network(const SlotControlRequest& request, std::shared_ptr<Emulator>& emulator, SlotControlReply& reply)
{
    NetworkManager::Change change;
    std::string error;
    if (!NetworkManager::ParseChange(request.settings, change, error) || !NetworkManager::ValidateChange(change, error))
    {
        BadRequest(reply, error);
        return;
    }
    if (change.Empty())
    {
        BadRequest(reply, "network: no settings given (card, host_access, hosts, com_port, ...)");
        return;
    }
    SlotRequest flags;
    flags.replaceIfIncompatible = request.replaceIfIncompatible;
    flags.dryRun = request.dryRun;
    if (!SlotControl::ParseMediaDisposition(request.media, flags.mediaDisposition))
    {
        BadRequest(reply, "media '" + request.media + "': expected save or discard");
        return;
    }
    EmulatorContext* context = emulator->GetContext();
    const std::string emulatorId = emulator->GetId();
    NetworkManager* network = context->pCore != nullptr ? context->pCore->GetNetworkManager() : nullptr;
    if (network == nullptr)
    {
        reply.status = "no-machine";
        reply.httpStatus = 404;
        reply.message = "no network support in this machine";
        return;
    }
    SlotManager* manager = context->pSlotManager;
    const bool declared = manager != nullptr && manager->Current().machine != nullptr;
    const SlotManager::Result current = declared ? manager->Snapshot() : SlotManager::Result{};
    const uint8_t configured = context->config.network.card;
    const uint8_t fitted = declared ? SlotManager::NetworkCardsOf(current)
                                    : static_cast<uint8_t>(configured & NetworkManager::kZxBusCards);
    const uint8_t wanted = change.card ? static_cast<uint8_t>(*change.card & NetworkManager::kZxBusCards) : fitted;
    const bool cardChange = wanted != fitted;

    StateNode settings = StateNode::Array();
    for (const auto& [key, value] : request.settings)
    {
        settings.push(key + "=" + value);
    }
    StateNode net = StateNode::Object();
    net["settings"] = std::move(settings);
    net["cardChange"] = cardChange;
    net["cardsBefore"] = networkspec::CardsToString(fitted);
    net["cards"] = networkspec::CardsToString(wanted);
    reply.body["op"] = "network";
    reply.body["emulatorId"] = emulatorId;

    if (!cardChange)
    {
        // Nothing on a bus slot changes: the settings apply to this machine, no restart
        StateNode restart = StateNode::Object();
        restart["restarted"] = false;
        restart["emulatorId"] = emulatorId;
        reply.body["restart"] = std::move(restart);
        if (request.dryRun)
        {
            reply.status = "dry-run";
            reply.message = "no card change: the settings would apply to the running machine, without a restart";
            net["settingsApplied"] = false;
            reply.body["network"] = std::move(net);
            return;
        }
        if (!network->RequestChange(change, error))
        {
            const bool recording = context->pTimeTravelHooks != nullptr && context->pTimeTravelHooks->IsRecording();
            reply.status = recording ? "recording" : "refused";
            reply.httpStatus = 409;
            reply.message = error;
            net["settingsApplied"] = false;
            reply.body["network"] = std::move(net);
            return;
        }
        reply.status = "accepted";
        reply.message = change.OnlyRemoteAccess()
                            ? "the host listeners move at the next frame boundary (connections stay)"
                            : "applied at the next frame boundary (at once while paused): the network devices are "
                              "fitted again, every connection closes";
        net["settingsApplied"] = true;
        reply.body["network"] = std::move(net);
        return;
    }

    if (!declared)
    {
        reply.status = "refused";
        reply.httpStatus = 409;
        reply.message = "refused: the model has no slot declaration, its ZX-bus network cards cannot change";
        reply.body["network"] = std::move(net);
        return;
    }
    std::vector<SlotRequest> requests;
    SlotManager::NetworkRequests(current, wanted, requests, &error);
    for (SlotRequest& one : requests)
    {
        one.replaceIfIncompatible = flags.replaceIfIncompatible;
        one.dryRun = flags.dryRun;
        one.mediaDisposition = flags.mediaDisposition;
    }

    // The other keys for the restarted machine (its cards are the wanted ones by then); ATM2IOESP is not a bus slot
    NetworkManager::Change rest = change;
    rest.card.reset();
    if (change.card && ((*change.card ^ configured) & networkspec::kCardAtm2IoEsp) != 0)
    {
        rest.card = change.card;
    }
    auto applied = std::make_shared<std::string>();
    auto applyRest = [rest, applied](Emulator& fresh) {
        if (rest.Empty())
        {
            return;
        }
        EmulatorContext* freshContext = fresh.GetContext();
        NetworkManager* freshNetwork =
            freshContext != nullptr && freshContext->pCore != nullptr ? freshContext->pCore->GetNetworkManager() : nullptr;
        std::string why;
        if (freshNetwork == nullptr)
        {
            *applied = "no network manager on the restarted machine";
        }
        else if (!freshNetwork->RequestChange(rest, why))
        {
            *applied = why;
        }
        else
        {
            *applied = "ok";
        }
    };

    network = nullptr;
    manager = nullptr;
    context = nullptr;
    emulator.reset();   // nothing here may keep the old machine alive across the restart
    RunChange(request, emulatorId, flags, reply, requests, applyRest);
    reply.body["op"] = "network";
    reply.body["slot"] = std::string();
    reply.body["card"] = networkspec::CardsToString(wanted);
    if (reply.status == "applied")
    {
        if (applied->empty())
        {
            net["settingsApplied"] = false;
            net["note"] = "the card change was the only setting";
        }
        else if (*applied == "ok")
        {
            net["settingsApplied"] = true;
            net["note"] = "the other settings were applied to the restarted machine";
        }
        else
        {
            net["settingsApplied"] = false;
            net["note"] = "the cards changed, the other settings were not applied: " + *applied;
            reply.message = net["note"].s;
        }
    }
    else
    {
        net["settingsApplied"] = false;
    }
    reply.body["network"] = std::move(net);
}

} // namespace

// region <SlotControlReply>

StateNode SlotControlReply::ToValue() const
{
    StateNode value = StateNode::Object();
    value["ok"] = Ok();
    value["status"] = status;
    value["message"] = message;
    for (const auto& [key, field] : body.members)
    {
        value[key] = field;
    }
    return value;
}

std::string SlotControlReply::ToJson() const
{
    return StateNodeToJsonText(ToValue());
}

std::string SlotControlReply::ToText() const
{
    std::ostringstream out;
    const StateNode* plan = body.find("plan");
    const StateNode* network = body.find("network");
    if (plan == nullptr && network != nullptr)
    {
        // Network settings without a card change (or refused before any plan): no restart
        out << status;
        if (!message.empty())
        {
            out << ": " << message;
        }
        out << "\n";
        return out.str();
    }
    if (plan == nullptr)
    {
        // Queries: the tree as text (matrix: the tables themselves)
        if (!Ok())
        {
            out << "Error [" << status << "]: " << message << "\n";
            return out.str();
        }
        if (const StateNode* tables = body.find("tables"))
        {
            for (const StateNode& table : tables->items)
            {
                out << "## " << table.find("name")->s << "\n\n" << table.find("markdown")->s << "\n";
            }
            return out.str();
        }
        return DeviceState::ToText(body);
    }

    out << status;
    if (!message.empty())
    {
        out << ": " << message;
    }
    out << "\n";
    if (const StateNode* lines = plan->find("lines"))
    {
        for (const StateNode& line : lines->items)
        {
            out << "  " << line.s << "\n";
        }
    }
    if (const StateNode* restart = body.find("restart"); restart != nullptr && restart->find("restarted")->b)
    {
        out << "restarted: emulator " << restart->find("previousEmulatorId")->s << " -> "
            << restart->find("emulatorId")->s << (restart->find("started")->b ? " (running)" : " (not started)")
            << "\n";
    }
    if (const StateNode* media = body.find("media"))
    {
        for (const StateNode& line : media->find("lines")->items)
        {
            out << "  media: " << line.s << "\n";
        }
    }
    if (const StateNode* stranded = body.find("stranded"))
    {
        for (const StateNode& medium : stranded->items)
        {
            out << "  unsaved: " << medium.find("slot")->s << " " << medium.find("source")->s << " ("
                << medium.find("changes")->s << ")\n";
        }
    }
    if (network != nullptr)
    {
        if (const StateNode* note = network->find("note"))
        {
            out << "network: " << note->s << "\n";
        }
    }
    return out.str();
}

// endregion </SlotControlReply>

const std::vector<std::string>& SlotControl::Verbs()
{
    static const std::vector<std::string> verbs = {"list", "catalog", "matrix", "plug", "remove", "set", "gs", "network"};
    return verbs;
}

bool SlotControl::ParseMediaDisposition(const std::string& text, MediaDisposition& out)
{
    if (text.empty() || text == "none" || text == "refuse")
    {
        out = MediaDisposition::None;
        return true;
    }
    if (text == "save")
    {
        out = MediaDisposition::Save;
        return true;
    }
    if (text == "discard")
    {
        out = MediaDisposition::Discard;
        return true;
    }
    return false;
}

bool SlotControl::CreateOverride(const std::vector<std::pair<std::string, std::string>>& keyValues,
                                 std::function<void(CONFIG&)>& out, std::string& error)
{
    SlotConfig slotConfig;
    ParseSlotsSection(keyValues, slotConfig);
    if (!slotConfig.errors.empty())
    {
        error.clear();
        for (const std::string& line : slotConfig.errors)
        {
            error += (error.empty() ? "" : "; ") + line;
        }
        return false;
    }
    for (SlotConfigEntry& entry : slotConfig.entries)
    {
        entry.source = "create slots " + entry.slot;
    }
    out = [slotConfig](CONFIG& config) { SlotManager::UseSlots(slotConfig, config); };
    return true;
}

std::vector<std::string> SlotControl::PlanLines(const SlotPlan& plan)
{
    std::vector<std::string> lines;
    std::string head = std::string(OpName(plan.op)) + " " + plan.card + (plan.card.empty() ? "" : " ") + "-> " +
                       plan.slot + ": " + (plan.allowed ? "allowed" : (plan.hardRefusal ? "refused" : "needs replaceIfIncompatible"));
    if (plan.allowed || !plan.hardRefusal)
    {
        head += std::string(", fit ") + FitName(plan.fit);
        if (!plan.adapter.empty())
        {
            head += " behind " + plan.adapter;
        }
    }
    lines.push_back(head);
    for (const PlanReason& reason : plan.reasons)
    {
        // A displacement's reason is its "removes" line below
        const bool displacement = reason.rule == Rule::D1 || reason.rule == Rule::D3 || reason.rule == Rule::D12 ||
                                  (reason.rule == Rule::Request && plan.op == SlotRequest::Op::Remove);
        if (reason.hard || !displacement || plan.removed.empty())
        {
            lines.push_back(std::string(reason.hard ? "refused" : "reason") + " (" + RuleName(reason.rule) + "): " +
                            reason.text);
        }
    }
    for (const RemovedCard& removed : plan.removed)
    {
        lines.push_back("removes " + removed.slot + " = " + removed.card +
                        (removed.optionsText.empty() ? "" : " [" + removed.optionsText + "]") + " (" +
                        RuleName(removed.rule) + "): " + removed.reason);
    }
    for (const ShadowedDevice& shadowed : plan.shadowed)
    {
        std::string ports;
        for (uint16_t port : shadowed.ports)
        {
            ports += (ports.empty() ? "" : " ") + PortText(port);
        }
        lines.push_back("shadows the built-in " + shadowed.device + " (" + ports + ")");
    }
    for (const SwitchedOffBuiltIn& off : plan.builtInSwitchedOff)
    {
        lines.push_back("switches off the built-in " + off.builtIn);
    }
    for (const RemovedFromSocket& removed : plan.removedFromSocket)
    {
        lines.push_back("takes the " + removed.chip + " (" + removed.builtIn + ") out of " + removed.socket);
    }
    for (const DisabledCard& disabled : plan.disabled)
    {
        lines.push_back("disables " + disabled.slot + " = " + disabled.card + ": " + disabled.reason);
    }
    for (const DeadPort& dead : plan.deadPorts)
    {
        lines.push_back("port " + FormatPortRange(dead.mask, dead.match) + " never reaches the card (the board serves it)");
    }
    for (const BusFight& fight : plan.busFights)
    {
        lines.push_back("bus fight on " + PortText(fight.port) + " with " + fight.device);
    }
    if (!plan.lostFunctions.empty())
    {
        std::string lost;
        for (Function function : plan.lostFunctions)
        {
            lost += (lost.empty() ? "" : ", ") + std::string(Describe(function).id);
        }
        lines.push_back("the machine loses: " + lost);
    }
    for (const MediaRelease& media : plan.media)
    {
        lines.push_back("releases " + media.mediaSlot + " of " + media.slot + " = " + media.card +
                        (media.dirty ? " (unsaved changes, disposition " + std::string(DispositionName(media.disposition)) + ")" : ""));
    }
    return lines;
}

StateNode SlotControl::PlanValue(const SlotManager::ChangePlan& change)
{
    const SlotPlan& plan = change.plan;
    StateNode node = StateNode::Object();
    node["allowed"] = change.Allowed();
    node["refusal"] = change.refusal;
    node["recording"] = change.recording;
    node["planAllowed"] = plan.allowed;
    node["hardRefusal"] = plan.hardRefusal;
    node["needsReplaceIfIncompatible"] = !plan.allowed && !plan.hardRefusal;
    node["needsConfirmation"] = plan.needsConfirmation;
    node["op"] = OpName(plan.op);
    node["slot"] = plan.slot;
    node["card"] = plan.card;
    node["options"] = OptionsOf(plan.card, plan.options);
    node["fit"] = FitName(plan.fit);
    node["adapter"] = plan.adapter;
    node["missingSignals"] = Signals(plan.missingSignals);
    node["arbitration"] = Describe(plan.arbitration).id;

    StateNode reasons = StateNode::Array();
    for (const PlanReason& reason : plan.reasons)
    {
        StateNode r = StateNode::Object();
        r["rule"] = RuleName(reason.rule);
        r["hard"] = reason.hard;
        r["text"] = reason.text;
        reasons.push(std::move(r));
    }
    node["reasons"] = std::move(reasons);
    node["exceptions"] = Strings(plan.exceptions);

    StateNode removed = StateNode::Array();
    for (const RemovedCard& card : plan.removed)
    {
        StateNode r = StateNode::Object();
        r["slot"] = card.slot;
        r["card"] = card.card;
        r["options"] = card.optionsText;
        r["rule"] = RuleName(card.rule);
        r["reason"] = card.reason;
        r["clashing"] = Functions(card.clashing);
        r["replacedInSlot"] = card.replacedInSlot;
        r["pointless"] = card.pointless;
        // What puts it back (an Undo plugs it again)
        r["undo"] = "plug " + card.slot + " " + card.card + (card.optionsText.empty() ? "" : " " + card.optionsText);
        removed.push(std::move(r));
    }
    node["removed"] = std::move(removed);

    StateNode shadowed = StateNode::Array();
    for (const ShadowedDevice& device : plan.shadowed)
    {
        StateNode s = StateNode::Object();
        s["device"] = device.device;
        s["by"] = device.by;
        s["ports"] = PortsNode(device.ports);
        shadowed.push(std::move(s));
    }
    node["shadowed"] = std::move(shadowed);

    StateNode off = StateNode::Array();
    for (const SwitchedOffBuiltIn& builtIn : plan.builtInSwitchedOff)
    {
        StateNode s = StateNode::Object();
        s["builtIn"] = builtIn.builtIn;
        s["functions"] = Functions(builtIn.functions);
        off.push(std::move(s));
    }
    node["builtInSwitchedOff"] = std::move(off);

    StateNode socket = StateNode::Array();
    for (const RemovedFromSocket& chip : plan.removedFromSocket)
    {
        StateNode s = StateNode::Object();
        s["builtIn"] = chip.builtIn;
        s["socket"] = chip.socket;
        s["chip"] = chip.chip;
        s["ports"] = PortsNode(chip.ports);
        socket.push(std::move(s));
    }
    node["removedFromSocket"] = std::move(socket);

    StateNode disabled = StateNode::Array();
    for (const DisabledCard& card : plan.disabled)
    {
        StateNode s = StateNode::Object();
        s["slot"] = card.slot;
        s["card"] = card.card;
        s["clashSlot"] = card.clashSlot;
        s["ports"] = Strings(card.ports);
        s["reason"] = card.reason;
        disabled.push(std::move(s));
    }
    node["disabled"] = std::move(disabled);

    StateNode dead = StateNode::Array();
    for (const DeadPort& port : plan.deadPorts)
    {
        StateNode s = StateNode::Object();
        s["range"] = FormatPortRange(port.mask, port.match);
        s["servedBy"] = Strings(port.servedBy);
        dead.push(std::move(s));
    }
    node["deadPorts"] = std::move(dead);

    StateNode fights = StateNode::Array();
    for (const BusFight& fight : plan.busFights)
    {
        StateNode s = StateNode::Object();
        s["device"] = fight.device;
        s["port"] = PortText(fight.port);
        fights.push(std::move(s));
    }
    node["busFights"] = std::move(fights);
    node["lostFunctions"] = Functions(plan.lostFunctions);

    StateNode media = StateNode::Array();
    for (const MediaRelease& release : plan.media)
    {
        StateNode s = StateNode::Object();
        s["slot"] = release.slot;
        s["card"] = release.card;
        s["mediaSlot"] = release.mediaSlot;
        s["dirty"] = release.dirty;
        s["disposition"] = DispositionName(release.disposition);
        media.push(std::move(s));
    }
    node["media"] = std::move(media);

    StateNode resulting = StateNode::Array();
    for (const SlotEntry& entry : plan.resultingSlots)
    {
        StateNode s = StateNode::Object();
        s["slot"] = entry.slot;
        s["card"] = entry.card;
        s["options"] = OptionsOf(entry.card, entry.options);
        s["adapter"] = entry.adapter;
        s["unrealistic"] = entry.unrealistic;
        s["disabled"] = entry.disabled;
        resulting.push(std::move(s));
    }
    node["resultingSlots"] = std::move(resulting);

    // The [SLOTS] the restarted machine gets (an allowed plan)
    std::vector<std::string> section;
    if (change.Allowed())
    {
        std::istringstream text(FormatSlotsSection(change.config));
        for (std::string line; std::getline(text, line);)
        {
            if (!line.empty())
            {
                section.push_back(line);
            }
        }
    }
    node["slotsSection"] = Strings(section);
    node["lines"] = Strings(PlanLines(plan));
    return node;
}

StateNode SlotControl::CarryValue(const SlotManager::CarryReport& carry)
{
    StateNode node = StateNode::Object();
    node["carried"] = carry.carried;
    node["kept"] = Strings(carry.kept);
    StateNode dropped = StateNode::Array();
    for (const SlotManager::CarryReport::Dropped& card : carry.dropped)
    {
        StateNode d = StateNode::Object();
        d["slot"] = card.slot;
        d["card"] = card.card;
        d["reason"] = card.reason;
        dropped.push(std::move(d));
    }
    node["dropped"] = std::move(dropped);
    node["lines"] = Strings(carry.lines);
    return node;
}

StateNode SlotControl::Catalog(const SlotManager::Result& current)
{
    StateNode cards = StateNode::Array();
    const SlotSet fitted = current.Fitted();
    for (const CardDef& card : Planner().Data().cards)
    {
        StateNode c = StateNode::Object();
        c["id"] = card.id;
        c["name"] = card.name;
        c["bus"] = Describe(card.bus).id;
        c["needs"] = Signals(card.needs);
        c["detection"] = Describe(card.detection).id;
        c["socketDefault"] = card.socketDefault;
        c["emulated"] = FindCardType(card.id) != nullptr || SlotManager::GroupOf(card.id) != SlotCardGroup::Count;

        StateNode options = StateNode::Array();
        for (const OptionDef& option : card.options)
        {
            StateNode o = StateNode::Object();
            o["name"] = Describe(option.key).id;
            o["kind"] = Describe(option.kind).id;
            StateNode values = StateNode::Array();
            std::string defaults;
            for (size_t i = 0; i < option.values.size(); i++)
            {
                StateNode v = StateNode::Object();
                v["id"] = option.values[i].id;
                v["label"] = option.values[i].label;
                values.push(std::move(v));
                if (option.defaultBits & Bit(static_cast<int>(i)))
                {
                    defaults += (defaults.empty() ? "" : ",") + std::string(option.values[i].id);
                }
            }
            o["values"] = std::move(values);
            o["default"] = defaults;
            o["description"] = option.description;
            options.push(std::move(o));
        }
        c["options"] = std::move(options);

        StateNode functions = StateNode::Array();
        for (const FunctionUse& use : card.functions)
        {
            functions.push(Describe(use.function).id);
        }
        c["functions"] = std::move(functions);
        StateNode ports = StateNode::Array();
        for (const PortClaim& claim : card.claims)
        {
            ports.push(FormatPortRange(claim.mask, claim.match));
        }
        c["ports"] = std::move(ports);
        if (card.portsNote[0] != '\0')
        {
            c["portsNote"] = card.portsNote;
        }
        StateNode media = StateNode::Array();
        for (const char* mediaSlot : card.media)
        {
            media.push(mediaSlot);
        }
        c["media"] = std::move(media);

        // How it would fit this machine now: the plan of a plug into the slot the planner suggests
        StateNode here = StateNode::Object();
        std::string fittedIn;
        for (const SlotEntry& entry : fitted)
        {
            if (entry.card == card.id)
            {
                fittedIn += (fittedIn.empty() ? "" : ",") + entry.slot;
            }
        }
        here["fittedIn"] = fittedIn;
        if (current.machine != nullptr)
        {
            SlotRequest request;
            request.op = SlotRequest::Op::Plug;
            request.card = card.id;
            request.dryRun = true;
            const SlotPlan plan = Planner().Plan(current.model, fitted, request);
            here["slot"] = plan.slot;
            here["fit"] = FitName(plan.fit);
            here["adapter"] = plan.adapter;
            here["outcome"] = plan.allowed ? "fits" : (plan.hardRefusal ? "refused" : "needs-replace");
            StateNode removes = StateNode::Array();
            for (const RemovedCard& removed : plan.removed)
            {
                removes.push(removed.slot + " = " + removed.card);
            }
            here["removes"] = std::move(removes);
            StateNode reasons = StateNode::Array();
            for (const PlanReason& reason : plan.reasons)
            {
                reasons.push(reason.text);
            }
            here["reasons"] = std::move(reasons);
        }
        c["thisMachine"] = std::move(here);
        cards.push(std::move(c));
    }
    return cards;
}

SlotControlReply SlotControl::Execute(const SlotControlRequest& request)
{
    SlotControlReply reply;
    std::string verb = request.verb.empty() ? "list" : request.verb;
    std::transform(verb.begin(), verb.end(), verb.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (verb == "matrix")
    {
        StateNode tables = StateNode::Array();
        for (std::string_view name : MatrixTableNames())
        {
            if (!request.table.empty() && request.table != name)
            {
                continue;
            }
            StateNode table = StateNode::Object();
            table["name"] = std::string(name);
            table["markdown"] = RenderMatrixTable(name);
            tables.push(std::move(table));
        }
        if (tables.size() == 0)
        {
            std::string names;
            for (std::string_view name : MatrixTableNames())
            {
                names += (names.empty() ? "" : ", ") + std::string(name);
            }
            BadRequest(reply, "unknown matrix table '" + request.table + "' (" + names + ")");
            return reply;
        }
        reply.body["tables"] = std::move(tables);
        return reply;
    }

    if (std::find(Verbs().begin(), Verbs().end(), verb) == Verbs().end())
    {
        std::string verbs;
        for (const std::string& v : Verbs())
        {
            verbs += (verbs.empty() ? "" : ", ") + v;
        }
        BadRequest(reply, "unknown slots verb '" + verb + "' (" + verbs + ")");
        return reply;
    }

    std::shared_ptr<Emulator> emulator = FindEmulator(request, reply);
    if (!emulator)
    {
        return reply;
    }
    EmulatorContext* context = emulator->GetContext();
    const std::string emulatorId = emulator->GetId();
    SlotManager* manager = context->pSlotManager;

    if (verb == "network")
    {
        Network(request, emulator, reply);
        return reply;
    }
    if (verb == "list")
    {
        reply.body = DeviceState::Slots(context);
        reply.body["emulatorId"] = emulatorId;
        return reply;
    }
    if (manager == nullptr || manager->Current().machine == nullptr)
    {
        reply.status = "no-machine";
        reply.httpStatus = 404;
        reply.message = "the machine has no slot declaration";
        return reply;
    }
    const SlotManager::Result current = manager->Snapshot();
    if (verb == "catalog")
    {
        reply.body["emulatorId"] = emulatorId;
        reply.body["model"] = current.machine->name;
        reply.body["cards"] = Catalog(current);
        return reply;
    }

    // Changes
    SlotRequest change;
    change.replaceIfIncompatible = request.replaceIfIncompatible;
    change.dryRun = request.dryRun;
    if (!ParseMediaDisposition(request.media, change.mediaDisposition))
    {
        BadRequest(reply, "media '" + request.media + "': expected save or discard");
        return reply;
    }

    if (verb == "gs")
    {
        GSTypeKind kind = GSTypeKind::NONE;
        if (!GsKindOf(request.card, kind))
        {
            BadRequest(reply, "personality '" + request.card + "': expected gs, gs-lw or neogs (also " +
                                  GS_PERSONALITY_NAMES + ")");
            return reply;
        }
        std::string error;
        const MediaDisposition disposition = change.mediaDisposition;
        // The request is the replacement of the card in the GS slot: the replace flag is set (Q10)
        if (!SlotManager::GeneralSoundRequest(current, context->config, kind, change, &error))
        {
            reply.status = "refused";
            reply.httpStatus = 409;
            reply.message = "refused: " + error;
            return reply;
        }
        change.dryRun = request.dryRun;
        change.mediaDisposition = disposition;
        // Anything besides the card in the GS slot is a displacement like any other (Q1): it needs the flag
        if (!request.replaceIfIncompatible)
        {
            const SlotManager::ChangePlan plan = manager->PlanChange(change);
            for (const RemovedCard& removed : plan.plan.removed)
            {
                if (removed.slot != change.slot)
                {
                    reply.status = "refused";
                    reply.httpStatus = 409;
                    reply.message = "needs replaceIfIncompatible: the switch would also remove " + removed.slot +
                                    " = " + removed.card + " (" + removed.reason + ")";
                    reply.body["plan"] = PlanValue(plan);
                    return reply;
                }
            }
        }
        emulator.reset();
        RunChange(request, emulatorId, change, reply);
        return reply;
    }

    if (verb == "plug")
    {
        if (request.card.empty())
        {
            BadRequest(reply, "plug needs a card (see the catalog)");
            return reply;
        }
        const CardDef* card = Planner().FindCard(request.card);
        if (card == nullptr && request.card != kEmptySocket)
        {
            BadRequest(reply, "unknown card '" + request.card + "' (see the catalog)");
            return reply;
        }
        change.op = SlotRequest::Op::Plug;
        change.slot = request.slot;
        change.card = request.card;
        change.adapter = request.adapter;
        if (card != nullptr && !ParseOptions(*card, request.options, change.options, reply))
        {
            return reply;
        }
    }
    else if (verb == "remove")
    {
        if (request.slot.empty())
        {
            BadRequest(reply, "remove needs a slot");
            return reply;
        }
        change.op = SlotRequest::Op::Remove;
        change.slot = request.slot;
    }
    else   // set
    {
        if (request.slot.empty())
        {
            BadRequest(reply, "set needs a slot");
            return reply;
        }
        const SlotManager::Slot* slot = current.FindSlot(request.slot);
        const CardDef* card = slot != nullptr ? Planner().FindCard(slot->entry.card) : nullptr;
        if (card == nullptr)
        {
            BadRequest(reply, "slot '" + request.slot + "' holds no card with options");
            return reply;
        }
        change.op = SlotRequest::Op::SetOptions;
        change.slot = request.slot;
        change.card = slot->entry.card;
        if (!ParseOptions(*card, request.options, change.options, reply))
        {
            return reply;
        }
    }

    emulator.reset();   // nothing here may keep the old machine alive across the restart
    RunChange(request, emulatorId, change, reply);
    return reply;
}
