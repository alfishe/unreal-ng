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
                
                // Settings management
                METHOD_ADD(EmulatorAPI::getSettings, "/{id}/settings", drogon::Get);
                METHOD_ADD(EmulatorAPI::getSetting, "/{id}/settings/{name}", drogon::Get);
                METHOD_ADD(EmulatorAPI::setSetting, "/{id}/settings/{name}", drogon::Put, drogon::Post);
                
                // State inspection
                METHOD_ADD(EmulatorAPI::getStateMemory, "/{id}/state/memory", drogon::Get);
                METHOD_ADD(EmulatorAPI::getStateMemoryRAM, "/{id}/state/memory/ram", drogon::Get);
                METHOD_ADD(EmulatorAPI::getStateMemoryROM, "/{id}/state/memory/rom", drogon::Get);
                METHOD_ADD(EmulatorAPI::getStateScreen, "/{id}/state/screen", drogon::Get);
                METHOD_ADD(EmulatorAPI::getStateScreenMode, "/{id}/state/screen/mode", drogon::Get);
                METHOD_ADD(EmulatorAPI::getStateScreenFlash, "/{id}/state/screen/flash", drogon::Get);
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
            
            // Settings management
            void getSettings(const drogon::HttpRequestPtr &req,
                           std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                           const std::string &id) const;
                           
            void getSetting(const drogon::HttpRequestPtr &req,
                          std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                          const std::string &id,
                          const std::string &name) const;
                          
            void setSetting(const drogon::HttpRequestPtr &req,
                          std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                          const std::string &id,
                          const std::string &name) const;
            
            // State inspection
            void getStateMemory(const drogon::HttpRequestPtr &req,
                              std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                              const std::string &id) const;
                              
            void getStateMemoryRAM(const drogon::HttpRequestPtr &req,
                                 std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                 const std::string &id) const;
                                 
            void getStateMemoryROM(const drogon::HttpRequestPtr &req,
                                 std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                 const std::string &id) const;
                                 
            void getStateScreen(const drogon::HttpRequestPtr &req,
                              std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                              const std::string &id) const;
                              
            void getStateScreenMode(const drogon::HttpRequestPtr &req,
                                  std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                  const std::string &id) const;
                                  
            void getStateScreenFlash(const drogon::HttpRequestPtr &req,
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