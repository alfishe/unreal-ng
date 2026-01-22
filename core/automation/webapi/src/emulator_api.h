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
    // region Root and OpenAPI (implementation: emulator_api.cpp)
    // Root redirect to OpenAPI (implementation: emulator_api.cpp)
    ADD_METHOD_TO(EmulatorAPI::rootRedirect, "/", drogon::Get);

    // OpenAPI specification
    ADD_METHOD_TO(EmulatorAPI::getOpenAPISpec, "/api/v1/openapi.json", drogon::Get);
    // endregion Root and OpenAPI

    // Note: CORS preflight (OPTIONS) requests are handled globally via registerSyncAdvice
    // in automation-webapi.cpp and main.cpp, so no need to register OPTIONS handlers here

    // region Lifecycle Management (implementation: api/lifecycle_api.cpp)
    // Lifecycle Management (implementation: api/lifecycle_api.cpp)
    // List all emulators
    ADD_METHOD_TO(EmulatorAPI::get, "/api/v1/emulator", drogon::Get);

    // Get overall emulator status
    ADD_METHOD_TO(EmulatorAPI::status, "/api/v1/emulator/status", drogon::Get);

    // Get available models
    ADD_METHOD_TO(EmulatorAPI::getModels, "/api/v1/emulator/models", drogon::Get);

    // Create a new emulator (without starting)
    ADD_METHOD_TO(EmulatorAPI::createEmulator, "/api/v1/emulator/create", drogon::Post);

    // Get emulator details
    ADD_METHOD_TO(EmulatorAPI::getEmulator, "/api/v1/emulator/{id}", drogon::Get);

    // Remove an emulator
    ADD_METHOD_TO(EmulatorAPI::removeEmulator, "/api/v1/emulator/{id}", drogon::Delete);

    // Control emulator state
    ADD_METHOD_TO(EmulatorAPI::startEmulator, "/api/v1/emulator/start",
                  drogon::Post);  // Create and start a new emulator
    ADD_METHOD_TO(EmulatorAPI::startExistingEmulator, "/api/v1/emulator/{id}/start",
                  drogon::Post);  // Start an existing emulator
    ADD_METHOD_TO(EmulatorAPI::stopEmulator, "/api/v1/emulator/{id}/stop", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::pauseEmulator, "/api/v1/emulator/{id}/pause", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::resumeEmulator, "/api/v1/emulator/{id}/resume", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::resetEmulator, "/api/v1/emulator/{id}/reset", drogon::Post);
    // endregion Lifecycle Management

    // region Tape/Disk/Snapshot Control (implementation: api/tape_disk_api.cpp and api/snapshot_api.cpp)
    // Tape control
    ADD_METHOD_TO(EmulatorAPI::loadTape, "/api/v1/emulator/{id}/tape/load", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::ejectTape, "/api/v1/emulator/{id}/tape/eject", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::playTape, "/api/v1/emulator/{id}/tape/play", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::stopTape, "/api/v1/emulator/{id}/tape/stop", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::rewindTape, "/api/v1/emulator/{id}/tape/rewind", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getTapeInfo, "/api/v1/emulator/{id}/tape/info", drogon::Get);

    // Disk control
    ADD_METHOD_TO(EmulatorAPI::insertDisk, "/api/v1/emulator/{id}/disk/{drive}/insert", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::createDisk, "/api/v1/emulator/{id}/disk/{drive}/create", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::ejectDisk, "/api/v1/emulator/{id}/disk/{drive}/eject", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getDiskInfo, "/api/v1/emulator/{id}/disk/{drive}/info", drogon::Get);
    
    // Disk inspection - drive listing
    ADD_METHOD_TO(EmulatorAPI::getDiskDrives, "/api/v1/emulator/{id}/disk", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getDiskDrive, "/api/v1/emulator/{id}/disk/{drive}", drogon::Get);
    
    // Disk inspection - sector data
    ADD_METHOD_TO(EmulatorAPI::getDiskSector, "/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getDiskSectorRaw, "/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}/raw", drogon::Get);
    
    // Disk inspection - track data
    ADD_METHOD_TO(EmulatorAPI::getDiskTrack, "/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getDiskTrackRaw, "/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}/raw", drogon::Get);
    
    // Disk inspection - whole image and system info
    ADD_METHOD_TO(EmulatorAPI::getDiskImage, "/api/v1/emulator/{id}/disk/{drive}/image", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getDiskSysinfo, "/api/v1/emulator/{id}/disk/{drive}/sysinfo", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getDiskCatalog, "/api/v1/emulator/{id}/disk/{drive}/catalog", drogon::Get);

    // Snapshot control
    ADD_METHOD_TO(EmulatorAPI::loadSnapshot, "/api/v1/emulator/{id}/snapshot/load", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::saveSnapshot, "/api/v1/emulator/{id}/snapshot/save", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getSnapshotInfo, "/api/v1/emulator/{id}/snapshot/info", drogon::Get);
    // endregion Tape/Disk/Snapshot Control

    // region Settings Management (implementation: api/settings_api.cpp)
    // Settings management
    ADD_METHOD_TO(EmulatorAPI::getSettings, "/api/v1/emulator/{id}/settings", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getSetting, "/api/v1/emulator/{id}/settings/{name}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::setSetting, "/api/v1/emulator/{id}/settings/{name}", drogon::Put, drogon::Post);
    // endregion Settings Management

    // region Feature Management (implementation: api/features_api.cpp)
    // Feature management
    ADD_METHOD_TO(EmulatorAPI::getFeatures, "/api/v1/emulator/{id}/features", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getFeature, "/api/v1/emulator/{id}/feature/{name}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::setFeature, "/api/v1/emulator/{id}/feature/{name}", drogon::Put, drogon::Post);
    // endregion Feature Management

    // region Memory State (implementation: api/state_memory_api.cpp)
    // State inspection
    ADD_METHOD_TO(EmulatorAPI::getStateMemory, "/api/v1/emulator/{id}/state/memory", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateMemoryRAM, "/api/v1/emulator/{id}/state/memory/ram", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateMemoryROM, "/api/v1/emulator/{id}/state/memory/rom", drogon::Get);
    // endregion Memory State

    // region Screen State (implementation: api/state_screen_api.cpp)
    ADD_METHOD_TO(EmulatorAPI::getStateScreen, "/api/v1/emulator/{id}/state/screen", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateScreenMode, "/api/v1/emulator/{id}/state/screen/mode", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateScreenFlash, "/api/v1/emulator/{id}/state/screen/flash", drogon::Get);
    // endregion Screen State

    // region Audio State (implementation: api/state_audio_api.cpp)
    // Audio state inspection (with emulator ID)
    ADD_METHOD_TO(EmulatorAPI::getStateAudioAY, "/api/v1/emulator/{id}/state/audio/ay", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioAYIndex, "/api/v1/emulator/{id}/state/audio/ay/{chip}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioAYRegister, "/api/v1/emulator/{id}/state/audio/ay/{chip}/register/{reg}",
                  drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioBeeper, "/api/v1/emulator/{id}/state/audio/beeper", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioGS, "/api/v1/emulator/{id}/state/audio/gs", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioCovox, "/api/v1/emulator/{id}/state/audio/covox", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioChannels, "/api/v1/emulator/{id}/state/audio/channels", drogon::Get);

    // Audio state inspection (active emulator - no ID required)
    ADD_METHOD_TO(EmulatorAPI::getStateAudioAYActive, "/api/v1/emulator/state/audio/ay", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioAYIndexActive, "/api/v1/emulator/state/audio/ay/{chip}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioAYRegisterActive, "/api/v1/emulator/state/audio/ay/{chip}/register/{reg}",
                  drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioBeeperActive, "/api/v1/emulator/state/audio/beeper", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioGSActive, "/api/v1/emulator/state/audio/gs", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioCovoxActive, "/api/v1/emulator/state/audio/covox", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioChannelsActive, "/api/v1/emulator/state/audio/channels", drogon::Get);
    // endregion Audio State

    // region Batch Command Execution (implementation: api/batch_api.cpp)
    ADD_METHOD_TO(EmulatorAPI::executeBatch, "/api/v1/batch/execute", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getBatchableCommands, "/api/v1/batch/commands", drogon::Get);
    // endregion Batch Command Execution

    // region Debug Commands (implementation: api/debug_api.cpp)
    // Stepping
    ADD_METHOD_TO(EmulatorAPI::step, "/api/v1/emulator/{id}/step", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::steps, "/api/v1/emulator/{id}/steps", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::stepOver, "/api/v1/emulator/{id}/stepover", drogon::Post);
    
    // Debug mode
    ADD_METHOD_TO(EmulatorAPI::getDebugMode, "/api/v1/emulator/{id}/debugmode", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::setDebugMode, "/api/v1/emulator/{id}/debugmode", drogon::Put);
    
    // Breakpoints
    ADD_METHOD_TO(EmulatorAPI::getBreakpoints, "/api/v1/emulator/{id}/breakpoints", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::addBreakpoint, "/api/v1/emulator/{id}/breakpoints", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::clearBreakpoints, "/api/v1/emulator/{id}/breakpoints", drogon::Delete);
    ADD_METHOD_TO(EmulatorAPI::removeBreakpoint, "/api/v1/emulator/{id}/breakpoints/{bp_id}", drogon::Delete);
    ADD_METHOD_TO(EmulatorAPI::enableBreakpoint, "/api/v1/emulator/{id}/breakpoints/{bp_id}/enable", drogon::Put);
    ADD_METHOD_TO(EmulatorAPI::disableBreakpoint, "/api/v1/emulator/{id}/breakpoints/{bp_id}/disable", drogon::Put);
    ADD_METHOD_TO(EmulatorAPI::getBreakpointStatus, "/api/v1/emulator/{id}/breakpoints/status", drogon::Get);
    
    // Memory inspection
    ADD_METHOD_TO(EmulatorAPI::getRegisters, "/api/v1/emulator/{id}/registers", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getMemory, "/api/v1/emulator/{id}/memory/{addr}", drogon::Get);
    
    // Analysis
    ADD_METHOD_TO(EmulatorAPI::getMemCounters, "/api/v1/emulator/{id}/memcounters", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getCallTrace, "/api/v1/emulator/{id}/calltrace", drogon::Get);
    
    // Disassembly
    ADD_METHOD_TO(EmulatorAPI::getDisasm, "/api/v1/emulator/{id}/disasm", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getDisasmPage, "/api/v1/emulator/{id}/disasm/page", drogon::Get);
    // endregion Debug Commands
    METHOD_LIST_END

    // region Root and OpenAPI Methods (implementation: emulator_api.cpp)
    // Root redirect
    void rootRedirect(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

    // OpenAPI specification
    void getOpenAPISpec(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    // endregion Root and OpenAPI Methods

    // region Lifecycle Management Methods (implementation: api/lifecycle_api.cpp)
    // List all emulators
    void get(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

    // Get overall emulator status
    void status(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

    // Get available models
    void getModels(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

    // Create a new emulator
    void createEmulator(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

    // Get emulator details
    void getEmulator(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id) const;

    // Remove an emulator
    void removeEmulator(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    // Control emulator state
    void startEmulator(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

    // Start an existing emulator
    void startExistingEmulator(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                               const std::string& id) const;

    void stopEmulator(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;

    void pauseEmulator(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    void resumeEmulator(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    void resetEmulator(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    // endregion Lifecycle Management Methods

    // region Tape/Disk/Snapshot Control Methods (implementation: api/tape_disk_api.cpp and api/snapshot_api.cpp)
    // Tape control
    void loadTape(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id) const;
    void ejectTape(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id) const;
    void playTape(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id) const;
    void stopTape(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id) const;
    void rewindTape(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id) const;
    void getTapeInfo(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id) const;

    void insertDisk(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id, const std::string& drive) const;
    void createDisk(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id, const std::string& drive) const;
    void ejectDisk(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id, const std::string& drive) const;
    void getDiskInfo(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id, const std::string& drive) const;
    
    // Disk inspection - drive listing
    void getDiskDrives(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       const std::string& id) const;
    void getDiskDrive(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id, const std::string& drive) const;
    
    // Disk inspection - sector data
    void getDiskSector(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       const std::string& id, const std::string& drive,
                       const std::string& cyl, const std::string& side, const std::string& sec) const;
    void getDiskSectorRaw(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                          const std::string& id, const std::string& drive,
                          const std::string& cyl, const std::string& side, const std::string& sec) const;
    
    // Disk inspection - track data
    void getDiskTrack(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id, const std::string& drive,
                      const std::string& cyl, const std::string& side) const;
    void getDiskTrackRaw(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         const std::string& id, const std::string& drive,
                         const std::string& cyl, const std::string& side) const;
    
    // Disk inspection - whole image and system info
    void getDiskImage(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id, const std::string& drive) const;
    void getDiskSysinfo(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const std::string& id, const std::string& drive) const;
    void getDiskCatalog(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const std::string& id, const std::string& drive) const;

    // Snapshot control
    void loadSnapshot(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    void saveSnapshot(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    void getSnapshotInfo(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    // endregion Tape/Disk/Snapshot Control Methods

    // region Settings Management Methods (implementation: api/settings_api.cpp)
    // Settings management
    void getSettings(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id) const;

    void getSetting(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id, const std::string& name) const;

    void setSetting(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id, const std::string& name) const;
    // endregion Settings Management Methods

    // region Feature Management Methods (implementation: api/features_api.cpp)
    // Feature management
    void getFeatures(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id) const;

    void getFeature(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id, const std::string& name) const;

    void setFeature(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id, const std::string& name) const;
    // endregion Feature Management Methods

    // region Memory State Methods (implementation: api/state_memory_api.cpp)
    // State inspection
    void getStateMemory(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    void getStateMemoryRAM(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    void getStateMemoryROM(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    void getStateScreen(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    void getStateScreenMode(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            const std::string& id) const;
    // endregion Memory State Methods

    // region Screen State Methods (implementation: api/state_screen_api.cpp)
    void getStateScreenFlash(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             const std::string& id) const;
    // endregion Screen State Methods

    // region Audio State Methods (implementation: api/state_audio_api.cpp)
    // Audio state inspection
    void getStateAudioAY(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    void getStateAudioAYIndex(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                              const std::string& chip) const;

    void getStateAudioAYRegister(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                                 const std::string& chip, const std::string& reg) const;

    void getStateAudioBeeper(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             const std::string& id) const;

    void getStateAudioGS(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    void getStateAudioCovox(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            const std::string& id) const;

    void getStateAudioChannels(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                               const std::string& id) const;

    // Audio state inspection (active emulator - no ID required)
    void getStateAudioAYActive(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

    void getStateAudioAYIndexActive(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                    const std::string& chip) const;

    void getStateAudioAYRegisterActive(const drogon::HttpRequestPtr& req,
                                       std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                       const std::string& chip, const std::string& reg) const;

    void getStateAudioBeeperActive(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

    void getStateAudioGSActive(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

    void getStateAudioCovoxActive(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

    void getStateAudioChannelsActive(const drogon::HttpRequestPtr& req,
                                     std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    // endregion Audio State Methods

    // region Batch Command Execution Methods (implementation: api/batch_api.cpp)
    /// @brief Execute batch commands in parallel
    /// POST /api/v1/batch/execute
    void executeBatch(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// @brief Get list of batchable commands
    /// GET /api/v1/batch/commands
    void getBatchableCommands(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback);
    // endregion Batch Command Execution Methods

    // region Debug Commands Methods (implementation: api/debug_api.cpp)
    // Stepping
    void step(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
              const std::string& id) const;
    void steps(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) const;
    void stepOver(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id) const;
    
    // Debug mode
    void getDebugMode(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    void setDebugMode(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    
    // Breakpoints
    void getBreakpoints(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const std::string& id) const;
    void addBreakpoint(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       const std::string& id) const;
    void clearBreakpoints(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                          const std::string& id) const;
    void removeBreakpoint(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                          const std::string& id, const std::string& bpIdStr) const;
    void enableBreakpoint(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                          const std::string& id, const std::string& bpIdStr) const;
    void disableBreakpoint(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                           const std::string& id, const std::string& bpIdStr) const;
    void getBreakpointStatus(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             const std::string& id) const;
    
    // Memory inspection
    void getRegisters(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    void getMemory(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id, const std::string& addrStr) const;
    
    // Analysis
    void getMemCounters(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const std::string& id) const;
    void getCallTrace(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    
    // Disassembly
    void getDisasm(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id) const;
    void getDisasmPage(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       const std::string& id) const;
    // endregion Debug Commands Methods

    // region Helper Methods (implementation: emulator_api.cpp)
private:
    // Get emulator by ID (UUID) or index (numeric)
    // @param idOrIndex Either a UUID string or numeric index (0-based)
    // @return Shared pointer to emulator, or nullptr if not found
    std::shared_ptr<Emulator> getEmulatorByIdOrIndex(const std::string& idOrIndex) const;

    // Get emulator using global selection priority, then stateless fallback
    // First checks globally selected emulator, then falls back to stateless behavior
    // @return Shared pointer to emulator, or nullptr if no emulator can be selected
    std::shared_ptr<Emulator> getEmulatorWithGlobalSelection() const;

    // Get emulator using stateless auto-selection
    // Auto-selects only if exactly one emulator exists (stateless behavior)
    // @return Shared pointer to emulator, or nullptr if 0 or 2+ emulators exist
    std::shared_ptr<Emulator> getEmulatorStateless() const;

    // Helper method to handle emulator actions with common error handling
    void handleEmulatorAction(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                              std::function<std::string(std::shared_ptr<Emulator>)> action) const;
    // endregion Helper Methods
};
}  // namespace v1
}  // namespace api