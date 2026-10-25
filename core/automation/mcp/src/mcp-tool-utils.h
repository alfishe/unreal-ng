#pragma once

// Shared plumbing for smart-tool implementation files
//
// Endpoint building, error formatting, register summaries, sequential series
// execution and the resolve-and-forward pattern — the common vocabulary every
// smart tool (core 5 and Phase 2) is written against. Drogon-free (jsoncpp
// only), like the rest of the tool layer.

#include "mcp-tools.h"
#include "target-resolver.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
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

/// Percent-encodes a path segment (RFC 3986 unreserved characters kept
/// literal). Labels are free-form text ("umt entry"), so they must not be
/// spliced raw into a URL path.
inline std::string UrlEncodeSegment(const std::string& text)
{
    static const char* kHex = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(text.size());
    for (char c : text)
    {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            encoded += c;
        }
        else
        {
            encoded += '%';
            encoded += kHex[uc >> 4];
            encoded += kHex[uc & 0xF];
        }
    }
    return encoded;
}

/// The request path the loopback client sends: the tools build it with its query already
/// percent-encoded (UrlEncodeSegment), so it must not be encoded again - drogon's default urlEncode
/// turns every '%' into "%25", and a screenshot path "scratch/a.png" arrived as the file name
/// "scratch%2Fa.png". Keeps drogon's literal characters plus '%' and escapes the rest as drogon
/// does ('#' in "#5800", '+', blanks), for callers that splice raw text
inline std::string PrepareRequestPath(const std::string& path)
{
    static const char* kHex = "0123456789ABCDEF";
    static const std::string kLiteral = "-_.!~*'()&=/\\?%";
    std::string prepared;
    prepared.reserve(path.size());
    for (const char c : path)
    {
        const auto uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || kLiteral.find(c) != std::string::npos)
        {
            prepared += c;
        }
        else
        {
            prepared += '%';
            prepared += kHex[uc >> 4];
            prepared += kHex[uc & 0x0F];
        }
    }
    return prepared;
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

/// The automation gate refused the call (execution arbiter design §6a): another client's session holds the machine, or
/// the user took control back. Never "already running" / "pause first"
inline bool IsSessionRefusal(const Json::Value& body)
{
    if (!body.isObject())
        return false;
    const std::string error = body.get("error", "").asString();
    return error == "held_by_other" || error == "revoked_by_user" || error == "revoked_cooldown";
}

/// Compact one-line register summary
inline std::string FormatRegisters(const Json::Value& registers, bool full = false)
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
    if (registers["interrupt"]["halted"].asBool())
        out << " HALT";
    // The rest a debugger shows: the alternate set, I, R, the interrupt mode and flip-flops, MEMPTR
    const Json::Value& alternate = registers["alternate"];
    const Json::Value& interrupt = registers["interrupt"];
    if (full && alternate.isObject())
        out << "\nAF'=" << Hex16(alternate["af_"].asUInt()) << " BC'=" << Hex16(alternate["bc_"].asUInt())
            << " DE'=" << Hex16(alternate["de_"].asUInt()) << " HL'=" << Hex16(alternate["hl_"].asUInt());
    if (full && special.isObject())
    {
        char ir[24];
        std::snprintf(ir, sizeof(ir), " I=0x%02X R=0x%02X", special["i"].asUInt() & 0xFFu, special["r"].asUInt() & 0xFFu);
        out << ir << " MEMPTR=" << Hex16(special["memptr"].asUInt());
    }
    if (full && interrupt.isObject())
        out << " IM " << interrupt["im"].asUInt() << " IFF1=" << interrupt["iff1"].asUInt() << " IFF2="
            << interrupt["iff2"].asUInt();
    return out.str();
}

/// A small structured result as one line of key=value fields for the text ("model=Pentagon ram_kb=128
/// port_0x7FFD.value_hex=0x10"): clients that hand the model only content[] see the values. Nested keys are joined
/// with '.', arrays of scalars as [a,b]; arrays of objects as key[i].field. Past `limit` characters the rest is
/// counted, not shown
inline std::string FlatFields(const Json::Value& value, size_t limit = 1500)
{
    std::string text;
    size_t skipped = 0;
    std::function<void(const std::string&, const Json::Value&)> walk = [&](const std::string& key, const Json::Value& v) {
        if (v.isObject())
        {
            for (const std::string& name : v.getMemberNames())
                walk(key.empty() ? name : key + "." + name, v[name]);
            return;
        }
        std::string field;
        if (v.isArray())
        {
            bool scalars = true;
            for (const Json::Value& item : v)
                scalars = scalars && !item.isObject() && !item.isArray();
            if (!scalars)
            {
                for (Json::ArrayIndex i = 0; i < v.size(); ++i)
                    walk(key + "[" + std::to_string(i) + "]", v[i]);
                return;
            }
            field = key + "=[";
            for (Json::ArrayIndex i = 0; i < v.size(); ++i)
                field += (i ? "," : "") + v[i].asString();
            field += "]";
        }
        else if (v.isNull())
            field = key + "=null";
        else if (v.isString())
            field = key + "=" + (v.asString().find(' ') == std::string::npos ? v.asString() : "\"" + v.asString() + "\"");
        else if (v.isDouble())
        {
            char number[32];
            std::snprintf(number, sizeof(number), "%.10g", v.asDouble());
            field = key + "=" + number;
        }
        else
            field = key + "=" + v.asString();
        field.erase(std::remove(field.begin(), field.end(), '\n'), field.end());
        if (text.size() + field.size() + 1 > limit)
        {
            ++skipped;
            return;
        }
        text += (text.empty() ? "" : " ") + field;
    };
    walk("", value);
    if (skipped)
        text += " ... " + std::to_string(skipped) + " more field(s) (response_format json)";
    return text;
}

/// A GET /disasm body ({address, instructions[{address, bytes, mnemonic, label?, target?, targetLabel?,
/// effectiveAddress?, effectiveAddressLabel?}]}) as a listing, one instruction per line:
///   "label:" line, then "7396  CD 00 74     call #7400      ; -> label"
inline std::string FormatDisasmListing(const Json::Value& body)
{
    std::ostringstream out;
    if (!body.isMember("instructions") || !body["instructions"].isArray())
        return out.str();
    for (const Json::Value& line : body["instructions"])
    {
        if (line.isMember("label"))
            out << line["label"].asString() << ":\n";
        char head[8];
        std::snprintf(head, sizeof(head), "%04X", line["address"].asUInt() & 0xFFFFu);
        std::string bytes;
        const std::string hex = line["bytes"].asString();
        for (size_t i = 0; i + 1 < hex.size(); i += 2)
        {
            if (!bytes.empty())
                bytes += ' ';
            bytes += hex.substr(i, 2);
        }
        bytes.resize(std::max<size_t>(bytes.size(), 12), ' ');
        std::string mnemonic = line["mnemonic"].asString();
        std::string note;
        // The mnemonic names a label itself ("jp START-NEW (#11CB)"); "rst #38" does not
        if (line.isMember("targetLabel") && mnemonic.find(line["targetLabel"].asString()) == std::string::npos)
            note = "-> " + line["targetLabel"].asString();
        if (line.isMember("effectiveAddress"))
        {
            char ea[8];
            std::snprintf(ea, sizeof(ea), "%04X", line["effectiveAddress"].asUInt() & 0xFFFFu);
            note += std::string(note.empty() ? "" : ", ") + "ea #" + ea;
            if (line.isMember("effectiveAddressLabel"))
                note += " " + line["effectiveAddressLabel"].asString();
        }
        if (!note.empty())
            mnemonic.resize(std::max<size_t>(mnemonic.size(), 20), ' ');
        out << head << "  " << bytes << "  " << mnemonic;
        if (!note.empty())
            out << "  ; " << note;
        out << "\n";
    }
    std::string text = out.str();
    if (!text.empty() && text.back() == '\n')
        text.pop_back();
    return text;
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
    // The chain refers to itself weakly; a pending step's continuation holds it. A strong self-capture is a cycle:
    // the closure and all it captured were never freed (BUGS.md 2026-10-11 #7)
    auto runNext = std::make_shared<std::function<void(size_t)>>();
    *runNext = [stepsHolder, acc, done, weakSelf = std::weak_ptr<std::function<void(size_t)>>(runNext)](size_t index) {
        if (index >= stepsHolder->size())
        {
            done(*acc);
            return;
        }
        const auto runNext = weakSelf.lock();
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

/// Wraps a tool's done: a successful result gets its structured fields appended to the text as key=value
/// (FlatFields), for the calls whose text is a headline only ("Joystick status")
inline ToolCallback AppendFields(ToolCallback done)
{
    return [done](ToolResult result) {
        if (!result.isError && result.structured.isObject() && !result.structured.empty())
            result.text += "\n" + FlatFields(result.structured);
        done(std::move(result));
    };
}

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

/// Performs a WebAPI call with raw binary body and wraps the outcome into a ToolResult
inline void ForwardCallRaw(const std::string& method, const std::string& path,
                           const std::vector<uint8_t>& body,
                           const std::map<std::string, std::string>& headers,
                           IApiCaller& caller, const std::string& okPrefix, ToolCallback done)
{
    caller.CallRaw(method, path, body, headers, [okPrefix, done](int status, Json::Value responseBody) {
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

/// endregion </Calls>

} // namespace mcp
