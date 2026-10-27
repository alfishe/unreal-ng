#include "mcp-streamdeck.h"

#include <sstream>
#include <string>

#include "mcp-tool-utils.h"

namespace mcp
{

namespace
{
std::string Str(const Json::Value& value)
{
    if (value.isNull())
        return "-";
    if (value.isString())
        return value.asString().empty() ? "-" : value.asString();
    if (value.isBool())
        return value.asBool() ? "yes" : "no";
    if (value.isIntegral())
        return std::to_string(value.asInt64());
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, value);
}

/// The text of a reply (an agent may see only this)
std::string Summary(const std::string& verb, const Json::Value& reply)
{
    std::ostringstream out;
    const Json::Value& status = reply["status"];
    if (status.isObject())
    {
        if (verb != "status")
            out << "requested " << verb << "; ";
        out << "Stream Deck daemon " << Str(status["state"]) << " (mode " << Str(status["mode"]) << ", activity "
            << Str(status["activity"]) << "), pid " << Str(status["pid"]) << (status["own"].asBool() ? " (ours)" : "")
            << ", automation 127.0.0.1:" << Str(status["api_port"]) << ", Stream Decks on USB "
            << Str(status["deck_attached"]) << ", restarts " << Str(status["restarts"]);
        const Json::Value& daemon = status["daemon"];
        if (daemon.isObject())
        {
            out << "\nversion " << Str(daemon["version"]) << ", decks " << Str(daemon["decks"]) << ", viewers "
                << Str(daemon["viewers"]) << ", surface " << Str(daemon["surface"]);
            const Json::Value& pairing = daemon["pairing"];
            if (pairing.isObject())
                out << "\npairing " << Str(pairing["state"]) << ", paired " << Str(pairing["paired"]) << ", companion "
                    << Str(pairing["companion_id"]);
        }
        for (const Json::Value& deck : status["decks"])
        {
            out << "\ndeck " << Str(deck["serial"]) << " " << Str(deck["model"]) << " brightness "
                << Str(deck["brightness"]);
            if (deck.isMember("pack"))
                out << " pack " << Str(deck["pack"]) << " (setting " << Str(deck["pack_setting"]) << ")";
            out << (deck["real"].asBool() ? " (USB)" : " (own)");
        }
        if (status["error"].isString() && !status["error"].asString().empty())
            out << "\nerror: " << status["error"].asString();
        return out.str();
    }
    if (verb == "settings")
    {
        const Json::Value& s = reply["settings"];
        out << "mode " << Str(s["mode"]) << ", activity " << Str(s["activity"]) << ", api_port " << Str(s["api_port"])
            << ", viewers_port " << Str(s["viewers_port"]) << ", binary " << Str(s["binary"]);
        return out.str();
    }
    if (verb == "pairing" || verb == "pair" || verb == "pair_cancel")
    {
        const Json::Value& p = reply["pairing"];
        out << "pairing " << Str(p["state"]);
        if (p["state"].asString() == "pending")
            out << ": code " << Str(p["code"]) << " - the user approves it in unreal-qt's pairing prompt";
        if (p["error"].isString())
            out << " (" << Str(p["error"]) << ")";
        out << "; paired " << Str(p["paired"]) << ", companion " << Str(p["companion_id"]);
        return out.str();
    }
    if (verb == "revoke")
        return "companion " + Str(reply["id"]) + " revoked: the deck offers to pair again";
    if (verb == "brightness")
        return "deck " + Str(reply["deck"]["serial"]) + " brightness " + Str(reply["deck"]["brightness"]);
    if (verb == "packs")
    {
        out << "icon packs (default " << Str(reply["default"]) << "):";
        for (const Json::Value& pack : reply["packs"])
            out << "\n" << Str(pack["name"]) << " - " << Str(pack["title"]) << " (" << Str(pack["style"]) << ")";
        return out.str();
    }
    if (verb == "pack")
    {
        const Json::Value& d = reply["deck"];
        out << "deck " << Str(d["serial"]) << " draws the icon pack " << Str(d["pack"]) << " (setting "
            << Str(d["setting"]) << ", its scenario's " << Str(d["scenario_pack"]) << ")";
        return out.str();
    }
    if (verb == "udev")
        return Str(reply["rule"]);
    return Str(reply);
}
} // namespace

const std::vector<std::string>& StreamDeckToolVerbs()
{
    static const std::vector<std::string> verbs = {"status",      "start",  "stop",       "restart", "settings",
                                                   "pairing",     "pair",   "pair_cancel", "revoke",  "brightness",
                                                   "packs",       "pack",   "udev"};
    return verbs;
}

void RegisterStreamDeck(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["action"]["type"] = "string";
    for (const std::string& verb : StreamDeckToolVerbs())
        schema["properties"]["action"]["enum"].append(verb);
    schema["properties"]["action"]["description"] =
        "status (default); start / stop / restart; settings (read, or change with mode / activity / api_port / "
        "viewers_port / binary); pairing (state), pair (ask), pair_cancel; revoke (the daemon's companion); "
        "brightness (deck, value); packs (the icon packs); pack (deck, pack: a deck's icon pack); udev (the Linux "
        "rule)";
    schema["properties"]["mode"]["type"] = "string";
    schema["properties"]["mode"]["enum"].append("off");
    schema["properties"]["mode"]["enum"].append("deck");
    schema["properties"]["mode"]["enum"].append("always");
    schema["properties"]["mode"]["description"] = "settings: when unreal-qt starts the daemon (deck: when one is attached)";
    schema["properties"]["activity"]["type"] = "string";
    schema["properties"]["activity"]["enum"].append("off");
    schema["properties"]["activity"]["enum"].append("flash");
    schema["properties"]["activity"]["enum"].append("line");
    schema["properties"]["activity"]["description"] = "settings: how a deck shows what other surfaces do";
    schema["properties"]["api_port"]["type"] = "integer";
    schema["properties"]["viewers_port"]["type"] = "integer";
    schema["properties"]["binary"]["type"] = "string";
    schema["properties"]["binary"]["description"] = "settings: the unreal-streamdeck executable (\"\" = beside unreal-qt)";
    schema["properties"]["deck"]["type"] = "string";
    schema["properties"]["deck"]["description"] = "brightness, pack: the deck's serial (default first)";
    schema["properties"]["value"]["type"] = "integer";
    schema["properties"]["value"]["description"] = "brightness: 0-100 (omit to read)";
    schema["properties"]["pack"]["type"] = "string";
    schema["properties"]["pack"]["description"] =
        "pack: an icon pack name of action packs, or scenario (the deck's scenario's pack again); omit to read";
    schema["properties"]["id"]["type"] = "string";
    schema["properties"]["id"]["description"] = "revoke: a companion id (default the daemon's own)";

    registry.Register(
        "streamdeck",
        "The Stream Deck daemon (unreal-streamdeck), a companion that owns the attached Stream Deck: unreal-qt starts "
        "it when a deck is attached, restarts it after a crash and stops it on quit. Read its status (decks, viewers, "
        "pairing), start / stop / restart it, change its settings, set a deck's brightness or icon pack, ask it to "
        "pair (the user approves at the GUI: an agent cannot), revoke its companion only when the user asks.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const std::string verb = args.isMember("action") ? args["action"].asString() : std::string("status");
            std::string method = "GET";
            std::string path = "/api/v1/streamdeck/" + verb;
            Json::Value body(Json::objectValue);
            if (verb == "start" || verb == "stop" || verb == "restart" || verb == "pair" || verb == "revoke")
                method = "POST";
            if (verb == "pair_cancel")
            {
                method = "DELETE";
                path = "/api/v1/streamdeck/pair";
            }
            if (verb == "pairing")
                path = "/api/v1/streamdeck/pair";
            if (verb == "revoke" && args.isMember("id"))
                body["id"] = args["id"];
            if (verb == "settings")
                for (const char* key : {"mode", "activity", "api_port", "viewers_port", "binary"})
                    if (args.isMember(key))
                    {
                        body[key] = args[key];
                        method = "PUT";
                    }
            if (verb == "packs")
                path = "/api/v1/streamdeck/packs";
            if (verb == "pack")
            {
                const std::string deck = args.isMember("deck") ? args["deck"].asString() : std::string("first");
                path = "/api/v1/streamdeck/decks/" + UrlEncodeSegment(deck) + "/pack";
                if (args.isMember("pack"))
                {
                    method = "PUT";
                    body["pack"] = args["pack"];
                }
            }
            if (verb == "brightness")
            {
                const std::string deck = args.isMember("deck") ? args["deck"].asString() : std::string("first");
                path = "/api/v1/streamdeck/decks/" + UrlEncodeSegment(deck) + "/brightness";
                if (args.isMember("value"))
                {
                    method = "PUT";
                    body["value"] = args["value"];
                }
            }
            bool known = false;
            for (const std::string& v : StreamDeckToolVerbs())
                known = known || v == verb;
            if (!known)
            {
                done(ToolResult::Error("unknown action '" + verb +
                                       "' (status, start, stop, restart, settings, pairing, pair, pair_cancel, revoke, "
                                       "brightness, packs, pack, udev)"));
                return;
            }
            const bool sendBody = method == "POST" || method == "PUT";
            caller.Call(method, path, sendBody ? &body : nullptr, [done, verb](int status, Json::Value reply) {
                if (status != 200)
                {
                    done(ToolResult::Error("HTTP " + std::to_string(status) + ": " + DescribeErrorBody(reply)));
                    return;
                }
                // The text first: arguments have no evaluation order, gcc moves the reply out before Summary reads it
                std::string text = Summary(verb, reply);
                done(ToolResult::Ok(text, std::move(reply)));
            });
        });
}

} // namespace mcp
