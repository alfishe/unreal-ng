/// @author AI Assistant  
/// @date 22.01.2026
/// @brief WebAPI Capture endpoints (OCR, screen capture, ROM text)

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <debugger/analyzers/rom-print/screenocr.h>
#include <emulator/video/screencapture.h>
#include <json/json.h>

#include "../emulator_api.h"

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper to add CORS headers (declared extern in tape_disk_api.cpp)
extern void addCorsHeaders(HttpResponsePtr& resp);

/// @brief GET /api/v1/emulator/:id/capture/ocr
/// @brief OCR text from screen using ROM font bitmap matching
void EmulatorAPI::captureOcr(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Thread safety: reject operations on emulators being destroyed
    if (emulator->IsDestroying())
    {
        Json::Value error;
        error["error"] = "Service Unavailable";
        error["message"] = "Emulator is shutting down";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k503ServiceUnavailable);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Run OCR on screen
    std::string screenText = ScreenOCR::ocrScreen(id);
    
    if (screenText.empty())
    {
        Json::Value error;
        error["error"] = "Internal Server Error";
        error["message"] = "Failed to read screen";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Build response with screen text as array of lines
    Json::Value ret;
    ret["status"] = "success";
    ret["rows"] = 24;
    ret["cols"] = 32;
    
    // Split text into lines array
    Json::Value lines(Json::arrayValue);
    std::istringstream iss(screenText);
    std::string line;
    while (std::getline(iss, line))
    {
        lines.append(line);
    }
    ret["lines"] = lines;
    ret["text"] = screenText;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/:id/capture/screen
/// @brief Capture screen as image (GIF or PNG)
/// @param format Query param: "gif" (default) or "png"
/// @param mode Query param: "screen" (256x192, default) or "full" (with border)
void EmulatorAPI::captureScreen(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    if (emulator->IsDestroying())
    {
        Json::Value error;
        error["error"] = "Service Unavailable";
        error["message"] = "Emulator is shutting down";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k503ServiceUnavailable);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Parse query parameters
    std::string format = req->getParameter("format");
    std::string modeStr = req->getParameter("mode");
    
    if (format.empty()) format = "gif";
    CaptureMode mode = (modeStr == "full") ? CaptureMode::FullFramebuffer : CaptureMode::ScreenOnly;

    // Capture screen
    auto result = ScreenCapture::captureScreen(id, format, mode);

    if (!result.success)
    {
        Json::Value error;
        error["error"] = "Internal Server Error";
        error["message"] = result.errorMessage;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Build response
    Json::Value ret;
    ret["status"] = "success";
    ret["format"] = result.format;
    ret["width"] = result.width;
    ret["height"] = result.height;
    ret["size"] = static_cast<Json::UInt64>(result.originalSize);
    ret["data"] = result.base64Data;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

}  // namespace v1
}  // namespace api
