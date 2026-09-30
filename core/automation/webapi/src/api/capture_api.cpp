/// @author AI Assistant  
/// @date 22.01.2026
/// @brief WebAPI Capture endpoints (OCR, screen capture, ROM text)

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <debugger/analyzers/rom-print/screenocr.h>
#include <emulator/video/screencapture.h>
#include <json/json.h>

#include <algorithm>
#include <sstream>

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

    // Split text into lines array; the grid is 32x24 on the ZX screen, the text
    // mode's own size (80x25) when the mode has a text layer
    Json::Value lines(Json::arrayValue);
    std::istringstream iss(screenText);
    std::string line;
    size_t cols = 0;
    while (std::getline(iss, line))
    {
        cols = std::max(cols, line.size());
        lines.append(line);
    }
    ret["rows"] = lines.size();
    ret["cols"] = static_cast<Json::UInt>(cols);
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
    std::string path = req->getParameter("path");
    if (path.empty()) path = req->getParameter("filename");
    
    if (format.empty()) format = "gif";
    CaptureMode mode = (modeStr == "full") ? CaptureMode::FullFramebuffer : CaptureMode::ScreenOnly;

    // Capture screen
    auto result = ScreenCapture::captureScreen(id, format, mode, path);

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
    if (!result.savedFile.empty())
    {
        ret["saved"] = true;
        ret["file"] = result.savedFile;
    }
    if (!result.base64Data.empty())
    {
        ret["data"] = result.base64Data;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

}  // namespace v1
}  // namespace api
