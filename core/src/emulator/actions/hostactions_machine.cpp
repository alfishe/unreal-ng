/// @file hostactions_machine.cpp
/// @brief The catalogue actions of the machine area (Stream Deck use cases C1.5, C3.3, C4.3, C4.5, C5, C6.4, C6.5, C7,
/// U1.3, U1.5): front-panel switches, the previous model, the media verbs a key needs (insert, swap, export, discard,
/// rescan), recent files, RZX playback, the joystick on a connector, the Kempston mouse buttons, video recording with
/// the core's presets, clips of the TTD history and the audio capture of one source. Each calls the core object its
/// REST route calls (Emulator, MediaControl, RecentFiles, RzxLauncher, ControllerControl, DebugMouseManager,
/// capture::RecordingControl, the audio capture analyzer) and takes that route's companion scope.

#include "emulator/actions/hostactions.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "3rdparty/tinywav/tinywav.h"
#include "common/filehelper.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/audiocapture/audiocaptureanalyzer.h"
#include "debugger/debugmanager.h"
#include "debugger/ttd/timetravelhooks.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/mouse/debugmousemanager.h"
#include "emulator/actions/actionparams.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/capture/recordingcontrol.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/automationgate.h"
#include "emulator/io/controllers/controllercontrol.h"
#include "emulator/machinenames.h"
#include "emulator/media/mediacontrol.h"
#include "emulator/media/recentfiles.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/rzx/rzxlauncher.h"
#include "emulator/sound/audiomixer.h"
#include "emulator/sound/soundmanager.h"

namespace actions
{
namespace
{
/// region <Helpers>

uint32_t RouteScope(const char* method, const std::string& subpath)
{
    return automation_gate::ChangesMachine(method, subpath) ? automation_gate::CompanionScopeOf(method, subpath) : 0;
}

ActionRules RouteRules(const char* method, const char* subpath)
{
    ActionRules rules;
    rules.scope = ActionRules::Fixed(RouteScope(method, subpath));
    return rules;
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

ActionResult FromMedia(const MediaReply& reply)
{
    StateNode body = reply.ToValue();
    if (reply.result.Ok())
        return ActionResult::Success(std::move(body),
                                     reply.result.report.empty() ? std::string() : reply.result.report[0]);
    return ActionResult::Failure(ErrorForStatus(reply.HttpStatus()), reply.result.message, std::move(body));
}

MediaReply RunMedia(Emulator& emulator, const std::string& verb, const std::string& selector,
                    const std::string& path = {}, std::map<std::string, std::string> options = {})
{
    MediaRequest request;
    request.verb = verb;
    request.selector = selector;
    request.path = path;
    request.options = std::move(options);
    return MediaControl(emulator.GetContext()).Execute(request);
}

/// A MediaControl verb on params.drive, the other params as its options (`path` as the request's path)
ActionResult MediaVerb(const ActionCall& call, const char* verb)
{
    const std::optional<std::string> drive = TextParam(call.value, "drive");
    if (!drive || drive->empty())
        return BadRequest("drive: the slot (A..D, tape, ide0.master, sd..., or a slot id; GET /media lists them)");
    return FromMedia(RunMedia(*call.emulator, verb, *drive, TextParam(call.value, "path").value_or(""),
                              ParamOptions(call.value.params, {"drive", "path"})));
}

/// The slot's info through a selector (A..D, tape, an id or alias); nullopt with `error` otherwise
std::optional<SlotInfo> SlotOf(Emulator& emulator, const std::string& selector, std::string& error,
                               bool allowDetached = false)
{
    EmulatorContext* context = emulator.GetContext();
    if (context == nullptr || context->pMediaManager == nullptr)
    {
        error = "this machine has no media slots";
        return std::nullopt;
    }
    std::string slot;
    const MediaResult resolved = MediaControl::ResolveSelector(*context->pMediaManager, selector, slot, allowDetached);
    if (!resolved.Ok())
    {
        error = resolved.message;
        return std::nullopt;
    }
    std::optional<SlotInfo> info = context->pMediaManager->Info(slot);
    if (!info)
        error = "no slot '" + selector + "'";
    return info;
}

/// "<folder>/<stem>-<seconds since 1970>.<ext>", the folder created
std::string StampedFile(const std::string& folder, const std::string& stem, const std::string& extension)
{
    std::error_code ec;
    std::filesystem::create_directories(FileHelper::ToFsPath(folder), ec);
    return FileHelper::PathCombine(folder, stem + "-" + std::to_string(static_cast<long long>(std::time(nullptr))) +
                                               "." + extension);
}

std::string ReplaceExtension(const std::string& path, const std::string& extension)
{
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    const std::string stem = dot != std::string::npos && (slash == std::string::npos || dot > slash) ? path.substr(0, dot)
                                                                                                    : path;
    return stem + "." + extension;
}

/// endregion </Helpers>

/// region <Machine>

ActionResult FrontPanel(const ActionCall& call)
{
    const std::string name = Lower(TextParam(call.value, "name").value_or("turbo"));
    FrontPanelSwitch sw{};
    if (!ParseFrontPanelSwitch(name, sw))
        return BadRequest("name: turbo or cpm (the Profi's front panel)");
    const int now = call.emulator->GetFrontPanelSwitch(sw);
    if (now < 0)
        return ActionResult::Failure("Not Found", "This machine has no " + name + " switch");
    std::string error;
    const bool on = BoolParam(call.value, "on", error).value_or(now == 0);
    if (!error.empty())
        return BadRequest(error);
    if (!call.emulator->SetFrontPanelSwitch(sw, on))
        return Conflict("the " + name + " switch did not move (the input was refused: a TTD replay?)");
    // A running machine takes the input at its next frame boundary (the TTD live-input gateway): wait for it, a
    // few frames at most, so the answer and the key's state show the new position
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while ((call.emulator->GetFrontPanelSwitch(sw) == 1) != on && call.emulator->IsRunning() &&
           !call.emulator->IsPaused() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    StateNode body = StateNode::Object();
    body["name"] = name;
    body["on"] = call.emulator->GetFrontPanelSwitch(sw) == 1;
    body["pending"] = (call.emulator->GetFrontPanelSwitch(sw) == 1) != on;
    return ActionResult::Success(std::move(body), name + (on ? " on" : " off"));
}

std::optional<bool> FrontPanelState(Emulator& emulator, const ActionValue& value)
{
    FrontPanelSwitch sw{};
    if (!ParseFrontPanelSwitch(Lower(TextParam(value, "name").value_or("turbo")), sw))
        return std::nullopt;
    const int now = emulator.GetFrontPanelSwitch(sw);
    return now < 0 ? std::nullopt : std::optional<bool>(now == 1);
}

std::string CurrentMachineName(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    return context != nullptr ? machines::NameOf(context->config) : std::string();
}

ActionResult PreviousModel(const ActionCall& call)
{
    machines::MachineHistory::Entry previous;
    const std::string current = CurrentMachineName(*call.emulator);
    if (!machines::MachineHistory::Instance().Previous(current, previous))
        return Conflict("no previous model yet: this process ran " + (current.empty() ? std::string("one") : current) +
                        " only");
    ActionValue value = call.value;
    value.params["model"] = previous.name;
    if (previous.ramKb > 0 && value.params.find("ram_kb") == nullptr)
        value.params["ram_kb"] = static_cast<int64_t>(previous.ramKb);
    const ActionCall start{call.info, call.emulator, call.instanceId, value, call.origin, call.requester};
    ActionResult result = HostStartMachine(start);
    result.body["previous"] = previous.name;
    result.body["from"] = current;
    return result;
}

/// endregion </Machine>

/// region <Media>

/// Two drives exchange their disks (`with`), or a file replaces the disk (`path`: MediaControl's swap)
ActionResult SwapMedia(const ActionCall& call)
{
    const std::string drive = TextParam(call.value, "drive").value_or("");
    const std::string with = TextParam(call.value, "with").value_or("");
    const std::string path = TextParam(call.value, "path").value_or("");
    if (drive.empty())
        return BadRequest("drive: the slot (A..D, or a slot id)");
    if (!path.empty())
        return MediaVerb(call, "swap");
    if (with.empty())
        return BadRequest("with: the other drive (the two exchange their disks), or path: the file that replaces this "
                          "disk");
    std::string error;
    const bool save = BoolParam(call.value, "save", error).value_or(false);
    const bool discard = BoolParam(call.value, "discard", error).value_or(false);
    if (!error.empty())
        return BadRequest(error);
    if (save && discard)
        return BadRequest("say save or discard, not both");
    const std::optional<SlotInfo> a = SlotOf(*call.emulator, drive, error);
    const std::optional<SlotInfo> b = a ? SlotOf(*call.emulator, with, error) : std::nullopt;
    if (!a || !b)
        return BadRequest(error);
    if (a->descriptor.id == b->descriptor.id)
        return BadRequest("drive and with name the same slot");
    if (a->descriptor.kind != b->descriptor.kind)
        return BadRequest(a->descriptor.id + " and " + b->descriptor.id + " take different media");
    for (const SlotInfo* info : {&*a, &*b})
    {
        if (!info->present)
            continue;
        if (info->dirty && !save && !discard)
            return ActionResult::Failure("Conflict", info->descriptor.id + " has unsaved writes (" + info->changes +
                                                         "): say save or discard");
        // A blank disk's source is a description ("unformatted disk 80x2"), not a file
        if (info->pending || (!FileHelper::FileExists(info->source) && !FileHelper::FolderExists(info->source)))
            return Conflict(info->descriptor.id + " holds a medium without a file (a blank disk): save it to a file "
                                                  "first (media.save path=...)");
    }
    // Save first (the disks then come back from their files), then both out, then each into the other drive
    if (save)
        for (const SlotInfo* info : {&*a, &*b})
            if (info->present && info->dirty)
                if (MediaReply saved = RunMedia(*call.emulator, "save", info->descriptor.id); !saved.result.Ok())
                    return FromMedia(saved);
    std::map<std::string, std::string> out;
    if (discard)
        out["discard"] = "";
    for (const SlotInfo* info : {&*a, &*b})
        if (info->present)
            if (MediaReply ejected = RunMedia(*call.emulator, "eject", info->descriptor.id, {}, out);
                !ejected.result.Ok())
                return FromMedia(ejected);
    StateNode body = StateNode::Object();
    auto put = [&](const SlotInfo& from, const SlotInfo& into) -> std::optional<ActionResult> {
        if (!from.present)
            return std::nullopt;
        std::map<std::string, std::string> options;
        if (from.writeProtect)
            options["wp"] = "true";
        MediaReply inserted = RunMedia(*call.emulator, "insert", into.descriptor.id, from.source, options);
        if (!inserted.result.Ok())
            return FromMedia(inserted);
        body[into.descriptor.id] = from.source;
        return std::nullopt;
    };
    if (std::optional<ActionResult> failed = put(*a, *b))
        return *failed;
    if (std::optional<ActionResult> failed = put(*b, *a))
        return *failed;
    return ActionResult::Success(std::move(body), "Swapped " + a->descriptor.id + " and " + b->descriptor.id);
}

/// The medium written as another format: trd, scl, udi, fdi, dsk, td0, mgt, img, hfe, scp (a disk), tap, tzx (the
/// tape). Without a path: next to its file with the format's extension
ActionResult ExportMedia(const ActionCall& call)
{
    const std::string drive = TextParam(call.value, "drive").value_or("");
    if (drive.empty())
        return BadRequest("drive: the slot (A..D, tape, or a slot id)");
    std::string error;
    const bool force = BoolParam(call.value, "force", error).value_or(false);
    if (!error.empty())
        return BadRequest(error);
    const std::optional<SlotInfo> info = SlotOf(*call.emulator, drive, error, /*allowDetached*/ true);
    if (!info)
        return BadRequest(error);
    if (!info->present && !info->detached)
        return Conflict(info->descriptor.id + " is empty");
    std::string path = TextParam(call.value, "path").value_or("");
    std::string format = Lower(TextParam(call.value, "format").value_or(""));
    if (!format.empty() && format[0] == '.')
        format.erase(0, 1);
    static const std::vector<std::string> disks = {"trd", "scl", "udi", "fdi", "dsk", "td0",
                                                   "mgt", "img", "mb2", "hfe", "scp"};
    static const std::vector<std::string> tapes = {"tap", "tzx"};
    const bool tape = info->descriptor.kind == MediaKind::Tape;
    const bool floppy = info->descriptor.kind == MediaKind::Floppy;
    if (path.empty())
    {
        if (format.empty())
            return BadRequest(std::string("format: ") + (tape ? "tap or tzx" : floppy ? "trd, scl, udi, fdi, dsk, td0, "
                                                                                        "mgt, img, mb2, hfe or scp"
                                                                                      : "the image format") +
                              " (or path)");
        if (info->source.empty() || FileHelper::IsFolder(info->source))
            path = StampedFile(FileHelper::GetUserDataFolder("exports"), info->descriptor.id, format);
        else
            path = ReplaceExtension(info->source, format);
    }
    else if (!format.empty())
        path = ReplaceExtension(path, format);
    const std::string extension = Lower(FileHelper::GetFileExtension(path));
    const std::vector<std::string>& allowed = tape ? tapes : disks;
    if ((tape || floppy) && std::find(allowed.begin(), allowed.end(), extension) == allowed.end())
    {
        std::string list;
        for (const std::string& f : allowed)
            list += (list.empty() ? "" : ", ") + f;
        return BadRequest("format '" + extension + "': " + (tape ? "a tape" : "a disk") + " is written as " + list);
    }
    if (!force && FileHelper::FileExists(path))
        return ActionResult::Failure("Conflict", path + " exists: force: true to overwrite");
    MediaReply reply = RunMedia(*call.emulator, "export", info->descriptor.id, path);
    ActionResult result = FromMedia(reply);
    result.body["path"] = path;
    result.body["format"] = extension;
    if (result.ok)
        result.message = "Saved " + info->descriptor.id + " as " + path;
    return result;
}

/// endregion </Media>

/// region <Recent files>

ActionResult OpenRecent(const ActionCall& call)
{
    std::string error;
    const int64_t index = IntParam(call.value, "index", error).value_or(0);
    if (!error.empty())
        return BadRequest(error);
    const std::string kind = Lower(TextParam(call.value, "kind").value_or("all"));
    if (kind != "all" && !RecentFiles::IsKind(kind))
        return BadRequest("kind: all, snapshot, tape, disk, rzx, image or cd");
    const std::vector<RecentFiles::Entry> files = RecentFiles::Instance().List(kind);
    if (index < 0 || static_cast<size_t>(index) >= files.size())
        return ActionResult::Failure("Not Found", files.empty() ? "no recent " + (kind == "all" ? std::string("files")
                                                                                                 : kind + " files")
                                                                : "index " + std::to_string(index) + ": 0.." +
                                                                      std::to_string(files.size() - 1));
    const RecentFiles::Entry& entry = files[static_cast<size_t>(index)];
    if (!FileHelper::FileExists(entry.path) && !FileHelper::FolderExists(entry.path))
        return ActionResult::Failure("Not Found", entry.name + " is gone (" + entry.path + ")");
    StateNode opened = RecentFiles::EntryValue(entry, static_cast<size_t>(index));
    if (entry.kind == "rzx")
    {
        rzx::LaunchRequest request;
        request.emulatorId = call.instanceId;
        request.path = entry.path;
        const rzx::LaunchResult launched = rzx::RzxLauncher::Play(request);
        StateNode body = StateNode::Object();
        body["recent"] = opened;
        body["model_switched"] = launched.modelSwitched;
        if (launched.emulator)
            body["emulator_id"] = launched.emulator->GetId();
        if (!launched.play.Ok())
            return ActionResult::Failure("Conflict", launched.play.message, std::move(body));
        return ActionResult::Success(std::move(body), "RZX playing: " + entry.name);
    }
    if (entry.kind == "image" || entry.kind == "cd")
    {
        // Into the slot `insert auto` picks (the IDE disk, the SD card, the CD-ROM drive)
        MediaReply reply = RunMedia(*call.emulator, "insert", TextParam(call.value, "drive").value_or("auto"),
                                    entry.path);
        ActionResult result = FromMedia(reply);
        result.body["recent"] = opened;
        return result;
    }
    ActionValue value = call.value;
    value.params["path"] = entry.path;
    value.params["kind"] = entry.kind;
    const ActionCall open{call.info, call.emulator, call.instanceId, value, call.origin, call.requester};
    ActionResult result = HostOpenMedia(open);
    result.body["recent"] = opened;
    return result;
}

/// endregion </Recent files>

/// region <RZX>

StateNode RzxStatusValue(const rzx::SessionStatus& status)
{
    StateNode node = StateNode::Object();
    node["loaded"] = status.loaded;
    node["active"] = status.active;
    if (!status.loaded)
        return node;
    const rzx::PlayerStatus& player = status.player;
    const size_t slash = status.path.find_last_of("/\\");
    node["name"] = slash == std::string::npos ? status.path : status.path.substr(slash + 1);
    node["path"] = status.path;
    node["state"] = rzx::StateName(player.state);
    node["frame"] = static_cast<uint64_t>(player.frame);
    node["total_frames"] = static_cast<uint64_t>(player.totalFrames);
    node["progress"] = player.totalFrames ? static_cast<double>(player.frame) / static_cast<double>(player.totalFrames)
                                          : 0.0;
    node["desyncs"] = static_cast<uint64_t>(player.desyncs);
    return node;
}

ActionResult RzxPlay(const ActionCall& call)
{
    const std::string path = TextParam(call.value, "path").value_or("");
    if (path.empty())
        return BadRequest("path: the .rzx file");
    if (!FileHelper::FileExists(path))
        return ActionResult::Failure("Not Found", "No such file: " + path);
    std::string error;
    const bool tolerant = BoolParam(call.value, "tolerant", error).value_or(false);
    const bool switchModel = BoolParam(call.value, "switch_model", error).value_or(true);
    if (!error.empty())
        return BadRequest(error);
    rzx::LaunchRequest request;
    request.emulatorId = call.instanceId;
    request.path = path;
    request.switchModel = switchModel;
    request.options.desyncMode = tolerant ? rzx::DesyncMode::Tolerant : rzx::DesyncMode::Strict;
    const rzx::LaunchResult launched = rzx::RzxLauncher::Play(request);
    StateNode body = StateNode::Object();
    body["path"] = path;
    body["model_switched"] = launched.modelSwitched;
    if (launched.modelSwitched)
        body["model"] = launched.switchedToModel;
    if (launched.emulator)
        body["emulator_id"] = launched.emulator->GetId();
    if (!launched.play.Ok())
    {
        body["code"] = rzx::PlayErrorName(launched.play.error);
        const bool conflict = launched.play.error == rzx::PlayError::ModelMismatch ||
                              launched.play.error == rzx::PlayError::Refused;
        return ActionResult::Failure(conflict ? "Conflict" : "Bad Request", launched.play.message, std::move(body));
    }
    if (launched.emulator)
        body["rzx"] = RzxStatusValue(launched.emulator->GetRzxStatus());
    return ActionResult::Success(std::move(body), launched.modelSwitched ? "Model switched; RZX playing"
                                                                         : std::string("RZX playing"));
}

ActionResult RzxSeek(const ActionCall& call)
{
    std::string error;
    const std::optional<int64_t> frame = IntParam(call.value, "frame", error);
    const std::optional<int64_t> by = IntParam(call.value, "by", error);
    if (!error.empty())
        return BadRequest(error);
    if (frame.has_value() == by.has_value())
        return BadRequest("frame: the frame to go to (0 = the start), or by: frames forward / back (-500)");
    const rzx::SessionStatus status = call.emulator->GetRzxStatus();
    if (!status.loaded)
        return Conflict("no RZX recording played on this machine");
    const int64_t current = static_cast<int64_t>(status.player.frame);
    const int64_t total = static_cast<int64_t>(status.player.totalFrames);
    const int64_t target = std::clamp<int64_t>(frame ? *frame : current + *by, 0, std::max<int64_t>(0, total));
    if (!call.emulator->SeekRzx(static_cast<uint64_t>(target), &error))
        return Conflict(error);
    StateNode body = StateNode::Object();
    body["frame"] = target;
    body["rzx"] = RzxStatusValue(call.emulator->GetRzxStatus());
    return ActionResult::Success(std::move(body), "RZX at frame " + std::to_string(target));
}

/// endregion </RZX>

/// region <Input>

/// input.joystick_type: the retro device on a connector (ControllerControl plug / unplug)
ActionResult JoystickType(const ActionCall& call)
{
    EmulatorContext* context = call.emulator->GetContext();
    if (context == nullptr || context->pControllerManager == nullptr)
        return Conflict("this machine has no controller connectors");
    const std::string kind = Lower(TextParam(call.value, "kind").value_or(""));
    std::string connector = TextParam(call.value, "connector").value_or("auto");
    controllers::ControllerRequest request;
    request.selector = connector;
    if (kind == "none" || kind == "off" || kind == "unplug")
    {
        if (Lower(connector) == "auto")
            return BadRequest("kind none: name the connector to empty (GET /controllers/connectors lists them)");
        request.verb = "unplug";
    }
    else
    {
        request.verb = "plug";
        if (kind == "kempston")
            request.device = "kempston-joystick";
        else if (kind == "sinclair1" || kind == "sinclair" || kind == "sinclair2")
        {
            request.device = "sinclair-joystick";
            request.options["port"] = kind == "sinclair2" ? "2" : "1";
        }
        else if (kind == "cursor" || kind == "protek" || kind == "agf")
            request.device = "cursor-joystick";
        else if (kind == "fuller")
            request.device = "fuller-joystick";
        else if (kind == "keys")
            request.device = "keys-joystick";
        else if (kind == "md6" || kind == "md6-pad" || kind == "pad")
            request.device = "md6-pad";
        else if (kind.find('-') != std::string::npos)
            request.device = kind;  // a retro device's own name (sinclair-joystick, ...)
        else
            return BadRequest("kind: kempston, sinclair1, sinclair2, cursor, fuller, keys, md6 or none");
        for (const char* option : {"port", "shift", "keys"})
            if (const std::optional<std::string> value = TextParam(call.value, option))
                request.options[option] = *value;
    }
    const controllers::ControllerReply reply = controllers::ControllerControl(context).Execute(request);
    StateNode body = reply.ToValue();
    if (!reply.result.Ok())
        return ActionResult::Failure(ErrorForStatus(reply.HttpStatus()), reply.result.message, std::move(body));
    return ActionResult::Success(std::move(body), (request.verb == "plug" ? request.device + " on " : "Emptied ") +
                                                      reply.connector);
}

DebugMouseManager* Mouse(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    return context != nullptr && context->pDebugManager != nullptr ? context->pDebugManager->GetMouseManager()
                                                                   : nullptr;
}

ActionResult FromMouse(const MouseInjectResult& result, StateNode body = StateNode::Object())
{
    if (!result.warning.empty())
        body["warning"] = result.warning;
    if (result.ok())
        return ActionResult::Success(std::move(body), result.message);
    switch (result.status)
    {
        case MouseInjectStatus::InvalidArgument:
            return ActionResult::Failure("Bad Request", result.message, std::move(body));
        case MouseInjectStatus::ReplayActive:
            return ActionResult::Failure("not_now", result.message, std::move(body));
        default:
            return ActionResult::Failure("Conflict", result.message, std::move(body));
    }
}

ActionResult MouseButtonAction(const ActionCall& call)
{
    DebugMouseManager* mouse = Mouse(*call.emulator);
    if (mouse == nullptr)
        return Conflict("Mouse manager not available");
    const std::string name = TextParam(call.value, "button").value_or("left");
    const std::optional<MouseButton> button = DebugMouseManager::ResolveButtonName(name);
    if (!button)
        return BadRequest("button: left, right or middle");
    std::string error;
    const bool hold = BoolParam(call.value, "hold", error).value_or(false);
    const int64_t frames = IntParam(call.value, "frames", error).value_or(DebugMouseManager::DEFAULT_CLICK_FRAMES);
    if (!error.empty())
        return BadRequest(error);
    StateNode body = StateNode::Object();
    body["button"] = DebugMouseManager::GetButtonName(*button);
    if (!hold)
    {
        if (!call.value.pressed)
        {
            body["ignored"] = "release";  // a click is whole on the press
            return ActionResult::Success(std::move(body));
        }
        return FromMouse(mouse->Click(*button, static_cast<uint32_t>(std::clamp<int64_t>(frames, 1, 1000))),
                         std::move(body));
    }
    body["pressed"] = call.value.pressed;
    return FromMouse(call.value.pressed ? mouse->PressButton(*button) : mouse->ReleaseButton(*button),
                     std::move(body));
}

/// endregion </Input>

/// region <Recording>

/// A recording verb with the action's params as its options
ActionResult FromRecording(const capture::RecordingReply& reply)
{
    if (reply.ok)
        return ActionResult::Success(reply.body, reply.message);
    const std::string error = reply.error == "Bad Request" || reply.error == "Conflict" ||
                                      reply.error == "Not Found" || reply.error == "Not Implemented"
                                  ? reply.error
                                  : std::string("Conflict");
    StateNode body = reply.body;
    if (error != reply.error)
        body["code"] = reply.error;
    return ActionResult::Failure(error, reply.message, std::move(body));
}

ActionResult Recording(const ActionCall& call, const char* verb)
{
    std::map<std::string, std::string> options = ParamOptions(call.value.params);
    if (std::string(verb) != "toggle" && std::string(verb) != "start")
        options.clear();
    capture::RecordingControl control(*call.emulator, capture::RecordingControl::UserRecordingsFolder());
    return FromRecording(control.Execute(verb, options));
}

std::optional<bool> RecordingOn(Emulator& emulator, bool paused)
{
    const StateNode node = capture::RecordingReadout(emulator);
    const StateNode* available = node.find("available");
    if (available == nullptr || !available->b)
        return std::nullopt;
    const StateNode* value = node.find(paused ? "paused" : "recording");
    return value != nullptr && value->b;
}

ActionResult ClipLastAction(const ActionCall& call)
{
    std::string error;
    const double seconds = NumberParam(call.value, "seconds", error).value_or(10.0);
    const int64_t scale = IntParam(call.value, "scale", error).value_or(1);
    if (!error.empty())
        return BadRequest(error);
    return FromRecording(capture::ClipLast(*call.emulator, seconds, TextParam(call.value, "format").value_or("gif"),
                                           TextParam(call.value, "path").value_or(""),
                                           static_cast<uint32_t>(std::clamp<int64_t>(scale, 1, 4))));
}

ActionResult ExportClipAction(const ActionCall& call)
{
    // TTD export-clip: a folder gets the lossless clip (RGBA chunks, plane B, meta), a .gif / .mp4 / ... file a video
    std::map<std::string, std::string> options = ParamOptions(call.value.params);
    if (options.count("from") == 0 || options.count("to") == 0)
        return BadRequest("from, to: the history's frames (ttd position names its first and last)");
    if (options["path"].empty())
        options["path"] = StampedFile(capture::RecordingControl::UserRecordingsFolder(), "clip", "gif");
    const ttd::TTDReply reply = ttd::TTDControl(call.emulator->GetContext()).Execute({"export-clip", options});
    if (reply.Ok())
        return ActionResult::Success(reply.body, reply.message.empty() ? "Clip written to " + options["path"]
                                                                       : reply.message);
    return ActionResult::Failure(ttd::TTDControlErrorPhrase(reply.error), reply.message, reply.body);
}

/// cap.audio_capture: one source's own buffer (or the master mix) for N seconds of emulated time, as a WAV file. The
/// machine must run; the action waits for the capture (at most 4x its length in real time, then keeps what came)
ActionResult AudioCapture(const ActionCall& call)
{
    EmulatorContext* context = call.emulator->GetContext();
    AnalyzerManager* analyzers =
        context != nullptr && context->pDebugManager != nullptr ? context->pDebugManager->GetAnalyzerManager() : nullptr;
    AudioCaptureAnalyzer* capture =
        analyzers != nullptr ? analyzers->getAnalyzer<AudioCaptureAnalyzer>("audiocapture") : nullptr;
    if (capture == nullptr || context->pSoundManager == nullptr)
        return Conflict("audio capture is not available on this machine");
    std::string error;
    const double seconds = NumberParam(call.value, "seconds", error).value_or(5.0);
    if (!error.empty())
        return BadRequest(error);
    if (seconds < 0.01 || seconds > 30.0)
        return BadRequest("seconds: 0.01..30");
    AudioSourceType source = AudioSourceType::MasterMix;
    if (!AudioMixer::Capturable(context, TextParam(call.value, "source").value_or(""), source, error))
        return BadRequest(error);
    if (capture->isCaptureArmed())
        return Conflict("an audio capture is running (POST /audio/capture stop ends it)");
    const std::string sourceKey = AudioMixer::Key(source);
    std::string path = TextParam(call.value, "path").value_or("");
    if (path.empty())
        path = StampedFile(capture::RecordingControl::UserRecordingsFolder(), "audio-" + sourceKey, "wav");

    const uint32_t rate = context->pSoundManager->getCoreRate();
    const size_t target = static_cast<size_t>(seconds * static_cast<double>(rate)) * 2;  // interleaved stereo
    {
        Emulator::CpuHold hold(*call.emulator, "audio capture");  // the analyzer's subscriptions change parked
        analyzers->activate("audiocapture");
        capture->startCapture(target, source);
    }
    // Emulated time: in turbo it takes less; a starved host may take longer
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(static_cast<int64_t>(seconds * 4000.0) + 2000);
    while (!capture->isCaptureComplete() && std::chrono::steady_clock::now() < deadline && !call.emulator->IsPaused())
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    {
        Emulator::CpuHold hold(*call.emulator, "audio capture");
        capture->stopCapture();
    }
    const std::vector<int16_t>& buffer = capture->getBuffer();
    const size_t frames = std::min(buffer.size(), capture->getCapturedSamples()) / 2;
    StateNode body = StateNode::Object();
    body["source"] = sourceKey;
    body["seconds"] = static_cast<double>(frames) / static_cast<double>(std::max<uint32_t>(1, rate));
    body["sample_rate"] = rate;
    body["complete"] = capture->isCaptureComplete();
    body["path"] = path;
    if (frames == 0)
        return ActionResult::Failure("Conflict", "no samples came (is the machine running?)", std::move(body));
    std::error_code ec;
    std::filesystem::create_directories(FileHelper::ToFsPath(path).parent_path(), ec);
    TinyWav wav{};
    if (tinywav_open_write(&wav, 2, static_cast<int32_t>(rate), TW_INT16, TW_INTERLEAVED, path.c_str()) != 0)
        return ActionResult::Failure("Conflict", "cannot write " + path, std::move(body));
    tinywav_write_i(&wav, const_cast<void*>(static_cast<const void*>(buffer.data())), static_cast<int>(frames));
    tinywav_close_write(&wav);
    return ActionResult::Success(std::move(body), "Audio of " + sourceKey + " saved: " + path);
}

/// endregion </Recording>
}  // namespace

std::vector<RegistrationHandle> RegisterMachineAreaActions(ActionRegistry& registry)
{
    std::vector<RegistrationHandle> handles;
    auto add = [&](const char* id, const char* label, const char* group, ValueKind kind, const char* params,
                   const char* description, Executor run, ActionRules rules, bool held = false) {
        handles.push_back(registry.Register({id, label, group, kind, held, false, params, description, false},
                                            std::move(run), std::move(rules)));
    };
    const char* machine = "Capture and machine";
    const char* media = "Media";
    const char* input = "Input";

    // region <Machine>
    {
        ActionRules rules = RouteRules("POST", "/switches");
        rules.state = FrontPanelState;
        rules.toggleStem = "mach.switch";
        rules.toggleParam = "name";
        add("mach.switch", "Front-panel switch", machine, ValueKind::Press, "name (turbo|cpm), on? (else flips)",
            "Flip a front-panel switch of the machine: the Profi's TURBO (v3 / v5) or CP/M (v5).", FrontPanel,
            std::move(rules));
    }
    {
        ActionRules rules;
        rules.scope = ActionRules::Fixed(automation_gate::ScopeMachine);
        rules.unavailable = [](Emulator& emulator, const ActionValue&) {
            machines::MachineHistory::Entry previous;
            return machines::MachineHistory::Instance().Previous(CurrentMachineName(emulator), previous)
                       ? std::string()
                       : std::string("no previous model yet");
        };
        add("mach.previous", "Previous model", machine, ValueKind::Press,
            "replace? (true: in place), stranded? (keep), ram_power_on? (random|zero)",
            "Start the model used before this one (the readout machine.previous names it).", PreviousModel,
            std::move(rules));
    }
    // endregion </Machine>

    // region <Media>
    add("media.insert", "Insert a medium", media, ValueKind::Press,
        "drive (A..D, tape, ide0.master, sd..., or auto), path, wp?, access? (readonly|session|writethrough), save? / "
        "discard? / export? (the medium it replaces)",
        "Insert a file into a slot (a disk drive, the tape, an IDE disk, an SD card, a CD-ROM drive).",
        [](const ActionCall& call) { return MediaVerb(call, "insert"); }, RouteRules("POST", "/media/A/insert"));
    add("media.swap", "Swap disks", media, ValueKind::Press,
        "drive, with (another drive: the two exchange their disks) or path (a file replaces the disk), save? / discard?",
        "Exchange the disks of two drives, or replace a disk by another file.", SwapMedia,
        RouteRules("POST", "/media/A/swap"));
    add("media.export", "Save as another format", media, ValueKind::Press,
        "drive (A..D, tape), format (disk: trd, scl, udi, fdi, dsk, td0, mgt, img, hfe, scp; tape: tap, tzx), path? "
        "(else next to its file), force? (false)",
        "Write a disk or the tape as a file of another format (the medium keeps its own file).", ExportMedia,
        RouteRules("POST", "/media/A/export"));
    add("media.discard", "Discard changes", media, ValueKind::Press, "drive (A..D, or a slot id)",
        "Drop the unsaved writes of a disk (or a card): it is as its file again.",
        [](const ActionCall& call) { return MediaVerb(call, "discard"); }, RouteRules("POST", "/media/A/discard"));
    add("media.rescan", "Rescan", media, ValueKind::Press, "drive (a folder or composite medium), save? / discard?",
        "Build a folder or composite medium again from its files on the host.",
        [](const ActionCall& call) { return MediaVerb(call, "rescan"); }, RouteRules("POST", "/media/A/rescan"));
    add("media.open_recent", "Open a recent file", media, ValueKind::Press,
        "index? (0 = the newest), kind? (all|snapshot|tape|disk|rzx|image|cd), autostart? (true), drive?",
        "Open one of the files opened lately (the readout media.recent names them).", OpenRecent,
        RouteRules("POST", "/tape/load"));
    {
        ActionRules rules = RouteRules("POST", "/rzx/play");
        add("rzx.play", "Play an RZX", media, ValueKind::Press, "path, tolerant? (false), switch_model? (true)",
            "Play an RZX input recording (the model switches to the recording's).", RzxPlay, std::move(rules));
    }
    add("rzx.stop", "Stop the RZX", media, ValueKind::Press, "",
        "Stop playing the RZX recording; the machine runs on live.",
        [](const ActionCall& call) {
            const bool stopped = call.emulator->StopRzx();
            StateNode body = StateNode::Object();
            body["stopped"] = stopped;
            body["rzx"] = RzxStatusValue(call.emulator->GetRzxStatus());
            return ActionResult::Success(std::move(body), stopped ? "RZX playback stopped; the machine runs live"
                                                                  : "No RZX playback was running");
        },
        RouteRules("POST", "/rzx/stop"));
    {
        ActionRules rules = RouteRules("POST", "/rzx/seek");
        rules.unavailable = [](Emulator& emulator, const ActionValue&) {
            return emulator.GetRzxStatus().loaded ? std::string() : std::string("no RZX recording played");
        };
        add("rzx.seek", "Seek the RZX", media, ValueKind::Press, "frame (0 = the start) or by (frames, -500)",
            "Go to a frame of the RZX recording (back: from the nearest keyframe).", RzxSeek, std::move(rules));
    }
    // endregion </Media>

    // region <Input>
    add("input.joystick_type", "Joystick type", input, ValueKind::Press,
        "kind (kempston|sinclair1|sinclair2|cursor|fuller|keys|md6|none), connector? (auto), keys? (keys: "
        "up:q,down:a,...)",
        "Plug a joystick of a kind into a connector, or empty one (the readout input.connectors shows them).",
        JoystickType, RouteRules("POST", "/controllers/kempston/plug"));
    add("input.mouse_button", "Mouse button", input, ValueKind::Press,
        "button (left|right|middle), hold? (false: a click), frames? (2)",
        "Click a Kempston mouse button, or hold it while the control is held.", MouseButtonAction,
        RouteRules("POST", "/mouse/press"), true);
    add("input.mouse_release", "Release the mouse", input, ValueKind::Press, "",
        "Release every Kempston mouse button (and a click in progress).",
        [](const ActionCall& call) {
            DebugMouseManager* mouse = Mouse(*call.emulator);
            if (mouse == nullptr)
                return Conflict("Mouse manager not available");
            return FromMouse(mouse->ReleaseAllButtons());
        },
        RouteRules("POST", "/mouse/release_all"));
    // endregion </Input>

    // region <Recording>
    {
        ActionRules rules = RouteRules("POST", "/video/record");
        rules.state = [](Emulator& emulator, const ActionValue&) { return RecordingOn(emulator, false); };
        add("cap.record_toggle", "Record video", machine, ValueKind::Press,
            "preset? (gameplay|music|audio|gif|hq|multitrack), format?, container?, audio?, mode? "
            "(single|multi|audio), profile? (native|1080p|1440p|4k), path?, fps?, scale?, region?",
            "", [](const ActionCall& call) { return Recording(call, "toggle"); }, std::move(rules));
    }
    {
        ActionRules rules = RouteRules("POST", "/video/record");
        rules.state = [](Emulator& emulator, const ActionValue&) { return RecordingOn(emulator, true); };
        rules.unavailable = [](Emulator& emulator, const ActionValue&) {
            const std::optional<bool> on = RecordingOn(emulator, false);
            return on && *on ? std::string() : std::string("no recording runs");
        };
        add("cap.record_pause", "Pause recording", machine, ValueKind::Press, "",
            "Pause the video recording, or continue it.",
            [](const ActionCall& call) {
                const std::optional<bool> paused = RecordingOn(*call.emulator, true);
                return Recording(call, paused && *paused ? "resume" : "pause");
            },
            std::move(rules));
    }
    {
        ActionRules rules = RouteRules("POST", "/ttd/export-clip");
        rules.unavailable = [](Emulator& emulator, const ActionValue&) {
            EmulatorContext* context = emulator.GetContext();
            return context != nullptr && context->pTimeTravelHooks != nullptr && context->pTimeTravelHooks->IsRecording()
                       ? std::string()
                       : std::string("needs a running time travel recording (ttd.start)");
        };
        add("cap.clip_last", "Clip the last seconds", machine, ValueKind::Press,
            "seconds? (10), format? (gif|mp4|mkv|webm|mov|avi), path?, scale? (1..4)", "", ClipLastAction,
            std::move(rules));
    }
    add("ttd.export_clip", "Export a range as a clip", "Time travel", ValueKind::Press,
        "from, to (frames), path? (a .gif / .mp4 / .mkv / .webm / .mov / .avi file: a video, default a .gif under the "
        "recordings folder; a folder: the lossless clip), codec?, scale? (1..4), chunk? (the lossless clip)",
        "Write frames of the time travel history as a video file (or the lossless clip folder).", ExportClipAction,
        RouteRules("POST", "/ttd/export-clip"));
    {
        ActionRules rules = RouteRules("POST", "/audio/capture");
        rules.unavailable = [](Emulator& emulator, const ActionValue&) {
            return emulator.IsPaused() ? std::string("needs a running machine (the capture counts emulated samples)")
                                       : std::string();
        };
        add("cap.audio_capture", "Capture audio", machine, ValueKind::Press,
            "source? (master, beeper, ay1, covox, gs, ...), seconds? (5, 0.01..30), path? (a .wav)",
            "Record one sound source (or the mix) for some seconds into a WAV file.", AudioCapture, std::move(rules));
    }
    // endregion </Recording>
    return handles;
}

}  // namespace actions
