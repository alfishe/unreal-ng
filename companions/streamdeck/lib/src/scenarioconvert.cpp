// v1 -> v2 scenario documents (scenarios.md §1, §2; migration.md §4): the v1 kinds and the REST steps that have a
// catalogue action become `action` steps; settings that belong to the daemon and the host go.
#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "sdb/format.h"
#include "sdb/scenario.h"

namespace sdb::scn
{

namespace
{

json Act(const std::string& id, json value = nullptr)
{
    json a{{"kind", "action"}, {"id", id}};
    if (!value.is_null() && !(value.is_object() && value.empty()))
        a["value"] = std::move(value);
    return a;
}

// A copy of `body` with only `keys` (renamed by `rename`), null when none is there
json Pick(const json& body, std::initializer_list<std::pair<const char*, const char*>> keys)
{
    json out = json::object();
    if (!body.is_object())
        return out;
    for (const auto& [from, to] : keys)
        if (body.contains(from))
            out[to] = body[from];
    return out;
}

// A webapi step with a catalogue action (POST, the instance-relative path or /api/v1/emulator/start); nullopt = keep
std::optional<json> FromWebapi(const json& step)
{
    const std::string method = step.value("method", "POST");
    std::string path = step.value("path", "");
    const json body = step.contains("body") ? step["body"] : json::object();
    if (method != "POST" || path.empty())
        return std::nullopt;
    if (path == "/api/v1/emulator/start")
    {
        json v = Pick(body, {{"model", "model"}, {"ram_size", "ram_kb"}, {"ram_kb", "ram_kb"}, {"replace", "replace"}});
        if (!v.contains("model") || v["model"] == "$model")
            return std::nullopt;  // the same machine again: mach.cold_reset is the action, but it keeps the instance
        return Act("mach.start", v);
    }
    if (path.rfind("/api/", 0) == 0)
        return std::nullopt;  // another API (scripting, video wall): REST
    struct Plain
    {
        const char* path;
        const char* id;
    };
    static const Plain kPlain[] = {
        {"/pause", "run.pause"},
        {"/resume", "run.resume"},
        {"/step", "run.step_in"},
        {"/stepover", "run.step_over"},
        {"/stepout", "run.step_out"},
        {"/run_to_interrupt", "run.wait_int"},
        {"/nmi", "mach.nmi"},
        {"/ttd/start", "ttd.start"},
        {"/ttd/stop", "ttd.stop"},
        {"/ttd/resume", "ttd.resume_live"},
        {"/ttd/step-back", "ttd.step_back_frame"},
        {"/ttd/step-forward", "ttd.step_forward_frame"},
        {"/ttd/invalidate", "ttd.invalidate"},
    };
    for (const auto& p : kPlain)
        if (path == p.path && (!body.is_object() || body.empty()))
            return Act(p.id);
    if (path == "/run_frames")
        return Act("run.frames", Pick(body, {{"count", "count"}}));
    if (path == "/run_tstates")
        return Act("run.tstates", Pick(body, {{"count", "count"}}));
    if (path == "/run_scanlines")
        return Act("run.n_scanlines", Pick(body, {{"count", "count"}}));
    if (path == "/steps")
        return Act("run.steps", Pick(body, {{"count", "count"}}));
    if (path == "/ttd/step-instruction")
    {
        const std::string dir = body.is_object() ? body.value("dir", "") : "";
        if (dir == "back")
            return Act("ttd.step_back_instruction");
        if (dir == "forward")
            return Act("ttd.step_forward_instruction");
        return std::nullopt;
    }
    if (path == "/ttd/reverse-step")
        return Act("ttd.reverse_step", Pick(body, {{"count", "count"}}));
    if (path == "/ttd/seek")
        return Act("ttd.seek", Pick(body, {{"frame", "frame"}, {"tinframe", "tinframe"}, {"bookmark", "bookmark"}}));
    if (path == "/snapshot/load" && body.is_object() && body.contains("path"))
        return Act("media.open", json{{"path", body["path"]}, {"kind", "snapshot"}});
    if (path == "/tape/load" && body.is_object() && body.contains("path"))
        return Act("media.open", json{{"path", body["path"]}, {"kind", "tape"}, {"autostart", false}});
    if (path.size() == 14 && path.rfind("/disk/", 0) == 0 && path.compare(7, 7, "/insert") == 0 && body.is_object() &&
        body.contains("path"))
    {
        json v{{"path", body["path"]}, {"kind", "disk"}, {"drive", std::string(1, path[6])}};
        v["autostart"] = body.value("autostart", false);
        return Act("media.open", v);
    }
    for (const char* op : {"play", "stop", "pause", "rewind", "eject"})
        if (path == std::string("/tape/") + op)
            return Act("media.tape", json{{"op", op}});
    if (path == "/basic/run" && body.is_object())
        return Act("basic.run", Pick(body, {{"command", "command"}}));
    return std::nullopt;
}

struct Converter
{
    int runToggles = 0, seeks = 0, mapped = 0, kept = 0;
    std::vector<std::string> keptPaths;

    json Step(const json& a)
    {
        if (!a.is_object())
            return a;
        const std::string kind = a.value("kind", "");
        if (kind == "run_toggle")
        {
            ++runToggles;
            return Act("run.pause_toggle");
        }
        if (kind == "ttd_seek")
        {
            ++seeks;
            json v{{"d", "$ticks"}, {"rate", "$rate"}};
            const bool secs = a.value("unit", "frames") == "seconds";
            if (secs)
                v["unit"] = "seconds";
            // v1 defaults: min 1 (frames), max = min
            const json lo = a.contains("min") ? a["min"] : json(secs ? 0.02 : 1.0);
            v["min"] = lo;
            v["max"] = a.contains("max") ? a["max"] : lo;
            return Act("ttd.seek_by", v);
        }
        if (kind == "webapi")
        {
            if (auto m = FromWebapi(a))
            {
                ++mapped;
                return *m;
            }
            ++kept;
            const std::string p = a.value("method", "POST") + " " + a.value("path", "");
            if (std::find(keptPaths.begin(), keptPaths.end(), p) == keptPaths.end())
                keptPaths.push_back(p);
            return a;
        }
        json out = a;
        if (kind == "multi" && a.contains("steps") && a["steps"].is_array())
        {
            out["steps"] = json::array();
            for (const auto& st : a["steps"])
                out["steps"].push_back(Step(st));
        }
        else if (kind == "first" && a.contains("try") && a["try"].is_array())
        {
            out["try"] = json::array();
            for (const auto& st : a["try"])
                out["try"].push_back(Step(st));
        }
        else if (kind == "dir" || kind == "toggle")
        {
            for (const char* f : kind == "dir" ? std::initializer_list<const char*>{"left", "right"}
                                               : std::initializer_list<const char*>{"on", "off"})
                if (a.contains(f))
                    out[f] = Step(a[f]);
        }
        return out;
    }

    void Node(json& n)
    {
        if (!n.is_object())
            return;
        if (n.contains("do"))
            n["do"] = Step(n["do"]);
        if (n.contains("dials") && n["dials"].is_array())
            for (auto& d : n["dials"])
                for (const char* f : {"turn", "press"})
                    if (d.is_object() && d.contains(f))
                        d[f] = Step(d[f]);
        if (n.contains("items") && n["items"].is_array())
            for (auto& c : n["items"])
                Node(c);
    }
};

}  // namespace

json ConvertToV2(const json& doc, std::vector<std::string>* notes)
{
    if (!doc.is_object() || doc.value("version", 1) >= 2)
        return doc;
    json out = doc;
    out["format"] = "streamdeck-scenario";
    out["version"] = 2;
    auto note = [&](const std::string& n) {
        if (notes)
            notes->push_back(n);
    };
    if (out.contains("settings") && out["settings"].is_object())
    {
        json& st = out["settings"];
        if (st.contains("webapi"))
        {
            if (st["webapi"] != "http://127.0.0.1:8090")
                note("settings.webapi " + st["webapi"].dump() +
                     " dropped: the host is the daemon's (unreal-streamdeck run --host)");
            st.erase("webapi");
        }
        if (st.contains("emulator"))
        {
            if (st["emulator"] != "auto")
                note("settings.emulator " + st["emulator"].dump() +
                     " dropped: the target is the host's (follow the focused view, or pin one)");
            st.erase("emulator");
        }
        if (st.contains("companion_file"))
        {
            note("settings.companion_file dropped: the token is the daemon's");
            st.erase("companion_file");
        }
    }
    Converter c;
    if (out.contains("root"))
        c.Node(out["root"]);
    if (c.runToggles)
        note(sdb::Format("{} run_toggle -> action run.pause_toggle", c.runToggles));
    if (c.seeks)
        note(sdb::Format("{} ttd_seek -> action ttd.seek_by", c.seeks));
    if (c.mapped)
        note(sdb::Format("{} webapi steps -> catalogue actions", c.mapped));
    if (c.kept)
    {
        std::string list;
        for (size_t i = 0; i < c.keptPaths.size() && i < 8; ++i)
            list += (i ? ", " : "") + c.keptPaths[i];
        if (c.keptPaths.size() > 8)
            list += ", ...";
        note(sdb::Format("{} webapi steps kept (no action yet, the REST escape hatch): {}", c.kept, list));
    }
    return out;
}

}  // namespace sdb::scn
