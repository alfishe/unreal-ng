/// @brief WebAPI recent files (RecentFiles): GET /api/v1/recent[?kind=], DELETE /api/v1/recent[?kind=].
/// Opening one is the catalogue action media.open_recent (POST /api/v1/controllers/actions/media.open_recent
/// {"params": {"index": 0, "kind": "tape"}}).

#include <drogon/HttpResponse.h>
#include <emulator/media/recentfiles.h>
#include <json/json.h>

#include "../common/statenode_json.h"
#include "../emulator_api.h"

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

extern void addCorsHeaders(HttpResponsePtr& resp);

namespace
{
/// The kind of the query ("" = all); false with a 400 sent for an unknown one
bool KindOf(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>& callback, std::string& kind)
{
    kind = req->getParameter("kind");
    if (kind.empty() || kind == "all" || RecentFiles::IsKind(kind))
        return true;
    Json::Value error;
    error["error"] = "Bad Request";
    error["message"] = "kind '" + kind + "': all, snapshot, tape, disk, rzx, image or cd";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k400BadRequest);
    addCorsHeaders(resp);
    callback(resp);
    return false;
}
}  // namespace

void EmulatorAPI::getRecentFiles(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
    std::string kind;
    if (!KindOf(req, callback, kind))
        return;
    auto resp = HttpResponse::newHttpJsonResponse(StateNodeToJson(RecentFiles::Instance().ToValue(kind)));
    addCorsHeaders(resp);
    callback(resp);
}

void EmulatorAPI::clearRecentFiles(const HttpRequestPtr& req,
                                   std::function<void(const HttpResponsePtr&)>&& callback) const
{
    std::string kind;
    if (!KindOf(req, callback, kind))
        return;
    RecentFiles::Instance().Clear(kind);
    Json::Value body = StateNodeToJson(RecentFiles::Instance().ToValue(kind));
    body["cleared"] = kind.empty() ? "all" : kind;
    auto resp = HttpResponse::newHttpJsonResponse(body);
    addCorsHeaders(resp);
    callback(resp);
}

}  // namespace v1
}  // namespace api
