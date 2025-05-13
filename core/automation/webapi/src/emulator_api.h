#pragma once

#include <drogon/HttpController.h>

namespace api
{
    namespace v1
    {
        class EmulatorAPI : public drogon::HttpController<EmulatorAPI>
        {
        public:
            EmulatorAPI() = default;

            METHOD_LIST_BEGIN
                METHOD_ADD(EmulatorAPI::get, "", drogon::Get);             // path is /api/v1/EmulatorAPI
                METHOD_ADD(EmulatorAPI::status, "/status", drogon::Get);    // path is /api/v1/EmulatorAPI/status
            METHOD_LIST_END

            void get(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;
            void status(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;
        };
    } // namespace v1
} // namespace api