// OpenAPI Specification Handler
// Extracted from emulator_api.cpp - 2026-01-08
// Split into modular .inc files - 2026-02-07

#include <drogon/HttpResponse.h>
#include <json/json.h>

#include "emulator_api.h"

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper function defined in emulator_api.cpp
void addCorsHeaders(drogon::HttpResponsePtr& resp);

/// @brief GET /api/v1/openapi.json
/// @brief OpenAPI 3.0 specification
///
/// IMPORTANT: This OpenAPI specification is MANUALLY MAINTAINED and NOT auto-generated.
/// Any changes to API endpoints, parameters, or responses MUST be manually reflected here.
/// Failure to update this specification will result in documentation being out of sync
/// with the actual API implementation.
void EmulatorAPI::getOpenAPISpec(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback) const
{
    Json::Value spec;

    // OpenAPI version and info
    spec["openapi"] = "3.0.0";
    spec["info"]["title"] = "Unreal Speccy Emulator API";
    spec["info"]["description"] = "REST API for controlling and inspecting ZX Spectrum emulator instances";
    spec["info"]["version"] = "1.0.0";

    // Servers
    Json::Value servers(Json::arrayValue);
    Json::Value server;
    server["url"] = "http://localhost:8090";
    server["description"] = "Local development server";
    servers.append(server);
    spec["servers"] = servers;

    // Tags - defines the order of tag groups in Swagger UI
    Json::Value tags(Json::arrayValue);
    Json::Value tagEmulatorMgmt;
    tagEmulatorMgmt["name"] = "Emulator Management";
    tagEmulatorMgmt["description"] = "Emulator lifecycle and information";
    tags.append(tagEmulatorMgmt);
    Json::Value tagEmulatorCtrl;
    tagEmulatorCtrl["name"] = "Emulator Control";
    tagEmulatorCtrl["description"] = "Control emulator execution state";
    tags.append(tagEmulatorCtrl);
    Json::Value tagSettings;
    tagSettings["name"] = "Settings Management";
    tagSettings["description"] = "Emulator configuration and settings";
    tags.append(tagSettings);
    Json::Value tagFeatures;
    tagFeatures["name"] = "Feature Management";
    tagFeatures["description"] = "Runtime feature control";
    tags.append(tagFeatures);
    Json::Value tagTapeCtrl;
    tagTapeCtrl["name"] = "Tape Control";
    tagTapeCtrl["description"] = "Tape image control and playback";
    tags.append(tagTapeCtrl);
    Json::Value tagDiskCtrl;
    tagDiskCtrl["name"] = "Disk Control";
    tagDiskCtrl["description"] = "Disk image management";
    tags.append(tagDiskCtrl);
    Json::Value tagDiskInsp;
    tagDiskInsp["name"] = "Disk Inspection";
    tagDiskInsp["description"] = "Low-level disk data inspection";
    tags.append(tagDiskInsp);
    Json::Value tagSnapshotCtrl;
    tagSnapshotCtrl["name"] = "Snapshot Control";
    tagSnapshotCtrl["description"] = "Snapshot file loading and status";
    tags.append(tagSnapshotCtrl);
    Json::Value tagCapture;
    tagCapture["name"] = "Capture";
    tagCapture["description"] = "Screen capture and OCR";
    tags.append(tagCapture);
    Json::Value tagBasicCtrl;
    tagBasicCtrl["name"] = "BASIC Control";
    tagBasicCtrl["description"] = "BASIC program manipulation";
    tags.append(tagBasicCtrl);
    Json::Value tagKeyboard;
    tagKeyboard["name"] = "Keyboard Injection";
    tagKeyboard["description"] = "Keyboard input simulation";
    tags.append(tagKeyboard);
    Json::Value tagMemState;
    tagMemState["name"] = "Memory State";
    tagMemState["description"] = "Memory inspection (RAM/ROM)";
    tags.append(tagMemState);
    Json::Value tagScreenState;
    tagScreenState["name"] = "Screen State";
    tagScreenState["description"] = "Screen/video state inspection";
    tags.append(tagScreenState);
    Json::Value tagAudioState;
    tagAudioState["name"] = "Audio State";
    tagAudioState["description"] = "Audio hardware state";
    tags.append(tagAudioState);
    Json::Value tagAnalyzer;
    tagAnalyzer["name"] = "Analyzer Management";
    tagAnalyzer["description"] = "Control analyzer modules";
    tags.append(tagAnalyzer);
    Json::Value tagDebug;
    tagDebug["name"] = "Debug Commands";
    tagDebug["description"] = "Breakpoints, registers, and debugging";
    tags.append(tagDebug);
    Json::Value tagMemProfiler;
    tagMemProfiler["name"] = "Memory Profiler";
    tagMemProfiler["description"] = "Track memory access patterns";
    tags.append(tagMemProfiler);
    Json::Value tagCallTrace;
    tagCallTrace["name"] = "Call Trace Profiler";
    tagCallTrace["description"] = "Track CALL/RET/JP/JR/RST events";
    tags.append(tagCallTrace);
    Json::Value tagOpcodeProfiler;
    tagOpcodeProfiler["name"] = "Opcode Profiler";
    tagOpcodeProfiler["description"] = "Z80 opcode execution profiling";
    tags.append(tagOpcodeProfiler);
    Json::Value tagUnifiedProfiler;
    tagUnifiedProfiler["name"] = "Unified Profiler";
    tagUnifiedProfiler["description"] = "Control all profilers simultaneously";
    tags.append(tagUnifiedProfiler);
    Json::Value tagInterpreter;
    tagInterpreter["name"] = "Interpreter Control";
    tagInterpreter["description"] = "Python and Lua interpreter management";
    tags.append(tagInterpreter);

    spec["tags"] = tags;

    // Paths - split into domain-specific includes
    Json::Value paths;

#include "openapi/openapi_interpreter.inc"
#include "openapi/openapi_lifecycle.inc"
#include "openapi/openapi_tape_disk.inc"
#include "openapi/openapi_snapshot.inc"
#include "openapi/openapi_capture.inc"
#include "openapi/openapi_basic.inc"
#include "openapi/openapi_keyboard.inc"
#include "openapi/openapi_settings.inc"
#include "openapi/openapi_features.inc"
#include "openapi/openapi_state.inc"
#include "openapi/openapi_analyzers.inc"
#include "openapi/openapi_debug.inc"
#include "openapi/openapi_profiler.inc"

    spec["paths"] = paths;

    // Components/Schemas
    Json::Value schemas;

#include "openapi/openapi_schemas.inc"

    spec["components"]["schemas"] = schemas;

    auto resp = HttpResponse::newHttpJsonResponse(spec);
    addCorsHeaders(resp);
    callback(resp);
}

}  // namespace v1
}  // namespace api
