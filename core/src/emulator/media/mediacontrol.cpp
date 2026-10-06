#include "stdafx.h"

#include "mediacontrol.h"

#include "emulator/io/storage/compose/changeattributor.h"
#include "emulator/io/storage/compose/composedlayout.h"
#include "emulator/io/storage/fat/fatsynthvolume.h"
#include "emulator/io/storage/sessionwritemap.h"
#include "emulator/media/composedescriptor.h"
#include "emulator/media/compositemediumfactory.h"

#include "emulator/io/ide/idecontroller.h"
#include "emulator/io/storage/cd/cdimage.h"
#include "emulator/io/storage/hddimageformats.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>
#include <set>

#include "common/filehelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/media/floppyformats.h"
#include "emulator/media/mediaformatregistry.h"
#include "emulator/media/mediatargets.h"
#include "emulator/state/statenodejson.h"

namespace
{
    StateNode Strings(const std::vector<std::string>& items)
    {
        StateNode array = StateNode::Array();
        for (const std::string& item : items)
            array.push(item);
        return array;
    }

    /// "{...}": a composition descriptor's JSON text instead of a path
    bool IsInlineDescriptor(const std::string& path)
    {
        const size_t first = path.find_first_not_of(" \t\r\n");
        return first != std::string::npos && path[first] == '{';
    }

    std::string Hex64(uint64_t value)
    {
        char text[24];
        std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(value));
        return text;
    }

    /// The `compose` / `layers` body: the layout and every layer
    StateNode CompositeValue(const CompositeInfo& info)
    {
        StateNode value = StateNode::Object();
        value["descriptor"] = info.descriptor;
        value["fs"] = info.fsName;
        value["build"] = info.build;
        value["sectors"] = info.sectors;
        value["bytes"] = info.sectors * 512;
        value["clusters"] = static_cast<uint64_t>(info.clusterCount);
        value["sectorsPerCluster"] = static_cast<uint64_t>(info.sectorsPerCluster);
        value["contentId"] = Hex64(info.contentId);
        value["files"] = info.files;
        value["fileBytes"] = info.bytes;
        value["sourceDevices"] = static_cast<uint64_t>(info.sourceDevices);
        StateNode layers = StateNode::Array();
        for (const CompositeLayerInfo& layer : info.layers)
        {
            StateNode l = StateNode::Object();
            l["name"] = layer.name;
            l["kind"] = layer.kind;
            l["path"] = layer.path;
            l["mount"] = layer.mount;
            l["from"] = layer.from;
            l["files"] = layer.files;
            l["bytes"] = layer.bytes;
            layers.push(std::move(l));
        }
        value["layers"] = std::move(layers);
        return value;
    }

    std::string Lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    std::string Trim(const std::string& text)
    {
        const size_t first = text.find_first_not_of(" \t");
        if (first == std::string::npos)
            return {};
        return text.substr(first, text.find_last_not_of(" \t") - first + 1);
    }

    MediaReply Fail(MediaError error, const std::string& message)
    {
        MediaReply reply;
        reply.result = MediaResult::Fail(error, message);
        return reply;
    }

    bool ParseBool(const std::string& text, bool& value)
    {
        const std::string v = Lower(Trim(text));
        if (v.empty() || v == "1" || v == "true" || v == "yes" || v == "on")
            value = true;
        else if (v == "0" || v == "false" || v == "no" || v == "off")
            value = false;
        else
            return false;
        return true;
    }

    bool ParseUnsigned(const std::string& text, uint64_t& value)
    {
        const std::string v = Trim(text);
        if (v.empty() || !std::all_of(v.begin(), v.end(), [](unsigned char c) { return std::isdigit(c); }))
            return false;
        try
        {
            value = std::stoull(v);
        }
        catch (const std::exception&)
        {
            return false;
        }
        return true;
    }

    using Options = std::map<std::string, std::string>;

    /// A flag's value; false when absent. Malformed values fail
    MediaResult Flag(const Options& options, const char* name, bool& value)
    {
        value = false;
        auto it = options.find(name);
        if (it == options.end())
            return MediaResult::Success();
        if (!ParseBool(it->second, value))
            return MediaResult::Fail(MediaError::BadRequest,
                                     std::string("option '") + name + "': expected true or false, got '" + it->second + "'");
        return MediaResult::Success();
    }

    const std::vector<std::string> kInsertOptions = {"access", "format", "fs", "codepage", "free", "wp", "kind", "device",
                                                     "save", "export", "discard", "end_recording", "async", "immediate"};

    /// The parked emulator: a save or export reads the medium while the guest
    /// cannot write to it (the same thing SaveDisk does)
    class ParkedEmulator
    {
    public:
        explicit ParkedEmulator(EmulatorContext* context) : _emulator(context ? context->pEmulator : nullptr)
        {
            if (_emulator && _emulator->IsRunning() && !_emulator->IsPaused())
            {
                _emulator->Pause(false);
                _emulator->WaitForPauseConfirmation(1000);
                _parked = true;
            }
        }
        ~ParkedEmulator()
        {
            if (_parked)
                _emulator->Resume(false);
        }
        ParkedEmulator(const ParkedEmulator&) = delete;
        ParkedEmulator& operator=(const ParkedEmulator&) = delete;

    private:
        Emulator* _emulator = nullptr;
        bool _parked = false;
    };

    std::string SlotList(const MediaManager& manager)
    {
        std::string list;
        for (const SlotInfo& info : manager.List())
        {
            list += (list.empty() ? "" : ", ") + info.descriptor.id;
            if (!info.descriptor.aliases.empty())
            {
                list += " (";
                for (size_t i = 0; i < info.descriptor.aliases.size(); i++)
                    list += (i ? ", " : "") + info.descriptor.aliases[i];
                list += ")";
            }
        }
        return list.empty() ? "none" : list;
    }

    bool ParseKind(const std::string& text, MediaKind& kind)
    {
        const std::string k = Lower(text);
        if (k == "floppy")
            kind = MediaKind::Floppy;
        else if (k == "tape")
            kind = MediaKind::Tape;
        else if (k == "block")
            kind = MediaKind::Block;
        else if (k == "optical")
            kind = MediaKind::Optical;
        else
            return false;
        return true;
    }
}  // namespace

/// region <MediaReply>

StateNode MediaReply::ToValue() const
{
    StateNode value = StateNode::Object();
    value["ok"] = result.Ok();
    if (!result.Ok())
    {
        value["error"] = MediaErrorCode(result.error);
        value["message"] = result.message;
    }
    if (!slot.empty())
        value["slot"] = slot;
    value["pending"] = pending;
    value["revision"] = revision;
    value["report"] = Strings(result.report);
    for (const auto& [key, field] : body.members)
        value[key] = field;
    return value;
}

std::string MediaReply::ToJson() const
{
    return StateNodeToJsonText(ToValue());
}

/// endregion </MediaReply>

MediaControl::MediaControl(EmulatorContext* context)
    : _context(context), _manager(context ? context->pMediaManager : nullptr)
{
}

const std::vector<std::string>& MediaControl::Verbs()
{
    static const std::vector<std::string> verbs = {"list", "info", "formats", "targets", "insert", "eject", "swap",
                                                   "save", "export", "discard", "rescan", "create", "protect",
                                                   "compose", "layers", "changes"};
    return verbs;
}

const std::vector<std::string>& MediaControl::OptionsFor(const std::string& verb)
{
    static const std::map<std::string, std::vector<std::string>> options = {
        {"list", {}},
        {"info", {}},
        {"formats", {"kind"}},
        {"targets", {}},
        {"insert", kInsertOptions},
        {"swap", kInsertOptions},
        {"eject", {"save", "export", "discard", "end_recording", "async"}},
        {"save", {"retarget", "compression", "compact", "fs", "size", "strategy", "force"}},
        {"export", {"compression", "parent", "compact", "fs", "size"}},
        {"discard", {"async"}},
        {"rescan", {"async"}},
        {"create", {"format", "cylinders", "sides", "size", "save", "export", "discard", "end_recording", "async"}},
        {"protect", {"on"}},
        {"compose", {"fs", "codepage", "free"}},
        {"layers", {}},
        {"changes", {}},
    };
    static const std::vector<std::string> none;
    auto it = options.find(verb);
    return it == options.end() ? none : it->second;
}

MediaReply MediaControl::Execute(const MediaRequest& request)
{
    if (!_manager)
        return Fail(MediaError::NotSupported, "this emulator has no media manager");
    MediaReply reply = Run(request);
    reply.revision = _manager->Revision();  // on failures too: the client's view stays current
    return reply;
}

MediaReply MediaControl::Run(const MediaRequest& request)
{

    const std::string verb = Lower(Trim(request.verb));
    const auto& verbs = Verbs();
    if (std::find(verbs.begin(), verbs.end(), verb) == verbs.end())
    {
        std::string known;
        for (const std::string& v : verbs)
            known += (known.empty() ? "" : ", ") + v;
        return Fail(MediaError::BadRequest, "unknown media verb '" + request.verb + "' (verbs: " + known + ")");
    }

    // Every option name is checked, so a typo fails the same way on every surface
    const auto& allowed = OptionsFor(verb);
    for (const auto& [name, value] : request.options)
    {
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end())
        {
            std::string list;
            for (const std::string& a : allowed)
                list += (list.empty() ? "" : ", ") + a;
            return Fail(MediaError::BadRequest, "unknown option '" + name + "' for " + verb +
                                                    (list.empty() ? " (it takes none)" : " (options: " + list + ")"));
        }
    }

    MediaReply reply;
    if (verb == "list")
        reply = List();
    else if (verb == "info")
        reply = Info(request);
    else if (verb == "formats")
        reply = Formats(request);
    else if (verb == "targets")
        reply = Targets(request);
    else if (verb == "insert")
        reply = Insert(request, false);
    else if (verb == "swap")
        reply = Insert(request, true);
    else if (verb == "eject")
        reply = Eject(request);
    else if (verb == "save")
        reply = Save(request);
    else if (verb == "export")
        reply = Export(request);
    else if (verb == "discard")
        reply = Discard(request);
    else if (verb == "rescan")
        reply = Rescan(request);
    else if (verb == "create")
        reply = Create(request);
    else if (verb == "compose")
        reply = Compose(request);
    else if (verb == "layers")
        reply = Layers(request);
    else if (verb == "changes")
        reply = Changes(request);
    else
        reply = Protect(request);
    return reply;
}

/// region <Composite media>

MediaReply MediaControl::Compose(const MediaRequest& request)
{
    MediaReply reply;
    const Options& o = request.options;
    CompositeBuildOptions options;
    if (auto it = o.find("fs"); it != o.end())
    {
        const std::string fs = Lower(Trim(it->second));
        if (fs == "fat16")
            options.fs = FatType::Fat16;
        else if (fs == "fat32")
            options.fs = FatType::Fat32;
        else
            return Fail(MediaError::BadRequest, "fs '" + it->second + "': expected fat16 or fat32");
    }
    if (auto it = o.find("codepage"); it != o.end())
    {
        CodePage page;
        if (!UnicodeHelper::ParseCodePage(it->second, page))
            return Fail(MediaError::BadRequest, "codepage '" + it->second + "': expected cp866 or cp1251");
        options.codePage = page;
    }
    if (auto it = o.find("free"); it != o.end())
    {
        uint64_t bytes = 0;
        if (!ComposeDescriptor::ParseSize(it->second, bytes))
            return Fail(MediaError::BadRequest, "free '" + it->second + "': expected a size (256MiB, 1048576)");
        options.freeBytes = bytes;
    }
    if (Trim(request.path).empty())
        return Fail(MediaError::BadRequest, "name a descriptor file (*.ucompose.yaml) or give its JSON text");

    const ComposeDescriptor descriptor = IsInlineDescriptor(request.path)
                                             ? ComposeDescriptor::Parse(request.path, std::filesystem::current_path(), ComposeDescriptor::kInlineName)
                                             : ComposeDescriptor::Load(FileHelper::ToFsPath(request.path));
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    reply.result = CompositeMediumFactory::Build(descriptor, options, volume, info);
    if (reply.result.Ok())
        reply.body["compose"] = CompositeValue(info);
    return reply;
}

MediaReply MediaControl::Layers(const MediaRequest& request)
{
    MediaReply reply;
    reply.result = ResolveSelector(*_manager, request.selector, reply.slot);
    if (!reply.result.Ok())
        return reply;
    Medium* medium = _manager->GetMedium(reply.slot);
    if (!medium || !medium->Composite())
        return Fail(MediaError::NotSupported, "slot '" + reply.slot + "' does not hold a composite medium");
    reply.body["layers"] = CompositeValue(*medium->Composite());
    reply.result.report = medium->Report();
    return reply;
}

MediaReply MediaControl::Changes(const MediaRequest& request)
{
    MediaReply reply;
    reply.result = ResolveSelector(*_manager, request.selector, reply.slot);
    if (!reply.result.Ok())
        return reply;
    Medium* medium = _manager->GetMedium(reply.slot);
    if (!medium || !medium->Block() || medium->Kind() != MediaKind::Block)
        return Fail(MediaError::NotSupported, "slot '" + reply.slot + "' does not hold a disk or a card");
    SessionWriteMap* session = medium->Session();
    if (!session)
        return Fail(MediaError::NotSupported, "slot '" + reply.slot +
                                                  "': the guest's writes are listed for media with session writes (this one is " +
                                                  AccessModeName(medium->Access()) + ")");

    ChangeSet set;
    std::string error;
    bool ok = false;
    {
        ParkedEmulator parked(_context);
        const auto* layout = dynamic_cast<const IComposedLayout*>(&session->Base());
        ok = ChangeAttributor::Attribute(session->Base(), *session, session->Changes(), layout, set, &error);
    }
    if (!ok)
        return Fail(MediaError::NotSupported, "slot '" + reply.slot + "': " + error);

    const CompositeInfo* composite = medium->Composite();
    StateNode changes = StateNode::Array();
    for (const FileChange& change : set.changes)
    {
        StateNode c = StateNode::Object();
        c["op"] = FileChange::OpName(change.op);
        c["path"] = change.path;
        if (change.op == FileChange::Op::Rename)
            c["oldPath"] = change.oldPath;
        std::string layer;
        if (change.layer >= 0 && composite && static_cast<size_t>(change.layer) < composite->layers.size())
            layer = composite->layers[static_cast<size_t>(change.layer)].name;
        c["layer"] = layer;
        c["sizeBefore"] = change.sizeBefore;
        c["sizeAfter"] = change.sizeAfter;
        changes.push(std::move(c));
    }
    reply.body["changes"] = std::move(changes);
    reply.body["warnings"] = Strings(set.warnings);
    reply.body["changedSectors"] = set.changedSectors;
    reply.body["directoriesRead"] = static_cast<uint64_t>(set.directoriesRead);
    reply.body["fullScan"] = set.fullScan;
    return reply;
}

/// endregion <Composite media>

/// region <Selectors>

MediaResult MediaControl::ResolveSelector(const MediaManager& manager, const std::string& selector, std::string& slotId,
                                          bool allowDetached)
{
    std::string text = Lower(Trim(selector));
    if (text.size() > 1 && text.back() == ':')
        text.pop_back();  // "a:" is drive A
    if (text.empty())
        return MediaResult::Fail(MediaError::BadRequest, "name a slot (" + SlotList(manager) + ")");

    const std::vector<SlotInfo> slots = manager.List();

    // 1. The id
    for (const SlotInfo& info : slots)
    {
        if (Lower(info.descriptor.id) == text)
        {
            slotId = info.descriptor.id;
            return MediaResult::Success();
        }
    }
    // 2. An alias
    for (const SlotInfo& info : slots)
    {
        for (const std::string& alias : info.descriptor.aliases)
        {
            if (Lower(alias) == text)
            {
                slotId = info.descriptor.id;
                return MediaResult::Success();
            }
        }
    }
    // 3. kind:index
    const size_t colon = text.find(':');
    MediaKind kind;
    uint64_t index = 0;
    if (colon != std::string::npos && ParseKind(text.substr(0, colon), kind) && ParseUnsigned(text.substr(colon + 1), index))
    {
        for (const SlotInfo& info : slots)
        {
            if (info.descriptor.kind == kind && info.index == static_cast<int>(index))
            {
                slotId = info.descriptor.id;
                return MediaResult::Success();
            }
        }
    }
    // 4. tag:a+b - every tag must be there, and one slot must match
    if (text.rfind("tag:", 0) == 0)
    {
        std::vector<std::string> wanted;
        std::string rest = text.substr(4);
        size_t start = 0;
        while (start <= rest.size())
        {
            const size_t plus = rest.find('+', start);
            const std::string tag = rest.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
            if (!tag.empty())
                wanted.push_back(tag);
            if (plus == std::string::npos)
                break;
            start = plus + 1;
        }
        std::vector<std::string> matches;
        for (const SlotInfo& info : slots)
        {
            const bool all = std::all_of(wanted.begin(), wanted.end(), [&info](const std::string& tag) {
                return std::find(info.tags.begin(), info.tags.end(), tag) != info.tags.end();
            });
            if (all && !wanted.empty())
                matches.push_back(info.descriptor.id);
        }
        if (matches.size() == 1)
        {
            slotId = matches.front();
            return MediaResult::Success();
        }
        if (matches.size() > 1)
        {
            std::string list;
            for (const std::string& m : matches)
                list += (list.empty() ? "" : ", ") + m;
            return MediaResult::Fail(MediaError::AmbiguousSlot, selector + " matches " + list + ": name one");
        }
    }
    // A detached medium's former slot
    if (allowDetached)
    {
        for (const SlotInfo& info : manager.Detached())
        {
            if (Lower(info.descriptor.id) == text)
            {
                slotId = info.descriptor.id;
                return MediaResult::Success();
            }
        }
    }
    return MediaResult::Fail(MediaError::UnknownSlot,
                             "no slot '" + Trim(selector) + "' on this machine (slots: " + SlotList(manager) + ")");
}

/// endregion </Selectors>

StateNode MediaControl::SlotValue(const SlotInfo& info)
{
    const SlotDescriptor& d = info.descriptor;
    StateNode slot = StateNode::Object();
    slot["id"] = d.id;
    slot["kind"] = MediaKindName(d.kind);
    if (!info.detached)
    {
        slot["label"] = d.label;
        slot["index"] = info.index;
        slot["aliases"] = Strings(d.aliases);
        slot["removable"] = d.removable;
        slot["acceptsFolder"] = d.acceptsFolder;
        slot["writeProtect"] = info.writeProtect;
        if (!d.guestName.empty())
            slot["guestName"] = d.guestName;
    }
    slot["tags"] = Strings(info.tags);
    slot["detached"] = info.detached;
    slot["state"] = info.detached ? "detached" : info.pending ? "pending" : info.present ? "present" : "empty";

    if (info.present || info.detached)
    {
        StateNode medium = StateNode::Object();
        medium["source"] = info.source;
        medium["format"] = info.format;
        medium["access"] = AccessModeName(info.access);
        medium["dirty"] = info.dirty;
        medium["dirtyUnits"] = info.changedUnits;
        medium["changes"] = info.changes;
        slot["medium"] = medium;
    }
    else
    {
        slot["medium"] = StateNode();
    }
    return slot;
}

/// A CD's sessions and tracks (an audio CD built from a folder: the file of each track)
static StateNode DiscValue(const CdImage& disc)
{
    auto msf = [](cd::Msf m) {
        char text[16];
        std::snprintf(text, sizeof(text), "%02u:%02u:%02u", static_cast<unsigned>(m.m), static_cast<unsigned>(m.s), static_cast<unsigned>(m.f));
        return std::string(text);
    };
    StateNode d = StateNode::Object();
    d["format"] = disc.Format();
    d["sessions"] = int(disc.SessionCount());
    d["lead_out_lba"] = static_cast<unsigned>(disc.LeadOutLba());
    StateNode tracks = StateNode::Array();
    for (size_t i = 0; i < disc.TrackCount(); i++)
    {
        const cd::Track& t = disc.TrackAt(i);
        StateNode track = StateNode::Object();
        track["number"] = int(t.number);
        track["session"] = int(t.session);
        track["type"] = cd::TrackModeName(t.mode);
        track["start_lba"] = static_cast<unsigned>(t.startLba);
        track["start_msf"] = msf(cd::LbaToMsf(t.startLba));
        track["length_msf"] = msf(cd::FramesToMsf(t.Frames()));
        if (!disc.TrackTitle(i).empty())
            track["title"] = disc.TrackTitle(i);
        tracks.push(track);
    }
    d["tracks"] = tracks;
    return d;
}

/// region <Verbs>

MediaReply MediaControl::List()
{
    MediaReply reply;
    StateNode slots = StateNode::Array();
    for (const SlotInfo& info : _manager->List())
        slots.push(SlotValue(info));
    StateNode detached = StateNode::Array();
    for (const SlotInfo& info : _manager->Detached())
        detached.push(SlotValue(info));
    reply.body["slots"] = slots;
    reply.body["detached"] = detached;
    return reply;
}

MediaReply MediaControl::Info(const MediaRequest& request)
{
    MediaReply reply;
    reply.result = ResolveSelector(*_manager, request.selector, reply.slot, /*allowDetached*/ true);
    if (!reply.result.Ok())
        return reply;

    if (auto info = _manager->Info(reply.slot))
    {
        reply.body["info"] = SlotValue(*info);
        reply.pending = info->pending;
        if (Medium* medium = _manager->GetMedium(reply.slot))
        {
            reply.result.report = medium->Report();  // skipped folder entries, format notes
            if (const CdImage* disc = medium->Cd(); disc && reply.body["info"]["medium"].isObject())
                reply.body["info"]["medium"]["disc"] = DiscValue(*disc);
        }
        return reply;
    }
    for (const SlotInfo& info : _manager->Detached())
    {
        if (info.descriptor.id == reply.slot)
            reply.body["info"] = SlotValue(info);
    }
    return reply;
}

MediaReply MediaControl::Formats(const MediaRequest& request)
{
    MediaReply reply;
    StateNode formats = StateNode::Object();
    auto it = request.options.find("kind");
    std::vector<MediaKind> kinds = {MediaKind::Floppy, MediaKind::Tape, MediaKind::Block, MediaKind::Optical};
    if (it != request.options.end())
    {
        MediaKind kind;
        if (!ParseKind(it->second, kind))
            return Fail(MediaError::BadRequest, "kind '" + it->second + "': expected floppy, tape, block or optical");
        kinds = {kind};
    }
    for (MediaKind kind : kinds)
        formats[MediaKindName(kind)] = Strings(MediaFormatRegistry::Extensions(kind));
    reply.body["formats"] = formats;
    return reply;
}

/// Where a file can go on this machine (media-drop-targets design §4.4): the
/// file's class, the targets in the chooser's order, the default and the
/// refusal. A refusal is an answer, not an error: the reply is ok
MediaReply MediaControl::Targets(const MediaRequest& request)
{
    const std::string path = Trim(request.path);
    if (path.empty())
        return Fail(MediaError::BadRequest, "targets needs a path (a file or a folder)");

    const MediaPlan plan = MediaTargets::Plan(_context, MediaTargets::Classify(path));
    MediaReply reply;

    StateNode file = StateNode::Object();
    file["path"] = plan.file.path;
    file["folder"] = plan.file.folder;
    StateNode kinds = StateNode::Array();
    for (FileKind kind : plan.file.kinds)
        kinds.push(StateNode(std::string(FileKindName(kind))));
    file["kinds"] = kinds;
    file["format"] = plan.file.format;
    file["evidence"] = Strings(plan.file.evidence);
    reply.body["file"] = file;

    StateNode targets = StateNode::Array();
    for (const MediaTarget& target : plan.targets)
    {
        StateNode t = StateNode::Object();
        t["action"] = target.action == MediaTarget::Action::Insert   ? "insert"
                      : target.action == MediaTarget::Action::Load ? "load"
                                                                     : "newMachine";
        t["as"] = FileKindName(target.as);
        t["slot"] = target.slotId.empty() ? StateNode() : StateNode(target.slotId);
        if (target.action == MediaTarget::Action::NewMachine)
            t["model"] = target.model;
        t["label"] = target.label;
        t["occupiedBy"] = target.occupiedBy.empty() ? StateNode() : StateNode(target.occupiedBy);
        t["dirty"] = target.dirty;
        t["autostart"] = target.autostart;
        targets.push(t);
    }
    reply.body["targets"] = targets;
    reply.body["default"] = plan.defaultTarget < 0 ? StateNode() : StateNode(static_cast<int64_t>(plan.defaultTarget));
    reply.body["refusal"] = plan.refusal.empty() ? StateNode() : StateNode(plan.refusal);
    if (plan.defaultTarget >= 0)
        reply.slot = plan.targets[static_cast<size_t>(plan.defaultTarget)].slotId;
    return reply;
}

MediaResult MediaControl::ChooseSlot(const std::string& path, const Options& options, std::string& slotId)
{
    // One analysis for every entry point (media-drop-targets design §4)
    FileClass file = MediaTargets::Classify(path);
    if (auto hint = options.find("kind"); hint != options.end())
    {
        MediaKind kind = MediaKind::Block;
        if (!ParseKind(hint->second, kind))
            return MediaResult::Fail(MediaError::BadRequest, "kind '" + hint->second + "': expected floppy, tape, block or optical");
        const std::string ext = Lower(FileHelper::GetFileExtension(path));
        switch (kind)
        {
            case MediaKind::Floppy: file.kinds = {FileKind::Floppy}; break;
            case MediaKind::Tape: file.kinds = {FileKind::Tape}; break;
            case MediaKind::Optical: file.kinds = {FileKind::Optical}; break;
            case MediaKind::Block:
                file.kinds = HddImageFormats::IsHardDiskExtension(ext) ? std::vector<FileKind>{FileKind::Hdd, FileKind::SdCard}
                                                                       : std::vector<FileKind>{FileKind::SdCard, FileKind::Hdd};
                break;
        }
    }
    // A snapshot, a recording or labels are no medium: `insert` takes media only
    file.kinds.erase(std::remove_if(file.kinds.begin(), file.kinds.end(), [](FileKind k) { return !MediaTargets::IsMedium(k); }),
                     file.kinds.end());
    if (file.kinds.empty())
        return MediaResult::Fail(MediaError::UnknownFormat,
                                 "'" + path + "' is no medium this emulator knows: name the slot, or say kind=floppy|tape|block|optical");

    const MediaPlan plan = MediaTargets::Plan(_context, file);
    if (plan.Refused())
        return MediaResult::Fail(MediaError::KindMismatch, plan.refusal);

    // One target: no question. Several: the caller names one - except floppy
    // drives, which are interchangeable: the first empty one (an occupied
    // drive is replaced only when every drive is taken, drive A first)
    // (the plan's order is fixed whatever the slots hold: the empty one is looked for here)
    const auto emptyFirst = std::find_if(plan.targets.begin(), plan.targets.end(),
                                         [](const MediaTarget& t) { return t.occupiedBy.empty(); });
    const MediaTarget& chosen = emptyFirst != plan.targets.end() ? *emptyFirst : plan.targets.front();
    if (plan.targets.size() > 1 && chosen.as != FileKind::Floppy)
    {
        const size_t slash = path.find_last_of("/\\");
        return MediaResult::Fail(MediaError::AmbiguousSlot, "several slots take '" +
                                                                (slash == std::string::npos ? path : path.substr(slash + 1)) +
                                                                "': " + plan.SlotList() + " - name one");
    }
    slotId = chosen.slotId;
    return MediaResult::Success();
}

MediaResult MediaControl::ApplyDisposition(const std::string& slotId, const Options& options, Disposition& remaining)
{
    remaining = Disposition::None;
    bool save = false;
    bool discard = false;
    if (MediaResult r = Flag(options, "save", save); !r.Ok())
        return r;
    if (MediaResult r = Flag(options, "discard", discard); !r.Ok())
        return r;
    auto exportIt = options.find("export");
    const bool exportTo = exportIt != options.end();
    if (static_cast<int>(save) + static_cast<int>(discard) + static_cast<int>(exportTo) > 1)
        return MediaResult::Fail(MediaError::BadRequest, "say one of save, export or discard");
    if (exportTo && Trim(exportIt->second).empty())
        return MediaResult::Fail(MediaError::BadRequest, "export needs a path");

    const auto info = _manager->Info(slotId);
    if (!info || !info->present || !info->dirty)
        return MediaResult::Success();  // nothing to decide about

    if (discard)
    {
        remaining = Disposition::Discard;
        return MediaResult::Success();
    }
    if (save || exportTo)
    {
        // Written with the guest parked; the slot then holds a clean medium (save)
        // or a copy exists (export), so the swap itself may drop the writes
        ParkedEmulator parked(_context);
        const MediaResult written = save ? _manager->Save(slotId) : _manager->Export(slotId, Trim(exportIt->second));
        if (!written.Ok())
            return written;
        remaining = Disposition::Discard;
    }
    return MediaResult::Success();
}

void MediaControl::Finish(MediaReply& reply, const Options& options)
{
    if (!reply.result.Ok())
        return;
    bool async = false;
    if (MediaResult r = Flag(options, "async", async); !r.Ok())
    {
        reply.result = r;
        return;
    }
    if (async)
    {
        const auto info = _manager->Info(reply.slot);
        reply.pending = info && info->pending;
        return;
    }
    reply.pending = !_manager->WaitApplied(reply.slot, kSyncTimeoutMs);
}

MediaReply MediaControl::Insert(const MediaRequest& request, bool swap)
{
    MediaReply reply;
    const std::string path = Trim(request.path);
    if (path.empty())
        return Fail(MediaError::BadRequest, std::string(swap ? "swap" : "insert") + " needs a path (a file or a folder)");

    if (!swap && Lower(Trim(request.selector)) == "auto")
        reply.result = ChooseSlot(path, request.options, reply.slot);
    else
        reply.result = ResolveSelector(*_manager, request.selector, reply.slot);
    if (!reply.result.Ok())
        return reply;
    const Options& o = request.options;

    // An IDE unit can change its drive first: device=cdrom puts a CD-ROM drive
    // there, device=disk a hard disk, device=cf a CompactFlash card on an IDE
    // adapter (the unit must be empty)
    if (auto it = o.find("device"); it != o.end())
    {
        const std::string device = Lower(Trim(it->second));
        if (device != "disk" && device != "cdrom" && device != "cf")
            return Fail(MediaError::BadRequest, "device '" + it->second + "': expected disk, cdrom or cf");
        const int unit = IdeController::UnitForSlot(reply.slot);
        if (unit < 0 || !_context || !_context->pIdeController)
            return Fail(MediaError::BadRequest, "device: only an IDE unit (ide0.master, ide0.slave; ide1.* on the Sprinter) changes its drive");
        std::string error;
        ParkedEmulator parked(_context);
        const IdeController::UnitKind kind = device == "cdrom" ? IdeController::UnitKind::Cdrom
                                             : device == "cf"  ? IdeController::UnitKind::CompactFlash
                                                               : IdeController::UnitKind::Disk;
        if (!_context->pIdeController->SetUnitKind(unit, kind, &error))
            return Fail(MediaError::BadRequest, error);
    }

    // Options
    InsertOptions options;
    if (auto it = o.find("access"); it != o.end())
    {
        AccessMode access;
        if (!ParseAccessMode(Trim(it->second), access))
            return Fail(MediaError::BadRequest, "access '" + it->second + "': expected readonly, session or writethrough");
        options.access = access;
    }
    if (auto it = o.find("fs"); it != o.end())
    {
        const std::string fs = Lower(Trim(it->second));
        if (fs != "fat16" && fs != "fat32")
            return Fail(MediaError::BadRequest, "fs '" + it->second + "': expected fat16 or fat32");
        options.fs = fs == "fat32" ? FatType::Fat32 : FatType::Fat16;
    }
    if (auto it = o.find("codepage"); it != o.end())
    {
        CodePage page;
        if (!UnicodeHelper::ParseCodePage(Trim(it->second), page))
            return Fail(MediaError::BadRequest, "codepage '" + it->second + "': expected cp866 or cp1251");
        options.codePage = page;
    }
    if (auto it = o.find("free"); it != o.end())
    {
        uint64_t bytes = 0;
        if (!ParseUnsigned(it->second, bytes))
            return Fail(MediaError::BadRequest, "free '" + it->second + "': expected a number of bytes");
        options.freeBytes = bytes;
    }
    bool flag = false;
    if (MediaResult r = Flag(o, "wp", flag); !r.Ok())
        return Fail(r.error, r.message);
    options.writeProtect = flag;
    if (MediaResult r = Flag(o, "end_recording", flag); !r.Ok())
        return Fail(r.error, r.message);
    options.endRecording = flag;
    if (MediaResult r = Flag(o, "immediate", flag); !r.Ok())
        return Fail(r.error, r.message);
    options.immediate = flag;

    MediaSource source;
    source.path = path;
    source.type = request.upload ? MediaSourceType::Upload
                                 : FileHelper::IsFolder(path) ? MediaSourceType::Folder : MediaSourceType::File;
    if (IsInlineDescriptor(path))
    {
        // A composition descriptor given as text (WebAPI / MCP / scripts) instead of a file
        source.inlineBody = path;
        source.path.clear();
        source.type = MediaSourceType::Composite;
    }
    if (auto it = o.find("format"); it != o.end())
        source.formatHint = Lower(Trim(it->second));

    if (MediaResult r = ApplyDisposition(reply.slot, o, options.disposition); !r.Ok())
    {
        reply.result = r;
        return reply;
    }
    options.cancelRequested = request.cancelRequested;
    options.onProgress = request.onProgress;
    reply.result = _manager->Insert(reply.slot, source, options);
    Finish(reply, o);
    return reply;
}

MediaReply MediaControl::Eject(const MediaRequest& request)
{
    MediaReply reply;
    reply.result = ResolveSelector(*_manager, request.selector, reply.slot);
    if (!reply.result.Ok())
        return reply;

    EjectOptions options;
    bool end = false;
    if (MediaResult r = Flag(request.options, "end_recording", end); !r.Ok())
        return Fail(r.error, r.message);
    options.endRecording = end;
    if (MediaResult r = ApplyDisposition(reply.slot, request.options, options.disposition); !r.Ok())
    {
        reply.result = r;
        return reply;
    }
    reply.result = _manager->Eject(reply.slot, options);
    Finish(reply, request.options);
    return reply;
}

/// `compact`, `fs`, `size` of save / export (S1 compact); a reply with an error when one is malformed
static MediaReply CompactOptions(const std::map<std::string, std::string>& o, bool& compact, std::optional<FatType>& fs,
                                 std::optional<uint64_t>& size)
{
    MediaReply reply;
    if (auto it = o.find("compact"); it != o.end())
    {
        const std::string v = Lower(Trim(it->second));
        if (v.empty() || v == "true" || v == "1" || v == "yes" || v == "on")
            compact = true;
        else if (v != "false" && v != "0" && v != "no" && v != "off")
        {
            reply.result = MediaResult::Fail(MediaError::BadRequest, "compact '" + it->second + "': expected true or false");
            return reply;
        }
    }
    if (auto it = o.find("fs"); it != o.end())
    {
        const std::string v = Lower(Trim(it->second));
        if (v == "fat16")
            fs = FatType::Fat16;
        else if (v == "fat32")
            fs = FatType::Fat32;
        else
        {
            reply.result = MediaResult::Fail(MediaError::BadRequest, "fs '" + it->second + "': expected fat16 or fat32");
            return reply;
        }
    }
    if (auto it = o.find("size"); it != o.end())
    {
        uint64_t bytes = 0;
        if (!ComposeDescriptor::ParseSize(it->second, bytes) || bytes == 0)
        {
            reply.result = MediaResult::Fail(MediaError::BadRequest, "size '" + it->second + "': expected a size (64MiB, 67108864)");
            return reply;
        }
        size = bytes;
    }
    if ((fs || size) && !compact)
        reply.result = MediaResult::Fail(MediaError::BadRequest, "fs and size go with compact (a re-synthesized volume)");
    return reply;
}

MediaReply MediaControl::Save(const MediaRequest& request)
{
    MediaReply reply;
    reply.result = ResolveSelector(*_manager, request.selector, reply.slot, /*allowDetached*/ true);
    if (!reply.result.Ok())
        return reply;

    SaveOptions options;
    options.path = Trim(request.path);
    auto it = request.options.find("retarget");
    if (it != request.options.end() && !ParseBool(it->second, options.allowRetarget))
        return Fail(MediaError::BadRequest, "retarget '" + it->second + "': expected true or false");
    if (auto compression = request.options.find("compression"); compression != request.options.end())
        options.compression = Trim(compression->second);
    if (MediaReply bad = CompactOptions(request.options, options.compact, options.fs, options.size); !bad.result.Ok())
        return bad;
    if (auto strategy = request.options.find("strategy"); strategy != request.options.end())
        options.strategy = Lower(Trim(strategy->second));
    if (auto force = request.options.find("force"); force != request.options.end() && !ParseBool(force->second, options.force))
        return Fail(MediaError::BadRequest, "force '" + force->second + "': expected true or false");

    SaveOutcome outcome;
    {
        ParkedEmulator parked(_context);
        reply.result = _manager->Save(reply.slot, options, &outcome);
    }
    if (reply.result.Ok())
    {
        reply.body["savedPath"] = outcome.savedPath;
        reply.body["retargeted"] = outcome.retargeted;
    }
    return reply;
}

MediaReply MediaControl::Export(const MediaRequest& request)
{
    MediaReply reply;
    reply.result = ResolveSelector(*_manager, request.selector, reply.slot, /*allowDetached*/ true);
    if (!reply.result.Ok())
        return reply;
    const std::string path = Trim(request.path);
    if (path.empty())
        return Fail(MediaError::BadRequest, "export needs a path");

    BlockWriteOptions options;
    if (auto it = request.options.find("compression"); it != request.options.end())
        options.compression = Trim(it->second);
    if (auto it = request.options.find("parent"); it != request.options.end())
        options.parent = Trim(it->second);
    if (MediaReply bad = CompactOptions(request.options, options.compact, options.fs, options.size); !bad.result.Ok())
        return bad;

    ParkedEmulator parked(_context);
    reply.result = _manager->Export(reply.slot, path, options);
    if (reply.result.Ok())
        reply.body["exportedPath"] = path;
    return reply;
}

MediaReply MediaControl::Discard(const MediaRequest& request)
{
    MediaReply reply;
    reply.result = ResolveSelector(*_manager, request.selector, reply.slot, /*allowDetached*/ true);
    if (!reply.result.Ok())
        return reply;
    reply.result = _manager->Discard(reply.slot);
    Finish(reply, request.options);
    return reply;
}

MediaReply MediaControl::Rescan(const MediaRequest& request)
{
    MediaReply reply;
    reply.result = ResolveSelector(*_manager, request.selector, reply.slot);
    if (!reply.result.Ok())
        return reply;
    reply.result = _manager->Rescan(reply.slot);
    Finish(reply, request.options);
    return reply;
}

MediaReply MediaControl::Create(const MediaRequest& request)
{
    MediaReply reply;
    reply.result = ResolveSelector(*_manager, request.selector, reply.slot);
    if (!reply.result.Ok())
        return reply;
    const auto info = _manager->Info(reply.slot);
    const Options& o = request.options;

    MediaSource blank;
    blank.type = MediaSourceType::Blank;
    std::unique_ptr<Medium> medium;
    if (info->descriptor.kind == MediaKind::Floppy)
    {
        BlankFloppySpec spec;
        if (auto it = o.find("format"); it != o.end())
            spec.format = Trim(it->second);
        uint64_t number = 0;
        if (auto it = o.find("cylinders"); it != o.end())
        {
            if (!ParseUnsigned(it->second, number) || number > 255)
                return Fail(MediaError::BadRequest, "cylinders '" + it->second + "': expected 40 or 80");
            spec.cylinders = static_cast<uint8_t>(number);
        }
        if (auto it = o.find("sides"); it != o.end())
        {
            if (!ParseUnsigned(it->second, number) || number > 255)
                return Fail(MediaError::BadRequest, "sides '" + it->second + "': expected 1 or 2");
            spec.sides = static_cast<uint8_t>(number);
        }
        std::unique_ptr<DiskImage> disk;
        const bool plus3 = _context && _context->config.mem_model == MM_PLUS3;
        if (MediaResult built = FloppyFormats::CreateBlank(plus3, spec, disk); !built.Ok())
            return Fail(built.error, built.message);
        medium = std::make_unique<Medium>(blank, AccessMode::Session, spec.format, std::move(disk));
        reply.body["format"] = spec.format;
        reply.body["cylinders"] = static_cast<int>(spec.cylinders);
        reply.body["sides"] = static_cast<int>(spec.sides);
    }
    else if (info->descriptor.kind == MediaKind::Block)
    {
        // A blank card / disk lives in memory: keep it to what memory holds
        constexpr uint64_t kMaxBlankBytes = 2ull * 1024 * 1024 * 1024;
        uint64_t bytes = 0;
        auto it = o.find("size");
        if (it == o.end() || !ParseUnsigned(it->second, bytes) || bytes == 0 || bytes % 512 != 0 || bytes > kMaxBlankBytes)
            return Fail(MediaError::BadRequest, "create on a block slot needs size: bytes, a multiple of 512, up to 2 GiB");
        medium = MediaFormatRegistry::WrapBlock(blank, AccessMode::Session, "blank", std::make_unique<MemoryDisk>(bytes / 512));
        reply.body["size"] = bytes;
    }
    else
    {
        return Fail(MediaError::NotSupported, std::string("no blank ") + MediaKindName(info->descriptor.kind) + " media yet");
    }

    InsertOptions options;
    bool end = false;
    if (MediaResult r = Flag(o, "end_recording", end); !r.Ok())
        return Fail(r.error, r.message);
    options.endRecording = end;
    if (MediaResult r = ApplyDisposition(reply.slot, o, options.disposition); !r.Ok())
    {
        reply.result = r;
        return reply;
    }
    reply.result = _manager->Insert(reply.slot, std::move(medium), options);
    Finish(reply, o);
    return reply;
}

MediaReply MediaControl::Protect(const MediaRequest& request)
{
    MediaReply reply;
    reply.result = ResolveSelector(*_manager, request.selector, reply.slot);
    if (!reply.result.Ok())
        return reply;
    bool on = true;
    if (auto it = request.options.find("on"); it != request.options.end() && !ParseBool(it->second, on))
        return Fail(MediaError::BadRequest, "on '" + it->second + "': expected true or false");
    reply.result = _manager->SetWriteProtect(reply.slot, on);
    reply.body["writeProtect"] = on;
    return reply;
}

/// endregion </Verbs>
