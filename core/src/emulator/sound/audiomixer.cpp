#include "stdafx.h"

#include "audiomixer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include "common/stringhelper.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/audiocapture/audiocaptureanalyzer.h"
#include "debugger/debugmanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/devicestate.h"

namespace AudioMixer
{
namespace
{
struct KeyName
{
    AudioSourceType type;
    const char* key;
};
const KeyName kKeys[] = {
    {AudioSourceType::MasterMix, "master"},       {AudioSourceType::Beeper, "beeper"},
    {AudioSourceType::AY1_All, "ay1"},            {AudioSourceType::AY2_All, "ay2"},
    {AudioSourceType::AY3_All, "ay3"},            {AudioSourceType::FM1, "fm1"},
    {AudioSourceType::FM2, "fm2"},                {AudioSourceType::COVOX, "covox"},
    {AudioSourceType::GeneralSound, "gs"},        {AudioSourceType::GeneralSoundMp3, "gs_mp3"},
    {AudioSourceType::Moonsound_FM, "moonsound_fm"}, {AudioSourceType::Moonsound_PCM, "moonsound_pcm"},
    // The ATAPI CD drives by IDE unit (0 ide0.master .. 3 ide1.slave)
    {AudioSourceType::CdAudio0, "cd0"}, {AudioSourceType::CdAudio1, "cd1"},
    {AudioSourceType::CdAudio2, "cd2"}, {AudioSourceType::CdAudio3, "cd3"},
    // ZX-MultiSound (a slot card)
    {AudioSourceType::MultiSoundSsg1, "ms_ssg1"}, {AudioSourceType::MultiSoundSsg2, "ms_ssg2"},
    {AudioSourceType::MultiSoundFm1, "ms_fm1"},   {AudioSourceType::MultiSoundFm2, "ms_fm2"},
    {AudioSourceType::MultiSoundSaa, "ms_saa"},   {AudioSourceType::MultiSoundPcm, "ms_pcm"},
    {AudioSourceType::MultiSoundMidi, "ms_midi"},
    // The ZX Spectrum Next (NextAudio's parts)
    {AudioSourceType::NextAy1, "next_ay1"},       {AudioSourceType::NextAy2, "next_ay2"},
    {AudioSourceType::NextAy3, "next_ay3"},       {AudioSourceType::NextDac, "next_dac"},
    // Wild Sound (a Sprinter ISA card)
    {AudioSourceType::WildSoundXm, "ws_xm"}, {AudioSourceType::WildSoundAy1, "ws_ay1"},
    {AudioSourceType::WildSoundAy2, "ws_ay2"},
};

std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool Flag(const std::string& text, const char* name, int& out, std::string& error)
{
    const std::string v = Lower(text);
    if (v.empty())
        out = -1;
    else if (v == "1" || v == "on" || v == "true" || v == "yes")
        out = 1;
    else if (v == "0" || v == "off" || v == "false" || v == "no")
        out = 0;
    else
    {
        error = std::string(name) + " must be 0 / 1 (on / off, true / false)";
        return false;
    }
    return true;
}

bool Number(const std::string& text, double& out)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return end && *end == '\0' && std::isfinite(out);
}

std::string KnownKeys(SoundManager* sm)
{
    std::string keys = "master";
    if (sm)
        for (const AudioDeviceInfo& d : sm->devices())
            keys += std::string(", ") + Key(d.type);
    return keys;
}

double Round3(double v)
{
    return std::round(1000.0 * v) / 1000.0;
}

double GainDb(float volume)
{
    return volume > 0.0f ? std::round(200.0 * std::log10(static_cast<double>(volume))) / 10.0 : -120.0;
}

/// A row's channels as the report shows them (`full`: name, gain_db too)
StateNode ChannelsNode(const std::vector<SoundManager::MixerChannelState>& channels, const char* rowKey,
                       bool rowAudible, bool full)
{
    StateNode list = StateNode::Array();
    const bool solo = std::any_of(channels.begin(), channels.end(),
                                  [](const SoundManager::MixerChannelState& c) { return c.solo; });
    for (const SoundManager::MixerChannelState& c : channels)
    {
        StateNode n = StateNode::Object();
        n["key"] = std::string(rowKey) + "." + c.desc.key;
        if (full)
        {
            n["channel"] = c.desc.key;
            n["name"] = c.desc.name;
        }
        n["muted"] = c.mute;
        n["solo"] = c.solo;
        n["audible"] = rowAudible && (solo ? c.solo : !c.mute);
        if (c.desc.hasVolume)
        {
            n["volume"] = static_cast<double>(c.volume);
            if (full)
                n["gain_db"] = GainDb(c.volume);
        }
        if (c.desc.hasMeter)
            n["peak"] = Round3(static_cast<double>(c.peak));
        list.push(n);
    }
    return list;
}

bool AnySolo(SoundManager* sm)
{
    for (const AudioDeviceInfo& d : sm->devices())
        if (d.solo)
            return true;
    return false;
}
}  // namespace

void SplitKey(const std::string& key, std::string& row, std::string& channel)
{
    const size_t dot = key.find('.');
    row = dot == std::string::npos ? key : key.substr(0, dot);
    channel = dot == std::string::npos ? std::string() : key.substr(dot + 1);
}

const char* Key(AudioSourceType type)
{
    for (const KeyName& k : kKeys)
        if (k.type == type)
            return k.key;
    return "custom";
}

bool FromKey(const std::string& key, AudioSourceType& type)
{
    const std::string wanted = Lower(key);
    for (const KeyName& k : kKeys)
    {
        if (wanted == k.key)
        {
            type = k.type;
            return true;
        }
    }
    return false;
}

bool ChangeFromStrings(const std::string& muted, const std::string& solo, const std::string& volume,
                       const std::string& gainDb, Change& change, std::string& error)
{
    change = Change();
    if (!Flag(muted, "muted", change.muted, error) || !Flag(solo, "solo", change.solo, error))
        return false;
    if (!volume.empty() && (!Number(volume, change.volume) || change.volume < 0.0 || change.volume > 1.0))
    {
        error = "volume must be 0..1";
        return false;
    }
    if (!gainDb.empty())
    {
        if (!Number(gainDb, change.gainDb) || change.gainDb > 0.0 || change.gainDb < -120.0)
        {
            error = "gain_db must be -120..0";
            return false;
        }
        change.hasGainDb = true;
    }
    return true;
}

bool Apply(EmulatorContext* context, const std::string& key, const Change& change, std::string& error)
{
    SoundManager* sm = context ? context->pSoundManager : nullptr;
    AudioSourceType type = AudioSourceType::Custom;
    std::string rowKey, channel;
    SplitKey(key, rowKey, channel);
    if (!sm || !FromKey(rowKey, type))
    {
        error = "unknown source '" + key + "' (" + KnownKeys(sm) + "; a channel: <source>.<channel>, e.g. ay1.a)";
        return false;
    }
    if (!channel.empty())
    {
        if (type == AudioSourceType::MasterMix || !sm->device(type))
        {
            error = "source '" + rowKey + "' is not fitted on this machine (" + KnownKeys(sm) + ")";
            return false;
        }
        float volume = -1.0f;
        if (change.hasGainDb)
            volume = static_cast<float>(std::pow(10.0, change.gainDb / 20.0));
        else if (change.volume >= 0.0)
            volume = static_cast<float>(change.volume);
        if (!sm->setMixerChannel(type, channel, change.muted, change.solo, volume, error))
        {
            error = rowKey + "." + channel + ": " + error;
            return false;
        }
        return true;
    }
    if (type == AudioSourceType::MasterMix)
    {
        if (change.solo >= 0 || change.volume >= 0.0 || change.hasGainDb)
        {
            error = "master takes muted only";
            return false;
        }
        if (change.muted == 1)
            sm->mute();
        else if (change.muted == 0)
            sm->unmute();
        return true;
    }
    if (!sm->device(type))
    {
        error = "source '" + key + "' is not fitted on this machine (" + KnownKeys(sm) + ")";
        return false;
    }
    if (change.muted >= 0)
        sm->setDeviceMute(type, change.muted != 0);
    if (change.solo >= 0)
        sm->setDeviceSolo(type, change.solo != 0);
    if (change.hasGainDb)
        sm->setDeviceVolume(type, static_cast<float>(std::pow(10.0, change.gainDb / 20.0)));
    else if (change.volume >= 0.0)
        sm->setDeviceVolume(type, static_cast<float>(change.volume));
    return true;
}

bool Capturable(EmulatorContext* context, const std::string& key, AudioSourceType& type, std::string& error)
{
    SoundManager* sm = context ? context->pSoundManager : nullptr;
    if (key.empty())
    {
        type = AudioSourceType::MasterMix;
        return true;
    }
    if (!sm || !FromKey(key, type))
    {
        error = "unknown source '" + key + "' (" + KnownKeys(sm) + ")";
        return false;
    }
    if (type != AudioSourceType::MasterMix && (!sm->device(type) || !sm->hasDeviceBuffer(type)))
    {
        error = "source '" + key + "' is not fitted on this machine (" + KnownKeys(sm) + ")";
        return false;
    }
    return true;
}
bool Read(EmulatorContext* context, const std::string& key, StateNode& out, std::string& error)
{
    SoundManager* sm = context ? context->pSoundManager : nullptr;
    AudioSourceType type = AudioSourceType::Custom;
    std::string rowKey, channel;
    SplitKey(key, rowKey, channel);
    if (!sm || !FromKey(rowKey, type))
    {
        error = "unknown source '" + key + "' (" + KnownKeys(sm) + ")";
        return false;
    }
    out = StateNode::Object();
    if (type == AudioSourceType::MasterMix)
    {
        if (!channel.empty())
        {
            error = "master has no channels";
            return false;
        }
        out["source"] = "master";
        out["muted"] = sm->isMuted();
        return true;
    }
    const AudioDeviceInfo* d = sm->device(type);
    if (!d)
    {
        error = "source '" + rowKey + "' is not fitted on this machine (" + KnownKeys(sm) + ")";
        return false;
    }
    const bool rowAudible = AnySolo(sm) ? d->solo : !d->mute;
    const std::vector<SoundManager::MixerChannelState> channels = sm->mixerChannels(type);
    if (!channel.empty())
    {
        const StateNode list = ChannelsNode(channels, Key(type), rowAudible, true);
        for (const StateNode& n : list.items)
            if (const StateNode* k = n.find("key"); k && k->s == rowKey + "." + channel)
            {
                out = n;
                out["source"] = n.find("key")->s;
                return true;
            }
        error = "unknown channel '" + key + "'";
        return false;
    }
    out["source"] = Key(type);
    out["name"] = d->name;
    out["muted"] = d->mute;
    out["solo"] = d->solo;
    out["audible"] = rowAudible;
    out["volume"] = static_cast<double>(d->volume);
    out["gain_db"] = GainDb(d->volume);
    out["peak"] = Round3(static_cast<double>(d->peak));
    out["active"] = d->activeRecently;
    if (!channels.empty())
        out["channels"] = ChannelsNode(channels, Key(type), rowAudible, true);
    return true;
}

std::optional<bool> Flag(EmulatorContext* context, const std::string& key, const char* what)
{
    StateNode node;
    std::string error;
    if (!Read(context, key, node, error))
        return std::nullopt;
    const StateNode* v = node.find(what);
    if (!v || v->kind != StateNode::Kind::Bool)
        return std::nullopt;
    return v->b;
}

StateNode Levels(EmulatorContext* context)
{
    SoundManager* sm = context ? context->pSoundManager : nullptr;
    StateNode ret = StateNode::Object();
    if (!sm)
    {
        ret["available"] = false;
        return ret;
    }
    StateNode master = StateNode::Object();
    master["muted"] = sm->isMuted();
    ret["master"] = master;
    const bool anySolo = AnySolo(sm);
    ret["solo"] = anySolo;
    StateNode devices = StateNode::Array();
    for (const AudioDeviceInfo& d : sm->devices())
    {
        StateNode n = StateNode::Object();
        n["source"] = Key(d.type);
        n["muted"] = d.mute;
        n["solo"] = d.solo;
        const bool audible = anySolo ? d.solo : !d.mute;
        n["audible"] = audible;
        n["volume"] = Round3(static_cast<double>(d.volume));
        n["peak"] = Round3(static_cast<double>(d.peak));
        n["active"] = d.activeRecently;
        const std::vector<SoundManager::MixerChannelState> channels = sm->mixerChannels(d.type);
        if (!channels.empty())
            n["channels"] = ChannelsNode(channels, Key(d.type), audible, false);
        devices.push(n);
    }
    ret["devices"] = devices;
    return ret;
}
}  // namespace AudioMixer

namespace DeviceState
{
StateNode AudioMixer(EmulatorContext* context)
{
    SoundManager* sm = context ? context->pSoundManager : nullptr;
    if (!sm)
    {
        StateNode n = StateNode::Object();
        n["available"] = false;
        n["description"] = "Sound manager not available";
        return n;
    }
    StateNode ret = StateNode::Object();
    ret["available"] = true;
    StateNode master = StateNode::Object();
    master["source"] = "master";
    master["muted"] = sm->isMuted();
    master["sample_rate_hz"] = static_cast<uint64_t>(sm->getCoreRate());
    ret["master"] = master;
    // What the host audio output (the speakers) received: a run not paced to real time (run_frames and the
    // other direct runs, TTD seek / replay, turbo) holds it; frame counters are emulated frames since creation.
    // holders: the holds active now, by reason (all 0 while the machine plays); holds_taken: ever taken, by
    // reason; stale_holds_cleared: holds a resume found without their reason and dropped (a leak, logged)
    StateNode host = StateNode::Object();
    host["held"] = sm->isHostOutputHeld();
    StateNode holders = StateNode::Object();
    StateNode taken = StateNode::Object();
    for (size_t i = 0; i < SoundManager::kHostHoldReasons; i++)
    {
        const auto reason = static_cast<SoundManager::HostHoldReason>(i);
        holders[SoundManager::HostHoldReasonName(reason)] = static_cast<int64_t>(sm->hostOutputHolds(reason));
        taken[SoundManager::HostHoldReasonName(reason)] = sm->hostOutputHoldsTaken(reason);
    }
    host["holders"] = holders;
    host["holds_taken"] = taken;
    host["stale_holds_cleared"] = sm->hostOutputStaleHoldsCleared();
    host["frames_delivered"] = sm->hostFramesDelivered();
    host["frames_audible"] = sm->hostFramesAudible();
    host["frames_held"] = sm->hostFramesHeld();
    ret["host_output"] = host;
    StateNode devices = StateNode::Array();
    bool anySolo = false;
    for (const AudioDeviceInfo& d : sm->devices())
        anySolo = anySolo || d.solo;
    for (const AudioDeviceInfo& d : sm->devices())
    {
        StateNode n = StateNode::Object();
        n["source"] = ::AudioMixer::Key(d.type);
        n["name"] = d.name;
        n["muted"] = d.mute;
        n["solo"] = d.solo;
        n["audible"] = anySolo ? d.solo : !d.mute;
        n["volume"] = static_cast<double>(d.volume);
        n["gain_db"] = ::AudioMixer::GainDb(d.volume);
        n["peak"] = ::AudioMixer::Round3(static_cast<double>(d.peak));
        n["active"] = d.activeRecently;
        n["capturable"] = sm->hasDeviceBuffer(d.type);
        // The row's channels (<source>.<channel>: muted, solo within the row, volume; peak where the device meters)
        const std::vector<SoundManager::MixerChannelState> channels = sm->mixerChannels(d.type);
        if (!channels.empty())
            n["channels"] = ::AudioMixer::ChannelsNode(channels, ::AudioMixer::Key(d.type),
                                                       anySolo ? d.solo : !d.mute, true);
        // Silent by hardware: a slot card's IORQGE hides the device ("shadowed by zxbus.1")
        const std::string state = sm->deviceState(d.type);
        if (!state.empty())
            n["state"] = state;
        devices.push(n);
    }
    ret["devices"] = devices;
    if (context->pDebugManager && context->pDebugManager->GetAnalyzerManager())
        ret["capture_source"] = ::AudioMixer::Key(static_cast<AudioSourceType>(context->pDebugManager->GetAnalyzerManager()->audioTapSource()));
    ret["note"] = "solo: only soloed devices are heard; peak / active: the last frame; capture one device with "
                  "/audio/capture {source} (its own buffer, before mute and volume); a channel is <source>.<channel> "
                  "(muted, solo within its row, volume; applied at the next frame start)";
    return ret;
}

StateNode AudioChannels(EmulatorContext* context)
{
    SoundManager* sm = context ? context->pSoundManager : nullptr;
    StateNode ret = StateNode::Object();
    const StateNode mixer = AudioMixer(context);
    auto mixerDevice = [&](const char* key) -> const StateNode* {
        if (const StateNode* devices = mixer.find("devices"))
            for (const StateNode& d : devices->items)
                if (const StateNode* source = d.find("source"); source && source->s == key)
                    return &d;
        return nullptr;
    };

    StateNode beeper = StateNode::Object();
    const StateNode* beeperDevice = mixerDevice("beeper");
    beeper["available"] = beeperDevice != nullptr;
    if (beeperDevice)
    {
        beeper["peak"] = beeperDevice->find("peak")->d;
        beeper["active"] = beeperDevice->find("active")->b;
        beeper["muted"] = beeperDevice->find("muted")->b;
    }
    ret["beeper"] = beeper;

    StateNode ay = StateNode::Object();
    const bool hasAy = sm && sm->hasTurboSound();
    ay["available"] = hasAy;
    if (hasAy)
    {
        StateNode chips = StateNode::Array();
        static const char* const kNames[] = {"A", "B", "C"};
        for (int chipIdx = 0; chipIdx < sm->getAYChipCount(); chipIdx++)
        {
            SoundChip_AY8910* chip = sm->getAYChip(chipIdx);
            if (!chip)
                continue;
            StateNode channels = StateNode::Array();
            const auto* toneGens = chip->getToneGenerators();
            for (int ch = 0; ch < 3; ch++)
            {
                StateNode c = StateNode::Object();
                c["name"] = std::string("AY") + std::to_string(chipIdx) + kNames[ch];
                c["active"] = toneGens[ch].toneEnabled() || toneGens[ch].noiseEnabled();
                c["volume"] = int(toneGens[ch].volume());
                c["envelope_enabled"] = toneGens[ch].envelopeEnabled();
                channels.push(c);
            }
            StateNode chipInfo = StateNode::Object();
            chipInfo["chip_index"] = chipIdx;
            chipInfo["channels"] = channels;
            chips.push(chipInfo);
        }
        ay["chips"] = chips;
    }
    ret["ay_channels"] = ay;

    // General Sound and Covox: subsets of the same reports /state/audio/gs and /state/audio/covox serve
    StateNode gs = StateNode::Object();
    {
        const StateNode full = Gs(context);
        const StateNode* available = full.find("available");
        gs["available"] = available && available->b;
        if (available && available->b)
        {
            for (const char* key : {"rom_loaded", "ram_kb", "command_pending", "data_pending"})
                if (const StateNode* v = full.find(key))
                    gs[key] = *v;
            if (const StateNode* channels = full.find("channels"))
                gs["channels"] = *channels;
        }
    }
    ret["general_sound"] = gs;
    StateNode covox = StateNode::Object();
    {
        const StateNode full = Covox(context);
        const StateNode* available = full.find("available");
        covox["available"] = available && available->b;
        if (available && available->b)
        {
            if (const StateNode* v = full.find("fitment"))
                covox["fitment"] = *v;
            if (const StateNode* v = full.find("channels"))
                covox["channels"] = *v;
        }
    }
    ret["covox"] = covox;

    StateNode master = StateNode::Object();
    master["muted"] = sm ? sm->isMuted() : false;
    master["sample_rate_hz"] = static_cast<uint64_t>(sm ? sm->getCoreRate() : 44100u);
    master["channels"] = "stereo";
    master["bit_depth"] = 16;
    ret["master"] = master;
    if (const StateNode* devices = mixer.find("devices"))
        ret["mixer"] = *devices;
    return ret;
}
}  // namespace DeviceState
