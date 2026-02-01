// OpenAPI Specification Handler
// Extracted from emulator_api.cpp - 2026-01-08

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
extern void addCorsHeaders(drogon::HttpResponsePtr& resp);

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
    Json::Value tagEmulatorMgmt; tagEmulatorMgmt["name"] = "Emulator Management"; tagEmulatorMgmt["description"] = "Emulator lifecycle and information"; tags.append(tagEmulatorMgmt);
    Json::Value tagEmulatorCtrl; tagEmulatorCtrl["name"] = "Emulator Control"; tagEmulatorCtrl["description"] = "Control emulator execution state"; tags.append(tagEmulatorCtrl);
    Json::Value tagSettings; tagSettings["name"] = "Settings Management"; tagSettings["description"] = "Emulator configuration and settings"; tags.append(tagSettings);
    Json::Value tagFeatures; tagFeatures["name"] = "Feature Management"; tagFeatures["description"] = "Runtime feature control"; tags.append(tagFeatures);
    Json::Value tagTapeCtrl; tagTapeCtrl["name"] = "Tape Control"; tagTapeCtrl["description"] = "Tape image control and playback"; tags.append(tagTapeCtrl);
    Json::Value tagDiskCtrl; tagDiskCtrl["name"] = "Disk Control"; tagDiskCtrl["description"] = "Disk image management"; tags.append(tagDiskCtrl);
    Json::Value tagDiskInsp; tagDiskInsp["name"] = "Disk Inspection"; tagDiskInsp["description"] = "Low-level disk data inspection"; tags.append(tagDiskInsp);
    Json::Value tagSnapshotCtrl; tagSnapshotCtrl["name"] = "Snapshot Control"; tagSnapshotCtrl["description"] = "Snapshot file loading and status"; tags.append(tagSnapshotCtrl);
    Json::Value tagCapture; tagCapture["name"] = "Capture"; tagCapture["description"] = "Screen capture and OCR"; tags.append(tagCapture);
    Json::Value tagBasicCtrl; tagBasicCtrl["name"] = "BASIC Control"; tagBasicCtrl["description"] = "BASIC program manipulation"; tags.append(tagBasicCtrl);
    Json::Value tagKeyboard; tagKeyboard["name"] = "Keyboard Injection"; tagKeyboard["description"] = "Keyboard input simulation"; tags.append(tagKeyboard);
    Json::Value tagMemState; tagMemState["name"] = "Memory State"; tagMemState["description"] = "Memory inspection (RAM/ROM)"; tags.append(tagMemState);
    Json::Value tagScreenState; tagScreenState["name"] = "Screen State"; tagScreenState["description"] = "Screen/video state inspection"; tags.append(tagScreenState);
    Json::Value tagAudioState; tagAudioState["name"] = "Audio State"; tagAudioState["description"] = "Audio hardware state"; tags.append(tagAudioState);
    Json::Value tagAnalyzer; tagAnalyzer["name"] = "Analyzer Management"; tagAnalyzer["description"] = "Control analyzer modules"; tags.append(tagAnalyzer);
    Json::Value tagDebug; tagDebug["name"] = "Debug Commands"; tagDebug["description"] = "Breakpoints, registers, and debugging"; tags.append(tagDebug);
    Json::Value tagMemProfiler; tagMemProfiler["name"] = "Memory Profiler"; tagMemProfiler["description"] = "Track memory access patterns"; tags.append(tagMemProfiler);
    Json::Value tagCallTrace; tagCallTrace["name"] = "Call Trace Profiler"; tagCallTrace["description"] = "Track CALL/RET/JP/JR/RST events"; tags.append(tagCallTrace);
    Json::Value tagOpcodeProfiler; tagOpcodeProfiler["name"] = "Opcode Profiler"; tagOpcodeProfiler["description"] = "Z80 opcode execution profiling"; tags.append(tagOpcodeProfiler);
    Json::Value tagBatchExec; tagBatchExec["name"] = "Batch Execution"; tagBatchExec["description"] = "Execute multiple commands in parallel across emulator instances"; tags.append(tagBatchExec);
    Json::Value tagUnifiedProfiler; tagUnifiedProfiler["name"] = "Unified Profiler"; tagUnifiedProfiler["description"] = "Control all profilers simultaneously"; tags.append(tagUnifiedProfiler);
    spec["tags"] = tags;

    // Paths
    Json::Value paths;

    // GET /api/v1/emulator
    paths["/api/v1/emulator"]["get"]["summary"] = "List all emulators";
    paths["/api/v1/emulator"]["get"]["tags"].append("Emulator Management");
    paths["/api/v1/emulator"]["get"]["responses"]["200"]["description"] = "Successful response";
    paths["/api/v1/emulator"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/EmulatorList";

    // POST /api/v1/emulator/create - Create new emulator (without starting)
    paths["/api/v1/emulator/create"]["post"]["summary"] = "Create new emulator";
    paths["/api/v1/emulator/create"]["post"]["tags"].append("Emulator Control");
    paths["/api/v1/emulator/create"]["post"]["requestBody"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/CreateEmulatorRequest";
    paths["/api/v1/emulator/create"]["post"]["responses"]["201"]["description"] = "Emulator created";

    // GET /api/v1/emulator/status
    paths["/api/v1/emulator/status"]["get"]["summary"] = "Get overall emulator status";
    paths["/api/v1/emulator/status"]["get"]["tags"].append("Emulator Management");
    paths["/api/v1/emulator/status"]["get"]["responses"]["200"]["description"] = "Successful response";

    // GET /api/v1/emulator/models
    paths["/api/v1/emulator/models"]["get"]["summary"] = "Get available emulator models";
    paths["/api/v1/emulator/models"]["get"]["tags"].append("Emulator Management");
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["description"] =
        "List of available ZX Spectrum models with RAM configurations";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["type"] =
        "object";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["count"]["type"] = "integer";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["count"]["description"] = "Total number of available models";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models"]["type"] = "array";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models"]["items"]["type"] = "object";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models"]["items"]["properties"]["id"]["type"] = "integer";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models"]["items"]["properties"]["name"]["type"] = "string";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models"]["items"]["properties"]["full_name"]["type"] = "string";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models"]["items"]["properties"]["default_ram_kb"]["type"] = "integer";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models"]["items"]["properties"]["available_ram_sizes_kb"]["type"] = "array";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]
         ["models"]["items"]["properties"]["available_ram_sizes_kb"]["items"]["type"] = "integer";

    // GET /api/v1/emulator/{id}
    paths["/api/v1/emulator/{id}"]["get"]["summary"] = "Get emulator details";
    paths["/api/v1/emulator/{id}"]["get"]["tags"].append("Emulator Management");
    paths["/api/v1/emulator/{id}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}"]["get"]["parameters"][0]["description"] = "Emulator UUID or index (0-based)";
    paths["/api/v1/emulator/{id}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}"]["get"]["responses"]["200"]["description"] = "Successful response";

    // DELETE /api/v1/emulator/{id}
    paths["/api/v1/emulator/{id}"]["delete"]["summary"] = "Remove emulator";
    paths["/api/v1/emulator/{id}"]["delete"]["tags"].append("Emulator Management");
    paths["/api/v1/emulator/{id}"]["delete"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}"]["delete"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}"]["delete"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}"]["delete"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}"]["delete"]["responses"]["204"]["description"] = "Emulator removed";

    // POST /api/v1/emulator/start - Create and start a new emulator
    paths["/api/v1/emulator/start"]["post"]["tags"].append("Emulator Control");
    paths["/api/v1/emulator/start"]["post"]["summary"] = "Create and start a new emulator";
    paths["/api/v1/emulator/start"]["post"]["description"] =
        "Creates a new emulator instance and immediately starts it";
    paths["/api/v1/emulator/start"]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/emulator/start"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["symbolic_id"]["type"] = "string";
    paths["/api/v1/emulator/start"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["model"]["type"] = "string";
    paths["/api/v1/emulator/start"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]
         ["ram_size"]["type"] = "integer";
    paths["/api/v1/emulator/start"]["post"]["responses"]["201"]["description"] = "Emulator created and started";
    paths["/api/v1/emulator/start"]["post"]["responses"]["201"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/EmulatorInfo";

    // Control endpoints
    // POST /api/v1/emulator/{id}/start - Start existing emulator
    paths["/api/v1/emulator/{id}/start"]["post"]["tags"].append("Emulator Control");
    paths["/api/v1/emulator/{id}/start"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/start"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/start"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/start"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/start"]["post"]["responses"]["200"]["description"] = "Emulator started";

    paths["/api/v1/emulator/{id}/stop"]["post"]["summary"] = "Stop emulator";
    paths["/api/v1/emulator/{id}/stop"]["post"]["tags"].append("Emulator Control");
    paths["/api/v1/emulator/{id}/stop"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/stop"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/stop"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/stop"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/stop"]["post"]["responses"]["200"]["description"] = "Emulator stopped";

    paths["/api/v1/emulator/{id}/pause"]["post"]["summary"] = "Pause emulator";
    paths["/api/v1/emulator/{id}/pause"]["post"]["tags"].append("Emulator Control");
    paths["/api/v1/emulator/{id}/pause"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/pause"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/pause"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/pause"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/pause"]["post"]["responses"]["200"]["description"] = "Emulator paused";

    paths["/api/v1/emulator/{id}/resume"]["post"]["summary"] = "Resume emulator";
    paths["/api/v1/emulator/{id}/resume"]["post"]["tags"].append("Emulator Control");
    paths["/api/v1/emulator/{id}/resume"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/resume"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/resume"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/resume"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/resume"]["post"]["responses"]["200"]["description"] = "Emulator resumed";

    paths["/api/v1/emulator/{id}/reset"]["post"]["summary"] = "Reset emulator";
    paths["/api/v1/emulator/{id}/reset"]["post"]["tags"].append("Emulator Control");
    paths["/api/v1/emulator/{id}/reset"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/reset"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/reset"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/reset"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/reset"]["post"]["responses"]["200"]["description"] = "Emulator reset";

    // Tape control endpoints
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["summary"] = "Load tape image";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["tags"].append("Tape Control");
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["description"] = "Path to tape image file (.tap, .tzx)";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["responses"]["200"]["description"] = "Tape loaded successfully";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["responses"]["400"]["description"] = "Invalid path or file format";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["responses"]["404"]["description"] = "Emulator not found";

    paths["/api/v1/emulator/{id}/tape/eject"]["post"]["summary"] = "Eject tape";
    paths["/api/v1/emulator/{id}/tape/eject"]["post"]["tags"].append("Tape Control");
    paths["/api/v1/emulator/{id}/tape/eject"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/tape/eject"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/tape/eject"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/tape/eject"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/eject"]["post"]["responses"]["200"]["description"] = "Tape ejected";

    paths["/api/v1/emulator/{id}/tape/play"]["post"]["summary"] = "Start tape playback";
    paths["/api/v1/emulator/{id}/tape/play"]["post"]["tags"].append("Tape Control");
    paths["/api/v1/emulator/{id}/tape/play"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/tape/play"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/tape/play"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/tape/play"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/play"]["post"]["responses"]["200"]["description"] = "Tape playback started";

    paths["/api/v1/emulator/{id}/tape/stop"]["post"]["summary"] = "Stop tape playback";
    paths["/api/v1/emulator/{id}/tape/stop"]["post"]["tags"].append("Tape Control");
    paths["/api/v1/emulator/{id}/tape/stop"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/tape/stop"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/tape/stop"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/tape/stop"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/stop"]["post"]["responses"]["200"]["description"] = "Tape playback stopped";

    paths["/api/v1/emulator/{id}/tape/rewind"]["post"]["summary"] = "Rewind tape to beginning";
    paths["/api/v1/emulator/{id}/tape/rewind"]["post"]["tags"].append("Tape Control");
    paths["/api/v1/emulator/{id}/tape/rewind"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/tape/rewind"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/tape/rewind"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/tape/rewind"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/rewind"]["post"]["responses"]["200"]["description"] = "Tape rewound";

    paths["/api/v1/emulator/{id}/tape/info"]["get"]["summary"] = "Get tape status";
    paths["/api/v1/emulator/{id}/tape/info"]["get"]["tags"].append("Tape Control");
    paths["/api/v1/emulator/{id}/tape/info"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/tape/info"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/tape/info"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/tape/info"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/info"]["get"]["responses"]["200"]["description"] = "Tape status information";

    // Disk control endpoints
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["summary"] = "Insert disk image";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["tags"].append("Disk Control");
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["parameters"][1]["name"] = "drive";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["parameters"][1]["description"] =
        "Drive letter (A-D) or number (0-3)";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["description"] = "Path to disk image file (.trd, .scl, .fdi, .udi)";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["responses"]["200"]["description"] =
        "Disk inserted successfully";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["responses"]["400"]["description"] =
        "Invalid path, file format, or drive parameter";

    // POST /api/v1/emulator/{id}/disk/{drive}/create - Create blank disk
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["summary"] = "Create blank disk";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["tags"].append("Disk Control");
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["description"] = 
        "Create a blank, unformatted disk and insert it into the specified drive. "
        "The disk is ready for TR-DOS FORMAT command.";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["parameters"][1]["name"] = "drive";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["parameters"][1]["description"] =
        "Drive letter (A, B, C, or D)";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["type"] = "object";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["cylinders"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["cylinders"]["description"] = "Number of cylinders/tracks (40 or 80, default: 80)";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["sides"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["sides"]["description"] = "Number of sides (1 or 2, default: 2)";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["responses"]["200"]["description"] =
        "Blank disk created and inserted";
    paths["/api/v1/emulator/{id}/disk/{drive}/create"]["post"]["responses"]["400"]["description"] =
        "Invalid drive or geometry parameters";

    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["summary"] = "Eject disk";
    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["tags"].append("Disk Control");
    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["parameters"][1]["name"] = "drive";
    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/eject"]["post"]["responses"]["200"]["description"] = "Disk ejected";

    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["summary"] = "Get disk drive status";
    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["tags"].append("Disk Control");
    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["parameters"][1]["name"] = "drive";
    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["responses"]["200"]["description"] =
        "Disk drive status information";

    // Disk Inspection endpoints
    // GET /api/v1/emulator/{id}/disk - List all drives
    paths["/api/v1/emulator/{id}/disk"]["get"]["summary"] = "List all disk drives";
    paths["/api/v1/emulator/{id}/disk"]["get"]["tags"].append("Disk Inspection");
    paths["/api/v1/emulator/{id}/disk"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disk"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disk"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk"]["get"]["responses"]["200"]["description"] = 
        "List of drives with status, FDC state, and auto-selection info";

    // GET /api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec} - Read sector
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["summary"] = "Read sector data";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["tags"].append("Disk Inspection");
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][1]["name"] = "drive";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][2]["name"] = "cyl";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][2]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][2]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][3]["name"] = "side";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][3]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][3]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][3]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][4]["name"] = "sec";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][4]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][4]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][4]["description"] = "Sector number (1-based)";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["parameters"][4]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}"]["get"]["responses"]["200"]["description"] = 
        "Sector data with address mark, CRC status, and base64-encoded content";

    // GET /api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}/raw - Read raw sector
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}/raw"]["get"]["summary"] = "Read raw sector bytes";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}/raw"]["get"]["tags"].append("Disk Inspection");
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}/raw"]["get"]["description"] = 
        "Returns raw sector bytes including gaps, sync, and marks";
    paths["/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}/raw"]["get"]["responses"]["200"]["description"] = 
        "Raw sector bytes as base64";

    // GET /api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side} - Read track summary
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["summary"] = "Read track summary";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["tags"].append("Disk Inspection");
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][1]["name"] = "drive";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][2]["name"] = "cyl";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][2]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][2]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][3]["name"] = "side";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][3]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][3]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["parameters"][3]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}"]["get"]["responses"]["200"]["description"] = 
        "Track overview with sector metadata";

    // GET /api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}/raw - Read raw track
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}/raw"]["get"]["summary"] = "Read raw track bytes";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}/raw"]["get"]["tags"].append("Disk Inspection");
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}/raw"]["get"]["description"] = 
        "Returns complete 6250-byte MFM stream";
    paths["/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}/raw"]["get"]["responses"]["200"]["description"] = 
        "Raw track bytes as base64";

    // GET /api/v1/emulator/{id}/disk/{drive}/image - Full image dump
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["summary"] = "Dump entire disk image";
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["tags"].append("Disk Inspection");
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["description"] = 
        "Returns all tracks concatenated as base64. Warning: large response.";
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["parameters"][1]["name"] = "drive";
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/image"]["get"]["responses"]["200"]["description"] = 
        "Complete disk image as base64 with geometry metadata";

    // GET /api/v1/emulator/{id}/disk/{drive}/sysinfo - TR-DOS system sector
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["summary"] = "Get TR-DOS system info";
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["tags"].append("Disk Inspection");
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["description"] = 
        "Parses TR-DOS system sector (T0/S9) with disk type, label, file count, free sectors";
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["parameters"][1]["name"] = "drive";
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/sysinfo"]["get"]["responses"]["200"]["description"] = 
        "Parsed TR-DOS system sector";

    // GET /api/v1/emulator/{id}/disk/{drive}/catalog - Disk catalog
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["summary"] = "Get disk catalog";
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["tags"].append("Disk Inspection");
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["description"] = 
        "Parses TR-DOS directory (sectors 1-8) returning file names, types, sizes";
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["parameters"][1]["name"] = "drive";
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/catalog"]["get"]["responses"]["200"]["description"] = 
        "Disk file listing";

    // Snapshot control endpoints
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["summary"] = "Load snapshot file";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["tags"].append("Snapshot Control");
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["type"] = "string";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["description"] = "Path to snapshot file (.z80, .sna)";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["responses"]["200"]["description"] =
        "Snapshot loaded successfully";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["responses"]["400"]["description"] =
        "Invalid path or file format";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["responses"]["404"]["description"] = "Emulator not found";

    // POST /api/v1/emulator/{id}/snapshot/save - Save snapshot file
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["summary"] = "Save snapshot file";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["tags"].append("Snapshot Control");
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["description"] = 
        "Save current emulator state to a snapshot file (.sna format)";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["type"] = "string";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["description"] = "Path to save snapshot file (.sna)";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["force"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["force"]["description"] = "Set to true to overwrite existing file";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["responses"]["200"]["description"] =
        "Snapshot saved successfully";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["responses"]["400"]["description"] =
        "Failed to save snapshot";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["responses"]["404"]["description"] = "Emulator not found";
    paths["/api/v1/emulator/{id}/snapshot/save"]["post"]["responses"]["409"]["description"] = 
        "File already exists (use force: true to overwrite)";

    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["summary"] = "Get snapshot status";
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["tags"].append("Snapshot Control");
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["responses"]["200"]["description"] =
        "Snapshot status information";

    // Capture Commands endpoints
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["summary"] = "OCR text from screen";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["tags"].append("Capture");
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["description"] = 
        "Extract text from screen using ROM font bitmap matching (OCR). "
        "Returns 24 lines x 32 characters. Uses ZX Spectrum ROM font patterns.";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["responses"]["200"]["description"] =
        "Screen OCR result";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["rows"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["cols"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["lines"]["type"] = "array";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["text"]["type"] = "string";

    // Capture screen endpoint
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["summary"] = "Capture screen as image";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["tags"].append("Capture");
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["description"] = 
        "Capture screen as GIF or PNG image. Returns base64-encoded data.";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][1]["name"] = "format";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][1]["schema"]["enum"].append("gif");
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][1]["schema"]["enum"].append("png");
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][2]["name"] = "mode";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][2]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][2]["schema"]["enum"].append("screen");
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][2]["schema"]["enum"].append("full");
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["responses"]["200"]["description"] =
        "Screen captured successfully";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["format"]["type"] = "string";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["width"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["height"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["size"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["data"]["type"] = "string";

    // BASIC Control endpoints
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["summary"] = "Execute BASIC command";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["tags"].append("BASIC Control");
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["description"] = 
        "Inject a command into BASIC edit buffer AND execute it via simulated ENTER key. "
        "If no command specified, executes RUN. "
        "Automatically handles 128K menu navigation if needed. "
        "Returns error if TR-DOS is active or not in BASIC editor.";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["command"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["command"]["description"] = "BASIC command to execute (e.g., 'RUN', 'LIST', 'PRINT 1+1')";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["responses"]["200"]["description"] = 
        "Command injected and executed";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["success"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["success"]["description"] = "True if command was injected and executed";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["message"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["message"]["description"] = "Human-readable result or error message";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["command"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["basic_mode"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["basic_mode"]["description"] = "Detected BASIC mode: '48K', '128K', 'trdos', or 'unknown'";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["responses"]["400"]["description"] = 
        "Not in BASIC editor, TR-DOS active, or other injection error";
    paths["/api/v1/emulator/{id}/basic/run"]["post"]["responses"]["404"]["description"] = "Emulator not found";

    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["summary"] = "Inject BASIC program into memory";
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["tags"].append("BASIC Control");
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["description"] = 
        "Inject a multi-line BASIC program into memory without executing. "
        "Uses loadProgram() to tokenize and write to program area. "
        "Lines should be separated by newlines and include line numbers.";
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["program"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["program"]["description"] = "BASIC program text (e.g., '10 PRINT \"HELLO\"\\n20 GOTO 10')";
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["responses"]["200"]["description"] = 
        "Program injected successfully";
    paths["/api/v1/emulator/{id}/basic/inject"]["post"]["responses"]["400"]["description"] = 
        "Missing program parameter or injection failed";

    paths["/api/v1/emulator/{id}/basic/extract"]["get"]["summary"] = "Extract BASIC program";
    paths["/api/v1/emulator/{id}/basic/extract"]["get"]["tags"].append("BASIC Control");
    paths["/api/v1/emulator/{id}/basic/extract"]["get"]["description"] = 
        "Extract the current BASIC program from emulator memory as plain text.";
    paths["/api/v1/emulator/{id}/basic/extract"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/basic/extract"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/basic/extract"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/basic/extract"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/extract"]["get"]["responses"]["200"]["description"] = 
        "BASIC program as text";

    paths["/api/v1/emulator/{id}/basic/clear"]["post"]["summary"] = "Clear BASIC program";
    paths["/api/v1/emulator/{id}/basic/clear"]["post"]["tags"].append("BASIC Control");
    paths["/api/v1/emulator/{id}/basic/clear"]["post"]["description"] = 
        "Clear the BASIC program in memory (equivalent to NEW command).";
    paths["/api/v1/emulator/{id}/basic/clear"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/basic/clear"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/basic/clear"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/basic/clear"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/clear"]["post"]["responses"]["200"]["description"] = 
        "Program cleared";

    paths["/api/v1/emulator/{id}/basic/state"]["get"]["summary"] = "Get BASIC environment state";
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["tags"].append("BASIC Control");
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["description"] = 
        "Get the current BASIC environment state including mode (48K/128K), menu vs editor, "
        "TR-DOS state, and readiness for commands.";
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["responses"]["200"]["description"] = 
        "BASIC state information";
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["state"]["type"] = "string";
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["state"]["description"] = "State: 'basic48k', 'basic128k', 'menu128k', 'trdos_active', 'trdos_sos_call', 'unknown'";
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["in_editor"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/basic/state"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["ready_for_commands"]["type"] = "boolean";

    // Keyboard Injection endpoints
    auto addKeyboardIdParam = [&](const std::string& path, const std::string& method) {
        paths[path][method]["parameters"][0]["name"] = "id";
        paths[path][method]["parameters"][0]["in"] = "path";
        paths[path][method]["parameters"][0]["required"] = true;
        paths[path][method]["parameters"][0]["schema"]["type"] = "string";
        paths[path][method]["tags"].append("Keyboard Injection");
    };

    // POST /api/v1/emulator/{id}/keyboard/tap
    addKeyboardIdParam("/api/v1/emulator/{id}/keyboard/tap", "post");
    paths["/api/v1/emulator/{id}/keyboard/tap"]["post"]["summary"] = "Tap a single key";
    paths["/api/v1/emulator/{id}/keyboard/tap"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["key"]["type"] = "string";
    paths["/api/v1/emulator/{id}/keyboard/tap"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["frames"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/keyboard/tap"]["post"]["responses"]["200"]["description"] = "Key tapped";

    // POST /api/v1/emulator/{id}/keyboard/combo
    addKeyboardIdParam("/api/v1/emulator/{id}/keyboard/combo", "post");
    paths["/api/v1/emulator/{id}/keyboard/combo"]["post"]["summary"] = "Tap a key combo";
    paths["/api/v1/emulator/{id}/keyboard/combo"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["keys"]["type"] = "array";
    paths["/api/v1/emulator/{id}/keyboard/combo"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["keys"]["items"]["type"] = "string";
    paths["/api/v1/emulator/{id}/keyboard/combo"]["post"]["responses"]["200"]["description"] = "Combo tapped";

    // POST /api/v1/emulator/{id}/keyboard/type
    addKeyboardIdParam("/api/v1/emulator/{id}/keyboard/type", "post");
    paths["/api/v1/emulator/{id}/keyboard/type"]["post"]["summary"] = "Type text sequence";
    paths["/api/v1/emulator/{id}/keyboard/type"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["text"]["type"] = "string";
    paths["/api/v1/emulator/{id}/keyboard/type"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["delay_frames"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/keyboard/type"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["tokenized"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/keyboard/type"]["post"]["responses"]["200"]["description"] = "Text queued";

    // POST /api/v1/emulator/{id}/keyboard/macro
    addKeyboardIdParam("/api/v1/emulator/{id}/keyboard/macro", "post");
    paths["/api/v1/emulator/{id}/keyboard/macro"]["post"]["summary"] = "Execute predefined macro";
    paths["/api/v1/emulator/{id}/keyboard/macro"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["name"]["type"] = "string";
    paths["/api/v1/emulator/{id}/keyboard/macro"]["post"]["responses"]["200"]["description"] = "Macro queued";

    // POST /api/v1/emulator/{id}/keyboard/release_all
    addKeyboardIdParam("/api/v1/emulator/{id}/keyboard/release_all", "post");
    paths["/api/v1/emulator/{id}/keyboard/release_all"]["post"]["summary"] = "Release all keys";
    paths["/api/v1/emulator/{id}/keyboard/release_all"]["post"]["responses"]["200"]["description"] = "All keys released";

    // GET /api/v1/emulator/{id}/keyboard/status
    addKeyboardIdParam("/api/v1/emulator/{id}/keyboard/status", "get");
    paths["/api/v1/emulator/{id}/keyboard/status"]["get"]["summary"] = "Get keyboard status";
    paths["/api/v1/emulator/{id}/keyboard/status"]["get"]["responses"]["200"]["description"] = "Keyboard status";

    // GET /api/v1/emulator/{id}/keyboard/keys
    addKeyboardIdParam("/api/v1/emulator/{id}/keyboard/keys", "get");
    paths["/api/v1/emulator/{id}/keyboard/keys"]["get"]["summary"] = "List valid keys";
    paths["/api/v1/emulator/{id}/keyboard/keys"]["get"]["responses"]["200"]["description"] = "Validated key names";

    // Settings Management endpoints
    paths["/api/v1/emulator/{id}/settings"]["get"]["summary"] = "Get all emulator settings";
    paths["/api/v1/emulator/{id}/settings"]["get"]["tags"].append("Settings Management");
    paths["/api/v1/emulator/{id}/settings"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/settings"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/settings"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/settings"]["get"]["parameters"][0]["description"] = "Emulator UUID or index";
    paths["/api/v1/emulator/{id}/settings"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/settings"]["get"]["responses"]["200"]["description"] = "Settings list";
    paths["/api/v1/emulator/{id}/settings"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/SettingsResponse";

    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["summary"] = "Get specific setting value";
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["tags"].append("Settings Management");
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["parameters"][1]["description"] = "Setting name";
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/settings/{name}"]["get"]["responses"]["200"]["description"] = "Setting value";

    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["summary"] = "Update specific setting";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["tags"].append("Settings Management");
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["value"]["type"] = "string";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["value"]["description"] = "New setting value";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["responses"]["200"]["description"] = "Setting updated";

    // Feature Management endpoints
    paths["/api/v1/emulator/{id}/features"]["get"]["summary"] = "List all emulator features";
    paths["/api/v1/emulator/{id}/features"]["get"]["tags"].append("Feature Management");
    paths["/api/v1/emulator/{id}/features"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/features"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/features"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/features"]["get"]["parameters"][0]["description"] = "Emulator UUID or index";
    paths["/api/v1/emulator/{id}/features"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/features"]["get"]["responses"]["200"]["description"] = "Feature list";
    paths["/api/v1/emulator/{id}/features"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/FeaturesResponse";

    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["summary"] = "Get specific feature state";
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["tags"].append("Feature Management");
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["parameters"][1]["description"] =
        "Feature name (e.g., sound, breakpoints, calltrace)";
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["responses"]["200"]["description"] = "Feature state";
    paths["/api/v1/emulator/{id}/feature/{name}"]["get"]["responses"]["404"]["description"] = "Feature not found";

    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["summary"] = "Enable or disable a feature";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["tags"].append("Feature Management");
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["enabled"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["enabled"]["description"] = "True to enable, false to disable";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["responses"]["200"]["description"] = "Feature state updated";
    paths["/api/v1/emulator/{id}/feature/{name}"]["put"]["responses"]["404"]["description"] = "Feature not found";

    // Memory State endpoints
    paths["/api/v1/emulator/{id}/state/memory"]["get"]["summary"] = "Get memory overview";
    paths["/api/v1/emulator/{id}/state/memory"]["get"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/state/memory"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/memory"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/memory"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/memory"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/memory"]["get"]["responses"]["200"]["description"] = "Memory state overview";

    paths["/api/v1/emulator/{id}/state/memory/ram"]["get"]["summary"] = "Get RAM state";
    paths["/api/v1/emulator/{id}/state/memory/ram"]["get"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/state/memory/ram"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/memory/ram"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/memory/ram"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/memory/ram"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/memory/ram"]["get"]["responses"]["200"]["description"] = "RAM state details";

    paths["/api/v1/emulator/{id}/state/memory/rom"]["get"]["summary"] = "Get ROM state";
    paths["/api/v1/emulator/{id}/state/memory/rom"]["get"]["tags"].append("Memory State");
    paths["/api/v1/emulator/{id}/state/memory/rom"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/memory/rom"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/memory/rom"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/memory/rom"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/memory/rom"]["get"]["responses"]["200"]["description"] = "ROM state details";

    // Screen State endpoints
    paths["/api/v1/emulator/{id}/state/screen"]["get"]["summary"] = "Get screen state overview";
    paths["/api/v1/emulator/{id}/state/screen"]["get"]["tags"].append("Screen State");
    paths["/api/v1/emulator/{id}/state/screen"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/screen"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/screen"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/screen"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/screen"]["get"]["responses"]["200"]["description"] = "Screen state overview";

    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["summary"] = "Get screen mode details";
    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["tags"].append("Screen State");
    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["responses"]["200"]["description"] =
        "Screen mode information";

    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["summary"] = "Get flash state";
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["tags"].append("Screen State");
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["responses"]["200"]["description"] =
        "Flash state information";

    // Audio state endpoints
    paths["/api/v1/emulator/{id}/state/audio/ay"]["get"]["summary"] = "Get AY chips overview";
    paths["/api/v1/emulator/{id}/state/audio/ay"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/ay"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/ay"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/ay"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/ay"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/ay"]["get"]["responses"]["200"]["description"] = "AY chips information";

    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["summary"] = "Get specific AY chip details";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][1]["name"] = "chip";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][1]["description"] =
        "AY chip index (0-based)";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}"]["get"]["responses"]["200"]["description"] = "AY chip details";

    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["summary"] =
        "Get AY chip register details";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["schema"]["type"] =
        "string";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["name"] = "chip";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["schema"]["type"] =
        "integer";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][2]["name"] = "reg";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][2]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][2]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][2]["description"] =
        "Register number (0-15)";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][2]["schema"]["type"] =
        "integer";
    paths["/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}"]["get"]["responses"]["200"]["description"] =
        "Register details";

    paths["/api/v1/emulator/{id}/state/audio/beeper"]["get"]["summary"] = "Get beeper state";
    paths["/api/v1/emulator/{id}/state/audio/beeper"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/beeper"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/beeper"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/beeper"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/beeper"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/beeper"]["get"]["responses"]["200"]["description"] = "Beeper state";

    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["summary"] = "Get General Sound state";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/gs"]["get"]["responses"]["200"]["description"] = "GS state";

    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["summary"] = "Get Covox state";
    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/covox"]["get"]["responses"]["200"]["description"] = "Covox state";

    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["summary"] = "Get audio channels overview";
    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["responses"]["200"]["description"] =
        "Audio channels information";

    // Active emulator endpoints (no ID required)
    paths["/api/v1/emulator/state/audio/ay"]["get"]["summary"] = "Get AY chips overview (active emulator)";
    paths["/api/v1/emulator/state/audio/ay"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/ay"]["get"]["responses"]["200"]["description"] = "AY chips information";

    paths["/api/v1/emulator/state/audio/ay/{chip}"]["get"]["summary"] =
        "Get specific AY chip details (active emulator)";
    paths["/api/v1/emulator/state/audio/ay/{chip}"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/ay/{chip}"]["get"]["parameters"][0]["name"] = "chip";
    paths["/api/v1/emulator/state/audio/ay/{chip}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/state/audio/ay/{chip}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/state/audio/ay/{chip}"]["get"]["parameters"][0]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/state/audio/ay/{chip}"]["get"]["responses"]["200"]["description"] = "AY chip details";

    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["summary"] =
        "Get AY chip register details (active emulator)";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["name"] = "chip";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["schema"]["type"] =
        "integer";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["name"] = "reg";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["description"] =
        "Register number (0-15)";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["schema"]["type"] =
        "integer";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["responses"]["200"]["description"] =
        "Register details";

    paths["/api/v1/emulator/state/audio/beeper"]["get"]["summary"] = "Get beeper state (active emulator)";
    paths["/api/v1/emulator/state/audio/beeper"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/beeper"]["get"]["responses"]["200"]["description"] = "Beeper state";

    paths["/api/v1/emulator/state/audio/gs"]["get"]["summary"] = "Get GS state (active emulator)";
    paths["/api/v1/emulator/state/audio/gs"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/gs"]["get"]["responses"]["200"]["description"] = "GS state";

    paths["/api/v1/emulator/state/audio/covox"]["get"]["summary"] = "Get Covox state (active emulator)";
    paths["/api/v1/emulator/state/audio/covox"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/covox"]["get"]["responses"]["200"]["description"] = "Covox state";

    paths["/api/v1/emulator/state/audio/channels"]["get"]["summary"] = "Get audio channels (active emulator)";
    paths["/api/v1/emulator/state/audio/channels"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/channels"]["get"]["responses"]["200"]["description"] =
        "Audio channels information";

    // Batch Execution endpoints
    paths["/api/v1/batch/execute"]["post"]["summary"] = "Execute batch commands in parallel";
    paths["/api/v1/batch/execute"]["post"]["description"] =
        "Execute multiple commands across emulator instances using a 4-thread pool. ~2-3ms for 48 instances.";
    paths["/api/v1/batch/execute"]["post"]["tags"].append("Batch Execution");
    paths["/api/v1/batch/execute"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/batch/execute"]["post"]["requestBody"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/BatchExecuteRequest";
    paths["/api/v1/batch/execute"]["post"]["responses"]["200"]["description"] = "All commands succeeded";
    paths["/api/v1/batch/execute"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/BatchResult";
    paths["/api/v1/batch/execute"]["post"]["responses"]["207"]["description"] =
        "Partial success (some commands failed)";
    paths["/api/v1/batch/execute"]["post"]["responses"]["207"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/BatchResult";
    paths["/api/v1/batch/execute"]["post"]["responses"]["400"]["description"] =
        "Invalid request (missing emulator/command, command not batchable)";

    paths["/api/v1/batch/commands"]["get"]["summary"] = "List batchable commands";
    paths["/api/v1/batch/commands"]["get"]["description"] =
        "Returns list of command names that can be used in batch execution";
    paths["/api/v1/batch/commands"]["get"]["tags"].append("Batch Execution");
    paths["/api/v1/batch/commands"]["get"]["responses"]["200"]["description"] = "List of batchable commands";
    paths["/api/v1/batch/commands"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/BatchableCommandsResponse";

    // Analyzer Management endpoints
    paths["/api/v1/emulator/{id}/analyzers"]["get"]["summary"] = "List all analyzers";
    paths["/api/v1/emulator/{id}/analyzers"]["get"]["tags"].append("Analyzer Management");
    paths["/api/v1/emulator/{id}/analyzers"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/analyzers"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzers"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzers"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzers"]["get"]["responses"]["200"]["description"] =
        "List of registered analyzers with status";

    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["summary"] = "Get analyzer status";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["tags"].append("Analyzer Management");
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["parameters"][1]["description"] =
        "Analyzer name (e.g., trdos)";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["get"]["responses"]["200"]["description"] = "Analyzer status";

    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["summary"] = "Enable or disable analyzer";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["tags"].append("Analyzer Management");
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["enabled"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["enabled"]["description"] = "True to enable, false to disable";
    paths["/api/v1/emulator/{id}/analyzer/{name}"]["put"]["responses"]["200"]["description"] = "Analyzer state updated";

    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["summary"] = "Get analyzer events";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["tags"].append("Analyzer Management");
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["description"] =
        "Retrieve captured events from an analyzer. Use limit query param to control count.";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][2]["name"] = "limit";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][2]["description"] =
        "Maximum number of events to return (default: 100)";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["get"]["responses"]["200"]["description"] =
        "List of analyzer events";

    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["summary"] = "Clear analyzer events";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["tags"].append("Analyzer Management");
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/events"]["delete"]["responses"]["200"]["description"] =
        "Events cleared";

    // Analyzer session control endpoint
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["summary"] = "Control analyzer session";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["tags"].append("Analyzer Management");
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["description"] =
        "Activate or deactivate analyzer session. Activate clears event buffers for fresh capture.";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["parameters"][1]["description"] =
        "Analyzer name (e.g., trdos)";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["action"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["action"]["enum"].append("activate");
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["action"]["enum"].append("deactivate");
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["action"]["description"] = "Session action: activate or deactivate";
    paths["/api/v1/emulator/{id}/analyzer/{name}/session"]["post"]["responses"]["200"]["description"] =
        "Session action completed";

    // Raw FDC events endpoint
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["summary"] = "Get raw FDC events";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["tags"].append("Analyzer Management");
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["description"] =
        "Retrieve raw FDC port I/O events with Z80 CPU context. All values are JSON numbers.";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][2]["name"] = "limit";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][2]["description"] =
        "Maximum number of events to return (default: 100)";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/fdc"]["get"]["responses"]["200"]["description"] =
        "List of raw FDC events with Z80 main registers and 16-byte stack snapshot";

    // Raw breakpoint events endpoint
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["summary"] = "Get raw breakpoint events";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["tags"].append("Analyzer Management");
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["description"] =
        "Retrieve raw breakpoint hit events with complete Z80 state. Includes main, alternate, index, and special registers. All values are JSON numbers.";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][1]["name"] = "name";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][2]["name"] = "limit";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][2]["description"] =
        "Maximum number of events to return (default: 100)";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints"]["get"]["responses"]["200"]["description"] =
        "List of raw breakpoint events with full Z80 state (main, alternate, IX, IY, I, R) and 16-byte stack snapshot";



    // Debug Commands endpoints
    // Stepping
    paths["/api/v1/emulator/{id}/step"]["post"]["summary"] = "Execute single instruction";
    paths["/api/v1/emulator/{id}/step"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/step"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/step"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/step"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/step"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/step"]["post"]["responses"]["200"]["description"] = "Instruction executed, returns new PC";

    paths["/api/v1/emulator/{id}/steps"]["post"]["summary"] = "Execute N instructions";
    paths["/api/v1/emulator/{id}/steps"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/steps"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/steps"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/steps"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/steps"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/steps"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["count"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/steps"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["count"]["description"] = "Number of instructions to execute";
    paths["/api/v1/emulator/{id}/steps"]["post"]["responses"]["200"]["description"] = "Instructions executed";

    paths["/api/v1/emulator/{id}/stepover"]["post"]["summary"] = "Step over call instruction";
    paths["/api/v1/emulator/{id}/stepover"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/stepover"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/stepover"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/stepover"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/stepover"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/stepover"]["post"]["responses"]["200"]["description"] = "Stepped over call";

    // Debug mode
    paths["/api/v1/emulator/{id}/debugmode"]["get"]["summary"] = "Get debug mode state";
    paths["/api/v1/emulator/{id}/debugmode"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/debugmode"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/debugmode"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/debugmode"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/debugmode"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/debugmode"]["get"]["responses"]["200"]["description"] = "Debug mode enabled state";

    paths["/api/v1/emulator/{id}/debugmode"]["put"]["summary"] = "Enable/disable debug mode";
    paths["/api/v1/emulator/{id}/debugmode"]["put"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/debugmode"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/debugmode"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/debugmode"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/debugmode"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/debugmode"]["put"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["enabled"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/debugmode"]["put"]["responses"]["200"]["description"] = "Debug mode updated";

    // Breakpoints
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["summary"] = "List all breakpoints";
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints"]["get"]["responses"]["200"]["description"] = "List of breakpoints";

    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["summary"] = "Add breakpoint";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/AddBreakpointRequest";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["responses"]["201"]["description"] = "Breakpoint created";
    paths["/api/v1/emulator/{id}/breakpoints"]["post"]["responses"]["201"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/AddBreakpointResponse";

    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["summary"] = "Clear all breakpoints";
    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints"]["delete"]["responses"]["200"]["description"] = "All breakpoints cleared";

    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["summary"] = "Remove specific breakpoint";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][1]["name"] = "bp_id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}"]["delete"]["responses"]["200"]["description"] = "Breakpoint removed";

    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["summary"] = "Enable breakpoint";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][1]["name"] = "bp_id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/enable"]["put"]["responses"]["200"]["description"] = "Breakpoint enabled";

    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["summary"] = "Disable breakpoint";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][1]["name"] = "bp_id";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/breakpoints/{bp_id}/disable"]["put"]["responses"]["200"]["description"] = "Breakpoint disabled";

    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["summary"] = "Get breakpoint status";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["description"] = 
        "Returns information about the last triggered breakpoint, including type (memory/port), address, and access mode";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["responses"]["200"]["description"] = "Breakpoint status";
    paths["/api/v1/emulator/{id}/breakpoints/status"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] =
        "#/components/schemas/BreakpointStatusResponse";

    // Memory inspection
    paths["/api/v1/emulator/{id}/registers"]["get"]["summary"] = "Get CPU registers";
    paths["/api/v1/emulator/{id}/registers"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/registers"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/registers"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/registers"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/registers"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/registers"]["get"]["responses"]["200"]["description"] = "CPU register values";

    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["summary"] = "Read memory";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][1]["name"] = "addr";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][1]["description"] = "Start address (hex or decimal)";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][2]["name"] = "len";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][2]["description"] = "Number of bytes to read (default: 16, max: 256)";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["get"]["responses"]["200"]["description"] = "Memory content";

    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["summary"] = "Write memory";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["description"] = "Write bytes to Z80 memory address space";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["parameters"][1]["name"] = "addr";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["parameters"][1]["description"] = "Start address (hex or decimal)";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["data"]["type"] = "array";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["data"]["items"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["data"]["description"] = "Byte values (0-255)";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["hex"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["hex"]["description"] = "Space-separated hex bytes";
    paths["/api/v1/emulator/{id}/memory/{addr}"]["put"]["responses"]["200"]["description"] = "Memory written";

    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["summary"] = "Read from physical page";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["description"] = "Read bytes from physical RAM/ROM/cache/misc page";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][1]["name"] = "type";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][1]["description"] = "Memory type: ram|rom|cache|misc";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][2]["name"] = "page";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][2]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][2]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][2]["description"] = "Page number";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][3]["name"] = "offset";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][3]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][3]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][3]["description"] = "Offset within page (0-16383)";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][3]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][4]["name"] = "len";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][4]["in"] = "query";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][4]["required"] = false;
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][4]["description"] = "Number of bytes to read (default: 128, max: 16384)";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["parameters"][4]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["get"]["responses"]["200"]["description"] = "Page content";

    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["summary"] = "Write to physical page";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["description"] = "Write bytes to physical page. ROM write requires force flag.";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][1]["name"] = "type";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][2]["name"] = "page";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][2]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][2]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][3]["name"] = "offset";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][3]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][3]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["parameters"][3]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["data"]["type"] = "array";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["data"]["items"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["hex"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["force"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["force"]["description"] = "Required for ROM write";
    paths["/api/v1/emulator/{id}/memory/{type}/{page}/{offset}"]["put"]["responses"]["200"]["description"] = "Page written";

    paths["/api/v1/emulator/{id}/memory/info"]["get"]["summary"] = "Get memory configuration";
    paths["/api/v1/emulator/{id}/memory/info"]["get"]["description"] = "Get page counts and current Z80 bank mappings";
    paths["/api/v1/emulator/{id}/memory/info"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/memory/info"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memory/info"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memory/info"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memory/info"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memory/info"]["get"]["responses"]["200"]["description"] = "Memory configuration";

    // Analysis
    paths["/api/v1/emulator/{id}/memcounters"]["get"]["summary"] = "Get memory access counters";
    paths["/api/v1/emulator/{id}/memcounters"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/memcounters"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/memcounters"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/memcounters"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/memcounters"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/memcounters"]["get"]["responses"]["200"]["description"] = "Memory access statistics";

    paths["/api/v1/emulator/{id}/calltrace"]["get"]["summary"] = "Get call trace";
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["parameters"][1]["name"] = "limit";
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["parameters"][1]["description"] = "Max entries to return (default: 50)";
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/calltrace"]["get"]["responses"]["200"]["description"] = "Call trace entries";

    // Disassembly endpoint
    paths["/api/v1/emulator/{id}/disasm"]["get"]["summary"] = "Disassemble Z80 code";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][1]["name"] = "address";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][1]["description"] = "Start address (hex or decimal, default: PC)";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][2]["name"] = "count";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][2]["description"] = "Number of instructions (default: 10, max: 100)";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/disasm"]["get"]["responses"]["200"]["description"] = "Disassembled instructions";

    // Physical page disassembly endpoint
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["summary"] = "Disassemble from physical RAM/ROM page";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["tags"].append("Debug Commands");
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][1]["name"] = "type";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][1]["description"] = "Memory type: 'ram' or 'rom'";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][2]["name"] = "page";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][2]["required"] = true;
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][2]["description"] = "Physical page number (0-255)";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][2]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][3]["name"] = "offset";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][3]["in"] = "query";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][3]["required"] = false;
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][3]["description"] = "Offset within page (default: 0)";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][3]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][4]["name"] = "count";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][4]["in"] = "query";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][4]["required"] = false;
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][4]["description"] = "Number of instructions (default: 10, max: 100)";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["parameters"][4]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/disasm/page"]["get"]["responses"]["200"]["description"] = "Disassembled instructions from physical page";

    // Memory Profiler control endpoints - individual actions
    paths["/api/v1/emulator/{id}/profiler/memory/start"]["post"]["summary"] = "Start memory profiler";
    paths["/api/v1/emulator/{id}/profiler/memory/start"]["post"]["tags"].append("Memory Profiler");
    paths["/api/v1/emulator/{id}/profiler/memory/start"]["post"]["description"] = "Start memory profiler session. Tracks read/write/execute patterns.";
    paths["/api/v1/emulator/{id}/profiler/memory/start"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/memory/start"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/memory/start"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/memory/start"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/start"]["post"]["responses"]["200"]["description"] = "Profiler started";

    paths["/api/v1/emulator/{id}/profiler/memory/stop"]["post"]["summary"] = "Stop memory profiler";
    paths["/api/v1/emulator/{id}/profiler/memory/stop"]["post"]["tags"].append("Memory Profiler");
    paths["/api/v1/emulator/{id}/profiler/memory/stop"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/memory/stop"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/memory/stop"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/memory/stop"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/stop"]["post"]["responses"]["200"]["description"] = "Profiler stopped";

    paths["/api/v1/emulator/{id}/profiler/memory/pause"]["post"]["summary"] = "Pause memory profiler";
    paths["/api/v1/emulator/{id}/profiler/memory/pause"]["post"]["tags"].append("Memory Profiler");
    paths["/api/v1/emulator/{id}/profiler/memory/pause"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/memory/pause"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/memory/pause"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/memory/pause"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/pause"]["post"]["responses"]["200"]["description"] = "Profiler paused";

    paths["/api/v1/emulator/{id}/profiler/memory/resume"]["post"]["summary"] = "Resume memory profiler";
    paths["/api/v1/emulator/{id}/profiler/memory/resume"]["post"]["tags"].append("Memory Profiler");
    paths["/api/v1/emulator/{id}/profiler/memory/resume"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/memory/resume"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/memory/resume"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/memory/resume"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/resume"]["post"]["responses"]["200"]["description"] = "Profiler resumed";

    paths["/api/v1/emulator/{id}/profiler/memory/clear"]["post"]["summary"] = "Clear memory profiler data";
    paths["/api/v1/emulator/{id}/profiler/memory/clear"]["post"]["tags"].append("Memory Profiler");
    paths["/api/v1/emulator/{id}/profiler/memory/clear"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/memory/clear"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/memory/clear"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/memory/clear"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/clear"]["post"]["responses"]["200"]["description"] = "Profiler data cleared";

    paths["/api/v1/emulator/{id}/profiler/memory/status"]["get"]["summary"] = "Get memory profiler status";
    paths["/api/v1/emulator/{id}/profiler/memory/status"]["get"]["tags"].append("Memory Profiler");
    paths["/api/v1/emulator/{id}/profiler/memory/status"]["get"]["description"] =
        "Get current memory profiler status including session state, tracking mode, and feature enabled status.";
    paths["/api/v1/emulator/{id}/profiler/memory/status"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/memory/status"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/memory/status"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/memory/status"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/status"]["get"]["responses"]["200"]["description"] = "Memory profiler status";

    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["summary"] = "Get per-page access summaries";
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["tags"].append("Memory Profiler");
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["description"] =
        "Get read/write/execute access counts aggregated per physical memory page.";
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["parameters"][1]["name"] = "limit";
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["parameters"][1]["description"] = "Maximum pages to return (default: all active)";
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/profiler/memory/pages"]["get"]["responses"]["200"]["description"] = "Per-page access summaries";

    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["summary"] = "Get address-level access counters";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["tags"].append("Memory Profiler");
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["description"] =
        "Get detailed read/write/execute counters for each address within a page or Z80 address space.";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][1]["name"] = "page";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][1]["description"] = "Physical page number (0-based)";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][2]["name"] = "mode";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][2]["description"] = "Address mode: z80 or physical (default: physical)";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["parameters"][2]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/counters"]["get"]["responses"]["200"]["description"] = "Address-level access counters";

    paths["/api/v1/emulator/{id}/profiler/memory/regions"]["get"]["summary"] = "Get monitored region statistics";
    paths["/api/v1/emulator/{id}/profiler/memory/regions"]["get"]["tags"].append("Memory Profiler");
    paths["/api/v1/emulator/{id}/profiler/memory/regions"]["get"]["description"] =
        "Get access statistics for all monitored memory regions.";
    paths["/api/v1/emulator/{id}/profiler/memory/regions"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/memory/regions"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/memory/regions"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/memory/regions"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/regions"]["get"]["responses"]["200"]["description"] = "Monitored region statistics";

    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["summary"] = "Save access data to file";
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["tags"].append("Memory Profiler");
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["description"] =
        "Save memory access profiling data to a file in the specified format.";
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["path"]["description"] = "Output file or directory path";
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["format"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["format"]["description"] = "Output format (yaml)";
    paths["/api/v1/emulator/{id}/profiler/memory/save"]["post"]["responses"]["200"]["description"] = "Data saved successfully";


    // Call Trace Profiler control endpoints - individual actions
    paths["/api/v1/emulator/{id}/profiler/calltrace/start"]["post"]["summary"] = "Start call trace profiler";
    paths["/api/v1/emulator/{id}/profiler/calltrace/start"]["post"]["tags"].append("Call Trace Profiler");
    paths["/api/v1/emulator/{id}/profiler/calltrace/start"]["post"]["description"] = "Start call trace profiler session. Tracks CALL/RET/JP/JR/RST events.";
    paths["/api/v1/emulator/{id}/profiler/calltrace/start"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/calltrace/start"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/calltrace/start"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/calltrace/start"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/calltrace/start"]["post"]["responses"]["200"]["description"] = "Profiler started";

    paths["/api/v1/emulator/{id}/profiler/calltrace/stop"]["post"]["summary"] = "Stop call trace profiler";
    paths["/api/v1/emulator/{id}/profiler/calltrace/stop"]["post"]["tags"].append("Call Trace Profiler");
    paths["/api/v1/emulator/{id}/profiler/calltrace/stop"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/calltrace/stop"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/calltrace/stop"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/calltrace/stop"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/calltrace/stop"]["post"]["responses"]["200"]["description"] = "Profiler stopped";

    paths["/api/v1/emulator/{id}/profiler/calltrace/pause"]["post"]["summary"] = "Pause call trace profiler";
    paths["/api/v1/emulator/{id}/profiler/calltrace/pause"]["post"]["tags"].append("Call Trace Profiler");
    paths["/api/v1/emulator/{id}/profiler/calltrace/pause"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/calltrace/pause"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/calltrace/pause"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/calltrace/pause"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/calltrace/pause"]["post"]["responses"]["200"]["description"] = "Profiler paused";

    paths["/api/v1/emulator/{id}/profiler/calltrace/resume"]["post"]["summary"] = "Resume call trace profiler";
    paths["/api/v1/emulator/{id}/profiler/calltrace/resume"]["post"]["tags"].append("Call Trace Profiler");
    paths["/api/v1/emulator/{id}/profiler/calltrace/resume"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/calltrace/resume"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/calltrace/resume"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/calltrace/resume"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/calltrace/resume"]["post"]["responses"]["200"]["description"] = "Profiler resumed";

    paths["/api/v1/emulator/{id}/profiler/calltrace/clear"]["post"]["summary"] = "Clear call trace profiler data";
    paths["/api/v1/emulator/{id}/profiler/calltrace/clear"]["post"]["tags"].append("Call Trace Profiler");
    paths["/api/v1/emulator/{id}/profiler/calltrace/clear"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/calltrace/clear"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/calltrace/clear"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/calltrace/clear"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/calltrace/clear"]["post"]["responses"]["200"]["description"] = "Profiler data cleared";

    paths["/api/v1/emulator/{id}/profiler/calltrace/status"]["get"]["summary"] = "Get call trace profiler status";
    paths["/api/v1/emulator/{id}/profiler/calltrace/status"]["get"]["tags"].append("Call Trace Profiler");
    paths["/api/v1/emulator/{id}/profiler/calltrace/status"]["get"]["description"] =
        "Get current call trace profiler status including session state, entry count, and buffer capacity.";
    paths["/api/v1/emulator/{id}/profiler/calltrace/status"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/calltrace/status"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/calltrace/status"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/calltrace/status"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/calltrace/status"]["get"]["responses"]["200"]["description"] = "Call trace profiler status";

    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["summary"] = "Get call trace entries";
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["tags"].append("Call Trace Profiler");
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["description"] =
        "Get recent control flow trace entries (CALL, RET, JP, JR, RST events) with PC, SP, and timing.";
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["parameters"][1]["name"] = "count";
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["parameters"][1]["description"] = "Number of entries to return (default: 100)";
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/profiler/calltrace/entries"]["get"]["responses"]["200"]["description"] = "Call trace entries";

    paths["/api/v1/emulator/{id}/profiler/calltrace/stats"]["get"]["summary"] = "Get call/return statistics";
    paths["/api/v1/emulator/{id}/profiler/calltrace/stats"]["get"]["tags"].append("Call Trace Profiler");
    paths["/api/v1/emulator/{id}/profiler/calltrace/stats"]["get"]["description"] =
        "Get aggregated statistics including call counts, return counts, max call depth, and top targets.";
    paths["/api/v1/emulator/{id}/profiler/calltrace/stats"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/calltrace/stats"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/calltrace/stats"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/calltrace/stats"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/calltrace/stats"]["get"]["responses"]["200"]["description"] = "Call/return statistics";


    // Opcode Profiler control endpoints - individual actions
    paths["/api/v1/emulator/{id}/profiler/opcode/start"]["post"]["summary"] = "Start opcode profiler";
    paths["/api/v1/emulator/{id}/profiler/opcode/start"]["post"]["tags"].append("Opcode Profiler");
    paths["/api/v1/emulator/{id}/profiler/opcode/start"]["post"]["description"] = "Start opcode profiler session. Enables feature and clears previous data.";
    paths["/api/v1/emulator/{id}/profiler/opcode/start"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/opcode/start"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/opcode/start"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/opcode/start"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/opcode/start"]["post"]["responses"]["200"]["description"] = "Profiler started";

    paths["/api/v1/emulator/{id}/profiler/opcode/stop"]["post"]["summary"] = "Stop opcode profiler";
    paths["/api/v1/emulator/{id}/profiler/opcode/stop"]["post"]["tags"].append("Opcode Profiler");
    paths["/api/v1/emulator/{id}/profiler/opcode/stop"]["post"]["description"] = "Stop opcode profiler session. Data is preserved.";
    paths["/api/v1/emulator/{id}/profiler/opcode/stop"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/opcode/stop"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/opcode/stop"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/opcode/stop"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/opcode/stop"]["post"]["responses"]["200"]["description"] = "Profiler stopped";

    paths["/api/v1/emulator/{id}/profiler/opcode/pause"]["post"]["summary"] = "Pause opcode profiler";
    paths["/api/v1/emulator/{id}/profiler/opcode/pause"]["post"]["tags"].append("Opcode Profiler");
    paths["/api/v1/emulator/{id}/profiler/opcode/pause"]["post"]["description"] = "Pause profiler session. Data is retained.";
    paths["/api/v1/emulator/{id}/profiler/opcode/pause"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/opcode/pause"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/opcode/pause"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/opcode/pause"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/opcode/pause"]["post"]["responses"]["200"]["description"] = "Profiler paused";

    paths["/api/v1/emulator/{id}/profiler/opcode/resume"]["post"]["summary"] = "Resume opcode profiler";
    paths["/api/v1/emulator/{id}/profiler/opcode/resume"]["post"]["tags"].append("Opcode Profiler");
    paths["/api/v1/emulator/{id}/profiler/opcode/resume"]["post"]["description"] = "Resume paused profiler session.";
    paths["/api/v1/emulator/{id}/profiler/opcode/resume"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/opcode/resume"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/opcode/resume"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/opcode/resume"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/opcode/resume"]["post"]["responses"]["200"]["description"] = "Profiler resumed";

    paths["/api/v1/emulator/{id}/profiler/opcode/clear"]["post"]["summary"] = "Clear opcode profiler data";
    paths["/api/v1/emulator/{id}/profiler/opcode/clear"]["post"]["tags"].append("Opcode Profiler");
    paths["/api/v1/emulator/{id}/profiler/opcode/clear"]["post"]["description"] = "Clear all profiler data without changing session state.";
    paths["/api/v1/emulator/{id}/profiler/opcode/clear"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/opcode/clear"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/opcode/clear"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/opcode/clear"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/opcode/clear"]["post"]["responses"]["200"]["description"] = "Profiler data cleared";

    paths["/api/v1/emulator/{id}/profiler/opcode/status"]["get"]["summary"] = "Get opcode profiler status";
    paths["/api/v1/emulator/{id}/profiler/opcode/status"]["get"]["tags"].append("Opcode Profiler");
    paths["/api/v1/emulator/{id}/profiler/opcode/status"]["get"]["description"] =
        "Get current profiler status including session state, total executions, and trace buffer size.";
    paths["/api/v1/emulator/{id}/profiler/opcode/status"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/opcode/status"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/opcode/status"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/opcode/status"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/opcode/status"]["get"]["responses"]["200"]["description"] = "Profiler status";
    paths["/api/v1/emulator/{id}/profiler/opcode/status"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/ProfilerStatusResponse";

    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["summary"] = "Get opcode execution counters";
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["tags"].append("Opcode Profiler");
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["description"] =
        "Get top N opcodes by execution count, sorted by frequency.";
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["parameters"][1]["name"] = "limit";
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["parameters"][1]["description"] = "Maximum opcodes to return (default: 100)";
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["responses"]["200"]["description"] = "Opcode counters";
    paths["/api/v1/emulator/{id}/profiler/opcode/counters"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/ProfilerCountersResponse";

    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["summary"] = "Get recent execution trace";
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["tags"].append("Opcode Profiler");
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["description"] =
        "Get recent opcode execution trace with PC, prefix, opcode, and CPU state.";
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["parameters"][1]["name"] = "count";
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["parameters"][1]["description"] = "Number of trace entries (default: 100)";
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["parameters"][1]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["responses"]["200"]["description"] = "Execution trace";
    paths["/api/v1/emulator/{id}/profiler/opcode/trace"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["$ref"] = "#/components/schemas/ProfilerTraceResponse";

    // Unified Profiler Control endpoints - individual actions
    paths["/api/v1/emulator/{id}/profiler/start"]["post"]["summary"] = "Start all profilers";
    paths["/api/v1/emulator/{id}/profiler/start"]["post"]["tags"].append("Unified Profiler");
    paths["/api/v1/emulator/{id}/profiler/start"]["post"]["description"] = "Start all profiler sessions (opcode, memory, calltrace) simultaneously.";
    paths["/api/v1/emulator/{id}/profiler/start"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/start"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/start"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/start"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/start"]["post"]["responses"]["200"]["description"] = "All profilers started";

    paths["/api/v1/emulator/{id}/profiler/stop"]["post"]["summary"] = "Stop all profilers";
    paths["/api/v1/emulator/{id}/profiler/stop"]["post"]["tags"].append("Unified Profiler");
    paths["/api/v1/emulator/{id}/profiler/stop"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/stop"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/stop"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/stop"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/stop"]["post"]["responses"]["200"]["description"] = "All profilers stopped";

    paths["/api/v1/emulator/{id}/profiler/pause"]["post"]["summary"] = "Pause all profilers";
    paths["/api/v1/emulator/{id}/profiler/pause"]["post"]["tags"].append("Unified Profiler");
    paths["/api/v1/emulator/{id}/profiler/pause"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/pause"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/pause"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/pause"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/pause"]["post"]["responses"]["200"]["description"] = "All profilers paused";

    paths["/api/v1/emulator/{id}/profiler/resume"]["post"]["summary"] = "Resume all profilers";
    paths["/api/v1/emulator/{id}/profiler/resume"]["post"]["tags"].append("Unified Profiler");
    paths["/api/v1/emulator/{id}/profiler/resume"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/resume"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/resume"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/resume"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/resume"]["post"]["responses"]["200"]["description"] = "All profilers resumed";

    paths["/api/v1/emulator/{id}/profiler/clear"]["post"]["summary"] = "Clear all profiler data";
    paths["/api/v1/emulator/{id}/profiler/clear"]["post"]["tags"].append("Unified Profiler");
    paths["/api/v1/emulator/{id}/profiler/clear"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/clear"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/clear"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/clear"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/clear"]["post"]["responses"]["200"]["description"] = "All profiler data cleared";

    paths["/api/v1/emulator/{id}/profiler/status"]["get"]["summary"] = "Get status of all profilers";
    paths["/api/v1/emulator/{id}/profiler/status"]["get"]["tags"].append("Unified Profiler");
    paths["/api/v1/emulator/{id}/profiler/status"]["get"]["description"] =
        "Get current status of all profilers (opcode, memory, calltrace) including session states.";
    paths["/api/v1/emulator/{id}/profiler/status"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/profiler/status"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/profiler/status"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/profiler/status"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/profiler/status"]["get"]["responses"]["200"]["description"] = "All profiler statuses";


    spec["paths"] = paths;

    // Components/Schemas
    Json::Value schemas;

    schemas["EmulatorList"]["type"] = "object";
    schemas["EmulatorList"]["properties"]["emulators"]["type"] = "array";
    schemas["EmulatorList"]["properties"]["emulators"]["items"]["$ref"] = "#/components/schemas/EmulatorInfo";
    schemas["EmulatorList"]["properties"]["count"]["type"] = "integer";

    schemas["EmulatorInfo"]["type"] = "object";
    schemas["EmulatorInfo"]["properties"]["id"]["type"] = "string";
    schemas["EmulatorInfo"]["properties"]["id"]["description"] = "Emulator UUID";
    schemas["EmulatorInfo"]["properties"]["state"]["type"] = "string";
    schemas["EmulatorInfo"]["properties"]["state"]["enum"].append("initialized");
    schemas["EmulatorInfo"]["properties"]["state"]["enum"].append("running");
    schemas["EmulatorInfo"]["properties"]["state"]["enum"].append("paused");
    schemas["EmulatorInfo"]["properties"]["state"]["enum"].append("stopped");
    schemas["EmulatorInfo"]["properties"]["is_running"]["type"] = "boolean";
    schemas["EmulatorInfo"]["properties"]["is_paused"]["type"] = "boolean";
    schemas["EmulatorInfo"]["properties"]["is_debug"]["type"] = "boolean";

    schemas["CreateEmulatorRequest"]["type"] = "object";
    schemas["CreateEmulatorRequest"]["properties"]["model"]["type"] = "string";
    schemas["CreateEmulatorRequest"]["properties"]["model"]["description"] = "Emulator model (e.g., ZX48, ZX128)";

    // Settings Management schemas
    schemas["SettingsResponse"]["type"] = "object";
    schemas["SettingsResponse"]["description"] = "List of emulator settings";
    schemas["SettingsResponse"]["properties"]["settings"]["type"] = "object";
    schemas["SettingsResponse"]["properties"]["settings"]["additionalProperties"]["type"] = "string";

    // Feature Management schemas
    schemas["FeaturesResponse"]["type"] = "object";
    schemas["FeaturesResponse"]["description"] = "List of all features";
    schemas["FeaturesResponse"]["properties"]["emulator_id"]["type"] = "string";
    schemas["FeaturesResponse"]["properties"]["features"]["type"] = "array";
    schemas["FeaturesResponse"]["properties"]["features"]["items"]["$ref"] = "#/components/schemas/FeatureInfo";

    schemas["FeatureInfo"]["type"] = "object";
    schemas["FeatureInfo"]["description"] = "Feature information";
    schemas["FeatureInfo"]["properties"]["id"]["type"] = "string";
    schemas["FeatureInfo"]["properties"]["enabled"]["type"] = "boolean";
    schemas["FeatureInfo"]["properties"]["description"]["type"] = "string";
    schemas["FeatureInfo"]["properties"]["mode"]["type"] = "string";

    // Debug Commands schemas
    
    // Breakpoints List Response
    schemas["BreakpointsListResponse"]["type"] = "object";
    schemas["BreakpointsListResponse"]["description"] = "List of breakpoints";
    schemas["BreakpointsListResponse"]["properties"]["count"]["type"] = "integer";
    schemas["BreakpointsListResponse"]["properties"]["breakpoints"]["type"] = "array";
    schemas["BreakpointsListResponse"]["properties"]["breakpoints"]["items"]["$ref"] = "#/components/schemas/BreakpointInfo";
    
    // Memory Breakpoint Info
    schemas["MemoryBreakpointInfo"]["type"] = "object";
    schemas["MemoryBreakpointInfo"]["description"] = "Memory breakpoint (execute/read/write)";
    schemas["MemoryBreakpointInfo"]["properties"]["id"]["type"] = "integer";
    schemas["MemoryBreakpointInfo"]["properties"]["type"]["type"] = "string";
    schemas["MemoryBreakpointInfo"]["properties"]["type"]["enum"].append("memory");
    schemas["MemoryBreakpointInfo"]["properties"]["address"]["type"] = "integer";
    schemas["MemoryBreakpointInfo"]["properties"]["execute"]["type"] = "boolean";
    schemas["MemoryBreakpointInfo"]["properties"]["read"]["type"] = "boolean";
    schemas["MemoryBreakpointInfo"]["properties"]["write"]["type"] = "boolean";
    schemas["MemoryBreakpointInfo"]["properties"]["active"]["type"] = "boolean";
    schemas["MemoryBreakpointInfo"]["properties"]["note"]["type"] = "string";
    schemas["MemoryBreakpointInfo"]["properties"]["group"]["type"] = "string";
    
    // Port Breakpoint Info
    schemas["PortBreakpointInfo"]["type"] = "object";
    schemas["PortBreakpointInfo"]["description"] = "Port breakpoint (in/out)";
    schemas["PortBreakpointInfo"]["properties"]["id"]["type"] = "integer";
    schemas["PortBreakpointInfo"]["properties"]["type"]["type"] = "string";
    schemas["PortBreakpointInfo"]["properties"]["type"]["enum"].append("port");
    schemas["PortBreakpointInfo"]["properties"]["address"]["type"] = "integer";
    schemas["PortBreakpointInfo"]["properties"]["address"]["description"] = "Port number";
    schemas["PortBreakpointInfo"]["properties"]["in"]["type"] = "boolean";
    schemas["PortBreakpointInfo"]["properties"]["out"]["type"] = "boolean";
    schemas["PortBreakpointInfo"]["properties"]["active"]["type"] = "boolean";
    schemas["PortBreakpointInfo"]["properties"]["note"]["type"] = "string";
    schemas["PortBreakpointInfo"]["properties"]["group"]["type"] = "string";
    
    // Keyboard Breakpoint Info
    schemas["KeyboardBreakpointInfo"]["type"] = "object";
    schemas["KeyboardBreakpointInfo"]["description"] = "Keyboard breakpoint (press/release)";
    schemas["KeyboardBreakpointInfo"]["properties"]["id"]["type"] = "integer";
    schemas["KeyboardBreakpointInfo"]["properties"]["type"]["type"] = "string";
    schemas["KeyboardBreakpointInfo"]["properties"]["type"]["enum"].append("keyboard");
    schemas["KeyboardBreakpointInfo"]["properties"]["address"]["type"] = "integer";
    schemas["KeyboardBreakpointInfo"]["properties"]["press"]["type"] = "boolean";
    schemas["KeyboardBreakpointInfo"]["properties"]["release"]["type"] = "boolean";
    schemas["KeyboardBreakpointInfo"]["properties"]["active"]["type"] = "boolean";
    schemas["KeyboardBreakpointInfo"]["properties"]["note"]["type"] = "string";
    schemas["KeyboardBreakpointInfo"]["properties"]["group"]["type"] = "string";
    
    // Generic BreakpointInfo (oneOf the above)
    schemas["BreakpointInfo"]["oneOf"][0]["$ref"] = "#/components/schemas/MemoryBreakpointInfo";
    schemas["BreakpointInfo"]["oneOf"][1]["$ref"] = "#/components/schemas/PortBreakpointInfo";
    schemas["BreakpointInfo"]["oneOf"][2]["$ref"] = "#/components/schemas/KeyboardBreakpointInfo";
    schemas["BreakpointInfo"]["discriminator"]["propertyName"] = "type";
    schemas["BreakpointInfo"]["discriminator"]["mapping"]["memory"] = "#/components/schemas/MemoryBreakpointInfo";
    schemas["BreakpointInfo"]["discriminator"]["mapping"]["port"] = "#/components/schemas/PortBreakpointInfo";
    schemas["BreakpointInfo"]["discriminator"]["mapping"]["keyboard"] = "#/components/schemas/KeyboardBreakpointInfo";
    
    // Breakpoint Status Response (last triggered)
    schemas["BreakpointStatusResponse"]["type"] = "object";
    schemas["BreakpointStatusResponse"]["description"] = "Last triggered breakpoint information";
    schemas["BreakpointStatusResponse"]["properties"]["is_paused"]["type"] = "boolean";
    schemas["BreakpointStatusResponse"]["properties"]["breakpoints_count"]["type"] = "integer";
    schemas["BreakpointStatusResponse"]["properties"]["last_triggered_id"]["type"] = "integer";
    schemas["BreakpointStatusResponse"]["properties"]["last_triggered_id"]["nullable"] = true;
    schemas["BreakpointStatusResponse"]["properties"]["last_triggered_type"]["type"] = "string";
    schemas["BreakpointStatusResponse"]["properties"]["last_triggered_type"]["enum"].append("memory");
    schemas["BreakpointStatusResponse"]["properties"]["last_triggered_type"]["enum"].append("port");
    schemas["BreakpointStatusResponse"]["properties"]["last_triggered_type"]["enum"].append("keyboard");
    schemas["BreakpointStatusResponse"]["properties"]["last_triggered_address"]["type"] = "integer";
    schemas["BreakpointStatusResponse"]["properties"]["paused_by_breakpoint"]["type"] = "boolean";
    
    // Add Breakpoint Request
    schemas["AddBreakpointRequest"]["type"] = "object";
    schemas["AddBreakpointRequest"]["description"] = "Request to add a breakpoint";
    schemas["AddBreakpointRequest"]["required"].append("type");
    schemas["AddBreakpointRequest"]["required"].append("address");
    schemas["AddBreakpointRequest"]["properties"]["type"]["type"] = "string";
    schemas["AddBreakpointRequest"]["properties"]["type"]["description"] = "Breakpoint type";
    schemas["AddBreakpointRequest"]["properties"]["type"]["enum"].append("execution");
    schemas["AddBreakpointRequest"]["properties"]["type"]["enum"].append("read");
    schemas["AddBreakpointRequest"]["properties"]["type"]["enum"].append("write");
    schemas["AddBreakpointRequest"]["properties"]["type"]["enum"].append("port_in");
    schemas["AddBreakpointRequest"]["properties"]["type"]["enum"].append("port_out");
    schemas["AddBreakpointRequest"]["properties"]["address"]["type"] = "integer";
    schemas["AddBreakpointRequest"]["properties"]["address"]["description"] = "Z80 address (0-65535) or port number (0-255)";
    schemas["AddBreakpointRequest"]["properties"]["note"]["type"] = "string";
    schemas["AddBreakpointRequest"]["properties"]["note"]["description"] = "Optional annotation";
    schemas["AddBreakpointRequest"]["properties"]["group"]["type"] = "string";
    schemas["AddBreakpointRequest"]["properties"]["group"]["description"] = "Optional group name";
    
    // Add Breakpoint Response
    schemas["AddBreakpointResponse"]["type"] = "object";
    schemas["AddBreakpointResponse"]["description"] = "Response after adding a breakpoint";
    schemas["AddBreakpointResponse"]["properties"]["status"]["type"] = "string";
    schemas["AddBreakpointResponse"]["properties"]["id"]["type"] = "integer";
    schemas["AddBreakpointResponse"]["properties"]["id"]["description"] = "Assigned breakpoint ID";
    schemas["AddBreakpointResponse"]["properties"]["type"]["type"] = "string";
    schemas["AddBreakpointResponse"]["properties"]["address"]["type"] = "integer";
    schemas["AddBreakpointResponse"]["properties"]["message"]["type"] = "string";

    // Memory State schemas
    schemas["MemoryStateResponse"]["type"] = "object";
    schemas["MemoryStateResponse"]["description"] = "Memory state overview";
    schemas["MemoryStateResponse"]["properties"]["total_ram"]["type"] = "integer";
    schemas["MemoryStateResponse"]["properties"]["total_rom"]["type"] = "integer";

    schemas["RAMStateResponse"]["type"] = "object";
    schemas["RAMStateResponse"]["description"] = "RAM state details";
    schemas["RAMStateResponse"]["properties"]["size"]["type"] = "integer";
    schemas["RAMStateResponse"]["properties"]["banks"]["type"] = "array";

    schemas["ROMStateResponse"]["type"] = "object";
    schemas["ROMStateResponse"]["description"] = "ROM state details";
    schemas["ROMStateResponse"]["properties"]["size"]["type"] = "integer";
    schemas["ROMStateResponse"]["properties"]["type"]["type"] = "string";

    // Screen State schemas
    schemas["ScreenStateResponse"]["type"] = "object";
    schemas["ScreenStateResponse"]["description"] = "Screen state overview";
    schemas["ScreenStateResponse"]["properties"]["mode"]["type"] = "string";
    schemas["ScreenStateResponse"]["properties"]["flash_enabled"]["type"] = "boolean";

    schemas["ScreenModeResponse"]["type"] = "object";
    schemas["ScreenModeResponse"]["description"] = "Screen mode information";
    schemas["ScreenModeResponse"]["properties"]["mode"]["type"] = "string";
    schemas["ScreenModeResponse"]["properties"]["resolution"]["type"] = "string";

    schemas["FlashStateResponse"]["type"] = "object";
    schemas["FlashStateResponse"]["description"] = "Flash state information";
    schemas["FlashStateResponse"]["properties"]["enabled"]["type"] = "boolean";
    schemas["FlashStateResponse"]["properties"]["phase"]["type"] = "integer";

    // Audio State schemas
    schemas["AYChipsResponse"]["type"] = "object";
    schemas["AYChipsResponse"]["description"] = "AY chips overview";
    schemas["AYChipsResponse"]["properties"]["chip_count"]["type"] = "integer";
    schemas["AYChipsResponse"]["properties"]["chips"]["type"] = "array";

    schemas["AYChipResponse"]["type"] = "object";
    schemas["AYChipResponse"]["description"] = "AY chip details";
    schemas["AYChipResponse"]["properties"]["chip_index"]["type"] = "integer";
    schemas["AYChipResponse"]["properties"]["registers"]["type"] = "array";

    schemas["AYRegisterResponse"]["type"] = "object";
    schemas["AYRegisterResponse"]["description"] = "AY chip register details";
    schemas["AYRegisterResponse"]["properties"]["register"]["type"] = "integer";
    schemas["AYRegisterResponse"]["properties"]["value"]["type"] = "integer";

    schemas["BeeperStateResponse"]["type"] = "object";
    schemas["BeeperStateResponse"]["description"] = "Beeper state";
    schemas["BeeperStateResponse"]["properties"]["enabled"]["type"] = "boolean";
    schemas["BeeperStateResponse"]["properties"]["value"]["type"] = "integer";

    schemas["GSStateResponse"]["type"] = "object";
    schemas["GSStateResponse"]["description"] = "General Sound state";
    schemas["GSStateResponse"]["properties"]["enabled"]["type"] = "boolean";
    schemas["GSStateResponse"]["properties"]["channels"]["type"] = "integer";

    schemas["CovoxStateResponse"]["type"] = "object";
    schemas["CovoxStateResponse"]["description"] = "Covox state";
    schemas["CovoxStateResponse"]["properties"]["enabled"]["type"] = "boolean";
    schemas["CovoxStateResponse"]["properties"]["value"]["type"] = "integer";

    schemas["AudioChannelsResponse"]["type"] = "object";
    schemas["AudioChannelsResponse"]["description"] = "Audio channels overview";
    schemas["AudioChannelsResponse"]["properties"]["channel_count"]["type"] = "integer";
    schemas["AudioChannelsResponse"]["properties"]["channels"]["type"] = "array";

    // Tape/Disk/Snapshot schemas
    schemas["TapeInfoResponse"]["type"] = "object";
    schemas["TapeInfoResponse"]["description"] = "Tape status information";
    schemas["TapeInfoResponse"]["properties"]["loaded"]["type"] = "boolean";
    schemas["TapeInfoResponse"]["properties"]["playing"]["type"] = "boolean";

    schemas["DiskInfoResponse"]["type"] = "object";
    schemas["DiskInfoResponse"]["description"] = "Disk drive status";
    schemas["DiskInfoResponse"]["properties"]["inserted"]["type"] = "boolean";
    schemas["DiskInfoResponse"]["properties"]["write_protected"]["type"] = "boolean";

    schemas["SnapshotInfoResponse"]["type"] = "object";
    schemas["SnapshotInfoResponse"]["description"] = "Snapshot status";
    schemas["SnapshotInfoResponse"]["properties"]["loaded"]["type"] = "boolean";
    schemas["SnapshotInfoResponse"]["properties"]["filename"]["type"] = "string";

    // Batch command schemas
    schemas["BatchCommand"]["type"] = "object";
    schemas["BatchCommand"]["description"] = "A single command in a batch";
    schemas["BatchCommand"]["required"].append("emulator");
    schemas["BatchCommand"]["required"].append("command");
    schemas["BatchCommand"]["properties"]["emulator"]["type"] = "string";
    schemas["BatchCommand"]["properties"]["emulator"]["description"] = "Emulator ID, UUID, or index";
    schemas["BatchCommand"]["properties"]["command"]["type"] = "string";
    schemas["BatchCommand"]["properties"]["command"]["description"] =
        "Command name: load-snapshot, reset, pause, resume, feature";
    schemas["BatchCommand"]["properties"]["arg1"]["type"] = "string";
    schemas["BatchCommand"]["properties"]["arg1"]["description"] = "First argument (e.g., file path, feature name)";
    schemas["BatchCommand"]["properties"]["arg2"]["type"] = "string";
    schemas["BatchCommand"]["properties"]["arg2"]["description"] = "Second argument (e.g., on/off for feature)";

    schemas["BatchExecuteRequest"]["type"] = "object";
    schemas["BatchExecuteRequest"]["description"] = "Batch execution request";
    schemas["BatchExecuteRequest"]["required"].append("commands");
    schemas["BatchExecuteRequest"]["properties"]["commands"]["type"] = "array";
    schemas["BatchExecuteRequest"]["properties"]["commands"]["items"]["$ref"] = "#/components/schemas/BatchCommand";

    schemas["BatchResult"]["type"] = "object";
    schemas["BatchResult"]["description"] = "Batch execution result";
    schemas["BatchResult"]["properties"]["success"]["type"] = "boolean";
    schemas["BatchResult"]["properties"]["total"]["type"] = "integer";
    schemas["BatchResult"]["properties"]["succeeded"]["type"] = "integer";
    schemas["BatchResult"]["properties"]["failed"]["type"] = "integer";
    schemas["BatchResult"]["properties"]["duration_ms"]["type"] = "number";
    schemas["BatchResult"]["properties"]["results"]["type"] = "array";

    schemas["BatchableCommandsResponse"]["type"] = "object";
    schemas["BatchableCommandsResponse"]["description"] = "List of batchable commands";
    schemas["BatchableCommandsResponse"]["properties"]["commands"]["type"] = "array";
    schemas["BatchableCommandsResponse"]["properties"]["count"]["type"] = "integer";

    // Opcode Profiler schemas
    schemas["ProfilerStatusResponse"]["type"] = "object";
    schemas["ProfilerStatusResponse"]["description"] = "Opcode profiler status";
    schemas["ProfilerStatusResponse"]["properties"]["enabled"]["type"] = "boolean";
    schemas["ProfilerStatusResponse"]["properties"]["instructions_executed"]["type"] = "integer";
    schemas["ProfilerStatusResponse"]["properties"]["unique_opcodes"]["type"] = "integer";
    schemas["ProfilerStatusResponse"]["properties"]["prefixes_tracked"]["type"] = "boolean";
    schemas["ProfilerStatusResponse"]["properties"]["timing_tracked"]["type"] = "boolean";
    schemas["ProfilerStatusResponse"]["properties"]["memory_tracked"]["type"] = "boolean";

    schemas["OpcodeStats"]["type"] = "object";
    schemas["OpcodeStats"]["properties"]["prefix"]["type"] = "integer";
    schemas["OpcodeStats"]["properties"]["opcode"]["type"] = "integer";
    schemas["OpcodeStats"]["properties"]["count"]["type"] = "integer";
    schemas["OpcodeStats"]["properties"]["total_tstates"]["type"] = "integer";
    schemas["OpcodeStats"]["properties"]["min_tstates"]["type"] = "integer";
    schemas["OpcodeStats"]["properties"]["max_tstates"]["type"] = "integer";
    schemas["OpcodeStats"]["properties"]["avg_tstates"]["type"] = "number";
    schemas["OpcodeStats"]["properties"]["mnemonic"]["type"] = "string";

    schemas["TraceEntry"]["type"] = "object";
    schemas["TraceEntry"]["properties"]["pc"]["type"] = "integer";
    schemas["TraceEntry"]["properties"]["prefix"]["type"] = "integer";
    schemas["TraceEntry"]["properties"]["opcode"]["type"] = "integer";
    schemas["TraceEntry"]["properties"]["flags"]["type"] = "integer";
    schemas["TraceEntry"]["properties"]["a"]["type"] = "integer";
    schemas["TraceEntry"]["properties"]["frame"]["type"] = "integer";
    schemas["TraceEntry"]["properties"]["tstate"]["type"] = "integer";

    schemas["ProfilerTraceResponse"]["type"] = "object";
    schemas["ProfilerTraceResponse"]["description"] = "Opcode execution trace";
    schemas["ProfilerTraceResponse"]["properties"]["emulator_id"]["type"] = "string";
    schemas["ProfilerTraceResponse"]["properties"]["trace_size"]["type"] = "integer";
    schemas["ProfilerTraceResponse"]["properties"]["requested_count"]["type"] = "integer";
    schemas["ProfilerTraceResponse"]["properties"]["returned_count"]["type"] = "integer";
    schemas["ProfilerTraceResponse"]["properties"]["trace"]["type"] = "array";
    schemas["ProfilerTraceResponse"]["properties"]["trace"]["items"]["$ref"] = "#/components/schemas/TraceEntry";

    spec["components"]["schemas"] = schemas;

    auto resp = HttpResponse::newHttpJsonResponse(spec);
    addCorsHeaders(resp);
    callback(resp);
}

}  // namespace v1
}  // namespace api
