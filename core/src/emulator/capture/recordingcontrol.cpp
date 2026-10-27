#include "stdafx.h"

#include "emulator/capture/recordingcontrol.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <optional>
#include <system_error>

#include "base/featuremanager.h"
#include "common/filehelper.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdcursorcell.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audiomixer.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/video/screen.h"

#ifdef ENABLE_RECORDING
#include "recordingmanager.h"
#include "recordingrequest.h"
#endif

namespace capture
{

namespace
{
std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string Trim(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

const std::string* Option(const std::map<std::string, std::string>& options, const char* name)
{
    auto it = options.find(name);
    return it == options.end() ? nullptr : &it->second;
}

/// A whole decimal number in [low, high]; false otherwise
bool ParseInt(const std::string& text, int64_t low, int64_t high, int64_t& out)
{
    const std::string t = Trim(text);
    if (t.empty() || t.size() > 12)
        return false;
    size_t i = t[0] == '-' ? 1 : 0;
    if (i == t.size())
        return false;
    for (size_t k = i; k < t.size(); k++)
        if (!std::isdigit(static_cast<unsigned char>(t[k])))
            return false;
    out = std::stoll(t);
    return out >= low && out <= high;
}

bool ParseNumber(const std::string& text, double low, double high, double& out)
{
    const std::string t = Trim(text);
    if (t.empty())
        return false;
    char* end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return end != nullptr && *end == '\0' && std::isfinite(out) && out >= low && out <= high;
}

/// "<folder>/<prefix>-<stamp>.<ext>", the folder created
std::string StampedPath(const std::string& folder, const std::string& prefix, const std::string& extension)
{
    static std::atomic<unsigned> counter{0};
    const long long stamp = static_cast<long long>(std::time(nullptr)) * 1000 + (counter++ % 1000);
    std::error_code ec;
    std::filesystem::create_directories(FileHelper::ToFsPath(folder), ec);
    return FileHelper::PathCombine(folder, prefix + "-" + std::to_string(stamp) + "." + extension);
}

std::string ExtensionOf(const std::string& path)
{
    return Lower(FileHelper::GetFileExtension(path));
}

/// The video codec a container takes when the request names none
std::string CodecForContainer(const std::string& container)
{
    if (container == "gif")
        return "gif";
    if (container == "webm")
        return "vp9";
    if (container == "avi")
        return "rawvideo";
    return "h264";  // mp4, mov, mkv
}

/// The container a video codec writes when the request names neither a container nor a file
std::string ContainerForCodec(const std::string& codec)
{
    if (codec == "gif")
        return "gif";
    if (codec == "vp9")
        return "webm";
    if (codec == "rawvideo")
        return "avi";
    return "mp4";
}

/// The audio codec an audio-only container takes when the request names none
std::string AudioForContainer(const std::string& container)
{
    if (container == "flac")
        return "flac";
    if (container == "mp3")
        return "mp3";
    if (container == "ogg")
        return "vorbis";
    if (container == "m4a")
        return "aac";
    return "pcm_s16le";  // wav
}

bool AudioOnlyContainer(const std::string& container)
{
    return container == "wav" || container == "flac" || container == "mp3" || container == "ogg" || container == "m4a";
}

void OutputFile(StateNode& body, const std::string& path)
{
    body["output"] = path;
    if (path.empty())
        return;
    std::error_code ec;
    const std::filesystem::path fs = FileHelper::ToFsPath(path);
    body["file_exists"] = std::filesystem::exists(fs, ec);
    const bool regular = !ec && std::filesystem::is_regular_file(fs, ec);
    body["file_size"] = regular ? static_cast<uint64_t>(std::filesystem::file_size(fs, ec)) : uint64_t{0};
}
}  // namespace

/// region <Presets>

const std::vector<RecordingPreset>& RecordingPresets()
{
    // The six of the Qt Quick Record menu, now the core's (2026-10-21): the Music preset writes a .flac (it was a
    // .wav named FLAC), the GIF one at 1x
    static const std::vector<RecordingPreset> presets = {
        {"gameplay", "Gameplay (MP4)", "Standard gameplay recording with H.264 video and AAC audio.", "single", "h264",
         "mp4", "aac", 2500, 192, 50.0f, 2},
        {"music", "Music (FLAC)", "Lossless audio-only recording for music capture.", "audio", "", "flac", "flac", 0,
         0, 50.0f, 1},
        {"audio", "Audio Only (WAV)", "Uncompressed PCM audio-only recording.", "audio", "", "wav", "pcm_s16le", 0, 0,
         50.0f, 1},
        {"gif", "GIF Animation", "Animated GIF for web sharing. No audio.", "single", "gif", "gif", "", 0, 0, 50.0f,
         1},
        {"hq", "High Quality (MKV)", "Premium quality H.265 video with lossless FLAC audio.", "single", "h265", "mkv",
         "flac", 0, 0, 50.0f, 2},
        {"multitrack", "Multi-Track (MKV)", "H.264 video with an audio track per sound source (master first).",
         "multi", "h264", "mkv", "flac", 2500, 0, 50.0f, 2},
    };
    return presets;
}

const RecordingPreset* FindRecordingPreset(const std::string& name)
{
    const std::string key = Lower(Trim(name));
    for (const RecordingPreset& preset : RecordingPresets())
        if (key == preset.id || key == Lower(preset.title))
            return &preset;
    return nullptr;
}

StateNode RecordingPresetsValue()
{
    StateNode list = StateNode::Array();
    for (const RecordingPreset& preset : RecordingPresets())
    {
        StateNode item = StateNode::Object();
        item["id"] = preset.id;
        item["title"] = preset.title;
        item["description"] = preset.description;
        item["mode"] = preset.mode;
        item["format"] = preset.format;
        item["container"] = preset.container;
        item["audio"] = preset.audio;
        item["video_bitrate"] = preset.videoBitrate;
        item["audio_bitrate"] = preset.audioBitrate;
        item["fps"] = static_cast<double>(preset.fps);
        item["scale"] = preset.scale;
        list.push(std::move(item));
    }
    return list;
}

/// endregion </Presets>

/// region <Reply>

int RecordingReply::HttpStatus() const
{
    if (ok)
        return 200;
    if (error == "Bad Request")
        return 400;
    if (error == "Not Found")
        return 404;
    if (error == "Conflict")
        return 409;
    if (error == "Not Implemented")
        return 501;
    return 500;
}

RecordingReply RecordingReply::Fail(std::string error, std::string message, StateNode body)
{
    RecordingReply reply;
    reply.ok = false;
    reply.error = std::move(error);
    reply.message = std::move(message);
    reply.body = std::move(body);
    return reply;
}

/// endregion </Reply>

RecordingControl::RecordingControl(Emulator& emulator, std::string defaultFolder)
    : _emulator(emulator), _context(emulator.GetContext()), _defaultFolder(std::move(defaultFolder))
{
}

const std::vector<std::string>& RecordingControl::Verbs()
{
    static const std::vector<std::string> verbs = {"start", "stop", "pause", "resume", "toggle", "status", "presets"};
    return verbs;
}

const std::vector<std::string>& RecordingControl::OptionsFor(const std::string& verb)
{
    static const std::vector<std::string> start = {"preset",  "format", "container", "filename",     "path",
                                                   "audio",   "video_bitrate", "audio_bitrate", "fps", "scale",
                                                   "region",  "profile", "acceleration", "mode"};
    static const std::vector<std::string> none;
    return verb == "start" || verb == "toggle" ? start : none;
}

std::string RecordingControl::UserRecordingsFolder()
{
    std::string folder = FileHelper::GetUserDataFolder("recordings");
    if (folder.empty())
    {
        std::error_code ec;
        folder = FileHelper::FromFsPath(std::filesystem::temp_directory_path(ec) / "unreal-ng-recordings");
    }
    return folder;
}

RecordingReply RecordingControl::Execute(const std::string& verb, const std::map<std::string, std::string>& options)
{
    const std::string v = Lower(Trim(verb));
    if (v == "presets")
    {
        RecordingReply reply;
        reply.body["presets"] = RecordingPresetsValue();
        return reply;
    }
    if (std::find(Verbs().begin(), Verbs().end(), v) == Verbs().end())
        return RecordingReply::Fail("Bad Request", "Unknown action '" + verb +
                                                       "' (expected start|stop|pause|resume|toggle|status|presets)");
    const std::vector<std::string>& allowed = OptionsFor(v);
    for (const auto& [name, value] : options)
    {
        (void)value;
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end())
            return RecordingReply::Fail("Bad Request", "Unknown option '" + name + "' for " + v);
    }
#ifdef ENABLE_RECORDING
    RecordingManager* rm = _context != nullptr ? _context->pRecordingManager : nullptr;
    if (rm == nullptr)
        return RecordingReply::Fail("Internal Error", "Recording manager not available for this emulator");
    if (v == "toggle")
        return rm->IsRecording() || rm->IsPaused() ? Stop() : Start(options);
    if (v == "start")
        return Start(options);
    if (v == "stop")
        return Stop();
    if (v == "pause")
        return Pause();
    if (v == "resume")
        return Resume();
    return Status();
#else
    return RecordingReply::Fail("Not Implemented", "recording support is disabled in this build (ENABLE_RECORDING=OFF)");
#endif
}

#ifdef ENABLE_RECORDING

RecordingReply RecordingControl::Start(const std::map<std::string, std::string>& options)
{
    RecordingManager* rm = _context->pRecordingManager;
    if (rm->IsRecording() || rm->IsPaused())
        return RecordingReply::Fail("Conflict", "A recording is already active - stop it first");

    // The preset first; what the request names wins
    const RecordingPreset* preset = nullptr;
    if (const std::string* name = Option(options, "preset"))
    {
        preset = FindRecordingPreset(*name);
        if (preset == nullptr)
        {
            std::string ids;
            for (const RecordingPreset& p : RecordingPresets())
                ids += (ids.empty() ? "" : ", ") + std::string(p.id);
            return RecordingReply::Fail("Bad Request", "Unknown preset '" + *name + "' (" + ids + ")");
        }
    }
    std::string mode = preset != nullptr ? preset->mode : "single";
    if (const std::string* text = Option(options, "mode"))
        mode = Lower(Trim(*text));
    if (mode == "single-track" || mode == "singletrack")
        mode = "single";
    else if (mode == "multi-track" || mode == "multitrack")
        mode = "multi";
    else if (mode == "audio-only" || mode == "audioonly")
        mode = "audio";
    else if (mode == "channel-split" || mode == "channelsplit")
        mode = "split";
    if (mode == "split")
        return RecordingReply::Fail("Not Implemented",
                                    "mode split (one track per AY channel) is not implemented yet: use single, multi or "
                                    "audio");
    if (mode != "single" && mode != "multi" && mode != "audio")
        return RecordingReply::Fail("Bad Request", "mode '" + mode + "': expected single, multi or audio");
    const bool audioOnly = mode == "audio";

    std::string filename;
    if (const std::string* text = Option(options, "filename"))
        filename = Trim(*text);
    if (filename.empty())
        if (const std::string* text = Option(options, "path"))
            filename = Trim(*text);
    std::string container = Lower(Trim(Option(options, "container") ? *Option(options, "container") : std::string()));
    if (container.empty() && !filename.empty())
        container = ExtensionOf(filename);

    std::string format;
    if (const std::string* text = Option(options, "format"))
        format = Lower(Trim(*text));
    else if (audioOnly)
        format = "";
    else if (preset != nullptr && preset->format[0] != '\0')
        format = preset->format;
    else if (!container.empty())
        format = CodecForContainer(container);
    else
        format = "gif";  // the old default of every plane
    if (format == "none" || format == "off")
        format.clear();
    if (audioOnly && !format.empty())
        return RecordingReply::Fail("Bad Request", "mode audio records no video: leave format out (or none)");
    if (!audioOnly && format.empty())
        return RecordingReply::Fail("Bad Request", "format none records no video: use mode audio");
    if (container.empty())
        container = preset != nullptr && (audioOnly || std::string(preset->format) == format) ? preset->container
                    : audioOnly                                                               ? std::string("wav")
                                                                                              : ContainerForCodec(format);
    if (audioOnly && !AudioOnlyContainer(container))
        return RecordingReply::Fail("Bad Request",
                                    "mode audio writes wav, flac, mp3, ogg or m4a (container '" + container + "')");

    std::string audio;
    if (const std::string* text = Option(options, "audio"))
        audio = RecordingRequest::NormalizeAudioCodec(*text);
    else if (preset != nullptr && (audioOnly || std::string(preset->format) == format))
        audio = preset->audio;
    if (audioOnly && audio.empty())
        audio = AudioForContainer(container);
    if (mode == "multi" && audio.empty())
        return RecordingReply::Fail("Bad Request", "mode multi needs an audio codec (audio: flac, aac, ...)");

    std::string error;
    int64_t number = 0;
    uint32_t videoBitrate = preset != nullptr ? preset->videoBitrate : 0;
    uint32_t audioBitrate = preset != nullptr ? preset->audioBitrate : 0;
    if (const std::string* text = Option(options, "video_bitrate"))
    {
        if (!ParseInt(*text, 0, 1000000, number))
            return RecordingReply::Fail("Bad Request", "video_bitrate: kbps, 0 or 100..200000");
        videoBitrate = static_cast<uint32_t>(number);
    }
    if (const std::string* text = Option(options, "audio_bitrate"))
    {
        if (!ParseInt(*text, 0, 1000000, number))
            return RecordingReply::Fail("Bad Request", "audio_bitrate: kbps, 0 or 32..512");
        audioBitrate = static_cast<uint32_t>(number);
    }
    if (audio.empty() && preset != nullptr && !Option(options, "audio_bitrate"))
        audioBitrate = 0;
    double fps = preset != nullptr ? static_cast<double>(preset->fps) : 50.0;
    if (const std::string* text = Option(options, "fps"))
    {
        double value = 0;
        if (!ParseNumber(*text, -1e9, 1e9, value))
            return RecordingReply::Fail("Bad Request", "fps: a number 1..100");
        fps = std::clamp(value, 1.0, 100.0);
    }
    int64_t scale = preset != nullptr ? preset->scale : 1;
    if (const std::string* text = Option(options, "scale"))
    {
        if (!ParseInt(*text, -1000, 1000, scale))
            return RecordingReply::Fail("Bad Request", "scale: 1..4");
        scale = std::clamp<int64_t>(scale, 1, 4);
    }
    std::string region = "full";
    if (const std::string* text = Option(options, "region"))
        region = Lower(Trim(*text));
    if (region != "full" && region != "screen" && region != "main")
        return RecordingReply::Fail("Bad Request", "region '" + region + "': expected full or screen");
    const std::string profile =
        RecordingRequest::NormalizeProfile(Option(options, "profile") ? *Option(options, "profile") : std::string());
    const std::string acceleration = RecordingRequest::NormalizeAcceleration(
        Option(options, "acceleration") ? *Option(options, "acceleration") : std::string());
    if (profile.empty())
        return RecordingReply::Fail("Bad Request", "Unknown profile (native, 1080p, 1440p, 4k).");
    if (acceleration.empty())
        return RecordingReply::Fail("Bad Request", "Unknown acceleration (auto, hardware, software).");

    if (filename.empty())
        filename = StampedPath(_defaultFolder, "video-" + _emulator.GetId().substr(0, 8), container);
    if (!audioOnly)
    {
        error = RecordingRequest::ValidateProfile(profile, format);
        if (error.empty())
            error = RecordingRequest::ValidateAudio(format, filename, audio);
    }
    if (error.empty())
        error = RecordingRequest::ValidateBitrates(videoBitrate, audioBitrate, audio);
    if (!error.empty())
        return RecordingReply::Fail("Bad Request", error);

    // Configuration setters refuse changes mid-recording: configure and start with the CPU held (no frame in
    // between; the pause is left as it was)
    std::optional<Emulator::CpuHold> hold;
    hold.emplace(_emulator, "recording start");
    rm->SetVideoFrameRate(static_cast<float>(fps));
    rm->SetScaleFactor(static_cast<uint32_t>(scale));
    rm->SetOutputProfile(profile);
    rm->SetEncoderAcceleration(EncoderAccelerationFromName(acceleration));
    const VideoCaptureRegion captureRegion =
        region == "full" ? VideoCaptureRegion::FullFrame : VideoCaptureRegion::MainScreen;
    rm->SetCaptureRegion(captureRegion);
    rm->ClearAudioTracks();
    rm->SelectAudioSource(AudioSourceType::MasterMix);
    rm->SetRecordingMode(mode == "multi" ? RecordingMode::MultiTrack
                         : audioOnly     ? RecordingMode::AudioOnly
                                         : RecordingMode::SingleTrack);
    size_t tracks = 0;
    if (mode == "multi" && _context->pSoundManager != nullptr)
    {
        // The master mix first, then every fitted source with its own buffer
        auto addTrack = [&](AudioSourceType type, const std::string& name) {
            AudioTrackConfig track;
            track.name = name;
            track.source = type;
            track.codec = audio;
            track.bitrate = audioBitrate > 0 ? audioBitrate : 192;
            rm->AddAudioTrack(track);
            tracks++;
        };
        addTrack(AudioSourceType::MasterMix, "Master");
        for (const AudioDeviceInfo& device : _context->pSoundManager->devices())
        {
            AudioSourceType type{};
            std::string ignored;
            const std::string key = AudioMixer::Key(device.type);
            if (device.type != AudioSourceType::MasterMix && AudioMixer::Capturable(_context, key, type, ignored))
                addTrack(device.type, key);
        }
    }

    // StartRecording fails cleanly without the 'recording' feature: switched on for it
    FeatureManager* features = _context->pFeatureManager;
    const bool featureWasOff = features != nullptr && !features->isEnabled(Features::kRecording);
    if (featureWasOff)
        features->setFeature(Features::kRecording, true);
    const bool started = rm->StartRecording(filename, format, audio, videoBitrate, audioBitrate);
    hold.reset();
    if (!started)
    {
        if (featureWasOff)
            features->setFeature(Features::kRecording, false);
        rm->SetRecordingMode(RecordingMode::SingleTrack);
        rm->ClearAudioTracks();
        return RecordingReply::Fail("Recording start failed", rm->GetLastRecordingError());
    }

    RecordingReply reply;
    StateNode& body = reply.body;
    body["status"] = "success";
    body["recording"] = true;
    if (preset != nullptr)
        body["preset"] = preset->id;
    body["mode"] = mode;
    body["format"] = format;
    body["container"] = container;
    body["fps"] = fps;
    body["scale"] = scale;
    body["profile"] = profile;
    body["acceleration"] = acceleration;
    if (rm->HasFixedOutput())
    {
        body["output_width"] = rm->GetOutputWidth();
        body["output_height"] = rm->GetOutputHeight();
    }
    body["region"] = captureRegion == VideoCaptureRegion::MainScreen ? "screen" : "full";
    body["audio"] = rm->HasAudio();
    body["audio_codec"] = audio;
    body["audio_sample_rate"] = rm->HasAudio() ? rm->GetAudioSampleRate() : 0u;
    body["audio_channels"] = rm->HasAudio() ? rm->GetAudioChannels() : 0u;
    body["audio_tracks"] = static_cast<uint64_t>(mode == "multi" ? tracks : (rm->HasAudio() ? 1 : 0));
    body["video_bitrate"] = videoBitrate;
    body["audio_bitrate"] = audioBitrate;
    body["feature_auto_enabled"] = featureWasOff;
    OutputFile(body, rm->GetOutputFilename().empty() ? filename : rm->GetOutputFilename());
    reply.message = "Recording started - run the emulator to collect frames";
    body["message"] = reply.message;
    return reply;
}

namespace
{
void Stats(StateNode& target, const RecordingManager& rm)
{
    const RecordingManager::RecordingStats stats = rm.GetStats();
    target["frames_recorded"] = static_cast<uint64_t>(stats.framesRecorded);
    target["audio_samples_recorded"] = static_cast<uint64_t>(stats.audioSamplesRecorded);
    target["recorded_duration"] = stats.recordedDuration;
    target["emulated_duration"] = stats.emulatedDuration;
    target["output_file_size"] = static_cast<uint64_t>(stats.outputFileSize);
    target["average_frame_time_ms"] = stats.averageFrameTime;
    target["recent_fps"] = stats.recentFps;
    target["video_codec"] = rm.GetVideoCodec();
    target["profile"] = rm.GetOutputProfile();
    target["acceleration"] = EncoderAccelerationName(rm.GetEncoderAcceleration());
    target["audio"] = rm.HasAudio();
    target["audio_codec"] = rm.HasAudio() ? rm.GetAudioCodec() : std::string();
    target["audio_sample_rate"] = rm.HasAudio() ? rm.GetAudioSampleRate() : 0u;
    target["audio_channels"] = rm.HasAudio() ? rm.GetAudioChannels() : 0u;
    target["audio_duration"] = rm.HasAudio() ? rm.GetAudioDuration() : 0.0;
}
}  // namespace

RecordingReply RecordingControl::Stop()
{
    RecordingManager* rm = _context->pRecordingManager;
    if (!rm->IsRecording() && !rm->IsPaused())
        return RecordingReply::Fail("Conflict", "No active recording to stop");
    rm->StopRecording();
    rm->SetRecordingMode(RecordingMode::SingleTrack);
    RecordingReply reply;
    reply.message = "Recording stopped";
    reply.body["status"] = "success";
    reply.body["message"] = reply.message;
    reply.body["recording"] = false;
    Stats(reply.body, *rm);
    OutputFile(reply.body, rm->GetOutputFilename());
    return reply;
}

RecordingReply RecordingControl::Pause()
{
    RecordingManager* rm = _context->pRecordingManager;
    if (!rm->IsRecording())
        return RecordingReply::Fail("Conflict", "No active recording to pause");
    rm->PauseRecording();
    RecordingReply reply;
    reply.message = "Recording paused";
    reply.body["status"] = "success";
    reply.body["message"] = reply.message;
    reply.body["recording"] = rm->IsRecording();
    reply.body["paused"] = rm->IsPaused();
    OutputFile(reply.body, rm->GetOutputFilename());
    return reply;
}

RecordingReply RecordingControl::Resume()
{
    RecordingManager* rm = _context->pRecordingManager;
    if (!rm->IsPaused())
        return RecordingReply::Fail("Conflict", "Recording is not paused");
    rm->ResumeRecording();
    RecordingReply reply;
    reply.message = "Recording resumed";
    reply.body["status"] = "success";
    reply.body["message"] = reply.message;
    reply.body["recording"] = rm->IsRecording();
    reply.body["paused"] = rm->IsPaused();
    OutputFile(reply.body, rm->GetOutputFilename());
    return reply;
}

RecordingReply RecordingControl::Status()
{
    RecordingManager* rm = _context->pRecordingManager;
    RecordingReply reply;
    StateNode& body = reply.body;
    body["emulator_id"] = _emulator.GetId();
    body["recording"] = rm->IsRecording();
    body["paused"] = rm->IsPaused();
    body["feature_enabled"] = rm->isFeatureEnabled();
    body["realtime_capable"] = rm->IsRealtimeCapable();
    if (!rm->GetLastRecordingError().empty())
        body["last_error"] = rm->GetLastRecordingError();
    Stats(body, *rm);
    OutputFile(body, rm->GetOutputFilename());
    return reply;
}

#else

RecordingReply RecordingControl::Start(const std::map<std::string, std::string>&)
{
    return RecordingReply::Fail("Not Implemented", "recording support is disabled in this build");
}
RecordingReply RecordingControl::Stop()
{
    return Start({});
}
RecordingReply RecordingControl::Pause()
{
    return Start({});
}
RecordingReply RecordingControl::Resume()
{
    return Start({});
}
RecordingReply RecordingControl::Status()
{
    return Start({});
}

#endif  // ENABLE_RECORDING

/// region <Clips>

RecordingReply ExportTtdClip(Emulator& emulator, uint64_t fromFrame, uint64_t toFrame, const std::string& path,
                             const std::string& codec, uint32_t scale)
{
#ifdef ENABLE_RECORDING
    EmulatorContext* context = emulator.GetContext();
    if (context == nullptr || context->pRecordingManager == nullptr)
        return RecordingReply::Fail("Internal Error", "Recording manager not available for this emulator");
    ttd::TimeTravelController* controller = context->pTimeTravelController;
    if (controller == nullptr)
        return RecordingReply::Fail("Conflict", "a video clip of the history needs the TTD engine (UNREAL_TTD_BACKEND)");
    if (path.empty())
        return RecordingReply::Fail("Bad Request", "path: the video file to write (.gif, .mp4, .mkv, .webm, .mov, .avi)");
    const std::string container = ExtensionOf(path);
    if (container != "gif" && container != "mp4" && container != "mkv" && container != "webm" && container != "mov" &&
        container != "avi")
        return RecordingReply::Fail("Bad Request",
                                    "path: a .gif, .mp4, .mkv, .webm, .mov or .avi file (got '." + container + "')");
    const std::string videoCodec = codec.empty() ? CodecForContainer(container) : Lower(Trim(codec));
    RecordingManager* rm = context->pRecordingManager;
    if (rm->IsRecording() || rm->IsPaused())
        return RecordingReply::Fail("Conflict", "a video recording is running: stop it first (the clip uses the recorder)");
    if (fromFrame > toFrame)
        return RecordingReply::Fail("Bad Request", "from must not be after to");

    FeatureManager* features = context->pFeatureManager;
    const bool featureWasOff = features != nullptr && !features->isEnabled(Features::kRecording);
    if (featureWasOff)
        features->setFeature(Features::kRecording, true);
    rm->SetVideoFrameRate(50.0f);
    rm->SetScaleFactor(std::clamp<uint32_t>(scale, 1, 4));
    rm->SetOutputProfile("native");
    rm->SetCaptureRegion(VideoCaptureRegion::FullFrame);
    rm->SetRecordingMode(RecordingMode::SingleTrack);
    rm->ClearAudioTracks();
    if (!rm->StartRecording(path, videoCodec, "", 0, 0))
    {
        if (featureWasOff)
            features->setFeature(Features::kRecording, false);
        return RecordingReply::Fail("Recording start failed", rm->GetLastRecordingError());
    }
    uint64_t frames = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    const std::string walkError =
        controller->VisitComposedFramesAsOperation(fromFrame, toFrame, [&](const ttd::TTDComposedFrame& f) {
            if (f.rgba == nullptr || f.width == 0 || f.height == 0)
                return false;
            FramebufferDescriptor descriptor;
            descriptor.width = static_cast<uint16_t>(f.width);
            descriptor.height = static_cast<uint16_t>(f.height);
            // The encoder only reads the picture
            descriptor.memoryBuffer = const_cast<uint8_t*>(f.rgba);
            descriptor.memoryBufferSize = f.rgbaBytes;
            width = descriptor.width;
            height = descriptor.height;
            rm->CaptureFrame(descriptor);
            frames++;
            return true;
        });
    rm->StopRecording();
    if (featureWasOff)
        features->setFeature(Features::kRecording, false);
    StateNode body = StateNode::Object();
    body["from"] = fromFrame;
    body["to"] = toFrame;
    body["frames"] = frames;
    body["codec"] = videoCodec;
    body["width"] = static_cast<unsigned>(width);
    body["height"] = static_cast<unsigned>(height);
    body["seconds"] = static_cast<double>(frames) / std::max(1.0, ttd::TTDFrameRate(context));
    OutputFile(body, path);
    if (!walkError.empty())
        return RecordingReply::Fail("Conflict", walkError, std::move(body));
    RecordingReply reply;
    reply.body = std::move(body);
    reply.message = "Clip saved: " + path + " (" + std::to_string(frames) + " frames)";
    return reply;
#else
    (void)emulator;
    (void)fromFrame;
    (void)toFrame;
    (void)path;
    (void)codec;
    (void)scale;
    return RecordingReply::Fail("Not Implemented", "recording support is disabled in this build");
#endif
}

namespace
{
/// A TTD verb's reply as a recording reply (the error phrase of TTDControl)
RecordingReply FromTtd(const ttd::TTDReply& reply)
{
    if (reply.Ok())
        return RecordingReply{};
    const std::string message = !reply.message.empty() ? reply.message : std::string("time travel refused");
    return RecordingReply::Fail(ttd::TTDControlErrorPhrase(reply.error), message, reply.body);
}

const StateNode* Child(const StateNode& node, const char* a, const char* b = nullptr)
{
    const StateNode* found = node.find(a);
    return found != nullptr && b != nullptr ? found->find(b) : found;
}
}  // namespace

RecordingReply ClipLast(Emulator& emulator, double seconds, const std::string& format, const std::string& path,
                        uint32_t scale)
{
    EmulatorContext* context = emulator.GetContext();
    if (context == nullptr || context->pTimeTravelHooks == nullptr)
        return RecordingReply::Fail("Conflict", "time travel is not available on this machine");
    if (!(seconds > 0.0) || seconds > 600.0)
        return RecordingReply::Fail("Bad Request", "seconds: 0.1..600");
    const std::string container = format.empty() ? std::string("gif") : Lower(Trim(format));
    ttd::TTDControl ttd(context);
    const ttd::TTDReply position = ttd.Execute({"position", {}});
    const StateNode* state = position.body.find("state");
    const StateNode* endFrame = Child(position.body, "session_end", "frame");
    const StateNode* endTin = Child(position.body, "session_end", "tinframe");
    const StateNode* first = position.body.find("first_frame");
    if (!position.Ok() || state == nullptr || endFrame == nullptr || endTin == nullptr || first == nullptr)
        return RecordingReply::Fail("Conflict", "no time travel history");
    if (state->s != "recording")
        return RecordingReply::Fail("Conflict", "a clip of the last seconds needs a running TTD recording: start it "
                                                "(ttd.start, or Debug > Time Travel > Always Record)");
    const bool wasPaused = emulator.IsPaused();
    const int64_t end = endFrame->i;
    const int64_t tin = endTin->i;
    // Browsing pauses the recording at its end (the engine; D8): everything recorded so far is kept
    const ttd::TTDReply parked =
        ttd.Execute({"seek", {{"frame", std::to_string(end)}, {"tinframe", std::to_string(tin)}}});
    if (!parked.Ok())
        return FromTtd(parked);

    const double fps = std::max(1.0, ttd::TTDFrameRate(context));
    const int64_t last = std::max<int64_t>(first->i, end - 1);  // the last frame recorded whole
    const int64_t from = std::max<int64_t>(first->i, last - static_cast<int64_t>(std::llround(seconds * fps)) + 1);
    const std::string file = !path.empty() ? path
                                           : StampedPath(RecordingControl::UserRecordingsFolder(), "clip", container);
    RecordingReply reply = ExportTtdClip(emulator, static_cast<uint64_t>(from), static_cast<uint64_t>(last), file, {},
                                         scale);

    // Back to the paused point, and the recording goes on from there
    const ttd::TTDReply back = ttd.Execute(
        {"resume", {{"frame", std::to_string(end)}, {"tinframe", std::to_string(tin)}, {"media", "keep"}}});
    if (wasPaused && !emulator.IsPaused())
        emulator.Pause(execution::Requester{execution::Origin::Ttd, {}}, "clip");
    else if (!wasPaused && emulator.IsPaused())
        emulator.Resume(execution::Requester{execution::Origin::Ttd, {}});
    reply.body["recording_resumed"] = back.Ok();
    if (!back.Ok())
        reply.body["resume_error"] = back.message;
    reply.body["seconds_asked"] = seconds;
    return reply;
}

/// endregion </Clips>

StateNode RecordingReadout(Emulator& emulator)
{
    StateNode node = StateNode::Object();
#ifdef ENABLE_RECORDING
    EmulatorContext* context = emulator.GetContext();
    const RecordingManager* rm = context != nullptr ? context->pRecordingManager : nullptr;
    node["available"] = rm != nullptr;
    if (rm == nullptr)
        return node;
    const RecordingManager::RecordingStats stats = rm->GetStats();
    node["recording"] = rm->IsRecording() || rm->IsPaused();
    node["paused"] = rm->IsPaused();
    node["frames"] = static_cast<uint64_t>(stats.framesRecorded);
    node["seconds"] = std::round(stats.emulatedDuration * 10.0) / 10.0;
    node["bytes"] = static_cast<uint64_t>(stats.outputFileSize);
    const std::string output = rm->GetOutputFilename();
    const size_t slash = output.find_last_of("/\\");
    node["output"] = slash == std::string::npos ? output : output.substr(slash + 1);
#else
    (void)emulator;
    node["available"] = false;
#endif
    return node;
}

}  // namespace capture
