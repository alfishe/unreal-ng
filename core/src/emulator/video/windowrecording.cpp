#include "windowrecording.h"

#include <emulator/emulator.h>
#include <emulator/video/windowdisplay.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <utility>

namespace WindowRecording
{
namespace
{
Result Fail(Status status, std::string error)
{
    Result r;
    r.status = status;
    r.error = std::move(error);
    return r;
}

constexpr const char* kHeadless = "No window: the emulator runs headless";

/// A whole number word, decimal; false for anything else
bool ParseInt(const std::string& word, int& out)
{
    double number = 0;
    if (!WindowDisplay::ParseNumber(word, number) || number != static_cast<double>(static_cast<long long>(number)) ||
        number < -1e9 || number > 1e9)
        return false;
    out = static_cast<int>(number);
    return true;
}

/// The ranges every surface refuses alike; empty when the request is fine
std::string Validate(const WindowRecordingRequest& request)
{
    if (request.path.empty())
        return "path: the file to write (.mov for prores, .mp4 for h264), server side";
    if (request.codec != "prores" && request.codec != "h264")
        return "codec takes prores or h264";
    if (request.layout != "below" && request.layout != "side")
        return "layout takes below or side";
    if (request.pictureHeight < 0 || request.pictureHeight > kMaxPictureHeight)
        return "picture_height takes 0.." + std::to_string(kMaxPictureHeight);
    if (request.scale < kMinScale || request.scale > kMaxScale)
        return "scale takes " + std::to_string(kMinScale) + ".." + std::to_string(kMaxScale);
    if (request.fps < 0 || request.fps > kMaxFps)
        return "fps takes 0 (the machine's frame rate) or 1.." + std::to_string(kMaxFps);
    // The sound is the machine's, frame by frame: another picture rate would drift away from it
    if (request.audio && request.fps != 0)
        return "A sound track needs fps 0 (the machine's rate): leave fps out, or add audio=false";
    return {};
}
}  // namespace

const char* StatusName(Status status)
{
    switch (status)
    {
        case Status::Ok: return "ok";
        case Status::NoWindow: return "no-window";
        case Status::BadParameter: return "bad-parameter";
        case Status::Unavailable: return "unavailable";
        case Status::NotPaused: return "not-paused";
        case Status::Revoked: return "revoked";
    }
    return "ok";
}

bool ApplyWord(WindowRecordingRequest& request, const std::string& key, const std::string& value, std::string& error)
{
    int number = 0;
    const bool isNumber = ParseInt(value, number);
    if (key == "path")
        request.path = value;
    else if (key == "keyboard")
    {
        // on / true = the keyboard the window shows, off / false / none = the picture only, else a picture id
        bool on = false;
        if (WindowDisplay::ParseBool(value, on))
            request.keyboard = on ? "default" : "";
        else
            request.keyboard = WindowDisplay::Lower(value) == "none" ? std::string() : value;
    }
    else if (key == "codec" || key == "layout")
        (key == "codec" ? request.codec : request.layout) = WindowDisplay::Lower(value);
    else if (key == "audio")
    {
        if (WindowDisplay::Lower(value) == "none")
            request.audio = false;
        else if (!WindowDisplay::ParseBool(value, request.audio))
            error = "audio takes on or off";
    }
    else if (key == "fps" || key == "scale" || key == "picture_height" || key == "width" || key == "height")
    {
        if (!isNumber || number < 0 || number > 65535)
            error = key + " takes a whole number";
        else if (key == "fps")
            request.fps = number;
        else if (key == "scale")
            request.scale = number;
        else if (key == "picture_height")
            request.pictureHeight = number;
        else
            (key == "width" ? request.frameWidth : request.frameHeight) = static_cast<uint16_t>(number);
    }
    else
        error = "Unknown key '" + key + "': path, " + kKeys;
    return error.empty();
}

Result Start(const std::string& emulatorId, const WindowRecordingRequest& request)
{
    const std::string invalid = Validate(request);
    if (!invalid.empty())
        return Fail(Status::BadParameter, invalid);
    const auto provider = WindowView::Provider();
    if (!provider)
        return Fail(Status::NoWindow, kHeadless);
    Result r;
    if (!provider->StartWindowRecording(emulatorId, request, r.recording, r.error))
        return Fail(Status::Unavailable, r.error);
    return r;
}

Result Get()
{
    const auto provider = WindowView::Provider();
    if (!provider)
        return Fail(Status::NoWindow, kHeadless);
    Result r;
    if (!provider->GetWindowRecording(r.recording, r.error))
        return Fail(Status::Unavailable, r.error);
    return r;
}

Result Stop()
{
    const auto provider = WindowView::Provider();
    if (!provider)
        return Fail(Status::NoWindow, kHeadless);
    Result r;
    if (!provider->StopWindowRecording(r.recording, r.error))
        return Fail(Status::Unavailable, r.error);
    return r;
}

Result RunFrames(Emulator& emulator, int frames)
{
    if (frames < 1 || frames > kMaxFrames)
        return Fail(Status::BadParameter, "frames: 1.." + std::to_string(kMaxFrames));
    if (!emulator.IsPaused())
        return Fail(Status::NotPaused, "Pause the machine first: it runs frame by frame here");
    const auto provider = WindowView::Provider();
    if (!provider)
        return Fail(Status::NoWindow, kHeadless);
    Result r;
    if (!provider->GetWindowRecording(r.recording, r.error) || !r.recording.recording)
        return Fail(Status::Unavailable,
                    r.error.empty() ? "The window is not being recorded: video/window/record first" : r.error);
    std::string why;
    for (int frame = 0; frame < frames; ++frame)
    {
        if (!provider->GetWindowRecording(r.recording, why) || !r.recording.recording)
            return Fail(Status::Unavailable, why.empty() ? "The recording stopped" : why);
        const uint64_t before = r.recording.frames;
        emulator.RunFrame();
        if (emulator.LastDirectRunAborted())
            return Fail(Status::Revoked, "The user took control back");
        // The window draws the frame (and the recording takes it) before the next one runs
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (provider->GetWindowRecording(r.recording, why) && r.recording.frames <= before)
        {
            if (std::chrono::steady_clock::now() > deadline)
                return Fail(Status::Unavailable,
                            "The window recorded nothing within 10 s (frame " + std::to_string(frame) + ")");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    r.ran = frames;
    return r;
}

StateNode Report(const WindowRecordingStatus& s)
{
    StateNode node = StateNode::Object();
    node["recording"] = s.recording;
    node["path"] = s.path;
    node["keyboard"] = s.keyboard;
    node["frames"] = s.frames;
    node["width"] = static_cast<uint64_t>(s.width);
    node["height"] = static_cast<uint64_t>(s.height);
    node["fps"] = s.fps;
    node["audio"] = s.audio;
    node["sample_rate"] = static_cast<uint64_t>(s.sampleRate);
    node["audio_samples"] = s.audioSamples;
    return node;
}

StateNode ErrorNode(const Result& r)
{
    StateNode node = StateNode::Object();
    node["ok"] = false;
    node["error"] = r.error;
    node["kind"] = StatusName(r.status);
    return node;
}

std::string Summary(const WindowRecordingStatus& s)
{
    char fps[32];
    std::snprintf(fps, sizeof(fps), "%.3f", s.fps);
    std::string line = std::string(s.recording ? "window recording to " : "window recording off, last ") + s.path + ": " +
                       std::to_string(s.width) + "x" + std::to_string(s.height) + " at " + fps + " fps";
    line += s.audio ? ", sound " + std::to_string(s.sampleRate) + " Hz" : std::string(", no sound");
    line += ", " + std::to_string(s.frames) + " frames";
    line += s.keyboard.empty() ? ", no keyboard" : ", keyboard " + s.keyboard;
    return line;
}
}  // namespace WindowRecording
