#include "hello_world_api.h"


namespace api
{
    namespace v1
    {
        void HelloWorld::get(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback) const
        {
            Json::Value ret;
            ret["message"] = "Hello, World!";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
            callback(resp);
        }
    }
}