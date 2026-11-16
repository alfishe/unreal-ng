#pragma once

/// @file espxtensaruntime.h
/// @brief The VDAC4 firmware's app runtime on the Xtensa interpreter: IVdac4AppRuntime over EspAppHost.
///
/// Vdac4EspFirmware (the ESP's command dispatcher) calls the runtime synchronously: APP_START waits for main and
/// start, a command for app->command, as the firmware's loopTask does. Here the ESP runs in small steps until
/// that call is finished, the app's other tasks running alongside; Poll brings the ESP up to the firmware's time.
///
/// Design: docs/inprogress/2026-11-15-tsconf-vdac4/design.md §8.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "emulator/platforms/tsconf/vdac4/vdac4appruntime.h"

namespace vdac4
{

class EspAppHost;

class EspXtensaRuntime final : public IVdac4AppRuntime
{
public:
    /// ESP time a start() or a command may take before the call is given up (the board's watchdog is 5 s)
    static constexpr uint64_t kCallLimitUs = 5'000'000;

    /// `audio`: played PCM, interleaved int16 stereo at `rate`; `log`: app log lines; `mhz`: the ESP core clock
    /// ([VDAC4] EspMhz; one instruction per cycle, 0 = the board's 240 MHz)
    EspXtensaRuntime(std::function<void(const int16_t*, size_t, uint32_t)> audio,
                     std::function<void(const std::string&)> log, uint32_t mhz = 0);
    ~EspXtensaRuntime() override;

    int StartApp(const std::string& module, const std::vector<uint8_t>& elf, IVdac4AppHost& host,
                 Vdac4AppInfo& info) override;
    void StopApp() override;
    int Command(uint8_t command, const uint8_t* data, size_t size) override;
    void Poll(uint32_t nowMs) override;

    EspAppHost& Host() { return *_host; }

private:
    std::unique_ptr<EspAppHost> _host;
    IVdac4AppHost* _appHost = nullptr;
    Vdac4AppInfo* _info = nullptr;
    std::function<void(const std::string&)> _log;
};

}  // namespace vdac4
