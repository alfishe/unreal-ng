#include "emulator_api.h"

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/platform.h>
#include <json/json.h>

#include <algorithm>
#include <fstream>
#include <sstream>

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{
// Helper function to add CORS headers to responses
void addCorsHeaders(HttpResponsePtr& resp)
{
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
}

// Helper function to load HTML file from resources
std::string loadHtmlFile(const std::string& filename)
{
    // Try multiple possible locations for the HTML resources
    std::vector<std::string> searchPaths = {
        // Development/Build paths
        "./resources/html/",                         // In current directory (from bin when running)
        "../resources/html/",                        // One level up (from bin to build root)
        "./core/automation/webapi/resources/html/",  // Development/build from project root
        "../../resources/html/",                     // Two levels up

        // macOS .app bundle path
        "../Resources/html/",     // From MacOS folder to Resources folder in .app bundle
        "../../Resources/html/",  // Alternative .app bundle structure

        // Standard installation paths
        "/usr/local/share/unreal-speccy/resources/html/",  // Unix standard install
        "/usr/share/unreal-speccy/resources/html/",        // Unix system install
        "./share/unreal-speccy/resources/html/",           // Relative to install prefix
    };

    for (const auto& basePath : searchPaths)
    {
        std::string fullPath = basePath + filename;
        std::ifstream file(fullPath);
        if (file.is_open())
        {
            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }
    }

    // Fallback: return a minimal HTML page if file not found
    return "<!DOCTYPE html><html><head><title>Error</title></head><body>"
           "<h1>Resource Not Found</h1>"
           "<p>Could not load HTML resource: " +
           filename +
           "</p>"
           "<p>Searched paths: development builds, macOS .app bundle, standard installations</p>"
           "<p>Please ensure resources are properly installed.</p>"
           "</body></html>";
}

// Helper function to convert emulator state to string
std::string stateToString(EmulatorStateEnum state)
{
    switch (state)
    {
        case StateInitialized:
            return "initialized";
        case StateRun:
            return "running";
        case StatePaused:
            return "paused";
        case StateStopped:
            return "stopped";
        default:
            return "unknown";
    }
}

/// @brief GET /
/// @brief Root redirect to OpenAPI documentation
void EmulatorAPI::rootRedirect(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
    // Load HTML page from disk
    std::string html = loadHtmlFile("index.html");

    auto resp = HttpResponse::newHttpResponse();
    resp->setBody(html);
    resp->setContentTypeCode(ContentType::CT_TEXT_HTML);
    addCorsHeaders(resp);
    callback(resp);
}

// Note: CORS preflight (OPTIONS) is now handled globally via registerSyncAdvice
// in automation-webapi.cpp and main.cpp


/// @brief Helper method to get emulator by ID (UUID) or index (numeric)
/// This does NOT auto-select; it returns nullptr if the specific ID/index is not found
std::shared_ptr<Emulator> EmulatorAPI::getEmulatorByIdOrIndex(const std::string& idOrIndex) const
{
    auto manager = EmulatorManager::GetInstance();

    // Try to parse as index first (check if ENTIRE string is numeric)
    bool isNumeric = !idOrIndex.empty() && std::all_of(idOrIndex.begin(), idOrIndex.end(), ::isdigit);
    int index = -1;
    
    if (isNumeric)
    {
        try
        {
            index = std::stoi(idOrIndex);
        }
        catch (const std::exception&)
        {
            // Overflow or other parsing error
            isNumeric = false;
        }
    }

    if (isNumeric && index >= 0)
    {
        // It's a valid index, try to get by index
        return manager->GetEmulatorByIndex(index);
    }
    else
    {
        // It's not numeric or negative, treat as UUID
        return manager->GetEmulator(idOrIndex);
    }
}

/// @brief Helper method for emulator selection with global selection priority
/// First checks for globally selected emulator, then falls back to stateless behavior
/// Returns nullptr if no emulator can be selected (requires explicit selection)
std::shared_ptr<Emulator> EmulatorAPI::getEmulatorWithGlobalSelection() const
{
    auto manager = EmulatorManager::GetInstance();

    // First priority: Check if there's a globally selected emulator
    std::string selectedId = manager->GetSelectedEmulatorId();
    if (!selectedId.empty())
    {
        auto emulator = manager->GetEmulator(selectedId);
        if (emulator)
        {
            return emulator;
        }
    }

    // Second priority: Fallback to stateless behavior (only one emulator)
    return getEmulatorStateless();
}

/// @brief Helper method for stateless emulator auto-selection
/// Returns an emulator only if there's exactly one running (stateless behavior)
/// Returns nullptr if there are 0 or 2+ emulators (requires explicit selection)
std::shared_ptr<Emulator> EmulatorAPI::getEmulatorStateless() const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulatorIds = manager->GetEmulatorIds();

    if (emulatorIds.size() == 1)
    {
        // Only one emulator - auto-select it (stateless behavior)
        return manager->GetEmulator(emulatorIds[0]);
    }

    // 0 or 2+ emulators - return nullptr
    return nullptr;
}
}  // namespace v1
}  // namespace api
