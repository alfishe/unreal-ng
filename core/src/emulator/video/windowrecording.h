#pragma once

/// @file windowrecording.h
/// @brief The GUI window recording (the picture as the window draws it, CRT and all, the on-screen keyboard, the
/// machine's sound, frame for frame at the machine's rate), shared by every automation surface: CLI `video window
/// record`, WebAPI /video/window/record*, Lua / Python window_record() / window_record_frames() / window_record_stop()
/// / window_record_status(), MCP capture_media window_record_* (through the WebAPI).
///
/// The window belongs to the front end: the core asks the IWindowViewProvider it registered (windowview.h). Headless
/// there is none: every call answers "no-window". Validation lives here, so every surface refuses the same values with
/// the same message; the provider only records.

#include <emulator/state/statenode.h>
#include <emulator/video/windowview.h>

#include <cstdint>
#include <string>

class Emulator;

namespace WindowRecording
{
/// The fields a start takes besides the file, in the order every surface lists them
inline constexpr const char* kKeys = "keyboard, codec, audio, fps, scale, picture_height, width, height, layout";

inline constexpr int kMaxFps = 100;
inline constexpr int kMinScale = 1;
inline constexpr int kMaxScale = 4;
inline constexpr int kMaxPictureHeight = 4320;
inline constexpr int kMaxFrames = 100000;  ///< frames one record/frames call runs

enum class Status
{
    Ok,
    NoWindow,      ///< no front end window (headless)
    BadParameter,  ///< a value out of range, an unknown key
    Unavailable,   ///< the front end refused (no ffmpeg, not recording, another instance shown, a wrong keyboard)
    NotPaused,     ///< record/frames on a running machine
    Revoked        ///< the user took control back while the frames ran
};

/// "ok", "no-window", "bad-parameter", "unavailable", "not-paused", "revoked": the `kind` of an error on every surface
const char* StatusName(Status status);

struct Result
{
    Status status = Status::Ok;
    std::string error;
    WindowRecordingStatus recording;
    int ran = 0;  ///< frames run by RunFrames

    bool Ok() const { return status == Status::Ok; }
};

/// Put one `key` = `value` word (as a command line renders it) into `request`. False with `error` for an unknown key
/// or a value of the wrong kind; ranges are checked by Start()
bool ApplyWord(WindowRecordingRequest& request, const std::string& key, const std::string& value, std::string& error);

/// Validate `request`, then start recording the window showing `emulatorId`
Result Start(const std::string& emulatorId, const WindowRecordingRequest& request);
/// The recording's status (recording false when there is none)
Result Get();
/// Finish the file: waits for ffmpeg, then muxes the sound in (seconds)
Result Stop();
/// A paused machine runs `frames` frames, each one drawn by the window and written before the next runs. Blocks until
/// they ran (the caller's thread: not the GUI's). The run control check is the caller's (its session, its origin)
Result RunFrames(Emulator& emulator, int frames);

/// {recording, path, keyboard, frames, width, height, fps, audio, sample_rate, audio_samples}
StateNode Report(const WindowRecordingStatus& s);
/// {ok: false, error, kind} for a failed call
StateNode ErrorNode(const Result& r);
/// One line, e.g. "window recording to /x/demo.mov: 1406x1828 at 48.828 fps, sound 44100 Hz, 0 frames, keyboard
/// pentagon2plus"
std::string Summary(const WindowRecordingStatus& s);
}  // namespace WindowRecording
