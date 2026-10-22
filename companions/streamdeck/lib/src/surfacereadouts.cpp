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

bool Scrubs(const Dial& d)
{
    return d.turn &&
           (d.turn->kind == "ttd_seek" || (d.turn->kind == "action" && d.turn->spec.value("id", "") == "ttd.seek_by"));
}

void Collect(const Node& n, std::set<std::string>& readouts, std::set<std::string>& paths, bool& timeline)
{
    if (!n.state.empty())
    {
        paths.insert(n.state);
        readouts.insert(SplitReadoutPath(n.state).name);
    }
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
    for (std::string_view base : kBase)
        if (path.substr(0, base.size()) == base && (path.size() == base.size() || path[base.size()] == '.'))
        {
            r.name = std::string(base);
            if (path.size() > base.size())
                r.field = std::string(path.substr(base.size() + 1));
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
    if (auto it = value.find(ref.field); it != value.end())
        return *it;
    return nullptr;
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
