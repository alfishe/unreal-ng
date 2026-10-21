#pragma once

/// @file cli-audio-mixer.h
/// @brief CLI `mixer` (automation audit G13): the per-device mixer by source key, the core's
/// AudioMixer / DeviceState::AudioMixer (the same as /audio/mixer, Lua / Python audio_mixer).
/// Header-only so core-tests drive it without a CLI socket.
///
///   mixer                                               every device: muted, solo, volume, peak; its channels
///   mixer <source> [muted=0|1] [solo=0|1] [volume=0..1] [gain_db=-120..0]
///   mixer <source>.<channel> [muted=0|1] [solo=0|1] [volume=0..1] [gain_db=-120..0]
///
/// Worked example: `mixer covox muted=1` silences the Covox (the Sprinter's DAC) in the mix; `mixer ay1.b solo=1`
/// leaves only channel B of the first AY (solo within the row).

#include <cstdio>
#include <string>
#include <vector>

#include "emulator/sound/audiomixer.h"
#include "emulator/state/devicestate.h"

namespace CliAudioMixer
{
/// `only`: one row ("ay1", the row of "ay1.a") with its channels; "*": every row with its channels; "": the rows
inline std::string Table(const StateNode& mixer, const char* newline, const std::string& only = std::string())
{
    const StateNode* devices = mixer.find("devices");
    if (!devices)
    {
        const StateNode* d = mixer.find("description");
        return "Error: " + (d ? d->s : std::string("no mixer")) + newline;
    }
    std::string out;
    const StateNode* master = mixer.find("master");
    const StateNode* muted = master ? master->find("muted") : nullptr;
    out += std::string("master: ") + (muted && muted->b ? "muted" : "on") + newline;
    // What the speakers get: held while a run not paced to real time runs, with the holders by reason
    if (const StateNode* host = mixer.find("host_output"))
    {
        auto count = [](const StateNode* node, const char* key) {
            const StateNode* v = node ? node->find(key) : nullptr;
            return v ? static_cast<long long>(v->i) : 0LL;
        };
        const StateNode* held = host->find("held");
        const StateNode* holders = host->find("holders");
        char line[256];
        std::snprintf(line, sizeof line,
                      "host output: %s (holders: direct_run %lld, ttd_replay %lld, turbo %lld; stale cleared %lld) "
                      "frames delivered %lld, audible %lld, held %lld",
                      held && held->b ? "held" : "playing", count(holders, "direct_run"), count(holders, "ttd_replay"),
                      count(holders, "turbo"), count(host, "stale_holds_cleared"), count(host, "frames_delivered"),
                      count(host, "frames_audible"), count(host, "frames_held"));
        out += line;
        out += newline;
    }
    out += std::string("source         muted solo  volume  gain_dB  peak   active  name") + newline;
    const std::string onlyRow = only.substr(0, only.find('.'));
    bool anyChannels = false;
    for (const StateNode& d : devices->items)
    {
        auto b = [&](const char* key) { const StateNode* v = d.find(key); return v && v->b; };
        if (const StateNode* source = d.find("source"); !onlyRow.empty() && onlyRow != "*" && source && source->s != onlyRow)
            continue;
        anyChannels = anyChannels || d.find("channels") != nullptr;
        auto num = [&](const char* key) { const StateNode* v = d.find(key); return v ? (v->kind == StateNode::Kind::Double ? v->d : static_cast<double>(v->i)) : 0.0; };
        auto str = [&](const char* key) { const StateNode* v = d.find(key); return v ? v->s : std::string(); };
        char line[200];
        std::snprintf(line, sizeof line, "%-14s %-5s %-5s %6.2f  %7.1f  %5.3f  %-6s  %s", str("source").c_str(),
                      b("muted") ? "yes" : "no", b("solo") ? "yes" : "no", num("volume"), num("gain_db"), num("peak"),
                      b("active") ? "yes" : "no", str("name").c_str());
        out += line;
        out += newline;
        // The row's channels (<source>.<channel>): muted, solo within the row, volume; peak where metered
        if (const StateNode* channels = only.empty() ? nullptr : d.find("channels"))
        {
            for (const StateNode& c : channels->items)
            {
                auto cb = [&](const char* key) { const StateNode* v = c.find(key); return v && v->b; };
                auto cs = [&](const char* key) { const StateNode* v = c.find(key); return v ? v->s : std::string(); };
                const StateNode* volume = c.find("volume");
                const StateNode* gain = c.find("gain_db");
                const StateNode* peak = c.find("peak");
                char vol[16] = "     -";
                char db[16] = "      -";
                char pk[16] = "    -";
                if (volume)
                    std::snprintf(vol, sizeof vol, "%6.2f", volume->d);
                if (gain)
                    std::snprintf(db, sizeof db, "%7.1f", gain->kind == StateNode::Kind::Double ? gain->d : static_cast<double>(gain->i));
                if (peak)
                    std::snprintf(pk, sizeof pk, "%5.3f", peak->d);
                std::snprintf(line, sizeof line, "  %-12s %-5s %-5s %s  %s  %s  %-6s  %s", cs("key").c_str(),
                              cb("muted") ? "yes" : "no", cb("solo") ? "yes" : "no", vol, db, pk,
                              cb("audible") ? "" : "silent", cs("name").c_str());
                out += line;
                out += newline;
            }
        }
    }
    if (only.empty() && anyChannels)
        out += std::string("channels: mixer <source> (one row), mixer channels (all); set: mixer <source>.<channel> ...") +
               newline;
    return out;
}

/// args[0] = "mixer"
inline std::string Text(EmulatorContext* context, const std::vector<std::string>& args, const char* newline = "\n")
{
    if (args.size() <= 1)
        return Table(DeviceState::AudioMixer(context), newline);
    if (args[1] == "channels" && args.size() == 2)
        return Table(DeviceState::AudioMixer(context), newline, "*");
    std::string muted, solo, volume, gainDb;
    for (size_t i = 2; i < args.size(); i++)
    {
        const size_t eq = args[i].find('=');
        if (eq == std::string::npos)
            return "Error: options are key=value (muted, solo, volume, gain_db)" + std::string(newline);
        const std::string key = args[i].substr(0, eq);
        const std::string value = args[i].substr(eq + 1);
        if (key == "muted" || key == "mute")
            muted = value;
        else if (key == "solo")
            solo = value;
        else if (key == "volume")
            volume = value;
        else if (key == "gain_db")
            gainDb = value;
        else
            return "Error: unknown option '" + key + "' (muted, solo, volume, gain_db)" + newline;
    }
    AudioMixer::Change change;
    std::string error;
    if (!AudioMixer::ChangeFromStrings(muted, solo, volume, gainDb, change, error) ||
        !AudioMixer::Apply(context, args[1], change, error))
        return "Error: " + error + newline;
    return Table(DeviceState::AudioMixer(context), newline, args[1] == "master" ? std::string() : args[1]);
}
}  // namespace CliAudioMixer
