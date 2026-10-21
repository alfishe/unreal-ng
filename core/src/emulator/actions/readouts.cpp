#include "emulator/actions/readouts.h"

#include <algorithm>
#include <memory>

#include "base/featuremanager.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/tape/tape.h"
#include "emulator/media/mediamanager.h"
#include "emulator/sound/soundmanager.h"

namespace actions
{

namespace
{
/// The file name of a source path ("" for none)
std::string BaseName(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

StateNode RunState(Emulator& emulator)
{
    StateNode node = StateNode::Object();
    node["state"] = !emulator.IsRunning() ? "stopped" : emulator.IsPaused() ? "paused" : "running";
    EmulatorContext* context = emulator.GetContext();
    // The host setting (1..16), not the composed rate with a machine's own turbo
    node["speed"] = static_cast<int64_t>(context != nullptr && context->pCore != nullptr
                                             ? context->pCore->GetHostSpeedMultiplier()
                                             : 1);
    node["turbo"] = emulator.IsTurboMode();
    return node;
}

StateNode Machine(Emulator& emulator)
{
    // Not GetMachineIdentity: it reads the screen and, on ZX-Poly, the group's lockstep; the config fields do not
    // change on a live machine (a model switch makes a new instance)
    StateNode node = StateNode::Object();
    EmulatorContext* context = emulator.GetContext();
    const TMemModel* model = context != nullptr ? Config::FindModelByEnum(context->config.mem_model) : nullptr;
    if (model != nullptr && model->ShortName != nullptr)
        node["model"] = std::string(model->ShortName);
    else
        node["model"] = StateNode();
    node["ram_kb"] = static_cast<int64_t>(context != nullptr ? context->config.ramsize : 0);
    const std::vector<std::string> ids = EmulatorManager::GetInstance()->GetEmulatorIds();
    const auto at = std::find(ids.begin(), ids.end(), emulator.GetId());
    if (at != ids.end())
        node["index"] = static_cast<int64_t>(at - ids.begin());
    else
        node["index"] = StateNode();
    return node;
}

StateNode MediaTape(Emulator& emulator)
{
    StateNode node = StateNode::Object();
    EmulatorContext* context = emulator.GetContext();
    std::optional<SlotInfo> slot;
    if (context != nullptr && context->pMediaManager != nullptr)
        slot = context->pMediaManager->Info("tape");
    node["loaded"] = slot.has_value() && slot->present;
    node["name"] = slot && slot->present ? BaseName(slot->source) : std::string();
    // The tape's plain counters, written by the emulation thread: a read may be one frame old (never
    // Tape::GetPosition, which walks the block the machine may be replacing)
    const Tape* tape = context != nullptr ? context->pTape : nullptr;
    const bool playing = tape != nullptr && tape->IsPlaying();
    const size_t blocks = tape != nullptr ? tape->GetBlockCatalog().size() : 0;
    const size_t block = tape != nullptr ? std::min(tape->GetConsumptionCursor(), blocks) : 0;
    node["playing"] = playing;
    node["block"] = static_cast<int64_t>(block);
    node["blocks"] = static_cast<int64_t>(blocks);
    node["progress"] = blocks > 0 ? static_cast<double>(block) / static_cast<double>(blocks) : 0.0;
    return node;
}

StateNode MediaDisk(Emulator& emulator)
{
    StateNode node = StateNode::Object();
    StateNode& drives = node["drives"];
    drives = StateNode::Array();
    EmulatorContext* context = emulator.GetContext();
    if (context == nullptr || context->pMediaManager == nullptr)
        return node;
    static const char* const kSlots[4] = {"fdd.a", "fdd.b", "fdd.c", "fdd.d"};
    for (int i = 0; i < 4; i++)
    {
        const std::optional<SlotInfo> slot = context->pMediaManager->Info(kSlots[i]);
        if (!slot)
            continue;  // the machine has no such drive
        StateNode drive = StateNode::Object();
        drive["letter"] = std::string(1, static_cast<char>('A' + i));
        drive["loaded"] = slot->present;
        drive["name"] = slot->present ? BaseName(slot->source) : std::string();
        drive["protect"] = slot->writeProtect;
        FDD* fdd = context->coreState.diskDrives[i];
        drive["busy"] = fdd != nullptr && fdd->getMotor();
        drives.push(std::move(drive));
    }
    return node;
}

StateNode AudioMute(Emulator& emulator)
{
    StateNode node = StateNode::Object();
    EmulatorContext* context = emulator.GetContext();
    node["muted"] = context != nullptr && context->pSoundManager != nullptr && context->pSoundManager->isMuted();
    return node;
}

StateNode DebugMode(Emulator& emulator)
{
    // The feature, not Emulator::IsDebug (that one goes stale when the feature is switched directly)
    StateNode node = StateNode::Object();
    FeatureManager* features = emulator.GetFeatureManager();
    node["enabled"] = features != nullptr && features->isEnabled(Features::kDebugMode);
    return node;
}

StateNode TtdPosition(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    if (context == nullptr || context->pTimeTravelHooks == nullptr)
    {
        StateNode node = StateNode::Object();
        node["available"] = false;
        return node;
    }
    // The `position` verb reads the published cursor: it never takes a session operation (control surfaces §7.1)
    ttd::TTDReply reply = ttd::TTDControl(context).Execute({"position", {}});
    StateNode node = std::move(reply.body);
    node["available"] = reply.Ok();
    return node;
}

/// `toggle.<stem>` or `toggle.<stem>.<param value>`: the action and its params; false when no toggle has that name
bool ToggleOf(const std::string& name, std::string& id, StateNode& params)
{
    for (const ActionRegistry::ToggleReadout& toggle : ActionRegistry::Instance().ToggleReadouts())
    {
        if (toggle.param.empty())
        {
            if (name == toggle.stem)
            {
                id = toggle.id;
                params = StateNode::Object();
                return true;
            }
            continue;
        }
        const std::string prefix = toggle.stem + ".";
        if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0)
        {
            id = toggle.id;
            params = StateNode::Object();
            params[toggle.param] = name.substr(prefix.size());
            return true;
        }
    }
    return false;
}
}  // namespace

const std::vector<std::string>& BaseReadoutNames()
{
    static const std::vector<std::string> names = {"ttd.position", "run.state",  "machine",   "media.tape",
                                                   "media.disk",   "audio.mute", "debug.mode"};
    return names;
}

std::vector<std::string> ReadoutNames(Emulator& emulator)
{
    std::vector<std::string> names = BaseReadoutNames();
    FeatureManager* features = emulator.GetFeatureManager();
    for (const ActionRegistry::ToggleReadout& toggle : ActionRegistry::Instance().ToggleReadouts())
    {
        if (toggle.param.empty())
            names.push_back(toggle.stem);
        else if (toggle.stem == "toggle.feature" && features != nullptr)
            for (const FeatureManager::FeatureInfo& feature : features->listFeatures())
                names.push_back(toggle.stem + "." + feature.id);
        // Other param toggles (set.toggle) are read by name only: `toggle.set.fast_tape`
    }
    return names;
}

bool ReadoutMatches(const std::string& pattern, const std::string& name)
{
    if (pattern == "*" || pattern == name)
        return true;
    if (pattern.size() >= 2 && pattern.compare(pattern.size() - 2, 2, ".*") == 0)
    {
        const std::string prefix = pattern.substr(0, pattern.size() - 1);  // "media."
        return name.compare(0, prefix.size(), prefix) == 0;
    }
    return false;
}

StateNode ReadReadout(Emulator& emulator, const std::string& name)
{
    if (emulator.GetContext() == nullptr)
        return {};
    if (name == "ttd.position")
        return TtdPosition(emulator);
    if (name == "run.state")
        return RunState(emulator);
    if (name == "machine")
        return Machine(emulator);
    if (name == "media.tape")
        return MediaTape(emulator);
    if (name == "media.disk")
        return MediaDisk(emulator);
    if (name == "audio.mute")
        return AudioMute(emulator);
    if (name == "debug.mode")
        return DebugMode(emulator);
    std::string id;
    StateNode params;
    if (name.compare(0, 7, "toggle.") == 0 && ToggleOf(name, id, params))
    {
        StateNode node = StateNode::Object();
        const std::optional<bool> on = ActionRegistry::Instance().ToggleState(id, emulator.GetId(), params);
        if (on)
            node["on"] = *on;
        else
            node["on"] = StateNode();  // not known here (no such feature, no machine part for it)
        return node;
    }
    return {};
}

ReadoutSet ReadReadouts(const std::string& instanceId, const std::vector<std::string>& patterns)
{
    ReadoutSet set;
    std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->GetEmulator(instanceId);
    if (!emulator)
    {
        set.unknown = patterns;
        return set;
    }
    const std::vector<std::string> names = ReadoutNames(*emulator);
    if (patterns.empty())
    {
        for (const std::string& name : names)
            set.values[name] = ReadReadout(*emulator, name);
        return set;
    }
    for (const std::string& pattern : patterns)
    {
        bool matched = false;
        for (const std::string& name : names)
            if (ReadoutMatches(pattern, name))
            {
                matched = true;
                if (set.values.find(name) == nullptr)
                    set.values[name] = ReadReadout(*emulator, name);
            }
        // A param toggle not listed (toggle.set.<name>) is read by its name
        if (!matched && pattern.find('*') == std::string::npos)
        {
            StateNode value = ReadReadout(*emulator, pattern);
            if (value.kind != StateNode::Kind::Null)
            {
                set.values[pattern] = std::move(value);
                matched = true;
            }
        }
        if (!matched)
            set.unknown.push_back(pattern);
    }
    return set;
}

}  // namespace actions
