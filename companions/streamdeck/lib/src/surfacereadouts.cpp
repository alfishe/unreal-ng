#include "sdb/surfacereadouts.h"

#include <algorithm>
#include <array>
#include <set>

namespace sdb::scn
{

namespace
{
constexpr std::array<std::string_view, 7> kBase = {"ttd.position", "run.state",  "machine",   "media.tape",
                                                   "media.disk",   "audio.mute", "debug.mode"};

// readouts.h's table, in its order (toggle.<id> and audio.source.<key> are prefixes)
constexpr std::array<std::string_view, 30> kKnown = {
    "ttd.position",    "run.state",         "machine",          "media.tape",       "media.disk",
    "audio.mute",      "debug.mode",        "audio.mixer",      "host.stats",       "host.clients",
    "emulator.stats",  "debug.breakpoints", "media.slots",      "recording.state",  "debug.labels",
    "ttd.status",      "debug.analyzers",   "controllers.state", "video.window",    "video.crt_params",
    "video.viewport",  "video.temporal",    "machine.previous", "machine.switches", "media.recent",     "media.rzx",
    "input.connectors", "cap.record",       "actions.unavailable", "debug.buffers"};

// The known name a path starts with, the longest ("machine.previous.model" -> machine.previous); "" = none
std::string_view KnownName(std::string_view path)
{
    std::string_view best;
    for (std::string_view name : kKnown)
        if (name.size() > best.size() && path.substr(0, name.size()) == name &&
            (path.size() == name.size() || path[name.size()] == '.'))
            best = name;
    return best;
}

void AddValue(const Face& f, std::set<std::string>& readouts, std::set<std::string>& paths)
{
    if (!f.value || f.value->readout.empty())
        return;
    paths.insert(f.value->readout);
    readouts.insert(SplitReadoutPath(f.value->readout).name);
}

bool Scrubs(const Dial& d)
{
    return d.turn &&
           (d.turn->kind == "ttd_seek" || (d.turn->kind == "action" && d.turn->spec.value("id", "") == "ttd.seek_by"));
}

void Collect(const Node& n, std::set<std::string>& readouts, std::set<std::string>& paths, bool& timeline)
{
    if (n.action.kind == "action")
    {
        // which of the catalogue's actions cannot run now: their keys grey out, a press says why
        paths.insert("actions.unavailable");
        readouts.insert("actions.unavailable");
    }
    if (!n.state.empty())
    {
        paths.insert(n.state);
        readouts.insert(SplitReadoutPath(n.state).name);
    }
    AddValue(n.face, readouts, paths);
    if (n.faceOn)
        AddValue(*n.faceOn, readouts, paths);
    if (n.IsMenu())
    {
        const std::string& status = n.strip.status;
        if (status == "ttd")
            timeline = true;
        else if (status == "run")
            readouts.insert("run.state");
        else if (status == "tape")
            readouts.insert("media.tape");
        else if (status == "target")
            readouts.insert("machine");
        if (std::any_of(n.dials.begin(), n.dials.end(), Scrubs))
            timeline = true;
    }
    for (const auto& c : n.items)
        Collect(c, readouts, paths, timeline);
}
}  // namespace

std::span<const std::string_view> BaseReadouts()
{
    return kBase;
}

ReadoutRef SplitReadoutPath(std::string_view path)
{
    ReadoutRef r;
    if (path.substr(0, 7) == "toggle.")
    {
        r.name = std::string(path);
        if (r.name.size() > 10 && r.name.compare(r.name.size() - 3, 3, ".on") == 0)
        {
            r.name.resize(r.name.size() - 3);
            r.field = "on";
        }
        return r;
    }
    if (const std::string_view base = KnownName(path); !base.empty())
    {
        r.name = std::string(base);
        if (path.size() > base.size())
            r.field = std::string(path.substr(base.size() + 1));
        return r;
    }
    if (path.substr(0, 13) == "audio.source.")
    {
        // audio.source.<key>: the key is the readout's last part (audio.source.ay1, audio.source.ay1.a)
        r.name = std::string(path);
        return r;
    }
    const size_t first = path.find('.');
    const size_t second = first == std::string_view::npos ? first : path.find('.', first + 1);
    r.name = std::string(path.substr(0, second));
    if (second != std::string_view::npos)
        r.field = std::string(path.substr(second + 1));
    return r;
}

json ReadoutField(const ReadoutRef& ref, const json& value)
{
    if (ref.field.empty())
        return value;
    if (!value.is_object())
        return nullptr;
    if (ref.name == "run.state" && (ref.field == "paused" || ref.field == "running" || ref.field == "stopped"))
        return value.value("state", "") == ref.field;
    return FieldOf(value, ref.field);
}

json FieldOf(const json& value, std::string_view field)
{
    const json* at = &value;
    while (!field.empty())
    {
        const size_t dot = field.find('.');
        const std::string part(field.substr(0, dot));
        field = dot == std::string_view::npos ? std::string_view() : field.substr(dot + 1);
        if (at->is_object())
        {
            auto it = at->find(part);
            if (it == at->end())
                return nullptr;
            at = &*it;
        }
        else if (at->is_array() && !part.empty() && part.find_first_not_of("0123456789") == std::string::npos &&
                 part.size() < 9 && std::stoul(part) < at->size())
            at = &(*at)[std::stoul(part)];
        else
            return nullptr;
    }
    return *at;
}

std::span<const std::string_view> KnownReadouts()
{
    return kKnown;
}

bool IsKnownReadoutPath(std::string_view path)
{
    if (!IsReadoutPath(path))
        return false;
    if (path.substr(0, 7) == "toggle." || (path.substr(0, 13) == "audio.source." && path.size() > 13))
        return true;
    return !KnownName(path).empty();
}

SurfaceNeeds NeedsOf(const Scenario& s)
{
    std::set<std::string> readouts, paths;
    SurfaceNeeds needs;
    Collect(s.root, readouts, paths, needs.timeline);
    if (needs.timeline)
        readouts.insert("ttd.position");
    needs.readouts.assign(readouts.begin(), readouts.end());
    needs.paths.assign(paths.begin(), paths.end());
    needs.target = s.settings.target;
    return needs;
}

}  // namespace sdb::scn
