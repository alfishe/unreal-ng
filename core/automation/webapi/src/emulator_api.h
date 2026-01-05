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
                // Root redirect to OpenAPI
                ADD_METHOD_TO(EmulatorAPI::rootRedirect, "/", drogon::Get);
                ADD_METHOD_TO(EmulatorAPI::corsPreflight, "/", drogon::Options);

                // OpenAPI specification
                ADD_METHOD_TO(EmulatorAPI::getOpenAPISpec, "/api/v1/openapi.json", drogon::Get);
                ADD_METHOD_TO(EmulatorAPI::corsPreflight, "/api/v1/openapi.json", drogon::Options);

                // List all emulators
                ADD_METHOD_TO(EmulatorAPI::get, "/api/v1/emulator", drogon::Get);

                // Get overall emulator status
                ADD_METHOD_TO(EmulatorAPI::status, "/api/v1/emulator/status", drogon::Get);

                // Get available models
                ADD_METHOD_TO(EmulatorAPI::getModels, "/api/v1/emulator/models", drogon::Get);

                // Create a new emulator
                ADD_METHOD_TO(EmulatorAPI::createEmulator, "/api/v1/emulator", drogon::Post);

                // Get emulator details
                ADD_METHOD_TO(EmulatorAPI::getEmulator, "/api/v1/emulator/{id}", drogon::Get);

                // Remove an emulator
                ADD_METHOD_TO(EmulatorAPI::removeEmulator, "/api/v1/emulator/{id}", drogon::Delete);

                // Control emulator state
                ADD_METHOD_TO(EmulatorAPI::startEmulator, "/api/v1/emulator/{id}/start", drogon::Post);
                ADD_METHOD_TO(EmulatorAPI::stopEmulator, "/api/v1/emulator/{id}/stop", drogon::Post);
                ADD_METHOD_TO(EmulatorAPI::pauseEmulator, "/api/v1/emulator/{id}/pause", drogon::Post);
                ADD_METHOD_TO(EmulatorAPI::resumeEmulator, "/api/v1/emulator/{id}/resume", drogon::Post);
                ADD_METHOD_TO(EmulatorAPI::resetEmulator, "/api/v1/emulator/{id}/reset", drogon::Post);

                // Settings management
                ADD_METHOD_TO(EmulatorAPI::getSettings, "/api/v1/emulator/{id}/settings", drogon::Get);
                ADD_METHOD_TO(EmulatorAPI::getSetting, "/api/v1/emulator/{id}/settings/{name}", drogon::Get);
                ADD_METHOD_TO(EmulatorAPI::setSetting, "/api/v1/emulator/{id}/settings/{name}", drogon::Put, drogon::Post);

                // State inspection
                ADD_METHOD_TO(EmulatorAPI::getStateMemory, "/api/v1/emulator/{id}/state/memory", drogon::Get);
                ADD_METHOD_TO(EmulatorAPI::getStateMemoryRAM, "/api/v1/emulator/{id}/state/memory/ram", drogon::Get);
                ADD_METHOD_TO(EmulatorAPI::getStateMemoryROM, "/api/v1/emulator/{id}/state/memory/rom", drogon::Get);
                ADD_METHOD_TO(EmulatorAPI::getStateScreen, "/api/v1/emulator/{id}/state/screen", drogon::Get);
                ADD_METHOD_TO(EmulatorAPI::getStateScreenMode, "/api/v1/emulator/{id}/state/screen/mode", drogon::Get);
                ADD_METHOD_TO(EmulatorAPI::getStateScreenFlash, "/api/v1/emulator/{id}/state/screen/flash", drogon::Get);

                // Audio state inspection (with emulator ID)
                ADD_METHOD_TO(EmulatorAPI::getStateAudioAY, "/api/v1/emulator/{id}/state/audio/ay", drogon::Get);
                ADD_METHOD_TO(EmulatorAPI::getStateAudioAYIndex, "/api/v1/emulator/{id}/state/audio/ay/{chip}", drogon::Get);
                ADD_METHOD_TO(EmulatorAPI::getStateAudioAYRegister, "/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}", drogon::Get);
                ADD_METHOD_TO(EmulatorAPI::getStateAudioBeeper, "/api/v1/emulator/{id}/state/audio/beeper", drogon::Get);
                ADD_METHOD_TO(EmulatorAPI::getStateAudioGS, "/api/v1/emulator/{id}/state/audio/gs", drogon::Get);
                ADD_METHOD_TO(EmulatorAPI::getStateAudioCovox, "/api/v1/emulator/{id}/state/audio/covox", drogon::Get);
                ADD_METHOD_TO(EmulatorAPI::getStateAudioChannels, "/api/v1/emulator/{id}/state/audio/channels", drogon::Get);

                // Audio state inspection (active emulator - no ID required)
                ADD_METHOD_TO(EmulatorAPI::getStateAudioAYActive, "/api/v1/emulator/state/audio/ay", drogon::Get);
                ADD_METHOD_TO(EmulatorAPI::getStateAudioAYIndexActive, "/api/v1/emulator/state/audio/ay/{chip}", drogon::Get);
                ADD_METHOD_TO(EmulatorAPI::getStateAudioAYRegisterActive, "/api/v1/emulator/state/audio/ay/{chip}/register/{reg}", drogon::Get);
                ADD_METHOD_TO(EmulatorAPI::getStateAudioBeeperActive, "/api/v1/emulator/state/audio/beeper", drogon::Get);
                ADD_METHOD_TO(EmulatorAPI::getStateAudioGSActive, "/api/v1/emulator/state/audio/gs", drogon::Get);
                ADD_METHOD_TO(EmulatorAPI::getStateAudioCovoxActive, "/api/v1/emulator/state/audio/covox", drogon::Get);
                ADD_METHOD_TO(EmulatorAPI::getStateAudioChannelsActive, "/api/v1/emulator/state/audio/channels", drogon::Get);
            METHOD_LIST_END

            // Root redirect
            void rootRedirect(const drogon::HttpRequestPtr &req,
                             std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

            // CORS preflight handler
            void corsPreflight(const drogon::HttpRequestPtr &req,
                              std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

            // OpenAPI specification
            void getOpenAPISpec(const drogon::HttpRequestPtr &req,
                               std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

            // List all emulators
            void get(const drogon::HttpRequestPtr &req, 
                    std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;
            
            // Get overall emulator status
            void status(const drogon::HttpRequestPtr &req,
                       std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

            // Get available models
            void getModels(const drogon::HttpRequestPtr &req,
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

            void resetEmulator(const drogon::HttpRequestPtr &req,
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

            // Audio state inspection
            void getStateAudioAY(const drogon::HttpRequestPtr &req,
                               std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                               const std::string &id) const;

            void getStateAudioAYIndex(const drogon::HttpRequestPtr &req,
                                    std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                    const std::string &id,
                                    const std::string &chip) const;

            void getStateAudioAYRegister(const drogon::HttpRequestPtr &req,
                                       std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                       const std::string &id,
                                       const std::string &chip,
                                       const std::string &reg) const;

            void getStateAudioBeeper(const drogon::HttpRequestPtr &req,
                                   std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                   const std::string &id) const;

            void getStateAudioGS(const drogon::HttpRequestPtr &req,
                               std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                               const std::string &id) const;

            void getStateAudioCovox(const drogon::HttpRequestPtr &req,
                                  std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                  const std::string &id) const;

            void getStateAudioChannels(const drogon::HttpRequestPtr &req,
                                     std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                     const std::string &id) const;

            // Audio state inspection (active emulator - no ID required)
            void getStateAudioAYActive(const drogon::HttpRequestPtr &req,
                                      std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

            void getStateAudioAYIndexActive(const drogon::HttpRequestPtr &req,
                                           std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                           const std::string &chip) const;

            void getStateAudioAYRegisterActive(const drogon::HttpRequestPtr &req,
                                              std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                              const std::string &chip,
                                              const std::string &reg) const;

            void getStateAudioBeeperActive(const drogon::HttpRequestPtr &req,
                                          std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

            void getStateAudioGSActive(const drogon::HttpRequestPtr &req,
                                      std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

            void getStateAudioCovoxActive(const drogon::HttpRequestPtr &req,
                                         std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

            void getStateAudioChannelsActive(const drogon::HttpRequestPtr &req,
                                            std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

        private:
            /**
             * @brief Get emulator by ID (UUID) or index (numeric)
             * @param idOrIndex Either a UUID string or numeric index (0-based)
             * @return Shared pointer to emulator, or nullptr if not found
             */
            std::shared_ptr<Emulator> getEmulatorByIdOrIndex(const std::string& idOrIndex) const;
            // Helper method to handle emulator actions with common error handling
            void handleEmulatorAction(
                const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                const std::string &id,
                std::function<std::string(std::shared_ptr<Emulator>)> action) const;
        };
    } // namespace v1
} // namespace api