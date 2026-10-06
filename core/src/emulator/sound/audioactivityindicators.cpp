#include "audioactivityindicators.h"

#include <algorithm>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

static_assert(static_cast<int>(AudioSource::MultiSoundMidi) + 1 == 21, "HUD_SOURCES must cover every AudioSource");

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

    // A TurboSound FM in the AY socket: its SSG parts light the TSFM indicator, not the board AY's (the FM rows are
    // registered only for it)
    bool tsfm = false;
    for (const AudioDeviceInfo& d : devices)
        tsfm = tsfm || d.type == AudioSourceType::FM1;

    bool on[HUD_SOURCES] = {};
    on[static_cast<int>(AudioSource::Beeper)] = held(AudioSourceType::Beeper);
    on[static_cast<int>(AudioSource::Covox)] = held(AudioSourceType::COVOX);
    const bool ssg1 = held(AudioSourceType::AY1_All);
    const bool ssg2 = held(AudioSourceType::AY2_All);
    on[static_cast<int>(AudioSource::AY)] = !tsfm && ssg1 && !ssg2;
    on[static_cast<int>(AudioSource::TurboSound)] = !tsfm && ssg2;
    on[static_cast<int>(AudioSource::TSFM)] = tsfm && (ssg1 || ssg2);
    on[static_cast<int>(AudioSource::FM)] = held(AudioSourceType::FM1) || held(AudioSourceType::FM2);
    on[static_cast<int>(neoGSFitted ? AudioSource::NeoGS : AudioSource::GeneralSound)] = held(AudioSourceType::GeneralSound);
    on[static_cast<int>(AudioSource::NeoGSMp3)] = held(AudioSourceType::GeneralSoundMp3);
    on[static_cast<int>(AudioSource::NeoGSDma)] = _framesSinceNeoGSDma < HOLD_FRAMES;
    on[static_cast<int>(AudioSource::NeoGSTransfer)] = _framesSinceNeoGSTransfer < HOLD_FRAMES;
    on[static_cast<int>(AudioSource::MoonFM)] = held(AudioSourceType::Moonsound_FM);
    on[static_cast<int>(AudioSource::MoonPCM)] = held(AudioSourceType::Moonsound_PCM);
    on[static_cast<int>(AudioSource::CdAudio)] = held(AudioSourceType::CdAudio0) || held(AudioSourceType::CdAudio1) ||
                                                 held(AudioSourceType::CdAudio2) || held(AudioSourceType::CdAudio3);
    // The ZX-MultiSound: one indicator per row (the YM2203 pair per chip, as the TSFM's 2 x AY + 2 x FM, then PCM,
    // SAA, MIDI)
    on[static_cast<int>(AudioSource::MultiSoundSsg1)] = held(AudioSourceType::MultiSoundSsg1);
    on[static_cast<int>(AudioSource::MultiSoundSsg2)] = held(AudioSourceType::MultiSoundSsg2);
    on[static_cast<int>(AudioSource::MultiSoundFm1)] = held(AudioSourceType::MultiSoundFm1);
    on[static_cast<int>(AudioSource::MultiSoundFm2)] = held(AudioSourceType::MultiSoundFm2);
    on[static_cast<int>(AudioSource::MultiSoundPcm)] = held(AudioSourceType::MultiSoundPcm);
    on[static_cast<int>(AudioSource::MultiSoundSaa)] = held(AudioSourceType::MultiSoundSaa);
    on[static_cast<int>(AudioSource::MultiSoundMidi)] = held(AudioSourceType::MultiSoundMidi);

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
