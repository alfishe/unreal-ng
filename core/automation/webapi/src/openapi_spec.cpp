// OpenAPI Specification Handler
// Extracted from emulator_api.cpp - 2026-01-08

#include "emulator_api.h"

#include <drogon/HttpResponse.h>
#include <json/json.h>

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
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["description"] = "List of available ZX Spectrum models with RAM configurations";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]["count"]["type"] = "integer";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]["count"]["description"] = "Total number of available models";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]["models"]["type"] = "array";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]["models"]["items"]["type"] = "object";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]["models"]["items"]["properties"]["id"]["type"] = "integer";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]["models"]["items"]["properties"]["name"]["type"] = "string";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]["models"]["items"]["properties"]["full_name"]["type"] = "string";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]["models"]["items"]["properties"]["default_ram_kb"]["type"] = "integer";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]["models"]["items"]["properties"]["available_ram_sizes_kb"]["type"] = "array";
    paths["/api/v1/emulator/models"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"]["models"]["items"]["properties"]["available_ram_sizes_kb"]["items"]["type"] = "integer";


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
    paths["/api/v1/emulator/start"]["post"]["description"] = "Creates a new emulator instance and immediately starts it";
    paths["/api/v1/emulator/start"]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/emulator/start"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["symbolic_id"]["type"] = "string";
    paths["/api/v1/emulator/start"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["model"]["type"] = "string";
    paths["/api/v1/emulator/start"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["ram_size"]["type"] = "integer";
    paths["/api/v1/emulator/start"]["post"]["responses"]["201"]["description"] = "Emulator created and started";
    paths["/api/v1/emulator/start"]["post"]["responses"]["201"]["content"]["application/json"]["schema"]["$ref"] = "#/components/schemas/EmulatorInfo";

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
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["path"]["type"] = "string";
    paths["/api/v1/emulator/{id}/tape/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["path"]["description"] = "Path to tape image file (.tap, .tzx)";
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
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["parameters"][1]["description"] = "Drive letter (A-D) or number (0-3)";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["path"]["type"] = "string";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["path"]["description"] = "Path to disk image file (.trd, .scl, .fdi, .udi)";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["responses"]["200"]["description"] = "Disk inserted successfully";
    paths["/api/v1/emulator/{id}/disk/{drive}/insert"]["post"]["responses"]["400"]["description"] = "Invalid path, file format, or drive parameter";

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
    paths["/api/v1/emulator/{id}/disk/{drive}/info"]["get"]["responses"]["200"]["description"] = "Disk drive status information";

    // Snapshot control endpoints
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["summary"] = "Load snapshot file";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["tags"].append("Snapshot Control");
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["path"]["type"] = "string";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["path"]["description"] = "Path to snapshot file (.z80, .sna)";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["responses"]["200"]["description"] = "Snapshot loaded successfully";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["responses"]["400"]["description"] = "Invalid path or file format";
    paths["/api/v1/emulator/{id}/snapshot/load"]["post"]["responses"]["404"]["description"] = "Emulator not found";

    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["summary"] = "Get snapshot status";
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["tags"].append("Snapshot Control");
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/snapshot/info"]["get"]["responses"]["200"]["description"] = "Snapshot status information";

    // Settings Management endpoints
    paths["/api/v1/emulator/{id}/settings"]["get"]["summary"] = "Get all emulator settings";
    paths["/api/v1/emulator/{id}/settings"]["get"]["tags"].append("Settings Management");
    paths["/api/v1/emulator/{id}/settings"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/settings"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/settings"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/settings"]["get"]["parameters"][0]["description"] = "Emulator UUID or index";
    paths["/api/v1/emulator/{id}/settings"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/settings"]["get"]["responses"]["200"]["description"] = "Settings list";
    paths["/api/v1/emulator/{id}/settings"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] = "#/components/schemas/SettingsResponse";

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
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["value"]["type"] = "string";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["value"]["description"] = "New setting value";
    paths["/api/v1/emulator/{id}/settings/{name}"]["put"]["responses"]["200"]["description"] = "Setting updated";

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
    paths["/api/v1/emulator/{id}/state/screen/mode"]["get"]["responses"]["200"]["description"] = "Screen mode information";

    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["summary"] = "Get flash state";
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["tags"].append("Screen State");
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/screen/flash"]["get"]["responses"]["200"]["description"] = "Flash state information";

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
    paths["/api/v1/emulator/{id}/state/audio/channels"]["get"]["responses"]["200"]["description"] = "Audio channels information";

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
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][0]["schema"]["type"] = "integer";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["name"] = "reg";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["description"] =
        "Register number (0-15)";
    paths["/api/v1/emulator/state/audio/ay/{chip}/register/{reg}"]["get"]["parameters"][1]["schema"]["type"] = "integer";
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

    paths["/api/v1/emulator/state/audio/channels"][" get"]["summary"] = "Get audio channels (active emulator)";
    paths["/api/v1/emulator/state/audio/channels"]["get"]["tags"].append("Audio State (Active)");
    paths["/api/v1/emulator/state/audio/channels"]["get"]["responses"]["200"]["description"] = "Audio channels information";

    // Python Interpreter Control endpoints
    paths["/api/v1/python/exec"]["post"]["summary"] = "Execute Python code";
    paths["/api/v1/python/exec"]["post"]["tags"].append("Python Interpreter");
    paths["/api/v1/python/exec"]["post"]["description"] = "Execute Python code synchronously. Requires Python automation enabled at compile-time.";
    paths["/api/v1/python/exec"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/python/exec"]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/python/exec"]["post"]["requestBody"]["content"]["application/json"]["schema"]["required"].append("code");
    paths["/api/v1/python/exec"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["code"]["type"] = "string";
    paths["/api/v1/python/exec"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["code"]["description"] = "Python code to execute";
    paths["/api/v1/python/exec"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["code"]["example"] = "print('Hello from Python')";
    paths["/api/v1/python/exec"]["post"]["responses"]["200"]["description"] = "Code executed successfully";
    paths["/api/v1/python/exec"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] = "#/components/schemas/InterpreterExecResponse";
    paths["/api/v1/python/exec"]["post"]["responses"]["400"]["description"] = "Bad request - missing code parameter";
    paths["/api/v1/python/exec"]["post"]["responses"]["500"]["description"] = "Execution error";
    paths["/api/v1/python/exec"]["post"]["responses"]["503"]["description"] = "Python automation not available";

    paths["/api/v1/python/file"]["post"]["summary"] = "Execute Python file";
    paths["/api/v1/python/file"]["post"]["tags"].append("Python Interpreter");
    paths["/api/v1/python/file"]["post"]["description"] = "Load and execute Python file from absolute path";
    paths["/api/v1/python/file"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/python/file"]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/python/file"]["post"]["requestBody"]["content"]["application/json"]["schema"]["required"].append("path");
    paths["/api/v1/python/file"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["path"]["type"] = "string";
    paths["/api/v1/python/file"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["path"]["description"] = "Absolute path to Python file (.py)";
    paths["/api/v1/python/file"]["post"]["responses"]["200"]["description"] = "File executed successfully";
    paths["/api/v1/python/file"]["post"]["responses"]["400"]["description"] = "Invalid file path";
    paths["/api/v1/python/file"]["post"]["responses"]["404"]["description"] = "File not found";
    paths["/api/v1/python/file"]["post"]["responses"]["500"]["description"] = "Execution error";
    paths["/api/v1/python/file"]["post"]["responses"]["503"]["description"] = "Python automation not available";

    paths["/api/v1/python/status"]["get"]["summary"] = "Get Python interpreter status";
    paths["/api/v1/python/status"]["get"]["tags"].append("Python Interpreter");
    paths["/api/v1/python/status"]["get"]["description"] = "Get current status and availability of Python interpreter";
    paths["/api/v1/python/status"]["get"]["responses"]["200"]["description"] = "Status information";
    paths["/api/v1/python/status"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] = "#/components/schemas/InterpreterStatusResponse";

    paths["/api/v1/python/stop"]["post"]["summary"] = "Stop Python execution";
    paths["/api/v1/python/stop"]["post"]["tags"].append("Python Interpreter");
    paths["/api/v1/python/stop"]["post"]["description"] = "Send interrupt signal to stop running Python code";
    paths["/api/v1/python/stop"]["post"]["responses"]["200"]["description"] = "Interrupt signal sent";
    paths["/api/v1/python/stop"]["post"]["responses"]["503"]["description"] = "Python automation not available";

    // Lua Interpreter Control endpoints
    paths["/api/v1/lua/exec"]["post"]["summary"] = "Execute Lua code";
    paths["/api/v1/lua/exec"]["post"]["tags"].append("Lua Interpreter");
    paths["/api/v1/lua/exec"]["post"]["description"] = "Execute Lua code synchronously. Requires Lua automation enabled at compile-time.";
    paths["/api/v1/lua/exec"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/lua/exec"]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/lua/exec"]["post"]["requestBody"]["content"]["application/json"]["schema"]["required"].append("code");
    paths["/api/v1/lua/exec"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["code"]["type"] = "string";
    paths["/api/v1/lua/exec"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["code"]["description"] = "Lua code to execute";
    paths["/api/v1/lua/exec"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["code"]["example"] = "print('Hello from Lua')";
    paths["/api/v1/lua/exec"]["post"]["responses"]["200"]["description"] = "Code executed successfully";
    paths["/api/v1/lua/exec"]["post"]["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] = "#/components/schemas/InterpreterExecResponse";
    paths["/api/v1/lua/exec"]["post"]["responses"]["400"]["description"] = "Bad request - missing code parameter";
    paths["/api/v1/lua/exec"]["post"]["responses"]["500"]["description"] = "Execution error";
    paths["/api/v1/lua/exec"]["post"]["responses"]["503"]["description"] = "Lua automation not available";

    paths["/api/v1/lua/file"]["post"]["summary"] = "Execute Lua file";
    paths["/api/v1/lua/file"]["post"]["tags"].append("Lua Interpreter");
    paths["/api/v1/lua/file"]["post"]["description"] = "Load and execute Lua file from absolute path";
    paths["/api/v1/lua/file"]["post"]["requestBody"]["required"] = true;
    paths["/api/v1/lua/file"]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
    paths["/api/v1/lua/file"]["post"]["requestBody"]["content"]["application/json"]["schema"]["required"].append("path");
    paths["/api/v1/lua/file"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["path"]["type"] = "string";
    paths["/api/v1/lua/file"]["post"]["requestBody"]["content"]["application/json"]["schema"]["properties"]["path"]["description"] = "Absolute path to Lua file (.lua)";
    paths["/api/v1/lua/file"]["post"]["responses"]["200"]["description"] = "File executed successfully";
    paths["/api/v1/lua/file"]["post"]["responses"]["400"]["description"] = "Invalid file path";
    paths["/api/v1/lua/file"]["post"]["responses"]["404"]["description"] = "File not found";
    paths["/api/v1/lua/file"]["post"]["responses"]["500"]["description"] = "Execution error";
    paths["/api/v1/lua/file"]["post"]["responses"]["503"]["description"] = "Lua automation not available";

    paths["/api/v1/lua/status"]["get"]["summary"] = "Get Lua interpreter status";
    paths["/api/v1/lua/status"]["get"]["tags"].append("Lua Interpreter");
    paths["/api/v1/lua/status"]["get"]["description"] = "Get current status and availability of Lua interpreter";
    paths["/api/v1/lua/status"]["get"]["responses"]["200"]["description"] = "Status information";
    paths["/api/v1/lua/status"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] = "#/components/schemas/InterpreterStatusResponse";

    paths["/api/v1/lua/stop"]["post"]["summary"] = "Request Lua execution stop";
    paths["/api/v1/lua/stop"]["post"]["tags"].append("Lua Interpreter");
  paths["/api/v1/lua/stop"]["post"]["description"] = "Request Lua execution stop (requires cooperative script checking)";
    paths["/api/v1/lua/stop"]["post"]["responses"]["200"]["description"] = "Stop request noted";
    paths["/api/v1/lua/stop"]["post"]["responses"]["503"]["description"] = "Lua automation not available";

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

    // Interpreter Control schemas
    schemas["InterpreterExecResponse"]["type"] = "object";
    schemas["InterpreterExecResponse"]["description"] = "Interpreter code execution response";
    schemas["InterpreterExecResponse"]["properties"]["success"]["type"] = "boolean";
    schemas["InterpreterExecResponse"]["properties"]["message"]["type"] = "string";
    schemas["InterpreterExecResponse"]["properties"]["error"]["type"] = "string";
    schemas["InterpreterExecResponse"]["properties"]["output"]["type"] = "string";
    schemas["InterpreterExecResponse"]["properties"]["output"]["description"] = "Captured stdout output from script execution";
    schemas["InterpreterExecResponse"]["properties"]["path"]["type"] = "string";
    schemas["InterpreterExecResponse"]["properties"]["path"]["description"] = "File path (for file execution)";

    schemas["InterpreterStatusResponse"]["type"] = "object";
    schemas["InterpreterStatusResponse"]["description"] = "Interpreter status information";
    schemas["InterpreterStatusResponse"]["properties"]["available"]["type"] = "boolean";
    schemas["InterpreterStatusResponse"]["properties"]["initialized"]["type"] = "boolean";
    schemas["InterpreterStatusResponse"]["properties"]["status"]["type"] = "string";
    schemas["InterpreterStatusResponse"]["properties"]["message"]["type"] = "string";
    schemas["InterpreterStatusResponse"]["properties"]["error"]["type"] = "string";

    spec["components"]["schemas"] = schemas;

    // Tags
    Json::Value tags(Json::arrayValue);
    Json::Value tag1;
    tag1["name"] = "Emulator Management";
    tag1["description"] = "Emulator lifecycle and information";
    tags.append(tag1);

    Json::Value tag2;
    tag2["name"] = "Emulator Control";
    tag2["description"] = "Control emulator execution state";
    tags.append(tag2);

    Json::Value tag3Settings;
    tag3Settings["name"] = "Settings Management";
    tag3Settings["description"] = "Emulator configuration and settings";
    tags.append(tag3Settings);

    Json::Value tag4Tape;
    tag4Tape["name"] = "Tape Control";
    tag4Tape["description"] = "Tape image control and playback";
    tags.append(tag4Tape);

    Json::Value tag5Disk;
    tag5Disk["name"] = "Disk Control";
    tag5Disk["description"] = "Disk image management";
    tags.append(tag5Disk);

    Json::Value tag6Snapshot;
    tag6Snapshot["name"] = "Snapshot Control";
    tag6Snapshot["description"] = "Snapshot file loading and status";
   tags.append(tag6Snapshot);

    Json::Value tag7Memory;
    tag7Memory["name"] = "Memory State";
    tag7Memory["description"] = "Memory inspection (RAM/ROM)";
    tags.append(tag7Memory);

    Json::Value tag8Screen;
    tag8Screen["name"] = "Screen State";
    tag8Screen["description"] = "Screen/video state inspection";
    tags.append(tag8Screen);

    Json::Value tag9Audio;
    tag9Audio["name"] = "Audio State";
    tag9Audio["description"] = "Inspect audio hardware state (with emulator ID)";
    tags.append(tag9Audio);

    Json::Value tag10AudioActive;
    tag10AudioActive["name"] = "Audio State (Active)";
    tag10AudioActive["description"] = "Inspect audio hardware state (active/most recent emulator)";
    tags.append(tag10AudioActive);

    Json::Value tagPythonInterpreter;
    tagPythonInterpreter["name"] = "Python Interpreter";
    tagPythonInterpreter["description"] = "Remote Python interpreter control";
    tags.append(tagPythonInterpreter);

    Json::Value tagLuaInterpreter;
    tagLuaInterpreter["name"] = "Lua Interpreter";
    tagLuaInterpreter["description"] = "Remote Lua interpreter control";
    tags.append(tagLuaInterpreter);

    spec["tags"] = tags;

    auto resp = HttpResponse::newHttpJsonResponse(spec);
    addCorsHeaders(resp);
    callback(resp);
}

}  // namespace v1
}  // namespace api
