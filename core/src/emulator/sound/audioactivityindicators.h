#pragma once

#include <stdafx.h>

#include <vector>

#include "common/uuid.h"
#include "emulator/sound/audiodeviceinfo.h"

/// @file audioactivityindicators.h
/// @brief HUD audio nudges, driven by the audio-settings LEDs.
///
/// SoundManager computes one "this frame carried sound" flag per mixer
/// source - the LED (AudioDeviceInfo::activeRecently). This class holds each
/// flag for a second and posts NC_AUDIO_ACTIVITY from it, so the HUD nudge
/// and the LED are the same measurement: the LED follows the frame, the
/// nudge stays up across short gaps instead of flickering.
///
/// Only sound counts. Register traffic that leaves the output silent (the
/// 128K ROM's AY init and its keypad polling through the AY I/O port) never
/// reaches a source buffer, so it lights neither.
///
/// Mixer sources map onto HUD sources: AY 1 alone -> AY, AY 2 -> TurboSound
/// (single-AY playback only uses chip 0), FM 1/FM 2 -> FM, the rest 1:1.
class AudioActivityIndicators
{
public:
    AudioActivityIndicators() { reset(); }

    /// Frames a nudge stays after the last frame with sound (~1 s)
    static constexpr int HOLD_FRAMES = 50;

    /// Account one mixed frame from the sources' LEDs and post
    /// NC_AUDIO_ACTIVITY: every frame while a nudge is held (the HUD keeps a
    /// short TTL in case frames stop, e.g. pause), once more when it ends
    void endFrame(const unreal::UUID& emulatorId, const std::vector<AudioDeviceInfo>& devices);

    /// Forget all activity (reset)
    void reset();

    /// Emulator paused: post "inactive" for every nudge still on, then reset
    void stop(const unreal::UUID& emulatorId);

    /// Mixer source `type` had sound within the last HOLD_FRAMES frames
    bool held(AudioSourceType type) const;

private:
    static constexpr int SOURCE_TYPES = static_cast<int>(AudioSourceType::Custom) + 1;
    static constexpr int HUD_SOURCES = 9;  // AudioSource values (checked in the .cpp)

    int _framesSinceSound[SOURCE_TYPES];
    bool _posted[HUD_SOURCES];  // HUD nudge on, as last posted
};
