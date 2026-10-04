#pragma once

/// `videorecord start` argument parsing for the CLI. Header-only, no socket or CLIProcessor dependencies, so
/// core-tests can unit-test it. The codec rules are RecordingRequest's (shared with the WebAPI, Lua and Python).
///
///   videorecord start [format] [file] [--fps N] [--scale N] [--region full|screen] [--audio-rate N|auto]
///                     [--audio CODEC] [--video-bitrate KBPS] [--audio-bitrate KBPS]

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

#include "emulator/sound/audio.h"
#include "recordingrequest.h"

namespace CliVideoRecord
{

struct StartOptions
{
    std::string format = "gif";
    std::string filename;  ///< "" = default path under the temp directory (the caller fills it)
    float fps = 50.0f;
    uint32_t scale = 1;
    bool screenRegion = false;  ///< --region screen|main; false = the whole frame with its border (the default)
    bool hasAudioRate = false;
    uint32_t audioRate = 0;  ///< 0 = auto (with hasAudioRate)
    std::string audio;       ///< normalized audio codec, "" = video only
    uint32_t videoBitrate = 0;
    uint32_t audioBitrate = 0;
};

/// Whole-token base-10 unsigned value
inline bool ParseKbps(const std::string& text, uint32_t& out)
{
    if (text.empty() || text.size() > 9 ||
        !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
        return false;
    out = static_cast<uint32_t>(std::stoul(text));
    return true;
}

/// Default file extension for a format when no file is given (h264/h265/hevc/vp9 -> mkv, rawvideo -> avi)
inline std::string DefaultExtension(const std::string& format)
{
    if (format == "h264" || format == "h265" || format == "hevc" || format == "vp9")
        return "mkv";
    if (format == "rawvideo")
        return "avi";
    return format;
}

/// Parse `videorecord start ...`; args[0] is "start". Checks the options' syntax and ranges; the audio/container
/// check needs the final filename, so it runs in Validate() after the caller picked the default path
inline bool ParseStart(const std::vector<std::string>& args, StartOptions& out, std::string& error)
{
    out = StartOptions();
    int positional = 0;  // [format] then [file]
    for (size_t i = 1; i < args.size(); i++)
    {
        const std::string& arg = args[i];
        const bool hasValue = i + 1 < args.size();
        if (arg == "--fps" && hasValue)
        {
            try
            {
                out.fps = std::stof(args[++i]);
            }
            catch (...)
            {
                error = "Invalid fps value.";
                return false;
            }
            out.fps = std::clamp(out.fps, 1.0f, 100.0f);
        }
        else if (arg == "--scale" && hasValue)
        {
            uint32_t scale = 0;
            if (!ParseKbps(args[++i], scale))
            {
                error = "Invalid scale value.";
                return false;
            }
            out.scale = std::clamp<uint32_t>(scale, 1, 4);
        }
        else if (arg == "--region" && hasValue)
        {
            std::string region = args[++i];
            std::transform(region.begin(), region.end(), region.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (region != "full" && region != "screen" && region != "main")
            {
                error = "Unknown region '" + region + "': use full or screen.";
                return false;
            }
            out.screenRegion = region != "full";
        }
        else if (arg == "--audio-rate" && hasValue)
        {
            std::string rate = args[++i];
            std::transform(rate.begin(), rate.end(), rate.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            out.hasAudioRate = true;
            if (rate == "auto")
            {
                out.audioRate = 0;
                continue;
            }
            uint32_t value = 0;
            if (!ParseKbps(rate, value))
            {
                error = "Invalid audio rate value. Use 44100..192000 or auto.";
                return false;
            }
            if (!IsSupportedCoreRate(value))
            {
                error = "Unsupported audio rate " + rate + ". Use 44100, 48000, 88200, 96000, 176400, 192000 or auto.";
                return false;
            }
            out.audioRate = value;
        }
        else if (arg == "--audio" && hasValue)
        {
            out.audio = RecordingRequest::NormalizeAudioCodec(args[++i]);
        }
        else if ((arg == "--video-bitrate" || arg == "--audio-bitrate") && hasValue)
        {
            uint32_t value = 0;
            if (!ParseKbps(args[++i], value))
            {
                error = "Invalid " + arg.substr(2) + " value (kbps).";
                return false;
            }
            (arg == "--video-bitrate" ? out.videoBitrate : out.audioBitrate) = value;
        }
        else if (arg.size() > 2 && arg.compare(0, 2, "--") == 0)
        {
            error = "Unknown or incomplete option '" + arg +
                    "'. Options: --fps N, --scale N, --region full|screen, --audio-rate N|auto, --audio CODEC, --video-bitrate KBPS, "
                    "--audio-bitrate KBPS.";
            return false;
        }
        else if (positional == 0)
        {
            out.format = arg;
            positional++;
        }
        else if (positional == 1)
        {
            out.filename = arg;
            positional++;
        }
    }
    return true;
}

/// The audio codec against the output container and the bitrate ranges ("" error = accepted)
inline bool Validate(const StartOptions& options, std::string& error)
{
    error = RecordingRequest::ValidateAudio(options.format, options.filename, options.audio);
    if (error.empty())
        error = RecordingRequest::ValidateBitrates(options.videoBitrate, options.audioBitrate, options.audio);
    return error.empty();
}

}  // namespace CliVideoRecord
