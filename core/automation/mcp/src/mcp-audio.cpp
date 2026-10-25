#include "mcp-audio.h"

#include <cstdio>
#include <memory>
#include <sstream>
#include <string>

#include "mcp-tool-utils.h"
#include "target-resolver.h"

namespace mcp
{

namespace
{
std::string Compact(const Json::Value& value)
{
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, value);
}

std::string Num(const Json::Value& value, const char* format)
{
    if (!value.isNumeric())
        return "-";
    char text[32];
    std::snprintf(text, sizeof text, format, value.asDouble());
    return text;
}

std::string Flags(const Json::Value& row)
{
    std::string out;
    if (row["muted"].asBool())
        out += " muted";
    if (row["solo"].asBool())
        out += " solo";
    if (row.isMember("audible") && !row["audible"].asBool())
        out += " silent";
    if (row["active"].asBool())
        out += " active";
    return out;
}

/// One row or channel as a line: key, flags, volume / gain, peak
std::string Line(const Json::Value& row, const std::string& indent)
{
    std::ostringstream out;
    const std::string key = row.isMember("key") ? row["key"].asString() : row["source"].asString();
    out << indent << key;
    if (row.isMember("name"))
        out << " (" << row["name"].asString() << ")";
    out << ":" << Flags(row);
    if (row.isMember("volume"))
        out << " volume " << Num(row["volume"], "%.2f");
    if (row.isMember("gain_db"))
        out << " (" << Num(row["gain_db"], "%.1f") << " dB)";
    if (row.isMember("peak"))
        out << " peak " << Num(row["peak"], "%.3f");
    return out.str();
}

/// The text of a mixer report (an agent may see only this): rows, and the channels of `only` ("*" = all)
std::string MixerText(const Json::Value& mixer, const std::string& only)
{
    std::ostringstream out;
    const Json::Value& master = mixer["master"];
    out << "master: " << (master["muted"].asBool() ? "muted" : "on");
    if (master.isMember("sample_rate_hz"))
        out << ", " << master["sample_rate_hz"].asUInt() << " Hz";
    if (mixer.isMember("solo") && mixer["solo"].asBool())
        out << ", solo active";
    const std::string onlyRow = only.substr(0, only.find('.'));
    size_t withChannels = 0;
    for (const Json::Value& row : mixer["devices"])
    {
        const bool shown = onlyRow.empty() || onlyRow == "*" || row["source"].asString() == onlyRow;
        if (!shown)
            continue;
        out << "\n" << Line(row, "");
        if (row.isMember("channels"))
        {
            withChannels++;
            if (onlyRow.empty())
                out << " [" << row["channels"].size() << " channels]";
            else
                for (const Json::Value& channel : row["channels"])
                    out << "\n" << Line(channel, "  ");
        }
    }
    if (onlyRow.empty() && withChannels > 0)
        out << "\nchannels: action=get source=<row> (one row's), action=levels (all); set one with "
               "action=set source=<row>.<channel> (ay1.a)";
    return out.str();
}

std::string SettingsText(const Json::Value& reply)
{
    const Json::Value& audio = reply["settings"]["audio"];
    std::ostringstream out;
    out << "sound settings:";
    for (const std::string& name : audio.getMemberNames())
    {
        const Json::Value& v = audio[name];
        out << "\n" << name << " = " << (v.isString() ? v.asString() : v.isNull() ? "unavailable" : Compact(v));
    }
    out << "\nchange one: action=set_setting name=<name> value=<value>";
    return out.str();
}
} // namespace

const std::vector<std::string>& AudioMixerToolActions()
{
    static const std::vector<std::string> actions = {"get", "source", "set", "mute", "unmute", "levels", "settings",
                                                      "set_setting"};
    return actions;
}

void RegisterAudioMixer(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["action"]["type"] = "string";
    for (const std::string& action : AudioMixerToolActions())
        schema["properties"]["action"]["enum"].append(action);
    schema["properties"]["action"]["description"] =
        "get (default: the rows; with source: that row and its channels), source (one row or channel), set (mute / "
        "solo / volume / gain_db of a row or channel; source master takes muted only), mute / unmute (set muted of source, "
        "default master), levels (every row's and "
        "channel's levels and mute / solo: what the WebSocket topic audio.mixer pushes), settings (the sound settings), "
        "set_setting (name, value)";
    schema["properties"]["source"]["type"] = "string";
    schema["properties"]["source"]["description"] =
        "A row (master, beeper, ay1, ay2, ay3, fm1, fm2, covox, gs, gs_mp3, moonsound_fm, moonsound_pcm, cd0..cd3, "
        "ms_ssg1, ms_ssg2, ms_fm1, ms_fm2, ms_saa, ms_pcm, ms_midi, next_ay1..3, next_dac: those fitted) or a channel "
        "<row>.<channel> (ay1.a, covox.lb, gs.3, fm1.2, moonsound_fm.18, moonsound_pcm.24, ms_midi.10, next_dac.c)";
    schema["properties"]["muted"]["type"] = "boolean";
    schema["properties"]["solo"]["type"] = "boolean";
    schema["properties"]["solo"]["description"] =
        "A row: only soloed rows are heard; a channel: only the soloed channels of its row";
    schema["properties"]["volume"]["type"] = "number";
    schema["properties"]["volume"]["description"] = "0..1 (linear)";
    schema["properties"]["gain_db"]["type"] = "number";
    schema["properties"]["gain_db"]["description"] = "-120..0 dB (instead of volume)";
    schema["properties"]["name"]["type"] = "string";
    schema["properties"]["name"]["description"] =
        "set_setting: ay_stereo (abc|acb|mono), ay_chip (ay8910|ym2149), ay_voicing, ay_room, ay_punch, beeper_punch, "
        "fm_trim_db (-12..12, 0.5 steps), neogs_stereo (separated|gs|mono), covox_dc_removal (on|off), fir_filter "
        "(on|off: Sound HQ), audio_rate (44100..192000 or auto)";
    schema["properties"]["value"]["description"] = "set_setting: the value (text, bool or number)";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["description"] = "Emulator id, or 'auto'";

    registry.Register(
        "audio_mixer",
        "The sound mixer and the sound settings of a machine. Every source fitted is a row (beeper, the AYs, TurboSound "
        "FM, Covox / SounDrive, General Sound / NeoGS, MoonSound FM and PCM, CD audio, the ZX-MultiSound's parts, the "
        "Next's AYs and DAC) with mute, solo, volume and a level meter; rows have channels (<row>.<channel>: AY A/B/C, "
        "the SounDrive's four DACs, GS channels, the OPL4's 18 FM channels and 24 PCM slots, the YM2203's 3 FM "
        "channels, the 16 MIDI parts, the Next DAC's A..D) with mute, solo within the row and volume. Changes apply at "
        "the next frame start and never touch the machine's state. Also the sound settings: AY stereo / chip, voicing, "
        "room, punch, FM trim, NeoGS stereo, Covox DC removal, the FIR filter (Sound HQ), the core audio rate.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const std::string requested = args.isMember("action") ? args["action"].asString() : std::string("get");
            bool known = false;
            for (const std::string& a : AudioMixerToolActions())
                known = known || a == requested;
            if (!known)
            {
                done(ToolResult::Error("unknown action '" + requested +
                                       "' (get, source, set, mute, unmute, levels, settings, set_setting)"));
                return;
            }
            // mute / unmute: set muted, of the master unless a source is named
            const bool muteShortcut = requested == "mute" || requested == "unmute";
            const std::string action = muteShortcut ? std::string("set") : requested;
            std::string source = args.isMember("source") ? args["source"].asString() : std::string();
            if (muteShortcut && source.empty())
                source = "master";
            if ((action == "source" || action == "set") && source.empty())
            {
                done(ToolResult::Error(action + " takes source (a row such as ay1, or a channel such as ay1.a)"));
                return;
            }
            auto body = std::make_shared<Json::Value>(Json::objectValue);
            if (muteShortcut)
                (*body)["muted"] = requested == "mute";
            else if (action == "set")
                for (const char* key : {"muted", "solo", "volume", "gain_db"})
                    if (args.isMember(key))
                        (*body)[key] = args[key];
            if (action == "set_setting")
            {
                if (!args.isMember("name") || !args.isMember("value"))
                {
                    done(ToolResult::Error("set_setting takes name and value"));
                    return;
                }
                (*body)["value"] = args["value"];
            }
            const std::string name = args.isMember("name") ? args["name"].asString() : std::string();
            TargetResolver::ResolveFromArgs(args, caller, [action, source, name, body, &caller, done](bool ok, const std::string& idOrError) {
                if (!ok)
                {
                    done(ToolResult::Error(idOrError));
                    return;
                }
                const std::string& id = idOrError;
                std::string method = "GET";
                std::string path;
                if (action == "get")
                    path = Endpoint(id, "/audio/mixer");
                else if (action == "source")
                    path = Endpoint(id, "/audio/mixer/" + UrlEncodeSegment(source));
                else if (action == "set")
                {
                    method = "PUT";
                    path = Endpoint(id, "/audio/mixer/" + UrlEncodeSegment(source));
                }
                else if (action == "levels")
                    path = Endpoint(id, "/audio/levels");
                else if (action == "settings")
                    path = Endpoint(id, "/settings");
                else
                {
                    method = "PUT";
                    path = Endpoint(id, "/settings/" + UrlEncodeSegment(name));
                }
                caller.Call(method, path, method == "PUT" ? body.get() : nullptr,
                            [action, source, name, done](int status, Json::Value reply) {
                                if (status == 0)
                                {
                                    done(ToolResult::Error("WebAPI unreachable - is the emulator running with WebAPI "
                                                           "enabled (port 8090)?"));
                                    return;
                                }
                                if (status < 200 || status >= 300)
                                {
                                    done(ToolResult::Error("audio_mixer " + action + ": HTTP " + std::to_string(status) +
                                                           ": " + DescribeErrorBody(reply)));
                                    return;
                                }
                                std::string text;
                                if (action == "get" || action == "set")
                                    text = (action == "set" ? "set " + source + "\n" : std::string()) +
                                           MixerText(reply, source == "master" ? std::string() : source);
                                else if (action == "levels")
                                    text = MixerText(reply, "*");
                                else if (action == "source")
                                {
                                    text = Line(reply, "");
                                    for (const Json::Value& channel : reply["channels"])
                                        text += "\n" + Line(channel, "  ");
                                }
                                else if (action == "settings")
                                    text = SettingsText(reply);
                                else
                                    text = name + " = " +
                                           (reply["value"].isString() ? reply["value"].asString() : Compact(reply["value"])) +
                                           (reply.isMember("message") ? " (" + reply["message"].asString() + ")" : "");
                                done(ToolResult::Ok(text, std::move(reply)));
                            });
            });
        });
}

} // namespace mcp
