#include "audioactivityindicators.h"

#include <algorithm>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

static_assert(static_cast<int>(AudioSource::CdAudio) + 1 == 14, "HUD_SOURCES must cover every AudioSource");

void AudioActivityIndicators::endFrame(const unreal::UUID& emulatorId, const std::vector<AudioDeviceInfo>& devices,
                                       bool neoGSFitted, bool neoGSDma, bool neoGSTransfer)
{
    // Not mixer sources: the card's data movement, held the same second
    _framesSinceNeoGSDma = neoGSDma ? 0 : std::min(_framesSinceNeoGSDma + 1, HOLD_FRAMES);
    _framesSinceNeoGSTransfer = neoGSTransfer ? 0 : std::min(_framesSinceNeoGSTransfer + 1, HOLD_FRAMES);

    for (const AudioDeviceInfo& d : devices)
    {
        int& frames = _framesSinceSound[static_cast<int>(d.type)];
        if (d.activeRecently)
            frames = 0;
        else if (frames < HOLD_FRAMES)
            frames++;
    }

    bool on[HUD_SOURCES] = {};
    on[static_cast<int>(AudioSource::Beeper)] = held(AudioSourceType::Beeper);
    on[static_cast<int>(AudioSource::Covox)] = held(AudioSourceType::COVOX);
    on[static_cast<int>(AudioSource::AY)] = held(AudioSourceType::AY1_All) && !held(AudioSourceType::AY2_All);
    on[static_cast<int>(AudioSource::TurboSound)] = held(AudioSourceType::AY2_All);
    on[static_cast<int>(AudioSource::FM)] = held(AudioSourceType::FM1) || held(AudioSourceType::FM2);
    on[static_cast<int>(neoGSFitted ? AudioSource::NeoGS : AudioSource::GeneralSound)] = held(AudioSourceType::GeneralSound);
    on[static_cast<int>(AudioSource::NeoGSMp3)] = held(AudioSourceType::GeneralSoundMp3);
    on[static_cast<int>(AudioSource::NeoGSDma)] = _framesSinceNeoGSDma < HOLD_FRAMES;
    on[static_cast<int>(AudioSource::NeoGSTransfer)] = _framesSinceNeoGSTransfer < HOLD_FRAMES;
    on[static_cast<int>(AudioSource::MoonFM)] = held(AudioSourceType::Moonsound_FM);
    on[static_cast<int>(AudioSource::MoonPCM)] = held(AudioSourceType::Moonsound_PCM);
    on[static_cast<int>(AudioSource::CdAudio)] = held(AudioSourceType::CdAudio0) || held(AudioSourceType::CdAudio1) ||
                                                 held(AudioSourceType::CdAudio2) || held(AudioSourceType::CdAudio3);

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    for (int source = 0; source < HUD_SOURCES; source++)
    {
        if (!on[source] && !_posted[source])
            continue;

        mc.Post(NC_AUDIO_ACTIVITY,
                new AudioActivityPayload(emulatorId, static_cast<AudioSource>(source), on[source]));
        _posted[source] = on[source];
    }
}

void AudioActivityIndicators::reset()
{
    for (int& frames : _framesSinceSound)
        frames = HOLD_FRAMES;
    _framesSinceNeoGSDma = HOLD_FRAMES;
    _framesSinceNeoGSTransfer = HOLD_FRAMES;
    for (bool& posted : _posted)
        posted = false;
}

void AudioActivityIndicators::stop(const unreal::UUID& emulatorId)
{
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    for (int source = 0; source < HUD_SOURCES; source++)
    {
        if (_posted[source])
            mc.Post(NC_AUDIO_ACTIVITY, new AudioActivityPayload(emulatorId, static_cast<AudioSource>(source), false));
    }
    reset();
}

bool AudioActivityIndicators::held(AudioSourceType type) const
{
    const int index = static_cast<int>(type);
    return index >= 0 && index < SOURCE_TYPES && _framesSinceSound[index] < HOLD_FRAMES;
}
