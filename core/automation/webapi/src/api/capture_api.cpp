/// @author AI Assistant  
/// @date 22.01.2026
/// @brief WebAPI Capture endpoints (OCR, screen capture, ROM text)

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <debugger/analyzers/rom-print/screenocr.h>
#include <emulator/video/framebufferexport.h>
#include <drogon/utils/Utilities.h>
#include <emulator/emulatorcontext.h>
#include <emulator/video/screen.h>
#include <emulator/video/screenshotter.h>
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

/// @brief GET /api/v1/emulator/:id/capture/framebuffer?format=rgba|index&encoding=binary|base64
/// @brief The picture as raw pixels (FramebufferExport, automation audit G14): binary (default,
/// application/octet-stream with X-Width / X-Height / X-Format / X-Encoding) or JSON with base64 data
void EmulatorAPI::captureFramebuffer(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id) const
{
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    auto fail = [&callback](HttpStatusCode code, const std::string& error, const std::string& message) {
        Json::Value body;
        body["error"] = error;
        body["message"] = message;
        auto resp = HttpResponse::newHttpJsonResponse(body);
        resp->setStatusCode(code);
        addCorsHeaders(resp);
        callback(resp);
    };
    if (!emulator)
        return fail(HttpStatusCode::k404NotFound, "Not Found", "Emulator not found");
    const std::string format = req->getParameter("format").empty() ? std::string("rgba") : req->getParameter("format");
    const std::string encoding = req->getParameter("encoding").empty() ? std::string("binary") : req->getParameter("encoding");
    if (encoding != "binary" && encoding != "base64")
        return fail(HttpStatusCode::k400BadRequest, "Bad Request", "encoding must be binary or base64");
    FramebufferExport::Frame frame;
    std::string error;
    if (!FramebufferExport::Capture(emulator->GetContext(), format, frame, error))
        return fail(format == "rgba" || format == "index" ? HttpStatusCode::k409Conflict : HttpStatusCode::k400BadRequest,
                    format == "rgba" || format == "index" ? "Conflict" : "Bad Request", error);
    if (encoding == "base64")
    {
        Json::Value body;
        body["width"] = frame.width;
        body["height"] = frame.height;
        body["format"] = frame.format;
        body["encoding"] = frame.encoding;
        body["bytes"] = static_cast<Json::UInt64>(frame.bytes.size());
        body["data"] = drogon::utils::base64Encode(frame.bytes.data(), frame.bytes.size());
        auto resp = HttpResponse::newHttpJsonResponse(body);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    auto resp = HttpResponse::newHttpResponse();
    resp->setContentTypeCode(CT_APPLICATION_OCTET_STREAM);
    resp->setBody(std::string(frame.bytes.begin(), frame.bytes.end()));
    resp->addHeader("X-Width", std::to_string(frame.width));
    resp->addHeader("X-Height", std::to_string(frame.height));
    resp->addHeader("X-Format", frame.format);
    resp->addHeader("X-Encoding", frame.encoding);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/:id/capture/planeb
/// @brief ZX DLSS plane B of the current frame as raw binary (application/octet-stream):
/// width x height little-endian uint16, same layout as the full framebuffer; encoding in
/// Screen::kPlaneB* (bits 0-7 attribute, 8-11 color index, 12 ink, 13-14 role).
/// Geometry in X-PlaneB-Width / X-PlaneB-Height. 409 while the zxdlss feature is off.
void EmulatorAPI::capturePlaneB(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    (void)req;
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    auto fail = [&callback](HttpStatusCode code, const std::string& error, const std::string& message) {
        Json::Value body;
        body["error"] = error;
        body["message"] = message;
        auto resp = HttpResponse::newHttpJsonResponse(body);
        resp->setStatusCode(code);
        addCorsHeaders(resp);
        callback(resp);
    };
    if (!emulator)
        return fail(HttpStatusCode::k404NotFound, "Not Found", "Emulator not found");

    EmulatorContext* context = emulator->GetContext();
    Screen* screen = context ? context->pScreen : nullptr;
    // The latched copy of the presented frame: the live buffer belongs to the
    // emulation thread, which may be drawing into it (or resizing it) now
    std::vector<uint16_t> planeB;
    if (!screen || !screen->CopyPresentedPlaneB(planeB))
        return fail(HttpStatusCode::k409Conflict, "Conflict",
                    "Plane B is off - enable the 'zxdlss' feature (takes effect at the next frame)");

    const FramebufferDescriptor& fb = screen->GetFramebufferDescriptor();
    auto resp = HttpResponse::newHttpResponse();
    resp->setContentTypeCode(CT_APPLICATION_OCTET_STREAM);
    resp->setBody(std::string(reinterpret_cast<const char*>(planeB.data()), planeB.size() * sizeof(uint16_t)));
    resp->addHeader("X-PlaneB-Width", std::to_string(fb.width));
    resp->addHeader("X-PlaneB-Height", std::to_string(fb.height));
    resp->addHeader("X-PlaneB-Format", "u16le: attr[0:7] color[8:11] ink[12] role[13:14] (1 screen, 2 border)");
    addCorsHeaders(resp);
    callback(resp);
}

namespace
{
Json::Value RectJson(const PictureRect& r)
{
    Json::Value v;
    v["x"] = r.x;
    v["y"] = r.y;
    v["width"] = r.width;
    v["height"] = r.height;
    return v;
}

HttpResponsePtr ScreenshotErrorResponse(HttpStatusCode status, const char* reason, const std::string& message,
                                        const char* kind)
{
    Json::Value error;
    error["error"] = reason;
    error["message"] = message;
    error["kind"] = kind;
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(status);
    addCorsHeaders(resp);
    return resp;
}
}  // namespace

/// @brief GET /api/v1/emulator/:id/capture/screen
/// @brief Screenshot of the emulator's presented frame, as PNG (default) or GIF
/// @param area Query param: "full" (default: the whole frame, border included) or "screen" (the working
///        picture the frame's own geometry names: the paper of a Spectrum, the graphics window of a TS-Conf,
///        the whole FT812 picture). On a hires mode both can be the whole frame
/// @param mode Deprecated alias of area ("full" / "screen"); a conflicting pair is an error
/// @param format Query param: "png" (default) or "gif" (256 colors)
/// @param path, filename Save the image to this server-side path instead of returning it
/// The answer always carries the frame's geometry and the rectangle that was cut, so a caller never has to
/// guess what it got. Design: docs/inprogress/2026-10-03-screenshotter/design.md
void EmulatorAPI::captureScreen(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);

    if (!emulator)
    {
        callback(ScreenshotErrorResponse(HttpStatusCode::k404NotFound, "Not Found", "Emulator not found",
                                         Screenshotter::ErrorName(ScreenshotError::NotFound)));
        return;
    }

    if (emulator->IsDestroying())
    {
        callback(ScreenshotErrorResponse(HttpStatusCode::k503ServiceUnavailable, "Service Unavailable",
                                         "Emulator is shutting down", "shutting-down"));
        return;
    }

    // Parameters: a word that is not one of the documented values is an error, never a silent default
    const std::string areaText = req->getParameter("area");
    const std::string modeText = req->getParameter("mode");
    const std::string formatText = req->getParameter("format");
    const std::string sourceText = req->getParameter("source");
    std::string path = req->getParameter("path");
    if (path.empty())
        path = req->getParameter("filename");

    ScreenshotOptions options;  // whole frame, PNG
    std::string badWord;
    if (!Screenshotter::ParseRequestWords(areaText, modeText, formatText, sourceText, options, badWord))
    {
        callback(ScreenshotErrorResponse(HttpStatusCode::k400BadRequest, "Bad Request", badWord,
                                         Screenshotter::ErrorName(ScreenshotError::BadParameter)));
        return;
    }
    options.saveTo = path;

    const ScreenshotResult shot = Screenshotter::Take(id, options);
    if (!shot.ok)
    {
        HttpStatusCode status = HttpStatusCode::k500InternalServerError;
        const char* reason = "Internal Server Error";
        switch (shot.error)
        {
            case ScreenshotError::NotFound:
                status = HttpStatusCode::k404NotFound;
                reason = "Not Found";
                break;
            case ScreenshotError::NoFrame:
                status = HttpStatusCode::k409Conflict;  // the emulator has not presented a frame yet: try again
                reason = "Conflict";
                break;
            case ScreenshotError::BadParameter:
                status = HttpStatusCode::k400BadRequest;
                reason = "Bad Request";
                break;
            default:
                break;
        }
        callback(ScreenshotErrorResponse(status, reason, shot.errorMessage, Screenshotter::ErrorName(shot.error)));
        return;
    }

    Json::Value ret;
    ret["status"] = "success";
    ret["format"] = Screenshotter::FormatName(shot.format);
    ret["area"] = Screenshotter::AreaName(options.area);
    ret["source"] = Screenshotter::RequestSourceName(options.source);  // which frame was asked for
    ret["width"] = shot.width;
    ret["height"] = shot.height;
    ret["size"] = static_cast<Json::UInt64>(shot.encodedSize);
    ret["crop"] = RectJson(shot.crop);  // the returned image's rectangle inside the frame
    ret["screen_window"] = RectJson(shot.frame.screenWindow);
    Json::Value frame;
    frame["width"] = shot.frame.width;
    frame["height"] = shot.frame.height;
    frame["mode"] = shot.frame.source == FrameSource::External ? std::string("external")
                                                               : Screen::GetVideoModeName(shot.frame.videoMode);
    frame["source"] = Screenshotter::SourceName(shot.frame.source);
    frame["frame_number"] = static_cast<Json::UInt64>(shot.frame.frameNumber);
    if (shot.frame.beamLine >= 0)
    {
        // A live frame of a stopped machine: where the beam stood. The pixels it has not reached are the
        // previous frame's, so `partial` says whether this frame is part old, part new
        frame["partial"] = shot.frame.partial;
        frame["beam"]["line"] = shot.frame.beamLine;
        frame["beam"]["tstate"] = shot.frame.beamTstate;
    }
    ret["frame"] = frame;
    if (!shot.savedFile.empty())
    {
        ret["saved"] = true;
        ret["file"] = shot.savedFile;
    }
    else
    {
        ret["data"] = Screenshotter::Base64Encode(shot.bytes);
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

}  // namespace v1
}  // namespace api
