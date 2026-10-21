#include "soundcharactersettings.h"

#include <algorithm>
#include <cctype>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "base/featuremanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"

namespace
{
constexpr std::string_view kAYVoicing = "ay_voicing";
constexpr std::string_view kAYPunch = "ay_punch";
constexpr std::string_view kAYRoom = "ay_room";
constexpr std::string_view kBeeperPunch = "beeper_punch";
constexpr std::string_view kAYStereo = "ay_stereo";
constexpr std::string_view kAYChip = "ay_chip";
constexpr std::string_view kFmTrim = "fm_trim_db";
constexpr std::string_view kNeoGSStereo = "neogs_stereo";
constexpr std::string_view kCovoxDc = "covox_dc_removal";
constexpr std::string_view kFirFilter = "fir_filter";
constexpr const char* kUnavailable = "unavailable";

const char* StereoId(AYStereoMode mode)
{
    switch (mode)
    {
        case AYStereoMode::ACB:
            return "acb";
        case AYStereoMode::Mono:
            return "mono";
        default:
            return "abc";
    }
}

const char* NeoGSStereoId(NeoGSConfig::StereoMode mode)
{
    switch (mode)
    {
        case NeoGSConfig::StereoMode::GS:
            return "gs";
        case NeoGSConfig::StereoMode::Mono:
            return "mono";
        default:
            return "separated";
    }
}

FeatureManager* FeaturesOf(const SoundManager& sound)
{
    EmulatorContext* context = sound.getContext();
    return context ? context->pFeatureManager : nullptr;
}

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
         "AY / SSG tone voicing (fixed EQ after the chip): classic (default: trims the very low bass and the thump of volume changes), "
         "headphones (classic bass, soft highs), flat (hardware line out), "
         "warm, tv or small_speaker",
         false, false},
        {"ay_punch", "AY transient enhancement (Sound HQ only)", true, false},
        {"ay_room", "AY headphone crossfeed level (Sound HQ only)", false, false},
        {"beeper_punch", "Beeper attack enhancement (Sound HQ only)", true, false},
        {"ay_stereo", "AY socket stereo mixing: abc, acb or mono", false, false},
        {"ay_chip", "AY socket chip (its DAC curve): ay8910 (brighter) or ym2149 (warmer); a TurboSound FM is fixed",
         false, false},
        {"fm_trim_db", "YM2203 FM level over the measured board level, -12..+12 dB in 0.5 dB steps (TurboSound FM, "
                       "ZX-MultiSound)",
         false, true},
        {"neogs_stereo", "NeoGS listening choice: separated (as on the board), gs (the classic GS's 50% cross-feed) "
                         "or mono",
         false, false},
        {"covox_dc_removal", "Covox / SounDrive DC offset removal", true, false},
        {"fir_filter", "Sound HQ: the AY FIR decimation and the character chains (the soundhq feature)", true, false},
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
    if (key == kAYStereo)
        return StereoId(sound.getAYStereoMode());
    if (key == kAYChip)
    {
        const ITurboSoundDevice* ts = sound.getTurboSound();
        if (ts && ts->hasFm())
            return "ym2203";
        return sound.getAYChipModel() == AYChipModel::YM2149 ? "ym2149" : "ay8910";
    }
    if (key == kFmTrim)
    {
        double db = 0.0;
        if (!sound.fmTrimDb(db))
            return kUnavailable;
        char text[16];
        std::snprintf(text, sizeof text, "%.1f", db);
        return text;
    }
    if (key == kNeoGSStereo)
        return NeoGSStereoId(sound.neoGSStereoMode());
    if (key == kCovoxDc)
    {
        const Covox* covox = sound.getCovox();
        return covox ? (covox->isDCRemovalEnabled() ? "on" : "off") : kUnavailable;
    }
    if (key == kFirFilter)
    {
        const FeatureManager* features = FeaturesOf(sound);
        return features ? (features->isEnabled(Features::kSoundHQ) ? "on" : "off") : kUnavailable;
    }
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
    else if (key == kAYPunch || key == kBeeperPunch || key == kCovoxDc || key == kFirFilter)
    {
        values = {"on", "off"};
    }
    else if (key == kAYStereo)
    {
        values = {"abc", "acb", "mono"};
    }
    else if (key == kAYChip)
    {
        values = {"ay8910", "ym2149"};
    }
    else if (key == kFmTrim)
    {
        values = {"-12.0 .. 12.0 (0.5 dB steps)"};
    }
    else if (key == kNeoGSStereo)
    {
        values = {"separated", "gs", "mono"};
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

    const std::string text = ToLower(value);
    if (key == kAYStereo)
    {
        if (text != "abc" && text != "acb" && text != "mono")
            return reject();
        sound.setAYStereoMode(text == "acb" ? AYStereoMode::ACB : text == "mono" ? AYStereoMode::Mono : AYStereoMode::ABC);
        return true;
    }
    if (key == kAYChip)
    {
        AYChipModel model = AYChipModel::AY8910;
        if (text == "ay8910" || text == "ay-3-8910" || text == "ay-3-8912" || text == "ay")
            model = AYChipModel::AY8910;
        else if (text == "ym2149" || text == "ym")
            model = AYChipModel::YM2149;
        else
            return reject();
        if (!sound.setAYChipModel(model))
        {
            error = "ay_chip is fixed: the AY socket holds a TurboSound FM (2 x YM2203, the SSG halves are YM2149)";
            return false;
        }
        return true;
    }
    if (key == kFmTrim)
    {
        double current = 0.0;
        if (!sound.fmTrimDb(current))
        {
            error = "fm_trim_db is unavailable: the machine has no YM2203 FM (TurboSound FM, ZX-MultiSound)";
            return false;
        }
        char* end = nullptr;
        const std::string number(value);
        const double db = std::strtod(number.c_str(), &end);
        if (number.empty() || !end || *end != '\0' || !std::isfinite(db) || db < -12.0 || db > 12.0)
        {
            error = "Invalid fm_trim_db value '" + number + "'. Use -12 .. 12 (dB, 0.5 dB steps)";
            return false;
        }
        sound.setFmTrimDb(std::round(db * 2.0) / 2.0);
        return true;
    }
    if (key == kNeoGSStereo)
    {
        if (text == "separated" || text == "board")
            sound.setNeoGSStereoMode(NeoGSConfig::StereoMode::Separated);
        else if (text == "gs" || text == "legacy")
            sound.setNeoGSStereoMode(NeoGSConfig::StereoMode::GS);
        else if (text == "mono")
            sound.setNeoGSStereoMode(NeoGSConfig::StereoMode::Mono);
        else
            return reject();
        return true;
    }
    if (key == kCovoxDc || key == kFirFilter)
    {
        bool enabled = false;
        if (!ParseBool(value, enabled))
            return reject();
        if (key == kCovoxDc)
        {
            Covox* covox = sound.getCovox();
            if (!covox)
            {
                error = "covox_dc_removal is unavailable: the machine has no Covox / SounDrive";
                return false;
            }
            covox->setDCRemovalEnabled(enabled);
            return true;
        }
        FeatureManager* features = FeaturesOf(sound);
        if (!features || !features->setFeature(Features::kSoundHQ, enabled))
        {
            error = features ? features->refusalReason(Features::kSoundHQ, enabled) : "FeatureManager not available";
            return false;
        }
        return true;
    }

    error = "Unknown sound setting: " + key;
    return false;
}
