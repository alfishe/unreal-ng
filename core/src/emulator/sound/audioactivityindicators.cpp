#include "audioactivityindicators.h"

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

static_assert(static_cast<int>(AudioSource::MoonPCM) + 1 == 9, "HUD_SOURCES must cover every AudioSource");

void AudioActivityIndicators::endFrame(const unreal::UUID& emulatorId, const std::vector<AudioDeviceInfo>& devices)
{
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
    on[static_cast<int>(AudioSource::GeneralSound)] = held(AudioSourceType::GeneralSound);
    on[static_cast<int>(AudioSource::MoonFM)] = held(AudioSourceType::Moonsound_FM);
    on[static_cast<int>(AudioSource::MoonPCM)] = held(AudioSourceType::Moonsound_PCM);

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
