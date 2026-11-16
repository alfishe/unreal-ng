#pragma once

/// @file vdac4espfirmware.h
/// @brief The VDAC4 ESP firmware's command layer: what Application::handle (src/main.cpp) and ModuleApp
/// (src/apps/module_app.cpp) do with the Z80's frames.
///
/// Port of VDAC4 `ESP32 S3 Zero N8R8/Firmware` (MIT License, Copyright (c) andrewinsidelazarev):
///   src/main.cpp           Application::handle, pollLink, handleSysInfo, handleReset, handleSysClock,
///                          handlePathGet / handlePathSet, handleTangRead / handleTangWrite, LINK_PROBE,
///                          reportError / clearError, sendWifiFailure / sendNetworkFailure, sendEvent
///   src/apps/module_app.cpp handleQuery, allocateDelivery, handlePut, handleCommit, adoptResource,
///                          dropDelivery, handleStart, startApp, stopApp, moduleInUse, handleCommand (app range),
///                          coreReply, coreEvent, coreLinkMedia, coreAppDone, coreResource(Release), pollCore1
///   include/zifi/ft_detach_guard.hpp   FtDetachGuard
///
/// Implemented: ECHO 00, SYS_INFO 02 (ACK, then 82 text; FW:unreal-ng), PING 04 -> F0, GET_STEP 05,
/// SYS_RESET 0F (ACK; Vdac4Esp flushes, waits 100 ms and restarts), SYS_CLOCK 29 (validated and kept; no use of it
/// here), PATH_GET 2A / PATH_SET 2B (path.cfg in the cache directory), LINK_PROBE 3E, TANG_READ 45 /
/// TANG_WRITE 46 (through IVdac4TangLink; only the setting registers are writable), MOD_QUERY 69 / MOD_PUT 6A /
/// MOD_COMMIT 6B, APP_START 6C / APP_STOP 6D, the app's command range and media range (IVdac4AppRuntime).
///
/// Answered as this firmware answers without the service ("not available"):
///   - Wi-Fi / network 01 03 06-0E 10-14 20 21 23: ACK (not 12), EE "network not available", the failure
///     response of sendWifiFailure / sendNetworkFailure; app network commands 1A-1D 22 24-28: ACK, EE, [0].
///   - VDISK 15-19 1E: result kNoCard (4) in the result frame (95-99, 9E).
///   - CORE_LIST 2C / CORE_APP 2D: the firmware's no-LINK replies.
///   - audio 30-3F (other than 3E) and VFS 40-5F: no app takes them -> EE "unsupported:XX" (the audio player and
///     the VFS bridge are not emulated).
///
/// PSRAM: deliveries and resources share kPsramBytes (8 MiB PSRAM less the firmware's own use); a delivery that
/// does not fit stops the app and releases resources, largest first, as allocateDelivery does.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "vdac4appruntime.h"
#include "vdac4espmodules.h"

class EspFrameTransport;
class EspSpiLink;
class Vdac4MediaRange;
class Vdac4EspStateWriter;
class Vdac4EspStateReader;
struct EspPacket;

class Vdac4EspFirmware final : private IVdac4AppHost
{
public:
    static constexpr const char* kVersion = "unreal-ng";
    static constexpr uint32_t kMaxModuleBytes = 2 * 1024 * 1024;
    static constexpr uint32_t kMaxResourceBytes = 7 * 1024 * 1024;
    static constexpr size_t kMaxResources = 4;
    static constexpr size_t kPsramBytes = 7 * 1024 * 1024 + 512 * 1024;
    static constexpr uint32_t kAbandonedMs = 5000;
    static constexpr uint32_t kFtDetachPeriodMs = 100;
    static constexpr size_t kMaxPacketsPerPass = 64;

    /// `cacheDirectory` stands for the ESP's LittleFS: modules in `<dir>/zm`, `<dir>/path.cfg`
    Vdac4EspFirmware(EspFrameTransport& transport, EspSpiLink& link, Vdac4MediaRange& media,
                     std::filesystem::path cacheDirectory, size_t cacheCapacity = EspModuleCache::kDefaultCapacity);
    ~Vdac4EspFirmware() override;

    void SetRuntime(IVdac4AppRuntime* runtime) { _runtime = runtime; }
    void SetTang(IVdac4TangLink* tang) { _tang = tang; }
    /// Every packet the Z80 sent, before it is handled (diagnostics)
    void SetPacketObserver(std::function<void(uint8_t command, size_t length, uint32_t nowMs)> observer)
    {
        _packetObserver = std::move(observer);
    }

    /// Power-up (setup): read the cache index, forget everything held in RAM
    void Boot(uint32_t nowMs);
    /// Power lost or restart: the app stops, RAM is gone (flash stays)
    void Shutdown();

    /// pollLink passes until no whole packet is waiting, a send waits for room, or SYS_RESET came
    void Pass(uint32_t nowMs);

    /// SYS_RESET answered: Vdac4Esp now flushes the ring, waits 100 ms and restarts
    bool ResetRequested() const { return _resetRequested; }

    bool AppRunning() const { return _appRunning; }
    const std::string& AppModule() const { return _appModule; }
    const Vdac4AppInfo& AppInfo() const { return _appInfo; }
    uint8_t LastStep() const { return _lastStep; }
    const char* LastError() const { return _lastError; }
    EspModuleCache& Cache() { return _cache; }
    size_t ResourceCount() const;

    void Save(Vdac4EspStateWriter& out) const;
    bool Load(Vdac4EspStateReader& in);

private:
    struct HeldResource
    {
        std::string name;
        uint32_t stamp = 0;
        std::vector<uint8_t> data;
    };

    std::function<void(uint8_t, size_t, uint32_t)> _packetObserver;

    // Application
    void Handle(const EspPacket& packet);
    void ReportError(const char* text);
    void ClearError() { _lastError[0] = 0; }
    void HandleSysInfo();
    void HandleSysClock(const EspPacket& packet);
    void HandlePathGet(const EspPacket& packet);
    void HandlePathSet(const EspPacket& packet);
    void HandleTangRead(const EspPacket& packet);
    void HandleTangWrite(const EspPacket& packet);
    void HandleLinkProbe(const EspPacket& packet);
    void HandleNetwork(uint8_t command);
    void HandleVdisk(uint8_t command);
    bool DispatchToApps(const EspPacket& packet);
    void Send(uint8_t command, const uint8_t* data = nullptr, size_t length = 0);

    // ModuleApp
    void ReplyQuery(uint8_t state);
    void HandleQuery(const EspPacket& packet);
    bool AllocateDelivery(uint32_t size, bool resource);
    void HandlePut(const EspPacket& packet);
    void HandleCommit();
    void HandleStart(const EspPacket& packet);
    void DropDelivery();
    int StartApp(const std::string& name);
    void StopApp();
    bool ModuleInUse(const std::string& name) const;
    size_t PsramUsed() const;
    void PollFtDetach();

    // IVdac4AppHost
    bool SendFrame(uint8_t command, const uint8_t* data, size_t size) override;
    int SendEvent(uint8_t code, const uint8_t* data, size_t size) override;
    int Reply(uint8_t response, const uint8_t* data, size_t size) override;
    int SetMediaRange(uint8_t first, uint8_t last) override;
    void AppDone() override;
    uint32_t NowMs() const override { return _nowMs; }
    int Resource(const char* name, const uint8_t*& data, size_t& size) override;
    void ReleaseResource(const char* name) override;
    int ReadModule(const char* name, std::vector<uint8_t>& out) override;
    void FtOpened(bool selected) override;
    IVdac4TangLink* Tang() override { return _tang; }

    EspFrameTransport& _transport;
    EspSpiLink& _link;
    Vdac4MediaRange& _media;
    std::filesystem::path _directory;
    EspModuleCache _cache;
    IVdac4AppRuntime* _runtime = nullptr;
    IVdac4TangLink* _tang = nullptr;
    uint32_t _nowMs = 0;

    uint8_t _lastStep = 0;
    char _lastError[49] = {};
    bool _resetRequested = false;
    uint8_t _clock[7] = {};  // SYS_CLOCK: the last accepted local time (year LE16, month, day, h, m, s)
    bool _clockSet = false;

    // Module delivery
    bool _deliveryActive = false;
    std::vector<uint8_t> _delivery;
    std::string _deliveryName;
    uint32_t _deliverySize = 0;
    uint32_t _deliveryStamp = 0;
    uint32_t _deliveryReceived = 0;
    bool _deliveryResource = false;
    std::vector<HeldResource> _resources;

    // App
    bool _appRunning = false;
    bool _appStale = false;
    bool _inCommand = false;
    bool _appDone = false;
    uint32_t _appCommandMs = 0;
    std::string _appModule;
    Vdac4AppInfo _appInfo;
    uint32_t _ftDetachState = 0;  // FtDetachGuard: bit 0 armed, bit 1 personality 2 seen, rest: last check ms
};
