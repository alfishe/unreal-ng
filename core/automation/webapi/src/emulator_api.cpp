#include "emulator_api.h"

#include <emulator/emulator.h>

namespace api
{
    namespace v1
    {
        void EmulatorAPI::get(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback) const
        {
            Json::Value ret;
            ret["message"] = "Emulator greets you!";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
            callback(resp);
        }

        void EmulatorAPI::status(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback) const
        {
            uint32_t emuCount = 0;
            //uint32_t emuCount = Emulator::getInstancesCount();
            //Emulator& emulator = Emulator::

            Json::Value ret;
            ret["message"] = "Emulator status";
            ret["count"] = emuCount;
            auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
            callback(resp);
        }
    }
}