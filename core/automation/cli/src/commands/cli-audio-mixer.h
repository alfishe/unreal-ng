#pragma once

/// @file cli-audio-mixer.h
/// @brief CLI `mixer` (automation audit G13): the per-device mixer by source key, the core's
/// AudioMixer / DeviceState::AudioMixer (the same as /audio/mixer, Lua / Python audio_mixer).
/// Header-only so core-tests drive it without a CLI socket.
///
///   mixer                                               every device: muted, solo, volume, peak
///   mixer <source> [muted=0|1] [solo=0|1] [volume=0..1] [gain_db=-120..0]
///
/// Worked example: `mixer covox muted=1` silences the Covox (the Sprinter's DAC) in the mix.

#include <cstdio>
#include <string>
#include <vector>

#include "emulator/sound/audiomixer.h"
#include "emulator/state/devicestate.h"

namespace CliAudioMixer
{
inline std::string Table(const StateNode& mixer, const char* newline)
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
    out += std::string("source         muted solo  volume  gain_dB  peak   active  name") + newline;
    for (const StateNode& d : devices->items)
    {
        auto b = [&](const char* key) { const StateNode* v = d.find(key); return v && v->b; };
        auto num = [&](const char* key) { const StateNode* v = d.find(key); return v ? (v->kind == StateNode::Kind::Double ? v->d : static_cast<double>(v->i)) : 0.0; };
        auto str = [&](const char* key) { const StateNode* v = d.find(key); return v ? v->s : std::string(); };
        char line[200];
        std::snprintf(line, sizeof line, "%-14s %-5s %-5s %6.2f  %7.1f  %5.3f  %-6s  %s", str("source").c_str(),
                      b("muted") ? "yes" : "no", b("solo") ? "yes" : "no", num("volume"), num("gain_db"), num("peak"),
                      b("active") ? "yes" : "no", str("name").c_str());
        out += line;
        out += newline;
    }
    return out;
}

/// args[0] = "mixer"
inline std::string Text(EmulatorContext* context, const std::vector<std::string>& args, const char* newline = "\n")
{
    if (args.size() <= 1)
        return Table(DeviceState::AudioMixer(context), newline);
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
    return Table(DeviceState::AudioMixer(context), newline);
}
}  // namespace CliAudioMixer
