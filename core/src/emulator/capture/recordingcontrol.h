#pragma once

/// @file recordingcontrol.h
/// @brief Video and audio recording for every surface, the way MediaControl is for media: the WebAPI (and MCP through
/// it), CLI, Lua, Python and the catalogue actions (cap.record_toggle, cap.record_pause, cap.clip_last) turn their
/// input into options and the reply into their output, so the presets, the option names, the defaults and the
/// refusals cannot drift apart. The machine's RecordingManager does the work.
///
/// Verbs: start, stop, pause, resume, toggle (start, or stop a running one), status, presets.
///
/// start options (all optional): preset (an id or a title of RecordingPresets()), format (the video codec: gif, h264,
/// h265 / hevc, vp9, rawvideo; none with mode audio), container (the file's extension when no filename is given: mp4,
/// mov, mkv, webm, avi, gif; wav, flac, mp3, ogg for audio), filename (or path), audio (the audio codec: aac, mp3,
/// opus, vorbis, flac, pcm_s16le; none = video only), video_bitrate, audio_bitrate (kbps), fps (1..100), scale (1..4),
/// region (full / screen), profile (native, 1080p, 1440p, 4k), acceleration (auto, hardware, software), mode (single,
/// multi = one audio track per fitted sound source, audio = audio only; split is not implemented yet).
/// A preset fills what the request leaves out.
///
/// Also the clip of a TTD range (ExportTtdClip) and the clip of the last seconds (ClipLast): the frames of the history
/// as a video file, written by the same RecordingManager (video only).
///
/// Example: Execute("start", {{"preset", "gameplay"}}) -> mp4, H.264 + AAC, 2x, the file under the default folder.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class Emulator;
class EmulatorContext;

namespace capture
{

struct RecordingPreset
{
    const char* id;           ///< "gameplay": what a request names
    const char* title;        ///< "Gameplay (MP4)": the Qt menu's text (a request may name it too)
    const char* description;
    const char* mode;         ///< single, multi, audio
    const char* format;       ///< the video codec ("" for audio only)
    const char* container;    ///< the file's extension
    const char* audio;        ///< the audio codec ("" = none)
    uint32_t videoBitrate;    ///< kbps, 0 = the encoder's default
    uint32_t audioBitrate;    ///< kbps, 0 = the encoder's default
    float fps;
    uint32_t scale;
};

/// The core's presets (the Qt Quick Record menu shows the same ones): gameplay, music, audio, gif, hq, multitrack
const std::vector<RecordingPreset>& RecordingPresets();
/// By id or title, case-insensitive; nullptr otherwise
const RecordingPreset* FindRecordingPreset(const std::string& name);
/// The presets as surfaces list them: [{id, title, description, mode, format, container, audio, ...}]
StateNode RecordingPresetsValue();

struct RecordingReply
{
    bool ok = true;
    /// "" on success, else "Bad Request", "Conflict", "Not Found", "Not Implemented", "Recording start failed",
    /// "Internal Error"
    std::string error;
    std::string message;
    StateNode body = StateNode::Object();

    int HttpStatus() const;
    static RecordingReply Fail(std::string error, std::string message, StateNode body = StateNode::Object());
};

class RecordingControl
{
public:
    /// `defaultFolder`: where a start without a filename writes (each plane keeps its own: the WebAPI
    /// <temp>/unreal-mcp, Python <temp>/unreal-python, the actions <user data>/recordings)
    RecordingControl(Emulator& emulator, std::string defaultFolder);

    RecordingReply Execute(const std::string& verb, const std::map<std::string, std::string>& options = {});

    static const std::vector<std::string>& Verbs();
    /// The options a verb accepts (help texts, the OpenAPI spec, tests)
    static const std::vector<std::string>& OptionsFor(const std::string& verb);

    /// The default folder of the actions and the Qt client: <user data>/recordings (a temp folder without one)
    static std::string UserRecordingsFolder();

private:
    RecordingReply Start(const std::map<std::string, std::string>& options);
    RecordingReply Stop();
    RecordingReply Pause();
    RecordingReply Resume();
    RecordingReply Status();

    Emulator& _emulator;
    EmulatorContext* _context = nullptr;
    std::string _defaultFolder;
};

/// The frames [fromFrame, toFrame] of the TTD history as a video file at `path` (its extension: gif, mp4, mkv, webm,
/// mov, avi; `codec` overrides the container's default video codec), at `fps` (0: the machine's frame rate), `scale`
/// 1..4. Needs the TTD engine and a history that is not recording (a recording is paused for browsing first: see
/// ClipLast). Leaves the machine at toFrame. The reply: frames, path, width, height, seconds
RecordingReply ExportTtdClip(Emulator& emulator, uint64_t fromFrame, uint64_t toFrame, const std::string& path,
                             const std::string& codec = {}, uint32_t scale = 1);

/// The last `seconds` of a running TTD recording as a video file (ExportTtdClip): the recording pauses for browsing
/// at its end, the clip is written, the machine goes back to that point and the recording goes on (the run state as
/// it was). Without a recording: "Conflict" with what to do. `path` "" = <user data>/recordings/clip-<stamp>.<format>
RecordingReply ClipLast(Emulator& emulator, double seconds, const std::string& format, const std::string& path,
                        uint32_t scale = 1);

/// The status a key shows: {recording, paused, frames, seconds, bytes, output, available}. Reads only (cheap)
StateNode RecordingReadout(Emulator& emulator);

}  // namespace capture
