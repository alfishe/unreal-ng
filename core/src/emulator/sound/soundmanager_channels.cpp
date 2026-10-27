#include "stdafx.h"

#include <algorithm>
#include <cctype>

#include "emulator/sound/mixerchannels.h"
#include "emulator/sound/modelaudiosource.h"
#include "emulator/sound/soundmanager.h"

/// @file soundmanager_channels.cpp
/// @brief SoundManager's mixer channel layer (mixerchannels.h), the parts of a model audio source (the Next) and the
/// AY socket's stereo mode / chip model requests. Every change is host-side: requested on any thread, applied by the
/// emulation thread at the frame start.

/// region <Mixer channels>

SoundManager::MixerChannelRow* SoundManager::channelRowLocked(AudioSourceType type) const
{
    if (!device(type))
        return nullptr;
    auto it = std::find_if(_channelRows.begin(), _channelRows.end(),
                           [type](const MixerChannelRow& r) { return r.type == type; });
    thread_local std::vector<MixerChannelDesc> layout;
    layout.clear();
    MixerChannels::Describe(*this, type, layout);
    if (it != _channelRows.end())
    {
        // The same channels: nothing to do. Another layout (a GS became a NeoGS, a card was swapped): the state of
        // the channels whose key stays is kept
        bool same = it->channels.size() == layout.size();
        for (size_t i = 0; same && i < layout.size(); i++)
            same = it->channels[i].desc.key == layout[i].key;
        if (!same)
        {
            std::vector<MixerChannelState> channels;
            for (const MixerChannelDesc& desc : layout)
            {
                MixerChannelState state;
                for (const MixerChannelState& old : it->channels)
                    if (old.desc.key == desc.key)
                        state = old;
                state.desc = desc;
                if (!desc.hasVolume)
                    state.volume = 1.0f;
                channels.push_back(state);
            }
            it->channels = std::move(channels);
        }
        if (it->channels.empty())
        {
            _channelRows.erase(it);
            return nullptr;
        }
        return &*it;
    }
    if (layout.empty())
        return nullptr;
    MixerChannelRow row;
    row.type = type;
    for (const MixerChannelDesc& desc : layout)
    {
        MixerChannelState state;
        state.desc = desc;
        row.channels.push_back(state);
    }
    _channelRows.push_back(std::move(row));
    return &_channelRows.back();
}

std::vector<SoundManager::MixerChannelState> SoundManager::mixerChannels(AudioSourceType type) const
{
    std::lock_guard<std::mutex> lock(_channelMutex);
    const MixerChannelRow* row = channelRowLocked(type);
    return row ? row->channels : std::vector<MixerChannelState>();
}

bool SoundManager::setMixerChannel(AudioSourceType type, const std::string& channel, int muted, int solo, float volume,
                                   std::string& error)
{
    std::lock_guard<std::mutex> lock(_channelMutex);
    MixerChannelRow* row = channelRowLocked(type);
    if (!row)
    {
        error = "this source has no channels";
        return false;
    }
    std::string key = channel;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto it = std::find_if(row->channels.begin(), row->channels.end(),
                           [&](const MixerChannelState& c) { return c.desc.key == key; });
    if (it == row->channels.end())
    {
        error = "unknown channel '" + channel + "' (";
        for (size_t i = 0; i < row->channels.size(); i++)
            error += (i ? ", " : "") + row->channels[i].desc.key;
        error += ")";
        return false;
    }
    if (volume >= 0.0f && !it->desc.hasVolume)
    {
        error = "channel '" + key + "' has no volume (muted and solo only)";
        return false;
    }
    if (muted >= 0)
        it->mute = muted != 0;
    if (solo >= 0)
        it->solo = solo != 0;
    if (volume >= 0.0f)
        it->volume = std::clamp(volume, 0.0f, 1.0f);
    row->touched = true;
    return true;
}

void SoundManager::applyMixerChannels()
{
    // Never waits: a reader or a writer holding the store (microseconds) moves the apply to the next frame
    std::unique_lock<std::mutex> lock(_channelMutex, std::try_to_lock);
    if (!lock.owns_lock())
        return;
    // A row whose device went (a card out, the GS slot emptied) goes with it
    std::erase_if(_channelRows, [this](const MixerChannelRow& r) { return device(r.type) == nullptr; });
    for (MixerChannelRow& row : _channelRows)
    {
        if (!row.touched)
            continue;
        // The device may have changed its channels (a personality switch): the layout follows it
        if (MixerChannels::Count(*this, row.type) != row.channels.size())
        {
            const AudioSourceType type = row.type;
            if (!channelRowLocked(type))
                break;  // the row is gone; the vector changed, the rest goes at the next frame
            continue;
        }
        const bool solo = std::any_of(row.channels.begin(), row.channels.end(),
                                      [](const MixerChannelState& c) { return c.solo; });
        for (size_t i = 0; i < row.channels.size(); i++)
        {
            const MixerChannelState& c = row.channels[i];
            MixerChannels::Apply(*this, row.type, i, c.mute || (solo && !c.solo), c.desc.hasVolume ? c.volume : 1.0f);
        }
    }
}

void SoundManager::readMixerChannelMeters()
{
    std::unique_lock<std::mutex> lock(_channelMutex, std::try_to_lock);
    if (!lock.owns_lock())
        return;
    for (MixerChannelRow& row : _channelRows)
        for (size_t i = 0; i < row.channels.size(); i++)
        {
            MixerChannelState& c = row.channels[i];
            if (c.desc.hasMeter && !MixerChannels::Peak(*this, row.type, i, c.peak))
                c.peak = 0.0f;
        }
}

/// endregion </Mixer channels>

/// region <Model audio parts>

int SoundManager::modelAudioPart(AudioSourceType type) const
{
    if (!_modelAudio)
        return -1;
    const size_t parts = _modelAudio->AudioPartCount();
    for (size_t p = 0; p < parts; p++)
        if (_modelAudio->AudioPartType(p) == type)
            return static_cast<int>(p);
    return -1;
}

void SoundManager::applyModelAudioParts()
{
    const size_t parts = _modelAudio ? _modelAudio->AudioPartCount() : 0;
    if (parts == 0)
        return;
    // The audibility rule of the mix (handleFrameEnd): with a solo anywhere only soloed rows are heard
    const bool soloActive =
        std::any_of(_devices.begin(), _devices.end(), [](const AudioDeviceInfo& d) { return d.solo; });
    for (size_t p = 0; p < parts; p++)
    {
        const AudioDeviceInfo* d = device(_modelAudio->AudioPartType(p));
        const bool audible = d && (soloActive ? d->solo : !d->mute);
        _modelAudio->SetAudioPartGain(p, audible ? d->volume : 0.0f);
    }
}

/// endregion </Model audio parts>

/// region <AY socket requests>

void SoundManager::setAYStereoMode(AYStereoMode mode)
{
    _requestedAYStereo.store(static_cast<uint8_t>(mode), std::memory_order_release);
}

bool SoundManager::setAYChipModel(AYChipModel model)
{
    if (_turboSound && _turboSound->hasFm())
        return false;
    _requestedAYChipModel.store(static_cast<uint8_t>(model), std::memory_order_release);
    return true;
}

void SoundManager::applyAYSocketRequests()
{
    const uint8_t stereo = _requestedAYStereo.load(std::memory_order_acquire);
    const uint8_t model = _requestedAYChipModel.load(std::memory_order_acquire);
    const bool deviceChanged = _appliedAYDevice != _turboSound;
    if (!deviceChanged && stereo == _appliedAYStereo && model == _appliedAYChipModel)
        return;
    if (_turboSound)
    {
        for (int i = 0; i < _turboSound->getChipCount(); i++)
        {
            SoundChip_AY8910* chip = _turboSound->getChip(i);
            if (!chip)
                continue;
            chip->setStereoMode(static_cast<AYStereoMode>(stereo));
            if (!_turboSound->hasFm())  // a YM2203's SSG half is a fixed YM2149
                chip->setChipModel(static_cast<AYChipModel>(model));
        }
    }
    _appliedAYStereo = stereo;
    _appliedAYChipModel = model;
    _appliedAYDevice = _turboSound;
}

/// endregion </AY socket requests>
