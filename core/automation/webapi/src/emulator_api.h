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
    // The operation log (machine state coordinator, api/operations_api.cpp): who did what to the machine, when, result
    ADD_METHOD_TO(EmulatorAPI::getOperations, "/api/v1/emulator/{id}/operations", drogon::Get);
    // NMI pulse (Scorpion MNI "magic button" with {"magic": true})
    ADD_METHOD_TO(EmulatorAPI::requestNmi, "/api/v1/emulator/{id}/nmi", drogon::Post);
    // Front-panel switches (Profi TURBO): GET lists them, POST {"name": "turbo", "on": true} flips one
    ADD_METHOD_TO(EmulatorAPI::getSwitches, "/api/v1/emulator/{id}/switches", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::setSwitch, "/api/v1/emulator/{id}/switches", drogon::Post);

    // Switch machine model (stops current emulator, creates new one with different model)
    ADD_METHOD_TO(EmulatorAPI::switchModel, "/api/v1/emulator/{id}/model", drogon::Post);

    // ZX-Poly group status (four synchronized modules; any member id)
    ADD_METHOD_TO(EmulatorAPI::getZXPolyStatus, "/api/v1/emulator/{id}/zxpoly", drogon::Get);
    // endregion Lifecycle Management

    // region Tape/Disk/Snapshot Control (implementation: api/tape_disk_api.cpp and api/snapshot_api.cpp)
    // Tape control
    ADD_METHOD_TO(EmulatorAPI::loadTape, "/api/v1/emulator/{id}/tape/load", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::ejectTape, "/api/v1/emulator/{id}/tape/eject", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::playTape, "/api/v1/emulator/{id}/tape/play", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::pauseTape, "/api/v1/emulator/{id}/tape/pause", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::stopTape, "/api/v1/emulator/{id}/tape/stop", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::rewindTape, "/api/v1/emulator/{id}/tape/rewind", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::seekTape, "/api/v1/emulator/{id}/tape/seek", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getTape, "/api/v1/emulator/{id}/tape", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getTapeInfo, "/api/v1/emulator/{id}/tape/info", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getTapeBlock, "/api/v1/emulator/{id}/tape/blocks/{index}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::renderTapeAudio, "/api/v1/emulator/{id}/tape/render", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::importTapeAudio, "/api/v1/emulator/{id}/tape/import", drogon::Post);

    // Disk control
    // Media: every slot of the machine (floppy, SD, later tape / IDE / CD) through MediaControl
    // (implementation: api/media_api.cpp; media-control-design.md §3.9)
    ADD_METHOD_TO(EmulatorAPI::getMediaList, "/api/v1/emulator/{id}/media", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getMediaSlot, "/api/v1/emulator/{id}/media/{slot}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postMediaVerb, "/api/v1/emulator/{id}/media/{slot}/{verb}", drogon::Post);

    // Controllers: the machine's connectors and their retro devices through ControllerControl
    // (implementation: api/controllers_api.cpp; controller-manager surfaces.md §6)
    ADD_METHOD_TO(EmulatorAPI::getControllers, "/api/v1/emulator/{id}/controllers", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getController, "/api/v1/emulator/{id}/controllers/{connector}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postControllerVerb, "/api/v1/emulator/{id}/controllers/{connector}/{verb}",
                  drogon::Post);
    // The host side, for the process (HostInputControl): devices, context, profiles, profile, bindings, haptics (GET);
    // save, delete, apply, bind, unbind, scheme, rumble, output, light (POST)
    ADD_METHOD_TO(EmulatorAPI::getHostControllers, "/api/v1/controllers/{verb}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postHostControllers, "/api/v1/controllers/{verb}", drogon::Post);
    // Host actions (core ActionRegistry; implementation: api/actions_api.cpp; control surfaces actions.md §6). GET
    // /api/v1/controllers/actions is the {verb} route above ("actions"), handled there before the host input verbs
    ADD_METHOD_TO(EmulatorAPI::postAction, "/api/v1/controllers/actions/{action}", drogon::Post);
    // The instance's surface context (actions::ContextState, control surfaces architecture §6; set by the ctx.set
    // action). /api/v1/controllers/context is the host controllers' own verb (the focused instance of the router)
    ADD_METHOD_TO(EmulatorAPI::getContext, "/api/v1/surfaces/context", drogon::Get);
    // Targets of the control surfaces (actions::TargetState, Stream Deck targeting.md), the readouts and the
    // activity feed (implementation: api/surfaces_api.cpp over actions::surfaces::Call, the verbs of every plane)
    ADD_METHOD_TO(EmulatorAPI::getSurfaceViews, "/api/v1/surfaces/views", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postSurfaceView, "/api/v1/surfaces/views", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::putSurfaceView, "/api/v1/surfaces/views/{view}", drogon::Put);
    ADD_METHOD_TO(EmulatorAPI::deleteSurfaceView, "/api/v1/surfaces/views/{view}", drogon::Delete);
    ADD_METHOD_TO(EmulatorAPI::getSurfaceTarget, "/api/v1/surfaces/target", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getSurfaceSessions, "/api/v1/surfaces/sessions", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postSurfaceSession, "/api/v1/surfaces/sessions", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getSurfaceSession, "/api/v1/surfaces/sessions/{session}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::deleteSurfaceSession, "/api/v1/surfaces/sessions/{session}", drogon::Delete);
    ADD_METHOD_TO(EmulatorAPI::putSurfacePin, "/api/v1/surfaces/sessions/{session}/pin", drogon::Put);
    ADD_METHOD_TO(EmulatorAPI::getSurfaceReadouts, "/api/v1/surfaces/readouts", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getSurfaceActivity, "/api/v1/surfaces/activity", drogon::Get);
    // The Stream Deck daemon's supervision by unreal-qt, its settings, pairing and deck brightness (implementation:
    // api/streamdeck_api.cpp over companion::streamdeck::Call, the verbs of every plane; Stream Deck daemon.md §7)
    ADD_METHOD_TO(EmulatorAPI::getStreamDeck, "/api/v1/streamdeck", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStreamDeckVerb, "/api/v1/streamdeck/{verb}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postStreamDeckVerb, "/api/v1/streamdeck/{verb}", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::putStreamDeckVerb, "/api/v1/streamdeck/{verb}", drogon::Put);
    ADD_METHOD_TO(EmulatorAPI::deleteStreamDeckVerb, "/api/v1/streamdeck/{verb}", drogon::Delete);
    ADD_METHOD_TO(EmulatorAPI::getStreamDeckBrightness, "/api/v1/streamdeck/decks/{deck}/brightness", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::putStreamDeckBrightness, "/api/v1/streamdeck/decks/{deck}/brightness", drogon::Put);
    ADD_METHOD_TO(EmulatorAPI::getStreamDeckPack, "/api/v1/streamdeck/decks/{deck}/pack", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::putStreamDeckPack, "/api/v1/streamdeck/decks/{deck}/pack", drogon::Put);
    // Monitoring: the host process, the clients, each instance's pace and summaries (implementation:
    // api/monitoring_api.cpp over monitoring::Call, the verbs of every plane; the WebSocket topics of the same names)
    ADD_METHOD_TO(EmulatorAPI::getMonitor, "/api/v1/monitor", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getMonitorName, "/api/v1/monitor/{name}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getBreakpointGroups, "/api/v1/emulator/{id}/breakpoints/groups", drogon::Get);

    // ZX-bus slots: the machine's buses, slots and cards (implementation: api/slots_api.cpp over SlotControl,
    // core/src/emulator/slots/slotcontrol.h; ZX-bus slots architecture.md §9). A change restarts the machine
    ADD_METHOD_TO(EmulatorAPI::getSlots, "/api/v1/emulator/{id}/slots", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getSlotsCatalog, "/api/v1/emulator/{id}/slots/catalog", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getSlotsMatrix, "/api/v1/emulator/{id}/slots/matrix", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postSlotVerb, "/api/v1/emulator/{id}/slots/{slot}/{verb}", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::putSlotOptions, "/api/v1/emulator/{id}/slots/{slot}/options", drogon::Put);

    ADD_METHOD_TO(EmulatorAPI::insertDisk, "/api/v1/emulator/{id}/disk/{drive}/insert", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::createDisk, "/api/v1/emulator/{id}/disk/{drive}/create", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::ejectDisk, "/api/v1/emulator/{id}/disk/{drive}/eject", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getDiskInfo, "/api/v1/emulator/{id}/disk/{drive}/info", drogon::Get);

    // Disk inspection - drive listing
    ADD_METHOD_TO(EmulatorAPI::getDiskDrives, "/api/v1/emulator/{id}/disk", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getDiskDrive, "/api/v1/emulator/{id}/disk/{drive}", drogon::Get);

    // Disk inspection - sector data
    ADD_METHOD_TO(EmulatorAPI::getDiskSector, "/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}",
                  drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getDiskSectorRaw, "/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}/raw",
                  drogon::Get);
    // A debugger's sector write into the data field (SectorWrite)
    ADD_METHOD_TO(EmulatorAPI::putDiskSector, "/api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}",
                  drogon::Put);

    // Disk inspection - track data
    ADD_METHOD_TO(EmulatorAPI::getDiskTrack, "/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getDiskTrackRaw, "/api/v1/emulator/{id}/disk/{drive}/track/{cyl}/{side}/raw",
                  drogon::Get);

    // Disk inspection - whole image and system info
    ADD_METHOD_TO(EmulatorAPI::getDiskImage, "/api/v1/emulator/{id}/disk/{drive}/image", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getDiskSysinfo, "/api/v1/emulator/{id}/disk/{drive}/sysinfo", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getDiskCatalog, "/api/v1/emulator/{id}/disk/{drive}/catalog", drogon::Get);

    // Snapshot control
    ADD_METHOD_TO(EmulatorAPI::loadSnapshot, "/api/v1/emulator/{id}/snapshot/load", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::saveSnapshot, "/api/v1/emulator/{id}/snapshot/save", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::inspectSnapshot, "/api/v1/emulator/{id}/snapshot/inspect", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getSnapshotFormats, "/api/v1/emulator/{id}/snapshot/formats", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getSnapshotInfo, "/api/v1/emulator/{id}/snapshot/info", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::transferState, "/api/v1/emulator/{id}/snapshot/transfer", drogon::Post);

    // RZX input recordings (implementation: api/rzx_api.cpp)
    ADD_METHOD_TO(EmulatorAPI::playRzx, "/api/v1/emulator/{id}/rzx/play", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::stopRzx, "/api/v1/emulator/{id}/rzx/stop", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::seekRzx, "/api/v1/emulator/{id}/rzx/seek", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getRzxStatus, "/api/v1/emulator/{id}/rzx/status", drogon::Get);
    // endregion Tape/Disk/Snapshot Control

    // region Capture Commands (implementation: api/capture_api.cpp)
    // Screen OCR
    ADD_METHOD_TO(EmulatorAPI::captureOcr, "/api/v1/emulator/{id}/capture/ocr", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::captureScreen, "/api/v1/emulator/{id}/capture/screen", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::captureNextLayers, "/api/v1/emulator/{id}/capture/next-layers", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::capturePlaneB, "/api/v1/emulator/{id}/capture/planeb", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::captureFramebuffer, "/api/v1/emulator/{id}/capture/framebuffer", drogon::Get);
    // endregion Capture Commands

    // region BASIC Control (implementation: api/basic_api.cpp)
    // BASIC command execution and program management
    ADD_METHOD_TO(EmulatorAPI::basicRun, "/api/v1/emulator/{id}/basic/run", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::basicInject, "/api/v1/emulator/{id}/basic/inject", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::basicExtract, "/api/v1/emulator/{id}/basic/extract", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::basicClear, "/api/v1/emulator/{id}/basic/clear", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::basicState, "/api/v1/emulator/{id}/basic/state", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::basicMode, "/api/v1/emulator/{id}/basic/mode", drogon::Post);
    // endregion BASIC Control

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

    // region Analyzer Management (implementation: api/analyzers_api.cpp)
    // Analyzer control
    ADD_METHOD_TO(EmulatorAPI::getAnalyzers, "/api/v1/emulator/{id}/analyzers", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getAnalyzer, "/api/v1/emulator/{id}/analyzer/{name}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::setAnalyzer, "/api/v1/emulator/{id}/analyzer/{name}", drogon::Put, drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getAnalyzerEvents, "/api/v1/emulator/{id}/analyzer/{name}/events", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::clearAnalyzerEvents, "/api/v1/emulator/{id}/analyzer/{name}/events", drogon::Delete);

    // Session control
    ADD_METHOD_TO(EmulatorAPI::analyzerSession, "/api/v1/emulator/{id}/analyzer/{name}/session", drogon::Post);

    // Raw data access
    ADD_METHOD_TO(EmulatorAPI::getAnalyzerRawFDC, "/api/v1/emulator/{id}/analyzer/{name}/raw/fdc", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getAnalyzerRawBreakpoints, "/api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints",
                  drogon::Get);

    // Coverage analyzer control (executed-address coverage)
    ADD_METHOD_TO(EmulatorAPI::startCoverage, "/api/v1/emulator/{id}/coverage/start", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::stopCoverage, "/api/v1/emulator/{id}/coverage/stop", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::clearCoverage, "/api/v1/emulator/{id}/coverage/clear", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getCoverage, "/api/v1/emulator/{id}/coverage", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getCoverageGaps, "/api/v1/emulator/{id}/coverage/gaps", drogon::Get);

    // AY register-write log (MCP automation)
    ADD_METHOD_TO(EmulatorAPI::ayLog, "/api/v1/emulator/{id}/ay/log", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getAYLog, "/api/v1/emulator/{id}/ay/log", drogon::Get);

    // Audio capture (MCP automation)
    ADD_METHOD_TO(EmulatorAPI::audioCapture, "/api/v1/emulator/{id}/audio/capture", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::audioCaptureStatus, "/api/v1/emulator/{id}/audio/capture/status", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::audioCaptureResult, "/api/v1/emulator/{id}/audio/capture/result", drogon::Get);
    // Per-device mixer (implementation: api/state_audio_api.cpp; core AudioMixer, DeviceState::AudioMixer)
    ADD_METHOD_TO(EmulatorAPI::getAudioMixer, "/api/v1/emulator/{id}/audio/mixer", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::setAudioMixer, "/api/v1/emulator/{id}/audio/mixer/{source}", drogon::Put, drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getAudioMixerSource, "/api/v1/emulator/{id}/audio/mixer/{source}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getAudioLevels, "/api/v1/emulator/{id}/audio/levels", drogon::Get);
    // endregion Analyzer Management

    // region Video Recording (implementation: api/recording_api.cpp)
    // Video recording control (MCP automation)
    ADD_METHOD_TO(EmulatorAPI::videoRecord, "/api/v1/emulator/{id}/video/record", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::videoRecordStatus, "/api/v1/emulator/{id}/video/record/status", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::recordingPresets, "/api/v1/recording/presets", drogon::Get);
    // endregion Video Recording

    // region Recent files (implementation: api/recent_api.cpp)
    ADD_METHOD_TO(EmulatorAPI::getRecentFiles, "/api/v1/recent", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::clearRecentFiles, "/api/v1/recent", drogon::Delete);
    // endregion Recent files

    // region Memory State (implementation: api/state_memory_api.cpp)
    // State inspection
    ADD_METHOD_TO(EmulatorAPI::getStateMemory, "/api/v1/emulator/{id}/state/memory", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateMemoryRAM, "/api/v1/emulator/{id}/state/memory/ram", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateMemoryROM, "/api/v1/emulator/{id}/state/memory/rom", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStatePaging, "/api/v1/emulator/{id}/state/paging", drogon::Get);

    // Memory read/write operations
    ADD_METHOD_TO(EmulatorAPI::readMemory, "/api/v1/emulator/{id}/memory/read/{address}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::writeMemory, "/api/v1/emulator/{id}/memory/write", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::findMemory, "/api/v1/emulator/{id}/memory/find", drogon::Post);

    // Page-level memory access
    ADD_METHOD_TO(EmulatorAPI::readPage, "/api/v1/emulator/{id}/memory/page/{type}/{page}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::writePage, "/api/v1/emulator/{id}/memory/page/{type}/{page}", drogon::Post);

    // Device memory regions (implementation: api/memory_region_api.cpp; core DeviceMemory): the Sprinter's video RAM
    ADD_METHOD_TO(EmulatorAPI::getMemoryRegions, "/api/v1/emulator/{id}/memory/regions", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getMemoryRegion, "/api/v1/emulator/{id}/memory/region/{name}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postMemoryRegion, "/api/v1/emulator/{id}/memory/region/{name}", drogon::Post);

    // ROM protection control
    ADD_METHOD_TO(EmulatorAPI::getROMProtect, "/api/v1/emulator/{id}/memory/rom/protect", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::setROMProtect, "/api/v1/emulator/{id}/memory/rom/protect", drogon::Put, drogon::Post);
    // ZX-Evo flash ROM: the saved flash (status, save now, discard)
    ADD_METHOD_TO(EmulatorAPI::getROMFlash, "/api/v1/emulator/{id}/memory/rom/flash", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postROMFlash, "/api/v1/emulator/{id}/memory/rom/flash", drogon::Post);
    // endregion Memory State

    // region Screen State (implementation: api/state_screen_api.cpp)
    ADD_METHOD_TO(EmulatorAPI::getStateScreen, "/api/v1/emulator/{id}/state/screen", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateScreenMode, "/api/v1/emulator/{id}/state/screen/mode", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::putStateScreenOverscan, "/api/v1/emulator/{id}/state/screen/overscan", drogon::Put, drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getStateScreenFlash, "/api/v1/emulator/{id}/state/screen/flash", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateScreenAttributes, "/api/v1/emulator/{id}/state/screen/attributes", drogon::Get);

    // Deterministic screen-content digest (change detection)
    ADD_METHOD_TO(EmulatorAPI::getStateScreenDigest, "/api/v1/emulator/{id}/state/screen/digest", drogon::Get);

    // Static port-map introspection: devices x ports x gates + live routing flags
    ADD_METHOD_TO(EmulatorAPI::getPortsMap, "/api/v1/emulator/{id}/ports", drogon::Get);
    // A debugger's port write through the decoder (PortWrite)
    ADD_METHOD_TO(EmulatorAPI::postPortOut, "/api/v1/emulator/{id}/ports/out", drogon::Post);


    // Beam (raster) position + frame timing from the machine model
    ADD_METHOD_TO(EmulatorAPI::getBeamPosition, "/api/v1/emulator/{id}/video/beam", drogon::Get);
    // Video debug translation (PLAN #42, api/video_map_api.cpp): layout, pixel sources, byte -> pixels, text
    ADD_METHOD_TO(EmulatorAPI::getVideoLayout, "/api/v1/emulator/{id}/video/layout", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getVideoPixel, "/api/v1/emulator/{id}/video/pixel", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getVideoAddress, "/api/v1/emulator/{id}/video/address", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getVideoText, "/api/v1/emulator/{id}/video/text", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getVideoChanges, "/api/v1/emulator/{id}/video/changes", drogon::Get);
    // Temporal effects (ZX DLSS de-flicker, api/video_temporal_api.cpp): status, switch algorithm / off
    ADD_METHOD_TO(EmulatorAPI::getVideoTemporal, "/api/v1/emulator/{id}/video/temporal", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::setVideoTemporal, "/api/v1/emulator/{id}/video/temporal", drogon::Put, drogon::Post);
    // GUI window display settings (api/video_window_api.cpp): GPU acceleration, CRT effects, temporal blending
    ADD_METHOD_TO(EmulatorAPI::getVideoWindow, "/api/v1/emulator/{id}/video/window", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::setVideoWindow, "/api/v1/emulator/{id}/video/window", drogon::Put, drogon::Post);
    // The active CRT profile's parameters (api/video_window_api.cpp): list, set / nudge, reset
    ADD_METHOD_TO(EmulatorAPI::getVideoWindowCrtParams, "/api/v1/emulator/{id}/video/window/crt_params", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::setVideoWindowCrtParams, "/api/v1/emulator/{id}/video/window/crt_params", drogon::Put,
                  drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::resetVideoWindowCrtParams, "/api/v1/emulator/{id}/video/window/crt_params/reset",
                  drogon::Post);
    // The overscan frame's viewport (api/video_window_api.cpp): full, symmetric, standard, screen_only
    ADD_METHOD_TO(EmulatorAPI::getVideoViewport, "/api/v1/emulator/{id}/video/viewport", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::setVideoViewport, "/api/v1/emulator/{id}/video/viewport", drogon::Put, drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::startVideoWindowRecord, "/api/v1/emulator/{id}/video/window/record", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getVideoWindowRecord, "/api/v1/emulator/{id}/video/window/record", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::stopVideoWindowRecord, "/api/v1/emulator/{id}/video/window/record/stop", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::runVideoWindowRecordFrames, "/api/v1/emulator/{id}/video/window/record/frames", drogon::Post);
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
    // GS control actions (GS design §11.2)
    ADD_METHOD_TO(EmulatorAPI::postControlAudioGS, "/api/v1/emulator/{id}/control/audio/gs", drogon::Post);
    // GS triage: activity counters + opt-in port/DAC event trace
    ADD_METHOD_TO(EmulatorAPI::getStateAudioGSPortTrace, "/api/v1/emulator/{id}/state/audio/gs/porttrace", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postControlAudioGSPortTrace, "/api/v1/emulator/{id}/control/audio/gs/porttrace",
                  drogon::Post);

    // Audio state inspection (active emulator - no ID required)
    ADD_METHOD_TO(EmulatorAPI::getStateAudioAYActive, "/api/v1/emulator/state/audio/ay", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioAYIndexActive, "/api/v1/emulator/state/audio/ay/{chip}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioAYRegisterActive, "/api/v1/emulator/state/audio/ay/{chip}/register/{reg}",
                  drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioBeeperActive, "/api/v1/emulator/state/audio/beeper", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioGSActive, "/api/v1/emulator/state/audio/gs", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioCovoxActive, "/api/v1/emulator/state/audio/covox", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioChannelsActive, "/api/v1/emulator/state/audio/channels", drogon::Get);
    // TurboSound FM and Beta Disk state (implementation: api/state_device_api.cpp, core DeviceState reports)
    ADD_METHOD_TO(EmulatorAPI::getStateAudioFM, "/api/v1/emulator/{id}/state/audio/fm", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioFMIndex, "/api/v1/emulator/{id}/state/audio/fm/{chip}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateFdc, "/api/v1/emulator/{id}/state/fdc", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioFMActive, "/api/v1/emulator/state/audio/fm", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioFMIndexActive, "/api/v1/emulator/state/audio/fm/{chip}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioMoonSound, "/api/v1/emulator/{id}/state/audio/moonsound", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioMoonSoundPart, "/api/v1/emulator/{id}/state/audio/moonsound/{part}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioMoonSoundActive, "/api/v1/emulator/state/audio/moonsound", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioMoonSoundPartActive, "/api/v1/emulator/state/audio/moonsound/{part}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateFdcActive, "/api/v1/emulator/state/fdc", drogon::Get);
    // ZX-MultiSound and its MIDI synthesizer (api/state_device_api.cpp, DeviceState::MultiSound / Midi, MidiControl)
    ADD_METHOD_TO(EmulatorAPI::getStateAudioMultiSound, "/api/v1/emulator/{id}/state/audio/multisound", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateAudioMidi, "/api/v1/emulator/{id}/state/audio/midi", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postControlAudioMidi, "/api/v1/emulator/{id}/control/audio/midi", drogon::Post);
    // IDE board (implementation: api/state_device_api.cpp, core DeviceState::Ide)
    ADD_METHOD_TO(EmulatorAPI::getStateIde, "/api/v1/emulator/{id}/state/ide", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateIdeActive, "/api/v1/emulator/state/ide", drogon::Get);
    // CD audio of the ATAPI CD drives (implementation: api/cdaudio_api.cpp; CdAudioControl)
    ADD_METHOD_TO(EmulatorAPI::getStateCdAudio, "/api/v1/emulator/{id}/state/cdaudio", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateCdAudioActive, "/api/v1/emulator/state/cdaudio", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postCdAudioVerb, "/api/v1/emulator/{id}/cdaudio/{verb}", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::postCdAudioVerbActive, "/api/v1/emulator/cdaudio/{verb}", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getStateTsConf, "/api/v1/emulator/{id}/state/tsconf", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateTsConfActive, "/api/v1/emulator/state/tsconf", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateTsConfTsu, "/api/v1/emulator/{id}/state/tsconf/tsu", drogon::Get);
    // Sprinter Sp2000 (implementation: api/state_device_api.cpp, core DeviceState::Sprinter / SprinterPortTable /
    // SprinterPortLookup in ports/models/sprinter/sprinterdevicestate.cpp)
    ADD_METHOD_TO(EmulatorAPI::getStateSprinter, "/api/v1/emulator/{id}/state/sprinter", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateSprinterActive, "/api/v1/emulator/state/sprinter", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateNext, "/api/v1/emulator/{id}/state/next", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateNextActive, "/api/v1/emulator/state/next", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateNextRegs, "/api/v1/emulator/{id}/state/next/regs", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateNextMmu, "/api/v1/emulator/{id}/state/next/mmu", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateNextRegJournal, "/api/v1/emulator/{id}/state/next/reg-journal", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postNextRegJournal, "/api/v1/emulator/{id}/next/reg-journal", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getStateNextSd, "/api/v1/emulator/{id}/state/next/sd", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postNextSdJournal, "/api/v1/emulator/{id}/next/sd-journal", drogon::Post);
    // Bus analyzer (implementation: api/bus_api.cpp; docs/inprogress/2026-10-11-bus-analyzer)
    ADD_METHOD_TO(EmulatorAPI::getBuses, "/api/v1/emulator/{id}/bus", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getBusEvents, "/api/v1/emulator/{id}/bus/{bus}/events", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getBusFindings, "/api/v1/emulator/{id}/bus/{bus}/findings", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postBusTraceJournal, "/api/v1/emulator/{id}/bus/{bus}/trace-journal", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::busSdTiming, "/api/v1/emulator/{id}/bus/{bus}/sd-timing", drogon::Get, drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getBusExport, "/api/v1/emulator/{id}/bus/{bus}/export", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateNextDivMmc, "/api/v1/emulator/{id}/state/next/divmmc", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postNextDivMmcJournal, "/api/v1/emulator/{id}/next/divmmc-journal", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getStateNextNmi, "/api/v1/emulator/{id}/state/next/nmi", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateNextAudio, "/api/v1/emulator/{id}/state/next/audio", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateNextInput, "/api/v1/emulator/{id}/state/next/input", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateNextUart, "/api/v1/emulator/{id}/state/next/uart", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postNextNmiJournal, "/api/v1/emulator/{id}/next/nmi-journal", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getStateNextMagicPort, "/api/v1/emulator/{id}/state/next/magic-port", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postNextMagicPort, "/api/v1/emulator/{id}/next/magic-port", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getStateNextPixel, "/api/v1/emulator/{id}/state/next/pixel", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postNextLayerCapture, "/api/v1/emulator/{id}/next/layer-capture", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getStateNextDma, "/api/v1/emulator/{id}/state/next/dma", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateNextVideo, "/api/v1/emulator/{id}/state/next/video", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateNextPalette, "/api/v1/emulator/{id}/state/next/palette", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateNextPorts, "/api/v1/emulator/{id}/state/next/ports", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateNextCopper, "/api/v1/emulator/{id}/state/next/copper", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateNextSprites, "/api/v1/emulator/{id}/state/next/sprites", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateNextNextReg, "/api/v1/emulator/{id}/state/next/nextreg", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postNextNextReg, "/api/v1/emulator/{id}/next/nextreg", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getStateSprinterPorts, "/api/v1/emulator/{id}/state/sprinter/ports", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateSprinterPortLookup, "/api/v1/emulator/{id}/state/sprinter/ports/lookup", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateSprinterText, "/api/v1/emulator/{id}/state/sprinter/text", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateSprinterVideo, "/api/v1/emulator/{id}/state/sprinter/video", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateSprinterPalette, "/api/v1/emulator/{id}/state/sprinter/palette", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateSprinterSoundRing, "/api/v1/emulator/{id}/state/sprinter/sound/ring", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateSprinterBios, "/api/v1/emulator/{id}/state/sprinter/bios", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateSprinterZxMode, "/api/v1/emulator/{id}/state/sprinter/zx-mode", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateSprinterPldJournal, "/api/v1/emulator/{id}/state/sprinter/pld-journal", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postSprinterPldJournal, "/api/v1/emulator/{id}/sprinter/pld-journal", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::postSprinterBios, "/api/v1/emulator/{id}/sprinter/bios", drogon::Post);
    // CMOS clock (implementation: api/state_device_api.cpp, core DeviceState::Rtc + RtcAccess)
    ADD_METHOD_TO(EmulatorAPI::getStateRtc, "/api/v1/emulator/{id}/state/rtc", drogon::Get);
    // ZX Profi board chips: 8255, 8253, 8251 and the port map (implementation: api/state_device_api.cpp, core DeviceState::ProfiPeripherals)
    ADD_METHOD_TO(EmulatorAPI::getStateProfi, "/api/v1/emulator/{id}/state/profi", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateProfiActive, "/api/v1/emulator/state/profi", drogon::Get);
    // Network adapters (implementation: api/state_device_api.cpp, core DeviceState::Network)
    ADD_METHOD_TO(EmulatorAPI::getStateNetwork, "/api/v1/emulator/{id}/state/network", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateNetworkActive, "/api/v1/emulator/state/network", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postNetworkConfig, "/api/v1/emulator/{id}/network/config", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getStateRtcActive, "/api/v1/emulator/state/rtc", drogon::Get);
    // Automation sessions (execution arbiter design §6a): take / read / end run control; X-Unreal-Session header
    ADD_METHOD_TO(EmulatorAPI::postControlSession, "/api/v1/emulator/{id}/control/session", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getControlSession, "/api/v1/emulator/{id}/control/session", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::deleteControlSession, "/api/v1/emulator/{id}/control/session", drogon::Delete);
    // Companion clients (docs/inprogress/2026-10-16-companion-client): pairing, the companion itself, take-back
    ADD_METHOD_TO(EmulatorAPI::postControlTakeBack, "/api/v1/emulator/{id}/control/take-back", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::postControlAllowAgain, "/api/v1/emulator/{id}/control/allow-again", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::companionsCollection, "/api/v1/companions", drogon::Get, drogon::Delete);
    ADD_METHOD_TO(EmulatorAPI::companionItem, "/api/v1/companions/{cid}", drogon::Get, drogon::Post, drogon::Delete);
    ADD_METHOD_TO(EmulatorAPI::companionPairing, "/api/v1/companions/pairings/{pid}", drogon::Get, drogon::Delete);
    // Execution arbiter: who decides whether the Z80 runs (DeviceState::Execution)
    ADD_METHOD_TO(EmulatorAPI::getStateExecution, "/api/v1/emulator/{id}/state/execution", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateExecutionActive, "/api/v1/emulator/state/execution", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getRtcCells, "/api/v1/emulator/{id}/rtc/cells", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postRtcCells, "/api/v1/emulator/{id}/rtc/cells", drogon::Post);
    // ISA slots (Sprinter; implementation: api/state_device_api.cpp, core DeviceState::Isa + IsaAccess)
    ADD_METHOD_TO(EmulatorAPI::getStateIsa, "/api/v1/emulator/{id}/state/isa", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateIsaActive, "/api/v1/emulator/state/isa", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postControlIsa, "/api/v1/emulator/{id}/control/isa", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getStateIsaJournal, "/api/v1/emulator/{id}/state/isa/journal", drogon::Get);
    // Ethernet frames of the frame-level cards (core EthernetAccess)
    ADD_METHOD_TO(EmulatorAPI::getNetworkFrames, "/api/v1/emulator/{id}/network/frames", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postNetworkFrame, "/api/v1/emulator/{id}/network/frame", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getNetworkAdapters, "/api/v1/emulator/{id}/network/adapters", drogon::Get);
    // Everything the adapters sent and received (core TrafficAccess, network #91)
    ADD_METHOD_TO(EmulatorAPI::getNetworkTraffic, "/api/v1/emulator/{id}/network/traffic", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postNetworkTraffic, "/api/v1/emulator/{id}/network/traffic", drogon::Post);
    // Memory contention (implementation: api/state_device_api.cpp, core DeviceState::Contention)
    ADD_METHOD_TO(EmulatorAPI::getStateContention, "/api/v1/emulator/{id}/state/contention", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getStateContentionActive, "/api/v1/emulator/state/contention", drogon::Get);
    // endregion Audio State

    // region Debug Commands (implementation: api/debug_api.cpp)
    // Stepping
    ADD_METHOD_TO(EmulatorAPI::step, "/api/v1/emulator/{id}/step", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::steps, "/api/v1/emulator/{id}/steps", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::stepOver, "/api/v1/emulator/{id}/stepover", drogon::Post);
        ADD_METHOD_TO(EmulatorAPI::stepOut, "/api/v1/emulator/{id}/stepout", drogon::Post);
        ADD_METHOD_TO(EmulatorAPI::skipUntil, "/api/v1/emulator/{id}/skip_until", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::runTStates, "/api/v1/emulator/{id}/run_tstates", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::runToScanline, "/api/v1/emulator/{id}/run_to_scanline", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::runNScanlines, "/api/v1/emulator/{id}/run_scanlines", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::runToPixel, "/api/v1/emulator/{id}/run_to_pixel", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::runToInterrupt, "/api/v1/emulator/{id}/run_to_interrupt", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::runFrame, "/api/v1/emulator/{id}/run_frame", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::runFrames, "/api/v1/emulator/{id}/run_frames", drogon::Post);
    // The paced run (implementation: api/runpace_api.cpp, core execution::RunPacer)
    ADD_METHOD_TO(EmulatorAPI::getRunPace, "/api/v1/emulator/{id}/run_pace", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postRunPace, "/api/v1/emulator/{id}/run_pace", drogon::Post);

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
    ADD_METHOD_TO(EmulatorAPI::resetBreakpointHits, "/api/v1/emulator/{id}/breakpoints/hits/reset", drogon::Post);
    // Breakpoints by selection (all, an id, a kind, group:<name>), groups, notes (debug tools gaps, use cases D4.5)
    ADD_METHOD_TO(EmulatorAPI::getBreakpointsActive, "/api/v1/emulator/{id}/breakpoints/active", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::setBreakpointsActive, "/api/v1/emulator/{id}/breakpoints/active", drogon::Put);
    ADD_METHOD_TO(EmulatorAPI::removeBreakpoints, "/api/v1/emulator/{id}/breakpoints/remove", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::setBreakpointGroup, "/api/v1/emulator/{id}/breakpoints/{bp_id}/group", drogon::Put);
    ADD_METHOD_TO(EmulatorAPI::setBreakpointNote, "/api/v1/emulator/{id}/breakpoints/{bp_id}/note", drogon::Put);

    // Memory inspection and manipulation
    // NOTE: Route order matters! More specific routes must come BEFORE wildcard routes
    ADD_METHOD_TO(EmulatorAPI::getRegisters, "/api/v1/emulator/{id}/registers", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::setRegister, "/api/v1/emulator/{id}/registers/{name}", drogon::Put);
    ADD_METHOD_TO(EmulatorAPI::getMemoryInfo, "/api/v1/emulator/{id}/memory/info", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getMemoryMap, "/api/v1/emulator/{id}/memory/map", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getMemoryPage, "/api/v1/emulator/{id}/memory/{type}/{page}/{offset}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::putMemoryPage, "/api/v1/emulator/{id}/memory/{type}/{page}/{offset}", drogon::Put);
    ADD_METHOD_TO(EmulatorAPI::getMemory, "/api/v1/emulator/{id}/memory/{addr}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::putMemory, "/api/v1/emulator/{id}/memory/{addr}", drogon::Put);

    // Analysis
    ADD_METHOD_TO(EmulatorAPI::getMemCounters, "/api/v1/emulator/{id}/memcounters", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getCallTrace, "/api/v1/emulator/{id}/calltrace", drogon::Get);

    // Disassembly
    ADD_METHOD_TO(EmulatorAPI::getDisasm, "/api/v1/emulator/{id}/disasm", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getDebugSnapshot, "/api/v1/emulator/{id}/debug/snapshot", drogon::Get);
    // Long-poll: answers when the snapshot's seq moves past `since` (debugger additions tdd §6)
    ADD_METHOD_TO(EmulatorAPI::getDebugWait, "/api/v1/emulator/{id}/debug/wait", drogon::Get);
    // PC history with pages (debugger additions tdd §7)
    ADD_METHOD_TO(EmulatorAPI::getPcHistory, "/api/v1/emulator/{id}/debug/pchist", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postPcHistory, "/api/v1/emulator/{id}/debug/pchist", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getDisasmPage, "/api/v1/emulator/{id}/disasm/page", drogon::Get);
    // endregion Debug Commands

    // region Profiler Commands (implementation: api/profiler_api.cpp)
    // Opcode profiler control
    ADD_METHOD_TO(EmulatorAPI::opcodeProfilerStart, "/api/v1/emulator/{id}/profiler/opcode/start", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::opcodeProfilerStop, "/api/v1/emulator/{id}/profiler/opcode/stop", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::opcodeProfilerPause, "/api/v1/emulator/{id}/profiler/opcode/pause", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::opcodeProfilerResume, "/api/v1/emulator/{id}/profiler/opcode/resume", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::opcodeProfilerClear, "/api/v1/emulator/{id}/profiler/opcode/clear", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getProfilerStatus, "/api/v1/emulator/{id}/profiler/opcode/status", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getProfilerCounters, "/api/v1/emulator/{id}/profiler/opcode/counters", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getProfilerTrace, "/api/v1/emulator/{id}/profiler/opcode/trace", drogon::Get);

    // Memory profiler control
    ADD_METHOD_TO(EmulatorAPI::memoryProfilerStart, "/api/v1/emulator/{id}/profiler/memory/start", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::memoryProfilerStop, "/api/v1/emulator/{id}/profiler/memory/stop", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::memoryProfilerPause, "/api/v1/emulator/{id}/profiler/memory/pause", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::memoryProfilerResume, "/api/v1/emulator/{id}/profiler/memory/resume", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::memoryProfilerClear, "/api/v1/emulator/{id}/profiler/memory/clear", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getMemoryProfilerStatus, "/api/v1/emulator/{id}/profiler/memory/status", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getMemoryProfilerPages, "/api/v1/emulator/{id}/profiler/memory/pages", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getMemoryProfilerCounters, "/api/v1/emulator/{id}/profiler/memory/counters", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::memoryProfilerSave, "/api/v1/emulator/{id}/profiler/memory/save", drogon::Post);

    // Call trace profiler control
    ADD_METHOD_TO(EmulatorAPI::calltraceProfilerStart, "/api/v1/emulator/{id}/profiler/calltrace/start", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::calltraceProfilerStop, "/api/v1/emulator/{id}/profiler/calltrace/stop", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::calltraceProfilerPause, "/api/v1/emulator/{id}/profiler/calltrace/pause", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::calltraceProfilerResume, "/api/v1/emulator/{id}/profiler/calltrace/resume",
                  drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::calltraceProfilerClear, "/api/v1/emulator/{id}/profiler/calltrace/clear", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getCalltraceProfilerStatus, "/api/v1/emulator/{id}/profiler/calltrace/status",
                  drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getCalltraceProfilerEntries, "/api/v1/emulator/{id}/profiler/calltrace/entries",
                  drogon::Get);

    // Port trace (PDR) control — runtime feature "porttrace" (implementation: api/porttrace_api.cpp)
    ADD_METHOD_TO(EmulatorAPI::portTraceStart, "/api/v1/emulator/{id}/profiler/porttrace/start", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::portTraceStop, "/api/v1/emulator/{id}/profiler/porttrace/stop", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::portTracePause, "/api/v1/emulator/{id}/profiler/porttrace/pause", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::portTraceResume, "/api/v1/emulator/{id}/profiler/porttrace/resume", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::portTraceClear, "/api/v1/emulator/{id}/profiler/porttrace/clear", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getPortTraceStatus, "/api/v1/emulator/{id}/profiler/porttrace/status", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getPortTraceEvents, "/api/v1/emulator/{id}/profiler/porttrace/events", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getPortTraceFilter, "/api/v1/emulator/{id}/profiler/porttrace/filter", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::setPortTraceFilter, "/api/v1/emulator/{id}/profiler/porttrace/filter", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::setPortTraceConfig, "/api/v1/emulator/{id}/profiler/porttrace/config", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::savePortTrace, "/api/v1/emulator/{id}/profiler/porttrace/save", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::readPortTraceFile, "/api/v1/emulator/{id}/profiler/porttrace/readfile",
                  drogon::Post);

    // TS-Conf VDAC2 card: FT812 bus capture to an .evr replay stream (implementation: api/vdac2_api.cpp)
    ADD_METHOD_TO(EmulatorAPI::vdac2CaptureStart, "/api/v1/emulator/{id}/vdac2/capture/start", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::vdac2CaptureStop, "/api/v1/emulator/{id}/vdac2/capture/stop", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::vdac2CaptureStatus, "/api/v1/emulator/{id}/vdac2/capture/status", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::vdac2Metrics, "/api/v1/emulator/{id}/vdac2/metrics", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::vdac2MetricsSet, "/api/v1/emulator/{id}/vdac2/metrics", drogon::Put);

    // Unified profiler control (all profilers at once)
    ADD_METHOD_TO(EmulatorAPI::unifiedProfilerStart, "/api/v1/emulator/{id}/profiler/start", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::unifiedProfilerStop, "/api/v1/emulator/{id}/profiler/stop", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::unifiedProfilerPause, "/api/v1/emulator/{id}/profiler/pause", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::unifiedProfilerResume, "/api/v1/emulator/{id}/profiler/resume", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::unifiedProfilerClear, "/api/v1/emulator/{id}/profiler/clear", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getUnifiedProfilerStatus, "/api/v1/emulator/{id}/profiler/status", drogon::Get);

    // Frame cost — work-vs-idle t-state accounting per frame
    ADD_METHOD_TO(EmulatorAPI::getFrameCost, "/api/v1/emulator/{id}/frame_cost", drogon::Get);
    // endregion Profiler Commands

    // region Keyboard Injection (implementation: api/keyboard_api.cpp)
    // Key operations
    ADD_METHOD_TO(EmulatorAPI::keyTap, "/api/v1/emulator/{id}/keyboard/tap", drogon::Post);
    
    // Videowall API
    ADD_METHOD_TO(EmulatorAPI::setVideowallSingleSyncMode, "/api/v1/videowall/singlesync", drogon::Post);

    ADD_METHOD_TO(EmulatorAPI::keyPress, "/api/v1/emulator/{id}/keyboard/press", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::keyRelease, "/api/v1/emulator/{id}/keyboard/release", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::keyCombo, "/api/v1/emulator/{id}/keyboard/combo", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::keyMacro, "/api/v1/emulator/{id}/keyboard/macro", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::keyType, "/api/v1/emulator/{id}/keyboard/type", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::keyReleaseAll, "/api/v1/emulator/{id}/keyboard/release_all", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::keyAbort, "/api/v1/emulator/{id}/keyboard/abort", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::keyStatus, "/api/v1/emulator/{id}/keyboard/status", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::keyRoute, "/api/v1/emulator/{id}/keyboard/route", drogon::Post, drogon::Put);
    ADD_METHOD_TO(EmulatorAPI::keyList, "/api/v1/emulator/{id}/keyboard/keys", drogon::Get);
    // endregion Keyboard Injection

    // region Mouse Injection (implementation: api/mouse_api.cpp)
    ADD_METHOD_TO(EmulatorAPI::mouseMove, "/api/v1/emulator/{id}/mouse/move", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::mouseGlide, "/api/v1/emulator/{id}/mouse/glide", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::mousePress, "/api/v1/emulator/{id}/mouse/press", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::mouseRelease, "/api/v1/emulator/{id}/mouse/release", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::mouseClick, "/api/v1/emulator/{id}/mouse/click", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::mouseButtons, "/api/v1/emulator/{id}/mouse/buttons", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::mouseWheel, "/api/v1/emulator/{id}/mouse/wheel", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::mouseReleaseAll, "/api/v1/emulator/{id}/mouse/release_all", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::mouseSetCounters, "/api/v1/emulator/{id}/mouse/counters", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::mouseStatus, "/api/v1/emulator/{id}/mouse/status", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::mouseButtonList, "/api/v1/emulator/{id}/mouse/buttons", drogon::Get);
    // endregion Mouse Injection

    // region Joystick Injection (implementation: api/joystick_api.cpp)
    ADD_METHOD_TO(EmulatorAPI::joystickPress, "/api/v1/emulator/{id}/joystick/press", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::joystickRelease, "/api/v1/emulator/{id}/joystick/release", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::joystickSet, "/api/v1/emulator/{id}/joystick/set", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::joystickTap, "/api/v1/emulator/{id}/joystick/tap", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::joystickStatus, "/api/v1/emulator/{id}/joystick", drogon::Get);
    // endregion Joystick Injection

    // region TTD (Time-Travel Debug) (implementation: api/ttd_api.cpp)
    // Full TTD automation surface (Phase 2 complete). Per parent TDD §10.4.
    ADD_METHOD_TO(EmulatorAPI::getTTDStatus, "/api/v1/emulator/{id}/ttd/status", drogon::Get);
    // A .ttd file's header and recorded machine without loading it (no instance)
    ADD_METHOD_TO(EmulatorAPI::getTTDFileInfo, "/api/v1/ttd/file-info", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::startTTD, "/api/v1/emulator/{id}/ttd/start", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::stopTTD, "/api/v1/emulator/{id}/ttd/stop", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::invalidateTTD, "/api/v1/emulator/{id}/ttd/invalidate", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::historyLimitTTD, "/api/v1/emulator/{id}/ttd/history-limit", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::seekTTD, "/api/v1/emulator/{id}/ttd/seek", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::exportClipTTD, "/api/v1/emulator/{id}/ttd/export-clip", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::stepBackTTD, "/api/v1/emulator/{id}/ttd/step-back", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::stepForwardTTD, "/api/v1/emulator/{id}/ttd/step-forward", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::resumeTTD, "/api/v1/emulator/{id}/ttd/resume", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::getTTDPosition, "/api/v1/emulator/{id}/ttd/position", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getTTDMarkers, "/api/v1/emulator/{id}/ttd/markers", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::dumpTTD, "/api/v1/emulator/{id}/ttd/dump", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::loadTTD, "/api/v1/emulator/{id}/ttd/load", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::findLastTTD, "/api/v1/emulator/{id}/ttd/find-last", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::memoryAtTTD, "/api/v1/emulator/{id}/ttd/memory-at", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::memoryDiffTTD, "/api/v1/emulator/{id}/ttd/memory-diff", drogon::Post);
    // D40 - the write journal on demand: switch it, build it by replay
    ADD_METHOD_TO(EmulatorAPI::journalTTD, "/api/v1/emulator/{id}/ttd/journal", drogon::Get, drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::buildJournalTTD, "/api/v1/emulator/{id}/ttd/journal/build", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::cancelJournalBuildTTD, "/api/v1/emulator/{id}/ttd/journal/build/cancel", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::portEventsTTD, "/api/v1/emulator/{id}/ttd/port-events", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::stepInstructionTTD, "/api/v1/emulator/{id}/ttd/step-instruction", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::reverseStepTTD, "/api/v1/emulator/{id}/ttd/reverse-step", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::reverseContinueTTD, "/api/v1/emulator/{id}/ttd/reverse-continue", drogon::Post);
    // TD-4 — agent bookmarks: advisory annotations beside the timeline,
    // explicitly NOT replay barriers (unlike external-event markers).
    ADD_METHOD_TO(EmulatorAPI::getTTDBookmarks, "/api/v1/emulator/{id}/ttd/bookmarks", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::postTTDBookmark, "/api/v1/emulator/{id}/ttd/bookmarks", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::deleteTTDBookmark, "/api/v1/emulator/{id}/ttd/bookmarks/{label}", drogon::Delete);
    // TD-7 — coverage index queries
    ADD_METHOD_TO(EmulatorAPI::getTTDCoverageProbe, "/api/v1/emulator/{id}/ttd/coverage/probe", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getTTDCoverageScan, "/api/v1/emulator/{id}/ttd/coverage/scan", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getTTDCoverageSummary, "/api/v1/emulator/{id}/ttd/coverage/summary", drogon::Get);
    // endregion TTD

    // region Labels/Symbols (implementation: api/debug_api.cpp)
    ADD_METHOD_TO(EmulatorAPI::getLabels, "/api/v1/emulator/{id}/labels", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::addLabel, "/api/v1/emulator/{id}/labels", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::clearLabels, "/api/v1/emulator/{id}/labels", drogon::Delete);
    // NOTE: must be registered before /labels/{name} — parameterized routes are
    // matched in registration order (drogon ctrlVector_ linear scan)
    ADD_METHOD_TO(EmulatorAPI::resolveLabel, "/api/v1/emulator/{id}/labels/resolve", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::getLabel, "/api/v1/emulator/{id}/labels/{name}", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::removeLabel, "/api/v1/emulator/{id}/labels/{name}", drogon::Delete);
    ADD_METHOD_TO(EmulatorAPI::updateLabel, "/api/v1/emulator/{id}/labels/{name}", drogon::Put);
    ADD_METHOD_TO(EmulatorAPI::loadSymbols, "/api/v1/emulator/{id}/symbols/load", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::saveSymbols, "/api/v1/emulator/{id}/symbols/save", drogon::Post);
    // Symbol files and sets through SymbolControl (implementation: api/symbols_api.cpp). A set id holds a path
    // ("file:/x/game.sym"), so it travels in the body / query, not in the URL path
    ADD_METHOD_TO(EmulatorAPI::symbolFormats, "/api/v1/symbols/formats", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::symbolFormatsOf, "/api/v1/emulator/{id}/symbols/formats", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::symbolDetect, "/api/v1/emulator/{id}/symbols/detect", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::symbolSets, "/api/v1/emulator/{id}/symbols/sets", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::symbolSetChange, "/api/v1/emulator/{id}/symbols/sets", drogon::Put);
    ADD_METHOD_TO(EmulatorAPI::symbolSetDrop, "/api/v1/emulator/{id}/symbols/sets", drogon::Delete);
    ADD_METHOD_TO(EmulatorAPI::symbolImport, "/api/v1/emulator/{id}/symbols/import", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::symbolExport, "/api/v1/emulator/{id}/symbols/export", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::symbolScan, "/api/v1/emulator/{id}/symbols/scan", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::symbolImportLive, "/api/v1/emulator/{id}/symbols/import/live", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::symbolImportSource, "/api/v1/emulator/{id}/symbols/import/source", drogon::Post);
    // Assembler sources through AsmControl (implementation: api/asm_api.cpp)
    // Media signatures through SignatureControl (implementation: api/signature_api.cpp)
    ADD_METHOD_TO(EmulatorAPI::signatureFile, "/api/v1/signature", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::signatureCompare, "/api/v1/signature/compare", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::signatureMedia, "/api/v1/emulator/{id}/signature", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::asmFormats, "/api/v1/asm/formats", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::asmDialects, "/api/v1/asm/dialects", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::asmFiles, "/api/v1/emulator/{id}/asm/files", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::asmVerb, "/api/v1/emulator/{id}/asm/{verb}", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::asmSyncStatus, "/api/v1/emulator/{id}/asm/sync", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::asmSync, "/api/v1/emulator/{id}/asm/sync/{action}", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::asmSyncHints, "/api/v1/emulator/{id}/asm/sync/hints", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::asmSyncUnwatch, "/api/v1/emulator/{id}/asm/sync/watch", drogon::Delete);
    ADD_METHOD_TO(EmulatorAPI::asmSyncPushUnwatch, "/api/v1/emulator/{id}/asm/sync/push-watch", drogon::Delete);
    ADD_METHOD_TO(EmulatorAPI::asmSnapshotSyncStatus, "/api/v1/asm/sync", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::asmSnapshotSync, "/api/v1/asm/sync/{action}", drogon::Post);
    // endregion Labels/Symbols

    // region Source Listing (implementation: api/debug_api.cpp)
    ADD_METHOD_TO(EmulatorAPI::loadListing, "/api/v1/emulator/{id}/listing/load", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::listingSourceAt, "/api/v1/emulator/{id}/listing/source_at", drogon::Get);
    ADD_METHOD_TO(EmulatorAPI::stepLine, "/api/v1/emulator/{id}/listing/step_line", drogon::Post);
    ADD_METHOD_TO(EmulatorAPI::runToLine, "/api/v1/emulator/{id}/listing/run_to_line", drogon::Post);
    // endregion Source Listing

    // region Assembler (implementation: api/debug_api.cpp)
    ADD_METHOD_TO(EmulatorAPI::assembleCode, "/api/v1/emulator/{id}/assemble", drogon::Post);
    // endregion Assembler
    METHOD_LIST_END

    // Videowall API
    void setVideowallSingleSyncMode(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

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

    /// GET /api/v1/emulator/{id}/zxpoly - the ZX-Poly group the instance belongs to
    void getZXPolyStatus(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

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
    /// @brief GET /api/v1/emulator/{id}/operations[?since=&limit=] — the operation log (OperationsStatus::Report)
    void getOperations(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    // Pulse the NMI line; body {"magic": true} selects the Scorpion MNI variant
    // (Shadow Monitor paged before the NMI so #0066 executes monitor code)
    void requestNmi(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    // The machine's front-panel switches (Profi TURBO); a flip goes through the TTD input journal like a key
    void getSwitches(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void setSwitch(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    void switchModel(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    // endregion Lifecycle Management Methods

    // region CD audio (implementation: api/cdaudio_api.cpp)
    void getStateCdAudio(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         const std::string& id) const;
    void getStateCdAudioActive(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void postCdAudioVerb(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         const std::string& id, const std::string& verb) const;
    void postCdAudioVerbActive(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& verb) const;
    // endregion CD audio

    // region Media (implementation: api/media_api.cpp)
    void getMediaList(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    void getMediaSlot(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id, const std::string& slot) const;
    void postMediaVerb(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       const std::string& id, const std::string& slot, const std::string& verb) const;
    // endregion Media

    // region Controllers (implementation: api/controllers_api.cpp)
    void getControllers(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const std::string& id) const;
    void getController(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       const std::string& id, const std::string& connector) const;
    void postControllerVerb(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                            const std::string& connector, const std::string& verb) const;
    /// GET /api/v1/controllers/actions (?instance=): the catalogue with availability (api/actions_api.cpp)
    void getActions(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    /// GET /api/v1/surfaces/context (?instance=): the instance's surface context (actions::ContextState); set it
    /// with the ctx.set action (api/actions_api.cpp)
    void getContext(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    /// The surface verbs (api/surfaces_api.cpp; actions::surfaces::Call): views, targets, sessions and pins, readouts,
    /// the activity feed
    using SurfaceCallback = std::function<void(const drogon::HttpResponsePtr&)>;
    void getSurfaceViews(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback) const;
    void postSurfaceView(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback) const;
    void putSurfaceView(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback, const std::string& view) const;
    void deleteSurfaceView(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback, const std::string& view) const;
    void getSurfaceTarget(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback) const;
    void getSurfaceSessions(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback) const;
    void postSurfaceSession(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback) const;
    void getSurfaceSession(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback,
                           const std::string& session) const;
    void deleteSurfaceSession(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback,
                              const std::string& session) const;
    void putSurfacePin(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback, const std::string& session) const;
    void getSurfaceReadouts(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback) const;
    void getSurfaceActivity(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback) const;
    /// The Stream Deck daemon's verbs (api/streamdeck_api.cpp; companion::streamdeck::Call): GET /api/v1/streamdeck
    /// = status; GET settings, udev, pair, packs; POST start, stop, restart, pair, revoke; PUT settings; DELETE pair;
    /// GET / PUT decks/{deck}/brightness and decks/{deck}/pack
    void getStreamDeck(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback) const;
    void getStreamDeckVerb(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback, const std::string& verb) const;
    void postStreamDeckVerb(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback, const std::string& verb) const;
    void putStreamDeckVerb(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback, const std::string& verb) const;
    void deleteStreamDeckVerb(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback,
                              const std::string& verb) const;
    void getStreamDeckBrightness(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback,
                                 const std::string& deck) const;
    void putStreamDeckBrightness(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback,
                                 const std::string& deck) const;
    void getStreamDeckPack(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback, const std::string& deck) const;
    void putStreamDeckPack(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback, const std::string& deck) const;
    /// Monitoring (api/monitoring_api.cpp; monitoring::Call): GET /api/v1/monitor = `all`, /api/v1/monitor/{name} one
    /// name (host.stats, host.clients, emulator.stats, debug.breakpoints, ...); ?instance= an id or an index
    void getMonitor(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback) const;
    void getMonitorName(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback, const std::string& name) const;
    /// GET /api/v1/emulator/{id}/breakpoints/groups: monitoring::Call `breakpoint_groups` ({groups: [{name, total,
    /// active, ids}]}, the user breakpoints)
    void getBreakpointGroups(const drogon::HttpRequestPtr& req, SurfaceCallback&& callback, const std::string& id) const;
    /// POST /api/v1/controllers/actions/{action} {instance?, value?}: run it on the instance's actions worker
    void postAction(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& action) const;
    void getHostControllers(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            const std::string& verb) const;
    void postHostControllers(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             const std::string& verb) const;
    // endregion Controllers

    // region Tape/Disk/Snapshot Control Methods (implementation: api/tape_disk_api.cpp and api/snapshot_api.cpp)
    // Tape control
    void loadTape(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id) const;
    void ejectTape(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id) const;
    void playTape(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id) const;
    void pauseTape(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id) const;
    void stopTape(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id) const;
    void rewindTape(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id) const;
    void seekTape(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id) const;
    void getTape(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                 const std::string& id) const;
    void getTapeInfo(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id) const;
    void getTapeBlock(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id, const std::string& index) const;

    // Tape audio bridge (tape-audio-bridge design §7.2) — pure file
    // conversions; the instance scope is a surface convention only
    void renderTapeAudio(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void importTapeAudio(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    void insertDisk(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id, const std::string& drive) const;
    void createDisk(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id, const std::string& drive) const;
    void ejectDisk(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id, const std::string& drive) const;
    void getDiskInfo(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id, const std::string& drive) const;

    // Disk inspection - drive listing
    void getDiskDrives(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getDiskDrive(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id, const std::string& drive) const;

    // Disk inspection - sector data
    void getDiskSector(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                       const std::string& drive, const std::string& cyl, const std::string& side,
                       const std::string& sec) const;
    void getDiskSectorRaw(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                          const std::string& drive, const std::string& cyl, const std::string& side,
                          const std::string& sec) const;
    void putDiskSector(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                       const std::string& drive, const std::string& cyl, const std::string& side,
                       const std::string& sec) const;

    // Disk inspection - track data
    void getDiskTrack(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id, const std::string& drive, const std::string& cyl,
                      const std::string& side) const;
    void getDiskTrackRaw(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                         const std::string& drive, const std::string& cyl, const std::string& side) const;

    // Disk inspection - whole image and system info
    void getDiskImage(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id, const std::string& drive) const;
    void getDiskSysinfo(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                        const std::string& drive) const;
    void getDiskCatalog(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                        const std::string& drive) const;

    // Snapshot control
    void loadSnapshot(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    void saveSnapshot(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    /// POST /snapshot/inspect - what loading a file would do (its image and the plan), nothing is written
    void inspectSnapshot(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         const std::string& id) const;
    /// GET /snapshot/formats - which formats the machine can be saved in right now, with the reason for each it cannot
    void getSnapshotFormats(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            const std::string& id) const;
    void getSnapshotInfo(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void transferState(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    // RZX input recordings (api/rzx_api.cpp)
    void playRzx(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                 const std::string& id) const;
    void stopRzx(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                 const std::string& id) const;
    void seekRzx(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                 const std::string& id) const;
    void getRzxStatus(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    // endregion Tape/Disk/Snapshot Control Methods

    // region Capture Commands Methods (implementation: api/capture_api.cpp)
    void captureOcr(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id) const;
    void captureScreen(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    /// @brief GET /api/v1/emulator/{id}/capture/framebuffer?format=rgba|index&encoding=binary|base64 — raw pixels
    void captureFramebuffer(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    /// @brief GET /api/v1/emulator/{id}/capture/next-layers?format=png|rgba&alpha= — every layer + the winner map of one Next frame
    void captureNextLayers(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                           const std::string& id) const;
    void capturePlaneB(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    // endregion Capture Commands Methods

    // region BASIC Control Methods (implementation: api/basic_api.cpp)
    void basicRun(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id) const;
    void basicInject(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id) const;
    void basicExtract(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    void basicClear(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id) const;
    void basicState(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id) const;
    void basicMode(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id) const;
    // endregion BASIC Control Methods

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

    // region Analyzer Management Methods (implementation: api/analyzers_api.cpp)
    // Analyzer control
    void getAnalyzers(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;

    void getAnalyzer(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id, const std::string& name) const;

    void setAnalyzer(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id, const std::string& name) const;

    void getAnalyzerEvents(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                           const std::string& name) const;

    void clearAnalyzerEvents(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                             const std::string& name) const;

    // Session control
    void analyzerSession(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                         const std::string& name) const;

    // Raw data access
    void getAnalyzerRawFDC(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                           const std::string& name) const;

    void getAnalyzerRawBreakpoints(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                   const std::string& id, const std::string& name) const;

    // Coverage analyzer control (implementation: api/analyzers_api.cpp)
    void startCoverage(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    void stopCoverage(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    void clearCoverage(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    void getCoverage(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    /// @brief GET /api/v1/emulator/{id}/coverage/gaps?start=0x4000&end=0xFFFF&max_gaps=256
    void getCoverageGaps(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    /// @brief POST /api/v1/emulator/{id}/ay/log — body: {"action":"start|stop|clear", "capacity":4096}
    void ayLog(const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    /// @brief GET /api/v1/emulator/{id}/ay/log?limit=256&offset=0&tail=false
    void getAYLog(const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    void getAudioMixer(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       const std::string& id) const;
    void setAudioMixer(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       const std::string& id, const std::string& source) const;
    /// @brief GET /api/v1/emulator/{id}/audio/mixer/{source} - one row or channel (AudioMixer::Read)
    void getAudioMixerSource(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                             const std::string& source) const;
    /// @brief GET /api/v1/emulator/{id}/audio/levels - every row's and channel's levels (AudioMixer::Levels, the
    /// WebSocket topic audio.mixer)
    void getAudioLevels(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const std::string& id) const;
    /// @brief POST /api/v1/emulator/{id}/audio/capture — body: {"action":"start|stop|clear", "seconds":1.0}
    void audioCapture(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    /// @brief GET /api/v1/emulator/{id}/audio/capture/status
    void audioCaptureStatus(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    /// @brief GET /api/v1/emulator/{id}/audio/capture/result?wav=true
    void audioCaptureResult(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    // endregion Analyzer Management Methods

    // region Video Recording Methods (implementation: api/recording_api.cpp)
    /// @brief POST /api/v1/emulator/{id}/video/record — body: {"action":"start|stop|pause|resume|toggle|status",
    ///        "preset":"gameplay", "mode":"single|multi|audio", "format":"gif", "container":"mp4", "fps":50,
    ///        "scale":1..4, "region":"full"|"screen", "filename":"..."} (capture::RecordingControl)
    void videoRecord(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    /// @brief GET /api/v1/emulator/{id}/video/record/status
    void videoRecordStatus(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    /// @brief GET /api/v1/recording/presets — the core's recording presets
    void recordingPresets(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    // endregion Video Recording Methods

    // region Recent files Methods (implementation: api/recent_api.cpp)
    /// @brief GET /api/v1/recent[?kind=snapshot|tape|disk|rzx|image|cd] — the files opened lately (RecentFiles)
    void getRecentFiles(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    /// @brief DELETE /api/v1/recent[?kind=...] — forget them (one kind, or all)
    void clearRecentFiles(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    // endregion Recent files Methods

    // region Memory State Methods (implementation: api/state_memory_api.cpp)
    // State inspection
    void getStateMemory(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    void getStateMemoryRAM(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    void getStateMemoryROM(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    void getStatePaging(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    // Memory read/write operations
    void readMemory(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id, const std::string& address) const;

    void writeMemory(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id) const;

    /// @brief Search Z80 memory for a byte pattern. Body:
    /// {"pattern_hex": "AF 32 0E" | "pattern": [175, 50, 14], "start": 0, "end": 65535,
    ///  "max": 64, "alignment": 1|2}
void findMemory(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id) const;

    // Page-level memory access
    void getMemoryRegions(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getMemoryRegion(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         const std::string& id, const std::string& name) const;
    void postMemoryRegion(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                          const std::string& id, const std::string& name) const;
    void readPage(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id, const std::string& type, const std::string& page) const;

    void writePage(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id, const std::string& type, const std::string& page) const;

    // ROM protection control
    void getROMProtect(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    void setROMProtect(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    // ZX-Evo flash ROM persistence (EvoFlash, evoflashrequest.h)
    void getROMFlash(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void postROMFlash(const drogon::HttpRequestPtr& req,
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

    /// @brief PUT/POST /api/v1/emulator/{id}/state/screen/overscan {"enabled": bool, "viewport": "..."} - the Pentagon's
    /// 384x304 overscan frame on or off (the GUI's View > Overscan), with the viewport that crops it (symmetric |
    /// standard | screen_only | full). A video mode is the machine's own state, so this is the one mode that
    /// automation sets by an API rather than by a port
    void putStateScreenOverscan(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                const std::string& id) const;

    /// @brief GET /api/v1/emulator/{id}/state/screen/attributes — per-cell
    /// ink/paper/bright/flash decoded from screen attribute memory
    void getStateScreenAttributes(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                  const std::string& id) const;

    /// @brief GET /api/v1/emulator/{id}/video/beam — current raster beam position
    /// (t-state, line, dot, zone) plus frame timing derived from the machine model
    void getBeamPosition(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         const std::string& id) const;

    /// @brief GET /api/v1/emulator/{id}/video/layout — the current mode's layers, beam windows and framebuffer placement
    void getVideoLayout(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const std::string& id) const;
    /// @brief GET /api/v1/emulator/{id}/video/pixel?x=&y=[&layer=] or ?t= — the memory, registers and palette cell behind a pixel
    void getVideoPixel(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       const std::string& id) const;
    /// @brief GET /api/v1/emulator/{id}/video/address?page=&offset= or ?z80= — the pixels a byte feeds
    void getVideoAddress(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         const std::string& id) const;
    /// @brief GET /api/v1/emulator/{id}/video/text[?layer=] — the text grid of a text mode
    void getVideoText(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    /// @brief GET /api/v1/emulator/{id}/video/changes?frames=1|2 — the video change log (DeviceState::VideoChanges)
    void getVideoChanges(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         const std::string& id) const;
    /// @brief GET /api/v1/emulator/{id}/video/temporal — ZX DLSS de-flicker status (algorithm, delays, timing)
    void getVideoTemporal(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                          const std::string& id) const;
    /// @brief PUT|POST /api/v1/emulator/{id}/video/temporal — body {"algorithm": "<name>"|""}; returns the new status
    void setVideoTemporal(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                          const std::string& id) const;
    /// @brief GET /api/v1/emulator/{id}/video/window — the GUI window's display settings (409 headless)
    void getVideoWindow(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const std::string& id) const;
    /// @brief PUT|POST /api/v1/emulator/{id}/video/window — body of any WindowDisplay key (gpu, crt, crt_profile,
    /// blending, ..., fullscreen, scale, hud, hud_theme, hud_notices {id: bool}, osd_keyboard, ...); returns the
    /// settings after the change
    void setVideoWindow(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const std::string& id) const;
    /// @brief GET /api/v1/emulator/{id}/video/window/crt_params — the active CRT profile's parameters (409 headless)
    void getVideoWindowCrtParams(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                 const std::string& id) const;
    /// @brief PUT|POST /api/v1/emulator/{id}/video/window/crt_params — body {values: {name: number}, steps: {name:
    /// steps}, reset: [names] | true}; returns the parameters after the change
    void setVideoWindowCrtParams(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                 const std::string& id) const;
    /// @brief POST /api/v1/emulator/{id}/video/window/crt_params/reset — every parameter back to its initial value
    void resetVideoWindowCrtParams(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                   const std::string& id) const;
    /// @brief GET /api/v1/emulator/{id}/video/viewport — overscan and the viewport that crops it
    void getVideoViewport(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                          const std::string& id) const;
    /// @brief PUT|POST /api/v1/emulator/{id}/video/viewport — body {viewport: name | next | prev} (overscan on: the
    /// state/screen/overscan route)
    void setVideoViewport(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                          const std::string& id) const;
    /// @brief POST /api/v1/emulator/{id}/video/window/record — body {path, keyboard ("" | "default" | a picture id),
    /// codec (prores | h264), fps, scale, picture_height, width, height, layout (below | side)}: record the window as it draws, the on-screen keyboard under it (409 headless)
    void startVideoWindowRecord(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                const std::string& id) const;
    /// @brief GET /api/v1/emulator/{id}/video/window/record — {recording, path, keyboard, frames, width, height}
    void getVideoWindowRecord(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              const std::string& id) const;
    /// @brief POST /api/v1/emulator/{id}/video/window/record/frames — body {frames}: a paused machine runs that many
    /// frames, each one recorded before the next runs (frame-exact whatever the host's load)
    void runVideoWindowRecordFrames(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                    const std::string& id) const;
    /// @brief POST /api/v1/emulator/{id}/video/window/record/stop — finish the file; {path, frames, width, height}
    void stopVideoWindowRecord(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                               const std::string& id) const;

    /// @brief GET /api/v1/emulator/{id}/state/screen/digest?banks=5,7&include_border=true&start=&end=
    /// FNV-1a 64 digest over screen RAM pages (or an explicit Z80 range) with
    /// poll-driven change tracking
    void getStateScreenDigest(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              const std::string& id) const;

    /// @brief GET /api/v1/emulator/{id}/ports — static port map (which devices
    /// respond to which ports under which gating) + live routing flags
    /// (trdos_active, mouse_ports_decoded, shadow_monitor_paged)
    void getPortsMap(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id) const;
    void postPortOut(const drogon::HttpRequestPtr& req,
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

    /// @brief POST /api/v1/emulator/{id}/control/audio/gs — body:
    ///        {"action": "reset|reset_card|nmi|send_command|send_data|read_status|read_data|switch_personality",
    ///         "value": 0..255, "personality": "z80|lle|lw|lightweight"}
    ///        Host-port semantics (each flushes the GS coprocessor first);
    ///        switch_personality requests a runtime personality swap applied
    ///        at the next frame boundary
    void postControlAudioGS(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            const std::string& id) const;

    /// @brief GET /api/v1/emulator/{id}/state/audio/gs/porttrace?events=N
    ///        Always-on activity counters + trace session status, optionally
    ///        the last N buffered events (host ports, GS-side ports, DAC
    ///        fetches, interrupts) - the GS-coprocessor triage tool.
    void getStateAudioGSPortTrace(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                  const std::string& id) const;

    /// @brief POST /api/v1/emulator/{id}/control/audio/gs/porttrace — body:
    ///        {"action": "start|stop|pause|resume|clear"}
    void postControlAudioGSPortTrace(const drogon::HttpRequestPtr& req,
                                     std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                     const std::string& id) const;

    void getStateAudioCovox(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            const std::string& id) const;

    void getStateAudioChannels(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                               const std::string& id) const;

    // ZX-MultiSound / MIDI (api/state_device_api.cpp)
    void getStateAudioMultiSound(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                 const std::string& id) const;
    void getStateAudioMidi(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void postControlAudioMidi(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    // ZX-bus slots (api/slots_api.cpp)
    void getSlots(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id) const;
    void getSlotsCatalog(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getSlotsMatrix(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void postSlotVerb(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id, const std::string& slot, const std::string& verb) const;
    void putSlotOptions(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                        const std::string& slot) const;

    // Audio state inspection (active emulator - no ID required)
    // TurboSound FM / Beta Disk state (api/state_device_api.cpp)
    void getStateAudioMoonSound(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateAudioMoonSoundPart(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                                    const std::string& part) const;
    void getStateAudioMoonSoundActive(const drogon::HttpRequestPtr& req,
                                      std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void getStateAudioMoonSoundPartActive(const drogon::HttpRequestPtr& req,
                                          std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                          const std::string& part) const;
    void getStateAudioFM(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateAudioFMIndex(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                              const std::string& chip) const;
    void getStateFdc(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateAudioFMActive(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void getStateAudioFMIndexActive(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                    const std::string& chip) const;
    void getStateFdcActive(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void getStateIde(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateIdeActive(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void getStateTsConf(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateTsConfActive(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void getStateTsConfTsu(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateNext(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    void getStateNextActive(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void getStateNextRegs(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                          const std::string& id) const;
    void getStateNextMmu(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         const std::string& id) const;
    void getStateNextRegJournal(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void postNextRegJournal(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            const std::string& id) const;
    /// @brief GET /api/v1/emulator/{id}/state/next/sd?kinds=&card=&since=&from=&to=&limit= — the SD cards, the SPI engine and the SD journal
    void getStateNextSd(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const std::string& id) const;
    /// @brief POST /api/v1/emulator/{id}/next/sd-journal {"enabled", "clear", "capacity"} — switch, clear or resize the SD journal
    void postNextSdJournal(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                           const std::string& id) const;
    // region Bus analyzer (implementation: api/bus_api.cpp)
    /// @brief GET /api/v1/emulator/{id}/bus — every bus of the machine and its cards
    void getBuses(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id) const;
    /// @brief GET /api/v1/emulator/{id}/bus/{bus}/events?kinds=&slot=&since=&from=&to=&limit= — the bus's trace
    void getBusEvents(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id, const std::string& bus) const;
    /// @brief GET /api/v1/emulator/{id}/bus/{bus}/findings?rules=&since=&limit= — what the checks found
    void getBusFindings(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const std::string& id, const std::string& bus) const;
    /// @brief POST /api/v1/emulator/{id}/bus/{bus}/trace-journal {"enabled", "checks", "clear", "capacity"} — trace / checks on, off
    void postBusTraceJournal(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             const std::string& id, const std::string& bus) const;
    /// @brief GET / POST /api/v1/emulator/{id}/bus/{bus}/sd-timing {"slot", "profile", "seed"} — an SD card's timing profile
    void busSdTiming(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id, const std::string& bus) const;
    /// @brief GET /api/v1/emulator/{id}/bus/{bus}/export?format=vcd|jsonl&... — the trace as VCD or JSON lines
    void getBusExport(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id, const std::string& bus) const;
    // endregion
    /// @brief GET /api/v1/emulator/{id}/state/next/divmmc?kinds=&since=&from=&to=&limit= — the DivMMC, its automap entries and the DivMMC journal
    void getStateNextDivMmc(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            const std::string& id) const;
    /// @brief POST /api/v1/emulator/{id}/next/divmmc-journal {"enabled", "clear", "capacity"} — switch, clear or resize the DivMMC journal
    void postNextDivMmcJournal(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                               const std::string& id) const;
    /// @brief GET /api/v1/emulator/{id}/state/next/nmi?kinds=&since=&from=&to=&limit= — NMI sources, the Multiface, interrupts, reset and the journal
    void getStateNextNmi(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         const std::string& id) const;
    /// @brief GET /api/v1/emulator/{id}/state/next/audio — the Next's chips (pan, registers), stereo mode, turbosound and the DAC
    void getStateNextAudio(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                           const std::string& id) const;
    /// @brief GET /api/v1/emulator/{id}/state/next/input — the Next's joystick modes (NR #05 / #0B), the Kempston port, what is not modelled
    void getStateNextInput(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                           const std::string& id) const;
    /// @brief GET /api/v1/emulator/{id}/state/next/uart — the Next's two UARTs: baud, frame, FIFOs, status, interrupts and the peers
    void getStateNextUart(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                          const std::string& id) const;
    /// @brief POST /api/v1/emulator/{id}/next/nmi-journal {"enabled", "clear", "capacity"} — switch, clear or resize the interrupt / NMI / reset journal
    void postNextNmiJournal(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            const std::string& id) const;
    /// @brief GET /api/v1/emulator/{id}/state/next/magic-port?since=&limit= — the magic debug port: its setting and the text a program wrote to it
    void getStateNextMagicPort(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                               const std::string& id) const;
    /// @brief POST /api/v1/emulator/{id}/next/magic-port {"enabled", "port", "mode", "clear"} — switch the magic port, choose its port and mode, clear its output
    void postNextMagicPort(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                           const std::string& id) const;
    /// @brief GET /api/v1/emulator/{id}/state/next/pixel?x=&y=&sx= — the pixel inspector: winner, colour, per-layer why and source address
    void getStateNextPixel(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                           const std::string& id) const;
    /// @brief POST /api/v1/emulator/{id}/next/layer-capture {"enabled": true|false} — switch the per-layer record of the Next screen
    void postNextLayerCapture(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              const std::string& id) const;
    void getStateNextDma(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         const std::string& id) const;
    void getStateNextVideo(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                           const std::string& id) const;
    void getStateNextPalette(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             const std::string& id) const;
    void getStateNextPorts(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                           const std::string& id) const;
    void getStateNextCopper(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            const std::string& id) const;
    void getStateNextSprites(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             const std::string& id) const;
    void getStateNextNextReg(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             const std::string& id) const;
    void postNextNextReg(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         const std::string& id) const;
    void getStateSprinter(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateSprinterActive(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void getStateSprinterPorts(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateSprinterPortLookup(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                    const std::string& id) const;
    void getStateSprinterText(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateSprinterVideo(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateSprinterPalette(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateSprinterSoundRing(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                   const std::string& id) const;
    void getStateSprinterBios(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateSprinterZxMode(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateSprinterPldJournal(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void postSprinterPldJournal(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void postSprinterBios(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateNetwork(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateNetworkActive(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void postNetworkConfig(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void postControlSession(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getControlSession(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void deleteControlSession(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              const std::string& id) const;
    void postControlTakeBack(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             const std::string& id) const;
    void postControlAllowAgain(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                               const std::string& id) const;
    void companionsCollection(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void companionItem(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& cid) const;
    void companionPairing(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& pid) const;
    void getStateExecution(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                           const std::string& id) const;
    void getStateExecutionActive(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void getStateRtc(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateProfi(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateProfiActive(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void getStateRtcActive(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void getRtcCells(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void postRtcCells(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateIsa(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateIsaActive(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void postControlIsa(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateIsaJournal(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    /// GET /api/v1/emulator/{id}/network/traffic?since=&adapter=&kind=&last=&format=json|pcapng
    void getNetworkTraffic(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    /// POST /api/v1/emulator/{id}/network/traffic {action: clear|start|stop|ring, path, ring_bytes}
    void postNetworkTraffic(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    /// GET /api/v1/emulator/{id}/network/adapters - the host adapters the bridge can use (network SN6)
    void getNetworkAdapters(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getNetworkFrames(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void postNetworkFrame(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateContention(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getStateContentionActive(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

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

    // region Debug Commands Methods (implementation: api/debug_api.cpp)
    // Stepping
    void step(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
              const std::string& id) const;
    void steps(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) const;
    void stepOver(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id) const;
    void stepOut(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                 const std::string& id) const;

    /// @brief POST /api/v1/emulator/{id}/skip_until — body: {"pc": "0x8000" | 32768, "max_tstates": 70000000}
    ///        Fast-forwards execution until PC reaches the target (breakpoints are skipped)
    void skipUntil(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id) const;
    void runTStates(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id) const;
    void runToScanline(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       const std::string& id) const;
    void runNScanlines(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       const std::string& id) const;
    void runToPixel(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id) const;
    void runToInterrupt(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const std::string& id) const;
    void runFrame(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id) const;
    void runFrames(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id) const;
    /// The long run-control calls' bodies, run on LongCallPool (the handlers above dispatch them)
    void stepsNow(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) const;
    void stepOverNow(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) const;
    void stepOutNow(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) const;
    void skipUntilNow(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) const;
    void runTStatesNow(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) const;
    void runToScanlineNow(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) const;
    void runNScanlinesNow(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) const;
    void runToPixelNow(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) const;
    void runToInterruptNow(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) const;
    void runFrameNow(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) const;
    void runFramesNow(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) const;

    // Debug mode
    void getDebugMode(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    void setDebugMode(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;

    // Breakpoints
    void getBreakpoints(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void addBreakpoint(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    /// @brief POST /api/v1/emulator/{id}/breakpoints/hits/reset - hit counters back to 0 ({"id": N}: one, else all)
    void resetBreakpointHits(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void clearBreakpoints(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void removeBreakpoint(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                          const std::string& bpIdStr) const;
    void enableBreakpoint(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                          const std::string& bpIdStr) const;
    void disableBreakpoint(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                           const std::string& bpIdStr) const;
    void getBreakpointStatus(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             const std::string& id) const;
    // Breakpoints by selection, groups, notes (debug_api.cpp)
    void getBreakpointsActive(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void setBreakpointsActive(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void removeBreakpoints(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void setBreakpointGroup(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                            const std::string& bpIdStr) const;
    void setBreakpointNote(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                           const std::string& bpIdStr) const;

    // Memory inspection and manipulation
    void getRegisters(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    void setRegister(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id, const std::string& name) const;
    void getMemory(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id, const std::string& addrStr) const;
    void putMemory(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id, const std::string& addrStr) const;
    void getMemoryPage(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                       const std::string& type, const std::string& page, const std::string& offset) const;
    void putMemoryPage(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                       const std::string& type, const std::string& page, const std::string& offset) const;
    void getMemoryInfo(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getMemoryMap(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    // Analysis
    void getMemCounters(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getCallTrace(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;

    // Disassembly
    /// GET /debug/snapshot?disasm=&stack=&memory=<space>:<addr>:<len>[,...] - one coherent debugger snapshot (core DebugSnapshot)
    void getDebugSnapshot(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                          const std::string& id) const;
    void getDebugWait(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    void getPcHistory(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    void postPcHistory(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       const std::string& id) const;
    void getDisasm(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id) const;
    void getDisasmPage(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    // Labels/Symbols
    void getLabels(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id) const;
    void addLabel(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id) const;
    void getLabel(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id, const std::string& name) const;
    void removeLabel(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id, const std::string& name) const;
    void updateLabel(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id, const std::string& name) const;
    void clearLabels(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id) const;
    void loadSymbols(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id) const;
    void saveSymbols(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id) const;
    // Symbol files and sets (api/symbols_api.cpp)
    void symbolFormats(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void symbolFormatsOf(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         const std::string& id) const;
    void symbolDetect(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    void symbolSets(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id) const;
    void symbolSetChange(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         const std::string& id) const;
    void symbolSetDrop(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       const std::string& id) const;
    void symbolImport(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    void symbolExport(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    void symbolScan(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id) const;
    void symbolImportLive(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                          const std::string& id) const;
    void symbolImportSource(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            const std::string& id) const;
    // Media signatures (api/signature_api.cpp)
    void signatureFile(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void signatureCompare(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void signatureMedia(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const std::string& id) const;
    // Assembler sources (api/asm_api.cpp)
    void asmFormats(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void asmDialects(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void asmFiles(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id) const;
    void asmVerb(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                 const std::string& id, const std::string& verb) const;
    /// The source an assembler holds in RAM (asm-synchronizer): GET status, POST probe / extract
    void asmSyncStatus(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       const std::string& id) const;
    void asmSync(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                 const std::string& id, const std::string& action) const;
    void asmSyncHints(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    void asmSyncUnwatch(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const std::string& id) const;
    void asmSyncPushUnwatch(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            const std::string& id) const;
    /// The sync verbs on a snapshot file with no emulator instance: GET status, POST probe / extract / inject (snapshot required)
    void asmSnapshotSyncStatus(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void asmSnapshotSync(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         const std::string& action) const;
    void resolveLabel(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;

    // Source Listing
    void loadListing(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id) const;
    void listingSourceAt(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                         const std::string& id) const;
    void stepLine(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id) const;
    void runToLine(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id) const;
    // The paced run (api/runpace_api.cpp)
    void getRunPace(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id) const;
    void postRunPace(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id) const;
    void stepLineNow(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id) const;
    void runToLineNow(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;

    // Assembler
    void assembleCode(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    // endregion Debug Commands Methods

    // region Profiler Commands Methods (implementation: api/profiler_api.cpp)
    // Opcode profiler control
    void opcodeProfilerStart(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             const std::string& id) const;
    void opcodeProfilerStop(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            const std::string& id) const;
    void opcodeProfilerPause(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             const std::string& id) const;
    void opcodeProfilerResume(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              const std::string& id) const;
    void opcodeProfilerClear(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             const std::string& id) const;
    void getProfilerStatus(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getProfilerCounters(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             const std::string& id) const;
    void getProfilerTrace(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    // Memory profiler control
    void memoryProfilerStart(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             const std::string& id) const;
    void memoryProfilerStop(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            const std::string& id) const;
    void memoryProfilerPause(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             const std::string& id) const;
    void memoryProfilerResume(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              const std::string& id) const;
    void memoryProfilerClear(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             const std::string& id) const;
    void getMemoryProfilerStatus(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                 const std::string& id) const;
    /// @brief GET .../profiler/memory/pages?limit=N - per physical page read/write/execute totals
    void getMemoryProfilerPages(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                const std::string& id) const;
    /// @brief GET .../profiler/memory/counters?mode=z80|physical&page=N&start=&end=&format=dense|sparse
    ///        - per-address read/write/execute counters
    void getMemoryProfilerCounters(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                   const std::string& id) const;
    /// @brief POST .../profiler/memory/save {"path", "format":"yaml", "single_file"} - write the data to disk
    void memoryProfilerSave(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            const std::string& id) const;

    // Call trace profiler control
    void calltraceProfilerStart(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                const std::string& id) const;
    void calltraceProfilerStop(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                               const std::string& id) const;
    void calltraceProfilerPause(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                const std::string& id) const;
    void calltraceProfilerResume(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                 const std::string& id) const;
    void calltraceProfilerClear(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                const std::string& id) const;
    void getCalltraceProfilerStatus(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                    const std::string& id) const;
    void getCalltraceProfilerEntries(const drogon::HttpRequestPtr& req,
                                     std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                     const std::string& id) const;

    // Unified profiler control
    void unifiedProfilerStart(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              const std::string& id) const;
    void unifiedProfilerStop(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             const std::string& id) const;
    void unifiedProfilerPause(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              const std::string& id) const;
    void unifiedProfilerResume(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                               const std::string& id) const;
    void unifiedProfilerClear(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              const std::string& id) const;
    void getUnifiedProfilerStatus(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                  const std::string& id) const;

    /// @brief GET /api/v1/emulator/{id}/frame_cost — per-frame work-vs-idle t-state accounting
    void getFrameCost(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    // Port trace (PDR) control — runtime feature "porttrace"
    void portTraceStart(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void portTraceStop(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void portTracePause(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void portTraceResume(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void portTraceClear(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getPortTraceStatus(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            const std::string& id) const;
    void getPortTraceEvents(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            const std::string& id) const;
    void getPortTraceFilter(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            const std::string& id) const;
    void setPortTraceFilter(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            const std::string& id) const;
    void setPortTraceConfig(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            const std::string& id) const;
    void savePortTrace(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void readPortTraceFile(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                           const std::string& id) const;

    // TS-Conf VDAC2 card: FT812 bus capture (implementation: api/vdac2_api.cpp)
    void vdac2CaptureStart(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void vdac2CaptureStop(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void vdac2CaptureStatus(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            const std::string& id) const;
    void vdac2Metrics(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      const std::string& id) const;
    void vdac2MetricsSet(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;

    // endregion Profiler Commands Methods

    // region Keyboard Injection Methods (implementation: api/keyboard_api.cpp)
    void keyTap(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                const std::string& id) const;
    void keyPress(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id) const;
    void keyRelease(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id) const;
    void keyCombo(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id) const;
    void keyMacro(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id) const;
    void keyType(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                 const std::string& id) const;
    void keyReleaseAll(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void keyAbort(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id) const;
    void keyStatus(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id) const;
    void keyRoute(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                  const std::string& id) const;
    void keyList(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                 const std::string& id) const;
    // endregion Keyboard Injection Methods

    // region Mouse Injection Methods (implementation: api/mouse_api.cpp)
    void mouseMove(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                   const std::string& id) const;
    void mouseGlide(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id) const;
    void mousePress(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id) const;
    void mouseRelease(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void mouseClick(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id) const;
    void mouseButtons(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void mouseWheel(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    const std::string& id) const;
    void mouseReleaseAll(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void mouseSetCounters(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void mouseStatus(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id) const;
    void mouseButtonList(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    // endregion Mouse Injection Methods

    // region Joystick Injection Methods (implementation: api/joystick_api.cpp)
    void joystickPress(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void joystickRelease(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void joystickSet(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id) const;
    void joystickTap(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     const std::string& id) const;
    void joystickStatus(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    // endregion Joystick Injection Methods

    // region TTD Methods (implementation: api/ttd_api.cpp)
    // Per parent TDD §10.4. Full surface available after Phase 2 completion.
    void getTTDStatus(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getTTDFileInfo(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
    void startTTD(const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void stopTTD(const drogon::HttpRequestPtr& req,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void invalidateTTD(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void historyLimitTTD(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void seekTTD(const drogon::HttpRequestPtr& req,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void exportClipTTD(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void stepBackTTD(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void stepForwardTTD(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void resumeTTD(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getTTDPosition(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getTTDMarkers(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void dumpTTD(const drogon::HttpRequestPtr& req,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void loadTTD(const drogon::HttpRequestPtr& req,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void findLastTTD(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void memoryAtTTD(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void memoryDiffTTD(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void journalTTD(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void buildJournalTTD(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void cancelJournalBuildTTD(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                               const std::string& id) const;
    void portEventsTTD(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void stepInstructionTTD(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void reverseStepTTD(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void reverseContinueTTD(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    // TD-4 — agent bookmarks (advisory annotations, never replay barriers).
    void getTTDBookmarks(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void postTTDBookmark(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void deleteTTDBookmark(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id,
                           const std::string& label) const;
    // TD-7 — coverage index queries
    void getTTDCoverageProbe(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getTTDCoverageScan(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    void getTTDCoverageSummary(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& id) const;
    // endregion TTD Methods

    // region Helper Methods (implementation: emulator_api.cpp)
private:
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

// -----------------------------------------------------------------------
// Free-function helpers (declared in api::v1; implemented in emulator_api.cpp)
// Available to any handler in the api::v1 namespace — usable from
// ttd_api.cpp, state_audio_api.cpp, etc. without an EmulatorAPI instance.
// -----------------------------------------------------------------------
/// Resolve an emulator by ID (UUID) or numeric index.
/// Does NOT auto-select; returns nullptr if not found.
std::shared_ptr<Emulator> getEmulatorByIdOrIndex(const std::string& idOrIndex);

}  // namespace v1
}  // namespace api