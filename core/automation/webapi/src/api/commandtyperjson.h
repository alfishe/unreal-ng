#pragma once

#include <json/json.h>

#include <drogon/HttpResponse.h>

#include "debugger/analyzers/basic-lang/commandtyper.h"

/// JSON for a verified command input result (basic/run, keyboard/type with
/// tokenized): what the ROM did, why it failed, and on request the cyclogram
/// (debugger/analyzers/basic-lang/input-verification.md).
inline Json::Value CommandTyperResultToJson(const CommandTyper::Result& result, bool includeTrace)
{
    using ROMControlPoints::RomKind;

    Json::Value json;
    json["success"] = CommandTyper::Succeeded(result);
    json["outcome"] = CommandTyper::OutcomeName(result.outcome);
    if (result.failure != CommandTyper::Failure::None)
        json["failure"] = CommandTyper::FailureName(result.failure);
    json["message"] = result.message;
    json["editor"] = ROMControlPoints::RomName(result.editor);
    json["basic_mode"] = result.trdos ? "trdos"
                         : result.editor == RomKind::Basic48   ? "48K"
                         : result.editor == RomKind::Editor128 ? "128K"
                                                               : "unknown";
    json["err_nr"] = result.errNr;
    json["report"] = static_cast<uint8_t>(result.errNr + 1);
    json["bytes_typed"] = static_cast<Json::UInt64>(result.bytesTyped);
    json["frames"] = static_cast<Json::UInt64>(result.frames);

    if (includeTrace)
    {
        Json::Value trace(Json::arrayValue);
        for (const EditorMonitor::Event& e : result.cyclogram)
        {
            Json::Value event;
            event["frame"] = static_cast<Json::UInt64>(e.frame);
            event["tstate"] = e.tstate;
            event["point"] = ROMControlPoints::PointName(e.point);
            event["rom"] = ROMControlPoints::RomName(e.rom);
            event["pc"] = e.pc;
            event["a"] = e.a;
            event["last_k"] = e.lastK;
            event["err_nr"] = e.errNr;
            if (e.repeats)
                event["repeats"] = e.repeats;
            trace.append(event);
        }
        json["cyclogram"] = trace;
    }
    return json;
}

/// HTTP status for a result: 200 when the ROM did what was asked, 409 when the
/// machine was not in a state to take input, 422 when the ROM refused it
inline drogon::HttpStatusCode CommandTyperHttpStatus(const CommandTyper::Result& result)
{
    using Failure = CommandTyper::Failure;
    if (CommandTyper::Succeeded(result))
        return drogon::k200OK;
    switch (result.failure)
    {
        case Failure::InputLocked:
        case Failure::KeyboardBusy:
        case Failure::Busy:
        case Failure::EmulatorPaused:
        case Failure::TimedOut:
            return drogon::k409Conflict;
        case Failure::UnknownTarget:
        case Failure::Unsupported:
            return drogon::k400BadRequest;
        default:
            return drogon::k422UnprocessableEntity;
    }
}
