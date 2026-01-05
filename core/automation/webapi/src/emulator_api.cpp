#include "emulator_api.h"
#include <emulator/emulatormanager.h>
#include <emulator/emulator.h>
#include <emulator/platform.h>
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

        // GET /api/v1/emulator/:id/settings
        // Get all settings for an emulator
        void EmulatorAPI::getSettings(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, const std::string &id) const
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
            
            EmulatorContext* context = emulator->GetContext();
            if (!context) {
                Json::Value error;
                error["error"] = "Internal Error";
                error["message"] = "Unable to access emulator context";
                
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k500InternalServerError);
                callback(resp);
                return;
            }
            
            CONFIG& config = context->config;
            
            Json::Value ret;
            Json::Value settings(Json::objectValue);
            
            // I/O Acceleration settings
            Json::Value io_accel(Json::objectValue);
            io_accel["fast_tape"] = config.tape_traps != 0;
            io_accel["fast_disk"] = config.wd93_nodelay;
            settings["io_acceleration"] = io_accel;
            
            // Disk Interface settings
            Json::Value disk_if(Json::objectValue);
            disk_if["trdos_present"] = config.trdos_present;
            disk_if["trdos_traps"] = config.trdos_traps;
            settings["disk_interface"] = disk_if;
            
            ret["emulator_id"] = id;
            ret["settings"] = settings;
            
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            callback(resp);
        }

        // GET /api/v1/emulator/:id/settings/:name
        // Get a specific setting value
        void EmulatorAPI::getSetting(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, const std::string &id, const std::string &name) const
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
            
            EmulatorContext* context = emulator->GetContext();
            if (!context) {
                Json::Value error;
                error["error"] = "Internal Error";
                error["message"] = "Unable to access emulator context";
                
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k500InternalServerError);
                callback(resp);
                return;
            }
            
            CONFIG& config = context->config;
            Json::Value ret;
            
            if (name == "fast_tape") {
                ret["name"] = "fast_tape";
                ret["value"] = config.tape_traps != 0;
                ret["description"] = "Fast tape loading (bypasses audio emulation)";
            } else if (name == "fast_disk") {
                ret["name"] = "fast_disk";
                ret["value"] = config.wd93_nodelay;
                ret["description"] = "Fast disk I/O (removes WD1793 controller delays)";
            } else if (name == "trdos_present") {
                ret["name"] = "trdos_present";
                ret["value"] = config.trdos_present;
                ret["description"] = "Enable Beta128 TR-DOS disk interface";
            } else if (name == "trdos_traps") {
                ret["name"] = "trdos_traps";
                ret["value"] = config.trdos_traps;
                ret["description"] = "Use TR-DOS traps for faster disk operations";
            } else {
                Json::Value error;
                error["error"] = "Not Found";
                error["message"] = "Unknown setting: " + name;
                
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k404NotFound);
                callback(resp);
                return;
            }
            
            ret["emulator_id"] = id;
            
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            callback(resp);
        }

        // PUT/POST /api/v1/emulator/:id/settings/:name
        // Set a specific setting value
        void EmulatorAPI::setSetting(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, const std::string &id, const std::string &name) const
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
            
            EmulatorContext* context = emulator->GetContext();
            if (!context) {
                Json::Value error;
                error["error"] = "Internal Error";
                error["message"] = "Unable to access emulator context";
                
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k500InternalServerError);
                callback(resp);
                return;
            }
            
            // Parse request body for the new value
            auto json = req->getJsonObject();
            if (!json || !json->isMember("value")) {
                Json::Value error;
                error["error"] = "Bad Request";
                error["message"] = "Missing 'value' field in request body";
                
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k400BadRequest);
                callback(resp);
                return;
            }
            
            bool boolValue = (*json)["value"].asBool();
            CONFIG& config = context->config;
            Json::Value ret;
            
            if (name == "fast_tape") {
                config.tape_traps = boolValue ? 1 : 0;
                ret["name"] = "fast_tape";
                ret["value"] = boolValue;
                ret["message"] = std::string("Fast tape loading is now ") + (boolValue ? "enabled" : "disabled");
            } else if (name == "fast_disk") {
                config.wd93_nodelay = boolValue;
                ret["name"] = "fast_disk";
                ret["value"] = boolValue;
                ret["message"] = std::string("Fast disk I/O is now ") + (boolValue ? "enabled" : "disabled");
            } else if (name == "trdos_present") {
                config.trdos_present = boolValue;
                ret["name"] = "trdos_present";
                ret["value"] = boolValue;
                ret["message"] = std::string("TR-DOS interface is now ") + (boolValue ? "enabled" : "disabled");
                ret["restart_required"] = true;
            } else if (name == "trdos_traps") {
                config.trdos_traps = boolValue;
                ret["name"] = "trdos_traps";
                ret["value"] = boolValue;
                ret["message"] = std::string("TR-DOS traps are now ") + (boolValue ? "enabled" : "disabled");
            } else {
                Json::Value error;
                error["error"] = "Not Found";
                error["message"] = "Unknown setting: " + name;
                
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k404NotFound);
                callback(resp);
                return;
            }
            
            ret["emulator_id"] = id;
            
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            callback(resp);
        }
        
        // GET /api/v1/emulator/{id}/state/screen
        // Get screen configuration
        // GET /api/v1/emulator/{id}/state/memory
        // Get complete memory configuration
        void EmulatorAPI::getStateMemory(const HttpRequestPtr &req,
                                        std::function<void(const HttpResponsePtr &)> &&callback,
                                        const std::string &id) const
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
            
            EmulatorContext* context = emulator->GetContext();
            if (!context) {
                Json::Value error;
                error["error"] = "Internal Error";
                error["message"] = "Unable to access emulator context";
                
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k500InternalServerError);
                callback(resp);
                return;
            }
            
            CONFIG& config = context->config;
            Memory& memory = *context->pMemory;
            EmulatorState& state = context->emulatorState;
            Json::Value ret;
            
            // Model information
            std::string model = "ZX Spectrum 48K";
            if (config.mem_model == MM_SPECTRUM128)
                model = "ZX Spectrum 128K";
            else if (config.mem_model == MM_PENTAGON)
                model = "Pentagon 128K";
            else if (config.mem_model == MM_PLUS3)
                model = "ZX Spectrum +3";
            
            ret["model"] = model;
            
            // ROM configuration
            Json::Value rom;
            rom["active_page"] = static_cast<int>(memory.GetROMPage());
            rom["is_bank0_rom"] = memory.IsBank0ROM();
            ret["rom"] = rom;
            
            // RAM configuration
            Json::Value ram;
            ram["bank0"] = memory.IsBank0ROM() ? Json::Value::null : static_cast<int>(memory.GetRAMPageForBank0());
            ram["bank1"] = static_cast<int>(memory.GetRAMPageForBank1());
            ram["bank2"] = static_cast<int>(memory.GetRAMPageForBank2());
            ram["bank3"] = static_cast<int>(memory.GetRAMPageForBank3());
            ret["ram"] = ram;
            
            // Paging state (if applicable)
            if (config.mem_model != MM_SPECTRUM48)
            {
                Json::Value paging;
                paging["port_7ffd"] = static_cast<int>(state.p7FFD);
                paging["ram_bank_3"] = static_cast<int>(state.p7FFD & 0x07);
                paging["screen"] = (state.p7FFD & 0x08) ? 1 : 0;
                paging["rom_select"] = (state.p7FFD & 0x10) ? 1 : 0;
                paging["locked"] = (state.p7FFD & 0x20) ? true : false;
                ret["paging"] = paging;
            }
            
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            callback(resp);
        }
        
        // GET /api/v1/emulator/{id}/state/memory/ram
        // Get RAM banking details
        void EmulatorAPI::getStateMemoryRAM(const HttpRequestPtr &req,
                                           std::function<void(const HttpResponsePtr &)> &&callback,
                                           const std::string &id) const
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
            
            EmulatorContext* context = emulator->GetContext();
            if (!context) {
                Json::Value error;
                error["error"] = "Internal Error";
                error["message"] = "Unable to access emulator context";
                
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k500InternalServerError);
                callback(resp);
                return;
            }
            
            CONFIG& config = context->config;
            Memory& memory = *context->pMemory;
            EmulatorState& state = context->emulatorState;
            Json::Value ret;
            
            // Model
            std::string model = "ZX Spectrum 48K";
            if (config.mem_model == MM_SPECTRUM128)
                model = "ZX Spectrum 128K";
            else if (config.mem_model == MM_PENTAGON)
                model = "Pentagon 128K";
            else if (config.mem_model == MM_PLUS3)
                model = "ZX Spectrum +3";
            
            ret["model"] = model;
            
            // Bank mapping
            Json::Value banks;
            
            Json::Value bank0;
            bank0["address_range"] = "0x0000-0x3FFF";
            if (memory.IsBank0ROM())
            {
                bank0["type"] = "ROM";
                bank0["page"] = static_cast<int>(memory.GetROMPage());
                bank0["read_write"] = "read-only";
            }
            else
            {
                bank0["type"] = "RAM";
                bank0["page"] = static_cast<int>(memory.GetRAMPageForBank0());
                bank0["read_write"] = "read/write";
            }
            banks["bank0"] = bank0;
            
            Json::Value bank1;
            bank1["address_range"] = "0x4000-0x7FFF";
            bank1["type"] = "RAM";
            bank1["page"] = static_cast<int>(memory.GetRAMPageForBank1());
            bank1["read_write"] = "read/write";
            bank1["contended"] = true;
            bank1["note"] = "Screen 0 location";
            banks["bank1"] = bank1;
            
            Json::Value bank2;
            bank2["address_range"] = "0x8000-0xBFFF";
            bank2["type"] = "RAM";
            bank2["page"] = static_cast<int>(memory.GetRAMPageForBank2());
            bank2["read_write"] = "read/write";
            bank2["contended"] = false;
            banks["bank2"] = bank2;
            
            Json::Value bank3;
            bank3["address_range"] = "0xC000-0xFFFF";
            bank3["type"] = "RAM";
            bank3["page"] = static_cast<int>(memory.GetRAMPageForBank3());
            bank3["read_write"] = "read/write";
            bank3["contended"] = false;
            banks["bank3"] = bank3;
            
            ret["banks"] = banks;
            
            // Paging control (if applicable)
            if (config.mem_model != MM_SPECTRUM48)
            {
                Json::Value paging;
                paging["port_7ffd_hex"] = StringHelper::Format("0x%02X", state.p7FFD);
                paging["port_7ffd_value"] = static_cast<int>(state.p7FFD);
                paging["bits_0_2_ram"] = static_cast<int>(state.p7FFD & 0x07);
                paging["bit_3_screen"] = (state.p7FFD & 0x08) ? 1 : 0;
                paging["bit_4_rom"] = (state.p7FFD & 0x10) ? 1 : 0;
                paging["bit_5_lock"] = (state.p7FFD & 0x20) ? 1 : 0;
                ret["paging_control"] = paging;
            }
            
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            callback(resp);
        }
        
        // GET /api/v1/emulator/{id}/state/memory/rom
        // Get ROM configuration
        void EmulatorAPI::getStateMemoryROM(const HttpRequestPtr &req,
                                           std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                           const std::string &id) const
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
            
            EmulatorContext* context = emulator->GetContext();
            if (!context) {
                Json::Value error;
                error["error"] = "Internal Error";
                error["message"] = "Unable to access emulator context";
                
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k500InternalServerError);
                callback(resp);
                return;
            }
            
            CONFIG& config = context->config;
            Memory& memory = *context->pMemory;
            EmulatorState& state = context->emulatorState;
            Json::Value ret;
            
            // Model information
            std::string model = "ZX Spectrum 48K";
            int totalROMPages = 1;
            if (config.mem_model == MM_SPECTRUM128)
            {
                model = "ZX Spectrum 128K";
                totalROMPages = 2;
            }
            else if (config.mem_model == MM_PENTAGON)
            {
                model = "Pentagon 128K";
                totalROMPages = 4;
            }
            else if (config.mem_model == MM_PLUS3)
            {
                model = "ZX Spectrum +3";
                totalROMPages = 4;
            }
            
            ret["model"] = model;
            ret["total_rom_pages"] = totalROMPages;
            ret["active_rom_page"] = static_cast<int>(memory.GetROMPage());
            ret["rom_size_kb"] = totalROMPages * 16;
            
            // Available ROM pages
            Json::Value pages = Json::arrayValue;
            
            if (config.mem_model == MM_SPECTRUM48)
            {
                Json::Value page;
                page["page"] = 0;
                page["description"] = "48K BASIC ROM";
                page["active"] = true;
                pages.append(page);
            }
            else if (config.mem_model == MM_SPECTRUM128)
            {
                Json::Value page0;
                page0["page"] = 0;
                page0["description"] = "128K Editor/Menu ROM";
                page0["active"] = (memory.GetROMPage() == 0);
                pages.append(page0);
                
                Json::Value page1;
                page1["page"] = 1;
                page1["description"] = "48K BASIC ROM";
                page1["active"] = (memory.GetROMPage() == 1);
                pages.append(page1);
            }
            else if (config.mem_model == MM_PENTAGON)
            {
                Json::Value page0;
                page0["page"] = 0;
                page0["description"] = "Service ROM";
                page0["active"] = (memory.GetROMPage() == 0);
                pages.append(page0);
                
                Json::Value page1;
                page1["page"] = 1;
                page1["description"] = "TR-DOS ROM";
                page1["active"] = (memory.GetROMPage() == 1);
                pages.append(page1);
                
                Json::Value page2;
                page2["page"] = 2;
                page2["description"] = "128K Editor/Menu ROM";
                page2["active"] = (memory.GetROMPage() == 2);
                pages.append(page2);
                
                Json::Value page3;
                page3["page"] = 3;
                page3["description"] = "48K BASIC ROM";
                page3["active"] = (memory.GetROMPage() == 3);
                pages.append(page3);
            }
            else if (config.mem_model == MM_PLUS3)
            {
                Json::Value page0;
                page0["page"] = 0;
                page0["description"] = "+3 Editor ROM";
                page0["active"] = (memory.GetROMPage() == 0);
                pages.append(page0);
                
                Json::Value page1;
                page1["page"] = 1;
                page1["description"] = "48K BASIC ROM";
                page1["active"] = (memory.GetROMPage() == 1);
                pages.append(page1);
                
                Json::Value page2;
                page2["page"] = 2;
                page2["description"] = "+3DOS ROM";
                page2["active"] = (memory.GetROMPage() == 2);
                pages.append(page2);
                
                Json::Value page3;
                page3["page"] = 3;
                page3["description"] = "48K BASIC ROM (copy)";
                page3["active"] = (memory.GetROMPage() == 3);
                pages.append(page3);
            }
            
            ret["pages"] = pages;
            
            // Current mapping
            Json::Value mapping;
            if (memory.IsBank0ROM())
            {
                mapping["bank0_type"] = "ROM";
                mapping["bank0_page"] = static_cast<int>(memory.GetROMPage());
                mapping["bank0_access"] = "read-only";
            }
            else
            {
                mapping["bank0_type"] = "RAM";
                mapping["bank0_page"] = static_cast<int>(memory.GetRAMPageForBank0());
                mapping["bank0_access"] = "read/write";
            }
            ret["mapping"] = mapping;
            
            // Port info (if applicable)
            if (config.mem_model != MM_SPECTRUM48)
            {
                ret["port_7ffd_bit4_rom_select"] = (state.p7FFD & 0x10) ? 1 : 0;
            }
            
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            callback(resp);
        }
        
        void EmulatorAPI::getStateScreen(const HttpRequestPtr &req, 
                                        std::function<void(const HttpResponsePtr &)> &&callback,
                                        const std::string &id) const
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
            
            EmulatorContext* context = emulator->GetContext();
            if (!context) {
                Json::Value error;
                error["error"] = "Internal Error";
                error["message"] = "Unable to access emulator context";
                
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k500InternalServerError);
                callback(resp);
                return;
            }
            
            CONFIG& config = context->config;
            Json::Value ret;
            
            // Check if verbose mode is requested
            bool verbose = false;
            auto params = req->getParameters();
            if (params.find("verbose") != params.end()) {
                std::string verboseParam = params.at("verbose");
                verbose = (verboseParam == "true" || verboseParam == "1" || verboseParam == "yes");
            }
            
            bool is128K = (config.mem_model == MM_SPECTRUM128 || 
                          config.mem_model == MM_PENTAGON || 
                          config.mem_model == MM_PLUS3);
            
            std::string model = "ZX Spectrum 48K";
            if (config.mem_model == MM_SPECTRUM128)
                model = "ZX Spectrum 128K";
            else if (config.mem_model == MM_PENTAGON)
                model = "Pentagon 128K";
            else if (config.mem_model == MM_PLUS3)
                model = "ZX Spectrum +3";
            
            ret["model"] = model;
            ret["is_128k"] = is128K;
            ret["display_mode"] = "standard";
            ret["border_color"] = static_cast<int>(context->pScreen->GetBorderColor());
            
            if (is128K) {
                uint8_t port7FFD = context->emulatorState.p7FFD;
                bool shadowScreen = (port7FFD & 0x08) != 0;
                
                ret["active_screen"] = shadowScreen ? 1 : 0;
                ret["active_ram_page"] = shadowScreen ? 7 : 5;
            } else {
                ret["active_screen"] = 0;
                ret["active_ram_page"] = 5;
            }
            
            // Only include verbose details if requested
            if (!verbose) {
                auto resp = HttpResponse::newHttpJsonResponse(ret);
                callback(resp);
                return;
            }
            
            // Verbose mode - add detailed information
            if (is128K) {
                uint8_t port7FFD = context->emulatorState.p7FFD;
                bool shadowScreen = (port7FFD & 0x08) != 0;
                uint8_t ramBank = port7FFD & 0x07;
                
                ret["active_screen"] = shadowScreen ? 1 : 0;
                
                // Screen 0 info
                Json::Value screen0;
                screen0["name"] = "Screen 0 (normal)";
                screen0["ram_page"] = 5;
                screen0["physical_offset"] = "0x0000-0x1FFF";
                screen0["pixel_data"] = "0x0000-0x17FF (6144 bytes)";
                screen0["attributes"] = "0x1800-0x1AFF (768 bytes)";
                screen0["z80_access"] = "0x4000-0x7FFF (bank 1 - always accessible)";
                screen0["ula_display"] = !shadowScreen;
                screen0["contention"] = "active";
                ret["screen_0"] = screen0;
                
                // Screen 1 info
                Json::Value screen1;
                screen1["name"] = "Screen 1 (shadow)";
                screen1["ram_page"] = 7;
                screen1["physical_offset"] = "0x0000-0x1FFF";
                screen1["pixel_data"] = "0x0000-0x17FF (6144 bytes)";
                screen1["attributes"] = "0x1800-0x1AFF (768 bytes)";
                screen1["z80_access"] = (ramBank == 7) ? "0xC000-0xFFFF (bank 3, page 7 mapped)" : "not mapped";
                screen1["ula_display"] = shadowScreen;
                screen1["contention"] = (ramBank == 7) ? "inactive" : "n/a";
                ret["screen_1"] = screen1;
                
                // Port 0x7FFD info
                Json::Value port7FFD_info;
                char hexStr[5];
                snprintf(hexStr, sizeof(hexStr), "0x%02X", port7FFD);
                port7FFD_info["value_hex"] = hexStr;
                port7FFD_info["value_dec"] = port7FFD;
                
                std::string binary;
                for (int i = 7; i >= 0; i--)
                    binary += (port7FFD >> i) & 1 ? '1' : '0';
                port7FFD_info["value_bin"] = binary;
                
                port7FFD_info["ram_bank"] = ramBank;
                port7FFD_info["shadow_screen"] = shadowScreen;
                port7FFD_info["rom_select"] = (port7FFD & 0x10) ? "48K BASIC" : "128K Editor";
                port7FFD_info["paging_locked"] = (port7FFD & 0x20) != 0;
                
                ret["port_0x7FFD"] = port7FFD_info;
            } else {
                // 48K model - single screen
                Json::Value screen;
                screen["name"] = "Single screen";
                screen["physical_location"] = "RAM page 5, offset 0x0000-0x1FFF";
                screen["pixel_data"] = "0x4000-0x57FF (6144 bytes)";
                screen["attributes"] = "0x5800-0x5AFF (768 bytes)";
                screen["z80_access"] = "0x4000-0x7FFF (always accessible)";
                screen["contention"] = "active";
                ret["screen"] = screen;
            }
            
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            callback(resp);
        }
        
        // GET /api/v1/emulator/{id}/state/screen/mode
        // Get video mode details
        void EmulatorAPI::getStateScreenMode(const HttpRequestPtr &req, 
                                            std::function<void(const HttpResponsePtr &)> &&callback,
                                            const std::string &id) const
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
            
            EmulatorContext* context = emulator->GetContext();
            if (!context) {
                Json::Value error;
                error["error"] = "Internal Error";
                error["message"] = "Unable to access emulator context";
                
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k500InternalServerError);
                callback(resp);
                return;
            }
            
            CONFIG& config = context->config;
            Json::Value ret;
            
            std::string model = "ZX Spectrum 48K";
            if (config.mem_model == MM_SPECTRUM128)
                model = "ZX Spectrum 128K";
            else if (config.mem_model == MM_PENTAGON)
                model = "Pentagon 128K";
            else if (config.mem_model == MM_PLUS3)
                model = "ZX Spectrum +3";
            
            ret["model"] = model;
            ret["video_mode"] = "standard";
            ret["resolution"] = "256×192";
            ret["color_depth"] = "2 colors per attribute block";
            ret["attribute_size"] = "8×8 pixels";
            
            Json::Value memory;
            memory["pixel_data_bytes"] = 6144;
            memory["attribute_bytes"] = 768;
            memory["total_bytes"] = 6912;
            ret["memory_layout"] = memory;
            
            if (config.mem_model == MM_SPECTRUM128 || 
                config.mem_model == MM_PENTAGON || 
                config.mem_model == MM_PLUS3) {
                uint8_t port7FFD = context->emulatorState.p7FFD;
                bool shadowScreen = (port7FFD & 0x08) != 0;
                ret["active_screen"] = shadowScreen ? 1 : 0;
                ret["active_ram_page"] = shadowScreen ? 7 : 5;
            }
            
            ret["compatibility"] = "48K/128K/+2/+2A/+3 standard";
            
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            callback(resp);
        }
        
        // GET /api/v1/emulator/{id}/state/screen/flash
        // Get flash state
        void EmulatorAPI::getStateScreenFlash(const HttpRequestPtr &req, 
                                             std::function<void(const HttpResponsePtr &)> &&callback,
                                             const std::string &id) const
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
            
            EmulatorContext* context = emulator->GetContext();
            if (!context) {
                Json::Value error;
                error["error"] = "Internal Error";
                error["message"] = "Unable to access emulator context";
                
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k500InternalServerError);
                callback(resp);
                return;
            }
            
            EmulatorState& state = context->emulatorState;
            Json::Value ret;
            
            uint8_t flashCounter = (state.frame_counter / 16) & 1;
            uint8_t framesUntilToggle = 16 - (state.frame_counter % 16);
            
            ret["flash_phase"] = flashCounter ? "inverted" : "normal";
            ret["frames_until_toggle"] = framesUntilToggle;
            ret["flash_cycle_position"] = state.frame_counter % 32;
            ret["flash_cycle_total"] = 32;
            ret["toggle_interval_frames"] = 16;
            ret["toggle_interval_seconds"] = 0.32;  // at 50Hz
            
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            callback(resp);
        }
    }
}
