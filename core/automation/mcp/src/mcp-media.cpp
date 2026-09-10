// MCP smart tool: capture_media
//
// Actions and their WebAPI mappings:
//   screenshot     → GET  /capture/screen?format=&mode=   (base64 stripped unless include_image)
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
//
// Drogon-free; all calls go through the loopback IApiCaller.

#include "mcp-media.h"

#include "mcp-tool-utils.h"

#include <algorithm>
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

void RegisterCaptureMediaImpl(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["action"]["type"] = "string";
    schema["properties"]["action"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* action : {"screenshot", "screen_digest", "record_start", "record_stop", "record_status", "record_pause",
                               "record_resume", "audio_capture", "audio_status", "audio_result"})
    {
        schema["properties"]["action"]["enum"].append(action);
    }
    schema["properties"]["action"]["description"] = "Media operation: still capture, digest, video recording session, or audio capture.";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["properties"]["format"]["type"] = "string";
    schema["properties"]["format"]["description"] = "screenshot: png|gif (default png). record_start: gif (native) or h264/h265/hevc/vp9/rawvideo (default gif)";
    schema["properties"]["mode"]["type"] = "string";
    schema["properties"]["mode"]["enum"] = Json::Value(Json::arrayValue);
    schema["properties"]["mode"]["enum"].append("screen");
    schema["properties"]["mode"]["enum"].append("full");
    schema["properties"]["mode"]["description"] = "screenshot capture region: 256x192 screen only or full border area";
    schema["properties"]["include_image"]["type"] = "boolean";
    schema["properties"]["include_image"]["default"] = false;
    schema["properties"]["include_image"]["description"] = "Include the base64 image payload for screenshot (large; metadata only by default)";
    schema["properties"]["banks"]["type"] = "string";
    schema["properties"]["banks"]["description"] = "Physical RAM banks for screen_digest, e.g. \"5,7\" (default: model screen banks)";
    schema["properties"]["fps"]["type"] = "integer";
    schema["properties"]["fps"]["description"] = "Recording fps (1-100). With every_nth:Q the default is 50/Q so playback is real-time";
    schema["properties"]["scale"]["type"] = "integer";
    schema["properties"]["scale"]["description"] = "Integer upscale for recording (1-4, default 1)";
    schema["properties"]["region"]["type"] = "string";
    schema["properties"]["region"]["enum"] = Json::Value(Json::arrayValue);
    schema["properties"]["region"]["enum"].append("full");
    schema["properties"]["region"]["enum"].append("screen");
    schema["properties"]["region"]["enum"].append("main");
    schema["properties"]["region"]["description"] = "Recording capture region (default full)";
    schema["properties"]["filename"]["type"] = "string";
    schema["properties"]["filename"]["description"] = "Output file for recording (default: scratch-dir video-<id>-<stamp>.<ext>)";
    schema["properties"]["every_nth"]["description"] =
        "\"auto\" or integer ≥1 (requires frames): measure the effect's visual update quantum Q via screen digests "
        "(auto) or use Q directly; the recorder is paused across static frames and fps defaults to 50/Q";
    schema["properties"]["frames"]["type"] = "integer";
    schema["properties"]["frames"]["description"] = "Bounded recording: captured frames before automatic stop (1-10000)";
    schema["properties"]["seconds"]["type"] = "number";
    schema["properties"]["seconds"]["default"] = 1.0;
    schema["properties"]["seconds"]["description"] = "audio_capture duration in emulated seconds (0.01-30)";
    schema["properties"]["wav"]["type"] = "boolean";
    schema["properties"]["wav"]["default"] = false;
    schema["properties"]["wav"]["description"] = "Export a WAV file alongside the audio analysis (returns wav_path)";
    schema["required"].append("action");

    registry.Register(
        "capture_media",
        "Media capture: screenshots (PNG/GIF with OCR-friendly metadata), deterministic screen digests, video recording "
        "(GIF native) with optional every_nth:'auto' visual-quantum detection for duplicate-free bounded recordings, and "
        "one-shot audio capture with DSP analysis (dominant frequency, RMS/peak, optional WAV export).",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn& progress) {
            std::string action = args["action"].asString();

            if (action == "screenshot")
            {
                bool includeImage = args.isMember("include_image") && args["include_image"].asBool();
                TargetResolver::ResolveFromArgs(
                    args, caller, [&args, includeImage, &caller, done](bool ok, const std::string& idOrError) {
                        if (!ok)
                        {
                            done(ToolResult::Error(idOrError));
                            return;
                        }
                        std::string suffix = "/capture/screen";
                        std::string query;
                        if (args.isMember("format") && args["format"].isString() && !args["format"].asString().empty())
                        {
                            query += "format=" + args["format"].asString() + "&";
                        }
                        if (args.isMember("mode") && args["mode"].isString() && !args["mode"].asString().empty())
                        {
                            query += "mode=" + args["mode"].asString() + "&";
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
                            out << "Screenshot " << body.get("width", 0).asUInt() << "x" << body.get("height", 0).asUInt() << " "
                                << body.get("format", "").asString();
                            if (!includeImage && body.isMember("data"))
                            {
                                out << " (metadata only — " << body.get("size", 0).asUInt()
                                    << " bytes; include_image:true for pixels)";
                                body.removeMember("data");
                            }
                            out << " [target " << idOrError << "]";
                            done(ToolResult::Ok(out.str(), std::move(body)));
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
                ResolveAndForward(args, "GET", suffix, nullptr, caller, "Screen digest", done);
                return;
            }

            if (action == "record_stop" || action == "record_status" || action == "record_pause" || action == "record_resume")
            {
                if (action == "record_status")
                {
                    ResolveAndForward(args, "GET", "/video/record/status", nullptr, caller, "Recording status", done);
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
                        for (const char* field : {"format", "fps", "scale", "region", "filename"})
                        {
                            if (args.isMember(field) && !args[field].isNull())
                            {
                                (*body)[field] = args[field];
                            }
                        }

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
                                        out << "Recorded " << stopResponse["stats"].get("framesRecorded", frames).asUInt()
                                            << " frame(s)";
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
                            *cycle = [&caller, id, quantum, frames, reportEvery, remaining, cycle, finish, failWithStop,
                                      progress]() {
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
                    *sample = [&caller, id, sampleFrames, digests, sample, startRecording]() {
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

                TargetResolver::ResolveFromArgs(args, caller, [seconds, wantWav, &caller, done](bool ok, const std::string& idOrError) {
                    if (!ok)
                    {
                        done(ToolResult::Error(idOrError));
                        return;
                    }
                    const std::string& id = idOrError;

                    auto arm = std::make_shared<Json::Value>();
                    (*arm)["action"] = "start";
                    (*arm)["seconds"] = seconds;
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

            done(ToolResult::Error("Unknown action '" + action +
                                            "'. Valid: screenshot, screen_digest, record_start, record_stop, record_status, "
                                            "record_pause, record_resume, audio_capture, audio_status, audio_result"));
        });
}

} // namespace

void RegisterCaptureMedia(ToolRegistry& registry)
{
    RegisterCaptureMediaImpl(registry);
}

/// endregion </capture_media>

} // namespace mcp
