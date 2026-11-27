#include "machinepreset.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <optional>
#include <utility>

#include "base/featuremanager.h"
#include "emulator/config.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/executionarbiter.h"
#include "emulator/machinevariants.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/slots/slotplanner.h"
#include "emulator/sound/audiomixer.h"
#include "emulator/zxpoly/zxpolygroup.h"

namespace
{

const slots::SlotPlanner& Planner()
{
    static const slots::SlotPlanner planner;
    return planner;
}

/// One card of the request, parsed
struct CardAsk
{
    std::string slot;   ///< "" = the planner's choice
    std::string card;
    slots::SlotRequest request;
};

/// What the create-time override found (it runs inside the creation, with the model's configuration loaded)
struct SlotOutcome
{
    bool planned = false;
    SlotManager::ChangePlan plan;
    std::vector<std::string> machineSlots;   ///< the slots fitted before the cards, for a refusal's text
};

MachinePresetReply Refusal(const char* status, int httpStatus, std::string message)
{
    MachinePresetReply reply;
    reply.status = status;
    reply.httpStatus = httpStatus;
    reply.message = std::move(message);
    reply.text = reply.message;
    return reply;
}

MachinePresetReply BadRequest(std::string message)
{
    return Refusal("bad-request", 400, std::move(message));
}

StateNode Strings(const std::vector<std::string>& values)
{
    StateNode array = StateNode::Array();
    for (const std::string& value : values)
        array.push(value);
    return array;
}

std::string Joined(const std::vector<std::string>& values)
{
    std::string text;
    for (const std::string& value : values)
        text += (text.empty() ? "" : ", ") + value;
    return text;
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// A bool, 0 / 1, or the words a command line gives (on / off, true / false, yes / no)
bool FlagOf(const StateNode& value, bool& out)
{
    if (value.kind == StateNode::Kind::Bool)
    {
        out = value.b;
        return true;
    }
    if (value.kind == StateNode::Kind::Int && (value.i == 0 || value.i == 1))
    {
        out = value.i == 1;
        return true;
    }
    if (value.kind != StateNode::Kind::String)
        return false;
    const std::string text = Lower(value.s);
    if (text == "true" || text == "on" || text == "1" || text == "yes")
        out = true;
    else if (text == "false" || text == "off" || text == "0" || text == "no")
        out = false;
    else
        return false;
    return true;
}

/// Card options as slots plug takes them: "dip=ym,saa gsRam=2m", or an object {dip: "ym,saa"} / {dip: ["ym", "saa"]}
std::string OptionsText(const StateNode& value)
{
    if (value.kind == StateNode::Kind::String)
        return value.s;
    std::string text;
    if (value.isObject())
    {
        for (const auto& [name, item] : value.members)
        {
            std::string values;
            if (item.isArray())
            {
                for (const StateNode& one : item.items)
                    values += (values.empty() ? "" : ",") + (one.kind == StateNode::Kind::Int ? std::to_string(one.i) : one.s);
                if (values.empty())
                    values = "none";
            }
            else
                values = item.kind == StateNode::Kind::Int ? std::to_string(item.i) : item.s;
            text += (text.empty() ? "" : " ") + name + "=" + values;
        }
    }
    return text;
}

std::vector<std::string> CardIds()
{
    std::vector<std::string> ids;
    for (const slots::CardDef& card : Planner().Data().cards)
    {
        if (!card.socketDefault)
            ids.emplace_back(card.id);
    }
    return ids;
}

/// The plug request of one asked card; false with the refusal
bool FinishCard(CardAsk& ask, const std::string& optionsText, const std::string& adapter, bool replace, std::string& error)
{
    if (ask.slot == "auto")
        ask.slot.clear();
    if (ask.card.empty())
    {
        error = "cards: a card needs its id (\"edge.1=kempston\" or {slot, card})";
        return false;
    }
    const slots::CardDef* card = Planner().FindCard(ask.card);
    if (card == nullptr && ask.card != slots::kEmptySocket)
    {
        error = "cards: unknown card '" + ask.card + "'; the cards: " + Joined(CardIds());
        return false;
    }
    ask.request.op = slots::SlotRequest::Op::Plug;
    ask.request.slot = ask.slot;
    ask.request.card = ask.card;
    ask.request.adapter = adapter;
    ask.request.replaceIfIncompatible = replace;
    std::string optionError;
    if (card != nullptr && !slots::ParseCardOptions(*card, optionsText, ask.request.options, &optionError))
    {
        error = "cards: " + ask.card + ": " + optionError;
        return false;
    }
    return true;
}

/// "edge.1=kempston" (several comma separated) into `out`
bool CardsFromText(const std::string& text, std::vector<CardAsk>& out, std::string& error)
{
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find(',', start);
        if (end == std::string::npos)
            end = text.size();
        std::string word = text.substr(start, end - start);
        word.erase(0, word.find_first_not_of(" \t"));
        word.erase(word.find_last_not_of(" \t") + 1);
        start = end + 1;
        if (word.empty())
            continue;
        const size_t eq = word.find('=');
        if (eq == std::string::npos || eq == 0 || eq + 1 == word.size())
        {
            error = "cards: '" + word + "': expected <slot>=<card>, e.g. edge.1=kempston (slot auto: the planner's choice)";
            return false;
        }
        CardAsk ask;
        ask.slot = word.substr(0, eq);
        ask.card = word.substr(eq + 1);
        if (!FinishCard(ask, {}, {}, false, error))
            return false;
        out.push_back(std::move(ask));
    }
    return true;
}

bool CardsFromArgs(const StateNode& value, std::vector<CardAsk>& out, std::string& error)
{
    if (value.kind == StateNode::Kind::String)
        return CardsFromText(value.s, out, error);
    // An empty Lua table arrives as an empty object: no cards
    if (value.isObject() && value.members.empty())
        return true;
    if (!value.isArray())
    {
        error = "cards: expected a list of \"<slot>=<card>\" strings or {slot, card, options} objects";
        return false;
    }
    for (const StateNode& item : value.items)
    {
        if (item.kind == StateNode::Kind::String)
        {
            if (!CardsFromText(item.s, out, error))
                return false;
            continue;
        }
        if (!item.isObject())
        {
            error = "cards: expected \"<slot>=<card>\" or {slot, card, options}";
            return false;
        }
        CardAsk ask;
        std::string optionsText, adapter;
        bool replace = false;
        for (const auto& [name, field] : item.members)
        {
            if (name == "slot")
                ask.slot = field.s;
            else if (name == "card")
                ask.card = field.s;
            else if (name == "options")
                optionsText = OptionsText(field);
            else if (name == "adapter")
                adapter = field.s;
            else if (name == "replace" || name == "replace_if_incompatible" || name == "replaceIfIncompatible")
            {
                if (!FlagOf(field, replace))
                {
                    error = "cards: " + name + ": expected true or false";
                    return false;
                }
            }
            else
            {
                error = "cards: unknown field '" + name + "' (slot, card, options, adapter, replace)";
                return false;
            }
        }
        if (!FinishCard(ask, optionsText, adapter, replace, error))
            return false;
        out.push_back(std::move(ask));
    }
    return true;
}

/// The feature ids with their default state (a manager without a machine registers the defaults only)
const std::vector<FeatureManager::FeatureInfo>& DefaultFeatures()
{
    static const std::vector<FeatureManager::FeatureInfo> features = FeatureManager(nullptr).listFeatures();
    return features;
}

const FeatureManager::FeatureInfo* FindDefaultFeature(const std::string& idOrAlias)
{
    for (const FeatureManager::FeatureInfo& feature : DefaultFeatures())
    {
        if (feature.id == idOrAlias || (!feature.alias.empty() && feature.alias == idOrAlias))
            return &feature;
    }
    return nullptr;
}

std::vector<std::string> FeatureIds()
{
    std::vector<std::string> ids;
    for (const FeatureManager::FeatureInfo& feature : DefaultFeatures())
        ids.push_back(feature.id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<std::string> CreatableModels()
{
    std::vector<std::string> names;
    for (const TMemModel& model : EmulatorManager::GetInstance()->GetAvailableModels())
    {
        if (model.ShortName != nullptr && Config::IsModelCreatable(model))
            names.emplace_back(model.ShortName);
    }
    for (const MachineVariant& variant : MachineVariants::All())
        names.emplace_back(variant.name);
    return names;
}

/// The fitted cards of a slot set, "slot = card"
std::vector<std::string> FittedOf(const SlotManager::Result& result)
{
    std::vector<std::string> fitted;
    for (const SlotManager::Slot& slot : result.entries)
    {
        if (!slot.entry.disabled)
            fitted.push_back(slot.entry.slot + " = " + slot.entry.card);
    }
    return fitted;
}

/// The buses a card of this machine can go to ("edge", "zxbus"): a slot is <bus>.<n> or <bus>.next
std::vector<std::string> BusesOf(const SlotManager::Result& result)
{
    std::vector<std::string> buses;
    if (result.machine != nullptr)
    {
        for (const slots::BusDef& bus : result.machine->buses)
            buses.emplace_back(bus.id);
    }
    return buses;
}

void Remove(const std::shared_ptr<Emulator>& emulator)
{
    if (emulator)
        EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetId());
}

}  // namespace

StateNode MachinePresetReply::ToValue() const
{
    StateNode value = StateNode::Object();
    value["ok"] = Ok();
    value["status"] = status;
    if (!message.empty())
        value["message"] = message;
    value["text"] = text;
    for (const auto& [key, field] : body.members)
        value[key] = field;
    return value;
}

const std::vector<std::string>& MachinePreset::ArgumentNames()
{
    static const std::vector<std::string> names = {"model",  "ram",       "cards",        "features", "muted",
                                                   "paused", "fast_tape", "take_control", "replace",  "select",
                                                   "client", "lease_seconds"};
    return names;
}

bool MachinePreset::ArgumentsFromWords(const std::vector<std::string>& words, StateNode& args, std::string& error)
{
    args = StateNode::Object();
    for (const std::string& word : words)
    {
        const size_t eq = word.find('=');
        if (eq == std::string::npos)
        {
            if (args.find("model") != nullptr)
            {
                error = "'" + word + "': expected name=value (" + Joined(ArgumentNames()) + ")";
                return false;
            }
            args["model"] = word;
            continue;
        }
        const std::string name = word.substr(0, eq);
        const std::string value = word.substr(eq + 1);
        if (name == "card" || name == "cards")
            args["cards"].push(value);
        else if (name == "feature" || name == "features")
        {
            // feature=kempstonmouse=off, or several: features=kempstonmouse=off,fasttape=on
            size_t start = 0;
            while (start <= value.size())
            {
                size_t end = value.find(',', start);
                if (end == std::string::npos)
                    end = value.size();
                const std::string one = value.substr(start, end - start);
                start = end + 1;
                if (one.empty())
                    continue;
                const size_t at = one.find('=');
                if (at == std::string::npos || at == 0)
                {
                    error = "feature '" + one + "': expected <name>=on|off";
                    return false;
                }
                args["features"][one.substr(0, at)] = one.substr(at + 1);
            }
        }
        else if (name == "lease_seconds")
        {
            if (value.empty() || value.size() > 6 || !std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
            {
                error = "lease_seconds: expected a number of seconds";
                return false;
            }
            args[name] = static_cast<int64_t>(std::stol(value));
        }
        else
            args[name] = value;
    }
    return true;
}

MachinePresetReply MachinePreset::Execute(const StateNode& args, const MachinePresetCaller& caller)
{
    // 1. Everything that needs no machine: a refusal here has changed nothing
    if (!args.isObject())
        return BadRequest("preset: expected an object of arguments (" + Joined(ArgumentNames()) + ")");

    std::string model;
    RamPowerOn ram = RamPowerOn::Zero;
    std::vector<CardAsk> cards;
    std::vector<std::pair<std::string, bool>> features;   // canonical id, state; in the order asked
    bool muted = true, paused = true, fastTape = false, takeControl = true, replace = false, select = false;
    std::string client;
    uint32_t leaseSeconds = 60;

    for (const auto& [name, value] : args.members)
    {
        bool* flag = name == "muted"          ? &muted
                     : name == "paused"       ? &paused
                     : name == "fast_tape"    ? &fastTape
                     : name == "take_control" ? &takeControl
                     : name == "replace"      ? &replace
                     : name == "select"       ? &select
                                              : nullptr;
        if (flag != nullptr)
        {
            if (!FlagOf(value, *flag))
                return BadRequest(name + ": expected true or false");
        }
        else if (name == "model")
        {
            if (value.kind != StateNode::Kind::String)
                return BadRequest("model: expected a model short name; the models: " + Joined(CreatableModels()));
            model = value.s;
        }
        else if (name == "ram" || name == "ram_power_on")
        {
            if (value.kind != StateNode::Kind::String || !Config::ParseRamPowerOn(value.s, ram))
            {
                MachinePresetReply reply = BadRequest(name + ": expected zero or random");
                reply.body["valid"] = Strings({"zero", "random"});
                return reply;
            }
        }
        else if (name == "cards")
        {
            std::string error;
            if (!CardsFromArgs(value, cards, error))
            {
                MachinePresetReply reply = BadRequest(error);
                if (error.find("unknown card") != std::string::npos)
                    reply.body["valid"] = Strings(CardIds());
                return reply;
            }
        }
        else if (name == "features")
        {
            if (value.isArray() && value.items.empty())
                continue;   // an empty list from a script: no features
            if (!value.isObject())
                return BadRequest("features: expected an object {name: true | false}, e.g. {\"kempstonmouse\": false}");
            for (const auto& [feature, state] : value.members)
            {
                const FeatureManager::FeatureInfo* known = FindDefaultFeature(feature);
                if (known == nullptr)
                {
                    MachinePresetReply reply =
                        BadRequest("features: unknown feature '" + feature + "'; the features: " + Joined(FeatureIds()));
                    reply.body["valid"] = Strings(FeatureIds());
                    return reply;
                }
                bool on = false;
                if (!FlagOf(state, on))
                    return BadRequest("features: " + feature + ": expected true or false");
                features.emplace_back(known->id, on);
            }
        }
        else if (name == "client")
            client = value.s;
        else if (name == "lease_seconds")
        {
            if (value.kind != StateNode::Kind::Int || value.i < 0 || value.i > 86400)
                return BadRequest("lease_seconds: expected 0..86400 (0: no idle lease)");
            leaseSeconds = static_cast<uint32_t>(value.i);
        }
        else
        {
            MachinePresetReply reply = BadRequest("preset: unknown argument '" + name + "'; the arguments: " + Joined(ArgumentNames()));
            reply.body["valid"] = Strings(ArgumentNames());
            return reply;
        }
    }

    if (model.empty() || (Config::FindModelByShortName(model) == nullptr && MachineVariants::Find(model) == nullptr &&
                          ZXPolyGroup::FindConfiguration(model) == nullptr))
    {
        MachinePresetReply reply = BadRequest((model.empty() ? std::string("model: required")
                                                          : "model: unknown model '" + model + "'") +
                                           "; the models: " + Joined(CreatableModels()));
        reply.body["valid"] = Strings(CreatableModels());
        return reply;
    }
    if (ZXPolyGroup::FindConfiguration(model) != nullptr)
        return BadRequest("model: '" + model + "' is a ZX-Poly machine of four instances; a preset machine is one "
                          "instance (create it with the create verb)");

    EmulatorManager& manager = *EmulatorManager::GetInstance();

    // replace: another client's explicit session on a machine that would go is not ended from here
    std::vector<std::string> others;
    if (replace)
    {
        for (const std::string& id : manager.GetEmulatorIds())
        {
            const std::shared_ptr<Emulator> other = manager.GetEmulator(id);
            EmulatorContext* context = other ? other->GetContext() : nullptr;
            if (context == nullptr)
                continue;
            const EmulatorContext::RunControlState control = context->GetRunControlState();
            if (control.claimed && !control.implicit && !(caller.holdsControl && caller.holdsControl(*other)))
            {
                MachinePresetReply reply = Refusal(
                    "refused", 409,
                    "replace: instance " + id + " is held (" + EmulatorContext::DescribeHolder(control) +
                        "); nothing was created or destroyed. Create the preset machine without replace, or have that "
                        "session released");
                reply.body["held"] = id;
                return reply;
            }
            others.push_back(id);
        }
    }

    // 2. The machine, with the cards planned into its own slot set while it is created: the plan is the one a
    // slot plug makes (SlotManager::PlanChanges), the machine is built once with the result - no restart
    const auto slotOutcome = std::make_shared<SlotOutcome>();
    std::vector<slots::SlotRequest> requests;
    for (const CardAsk& ask : cards)
        requests.push_back(ask.request);
    const std::function<void(CONFIG&)> createOverride = [ram, requests, slotOutcome](CONFIG& config) {
        config.ramPowerOn = ram;
        if (requests.empty())
            return;
        const SlotManager::Result current = SlotManager::Plan(config);
        SlotManager::ChangePlan plan = SlotManager::PlanChanges(current, config, requests);
        if (plan.Allowed())
            SlotManager::UseSlots(plan.config, config);
        // The first run is the creation's; a later restart of the instance applies the override again
        if (!slotOutcome->planned)
        {
            slotOutcome->planned = true;
            slotOutcome->machineSlots = BusesOf(current);
            slotOutcome->plan = std::move(plan);
        }
    };

    std::string createError;
    std::shared_ptr<Emulator> emulator =
        manager.CreateEmulatorWithModel("", model, LoggerLevel::LogWarning, &createError, createOverride);
    if (!emulator || emulator->GetContext() == nullptr)
    {
        Remove(emulator);
        if (slotOutcome->planned && !slotOutcome->plan.Allowed())
            createError = slotOutcome->plan.refusal;
        return BadRequest("preset: the machine was not created: " +
                          (createError.empty() ? std::string("initialization failed") : createError));
    }
    EmulatorContext* context = emulator->GetContext();

    if (!cards.empty() && (!slotOutcome->planned || !slotOutcome->plan.Allowed()))
    {
        Remove(emulator);
        const bool needsReplace = slotOutcome->planned && !slotOutcome->plan.plan.hardRefusal &&
                                  !slotOutcome->plan.plan.allowed;
        MachinePresetReply reply = Refusal(
            "refused", 409,
            "cards: " + (slotOutcome->planned ? slotOutcome->plan.refusal : std::string("the slot plan was not made")) +
                (needsReplace ? " (a card as {slot, card, replace: true} allows the removals)" : "") +
                ". The buses of " + model + ": " +
                (slotOutcome->machineSlots.empty() ? std::string("none") : Joined(slotOutcome->machineSlots)) +
                " (a slot is <bus>.<n>, <bus>.next, ay-socket or auto). Nothing was created");
        if (slotOutcome->planned)
            reply.body["plan"] = SlotControl::PlanValue(slotOutcome->plan);
        reply.body["buses"] = Strings(slotOutcome->machineSlots);
        return reply;
    }

    // 3. Features: the tape accelerators first, then the names asked for (they win)
    FeatureManager* featureManager = emulator->GetFeatureManager();
    if (featureManager == nullptr)
    {
        Remove(emulator);
        return Refusal("failed", 500, "preset: the machine has no feature manager; nothing was created");
    }
    std::vector<std::pair<std::string, bool>> toSet = {{Features::kFastTape, fastTape}, {Features::kTurboTape, fastTape}};
    toSet.insert(toSet.end(), features.begin(), features.end());
    for (const auto& [id, on] : toSet)
    {
        if (!featureManager->hasFeature(id))
            continue;   // a feature this machine does not register: nothing to switch
        const std::string why = featureManager->refusalReason(id, on);
        if (!featureManager->setFeature(id, on))
        {
            Remove(emulator);
            return Refusal("refused", 409,
                           "features: " + id + " cannot be switched " + (on ? "on" : "off") +
                               (why.empty() ? std::string() : ": " + why) + ". Nothing was created");
        }
    }

    // 4. Sound: the mixer's master (host side only)
    {
        AudioMixer::Change change;
        change.muted = muted ? 1 : 0;
        std::string error;
        if (!AudioMixer::Apply(context, "master", change, error))
        {
            Remove(emulator);
            return Refusal("failed", 500, "muted: " + error + ". Nothing was created");
        }
    }

    // 5. The machine is built: the others go (replace)
    std::vector<std::string> replaced;
    for (const std::string& id : others)
    {
        if (manager.HasEmulator(id) && manager.RemoveEmulator(id))
            replaced.push_back(id);
    }

    // 6. Start. Paused: the pause is queued behind the start's run intent before the emulation thread exists
    // (Emulator::StartAsyncPaused), so the loop's first park holds it: the machine stands at power-on (frame 0, the
    // reset's 3 T-states, no instruction run) whatever the thread's timing. The answer names the position
    const std::string id = emulator->GetId();
    if (paused)
        emulator->StartAsyncPaused(execution::Requester{execution::Origin::Lifecycle, {}}, "preset");
    else if (!manager.StartEmulatorAsync(id))
    {
        Remove(emulator);
        return Refusal("failed", 500, "preset: the machine did not start; it was removed");
    }
    if (paused)
    {
        const execution::Result result = emulator->PauseAndWait(execution::Requester{execution::Origin::Lifecycle, {}},
                                                                std::chrono::milliseconds(5000), "preset");
        const bool parked = (result.outcome == execution::Outcome::Applied ||
                             result.outcome == execution::Outcome::Coalesced) &&
                            emulator->IsPaused();
        if (!parked)
        {
            Remove(emulator);
            return Refusal("failed", 500,
                           "preset: the machine did not pause within 5 s (" + result.detail + "); it was removed");
        }
    }
    // select: only on request the new machine becomes the selected one (the one a GUI shows, the default of calls
    // with no target). Several instances may run side by side, and which one is active is the harness's choice
    if (select)
        manager.SetSelectedEmulatorId(id);

    // 7. Run control for the caller
    StateNode control = StateNode::Object();
    control["held"] = false;
    if (takeControl && caller.takeControl)
    {
        std::string session, reason;
        if (!caller.takeControl(*emulator, client, leaseSeconds, session, reason))
        {
            Remove(emulator);
            return Refusal("refused", 409, "take_control: " + reason + ". The machine was removed; do not retry");
        }
        const EmulatorContext::RunControlState state = context->GetRunControlState();
        control["held"] = true;
        if (!session.empty())
            control["session"] = session;
        control["surface"] = state.surfaceLabel;
        control["client"] = state.client;
        control["lease_seconds"] = static_cast<int64_t>(state.leaseMs / 1000);
    }

    // 8. The answer: what the machine is, read back from it
    MachinePresetReply reply;
    reply.emulator = emulator;
    StateNode& body = reply.body;
    const MachineIdentity identity = EmulatorManager::GetMachineIdentity(*emulator);
    body["id"] = id;
    body["model"] = identity.Valid ? identity.Model : model;
    if (identity.Valid)
    {
        body["model_full_name"] = identity.ModelFullName;
        if (!identity.Variant.empty())
            body["variant"] = identity.Variant;
        body["ram_kb"] = static_cast<int64_t>(identity.RamKb);
    }
    const std::string ramName = Config::RamPowerOnName(context->config.ramPowerOn);
    body["ram"] = ramName;

    StateNode cardsNode = StateNode::Array();
    std::vector<std::string> cardTexts;
    if (context->pSlotManager != nullptr)
    {
        const SlotManager::Result fitted = context->pSlotManager->Snapshot();
        for (const SlotManager::Slot& slot : fitted.entries)
        {
            if (slot.entry.disabled)
                continue;
            StateNode card = StateNode::Object();
            card["slot"] = slot.entry.slot;
            card["card"] = slot.entry.card;
            if (const slots::CardDef* def = Planner().FindCard(slot.entry.card))
            {
                const std::string options = slots::FormatCardOptions(*def, slot.entry.options);
                if (!options.empty())
                    card["options"] = options;
            }
            if (!slot.entry.adapter.empty())
                card["adapter"] = slot.entry.adapter;
            const bool asked = std::any_of(cards.begin(), cards.end(), [&slot](const CardAsk& ask) {
                return ask.card == slot.entry.card && (ask.slot.empty() || ask.slot == slot.entry.slot ||
                                                       ask.slot.find(".next") != std::string::npos);
            });
            card["asked"] = asked;
            cardsNode.push(std::move(card));
        }
        cardTexts = FittedOf(fitted);
    }
    body["cards"] = std::move(cardsNode);

    // Features: the ones asked for, and every one that is not at its default on this machine
    StateNode featuresNode = StateNode::Object();
    StateNode askedNode = StateNode::Object();
    StateNode changedNode = StateNode::Object();
    std::vector<std::string> changedTexts;
    const std::vector<FeatureManager::FeatureInfo> live = featureManager->listFeatures();
    std::map<std::string, bool> liveState;
    for (const FeatureManager::FeatureInfo& feature : live)
        liveState[feature.id] = feature.enabled;
    for (const auto& [featureId, on] : features)
        askedNode[featureId] = liveState.count(featureId) ? liveState[featureId] : on;
    std::vector<std::string> ids;
    for (const auto& [featureId, on] : liveState)
        ids.push_back(featureId);
    for (const std::string& featureId : ids)
    {
        const FeatureManager::FeatureInfo* def = FindDefaultFeature(featureId);
        if (def == nullptr || def->enabled == liveState[featureId])
            continue;
        changedNode[featureId] = liveState[featureId];
        const bool asked = std::any_of(features.begin(), features.end(),
                                       [&featureId](const auto& one) { return one.first == featureId; });
        changedTexts.push_back(featureId + (liveState[featureId] ? " on" : " off") + (asked ? " (asked)" : ""));
    }
    featuresNode["asked"] = std::move(askedNode);
    featuresNode["not_default"] = std::move(changedNode);
    body["features"] = std::move(featuresNode);

    const bool isMuted = AudioMixer::Flag(context, "master", "muted").value_or(muted);
    const bool isPaused = emulator->IsPaused();
    const bool tapeFast = liveState[Features::kFastTape] || liveState[Features::kTurboTape];
    body["muted"] = isMuted;
    body["paused"] = isPaused;
    body["fast_tape"] = tapeFast;
    body["control"] = control;
    StateNode position = StateNode::Object();
    const int64_t frame = static_cast<int64_t>(context->emulatorState.frame_counter);
    const Z80State* z80 = emulator->GetZ80State();
    const int64_t t = z80 != nullptr ? static_cast<int64_t>(z80->t) : 0;
    position["frame"] = frame;
    position["t"] = t;
    if (z80 != nullptr)
        position["pc"] = static_cast<int64_t>(z80->pc);
    body["position"] = std::move(position);
    body["replaced"] = Strings(replaced);
    const bool isSelected = manager.GetSelectedEmulatorId() == id;
    body["selected"] = isSelected;
    const size_t instances = manager.GetEmulatorIds().size();
    body["instances"] = static_cast<int64_t>(instances);

    std::string text = "Preset machine " + id + " ready: " + body.find("model")->s +
                       (identity.Valid ? " (" + std::to_string(identity.RamKb) + " KB)" : std::string()) + ", RAM " +
                       ramName + ", " +
                       (isPaused ? "paused at frame " + std::to_string(frame) + " t " + std::to_string(t)
                                 : std::string("running")) +
                       ".\n";
    text += "Cards: " + (cardTexts.empty() ? std::string("none") : Joined(cardTexts)) + ".\n";
    text += "Features not at their default: " + (changedTexts.empty() ? std::string("none") : Joined(changedTexts)) + ".\n";
    text += std::string("Sound: master ") + (isMuted ? "muted" : "not muted") + ". Fast tape: " +
            (tapeFast ? "on" : "off (fasttape and turbotape off: a tape loads in real machine time)") + ".\n";
    if (control.find("held")->b)
    {
        const StateNode* session = control.find("session");
        text += "Run control: held by you (" + control.find("surface")->s +
                (session != nullptr ? ", session " + session->s : std::string()) + ").";
    }
    else
        text += "Run control: not taken.";
    if (!replaced.empty())
        text += "\nReplaced " + std::to_string(replaced.size()) + " instance(s): " + Joined(replaced) + ".";
    else if (instances > 1)
        text += "\n" + std::to_string(instances) + " instances exist" +
                (isSelected ? std::string(", this one is the selected one.")
                            : ", this one is not the selected one: name it (" + id +
                                  ") in calls that take a target, or create the preset machine with select or replace.");
    reply.text = text;
    return reply;
}
