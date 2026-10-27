// MCP smart tool: capture_media
//
// Actions and their WebAPI mappings:
//   screenshot     → GET  /capture/screen?format=&area=   (base64 stripped unless include_image; mode= is a deprecated alias)
//   screen_digest  → GET  /state/screen/digest?banks=
//   record_start   → POST /video/record {action:start,…}
//                    With frames:N it becomes a bounded recording; every_nth:"auto"
//                    samples screen digests frame-by-frame to find the effect's
//                    update quantum Q, records at 50/Q fps and skips the static
//                    frames by pausing/resuming the recorder between updates.
//   record_stop / record_status / record_pause / record_resume → /video/record*
//   audio_capture  → one-shot: POST /audio/capture {action:start, seconds} →
//                    run_frames → result (RMS/peak/dominant frequency, optional WAV)
//   audio_status   → GET /audio/capture/status
//   audio_result   → GET /audio/capture/result?wav=
//   temporal_status → GET /video/temporal (ZX DLSS de-flicker status, one-line summary)
//   temporal_set    → PUT /video/temporal {algorithm} ("off" / "" switches it off)
//   window_display     → GET /video/window (GUI window: GPU, CRT effects / profile, temporal blending)
//   window_display_set → PUT /video/window {display fields} (409 no-window headless)
//   window_crt_params     → GET /video/window/crt_params (the active CRT profile's parameters)
//   window_crt_params_set → PUT /video/window/crt_params {values, steps, reset}
//   viewport           → GET /video/viewport (the overscan frame's crop)
//   viewport_set       → PUT /video/viewport {viewport}
//   window_record_start  → POST /video/window/record {path, keyboard, codec, …} (the window as drawn + the OSD keyboard)
//   window_record_frames → POST /video/window/record/frames {frames} (paused machine: every frame recorded)
//   window_record_stop   → POST /video/window/record/stop
//   window_record_status → GET  /video/window/record
//   vdac2_capture_start  → POST /vdac2/capture/start {path} (TS-Conf VDAC2 card: FT812 bus to an .evr stream)
//   vdac2_capture_stop   → POST /vdac2/capture/stop
//   vdac2_capture_status → GET  /vdac2/capture/status
//
// Drogon-free; all calls go through the loopback IApiCaller.

#include "mcp-media.h"

#include "mcp-tool-utils.h"

#include <algorithm>
#include <cstdio>
#include <cmath>

namespace mcp
{

/// region <capture_media>

namespace
{

/// Computes the visual update quantum from digest samples (oldest first):
/// the largest gap between consecutive digest changes, clamped to [1, maxQuantum].
/// Too few changes → maxQuantum (slow/static screen).
unsigned ComputeQuantum(const std::vector<std::string>& digests, unsigned maxQuantum, bool& periodic)
{
    periodic = false;
    std::vector<unsigned> changes;
    for (unsigned i = 1; i < digests.size(); ++i)
    {
        if (digests[i] != digests[i - 1])
        {
            changes.push_back(i);
        }
    }
    if (changes.size() < 2)
    {
        return maxQuantum;  // no periodic change within the sample window
    }
    periodic = true;
    unsigned quantum = changes[0];
    for (unsigned j = 1; j < changes.size(); ++j)
    {
        quantum = std::max(quantum, changes[j] - changes[j - 1]);
    }
    if (quantum < 1) quantum = 1;
    if (quantum > maxQuantum) quantum = maxQuantum;
    return quantum;
}

/// One-line summary of a /video/temporal status, e.g. "ZX DLSS mod-tpgwafsd active, video +7 frames
/// (143 ms), audio +5, 4.8 ms/frame, correcting (period 2), corrected 40 of 100, shown raw 0, late 0, restarts 0"
std::string TemporalSummary(const Json::Value& status)
{
    std::ostringstream out;
    const std::string algorithm = status.get("algorithm", "").asString();
    if (algorithm.empty())
    {
        out << "ZX DLSS off, video +" << status.get("video_delay_frames", 0).asInt() << " frames";
        return out.str();
    }
    out << "ZX DLSS " << algorithm;
    if (status.get("active", false).asBool())
    {
        out << " active";
    }
    else if (!status.get("applicable", true).asBool())
    {
        out << " " << status.get("inactive_reason", "").asString();  // "not applicable: ..."
    }
    else
    {
        const std::string reason = status.get("inactive_reason", "").asString();
        out << " inactive" << (reason.empty() ? "" : " (" + reason + ")");
    }
    out << ", video +" << status.get("video_delay_frames", 0).asInt() << " frames ("
        << static_cast<long long>(std::llround(status.get("video_delay_ms", 0.0).asDouble())) << " ms), audio +"
        << status.get("audio_extra_delay_frames", 0).asInt();
    out.setf(std::ios::fixed);
    out.precision(1);
    out << ", " << status.get("average_ms", 0.0).asDouble() << " ms/frame, ";
    if (status.get("correcting", false).asBool())
        out << "correcting (" << status["shown_frame"].get("pattern", "").asString() << ")";
    else
        out << (status.get("showing_processed", false).asBool() ? "idle (nothing detected)" : "raw on screen");
    out << ", corrected " << status.get("corrected_frames", 0).asUInt64() << " of "
        << status.get("processed", 0).asUInt64() << ", shown raw " << status.get("shown_raw", 0).asUInt64()
        << ", late " << status.get("late", 0).asUInt64() << ", restarts " << status.get("restarts", 0).asUInt64();
    return out.str();
}

/// Summary of a /video/window/record answer: an agent sees the file, its size and the count in the text
std::string WindowRecordingSummary(const Json::Value& s)
{
    std::string line = std::string(s.get("recording", false).asBool() ? "window recording to " : "window recording off, last ") +
                       s.get("path", "").asString() + ": " + std::to_string(s.get("width", 0).asUInt()) + "x" +
                       std::to_string(s.get("height", 0).asUInt());
    if (s.isMember("fps"))
    {
        char fps[32];
        std::snprintf(fps, sizeof(fps), " at %.3f fps", s.get("fps", 0.0).asDouble());
        line += fps;
    }
    line += s.get("audio", false).asBool()
                ? ", sound " + std::to_string(s.get("sample_rate", 0).asUInt()) + " Hz"
                : std::string(", no sound");
    line += ", " + std::to_string(s.get("frames", 0).asUInt64()) + " frames";
    const std::string keyboard = s.get("keyboard", "").asString();
    line += keyboard.empty() ? ", no keyboard" : ", keyboard " + keyboard;
    return line;
}

/// A number for the text: an integer as it is, else up to 6 decimals ("0.3", not jsoncpp's 0.29999999999999999)
std::string NumberText(double value)
{
    const bool negative = value < 0;
    const auto scaled = static_cast<long long>(std::llround((negative ? -value : value) * 1e6));
    std::string fraction = std::to_string(scaled % 1000000);
    fraction.insert(0, 6 - fraction.size(), '0');
    while (!fraction.empty() && fraction.back() == '0')
        fraction.pop_back();
    return std::string(negative && scaled != 0 ? "-" : "") + std::to_string(scaled / 1000000) +
           (fraction.empty() ? "" : "." + fraction);
}

/// Summary of a /video/window answer, e.g. "window gpu on (available), crt basic, blending off (2 frames, equal)",
/// then the profiles the window offers (an agent may see only the text: it needs the words for crt_profile)
std::string WindowDisplaySummary(const Json::Value& s)
{
    std::string line = std::string("window gpu ") + (s.get("gpu", false).asBool() ? "on" : "off") +
                       (s.get("gpu_available", false).asBool() ? " (available)" : " (unavailable)");
    const std::string profile = s.get("crt_profile", "").asString();
    line += ", crt " + (s.get("crt", false).asBool() ? profile : "off (profile " + profile + ")");
    line += std::string(", blending ") + (s.get("blending", false).asBool() ? "on" : "off") + " (" +
            std::to_string(s.get("blending_frames", 0).asInt()) + " frames, " + s.get("blending_weights", "").asString() +
            ")";
    if (s.get("blending_weights", "").asString() == "exponential" && s.isMember("blending_decay"))
        line += ", decay " + NumberText(s["blending_decay"].asDouble());
    if (s.isMember("fullscreen"))
    {
        const int scale = s.get("scale", 0).asInt();
        line += s.get("fullscreen", false).asBool() ? ", full screen"
                                                     : (scale > 0 ? ", scale " + std::to_string(scale) + "x" : ", scale free");
        line += std::string(", toolbar ") + (s.get("toolbar", true).asBool() ? "on" : "off") + ", status bar " +
                (s.get("status_bar", true).asBool() ? "on" : "off");
        line += std::string(", hud ") + (s.get("hud", false).asBool() ? "on" : "off") + " (theme " +
                s.get("hud_theme", "").asString() + ")";
        line += std::string(", keyboard ") + (s.get("osd_keyboard", false).asBool() ? "on" : "off");
        if (!s.get("osd_keyboard_picture", "").asString().empty())
            line += " (" + s["osd_keyboard_picture"].asString() +
                    (s.get("osd_keyboard_style", "").asString().empty() ? "" : ", " + s["osd_keyboard_style"].asString()) +
                    ")";
        line += std::string(", next layers ") + (s.get("next_layers", false).asBool() ? "on" : "off");
    }
    // The words the setters take: an agent may see only this text
    for (const char* list : {"crt_profiles", "hud_themes", "osd_keyboard_pictures", "osd_keyboard_styles"})
    {
        const Json::Value& offered = s[list];
        if (!offered.isArray() || offered.empty())
            continue;
        line += std::string("\n") + list + ":";
        for (const Json::Value& name : offered)
            line += " " + name.asString();
    }
    const Json::Value& notices = s["hud_notices"];
    if (notices.isObject() && !notices.empty())
    {
        line += "\nhud_notices (off):";
        int off = 0;
        for (const std::string& id : notices.getMemberNames())
            if (!notices[id].asBool())
            {
                line += " " + id;
                ++off;
            }
        if (off == 0)
            line += " none (" + std::to_string(notices.size()) + " shown)";
    }
    return line;
}

/// Summary of a /video/window/crt_params answer: the profile, then a line per parameter with its range
std::string CrtParamsSummary(const Json::Value& p)
{
    const Json::Value& list = p["parameters"];
    std::string text = "CRT profile " + p.get("profile", "").asString() + " (" + p.get("kind", "").asString() + "), " +
                       std::to_string(list.isArray() ? list.size() : 0) + " parameters";
    if (!list.isArray())
        return text;
    for (const Json::Value& item : list)
        text += "\n  " + item.get("name", "").asString() + " = " +
                NumberText(item.get("value", 0).asDouble()) + " (" +
                NumberText(item.get("minimum", 0).asDouble()) + ".." +
                NumberText(item.get("maximum", 0).asDouble()) + ", step " +
                NumberText(item.get("step", 0).asDouble()) + ")";
    return text;
}

/// Summary of a /video/viewport answer
std::string ViewportSummary(const Json::Value& v)
{
    std::string line = std::string("overscan ") + (v.get("overscan", false).asBool() ? "on" : "off");
    if (!v.get("overscan_available", true).asBool())
        line += " (no overscan frame on this machine)";
    line += ", viewport " + v.get("viewport", "").asString() + " (" + v.get("display", "").asString() + " of " +
            v.get("framebuffer", "").asString() + ")";
    return line;
}

void RegisterCaptureMediaImpl(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["action"]["type"] = "string";
    schema["properties"]["action"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* action : {"screenshot", "screen_digest", "record_start", "record_stop", "record_status", "record_pause",
                               "record_resume", "record_presets", "audio_capture", "audio_status", "audio_result",
                               "temporal_status", "temporal_set", "window_display", "window_display_set",
                               "window_crt_params", "window_crt_params_set", "viewport", "viewport_set",
                               "window_record_start", "window_record_frames", "window_record_stop",
                               "window_record_status",
                               "vdac2_capture_start", "vdac2_capture_stop", "vdac2_capture_status",
                               "framebuffer"})
    {
        schema["properties"]["action"]["enum"].append(action);
    }
    schema["properties"]["action"]["description"] =
        "Media operation: still capture, digest, video recording session, audio capture, the ZX DLSS de-flicker "
        "(temporal_status / temporal_set), the GUI window's display settings (window_display / window_display_set: "
        "GPU acceleration, CRT effects and profile, temporal blending, full screen, scale, toolbar, status bar, HUD and "
        "its theme / notices, on-screen keyboard and its picture, the ZX Next layers window; unreal-qt only), the "
        "active CRT profile's parameters (window_crt_params / window_crt_params_set), the overscan frame's viewport "
        "(viewport / viewport_set), the window recording (window_record_start / window_record_frames / "
        "window_record_stop / window_record_status: the picture as the window draws it, CRT and all, with the "
        "on-screen keyboard and the sound; unreal-qt only), or the TS-Conf VDAC2 card's FT812 bus capture to an .evr replay stream "
        "(vdac2_capture_start with filename / vdac2_capture_stop / vdac2_capture_status), or the picture as raw "
        "pixels (framebuffer: format rgba | index - the Sprinter's u16 pens; metadata, base64 data with include_image).";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["properties"]["format"]["type"] = "string";
    schema["properties"]["format"]["description"] = "screenshot: png|gif (default png: lossless; gif has 256 colors). record_start: gif (native) or h264/h265/hevc/vp9/rawvideo (default: the preset's, else gif)";
    schema["properties"]["mode"]["type"] = "string";
    schema["properties"]["mode"]["enum"] = Json::Value(Json::arrayValue);
    schema["properties"]["mode"]["enum"].append("screen");
    schema["properties"]["mode"]["enum"].append("full");
    schema["properties"]["mode"]["description"] = "Deprecated alias of area for screenshot (screen|full)";
    schema["properties"]["area"]["type"] = "string";
    schema["properties"]["area"]["enum"] = Json::Value(Json::arrayValue);
    schema["properties"]["area"]["enum"].append("full");
    schema["properties"]["area"]["enum"].append("screen");
    schema["properties"]["area"]["description"] =
        "screenshot area: full (default) = the whole frame with its border; screen = the working picture of the "
        "frame (the paper of a Spectrum, the graphics window of a TS-Conf, the whole FT812 picture). On a hires mode "
        "both can be the whole frame. The answer carries the frame geometry and the rectangle that was cut";
    schema["properties"]["include_image"]["type"] = "boolean";
    schema["properties"]["include_image"]["default"] = false;
    schema["properties"]["include_image"]["description"] = "Include the base64 image payload for screenshot (large; metadata only by default)";
    schema["properties"]["banks"]["type"] = "string";
    schema["properties"]["banks"]["description"] = "Physical RAM banks for screen_digest, e.g. \"5,7\" (default: model screen banks)";
    schema["properties"]["fps"]["type"] = "integer";
    schema["properties"]["fps"]["description"] =
        "Recording fps (1-100). With every_nth:Q the default is 50/Q so playback is real-time. window_record_start: "
        "leave it out (the machine's own rate, 48.83 on a Pentagon; a sound track needs it)";
    schema["properties"]["scale"]["type"] = "integer";
    schema["properties"]["scale"]["description"] = "Integer upscale for recording (1-4, default 1)";
    schema["properties"]["audio"]["type"] = "string";
    schema["properties"]["audio"]["description"] =
        "record_start: audio codec for the emulated sound track (aac, mp3, opus, vorbis, flac, pcm_s16le). Omit "
        "for video only (default). Must fit the container: h264/hevc + aac in .mp4/.mov is native on macOS; "
        "gif has no audio (refused). window_record_start: the sound track is on unless audio is \"none\" / false "
        "(PCM in .mov, AAC in .mp4, frame for frame with the picture)";
    schema["properties"]["preset"]["type"] = "string";
    schema["properties"]["preset"]["description"] =
        "record_start: a recording preset (record_presets lists them: gameplay = H.264 + AAC mp4, music = FLAC audio only, "
        "audio = WAV audio only, gif, hq = H.265 + FLAC mkv, multitrack = an audio track per sound source); the other "
        "fields override it";
    schema["properties"]["container"]["type"] = "string";
    schema["properties"]["container"]["description"] =
        "record_start: the file's extension when no filename is given: mp4, mov, mkv, webm, avi, gif; wav, flac, mp3, "
        "ogg with record_mode audio";
    schema["properties"]["record_mode"]["type"] = "string";
    schema["properties"]["record_mode"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* mode : {"single", "multi", "audio"})
        schema["properties"]["record_mode"]["enum"].append(mode);
    schema["properties"]["record_mode"]["description"] =
        "record_start: single (one mixed audio track, default), multi (a track per fitted sound source, master first), "
        "audio (audio only: no video)";
    schema["properties"]["video_bitrate"]["type"] = "integer";
    schema["properties"]["video_bitrate"]["description"] = "record_start: video bitrate kbps (0 = default, 100-200000)";
    schema["properties"]["profile"]["type"] = "string";
    schema["properties"]["profile"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* name : {"native", "1080p", "1440p", "4k"})
        schema["properties"]["profile"]["enum"].append(name);
    schema["properties"]["profile"]["description"] =
        "record_start: output profile. native (default) = the picture at its own size times scale. 1080p / 1440p / 4k "
        "= a fixed frame (4k = 3840x2160): the picture is fitted into it with its aspect kept (as large as the "
        "frame allows, black bars, nearest-neighbor sampling - no blur). h264 / h265 only, any container";
    schema["properties"]["acceleration"]["type"] = "string";
    schema["properties"]["acceleration"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* name : {"auto", "hardware", "software"})
        schema["properties"]["acceleration"]["enum"].append(name);
    schema["properties"]["acceleration"]["description"] =
        "record_start: encoder acceleration. auto (default) = a GPU encoder when the machine has one; hardware = "
        "GPU only (fails without one); software = libx264 / libx265";
    schema["properties"]["audio_bitrate"]["type"] = "integer";
    schema["properties"]["audio_bitrate"]["description"] = "record_start: audio bitrate kbps (0 = default, 32-512)";
    schema["properties"]["region"]["type"] = "string";
    schema["properties"]["region"]["enum"] = Json::Value(Json::arrayValue);
    schema["properties"]["region"]["enum"].append("full");
    schema["properties"]["region"]["enum"].append("screen");
    schema["properties"]["region"]["enum"].append("main");
    schema["properties"]["region"]["description"] = "Recording capture region: full (default, the whole frame with its border) or screen/main (the working picture; the file keeps the start window's size, a window that changes later is scaled into it with black bars)";
    schema["properties"]["filename"]["type"] = "string";
    schema["properties"]["filename"]["description"] = "Output file for recording, screenshot (e.g. scratch/screen.png) or vdac2_capture_start (an .evr stream). Saves binary directly to disk on server side.";
    schema["properties"]["path"]["type"] = "string";
    schema["properties"]["path"]["description"] = "Alias for filename.";
    schema["properties"]["every_nth"]["description"] =
        "\"auto\" or integer ≥1 (requires frames): measure the effect's visual update quantum Q via screen digests "
        "(auto) or use Q directly; the recorder is paused across static frames and fps defaults to 50/Q";
    schema["properties"]["layer"]["type"] = "string";
    schema["properties"]["layer"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* layer : {"composed", "ula", "layer2", "tilemap", "sprites", "ids"})
        schema["properties"]["layer"]["enum"].append(layer);
    schema["properties"]["layer"]["description"] =
        "screenshot of a ZX Spectrum Next: ONE LAYER of the last completed frame instead of the composed picture - ula (with the border and "
        "LoRes), layer2, tilemap, sprites (PNG with alpha 0 where the layer does not paint, same 640x512 geometry as the screenshot), or ids "
        "(each pixel coloured by the layer that won it; the legend is in the answer). Exactly as drawn, copper effects included. format=rgba "
        "gives the raw bytes; alpha=false flattens transparent pixels onto the fallback colour. 409 when the machine is not a Next, the "
        "capture is off (invoke_api POST /api/v1/emulator/{id}/next/layer-capture {enabled:true}) or no frame was completed yet; all layers of one frame at once: "
        "invoke_api GET /api/v1/emulator/{id}/capture/next-layers";
    schema["properties"]["size"]["type"] = "string";
    schema["properties"]["size"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* size : {"screen", "grid", "pixel"})
        schema["properties"]["size"]["enum"].append(size);
    schema["properties"]["size"]["description"] =
        "screenshot with layer: screen (default, 640x512), grid (640x256: every line once) or pixel (320x256: one sample per Next pixel; "
        "loses the second sub-pixel of Layer 2 640 / Timex hi-res)";
    schema["properties"]["alpha"]["type"] = "boolean";
    schema["properties"]["alpha"]["description"] = "screenshot with layer: false flattens the pixels the layer does not paint onto the fallback colour (default true: alpha 0)";
    schema["properties"]["frames"]["type"] = "integer";
    schema["properties"]["frames"]["description"] =
        "Bounded recording: captured frames before automatic stop (1-10000). window_record_frames: emulated frames to "
        "run and record (1-100000; a call must end within 2 minutes: a few hundred at a time)";
    schema["properties"]["seconds"]["type"] = "number";
    schema["properties"]["seconds"]["default"] = 1.0;
    schema["properties"]["seconds"]["description"] = "audio_capture duration in emulated seconds (0.01-30)";
    schema["properties"]["source"]["type"] = "string";
    schema["properties"]["source"]["description"] =
        "screenshot: presented (default: the finished frame the window shows, a couple of frames behind) or live "
        "(the frame as drawn now, no delay, no ZX DLSS; a paused machine also reports where the beam stopped) or window "
        "(the picture as the unreal-qt window draws it: its shape, CRT shader and HUD, in display pixels; only for the "
        "instance the GUI shows, area full). "
        "audio_capture: one mixer device instead of the master mix - beeper, ay1, ay2, fm1, fm2, covox (also the "
        "Sprinter's Covox-Blaster DAC), gs, gs_mp3, moonsound_fm, moonsound_pcm, a ZX-MultiSound's ms_ssg1, ms_ssg2, "
        "ms_fm1, ms_fm2 (its YM2203 pair per chip), ms_saa, ms_pcm (GS + SounDrive DACs), ms_midi (inspect_state aspect audio_mixer lists "
        "the fitted ones); its own buffer, before mute / volume";
    schema["properties"]["wav"]["type"] = "boolean";
    schema["properties"]["wav"]["default"] = false;
    schema["properties"]["wav"]["description"] = "Export a WAV file alongside the audio analysis (returns wav_path)";
    schema["properties"]["algorithm"]["type"] = "string";
    schema["properties"]["algorithm"]["description"] =
        "temporal_set: ZX DLSS algorithm name (default/recommended mod-tpgwafsd; temporal_status lists all), or "
        "\"off\" / \"\" to switch it off. While on, video is delayed by the algorithm's look-ahead (7 frames for "
        "mod-tpgwafsd) and audio by the matching extra frames";
    schema["properties"]["display"]["type"] = "object";
    schema["properties"]["display"]["description"] =
        "window_display_set: any of gpu (bool), crt (bool), crt_profile (one of the window's crt_profiles: "
        "none, basic, ...; other than none switches CRT on), blending (bool, temporal blending / gigascreen "
        "smoothing), blending_frames (2..5), blending_weights (equal, exponential), blending_decay (0.1..0.9), "
        "fullscreen (bool), scale (1..4), toolbar, status_bar, hud (bool), hud_theme (one of hud_themes), "
        "hud_notices ({id: bool}, ids from window_display), osd_keyboard (bool), osd_keyboard_picture (one of "
        "osd_keyboard_pictures), osd_keyboard_style (photo, schematic, hybrid: the hybrid shows what a combination key "
        "presses), next_layers (bool), next_layers_mode (layers, composed, winner), next_layers_shown "
        "({ula|layer2|tilemap|sprites: bool}); the rest stay. Applied the way the View menu does; screenshot "
        "source=window shows the effect. Headless: no window (error)";
    for (const char* flag : {"gpu", "crt", "blending", "fullscreen", "toolbar", "status_bar", "hud", "osd_keyboard",
                             "next_layers"})
        schema["properties"]["display"]["properties"][flag]["type"] = "boolean";
    for (const char* word : {"crt_profile", "blending_weights", "hud_theme", "osd_keyboard_picture", "osd_keyboard_style",
                             "next_layers_mode"})
        schema["properties"]["display"]["properties"][word]["type"] = "string";
    schema["properties"]["display"]["properties"]["blending_frames"]["type"] = "integer";
    schema["properties"]["display"]["properties"]["scale"]["type"] = "integer";
    schema["properties"]["display"]["properties"]["blending_decay"]["type"] = "number";
    for (const char* map : {"hud_notices", "next_layers_shown"})
    {
        schema["properties"]["display"]["properties"][map]["type"] = "object";
        schema["properties"]["display"]["properties"][map]["additionalProperties"]["type"] = "boolean";
    }
    schema["properties"]["crt_params"]["type"] = "object";
    schema["properties"]["crt_params"]["description"] =
        "window_crt_params_set: {values: {name: number}, steps: {name: steps (a dial's, the parameter's own step)}, "
        "reset: true | name | [names]}; names from window_crt_params (a libretro preset's own, or basic's curvature, "
        "scanline_weight, ...)";
    schema["properties"]["viewport"]["type"] = "string";
    schema["properties"]["viewport"]["description"] =
        "viewport_set: full, symmetric, standard, screen_only, next or prev (the crop of the Pentagon's overscan "
        "frame; overscan itself: feature overscan or the state/screen/overscan route)";
    schema["properties"]["keyboard"]["type"] = "string";
    schema["properties"]["keyboard"]["description"] =
        "window_record_start: the on-screen keyboard in the file - default (the one the window shows) or a picture "
        "id of the machine (pentagon2plus, zx48k, ...) or of another with the same matrix (zxnext on a Pentagon); a wrong "
        "one lists the offered ids; omit for the picture only";
    schema["properties"]["codec"]["type"] = "string";
    schema["properties"]["codec"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* codec : {"prores", "h264"})
        schema["properties"]["codec"]["enum"].append(codec);
    schema["properties"]["codec"]["description"] =
        "window_record_start: prores (default: ProRes 4444, .mov) or h264 (4:4:4, .mp4); full colour, a CRT mask "
        "survives. Needs ffmpeg on the host";
    schema["properties"]["picture_height"]["type"] = "integer";
    schema["properties"]["picture_height"]["description"] =
        "window_record_start: draw the picture (CRT included) at this height instead of the window's (GPU only, "
        "0 = the window's); with scale 2 and 1080 a 4K frame keeps the mask through YouTube's 4:2:0";
    schema["properties"]["width"]["type"] = "integer";
    schema["properties"]["width"]["description"] = "window_record_start: the file's frame width (with height; 0 = picture + keyboard)";
    schema["properties"]["height"]["type"] = "integer";
    schema["properties"]["height"]["description"] = "window_record_start: the file's frame height (with width)";
    schema["properties"]["layout"]["type"] = "string";
    schema["properties"]["layout"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* layout : {"below", "side"})
        schema["properties"]["layout"]["enum"].append(layout);
    schema["properties"]["layout"]["description"] =
        "window_record_start: the keyboard below the picture (default) or right of it (side, with width / height)";
    schema["required"].append("action");

    registry.Register(
        "capture_media",
        "Media capture: screenshots (PNG/GIF with OCR-friendly metadata or saved to file), deterministic screen digests, video recording "
        "(GIF native; h264/hevc mp4/mov with an optional audio track: audio:'aac'; the core's presets: record_presets, "
        "record_start preset:'gameplay'; record_mode single | multi | audio) with optional every_nth:'auto' visual-quantum detection for duplicate-free bounded recordings, and "
        "one-shot audio capture with DSP analysis (dominant frequency, RMS/peak, optional WAV export), and the ZX DLSS "
        "temporal de-flicker (temporal_status / temporal_set: algorithm, video/audio delay it causes, timing), the GUI "
        "window's display settings (window_display / window_display_set: GPU acceleration, CRT effects and profile, "
        "temporal blending, full screen, scale, bars, HUD, on-screen keyboard, Next layers; window_crt_params / "
        "window_crt_params_set: the CRT profile's parameters), the overscan viewport (viewport / viewport_set), and the "
        "TS-Conf VDAC2 card's FT812 bus capture (vdac2_capture_start / _stop / _status: every chip select and byte "
        "with FT812 clocks and per-frame picture hashes, an .evr stream that replays the chip alone).",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn& progress) {
            std::string action = args["action"].asString();

            if (action == "screenshot")
            {
                bool includeImage = args.isMember("include_image") && args["include_image"].asBool();
                std::string pathArg;
                if (args.isMember("filename") && args["filename"].isString()) pathArg = args["filename"].asString();
                else if (args.isMember("path") && args["path"].isString()) pathArg = args["path"].asString();

                TargetResolver::ResolveFromArgs(
                    args, caller, [&args, includeImage, pathArg, &caller, done](bool ok, const std::string& idOrError) {
                        if (!ok)
                        {
                            done(ToolResult::Error(idOrError));
                            return;
                        }
                        std::string suffix = "/capture/screen";
                        std::string query;
                        if (args.isMember("format") && args["format"].isString() && !args["format"].asString().empty())
                        {
                            query += "format=" + UrlEncodeSegment(args["format"].asString()) + "&";
                        }
                        if (args.isMember("area") && args["area"].isString() && !args["area"].asString().empty())
                        {
                            query += "area=" + UrlEncodeSegment(args["area"].asString()) + "&";
                        }
                        if (args.isMember("mode") && args["mode"].isString() && !args["mode"].asString().empty())
                        {
                            query += "mode=" + UrlEncodeSegment(args["mode"].asString()) + "&";
                        }
                        if (args.isMember("source") && args["source"].isString() && !args["source"].asString().empty())
                        {
                            query += "source=" + UrlEncodeSegment(args["source"].asString()) + "&";
                        }
                        if (args.isMember("layer") && args["layer"].isString() && !args["layer"].asString().empty())
                        {
                            query += "layer=" + UrlEncodeSegment(args["layer"].asString()) + "&";
                        }
                        if (args.isMember("size") && args["size"].isString() && !args["size"].asString().empty())
                        {
                            query += "size=" + UrlEncodeSegment(args["size"].asString()) + "&";
                        }
                        if (args.isMember("alpha") && args["alpha"].isBool())
                        {
                            query += std::string("alpha=") + (args["alpha"].asBool() ? "true" : "false") + "&";
                        }
                        if (!pathArg.empty())
                        {
                            query += "path=" + UrlEncodeSegment(pathArg) + "&";
                        }
                        if (!query.empty())
                        {
                            query.pop_back();
                            suffix += "?" + query;
                        }
                        caller.Call("GET", Endpoint(idOrError, suffix), nullptr,
                                    [includeImage, idOrError, done](int status, Json::Value body) mutable {
                            if (status != 200)
                            {
                                std::string details = DescribeErrorBody(body);
                                done(ToolResult::Error("Screenshot failed (HTTP " + std::to_string(status) + ")" +
                                                       (details.empty() ? "" : ": " + details)));
                                return;
                            }
                            std::ostringstream out;
                            // What the image is: the area, the frame it was cut from and where the working picture is
                            std::ostringstream what;
                            what << body.get("area", "").asString();
                            if (body.isMember("layer"))  // one layer of a Next frame: which, and of which frame
                            {
                                what << "layer " << body["layer"].asString() << " of Next frame #" << body["frame"].get("sequence", 0).asUInt64()
                                     << (body["layer"].asString() == "ids" ? ", opaque" : (body.get("alpha", true).asBool() ? ", alpha 0 where not painted" : ", flattened"));
                                if (body["layer"].asString() == "ids" && body.isMember("legend"))
                                {
                                    what << "; legend:";
                                    for (const Json::Value& entry : body["legend"])
                                        what << " " << entry.get("id", 0).asUInt() << "=" << entry.get("name", "").asString() << "(" << entry.get("colour", "").asString() << ")";
                                }
                            }
                            if (body.get("source", "presented").asString() == "live")
                                what << ", live frame";
                            if (body.isMember("window"))  // source window: how the GUI window draws the frame
                            {
                                const Json::Value& view = body["window"];
                                what << ", as the window draws it (" << view.get("backend", "").asString() << ", picture "
                                     << view["picture"].get("width", 0).asUInt() << "x" << view["picture"].get("height", 0).asUInt()
                                     << " in a " << view["viewport"].get("width", 0).asUInt() << "x"
                                     << view["viewport"].get("height", 0).asUInt() << " viewport, pixel ratio "
                                     << view.get("device_pixel_ratio", 1.0).asDouble() << ")";
                            }
                            if (body.isMember("frame") && body.isMember("screen_window"))
                            {
                                const Json::Value& frame = body["frame"];
                                const Json::Value& window = body["screen_window"];
                                what << " of the " << frame.get("width", 0).asUInt() << "x" << frame.get("height", 0).asUInt()
                                     << " " << frame.get("mode", "").asString() << " frame, screen window "
                                     << window.get("width", 0).asUInt() << "x" << window.get("height", 0).asUInt() << " at ("
                                     << window.get("x", 0).asUInt() << "," << window.get("y", 0).asUInt() << ")";
                                if (frame.isMember("beam"))
                                {
                                    what << ", beam stopped at line " << frame["beam"].get("line", 0).asInt() << " T "
                                         << frame["beam"].get("tstate", 0).asInt()
                                         << (frame.get("partial", false).asBool() ? " (half-drawn frame)" : "");
                                }
                            }
                            if (body.get("saved", false).asBool() && body.isMember("file"))
                            {
                                out << "Screenshot saved to " << body["file"].asString() << " ("
                                    << body.get("width", 0).asUInt() << "x" << body.get("height", 0).asUInt() << " "
                                    << body.get("format", "").asString() << ", " << body.get("size", 0).asUInt() << " bytes; "
                                    << what.str() << ")";
                            }
                            else
                            {
                                out << "Screenshot " << body.get("width", 0).asUInt() << "x" << body.get("height", 0).asUInt() << " "
                                    << body.get("format", "").asString() << " (" << what.str() << ")";
                                if (!includeImage && body.isMember("data"))
                                {
                                    out << " (metadata only — " << body.get("size", 0).asUInt()
                                        << " bytes; include_image:true for pixels)";
                                    body.removeMember("data");
                                }
                            }
                            out << " [target " << idOrError << "]";
                            // The pixels as an MCP image block: the model looks at the picture instead of base64 text
                            std::string imageData;
                            if (includeImage && body.isMember("data") && body["data"].isString())
                                imageData = body["data"].asString();
                            const std::string mimeType = "image/" + body.get("format", "png").asString();
                            ToolResult result = ToolResult::Ok(out.str(), std::move(body));
                            result.imageData = std::move(imageData);
                            result.imageMimeType = mimeType;
                            done(std::move(result));
                        });
                    });
                return;
            }

            if (action == "screen_digest")
            {
                std::string suffix = "/state/screen/digest";
                if (args.isMember("banks") && args["banks"].isString() && !args["banks"].asString().empty())
                {
                    suffix += "?banks=" + args["banks"].asString();
                }
                ResolveAndForward(args, "GET", suffix, nullptr, caller, "Screen digest", AppendFields(done));
                return;
            }

            if (action == "record_presets")
            {
                caller.Call("GET", "/api/v1/recording/presets", nullptr, [done](int status, Json::Value body) {
                    if (status != 200)
                    {
                        done(ToolResult::Error(status == 0 ? "WebAPI unreachable" : DescribeErrorBody(body)));
                        return;
                    }
                    std::ostringstream out;
                    out << "Recording presets (record_start with preset: <id>):";
                    for (const Json::Value& preset : body["presets"])
                    {
                        out << "\n" << preset["id"].asString() << " - " << preset["title"].asString() << ": "
                            << preset["mode"].asString() << ", ." << preset["container"].asString();
                        if (!preset["format"].asString().empty())
                            out << ", " << preset["format"].asString();
                        if (!preset["audio"].asString().empty())
                            out << " + " << preset["audio"].asString();
                    }
                    const std::string text = out.str();
                    done(ToolResult::Ok(text, std::move(body)));
                });
                return;
            }

            if (action == "record_stop" || action == "record_status" || action == "record_pause" || action == "record_resume")
            {
                if (action == "record_status")
                {
                    ResolveAndForward(args, "GET", "/video/record/status", nullptr, caller, "Recording status", AppendFields(done));
                    return;
                }
                std::string op = action == "record_stop" ? "stop" : (action == "record_pause" ? "pause" : "resume");
                auto body = std::make_shared<Json::Value>();
                (*body)["action"] = op;
                ResolveAndForward(args, "POST", "/video/record", body.get(), caller,
                                  action == "record_stop" ? "Recording stopped" : ("Recording " + op + "d"), done);
                return;
            }

            if (action == "record_start")
            {
                bool bounded = args.isMember("frames");
                unsigned frames = bounded ? args["frames"].asUInt() : 0u;
                if (bounded && (frames < 1 || frames > 10000))
                {
                    done(ToolResult::Error("'frames' must be within [1, 10000]"));
                    return;
                }

                unsigned fixedQuantum = 0;  // 0 = auto sampling
                if (args.isMember("every_nth"))
                {
                    if (!bounded)
                    {
                        done(ToolResult::Error("every_nth requires 'frames' (bounded recording)"));
                        return;
                    }
                    if (args["every_nth"].isString() && args["every_nth"].asString() == "auto")
                    {
                        fixedQuantum = 0;
                    }
                    else if (args["every_nth"].isUInt())
                    {
                        fixedQuantum = args["every_nth"].asUInt();
                        if (fixedQuantum < 1) fixedQuantum = 1;
                        if (fixedQuantum > 8) fixedQuantum = 8;
                    }
                    else
                    {
                        done(ToolResult::Error("'every_nth' must be \"auto\" or an integer ≥ 1"));
                        return;
                    }
                }

                TargetResolver::ResolveFromArgs(args, caller, [&args, bounded, frames, fixedQuantum, &caller, done,
                                                                  progress](bool ok, const std::string& idOrError) {
                    if (!ok)
                    {
                        done(ToolResult::Error(idOrError));
                        return;
                    }
                    const std::string& id = idOrError;

                    // Builds and POSTs the /video/record start body, then runs the capture cycle
                    auto startRecording = [args, frames, &caller, id, done, progress](unsigned quantum, const std::string& quantumNote) {
                        auto body = std::make_shared<Json::Value>();
                        (*body)["action"] = "start";
                        if (quantum > 1 && !args.isMember("fps"))
                        {
                            unsigned fps = static_cast<unsigned>(std::lround(50.0 / quantum));
                            if (fps < 1) fps = 1;
                            (*body)["fps"] = fps;
                        }
                        for (const char* field : {"preset", "container", "format", "fps", "scale", "region", "filename",
                                                  "audio", "video_bitrate", "audio_bitrate", "profile", "acceleration"})
                        {
                            if (args.isMember(field) && !args[field].isNull())
                            {
                                (*body)[field] = args[field];
                            }
                        }
                        // 'mode' is the screenshot's area alias here: the recording mode comes as record_mode
                        if (args.isMember("record_mode") && args["record_mode"].isString())
                            (*body)["mode"] = args["record_mode"];

                        caller.Call("POST", Endpoint(id, "/video/record"), body.get(),
                                    [body, frames, quantum, quantumNote, &caller, id, done, progress](int status, Json::Value startResponse) mutable {
                            if (status == 409)
                            {
                                done(ToolResult::Error("Already recording — stop it first (record_stop)"));
                                return;
                            }
                            if (status < 200 || status >= 300)
                            {
                                done(ToolResult::Error("Recording start failed (HTTP " + std::to_string(status) + "): " +
                                                       DescribeErrorBody(startResponse)));
                                return;
                            }

                            // Unbounded (no 'frames'): the session stays open until record_stop
                            if (frames == 0)
                            {
                                std::ostringstream out;
                                out << "Recording started → " << startResponse.get("output", "").asString() << " ("
                                    << startResponse.get("format", "").asString();
                                if (startResponse.get("audio", false).asBool())
                                {
                                    out << " + " << startResponse.get("audio_codec", "").asString() << " audio "
                                        << startResponse.get("audio_sample_rate", 0).asUInt() << " Hz";
                                }
                                out << "); run the emulator, then record_stop [target " << id << "]";
                                done(ToolResult::Ok(out.str(), std::move(startResponse)));
                                return;
                            }

                            // Best-effort stop, then deliver an error
                            auto failWithStop = [&caller, id, done](const std::string& message) {
                                auto stopBody = std::make_shared<Json::Value>();
                                (*stopBody)["action"] = "stop";
                                caller.Call("POST", Endpoint(id, "/video/record"), stopBody.get(),
                                            [stopBody, message, done](int, Json::Value) {
                                                done(ToolResult::Error(message + " — recording stopped"));
                                            });
                            };

                            // Stops the recording and summarizes from the stop response
                            auto finish = [frames, quantum, quantumNote, &caller, id, done]() {
                                auto stopBody = std::make_shared<Json::Value>();
                                (*stopBody)["action"] = "stop";
                                caller.Call("POST", Endpoint(id, "/video/record"), stopBody.get(),
                                            [stopBody, frames, quantum, quantumNote, id, done](int status, Json::Value stopResponse) {
                                        if (status < 200 || status >= 300)
                                        {
                                            done(ToolResult::Error("Recording stop failed (HTTP " + std::to_string(status) + "): " +
                                                                   DescribeErrorBody(stopResponse)));
                                            return;
                                        }
                                        std::ostringstream out;
                                        out << "Recorded " << stopResponse.get("frames_recorded", frames).asUInt()
                                            << " frame(s)";
                                        if (stopResponse.get("audio", false).asBool())
                                        {
                                            out << " + " << stopResponse.get("audio_duration", 0.0).asDouble() << " s of "
                                                << stopResponse.get("audio_codec", "").asString() << " audio";
                                        }
                                        if (quantum > 1)
                                        {
                                            out << " (every_nth=" << quantum << " → ~" << (50.0 / quantum) << " Hz updates)";
                                        }
                                        if (!quantumNote.empty())
                                        {
                                            out << " [" << quantumNote << "]";
                                        }
                                        out << " → " << stopResponse.get("output", "").asString() << " ("
                                            << stopResponse.get("file_size", 0).asUInt64() << " bytes) [target " << id << "]";
                                        done(ToolResult::Ok(out.str(), std::move(stopResponse)));
                                    });
                            };

                            if (quantum <= 1)
                            {
                                // Plain 50 fps capture of `frames` frames
                                RunFrames(caller, id, frames, [finish, failWithStop](bool ok, Json::Value) {
                                    if (!ok)
                                    {
                                        failWithStop("Emulator did not run the requested frames");
                                        return;
                                    }
                                    finish();
                                });
                                return;
                            }

                            // Skip-cycle: capture 1 frame, skip quantum-1 static frames via recorder
                            // pause. Progress is reported per captured frame, throttled to ~20
                            // updates so long bounded recordings do not flood the stream.
                            const unsigned reportEvery = std::max(1u, frames / 20u);
                            auto remaining = std::make_shared<unsigned>(frames);
                            auto cycle = std::make_shared<std::function<void()>>();
                            // Weak self-reference, the pending call holds it (BUGS.md 2026-10-11 #7)
                            *cycle = [&caller, id, quantum, frames, reportEvery, remaining,
                                      weakSelf = std::weak_ptr<std::function<void()>>(cycle), finish, failWithStop,
                                      progress]() {
                                const auto cycle = weakSelf.lock();
                                RunFrames(caller, id, 1, [&caller, id, quantum, frames, reportEvery, remaining, cycle,
                                                          finish, failWithStop, progress](bool ok, Json::Value) {
                                    if (!ok)
                                    {
                                        failWithStop("Emulator did not run the requested frames");
                                        return;
                                    }
                                    const unsigned captured = frames - (--(*remaining));
                                    if (captured % reportEvery == 0 || *remaining == 0)
                                    {
                                        progress(captured, frames,
                                                 "captured " + std::to_string(captured) + " of " +
                                                     std::to_string(frames) + " frames");
                                    }
                                    if (*remaining == 0)
                                    {
                                        finish();
                                        return;
                                    }
                                    auto pauseBody = std::make_shared<Json::Value>();
                                    (*pauseBody)["action"] = "pause";
                                    caller.Call("POST", Endpoint(id, "/video/record"), pauseBody.get(),
                                                [pauseBody, &caller, id, quantum, remaining, cycle, finish, failWithStop](int status, Json::Value) mutable {
                                        if (status < 200 || status >= 300)
                                        {
                                            failWithStop("Recorder pause failed");
                                            return;
                                        }
                                        RunFrames(caller, id, quantum - 1, [&caller, id, remaining, cycle, finish, failWithStop](bool ok, Json::Value) {
                                            if (!ok)
                                            {
                                                failWithStop("Emulator did not run the skipped frames");
                                                return;
                                            }
                                            auto resumeBody = std::make_shared<Json::Value>();
                                            (*resumeBody)["action"] = "resume";
                                            caller.Call("POST", Endpoint(id, "/video/record"), resumeBody.get(),
                                                        [resumeBody, cycle, finish, failWithStop](int status, Json::Value) {
                                                    if (status < 200 || status >= 300)
                                                    {
                                                        failWithStop("Recorder resume failed");
                                                        return;
                                                    }
                                                    (*cycle)();
                                                });
                                        });
                                    });
                                });
                            };
                            (*cycle)();
                        });
                    };

                    if (!bounded)
                    {
                        startRecording(1, "");
                        return;
                    }

                    if (fixedQuantum > 0)
                    {
                        startRecording(fixedQuantum, "quantum given, not sampled");
                        return;
                    }

                    // every_nth:"auto" — sample digests frame-by-frame to find the quantum
                    const unsigned sampleFrames = 10;
                    const unsigned maxQuantum = 8;
                    auto digests = std::make_shared<std::vector<std::string>>();
                    auto sample = std::make_shared<std::function<void()>>();
                    // Weak self-reference, the pending call holds it (BUGS.md 2026-10-11 #7)
                    *sample = [&caller, id, sampleFrames, digests, weakSelf = std::weak_ptr<std::function<void()>>(sample),
                               startRecording]() {
                        const auto sample = weakSelf.lock();
                        caller.Call("GET", Endpoint(id, "/state/screen/digest"), nullptr,
                                    [&caller, id, sampleFrames, digests, sample, startRecording](int status, Json::Value body) mutable {
                                if (status != 200)
                                {
                                    startRecording(1, "screen digest unavailable — recording at 50 fps");
                                    return;
                                }
                                // "combined" is the endpoint's top-level digest field
                                // (per-bank "digest" values live under "banks")
                                digests->push_back(body.get("combined", "").asString());
                                if (digests->size() >= sampleFrames + 1)
                                {
                                    bool periodic = false;
                                    unsigned quantum = ComputeQuantum(*digests, maxQuantum, periodic);
                                    startRecording(quantum, periodic ? "quantum sampled over 10 frames"
                                                                     : "no periodic updates detected — recording every 8th frame");
                                    return;
                                }
                                RunFrames(caller, id, 1, [sample](bool, Json::Value) { (*sample)(); });
                            });
                    };
                    (*sample)();
                });
                return;
            }

            if (action == "audio_status")
            {
                ResolveAndForward(args, "GET", "/audio/capture/status", nullptr, caller, "Audio capture status", done);
                return;
            }

            if (action == "audio_result")
            {
                std::string suffix = "/audio/capture/result";
                if (args.isMember("wav") && args["wav"].asBool())
                {
                    suffix += "?wav=true";
                }
                ResolveAndForward(args, "GET", suffix, nullptr, caller, "Audio capture result", done);
                return;
            }

            if (action == "audio_capture")
            {
                double seconds = args.isMember("seconds") && args["seconds"].isNumeric() ? args["seconds"].asDouble() : 1.0;
                if (seconds < 0.01) seconds = 0.01;
                if (seconds > 30.0) seconds = 30.0;
                bool wantWav = args.isMember("wav") && args["wav"].asBool();
                const std::string source = args.isMember("source") && args["source"].isString() ? args["source"].asString() : "";

                TargetResolver::ResolveFromArgs(args, caller, [seconds, wantWav, source, &caller, done](bool ok, const std::string& idOrError) {
                    if (!ok)
                    {
                        done(ToolResult::Error(idOrError));
                        return;
                    }
                    const std::string& id = idOrError;

                    auto arm = std::make_shared<Json::Value>();
                    (*arm)["action"] = "start";
                    (*arm)["seconds"] = seconds;
                    if (!source.empty())
                        (*arm)["source"] = source;
                    caller.Call("POST", Endpoint(id, "/audio/capture"), arm.get(), [arm, seconds, wantWav, &caller, id, done](
                                                                                      int status, Json::Value armResponse) mutable {
                        if (status < 200 || status >= 300)
                        {
                            done(ToolResult::Error("Audio capture start failed (HTTP " + std::to_string(status) + "): " +
                                                   DescribeErrorBody(armResponse)));
                            return;
                        }

                        // ~50 frames per emulated second + 2 frames of margin
                        unsigned frames = static_cast<unsigned>(std::ceil(seconds * 50.0)) + 2;
                        RunFrames(caller, id, frames, [&caller, id, seconds, wantWav, armResponse, done](bool ran, Json::Value) mutable {
                            if (!ran)
                            {
                                done(ToolResult::Error("Emulator did not run — audio capture is armed but incomplete (poll audio_status)"));
                                return;
                            }
                            caller.Call("GET", Endpoint(id, "/audio/capture/status"), nullptr,
                                        [&caller, id, seconds, wantWav, armResponse, done](int status, Json::Value statusBody) mutable {
                                if (status == 200 && statusBody.get("complete", false).asBool())
                                {
                                    std::string suffix = "/audio/capture/result";
                                    if (wantWav)
                                    {
                                        suffix += "?wav=true";
                                    }
                                    caller.Call("GET", Endpoint(id, suffix), nullptr,
                                                [seconds, armResponse, done](int status, Json::Value result) mutable {
                                        if (status < 200 || status >= 300)
                                        {
                                            done(ToolResult::Error("Audio result fetch failed (HTTP " + std::to_string(status) +
                                                                   "): " + DescribeErrorBody(result)));
                                            return;
                                        }
                                        std::ostringstream out;
                                        out << "Audio " << result.get("duration_seconds", seconds).asDouble() << " s @ "
                                            << result.get("sample_rate", 0).asUInt64() << " Hz — dominant "
                                            << result.get("dominant_hz", 0.0).asDouble() << " Hz";
                                        if (result.isMember("left") && result["left"].isObject())
                                        {
                                            out << ", RMS L " << result["left"].get("rms", 0.0).asDouble() << " / R "
                                                << result["right"].get("rms", 0.0).asDouble();
                                        }
                                        if (result.isMember("wav_path"))
                                        {
                                            out << " — WAV: " << result["wav_path"].asString();
                                        }
                                        Json::Value structured = result;
                                        structured["armed"] = armResponse;
                                        done(ToolResult::Ok(out.str(), std::move(structured)));
                                    });
                                    return;
                                }
                                Json::Value structured = statusBody;
                                structured["armed"] = armResponse;
                                done(ToolResult::Ok("Audio capture in progress — poll audio_status", std::move(structured)));
                                return;
                            });
                        });
                    });
                });
                return;
            }

            if (action == "temporal_status" || action == "temporal_set")
            {
                Json::Value body;
                if (action == "temporal_set")
                {
                    if (!args.isMember("algorithm") || !args["algorithm"].isString())
                    {
                        done(ToolResult::Error("temporal_set needs 'algorithm': a name (e.g. mod-tpgwafsd) or \"off\""));
                        return;
                    }
                    const std::string algorithm = args["algorithm"].asString();
                    body["algorithm"] = algorithm == "off" ? std::string() : algorithm;
                }
                const bool set = action == "temporal_set";
                TargetResolver::ResolveFromArgs(args, caller, [set, body, &caller, done](bool ok, const std::string& idOrError) {
                    if (!ok)
                    {
                        done(ToolResult::Error(idOrError));
                        return;
                    }
                    auto request = std::make_shared<Json::Value>(body);
                    caller.Call(set ? "PUT" : "GET", Endpoint(idOrError, "/video/temporal"), set ? request.get() : nullptr,
                                [request, set, idOrError, done](int status, Json::Value response) {
                        if (status != 200)
                        {
                            done(ToolResult::Error(std::string(set ? "Temporal switch" : "Temporal status") + " failed (HTTP " +
                                                   std::to_string(status) + "): " + DescribeErrorBody(response)));
                            return;
                        }
                        // the text first: argument order is unspecified, and gcc moves `response` out before reading it
                        const std::string text = TemporalSummary(response) + " [target " + idOrError + "]";
                        done(ToolResult::Ok(text, std::move(response)));
                    });
                });
                return;
            }

            if (action == "window_display" || action == "window_display_set")
            {
                const bool set = action == "window_display_set";
                if (set && (!args.isMember("display") || !args["display"].isObject() || args["display"].empty()))
                {
                    done(ToolResult::Error("window_display_set needs 'display': an object of any of gpu, crt, "
                                           "crt_profile, blending, blending_frames, blending_weights"));
                    return;
                }
                const Json::Value body = set ? args["display"] : Json::Value();
                TargetResolver::ResolveFromArgs(args, caller, [set, body, &caller, done](bool ok, const std::string& idOrError) {
                    if (!ok)
                    {
                        done(ToolResult::Error(idOrError));
                        return;
                    }
                    auto request = std::make_shared<Json::Value>(body);
                    caller.Call(set ? "PUT" : "GET", Endpoint(idOrError, "/video/window"), set ? request.get() : nullptr,
                                [request, set, done](int status, Json::Value response) {
                        if (status != 200)
                        {
                            done(ToolResult::Error(std::string(set ? "Window display change" : "Window display") +
                                                   " failed (HTTP " + std::to_string(status) +
                                                   "): " + DescribeErrorBody(response)));
                            return;
                        }
                        // the text first: argument order is unspecified, and gcc moves `response` out before reading it
                        const std::string text = WindowDisplaySummary(response);
                        done(ToolResult::Ok(text, std::move(response)));
                    });
                });
                return;
            }

            if (action == "window_crt_params" || action == "window_crt_params_set" || action == "viewport" ||
                action == "viewport_set")
            {
                const bool params = action == "window_crt_params" || action == "window_crt_params_set";
                const bool set = action == "window_crt_params_set" || action == "viewport_set";
                Json::Value body;
                if (action == "window_crt_params_set")
                {
                    if (!args.isMember("crt_params") || !args["crt_params"].isObject() || args["crt_params"].empty())
                    {
                        done(ToolResult::Error("window_crt_params_set needs 'crt_params': {values: {name: number}, "
                                               "steps: {name: steps}, reset: true | name | [names]}"));
                        return;
                    }
                    body = args["crt_params"];
                }
                if (action == "viewport_set")
                {
                    if (!args.isMember("viewport") || !args["viewport"].isString())
                    {
                        done(ToolResult::Error(
                            "viewport_set needs 'viewport': full, symmetric, standard, screen_only, next or prev"));
                        return;
                    }
                    body["viewport"] = args["viewport"];
                }
                const std::string suffix = params ? "/video/window/crt_params" : "/video/viewport";
                TargetResolver::ResolveFromArgs(args, caller, [set, params, suffix, body, &caller,
                                                               done](bool ok, const std::string& idOrError) {
                    if (!ok)
                    {
                        done(ToolResult::Error(idOrError));
                        return;
                    }
                    auto request = std::make_shared<Json::Value>(body);
                    caller.Call(set ? "PUT" : "GET", Endpoint(idOrError, suffix), set ? request.get() : nullptr,
                                [request, params, done](int status, Json::Value response) {
                        if (status != 200)
                        {
                            done(ToolResult::Error(std::string(params ? "CRT parameters" : "Viewport") + " failed (HTTP " +
                                                   std::to_string(status) + "): " + DescribeErrorBody(response)));
                            return;
                        }
                        const std::string text = params ? CrtParamsSummary(response) : ViewportSummary(response);
                        done(ToolResult::Ok(text, std::move(response)));
                    });
                });
                return;
            }

            if (action == "window_record_start" || action == "window_record_frames" || action == "window_record_stop" ||
                action == "window_record_status")
            {
                Json::Value body(Json::objectValue);
                std::string route = "/video/window/record";
                std::string method = "POST";
                if (action == "window_record_start")
                {
                    const std::string path = args.isMember("filename") ? args["filename"].asString()
                                                                       : (args.isMember("path") ? args["path"].asString() : "");
                    if (path.empty())
                    {
                        done(ToolResult::Error("window_record_start needs 'filename': the .mov / .mp4 to write (server side)"));
                        return;
                    }
                    body["path"] = path;
                    for (const char* field : {"keyboard", "codec", "fps", "scale", "picture_height", "width", "height", "layout"})
                        if (args.isMember(field))
                            body[field] = args[field];
                    // The sound track is on unless switched off ("audio": false / "none" / "off")
                    if (args.isMember("audio"))
                    {
                        const Json::Value& audio = args["audio"];
                        const std::string word = audio.isString() ? audio.asString() : "";
                        body["audio"] = audio.isBool() ? audio.asBool()
                                                       : !(word == "none" || word == "off" || word == "false" || word.empty());
                    }
                }
                else if (action == "window_record_frames")
                {
                    if (!args.isMember("frames") || !args["frames"].isIntegral() || args["frames"].asInt() < 1)
                    {
                        done(ToolResult::Error("window_record_frames needs 'frames': how many frames to run and record"));
                        return;
                    }
                    body["frames"] = args["frames"];
                    route += "/frames";
                }
                else if (action == "window_record_stop")
                    route += "/stop";
                else
                    method = "GET";
                const std::string what = action;
                TargetResolver::ResolveFromArgs(args, caller, [what, route, method, body, &caller, done](bool ok, const std::string& idOrError) {
                    if (!ok)
                    {
                        done(ToolResult::Error(idOrError));
                        return;
                    }
                    auto request = std::make_shared<Json::Value>(body);
                    const std::string id = idOrError;
                    caller.Call(method, Endpoint(id, route), method == "GET" ? nullptr : request.get(),
                                [request, what, id, done](int status, Json::Value response) {
                        if (status != 200)
                        {
                            done(ToolResult::Error(what + " failed (HTTP " + std::to_string(status) +
                                                   "): " + DescribeErrorBody(response)));
                            return;
                        }
                        std::string text = WindowRecordingSummary(response);
                        if (what == "window_record_start")
                            text += "; pause, then window_record_frames {frames:N} records every frame (keys in between: "
                                    "type_input / keyboard press-release), window_record_stop to finish";
                        else if (what == "window_record_frames")
                            text = "ran " + std::to_string(response.get("ran", 0).asInt()) + " frames, " + text;
                        text += " [target " + id + "]";
                        done(ToolResult::Ok(text, std::move(response)));
                    });
                });
                return;
            }

            if (action == "vdac2_capture_start")
            {
                const std::string path = args.isMember("filename") ? args["filename"].asString()
                                                                   : (args.isMember("path") ? args["path"].asString() : "");
                if (path.empty())
                {
                    done(ToolResult::Error("vdac2_capture_start needs 'filename': the .evr file to write (server side)"));
                    return;
                }
                Json::Value body;
                body["path"] = path;
                ResolveAndForward(args, "POST", "/vdac2/capture/start", &body, caller, "VDAC2 capture started", done);
                return;
            }
            if (action == "framebuffer")
            {
                // GET /capture/framebuffer?encoding=base64 (core FramebufferExport); the pixels only on request
                const bool includeData = args.isMember("include_image") && args["include_image"].asBool();
                const std::string format = args.isMember("format") && args["format"].isString() && !args["format"].asString().empty()
                                               ? args["format"].asString()
                                               : "rgba";
                TargetResolver::ResolveFromArgs(args, caller, [includeData, format, &caller, done](bool ok, const std::string& idOrError) {
                    if (!ok)
                    {
                        done(ToolResult::Error(idOrError));
                        return;
                    }
                    caller.Call("GET", Endpoint(idOrError, "/capture/framebuffer?encoding=base64&format=" + format), nullptr,
                                [includeData, done](int status, Json::Value body) mutable {
                                    if (status != 200)
                                    {
                                        done(ToolResult::Error("Framebuffer failed (HTTP " + std::to_string(status) + "): " +
                                                               DescribeErrorBody(body)));
                                        return;
                                    }
                                    const std::string text = "Framebuffer " + std::to_string(body["width"].asUInt()) + " x " +
                                                             std::to_string(body["height"].asUInt()) + " " + body["format"].asString() +
                                                             " (" + body["encoding"].asString() + "), " +
                                                             std::to_string(body["bytes"].asUInt64()) + " bytes" +
                                                             (includeData ? "" : "; include_image:true for the base64 data");
                                    if (!includeData)
                                        body.removeMember("data");
                                    done(ToolResult::Ok(text, std::move(body)));
                                });
                });
                return;
            }
            if (action == "vdac2_capture_stop")
            {
                Json::Value body(Json::objectValue);
                ResolveAndForward(args, "POST", "/vdac2/capture/stop", &body, caller, "VDAC2 capture stopped", done);
                return;
            }
            if (action == "vdac2_capture_status")
            {
                ResolveAndForward(args, "GET", "/vdac2/capture/status", nullptr, caller, "VDAC2 capture status", done);
                return;
            }

            done(ToolResult::Error("Unknown action '" + action +
                                            "'. Valid: screenshot, screen_digest, record_start, record_stop, record_status, "
                                            "record_pause, record_resume, audio_capture, audio_status, audio_result, "
                                            "temporal_status, temporal_set, window_display, window_display_set, window_crt_params, "
                                            "window_crt_params_set, viewport, viewport_set, window_record_start, window_record_frames, "
                                            "window_record_stop, window_record_status, vdac2_capture_start, vdac2_capture_stop, framebuffer, "
                                            "vdac2_capture_status"));
        });
}

} // namespace

void RegisterCaptureMedia(ToolRegistry& registry)
{
    RegisterCaptureMediaImpl(registry);
}

/// endregion </capture_media>

} // namespace mcp
