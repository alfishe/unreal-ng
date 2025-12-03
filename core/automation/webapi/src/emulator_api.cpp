#include "emulator_api.h"
#include <emulator/emulatormanager.h>
#include <emulator/emulator.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

using namespace drogon;
using namespace api::v1;

namespace api
{
    namespace v1
    {
        // Helper function to convert emulator state to string
        std::string stateToString(EmulatorStateEnum state) {
            switch (state) {
                case StateInitialized: return "initialized";
                case StateRun: return "running";
                case StatePaused: return "paused";
                case StateStopped: return "stopped";
                default: return "unknown";
            }
        }

        // GET /api/v1/emulator
        // List all emulators
        void EmulatorAPI::get(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) const
        {
            auto manager = EmulatorManager::GetInstance();
            auto emulatorIds = manager->GetEmulatorIds();
            
            Json::Value ret;
            Json::Value emulators(Json::arrayValue);
            
            for (const auto& id : emulatorIds) {
                auto emulator = manager->GetEmulator(id);
                if (emulator) {
                    Json::Value emuInfo;
                    emuInfo["id"] = id;
                    emuInfo["state"] = stateToString(emulator->GetState());
                    emuInfo["is_running"] = emulator->IsRunning();
                    emuInfo["is_paused"] = emulator->IsPaused();
                    emuInfo["is_debug"] = emulator->IsDebug();
                    emulators.append(emuInfo);
                }
            }
            
            ret["emulators"] = emulators;
            ret["count"] = static_cast<Json::UInt>(emulatorIds.size());
            
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            callback(resp);
        }

        // GET /api/v1/emulator/status
        // Get overall emulator status
        void EmulatorAPI::status(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) const
        {
            auto manager = EmulatorManager::GetInstance();
            auto emulatorIds = manager->GetEmulatorIds();
            
            Json::Value ret;
            ret["emulator_count"] = static_cast<Json::UInt>(emulatorIds.size());
            
            // Count emulators by state
            Json::Value states(Json::objectValue);
            for (const auto& id : emulatorIds) {
                auto emulator = manager->GetEmulator(id);
                if (emulator) {
                    std::string state = stateToString(emulator->GetState());
                    if (states.isMember(state)) {
                        states[state] = states[state].asInt() + 1;
                    } else {
                        states[state] = 1;
                    }
                }
            }
            ret["states"] = states;
            
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            callback(resp);
        }

        // POST /api/v1/emulator
        // Create a new emulator instance
        void EmulatorAPI::createEmulator(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) const
        {
            auto manager = EmulatorManager::GetInstance();
            
            // Parse request body
            auto json = req->getJsonObject();
            std::string symbolicId = json ? (*json)["symbolic_id"].asString() : "";
            
            try {
                std::shared_ptr<Emulator> emulator;
                if (symbolicId.empty()) {
                    emulator = manager->CreateEmulator();
                } else {
                    emulator = manager->CreateEmulator(symbolicId);
                }
                
                Json::Value ret;
                ret["id"] = emulator->GetId();
                ret["state"] = stateToString(emulator->GetState());
                
                auto resp = HttpResponse::newHttpJsonResponse(ret);
                resp->setStatusCode(HttpStatusCode::k201Created);
                callback(resp);
            } catch (const std::exception& e) {
                Json::Value error;
                error["error"] = "Failed to create emulator";
                error["message"] = e.what();
                
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k500InternalServerError);
                callback(resp);
            }
        }

        // GET /api/v1/emulator/:id
        // Get emulator details
        void EmulatorAPI::getEmulator(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, const std::string &id) const
        {
            auto manager = EmulatorManager::GetInstance();
            auto emulator = manager->GetEmulator(id);
            
            if (!emulator) {
                Json::Value error;
                error["error"] = "Not Found";
                error["message"] = "Emulator with specified ID not found";
                
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k404NotFound);
                callback(resp);
                return;
            }
            
            Json::Value ret;
            ret["id"] = id;
            ret["state"] = stateToString(emulator->GetState());
            ret["is_running"] = emulator->IsRunning();
            ret["is_paused"] = emulator->IsPaused();
            ret["is_debug"] = emulator->IsDebug();
            
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            callback(resp);
        }

        // DELETE /api/v1/emulator/:id
        // Remove an emulator
        void EmulatorAPI::removeEmulator(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, const std::string &id) const
        {
            auto manager = EmulatorManager::GetInstance();
            
            if (!manager->HasEmulator(id)) {
                Json::Value error;
                error["error"] = "Not Found";
                error["message"] = "Emulator with specified ID not found";
                
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k404NotFound);
                callback(resp);
                return;
            }
            
            bool removed = manager->RemoveEmulator(id);
            
            Json::Value ret;
            if (removed) {
                ret["status"] = "success";
                ret["message"] = "Emulator removed successfully";
                
                auto resp = HttpResponse::newHttpJsonResponse(ret);
                resp->setStatusCode(HttpStatusCode::k200OK);
                callback(resp);
            } else {
                ret["status"] = "error";
                ret["message"] = "Failed to remove emulator";
                
                auto resp = HttpResponse::newHttpJsonResponse(ret);
                resp->setStatusCode(HttpStatusCode::k500InternalServerError);
                callback(resp);
            }
        }

        // POST /api/v1/emulator/:id/start
        // Start an emulator
        void EmulatorAPI::startEmulator(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, const std::string &id) const
        {
            handleEmulatorAction(req, std::move(callback), id, [](std::shared_ptr<Emulator> emu) {
                emu->StartAsync();
                return "Emulator started";
            });
        }

        // POST /api/v1/emulator/:id/stop
        // Stop an emulator
        void EmulatorAPI::stopEmulator(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, const std::string &id) const
        {
            handleEmulatorAction(req, std::move(callback), id, [](std::shared_ptr<Emulator> emu) {
                emu->Stop();
                return "Emulator stopped";
            });
        }

        // POST /api/v1/emulator/:id/pause
        // Pause an emulator
        void EmulatorAPI::pauseEmulator(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, const std::string &id) const
        {
            handleEmulatorAction(req, std::move(callback), id, [](std::shared_ptr<Emulator> emu) {
                emu->Pause();
                return "Emulator paused";
            });
        }

        // POST /api/v1/emulator/:id/resume
        // Resume an emulator
        void EmulatorAPI::resumeEmulator(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, const std::string &id) const
        {
            handleEmulatorAction(req, std::move(callback), id, [](std::shared_ptr<Emulator> emu) {
                emu->Resume();
                return "Emulator resumed";
            });
        }

        // Helper method to handle emulator actions with common error handling
        void EmulatorAPI::handleEmulatorAction(
            const HttpRequestPtr &req,
            std::function<void(const HttpResponsePtr &)> &&callback,
            const std::string &id,
            std::function<std::string(std::shared_ptr<Emulator>)> action) const
        {
            auto manager = EmulatorManager::GetInstance();
            auto emulator = manager->GetEmulator(id);
            
            if (!emulator) {
                Json::Value error;
                error["error"] = "Not Found";
                error["message"] = "Emulator with specified ID not found";
                
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k404NotFound);
                callback(resp);
                return;
            }
            
            try {
                std::string message = action(emulator);
                
                Json::Value ret;
                ret["status"] = "success";
                ret["message"] = message;
                ret["emulator_id"] = id;
                ret["state"] = stateToString(emulator->GetState());
                
                auto resp = HttpResponse::newHttpJsonResponse(ret);
                callback(resp);
            } catch (const std::exception& e) {
                Json::Value error;
                error["error"] = "Operation failed";
                error["message"] = e.what();
                error["emulator_id"] = id;
                
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k500InternalServerError);
                callback(resp);
            }
        }
    }
}