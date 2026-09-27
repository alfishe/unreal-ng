#include "soundcharactersettings.h"

#include <algorithm>
#include <cctype>

#include "emulator/sound/soundmanager.h"

namespace
{
constexpr std::string_view kAYVoicing = "ay_voicing";
constexpr std::string_view kAYPunch = "ay_punch";
constexpr std::string_view kAYRoom = "ay_room";
constexpr std::string_view kBeeperPunch = "beeper_punch";

std::string ToLower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool ParseBool(std::string_view text, bool& out)
{
    const std::string value = ToLower(text);
    if (value == "on" || value == "true" || value == "1")
    {
        out = true;
        return true;
    }
    if (value == "off" || value == "false" || value == "0")
    {
        out = false;
        return true;
    }
    return false;
}

std::string Join(const std::vector<std::string>& values)
{
    std::string out;
    for (const std::string& v : values)
    {
        if (!out.empty())
            out += ", ";
        out += v;
    }
    return out;
}
}  // namespace

const std::vector<SoundCharacterSettings::Descriptor>& SoundCharacterSettings::Descriptors()
{
    static const std::vector<Descriptor> kDescriptors = {
        {"ay_voicing",
         "AY / SSG tone voicing (fixed EQ after the chip): headphones (default: classic bass, soft highs), "
         "classic (trims the very low bass and the thump of volume changes), flat (hardware line out), "
         "warm, tv or small_speaker",
         false},
        {"ay_punch", "AY transient enhancement (Sound HQ only)", true},
        {"ay_room", "AY headphone crossfeed level (Sound HQ only)", false},
        {"beeper_punch", "Beeper attack enhancement (Sound HQ only)", true},
    };
    return kDescriptors;
}

const SoundCharacterSettings::Descriptor* SoundCharacterSettings::Find(std::string_view name)
{
    const std::string key = ToLower(name);
    for (const Descriptor& d : Descriptors())
    {
        if (key == d.name)
            return &d;
    }
    return nullptr;
}

std::string SoundCharacterSettings::Get(const SoundManager& sound, std::string_view name)
{
    const std::string key = ToLower(name);
    if (key == kAYVoicing)
        return FilterVoicing::presetId(sound.getAYVoicing());
    if (key == kAYPunch)
        return sound.getAYPunch() ? "on" : "off";
    if (key == kAYRoom)
        return AudioCharacterChain::roomModeId(sound.getAYRoomMode());
    if (key == kBeeperPunch)
        return sound.getBeeperPunch() ? "on" : "off";
    return std::string();
}

std::vector<std::string> SoundCharacterSettings::AllowedValues(std::string_view name)
{
    const std::string key = ToLower(name);
    std::vector<std::string> values;
    if (key == kAYVoicing)
    {
        FilterVoicing::forEachVisible([&](const FilterVoicing::Profile& p) { values.emplace_back(p.id); });
    }
    else if (key == kAYRoom)
    {
        for (int i = 0; i < static_cast<int>(AudioCharacterChain::RoomMode::COUNT); i++)
            values.emplace_back(AudioCharacterChain::roomModeId(static_cast<AudioCharacterChain::RoomMode>(i)));
    }
    else if (key == kAYPunch || key == kBeeperPunch)
    {
        values = {"on", "off"};
    }
    return values;
}

bool SoundCharacterSettings::Set(SoundManager& sound, std::string_view name, std::string_view value,
                                 std::string& error)
{
    const std::string key = ToLower(name);
    auto reject = [&]() {
        error = "Invalid " + key + " value '" + std::string(value) + "'. Use " + Join(AllowedValues(key));
        return false;
    };

    if (key == kAYVoicing)
    {
        FilterVoicing::Preset preset = FilterVoicing::Preset::Classic;
        if (!FilterVoicing::parsePreset(value, preset))
            return reject();
        sound.setAYVoicing(preset);
        return true;
    }
    if (key == kAYRoom)
    {
        AudioCharacterChain::RoomMode mode = AudioCharacterChain::RoomMode::Off;
        if (!AudioCharacterChain::parseRoomMode(value, mode))
            return reject();
        sound.setAYRoomMode(mode);
        return true;
    }
    if (key == kAYPunch || key == kBeeperPunch)
    {
        bool enabled = false;
        if (!ParseBool(value, enabled))
            return reject();
        if (key == kAYPunch)
            sound.setAYPunch(enabled);
        else
            sound.setBeeperPunch(enabled);
        return true;
    }

    error = "Unknown sound setting: " + key;
    return false;
}
