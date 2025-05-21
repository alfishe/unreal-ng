#pragma once

#include <drogon/HttpController.h>
#include <emulator/emulator.h>

namespace api
{
    namespace v1
    {
        class EmulatorAPI : public drogon::HttpController<EmulatorAPI>
        {
        public:
            EmulatorAPI() = default;

            METHOD_LIST_BEGIN
                // List all emulators
                METHOD_ADD(EmulatorAPI::get, "", drogon::Get);
                
                // Get overall emulator status
                METHOD_ADD(EmulatorAPI::status, "/status", drogon::Get);
                
                // Create a new emulator
                METHOD_ADD(EmulatorAPI::createEmulator, "", drogon::Post);
                
                // Get emulator details
                METHOD_ADD(EmulatorAPI::getEmulator, "/{id}", drogon::Get);
                
                // Remove an emulator
                METHOD_ADD(EmulatorAPI::removeEmulator, "/{id}", drogon::Delete);
                
                // Control emulator state
                METHOD_ADD(EmulatorAPI::startEmulator, "/{id}/start", drogon::Post);
                METHOD_ADD(EmulatorAPI::stopEmulator, "/{id}/stop", drogon::Post);
                METHOD_ADD(EmulatorAPI::pauseEmulator, "/{id}/pause", drogon::Post);
                METHOD_ADD(EmulatorAPI::resumeEmulator, "/{id}/resume", drogon::Post);
            METHOD_LIST_END

            // List all emulators
            void get(const drogon::HttpRequestPtr &req, 
                    std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;
            
            // Get overall emulator status
            void status(const drogon::HttpRequestPtr &req, 
                       std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;
            
            // Create a new emulator
            void createEmulator(const drogon::HttpRequestPtr &req,
                              std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;
            
            // Get emulator details
            void getEmulator(const drogon::HttpRequestPtr &req,
                           std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                           const std::string &id) const;
            
            // Remove an emulator
            void removeEmulator(const drogon::HttpRequestPtr &req,
                              std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                              const std::string &id) const;
            
            // Control emulator state
            void startEmulator(const drogon::HttpRequestPtr &req,
                             std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                             const std::string &id) const;
                              
            void stopEmulator(const drogon::HttpRequestPtr &req,
                            std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                            const std::string &id) const;
                            
            void pauseEmulator(const drogon::HttpRequestPtr &req,
                             std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                             const std::string &id) const;
                             
            void resumeEmulator(const drogon::HttpRequestPtr &req,
                              std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                              const std::string &id) const;
        
        private:
            // Helper method to handle emulator actions with common error handling
            void handleEmulatorAction(
                const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                const std::string &id,
                std::function<std::string(std::shared_ptr<Emulator>)> action) const;
        };
    } // namespace v1
} // namespace api