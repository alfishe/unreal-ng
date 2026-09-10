#pragma once

// Shared plumbing for smart-tool implementation files
//
// Endpoint building, error formatting, register summaries, sequential series
// execution and the resolve-and-forward pattern — the common vocabulary every
// smart tool (core 5 and Phase 2) is written against. Drogon-free (jsoncpp
// only), like the rest of the tool layer.

#include "mcp-tools.h"
#include "target-resolver.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace mcp
{

/// region <Formatting>

/// Builds "/api/v1/emulator/{id}" + suffix
inline std::string Endpoint(const std::string& id, const std::string& suffix = "")
{
    return "/api/v1/emulator/" + id + suffix;
}

/// Formats a 16-bit value as 0xNNNN
inline std::string Hex16(unsigned value)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%04X", value & 0xFFFFu);
    return buffer;
}

/// Extracts a human-readable message from a WebAPI error body ({"error": ..., "message": ...})
inline std::string DescribeErrorBody(const Json::Value& body)
{
    if (!body.isObject() || body.isNull())
    {
        return "";
    }
    std::string message = body.isMember("message") ? body["message"].asString() : "";
    std::string error = body.isMember("error") ? body["error"].asString() : "";
    if (!message.empty() && !error.empty())
    {
        return error + ": " + message;
    }
    return message.empty() ? error : message;
}

/// Compact one-line register summary
inline std::string FormatRegisters(const Json::Value& registers)
{
    if (!registers.isObject() || registers.isNull())
    {
        return "registers unavailable";
    }
    std::ostringstream out;
    const Json::Value& special = registers["special"];
    const Json::Value& main = registers["main"];
    const Json::Value& index = registers["index"];

    if (special.isObject())
    {
        out << "PC=" << Hex16(special["pc"].asUInt()) << " SP=" << Hex16(special["sp"].asUInt()) << " ";
    }
    if (main.isObject())
    {
        out << "AF=" << Hex16(main["af"].asUInt()) << " BC=" << Hex16(main["bc"].asUInt()) << " DE=" << Hex16(main["de"].asUInt())
            << " HL=" << Hex16(main["hl"].asUInt()) << " ";
    }
    if (index.isObject())
    {
        out << "IX=" << Hex16(index["ix"].asUInt()) << " IY=" << Hex16(index["iy"].asUInt());
    }
    return out.str();
}

/// Formats an address argument (integer or "0x…" string) for a query string
inline std::string AddressArg(const Json::Value& value)
{
    if (value.isString())
    {
        return value.asString();
    }
    return std::to_string(value.asUInt());
}

/// Converts a numeric argument (integer or "0x…"/"$…"/decimal string) into a
/// JSON number for request bodies — WebAPI body fields are numeric-only and
/// jsoncpp throws on asUInt() of a string. Returns a null Value when the
/// string cannot be parsed; callers should surface a tool error then.
inline Json::Value NumericOrHex(const Json::Value& value)
{
    if (!value.isString())
    {
        return value;
    }
    std::string text = value.asString();
    if (!text.empty() && text[0] == '$')
    {
        text = "0x" + text.substr(1);
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 0);
    if (end == text.c_str() || *end != '\0')
    {
        return Json::Value();
    }
    return Json::Value(static_cast<Json::UInt>(parsed));
}

/// endregion </Formatting>

/// region <Sequencing>

/// Runs steps sequentially; acc accumulates structured data across steps.
/// A step calls next(false) to stop the series early (partial acc is delivered).
using SeriesStep = std::function<void(Json::Value& acc, std::function<void(bool)> next)>;

inline void RunSeries(std::vector<SeriesStep> steps, std::function<void(Json::Value acc)> done)
{
    if (steps.empty())
    {
        done(Json::Value(Json::objectValue));
        return;
    }

    auto acc = std::make_shared<Json::Value>(Json::objectValue);
    auto stepsHolder = std::make_shared<std::vector<SeriesStep>>(std::move(steps));
    auto runNext = std::make_shared<std::function<void(size_t)>>();
    *runNext = [stepsHolder, acc, done, runNext](size_t index) {
        if (index >= stepsHolder->size())
        {
            done(*acc);
            return;
        }
        (*stepsHolder)[index](*acc, [stepsHolder, acc, done, runNext, index](bool ok) {
            if (!ok)
            {
                done(*acc);
                return;
            }
            (*runNext)(index + 1);
        });
    };
    (*runNext)(0);
}

/// Wraps steps so each completed step emits one progress report on the sink —
/// (i+1) of labels.size(), message = labels[i]. Takes ownership of the steps;
/// feed the returned vector to RunSeries. The progress sink is copied, so the
/// wrappers stay valid after the enclosing handler returns.
inline std::vector<SeriesStep> ReportSeriesProgress(std::vector<SeriesStep> steps, const ProgressFn& progress,
                                                    std::vector<std::string> labels)
{
    std::vector<SeriesStep> wrapped;
    const size_t total = steps.size();
    wrapped.reserve(total);
    for (size_t i = 0; i < total; ++i)
    {
        wrapped.push_back(
            [step = std::move(steps[i]), progress, label = std::move(labels[i]), index = i, total](Json::Value& acc,
                                                                                                    std::function<void(bool)> next) {
                step(acc, [progress, label, index, total, next = std::move(next)](bool ok) {
                    progress(static_cast<double>(index + 1), static_cast<double>(total), label);
                    next(ok);
                });
            });
    }
    return wrapped;
}

/// endregion </Sequencing>

/// region <Calls>

/// Performs a WebAPI call and wraps the outcome into a ToolResult.
/// okPrefix prefixes the text summary on success; the full body goes to structuredContent.
inline void ForwardCall(const std::string& method, const std::string& path, const Json::Value* body, IApiCaller& caller,
                        const std::string& okPrefix, ToolCallback done)
{
    caller.Call(method, path, body, [okPrefix, done](int status, Json::Value responseBody) {
        if (status >= 200 && status < 300)
        {
            done(ToolResult::Ok(okPrefix, std::move(responseBody)));
            return;
        }
        if (status == 0)
        {
            done(ToolResult::Error("WebAPI unreachable — is the emulator running with WebAPI enabled (port 8090)?"));
            return;
        }
        std::string details = DescribeErrorBody(responseBody);
        std::string text = "WebAPI returned HTTP " + std::to_string(status);
        if (!details.empty())
        {
            text += ": " + details;
        }
        done(ToolResult::Error(text));
    });
}

/// Runs N emulated frames (POST /run_frames) and continues with (ok, body)
inline void RunFrames(IApiCaller& caller, const std::string& id, unsigned frames,
                      std::function<void(bool ok, Json::Value body)> done)
{
    auto body = std::make_shared<Json::Value>();
    (*body)["frames"] = frames;
    caller.Call("POST", Endpoint(id, "/run_frames"), body.get(), [body, done = std::move(done)](int status, Json::Value responseBody) {
        done(status >= 200 && status < 300, std::move(responseBody));
    });
}

/// Resolves the target from args and forwards a simple call to /api/v1/emulator/{id}<suffix>.
/// The body is copied so it stays alive across the asynchronous target resolution.
inline void ResolveAndForward(const Json::Value& args, const std::string& method, const std::string& suffix, const Json::Value* body,
                              IApiCaller& caller, const std::string& okPrefix, ToolCallback done)
{
    auto bodyCopy = body != nullptr ? std::make_shared<Json::Value>(*body) : std::shared_ptr<Json::Value>();
    TargetResolver::ResolveFromArgs(args, caller, [method, suffix, bodyCopy, &caller, okPrefix, done](bool ok, const std::string& idOrError) {
        if (!ok)
        {
            done(ToolResult::Error(idOrError));
            return;
        }
        ForwardCall(method, Endpoint(idOrError, suffix), bodyCopy.get(), caller, okPrefix + " [target " + idOrError + "]", done);
    });
}

/// endregion </Calls>

} // namespace mcp
