#include "emulator/actions/hostactions.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/featuremanager.h"
#include "common/filehelper.h"
#include "debugger/analyzers/basic-lang/basicencoder.h"
#include "debugger/analyzers/basic-lang/commandtyper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/joystick/debugjoystickmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/timetravelhooks.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/actions/actionparams.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/automationgate.h"
#include "emulator/execution/runcontrol.h"
#include "emulator/machinenames.h"
#include "emulator/io/tape/tape.h"
#include "emulator/media/mediacontrol.h"
#include "emulator/media/modelswitch.h"
#include "emulator/sound/audiomixer.h"
#include "emulator/sound/soundcharactersettings.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/video/screen.h"
#include "emulator/video/screenshotter.h"
#include "emulator/video/viewportcontrol.h"
#include "loaders/snapshot/snapshotlauncher.h"

namespace actions
{
namespace
{
using automation_gate::ScopeMachine;
using execution::RunControl;
using execution::RunReply;

/// region <Rules and results>

/// The companion scope of a route (subpath under /api/v1/emulator/{id}), as the automation gate decides it for the
/// REST step the action replaces: 0 when the route changes nothing
uint32_t RouteScope(const char* method, const std::string& subpath)
{
    return automation_gate::ChangesMachine(method, subpath) ? automation_gate::CompanionScopeOf(method, subpath) : 0;
}

ActionRules Rules(uint32_t scope, bool needsPause = false)
{
    ActionRules rules;
    rules.scope = ActionRules::Fixed(scope);
    rules.needsPause = needsPause;
    return rules;
}

ActionRules RouteRules(const char* method, const char* subpath, bool needsPause = false)
{
    return Rules(RouteScope(method, subpath), needsPause);
}

/// The scope of a route whose last segment is a param ("/feature/" + name)
ActionRules NamedRouteRules(const char* method, std::string prefix, const char* param, std::string fallback)
{
    ActionRules rules;
    rules.scope = [method, prefix = std::move(prefix), param, fallback = std::move(fallback)](
                      Emulator&, const ActionValue& value) {
        return RouteScope(method, prefix + TextParam(value, param).value_or(fallback));
    };
    return rules;
}

std::string NeedsRunning(Emulator& emulator, const ActionValue&)
{
    return emulator.IsPaused() ? std::string("needs a running machine (the ROM types while frames run)")
                               : std::string();
}

std::string NoTtd(Emulator& emulator, const ActionValue&)
{
    EmulatorContext* context = emulator.GetContext();
    return context != nullptr && context->pTimeTravelHooks != nullptr ? std::string()
                                                                      : std::string("time travel is not available");
}

/// Time travel with a recorded history (the published cursor: never waits for the machine)
std::string NoHistory(Emulator& emulator, const ActionValue& value)
{
    if (std::string why = NoTtd(emulator, value); !why.empty())
        return why;
    return emulator.GetContext()->pTimeTravelHooks->PublishedCursor().history ? std::string()
                                                                                : std::string("record first");
}

ActionResult FromRun(RunReply reply)
{
    if (reply.Ok())
        return ActionResult::Success(std::move(reply.body), std::move(reply.message));
    return ActionResult::Failure(std::move(reply.error), std::move(reply.message), std::move(reply.body));
}

ActionResult FromTtd(ttd::TTDReply reply)
{
    if (reply.Ok())
        return ActionResult::Success(std::move(reply.body), std::move(reply.message));
    return ActionResult::Failure(ttd::TTDControlErrorPhrase(reply.error), std::move(reply.message),
                                 std::move(reply.body));
}

ActionResult FromMedia(const MediaReply& reply)
{
    StateNode body = reply.ToValue();
    if (reply.result.Ok())
        return ActionResult::Success(std::move(body),
                                     reply.result.report.empty() ? std::string() : reply.result.report[0]);
    return ActionResult::Failure(ErrorForStatus(reply.HttpStatus()), reply.result.message, std::move(body));
}

ActionResult BadRequest(std::string message)
{
    return ActionResult::Failure("Bad Request", std::move(message));
}

ActionResult Conflict(std::string message)
{
    return ActionResult::Failure("Conflict", std::move(message));
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// A drive letter A..D (or a..d, 0..3) as its index; false otherwise
bool DriveIndex(const std::string& drive, uint8_t& index)
{
    if (drive.size() != 1)
        return false;
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(drive[0])));
    if (c >= 'A' && c <= 'D')
        index = static_cast<uint8_t>(c - 'A');
    else if (c >= '0' && c <= '3')
        index = static_cast<uint8_t>(c - '0');
    else
        return false;
    return true;
}

std::string Extension(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return {};
    return Lower(path.substr(dot + 1));
}

bool OneOfList(const std::vector<std::string>& list, const std::string& item)
{
    for (const std::string& candidate : list)
        if (Lower(candidate) == item || Lower(candidate) == "." + item)
            return true;
    return false;
}

/// endregion </Rules and results>

/// region <Input and BASIC>

DebugKeyboardManager* Keyboard(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    return context != nullptr && context->pDebugManager != nullptr ? context->pDebugManager->GetKeyboardManager()
                                                                   : nullptr;
}

DebugJoystickManager* JoystickManager(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    return context != nullptr && context->pDebugManager != nullptr ? context->pDebugManager->GetJoystickManager()
                                                                   : nullptr;
}

/// The keys of input.key: `keys` (a list) or `key` (one name, or "caps+1"); "" with every name known, else the error
std::string KeyNames(const ActionValue& value, std::vector<std::string>& keys)
{
    keys = ListParam(value, "keys");
    if (keys.empty())
    {
        const std::optional<std::string> key = TextParam(value, "key");
        if (key && DebugKeyboardManager::IsKnownKeyName(*key))
            keys.push_back(*key);  // a name of its own, even one with a '+' in it
        else
            keys = ListParam(value, "key");
    }
    if (keys.empty())
        return "key: a key name, or keys: a list (the WebAPI's GET /keyboard/keys lists the names)";
    for (const std::string& key : keys)
        if (!DebugKeyboardManager::IsKnownKeyName(key))
            return "Unknown key '" + key + "' (the WebAPI's GET /keyboard/keys lists the names)";
    return {};
}

ActionResult FromTyper(const CommandTyper::Result& result, const std::string& text)
{
    using Failure = CommandTyper::Failure;
    StateNode body = StateNode::Object();
    const bool ok = CommandTyper::Succeeded(result);
    body["success"] = ok;
    body["outcome"] = CommandTyper::OutcomeName(result.outcome);
    if (result.failure != Failure::None)
        body["failure"] = CommandTyper::FailureName(result.failure);
    body["text"] = text;
    body["err_nr"] = static_cast<int>(result.errNr);
    body["report"] = static_cast<int>(static_cast<uint8_t>(result.errNr + 1));
    body["bytes_typed"] = static_cast<uint64_t>(result.bytesTyped);
    body["frames"] = result.frames;
    if (ok)
        return ActionResult::Success(std::move(body), result.message);
    const std::string error = result.failure == Failure::UnknownTarget ? "Bad Request" : "Conflict";
    return ActionResult::Failure(
        error, result.message.empty() ? CommandTyper::FailureName(result.failure) : result.message, std::move(body));
}

ActionResult FromJoystick(const JoystickInjectResult& result)
{
    StateNode body = StateNode::Object();
    if (!result.warning.empty())
        body["warning"] = result.warning;
    if (result.ok())
        return ActionResult::Success(std::move(body), result.message);
    switch (result.status)
    {
        case JoystickInjectStatus::InvalidArgument:
            return ActionResult::Failure("Bad Request", result.message, std::move(body));
        case JoystickInjectStatus::ReplayActive:
            return ActionResult::Failure("not_now", result.message, std::move(body));
        default:
            return ActionResult::Failure("Conflict", result.message, std::move(body));
    }
}

/// endregion </Input and BASIC>

/// region <Settings>

/// The settings of PUT /settings/{name} a surface sets (the WebAPI route's rules): speed, the fast / turbo loading
/// shortcuts, turbo mode and its audio, TR-DOS traps, the sound character (ay_voicing, ay_punch, ay_room,
/// beeper_punch)
ActionResult SetSetting(Emulator& emulator, const std::string& name, const StateNode& value)
{
    EmulatorContext* context = emulator.GetContext();
    StateNode body = StateNode::Object();
    body["name"] = name;
    if (const SoundCharacterSettings::Descriptor* sound = SoundCharacterSettings::Find(name))
    {
        if (context->pSoundManager == nullptr)
            return Conflict("no sound manager");
        std::string text;
        if (value.kind == StateNode::Kind::Bool)
            text = value.b ? "on" : "off";
        else if (value.kind == StateNode::Kind::String)
            text = value.s;
        else if (sound->isNumber && (value.kind == StateNode::Kind::Int || value.kind == StateNode::Kind::Double))
            text = value.kind == StateNode::Kind::Int ? std::to_string(value.i) : std::to_string(value.d);
        else
            return BadRequest(name + ": expected a string" + (sound->isBool     ? " or a bool"
                                                              : sound->isNumber ? " or a number"
                                                                                : ""));
        std::string error;
        if (!SoundCharacterSettings::Set(*context->pSoundManager, sound->name, text, error))
            return BadRequest(error);
        body["value"] = SoundCharacterSettings::Get(*context->pSoundManager, sound->name);
        return ActionResult::Success(std::move(body), name + " set (applied at the next frame boundary)");
    }
    if (name == "audio_rate")
    {
        // The core audio rate pin (the route's PUT /settings/audio_rate): a supported rate, or auto / 0 to release it
        SoundManager* sound = context->pSoundManager;
        if (sound == nullptr)
            return Conflict("no sound manager");
        int64_t rate = -1;
        if (value.kind == StateNode::Kind::Int)
            rate = value.i;
        else if (value.kind == StateNode::Kind::String)
        {
            const std::string text = Lower(value.s);
            if (text == "auto" || text == "device" || text == "default")
                rate = 0;
            else if (!text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c); }))
                rate = std::stoll(text.substr(0, 9));
        }
        if (rate < 0 || (rate != 0 && !IsSupportedCoreRate(static_cast<uint32_t>(rate))))
            return BadRequest("audio_rate: 44100, 48000, 88200, 96000, 176400, 192000 or auto");
        sound->setCoreRatePin(static_cast<uint32_t>(rate));
        if (rate == 0)
            body["value"] = "auto";
        else
            body["value"] = rate;
        body["core_rate_hz"] = static_cast<int64_t>(sound->getCoreRate());
        return ActionResult::Success(std::move(body), rate == 0 ? "audio_rate auto (the pin released)"
                                                                : "audio_rate pinned (applied at the next frame "
                                                                  "boundary, deferred while a recording runs)");
    }
    if (name == "speed")
    {
        const int64_t multiplier = value.kind == StateNode::Kind::Int ? value.i : 0;
        if (multiplier != 1 && multiplier != 2 && multiplier != 4 && multiplier != 8 && multiplier != 16)
            return BadRequest("speed must be 1, 2, 4, 8 or 16");
        if (!emulator.SetSpeedMultiplier(static_cast<uint8_t>(multiplier)))
        {
            const std::string why = emulator.SpeedLockReason();
            return Conflict(why.empty() ? "Cannot change the speed while TTD recording is active (only 1)" : why);
        }
        body["value"] = multiplier;
        return ActionResult::Success(std::move(body), "Speed multiplier set to " + std::to_string(multiplier) + "x");
    }
    if (value.kind != StateNode::Kind::Bool)
        return BadRequest(name + ": expected a bool");
    const bool on = value.b;
    body["value"] = on;
    FeatureManager* features = context->pFeatureManager;
    const char* feature = name == "fast_tape"    ? Features::kFastTape
                          : name == "turbo_tape" ? Features::kTurboTape
                          : name == "fast_disk"  ? Features::kFastDisk
                          : name == "turbo_mode" ? Features::kTurboMode
                                                 : nullptr;
    if (feature != nullptr)
    {
        // Through FeatureManager, so TTD's holds apply (no turbo, no fast loading while recording)
        if (features == nullptr)
            return Conflict("FeatureManager not available");
        if (!features->setFeature(feature, on) && (on || name != "turbo_mode"))
            return Conflict(features->refusalReason(feature, on));
        if (name == "fast_disk")
            context->config.wd93_nodelay = on;
        return ActionResult::Success(std::move(body), name + (on ? " on" : " off"));
    }
    if (name == "turbo_audio")
    {
        context->config.turbo_mode_audio = on;
        if (context->config.turbo_mode)
            emulator.EnableTurboMode(on);
        return ActionResult::Success(std::move(body), std::string("Audio in turbo mode ") + (on ? "on" : "off"));
    }
    if (name == "trdos_traps")
    {
        context->config.trdos_traps = on;
        return ActionResult::Success(std::move(body), std::string("TR-DOS traps ") + (on ? "on" : "off"));
    }
    return ActionResult::Failure("Not Found", "Unknown setting: " + name);
}

/// The current value of a bool setting set.toggle flips; nullopt for a name it does not know
std::optional<bool> SettingIsOn(Emulator& emulator, const std::string& name)
{
    EmulatorContext* context = emulator.GetContext();
    FeatureManager* features = context->pFeatureManager;
    if (name == "turbo_mode")
        return emulator.IsTurboMode();
    if (name == "fast_tape" || name == "turbo_tape" || name == "fast_disk")
    {
        const char* feature = name == "fast_tape"    ? Features::kFastTape
                              : name == "turbo_tape" ? Features::kTurboTape
                                                     : Features::kFastDisk;
        return features != nullptr && features->isEnabled(feature);
    }
    if (name == "turbo_audio")
        return context->config.turbo_mode_audio;
    if (name == "trdos_traps")
        return context->config.trdos_traps;
    if (const SoundCharacterSettings::Descriptor* sound = SoundCharacterSettings::Find(name);
        sound != nullptr && sound->isBool && context->pSoundManager != nullptr)
        return SoundCharacterSettings::Get(*context->pSoundManager, sound->name) == "on";
    return std::nullopt;
}

/// endregion </Settings>

/// region <Media and snapshots>

ActionResult LoadSnapshot(const std::string& instanceId, const std::string& path, std::optional<bool> switchModel)
{
    SnapshotLoadRequest request;
    request.emulatorId = instanceId;
    request.path = path;
    request.switchModel = switchModel;
    const SnapshotLoadResult result = SnapshotLauncher::Load(request);
    StateNode body = StateNode::Object();
    body["kind"] = "snapshot";
    body["path"] = path;
    body["model_switched"] = result.modelSwitched;
    if (result.emulator)
        body["emulator_id"] = result.emulator->GetId();
    if (!result.requiredModel.empty())
        body["required_model"] = result.requiredModel;
    if (!result.ok)
        return ActionResult::Failure(result.modelMismatch ? "Conflict" : "Bad Request", result.message,
                                     std::move(body));
    return ActionResult::Success(std::move(body), "Snapshot loaded");
}

/// LOAD "" typed into the editor (the 128K menu left for 48K BASIC first); "" when it was typed, else why not
std::string TypeTapeLoad(Emulator& emulator)
{
    if (emulator.IsPaused())
        return "the machine is paused: type LOAD \"\" yourself";
    if (Memory* memory = emulator.GetMemory();
        memory != nullptr && BasicEncoder::detectState(memory) == BasicEncoder::BasicState::Menu128K)
        BasicEncoder::navigateToBasic48K(&emulator);
    const CommandTyper::Result typed = CommandTyper::TypeAndWait(emulator, "LOAD \"\"", CommandTyper::Options{});
    if (CommandTyper::Succeeded(typed))
        return {};
    return typed.message.empty() ? std::string(CommandTyper::FailureName(typed.failure)) : typed.message;
}

ActionResult OpenTape(Emulator& emulator, const std::string& path, bool autostart)
{
    StateNode body = StateNode::Object();
    body["kind"] = "tape";
    body["path"] = path;
    if (std::string refusal = emulator.AskMediaChange(Emulator::MediaChange::LoadTape, 0, path); !refusal.empty())
        return ActionResult::Failure("Conflict", std::move(refusal), std::move(body));
    std::string reason;
    if (!emulator.LoadTape(path, &reason))
        return ActionResult::Failure("Bad Request", "Failed to load tape: " + reason, std::move(body));
    if (!autostart)
        return ActionResult::Success(std::move(body), "Tape loaded");
    const std::string why = TypeTapeLoad(emulator);
    body["autostarted"] = why.empty();
    if (!why.empty())
        body["autostart_message"] = why;
    return ActionResult::Success(std::move(body), why.empty() ? "Tape loaded, LOAD \"\" typed" : "Tape loaded; " + why);
}

ActionResult OpenDisk(Emulator& emulator, const std::string& path, uint8_t drive, bool autostart)
{
    StateNode body = StateNode::Object();
    body["kind"] = "disk";
    body["path"] = path;
    body["drive"] = std::string(1, static_cast<char>('A' + drive));
    if (std::string refusal = emulator.AskMediaChange(Emulator::MediaChange::LoadDisk, drive, path); !refusal.empty())
        return ActionResult::Failure("Conflict", std::move(refusal), std::move(body));
    if (autostart && drive == 0)
    {
        // A quick reset into TR-DOS that runs the disk (drive A only, as the route's autostart)
        const Emulator::DiskAutostartResult result = emulator.AutostartDisk(path, drive);
        body["autostarted"] = result.started;
        body["autostart_message"] = result.message;
        if (!result.mounted)
            return ActionResult::Failure("Bad Request", "Failed to insert disk: " + result.message, std::move(body));
        return ActionResult::Success(std::move(body), result.message);
    }
    std::string error;
    if (!emulator.LoadDisk(path, drive, &error))
        return ActionResult::Failure("Bad Request", "Failed to insert disk: " + error, std::move(body));
    return ActionResult::Success(std::move(body), "Disk inserted");
}

/// A MediaControl verb on params.drive (the slot selector: A..D, tape, an id or alias), the other params as its
/// options (`path` as the request's path)
ActionResult MediaVerb(const ActionCall& call, const char* verb)
{
    const std::optional<std::string> drive = TextParam(call.value, "drive");
    if (!drive || drive->empty())
        return BadRequest("drive: the slot (A..D, tape, or a slot id)");
    MediaRequest request;
    request.verb = verb;
    request.selector = *drive;
    request.path = TextParam(call.value, "path").value_or("");
    request.options = ParamOptions(call.value.params, {"drive", "path"});
    return FromMedia(MediaControl(call.emulator->GetContext()).Execute(request));
}

std::string QuickSnapshotPath(const ActionValue& value, int64_t slot)
{
    std::string folder = TextParam(value, "dir").value_or("");
    if (folder.empty())
    {
        const std::string user = FileHelper::GetUserDataPath();
        if (user.empty())
            return {};
        folder = FileHelper::PathCombine(user, "snapshots");
    }
    return FileHelper::PathCombine(folder, "quick-" + std::to_string(slot) + ".szx");
}

ActionResult SaveSnapshotTo(Emulator& emulator, const std::string& path)
{
    const bool saved = emulator.SaveSnapshot(path);
    const snapshot::SaveResult& result = emulator.LastSaveResult();
    StateNode body = result.ToStateNode();
    body["path"] = path;
    if (saved)
        return ActionResult::Success(std::move(body), "Snapshot saved: " + path);
    body["formats"] = emulator.SnapshotSaveFormats().ToStateNode();
    return ActionResult::Failure("Conflict", result.text.empty() ? result.reason : result.text, std::move(body));
}

/// media.open (and media.open_recent): a snapshot, a tape or a disk by its extension or `kind`
ActionResult OpenMedia(const ActionCall& call)
{
    const std::string path = TextParam(call.value, "path").value_or("");
    if (path.empty())
        return BadRequest("path: the file to open");
    if (!FileHelper::FileExists(path) && !FileHelper::FolderExists(path))
        return ActionResult::Failure("Not Found", "No such file: " + path);
    std::string error;
    const bool autostart = BoolParam(call.value, "autostart", error).value_or(true);
    const std::optional<bool> switchModel = BoolParam(call.value, "switch_model", error);
    if (!error.empty())
        return BadRequest(error);
    std::string kind = Lower(TextParam(call.value, "kind").value_or("auto"));
    if (kind == "auto")
    {
        const std::string ext = Extension(path);
        kind = OneOfList(Emulator::SupportedSnapshotExtensions(), ext) ? "snapshot"
               : Emulator::IsTapeExtension(ext) || OneOfList(Emulator::SupportedTapeExtensions(), ext) ? "tape"
               : OneOfList(Emulator::SupportedDiskExtensions(), ext)                                   ? "disk"
                                                                                                       : "";
        if (kind.empty())
            return BadRequest("Unknown file type '." + ext + "': give kind (snapshot, tape or disk)");
    }
    if (kind == "snapshot")
        return LoadSnapshot(call.instanceId, path, switchModel);
    if (kind == "tape")
        return OpenTape(*call.emulator, path, autostart);
    if (kind == "disk")
    {
        uint8_t drive = 0;
        if (!DriveIndex(TextParam(call.value, "drive").value_or("A"), drive))
            return BadRequest("drive: A..D");
        return OpenDisk(*call.emulator, path, drive, autostart);
    }
    return BadRequest("kind: auto, snapshot, tape or disk");
}

/// A size as bytes: a number, or with a K / M / G suffix (binary: 64M = 64 MiB); false otherwise
bool ParseSize(const std::string& text, uint64_t& bytes)
{
    if (text.empty())
        return false;
    uint64_t scale = 1;
    std::string digits = text;
    const char last = static_cast<char>(std::toupper(static_cast<unsigned char>(digits.back())));
    if (last == 'K' || last == 'M' || last == 'G')
    {
        scale = last == 'K' ? 1024ull : last == 'M' ? 1024ull * 1024 : 1024ull * 1024 * 1024;
        digits.pop_back();
    }
    if (digits.empty() || digits.size() > 12 ||
        !std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
        return false;
    bytes = std::stoull(digits) * scale;
    return true;
}

/// media.create: MediaControl's create, with a size that may carry a K / M / G suffix
ActionResult CreateMedium(const ActionCall& call)
{
    const std::optional<std::string> size = TextParam(call.value, "size");
    if (!size)
        return MediaVerb(call, "create");
    uint64_t bytes = 0;
    if (!ParseSize(*size, bytes))
        return BadRequest("size '" + *size + "': bytes, or a number with K, M or G (64M)");
    ActionValue value = call.value;
    value.params["size"] = bytes;
    const ActionCall sized{call.info, call.emulator, call.instanceId, value, call.origin, call.requester};
    return MediaVerb(sized, "create");
}

/// endregion </Media and snapshots>

/// region <Machine>

ActionResult StartMachine(const ActionCall& call)
{
    std::string error;
    const std::optional<std::string> model = TextParam(call.value, "model");
    const std::optional<int64_t> ramKb = IntParam(call.value, "ram_kb", error);
    const std::optional<bool> replace = BoolParam(call.value, "replace", error);
    const std::optional<bool> select = BoolParam(call.value, "select", error);
    if (!error.empty())
        return BadRequest(error);
    // A model, a machine variant (TSL-VDAC2, PROFI-PLUS, their aliases) or a ZX-Poly configuration: what
    // EmulatorManager creates by name
    machines::ResolvedName found;
    const uint32_t asked = ramKb ? static_cast<uint32_t>(std::max<int64_t>(0, *ramKb)) : 0;
    if (!machines::Resolve(model.value_or(""), asked, found, error))
        return BadRequest(error);
    const uint32_t ram = found.kind == machines::NameKind::Model ? found.ramKb : 0;
    std::optional<RamPowerOn> powerOn;
    if (const std::optional<std::string> text = TextParam(call.value, "ram_power_on"))
    {
        RamPowerOn mode = RamPowerOn::Random;
        if (!Config::ParseRamPowerOn(*text, mode))
            return BadRequest("ram_power_on: expected random or zero");
        powerOn = mode;
    }
    EmulatorManager& manager = *EmulatorManager::GetInstance();
    StateNode body = StateNode::Object();
    body["model"] = found.name;
    if (replace.value_or(true))
    {
        // In place: the new machine next to the old one, media and cards carried, the symbolic id and the selection
        // kept, the old one removed (its actions worker ends after this job)
        if (std::string refusal = call.emulator->AskModelSwitch("to " + *model); !refusal.empty())
            return Conflict(std::move(refusal));
        ModelSwitchRequest request;
        request.emulatorId = call.instanceId;
        request.model = found.name;
        request.ramKb = ram;
        request.stranded = StrandedMedia::Keep;
        if (const std::optional<std::string> text = TextParam(call.value, "stranded");
            text && !ModelSwitch::ParseStranded(*text, request.stranded))
            return BadRequest("stranded: expected refuse, save, discard or keep");
        request.ramPowerOn = powerOn;
        const ModelSwitchResult switched = ModelSwitch::Run(request);
        if (!switched.result.Ok() || !switched.emulator)
            return Conflict("the machine was not replaced: " + switched.result.message);
        manager.StartEmulatorAsync(switched.emulator->GetId());
        body["replaced"] = call.instanceId;
        body["emulator_id"] = switched.emulator->GetId();
        body["symbolic_id"] = switched.emulator->GetSymbolicId();
        return ActionResult::Success(std::move(body), "Replaced by a " + found.name);
    }
    std::function<void(CONFIG&)> configOverride;
    if (powerOn)
        configOverride = [mode = *powerOn](CONFIG& config) { config.ramPowerOn = mode; };
    std::shared_ptr<Emulator> created =
        ram > 0 ? manager.CreateEmulatorWithModelAndRAM("", found.name, ram, LoggerLevel::LogWarning, &error,
                                                        configOverride)
                : manager.CreateEmulatorWithModel("", found.name, LoggerLevel::LogWarning, &error, configOverride);
    if (!created)
        return BadRequest(error.empty() ? std::string("Emulator initialization failed") : error);
    const bool started = manager.StartEmulatorAsync(created->GetId());
    if (started && select.value_or(true))
        manager.SetSelectedEmulatorId(created->GetId());
    body["emulator_id"] = created->GetId();
    body["started"] = started;
    return ActionResult::Success(std::move(body), "Started a " + found.name);
}

ActionResult ColdReset(const ActionCall& call)
{
    EmulatorContext* context = call.emulator->GetContext();
    const TMemModel* model = Config::FindModelByEnum(context->config.mem_model);
    if (model == nullptr)
        return Conflict("the machine's model has no name to restart it with");
    if (std::string refusal = call.emulator->AskModelSwitch("restart"); !refusal.empty())
        return Conflict(std::move(refusal));
    ModelSwitchRequest request;
    request.emulatorId = call.instanceId;
    request.model = model->ShortName;
    request.ramKb = context->config.ramsize;
    request.keepConfigOverride = true;
    request.stranded = StrandedMedia::Keep;
    if (const std::optional<std::string> text = TextParam(call.value, "ram_power_on"))
    {
        RamPowerOn mode = RamPowerOn::Random;
        if (!Config::ParseRamPowerOn(*text, mode))
            return BadRequest("ram_power_on: expected random or zero");
        request.ramPowerOn = mode;
    }
    const ModelSwitch::RunStateBefore before = ModelSwitch::RunStateBefore::Of(*call.emulator);
    const ModelSwitchResult restarted = ModelSwitch::Run(request);
    if (!restarted.result.Ok() || !restarted.emulator)
        return Conflict("the machine was not restarted: " + restarted.result.message);
    // The run state the user had: a paused machine comes back started and paused, not "initialized"
    const std::string newId = restarted.emulator->GetId();
    ModelSwitch::StartAsBefore(*restarted.emulator, before,
                               [&newId]() { EmulatorManager::GetInstance()->StartEmulatorAsync(newId); });
    StateNode body = StateNode::Object();
    body["replaced"] = call.instanceId;
    body["emulator_id"] = restarted.emulator->GetId();
    body["model"] = std::string(model->ShortName);
    return ActionResult::Success(std::move(body), std::string("Restarted the ") + model->ShortName);
}

/// endregion </Machine>
}  // namespace

std::vector<RegistrationHandle> RegisterHostActions(ActionRegistry& registry)
{
    std::vector<RegistrationHandle> handles;
    auto add = [&](const char* id, const char* label, const char* group, ValueKind kind, const char* params,
                   Executor run, ActionRules rules, bool held = false) {
        handles.push_back(
            registry.Register({id, label, group, kind, held, false, params, {}, false}, std::move(run), std::move(rules)));
    };

    // region <Run: explicit pause / resume, counted runs>
    const char* run = "Run";
    add(
        "run.pause", "Pause", run, ValueKind::Press, "",
        [](const ActionCall& call) { return FromRun(RunControl(call.emulator).Pause(call.requester)); }, Rules(0));
    add(
        "run.resume", "Continue", run, ValueKind::Press, "",
        [](const ActionCall& call) { return FromRun(RunControl(call.emulator).Resume(call.requester)); },
        RouteRules("POST", "/resume"));
    add(
        "run.steps", "Step N instructions", run, ValueKind::Press, "count? (1..100000, 1)",
        [](const ActionCall& call) {
            std::string error;
            const int64_t count = IntParam(call.value, "count", error).value_or(1);
            if (!error.empty())
                return BadRequest(error);
            return FromRun(RunControl(call.emulator)
                               .Steps(call.requester, static_cast<unsigned>(std::clamp<int64_t>(count, 1, 100000))));
        },
        RouteRules("POST", "/steps", true));
    add(
        "run.frames", "Run N frames", run, ValueKind::Press, "count? (1..10000, 1)",
        [](const ActionCall& call) {
            std::string error;
            const int64_t count = IntParam(call.value, "count", error).value_or(1);
            if (!error.empty())
                return BadRequest(error);
            return FromRun(RunControl(call.emulator)
                               .RunFrames(call.requester, static_cast<unsigned>(std::clamp<int64_t>(count, 1, 10000))));
        },
        RouteRules("POST", "/run_frames", true));
    add(
        "run.tstates", "Run N t-states", run, ValueKind::Press, "count? (1..10000000, 1)",
        [](const ActionCall& call) {
            std::string error;
            const int64_t count = IntParam(call.value, "count", error).value_or(1);
            if (!error.empty())
                return BadRequest(error);
            return FromRun(
                RunControl(call.emulator)
                    .RunTStates(call.requester, static_cast<uint64_t>(std::clamp<int64_t>(count, 1, 10000000))));
        },
        RouteRules("POST", "/run_tstates", true));
    // endregion </Run>

    // region <Machine and capture>
    const char* machine = "Capture and machine";
    {
        ActionRules rules = Rules(ScopeMachine);
        add("mach.start", "Start a machine", machine, ValueKind::Press,
            "model (a short name, a variant such as TSL-VDAC2 or PROFI-PLUS, a ZX-Poly configuration such as "
            "ZXPOLY-128K: GET /emulator/models), ram_kb?, replace? (true: in place), stranded? (keep: refuse|save|"
            "discard|keep), ram_power_on? (random|zero), select? (true)",
            StartMachine, std::move(rules));
    }
    add("mach.cold_reset", "Restart (cold)", machine, ValueKind::Press, "ram_power_on? (random|zero)", ColdReset,
        Rules(ScopeMachine));
    add(
        "cap.screenshot_save", "Save a screenshot", machine, ValueKind::Press,
        "path, format? (png|gif), area?, source? (presented|live|window)",
        [](const ActionCall& call) {
            const std::string path = TextParam(call.value, "path").value_or("");
            if (path.empty())
                return BadRequest("path: the file to write");
            ScreenshotOptions options;
            std::string bad;
            if (!Screenshotter::ParseRequestWords(TextParam(call.value, "area").value_or(""),
                                                  TextParam(call.value, "mode").value_or(""),
                                                  TextParam(call.value, "format").value_or(""),
                                                  TextParam(call.value, "source").value_or(""), options, bad))
                return BadRequest(bad);
            options.saveTo = path;
            const ScreenshotResult shot = Screenshotter::Take(call.instanceId, options);
            if (!shot.ok)
                return ActionResult::Failure(shot.error == ScreenshotError::BadParameter ? "Bad Request"
                                             : shot.error == ScreenshotError::NotFound   ? "Not Found"
                                                                                         : "Conflict",
                                             shot.errorMessage);
            StateNode body = StateNode::Object();
            body["file"] = shot.savedFile;
            body["format"] = Screenshotter::FormatName(shot.format);
            body["width"] = static_cast<int>(shot.width);
            body["height"] = static_cast<int>(shot.height);
            return ActionResult::Success(std::move(body), "Screenshot saved: " + shot.savedFile);
        },
        RouteRules("GET", "/capture/screen"));
    // endregion </Machine and capture>

    // region <Input>
    const char* input = "Input";
    add(
        "input.key", "Key", input, ValueKind::Press,
        "key (or keys: a list, \"caps+1\"), hold? (false: a tap), frames? (2)",
        [](const ActionCall& call) {
            std::vector<std::string> keys;
            if (std::string bad = KeyNames(call.value, keys); !bad.empty())
                return BadRequest(std::move(bad));
            std::string error;
            const bool hold = BoolParam(call.value, "hold", error).value_or(false);
            const int64_t frames = IntParam(call.value, "frames", error).value_or(2);
            if (!error.empty())
                return BadRequest(error);
            DebugKeyboardManager* keyboard = Keyboard(*call.emulator);
            if (keyboard == nullptr)
                return Conflict("Keyboard manager not available");
            StateNode body = StateNode::Object();
            StateNode names = StateNode::Array();
            for (const std::string& key : keys)
                names.items.emplace_back(key);
            body["keys"] = std::move(names);
            if (!hold)
            {
                if (!call.value.pressed)
                {
                    body["ignored"] = "release";  // a tap is whole on the press
                    return ActionResult::Success(std::move(body));
                }
                const auto holdFrames = static_cast<uint16_t>(std::clamp<int64_t>(frames, 1, 1000));
                if (keys.size() == 1)
                    keyboard->TapKey(keys[0], holdFrames);
                else
                    keyboard->TapCombo(keys, holdFrames);
                body["frames"] = static_cast<int>(holdFrames);
                return ActionResult::Success(std::move(body), "Tapped");
            }
            if (call.value.pressed)
                keys.size() == 1 ? keyboard->PressKey(keys[0]) : keyboard->PressCombo(keys);
            else
                keys.size() == 1 ? keyboard->ReleaseKey(keys[0]) : keyboard->ReleaseCombo(keys);
            body["pressed"] = call.value.pressed;
            return ActionResult::Success(std::move(body), call.value.pressed ? "Pressed" : "Released");
        },
        RouteRules("POST", "/keyboard/press"), true);
    {
        ActionRules rules = RouteRules("POST", "/keyboard/type");
        // Tokenized typing is proven on the ROM's control points, frame by frame: the machine must run
        rules.unavailable = [](Emulator& emulator, const ActionValue& value) {
            std::string error;
            return BoolParam(value, "tokenized", error).value_or(false) ? NeedsRunning(emulator, value) : std::string();
        };
        add(
            "input.type", "Type text", input, ValueKind::Text, "tokenized? (false), delay_frames? (2)",
            [](const ActionCall& call) {
                const std::string& text = call.value.text;
                if (text.empty())
                    return BadRequest("text: what to type");
                std::string error;
                const bool tokenized = BoolParam(call.value, "tokenized", error).value_or(false);
                const int64_t delay = IntParam(call.value, "delay_frames", error).value_or(2);
                if (!error.empty())
                    return BadRequest(error);
                if (tokenized)
                {
                    CommandTyper::Options options;
                    options.pressEnter = false;
                    return FromTyper(CommandTyper::TypeAndWait(*call.emulator, text, options), text);
                }
                DebugKeyboardManager* keyboard = Keyboard(*call.emulator);
                if (keyboard == nullptr)
                    return Conflict("Keyboard manager not available");
                const std::string untypeable =
                    keyboard->TypeText(text, static_cast<uint16_t>(std::clamp<int64_t>(delay, 0, 1000)));
                if (!untypeable.empty())
                    return BadRequest("no key types these characters, nothing was typed: " + untypeable);
                StateNode body = StateNode::Object();
                body["text"] = text;
                return ActionResult::Success(std::move(body), "Typing queued");
            },
            std::move(rules));
    }
    add(
        "input.macro", "Key macro", input, ValueKind::Press, "name (e_mode, cat, format, break, ...)",
        [](const ActionCall& call) {
            const std::string name = TextParam(call.value, "name").value_or("");
            if (name.empty())
                return BadRequest("name: the macro (e_mode, g_mode, cat, format, break, ...)");
            DebugKeyboardManager* keyboard = Keyboard(*call.emulator);
            if (keyboard == nullptr)
                return Conflict("Keyboard manager not available");
            if (!keyboard->ExecuteNamedSequence(name))
                return BadRequest("Unknown macro '" + name + "'");
            StateNode body = StateNode::Object();
            body["name"] = name;
            return ActionResult::Success(std::move(body), "Macro started: " + name);
        },
        RouteRules("POST", "/keyboard/macro"));
    add(
        "input.joystick", "Joystick", input, ValueKind::Press,
        "buttons (up, down, left, right, fire; a list or \"up+fire\"), or dir + fire?; hold? (false: a tap), frames?",
        [](const ActionCall& call) {
            std::vector<std::string> buttons = ListParam(call.value, "buttons");
            for (const std::string& dir : ListParam(call.value, "dir"))
                buttons.push_back(dir);
            std::string error;
            if (BoolParam(call.value, "fire", error).value_or(false))
                buttons.emplace_back("fire");
            const bool hold = BoolParam(call.value, "hold", error).value_or(false);
            const std::optional<int64_t> frames = IntParam(call.value, "frames", error);
            if (!error.empty())
                return BadRequest(error);
            if (buttons.empty())
                return BadRequest("buttons: up, down, left, right, fire (or dir and fire)");
            std::string names;
            for (const std::string& button : buttons)
                names += (names.empty() ? "" : "+") + button;
            DebugJoystickManager* joystick = JoystickManager(*call.emulator);
            if (joystick == nullptr)
                return Conflict("Joystick manager not available");
            if (!hold)
            {
                if (!call.value.pressed)
                {
                    StateNode body = StateNode::Object();
                    body["ignored"] = "release";
                    return ActionResult::Success(std::move(body));
                }
                return FromJoystick(
                    joystick->TapChecked(names, frames.value_or(DebugJoystickManager::DEFAULT_TAP_FRAMES)));
            }
            return FromJoystick(call.value.pressed ? joystick->Press(names) : joystick->Release(names));
        },
        RouteRules("POST", "/joystick/press"), true);
    add(
        "input.release_all", "Release all keys and buttons", input, ValueKind::Press, "",
        [](const ActionCall& call) {
            if (DebugKeyboardManager* keyboard = Keyboard(*call.emulator))
            {
                keyboard->AbortSequence();
                keyboard->ReleaseAllKeys();
            }
            if (DebugJoystickManager* joystick = JoystickManager(*call.emulator))
                joystick->ReleaseAll();
            return ActionResult::Success(StateNode::Object(), "Released every key and button");
        },
        RouteRules("POST", "/keyboard/release_all"));
    // endregion </Input>

    // region <BASIC>
    const char* basic = "BASIC";
    {
        ActionRules rules = RouteRules("POST", "/basic/run");
        rules.unavailable = NeedsRunning;
        add(
            "basic.run", "Run a BASIC command", basic, ValueKind::Press,
            "command? (RUN), press_enter? (true), wait_report? (false), timeout_ms? (30000)",
            [](const ActionCall& call) {
                std::string error;
                const std::string command = TextParam(call.value, "command").value_or("RUN");
                CommandTyper::Options options;
                options.pressEnter = BoolParam(call.value, "press_enter", error).value_or(true);
                options.waitForReport = BoolParam(call.value, "wait_report", error).value_or(false);
                const int64_t timeout = IntParam(call.value, "timeout_ms", error).value_or(30000);
                if (!error.empty())
                    return BadRequest(error);
                // Typed through the keyboard matrix and proven on the ROM's control points, as POST /basic/run
                return FromTyper(
                    CommandTyper::TypeAndWait(*call.emulator, command, options,
                                              static_cast<uint32_t>(std::clamp<int64_t>(timeout, 100, 120000))),
                    command);
            },
            std::move(rules));
    }
    add(
        "basic.inject", "Write a BASIC program", basic, ValueKind::Text, "",
        [](const ActionCall& call) {
            if (call.value.text.empty())
                return BadRequest("text: the program listing (numbered lines)");
            const BasicEncoder::InjectionResult result =
                BasicEncoder::injectProgram(call.emulator.get(), call.value.text);
            if (!result.success)
                return Conflict(result.message);
            return ActionResult::Success(StateNode::Object(), result.message);
        },
        RouteRules("POST", "/basic/inject"));
    add(
        "basic.clear", "NEW (clear the program)", basic, ValueKind::Press, "",
        [](const ActionCall& call) {
            Memory* memory = call.emulator->GetMemory();
            BasicEncoder encoder;
            if (memory == nullptr || !encoder.loadProgram(memory, ""))
                return Conflict("Failed to clear the BASIC program");
            return ActionResult::Success(StateNode::Object(), "BASIC program cleared");
        },
        RouteRules("POST", "/basic/clear"));
    add(
        "basic.mode", "BASIC mode from the 128K menu", basic, ValueKind::Press, "mode (48k|128k)",
        [](const ActionCall& call) {
            const std::string mode = Lower(TextParam(call.value, "mode").value_or(""));
            if (mode != "48k" && mode != "128k")
                return BadRequest("mode: 48k or 128k");
            Memory* memory = call.emulator->GetMemory();
            if (memory == nullptr)
                return Conflict("Memory not available");
            const BasicEncoder::BasicState state = BasicEncoder::detectState(memory);
            const BasicEncoder::BasicState wanted =
                mode == "48k" ? BasicEncoder::BasicState::Basic48K : BasicEncoder::BasicState::Basic128K;
            StateNode body = StateNode::Object();
            body["mode"] = mode;
            if (state == wanted)
                return ActionResult::Success(std::move(body), "Already in " + mode + " BASIC");
            if (state != BasicEncoder::BasicState::Menu128K)
                return ActionResult::Failure("Conflict", "Not at the 128K menu: cannot switch to " + mode,
                                             std::move(body));
            if (mode == "48k")
                BasicEncoder::navigateToBasic48K(call.emulator.get());
            else
                BasicEncoder::navigateToBasic128K(call.emulator.get());
            return ActionResult::Success(std::move(body), "Switched to " + mode + " BASIC");
        },
        RouteRules("POST", "/basic/mode"));
    // endregion </BASIC>

    // region <Features and settings>
    const char* settings = "Settings";
    add(
        "feature.toggle", "Toggle a feature", settings, ValueKind::Press, "name (GET /features), on? (else flips)",
        [](const ActionCall& call) {
            const std::string name = TextParam(call.value, "name").value_or("");
            FeatureManager* features = call.emulator->GetFeatureManager();
            if (features == nullptr)
                return Conflict("Feature manager not available");
            if (name.empty() || !features->hasFeature(name))
            {
                StateNode body = StateNode::Object();
                StateNode available = StateNode::Array();
                for (const FeatureManager::FeatureInfo& feature : features->listFeatures())
                    available.items.emplace_back(feature.id);
                body["available_features"] = std::move(available);
                return ActionResult::Failure("Not Found", name.empty() ? "name: a feature" : "Unknown feature: " + name,
                                             std::move(body));
            }
            std::string error;
            const bool on = BoolParam(call.value, "on", error).value_or(!features->isEnabled(name));
            if (!error.empty())
                return BadRequest(error);
            if (!features->setFeature(name, on))
                return Conflict(features->refusalReason(name, on));
            StateNode body = StateNode::Object();
            body["name"] = name;
            body["enabled"] = features->isEnabled(name);
            return ActionResult::Success(std::move(body), "Feature '" + name + "' " + (on ? "enabled" : "disabled"));
        },
        NamedRouteRules("PUT", "/feature/", "name", ""));
    // A history the user recorded with TTD keeps its 1x: the speed is refused (with the reason), not the history
    ActionRules speedRules = RouteRules("PUT", "/settings/speed");
    speedRules.unavailable = [](Emulator& emulator, const ActionValue&) { return emulator.SpeedLockReason(); };
    speedRules.unavailableError = "Conflict";
    add(
        "set.speed", "Speed", settings, ValueKind::Press, "value? (1, 2, 4, 8, 16; else the next one)",
        [](const ActionCall& call) {
            std::string error;
            std::optional<int64_t> value = IntParam(call.value, "value", error);
            if (!error.empty())
                return BadRequest(error);
            if (!value)
            {
                const int current = call.emulator->GetSpeedMultiplier();
                value = current >= 16 ? 1 : std::max(1, current) * 2;  // 1 -> 2 -> 4 -> 8 -> 16 -> 1
            }
            return SetSetting(*call.emulator, "speed", StateNode(*value));
        },
        std::move(speedRules));
    add(
        "set.toggle", "Toggle a setting", settings, ValueKind::Press,
        "name (turbo_mode, fast_tape, turbo_tape, fast_disk, turbo_audio, trdos_traps, ay_punch, beeper_punch, "
        "covox_dc_removal, fir_filter), on?",
        [](const ActionCall& call) {
            const std::string name = TextParam(call.value, "name").value_or("");
            const std::optional<bool> current = SettingIsOn(*call.emulator, name);
            if (!current)
                return ActionResult::Failure("Not Found",
                                             name.empty() ? "name: a bool setting" : "Not a bool setting: " + name);
            std::string error;
            const bool on = BoolParam(call.value, "on", error).value_or(!*current);
            if (!error.empty())
                return BadRequest(error);
            return SetSetting(*call.emulator, name, StateNode(on));
        },
        NamedRouteRules("PUT", "/settings/", "name", ""));
    add(
        "set.value", "Set a setting", settings, ValueKind::Press,
        "name, value (speed, audio_rate, the bool ones, ay_voicing, ay_room, ay_punch, beeper_punch, ay_stereo, ay_chip, "
        "fm_trim_db, neogs_stereo, covox_dc_removal, fir_filter)",
        [](const ActionCall& call) {
            const std::string name = TextParam(call.value, "name").value_or("");
            const StateNode* value = call.value.params.find("value");
            if (name.empty() || value == nullptr)
                return BadRequest("name and value");
            StateNode typed = *value;
            const SoundCharacterSettings::Descriptor* soundText = SoundCharacterSettings::Find(name);
            if (typed.kind == StateNode::Kind::String && name != "audio_rate" && !(soundText && !soundText->isBool))
            {
                // The CLI and the scripts give text: a bool or a number as the route's JSON would
                std::string error;
                if (name == "speed")
                {
                    if (const std::optional<int64_t> number = IntParam(call.value, "value", error))
                        typed = StateNode(*number);
                }
                else if (const std::optional<bool> flag = BoolParam(call.value, "value", error))
                    typed = StateNode(*flag);
            }
            return SetSetting(*call.emulator, name, typed);
        },
        NamedRouteRules("PUT", "/settings/", "name", ""));
    // endregion </Features and settings>

    // region <Audio>
    const char* audio = "Audio";
    add(
        "audio.mute_toggle", "Mute", audio, ValueKind::Press, "on? (else flips)",
        [](const ActionCall& call) {
            EmulatorContext* context = call.emulator->GetContext();
            if (context->pSoundManager == nullptr)
                return Conflict("Sound manager not available");
            std::string error;
            const bool on = BoolParam(call.value, "on", error).value_or(!context->pSoundManager->isMuted());
            if (!error.empty())
                return BadRequest(error);
            AudioMixer::Change change;
            change.muted = on ? 1 : 0;
            if (!AudioMixer::Apply(context, "master", change, error))
                return Conflict(error);
            StateNode body = StateNode::Object();
            body["muted"] = on;
            return ActionResult::Success(std::move(body), on ? "Muted" : "Unmuted");
        },
        RouteRules("PUT", "/audio/mixer/master"));
    // A source or one of its channels: `source` (beeper, ay1, ... or a channel key ay1.a), or `source` + `channel`
    auto mixerKey = [](const ActionCall& call) {
        std::string source = Lower(TextParam(call.value, "source").value_or(""));
        const std::string channel = Lower(TextParam(call.value, "channel").value_or(""));
        if (!channel.empty() && source.find('.') == std::string::npos)
            source += "." + channel;
        return source;
    };
    // Mute or solo one source / channel (toggle without `on`)
    auto mixerFlag = [mixerKey](const ActionCall& call, const char* what) {
        EmulatorContext* context = call.emulator->GetContext();
        if (context->pSoundManager == nullptr)
            return Conflict("Sound manager not available");
        const std::string source = mixerKey(call);
        if (source.empty() || source == "master")
            return BadRequest("source: beeper, ay1, ay2, fm1, fm2, covox, gs, ... or a channel (ay1.a, gs.3); "
                              "master: audio.mute_toggle");
        const std::optional<bool> current = AudioMixer::Flag(context, source, what);
        if (!current)
        {
            std::string error;
            StateNode ignored;
            AudioMixer::Read(context, source, ignored, error);
            return ActionResult::Failure("Not Found", error);
        }
        std::string error;
        const bool on = BoolParam(call.value, "on", error).value_or(!*current);
        if (!error.empty())
            return BadRequest(error);
        AudioMixer::Change change;
        (std::string(what) == "solo" ? change.solo : change.muted) = on ? 1 : 0;
        if (!AudioMixer::Apply(context, source, change, error))
            return Conflict(error);
        StateNode body;
        AudioMixer::Read(context, source, body, error);
        return ActionResult::Success(std::move(body), source + (std::string(what) == "solo" ? (on ? " solo" : " solo off")
                                                                                         : (on ? " muted" : " unmuted")));
    };
    add(
        "audio.source_mute", "Mute a source", audio, ValueKind::Press,
        "source (beeper, ay1, ay2, ay3, fm1, fm2, covox, gs, gs_mp3, moonsound_fm, moonsound_pcm, cd0..cd3, ms_*, "
        "next_ay1..3, next_dac; a channel: ay1.a, covox.lb, gs.3, ...), channel?, on? (else flips)",
        [mixerFlag](const ActionCall& call) { return mixerFlag(call, "muted"); }, RouteRules("PUT", "/audio/mixer/beeper"));
    add(
        "audio.source_solo", "Solo a source", audio, ValueKind::Press,
        "source (a row: only soloed rows are heard; a channel ay1.a: only the soloed channels of its row), channel?, "
        "on? (else flips)",
        [mixerFlag](const ActionCall& call) { return mixerFlag(call, "solo"); }, RouteRules("PUT", "/audio/mixer/beeper"));
    add(
        "audio.source_volume", "Set a volume", audio, ValueKind::Press,
        "source (a row or a channel), channel?, volume (0..1) or gain_db (-120..0)",
        [mixerKey](const ActionCall& call) {
            EmulatorContext* context = call.emulator->GetContext();
            if (context->pSoundManager == nullptr)
                return Conflict("Sound manager not available");
            const std::string source = mixerKey(call);
            if (source.empty() || source == "master")
                return BadRequest("source: a row (beeper, ay1, ...) or a channel (ay1.a); master has no volume");
            std::string error;
            const std::optional<double> volume = NumberParam(call.value, "volume", error);
            const std::optional<double> gainDb = NumberParam(call.value, "gain_db", error);
            if (!error.empty())
                return BadRequest(error);
            if (!volume && !gainDb)
                return BadRequest("volume (0..1) or gain_db (-120..0)");
            AudioMixer::Change change;
            if (!AudioMixer::ChangeFromStrings("", "", volume ? std::to_string(*volume) : std::string(),
                                               gainDb ? std::to_string(*gainDb) : std::string(), change, error) ||
                !AudioMixer::Apply(context, source, change, error))
                return BadRequest(error);
            StateNode body;
            AudioMixer::Read(context, source, body, error);
            return ActionResult::Success(std::move(body), source + " volume set");
        },
        RouteRules("PUT", "/audio/mixer/beeper"));
    add(
        "audio.volume_by", "Volume", audio, ValueKind::Delta,
        "source? (all rows; a row, or a channel ay1.a), channel?, step? (0.05 per detent)",
        [mixerKey](const ActionCall& call) {
            EmulatorContext* context = call.emulator->GetContext();
            SoundManager* sound = context->pSoundManager;
            if (sound == nullptr)
                return Conflict("Sound manager not available");
            std::string error;
            const double step = NumberParam(call.value, "step", error).value_or(0.05);
            if (!error.empty())
                return BadRequest(error);
            std::string source = mixerKey(call);
            if (source.empty())
                source = "all";
            StateNode volumes = StateNode::Object();
            if (source.find('.') != std::string::npos)
            {
                // One channel
                StateNode node;
                if (!AudioMixer::Read(context, source, node, error))
                    return ActionResult::Failure("Not Found", error);
                const StateNode* current = node.find("volume");
                if (current == nullptr)
                    return BadRequest(source + " has no volume (muted and solo only)");
                AudioMixer::Change change;
                change.volume = std::clamp(current->d + call.value.delta * step, 0.0, 1.0);
                if (!AudioMixer::Apply(context, source, change, error))
                    return Conflict(error);
                volumes[source] = std::round(change.volume * 1000.0) / 1000.0;
                StateNode body = StateNode::Object();
                body["volumes"] = std::move(volumes);
                return ActionResult::Success(std::move(body));
            }
            std::vector<AudioSourceType> types;
            if (source == "all")
            {
                for (const AudioDeviceInfo& device : sound->devices())
                    if (device.type != AudioSourceType::MasterMix)
                        types.push_back(device.type);
            }
            else
            {
                AudioSourceType type{};
                if (source == "master" || !AudioMixer::FromKey(source, type))
                    return BadRequest("source: all, or one device (beeper, ay1, ...) or channel (ay1.a); master takes "
                                      "muted only");
                if (sound->device(type) == nullptr)
                    return ActionResult::Failure("Not Found", source + " is not fitted on this machine");
                types.push_back(type);
            }
            for (AudioSourceType type : types)
            {
                const AudioDeviceInfo* device = sound->device(type);
                if (device == nullptr)
                    continue;
                const double volume =
                    std::clamp(static_cast<double>(device->volume) + call.value.delta * step, 0.0, 1.0);
                sound->setDeviceVolume(type, static_cast<float>(volume));
                volumes[AudioMixer::Key(type)] = std::round(volume * 1000.0) / 1000.0;
            }
            StateNode body = StateNode::Object();
            body["volumes"] = std::move(volumes);
            return ActionResult::Success(std::move(body));
        },
        RouteRules("PUT", "/audio/mixer/beeper"));
    // endregion </Audio>

    // region <Media and snapshots>
    const char* media = "Media";
    add(
        "media.open", "Open software", media, ValueKind::Press,
        "path, kind? (auto|snapshot|tape|disk), drive? (A), autostart? (true), switch_model?",
        OpenMedia,
        RouteRules("POST", "/tape/load"));
    {
        ActionRules rules;
        rules.scope = [](Emulator&, const ActionValue& value) {
            const std::string op = TextParam(value, "op").value_or("play");
            return RouteScope("POST", "/tape/" + (op == "toggle" ? std::string("play") : op));
        };
        add(
            "media.tape", "Tape", media, ValueKind::Press, "op (play|pause|toggle|stop|rewind|eject|seek), block?",
            [](const ActionCall& call) {
                const std::string op = Lower(TextParam(call.value, "op").value_or(""));
                EmulatorContext* context = call.emulator->GetContext();
                if (context->pTape == nullptr)
                    return Conflict("Tape subsystem not available");
                StateNode body = StateNode::Object();
                body["op"] = op;
                if (op == "eject")
                {
                    std::string reason;
                    if (!call.emulator->EjectTape(&reason))
                        return Conflict(reason);
                    return ActionResult::Success(std::move(body), "Tape ejected");
                }
                if (op != "play" && op != "pause" && op != "toggle" && op != "stop" && op != "rewind" && op != "seek")
                    return BadRequest("op: play, pause, toggle, stop, rewind, eject or seek");
                std::string error;
                const std::optional<int64_t> block = IntParam(call.value, "block", error);
                if (!error.empty() || (op == "seek" && (!block || *block < 0)))
                    return BadRequest(error.empty() ? std::string("block: the block index (0..)") : error);
                // The CPU held for the operation, as the routes: the pause is left as it was
                Emulator::CpuHold hold(*call.emulator, "action media");
                Tape& tape = *context->pTape;
                const TapePlaybackState state = tape.GetPlaybackState();
                std::string message;
                if (op == "toggle" && state == TapePlaybackState::Playing)
                {
                    tape.pausePlayback();
                    message = "Tape paused";
                }
                else if (op == "play" || op == "toggle")
                {
                    if (!tape.EnsureImageLoaded())
                        return Conflict("Tape image has no loadable blocks");
                    if (state == TapePlaybackState::Paused)
                        tape.ResumePlaybackFromPause();
                    else
                        tape.StartPlaybackAtCursor();
                    message = state == TapePlaybackState::Paused ? "Tape playback resumed" : "Tape playback started";
                }
                else if (op == "pause")
                {
                    if (state != TapePlaybackState::Playing)
                        return Conflict("The tape is not playing");
                    tape.pausePlayback();
                    message = "Tape paused";
                }
                else if (op == "stop")
                {
                    tape.stopTape();
                    message = "Tape stopped";
                }
                else if (op == "rewind")
                {
                    tape.EnsureImageLoaded();
                    tape.RewindToStart();
                    message = "Tape rewound";
                }
                else
                {
                    if (!tape.EnsureImageLoaded() || !tape.SeekToBlock(static_cast<size_t>(*block)))
                        return BadRequest("No block " + std::to_string(*block) + " on the tape");
                    body["block"] = *block;
                    message = "Seeked to block " + std::to_string(*block);
                }
                return ActionResult::Success(std::move(body), std::move(message));
            },
            std::move(rules));
    }
    add(
        "media.eject", "Eject", media, ValueKind::Press, "drive (A..D, tape, a slot id), save? / discard? / export?",
        [](const ActionCall& call) {
            if (Lower(TextParam(call.value, "drive").value_or("")) == "tape")
            {
                std::string reason;
                if (!call.emulator->EjectTape(&reason))
                    return Conflict(reason);
                return ActionResult::Success(StateNode::Object(), "Tape ejected");
            }
            return MediaVerb(call, "eject");
        },
        RouteRules("POST", "/media/A/eject"));
    add(
        "media.protect", "Write protect", media, ValueKind::Press, "drive (A..D, a slot id), on? (else flips)",
        [](const ActionCall& call) {
            std::string error;
            std::optional<bool> on = BoolParam(call.value, "on", error);
            if (!error.empty())
                return BadRequest(error);
            const std::string drive = TextParam(call.value, "drive").value_or("");
            EmulatorContext* context = call.emulator->GetContext();
            if (!on && !drive.empty() && context->pMediaManager != nullptr)
            {
                std::string slot;
                if (MediaControl::ResolveSelector(*context->pMediaManager, drive, slot).Ok())
                    if (const std::optional<SlotInfo> info = context->pMediaManager->Info(slot))
                        on = !info->writeProtect;
            }
            ActionValue value = call.value;
            if (on)
                value.params["on"] = *on;
            const ActionCall flipped{call.info, call.emulator, call.instanceId, value, call.origin, call.requester};
            return MediaVerb(flipped, "protect");
        },
        RouteRules("POST", "/media/A/protect"));
    add(
        "media.save", "Save the medium", media, ValueKind::Press, "drive (A..D, a slot id), path?",
        [](const ActionCall& call) { return MediaVerb(call, "save"); }, RouteRules("POST", "/media/A/save"));
    add(
        "media.create", "New blank disk", media, ValueKind::Press,
        "drive (A..D, or an IDE / SD slot), format? (floppy: auto|trdos|plus3|unformatted (raw)), cylinders? (40|80), "
        "sides? (1|2), size? (IDE / SD: bytes, or 64M, 2G), save? / discard? / export? (the disk it replaces)",
        CreateMedium, RouteRules("POST", "/media/A/create"));
    add(
        "snapshot.quick", "Quick snapshot", media, ValueKind::Press, "op (save|load), slot? (1..9, 1), dir?",
        [](const ActionCall& call) {
            std::string error;
            const std::string op = Lower(TextParam(call.value, "op").value_or(""));
            const int64_t slot = IntParam(call.value, "slot", error).value_or(1);
            if (!error.empty())
                return BadRequest(error);
            if (op != "save" && op != "load")
                return BadRequest("op: save or load");
            if (slot < 1 || slot > 9)
                return BadRequest("slot: 1..9");
            const std::string path = QuickSnapshotPath(call.value, slot);
            if (path.empty())
                return Conflict("no user data folder for quick snapshots: give dir");
            if (op == "save")
            {
                const std::string folder = path.substr(0, path.find_last_of("/\\"));
                if (!FileHelper::FolderExists(folder) && !FileHelper::CreateFolders(folder))
                    return Conflict("cannot create " + folder);
                return SaveSnapshotTo(*call.emulator, path);
            }
            if (!FileHelper::FileExists(path))
                return Conflict("quick snapshot " + std::to_string(slot) + " is empty (save it first)");
            // The quick snapshot follows its model: a slot saved on another machine switches to it
            return LoadSnapshot(call.instanceId, path, true);
        },
        [] {
            ActionRules rules;
            rules.scope = [](Emulator&, const ActionValue& value) {
                return Lower(TextParam(value, "op").value_or("")) == "save" ? RouteScope("POST", "/snapshot/save")
                                                                            : RouteScope("POST", "/snapshot/load");
            };
            return rules;
        }());
    add(
        "snapshot.save", "Save a snapshot", media, ValueKind::Press, "path (.sna, .z80, .szx), force? (false)",
        [](const ActionCall& call) {
            const std::string path = TextParam(call.value, "path").value_or("");
            std::string error;
            const bool force = BoolParam(call.value, "force", error).value_or(false);
            if (!error.empty())
                return BadRequest(error);
            if (path.empty())
                return BadRequest("path: the file to write (.sna, .z80, .szx)");
            if (!force && FileHelper::FileExists(path))
                return Conflict("File already exists. Use force: true to overwrite.");
            return SaveSnapshotTo(*call.emulator, path);
        },
        RouteRules("POST", "/snapshot/save"));
    // endregion </Media and snapshots>

    // region <Video>
    const char* video = "Video";
    add(
        "video.overscan_toggle", "Overscan", video, ValueKind::Press, "on? (else flips)",
        [](const ActionCall& call) {
            std::string error;
            const bool on = BoolParam(call.value, "on", error).value_or(!call.emulator->IsOverscanMode());
            if (!error.empty())
                return BadRequest(error);
            // With the viewport the GUI picks (symmetric on, full off), as the REST route and View > Overscan
            const VideoViewport::Result set = VideoViewport::SetOverscan(*call.emulator, on);
            if (!set.Ok())
                return Conflict("this model has no overscan mode");
            StateNode body = VideoViewport::Report(*call.emulator);
            return ActionResult::Success(std::move(body), on ? "Overscan on" : "Overscan off");
        },
        RouteRules("PUT", "/state/screen/overscan"));
    add(
        "video.temporal", "De-flicker (ZX DLSS)", video, ValueKind::Press,
        "algorithm? (a name, or off; else flips between off and mod-tpgwafsd)",
        [](const ActionCall& call) {
            Screen* screen = call.emulator->GetContext()->pScreen;
            if (screen == nullptr)
                return Conflict("Screen not available");
            std::string algorithm = TextParam(call.value, "algorithm").value_or("");
            if (algorithm.empty())
                algorithm = screen->GetTemporalAlgorithm().empty() ? "mod-tpgwafsd" : "off";
            if (!screen->SetTemporalAlgorithm(algorithm == "off" ? std::string() : algorithm))
                return BadRequest("Unknown temporal algorithm '" + algorithm + "'");
            StateNode body = StateNode::Object();
            const std::string now = screen->GetTemporalAlgorithm();
            body["algorithm"] = now.empty() ? std::string("off") : now;
            return ActionResult::Success(std::move(body), "De-flicker " + (now.empty() ? std::string("off") : now));
        },
        RouteRules("PUT", "/video/temporal"));
    // endregion </Video>

    // region <Debugger>
    const char* debugger = "Debugger";
    add(
        "dbg.mode_toggle", "Debug mode", debugger, ValueKind::Press, "on? (else flips)",
        [](const ActionCall& call) {
            FeatureManager* features = call.emulator->GetFeatureManager();
            if (features == nullptr)
                return Conflict("Feature manager not available");
            std::string error;
            const bool on = BoolParam(call.value, "on", error).value_or(!features->isEnabled(Features::kDebugMode));
            if (!error.empty())
                return BadRequest(error);
            if (!features->setFeature(Features::kDebugMode, on))
                return Conflict(features->refusalReason(Features::kDebugMode, on));
            StateNode body = StateNode::Object();
            body["enabled"] = features->isEnabled(Features::kDebugMode);
            return ActionResult::Success(std::move(body), on ? "Debug mode on" : "Debug mode off");
        },
        RouteRules("PUT", "/debugmode"));
    add(
        "dbg.breakpoints_clear", "Clear breakpoints", debugger, ValueKind::Press, "",
        [](const ActionCall& call) {
            EmulatorContext* context = call.emulator->GetContext();
            BreakpointManager* breakpoints =
                context->pDebugManager != nullptr ? context->pDebugManager->GetBreakpointsManager() : nullptr;
            if (breakpoints == nullptr)
                return Conflict("Debug manager not available");
            // The user's breakpoints: the BASIC typer's control points and the debugger adapters' stay with their
            // owners
            breakpoints->ClearUserBreakpoints();
            return ActionResult::Success(StateNode::Object(), "All breakpoints cleared");
        },
        RouteRules("DELETE", "/breakpoints"));
    add(
        "dbg.breakpoint_hits_reset", "Reset hit counts", debugger, ValueKind::Press, "id? (else all)",
        [](const ActionCall& call) {
            BreakpointManager* breakpoints = call.emulator->GetBreakpointManager();
            if (breakpoints == nullptr)
                return Conflict("Breakpoint manager not available");
            std::string error;
            const std::optional<int64_t> id = IntParam(call.value, "id", error);
            if (!error.empty())
                return BadRequest(error);
            StateNode body = StateNode::Object();
            if (id)
            {
                if (*id < 0 || *id > 0xFFFF || !breakpoints->ResetHitCount(static_cast<uint16_t>(*id)))
                    return ActionResult::Failure("Not Found", "No breakpoint " + std::to_string(*id));
                body["reset"] = *id;
            }
            else
            {
                breakpoints->ResetAllHitCounts();
                body["reset"] = "all";
            }
            return ActionResult::Success(std::move(body), "Hit counts reset");
        },
        RouteRules("POST", "/breakpoints/hits/reset"));
    // endregion </Debugger>

    // region <Time travel: the TTDControl verbs with the params as options>
    const char* ttdGroup = "Time travel";
    auto verb = [](const char* name) {
        return [name](const ActionCall& call) {
            return FromTtd(
                ttd::TTDControl(call.emulator->GetContext()).Execute({name, ParamOptions(call.value.params)}));
        };
    };
    auto ttdRules = [](const char* method, const char* subpath, bool history = false) {
        ActionRules rules = RouteRules(method, subpath);
        rules.unavailable = history ? NoHistory : NoTtd;
        return rules;
    };
    add("ttd.start", "Start recording", ttdGroup, ValueKind::Press, "", verb("start"), ttdRules("POST", "/ttd/start"));
    add("ttd.stop", "Stop recording", ttdGroup, ValueKind::Press, "", verb("stop"), ttdRules("POST", "/ttd/stop"));
    add("ttd.seek", "Go to a frame or bookmark", ttdGroup, ValueKind::Press, "frame + tinframe?, or bookmark",
        verb("seek"), ttdRules("POST", "/ttd/seek", true));
    add("ttd.bookmark_delete", "Delete a bookmark", ttdGroup, ValueKind::Press, "label", verb("bookmark-delete"),
        ttdRules("DELETE", "/ttd/bookmarks/A", true));
    add("ttd.invalidate", "Drop the history", ttdGroup, ValueKind::Press, "", verb("invalidate"),
        ttdRules("POST", "/ttd/invalidate", true));
    add("ttd.history_limit", "History limit", ttdGroup, ValueKind::Press, "frames?, bytes?", verb("history-limit"),
        ttdRules("POST", "/ttd/history-limit"));
    add("ttd.journal", "Input journal", ttdGroup, ValueKind::Press, "enabled", verb("journal"),
        ttdRules("POST", "/ttd/journal"));
    add("ttd.dump", "Save the session", ttdGroup, ValueKind::Press, "path", verb("dump"),
        ttdRules("POST", "/ttd/dump", true));
    add("ttd.load", "Open a session", ttdGroup, ValueKind::Press, "path", verb("load"), ttdRules("POST", "/ttd/load"));
    // endregion </Time travel>

    // The machine area's actions (switches, previous model, media verbs, recent files, RZX, joystick types, mouse,
    // recording, clips): hostactions_machine.cpp
    for (RegistrationHandle& handle : RegisterMachineAreaActions(registry))
        handles.push_back(std::move(handle));
    return handles;
}

ActionResult HostStartMachine(const ActionCall& call)
{
    return StartMachine(call);
}

ActionResult HostOpenMedia(const ActionCall& call)
{
    return OpenMedia(call);
}

std::optional<bool> HostSettingIsOn(Emulator& emulator, const std::string& name)
{
    if (emulator.GetContext() == nullptr)
        return std::nullopt;
    return SettingIsOn(emulator, name);
}

}  // namespace actions
