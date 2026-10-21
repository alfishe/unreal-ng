#pragma once

// MCP smart tool: audio_mixer - the sound mixer and the sound settings: every row (beeper, ay1, fm1, covox, gs,
// moonsound_fm, ms_*, next_ay1, ...) and its channels (ay1.a, gs.3, moonsound_fm.7, ms_midi.10): read, mute / solo /
// volume / gain_db, the levels; the sound settings (ay_stereo, ay_chip, ay_voicing, ay_room, ay_punch, beeper_punch,
// fm_trim_db, neogs_stereo, covox_dc_removal, fir_filter, audio_rate). A thin adapter over the WebAPI routes
// /audio/mixer, /audio/mixer/{source}, /audio/levels and /settings (core AudioMixer, SoundCharacterSettings): the
// same keys, values and errors as every other surface.

#include <string>
#include <vector>

#include "mcp-tools.h"

namespace mcp
{

/// Registers the audio_mixer tool
void RegisterAudioMixer(ToolRegistry& registry);

/// The tool's actions: get, source, set, levels, settings, set_setting
const std::vector<std::string>& AudioMixerToolActions();

} // namespace mcp
