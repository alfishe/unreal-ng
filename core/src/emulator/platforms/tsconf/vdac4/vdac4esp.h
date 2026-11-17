#pragma once

/// @file vdac4esp.h
/// @brief The VDAC4 board's ESP32-S3 as a high-level emulated SPI device: the SPI2 slave, the firmware's link
/// task, its frame transport and its command layer, with boot, restart and saved state.
///
/// Native C++ HLE of VDAC4 `ESP32 S3 Zero N8R8/Firmware` (MIT License, Copyright (c) andrewinsidelazarev);
/// docs/inprogress/2026-11-15-tsconf-vdac4/design.md. Parts:
///   Vdac4EspSpi        SPI2 Slave HD buffer and DMA channels (vdac4espspi.h)
///   EspSpiLink         SpiLink / SpiLinkCore: turns, bulk, put, media (vdac4esplink.h, vdac4esplinkcore.h)
///   EspFrameTransport  FrameParser / Z80Transport (vdac4espframes.h)
///   Vdac4EspFirmware   Application / ModuleApp, module cache, path.cfg (vdac4espfirmware.h, vdac4espmodules.h)
///   IVdac4AppRuntime   the app behind the firmware (vdac4appruntime.h)
///
/// Attach Spi() to the Evo's SPI hub at kSpiSlot (2), select mask #10, active high.
///
/// Time: a callback returns emulated nanoseconds (monotonic; the machine's clock). Milliseconds are taken from it
/// wherever the firmware reads millis(). Every SPI select / exchange and every Advance brings the device up to
/// date; Advance() should run at least once per emulated millisecond while the Z80 does not touch the link
/// (frame end is enough for 20 ms granularity, at the cost of app output lagging that much).
///
/// Power and boot: PowerOn() starts the boot; until bootMs (EspBootMs, default 400 ms) have passed the SPI slave
/// is not configured (MISO #FF, writes lost). Then the link starts (zero exchange area, state word 5A D7 03
/// nonce) and the firmware loop runs. The boot nonce comes from a seeded generator and differs from the last one,
/// so the Z80 sees each restart. SYS_RESET: ACK, the ring is flushed (the Z80 reads it, at most 1 s), 100 ms,
/// restart (app stopped, RAM state gone, SPI unconfigured), boot again after bootMs with a new nonce.
///
/// Firmware passes: one right after each link turn (the WRBUF window end), and one per new millisecond from
/// Advance / bus accesses. A command that completes in a turn is answered in the same turn's aftermath, so the
/// Z80's next poll can see the response.
///
/// Saved state (Save / Load): everything except the host cache directory's files (the ESP's flash) and the app
/// runtime's own state beyond IVdac4AppRuntime::SaveState. Bounded: SPI buffers and DMA channels (<= 9 KB each),
/// rings (16 KB + 4 KB), the wait backlog (<= 64 KB), the delivery and resources (<= kPsramBytes, 7.5 MiB).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include "vdac4appruntime.h"
#include "vdac4espspi.h"

class SpiDevice;
class EspSpiLink;
class EspFrameTransport;
class Vdac4EspFirmware;
class Vdac4MediaRange;

class Vdac4Esp final : private Vdac4EspSpi::IHost
{
public:
    using TimeSource = std::function<uint64_t()>;  ///< emulated nanoseconds

    static constexpr uint32_t kDefaultBootMs = 400;
    static constexpr uint32_t kResetDelayMs = 100;
    static constexpr uint32_t kFlushTimeoutMs = 1000;

    struct Config
    {
        std::filesystem::path cacheDirectory;  ///< the ESP's LittleFS (module cache, path.cfg); per instance
        uint32_t bootMs = kDefaultBootMs;
        size_t cacheCapacity = 0;   ///< 0: the firmware's (LittleFS partition less its reserve)
        uint32_t nonceSeed = 0x5EED1234;
    };

    enum class Phase : uint8_t
    {
        Off,
        Booting,
        Running,
        Flushing,     ///< SYS_RESET: waiting for the Z80 to read the ring
        RestartWait,  ///< SYS_RESET: the 100 ms before restart
    };

    Vdac4Esp(TimeSource time, Config config);
    ~Vdac4Esp() override;
    Vdac4Esp(const Vdac4Esp&) = delete;
    Vdac4Esp& operator=(const Vdac4Esp&) = delete;

    SpiDevice& Spi();
    Vdac4EspSpi& SpiSlave() { return *_spi; }

    void SetRuntime(IVdac4AppRuntime* runtime);
    void SetTang(IVdac4TangLink* tang);

    /// Power applied (or EN pulsed): boot starts now
    void PowerOn();
    void PowerOff();
    /// Bring the device to the current emulated time
    void Advance();

    Phase GetPhase() const { return _phase; }
    bool Running() const { return _phase == Phase::Running; }
    uint8_t BootNonce() const { return _bootNonce; }
    uint32_t Boots() const { return _boots; }
    uint32_t NowMs() const;

    EspSpiLink& Link() { return *_link; }
    EspFrameTransport& Transport() { return *_transport; }
    Vdac4EspFirmware& Firmware() { return *_firmware; }
    const Vdac4MediaRange& MediaRange() const { return *_media; }

    void Save(std::vector<uint8_t>& out) const;
    bool Load(const uint8_t* data, size_t size);

private:
    static constexpr uint32_t kStateMagic = 0x34434456;  // "VDC4"
    static constexpr uint16_t kStateVersion = 1;

    // Vdac4EspSpi::IHost
    void OnBusAccess() override;
    void OnBufferWritten() override;

    void UpdateTimeline(uint32_t nowMs);
    void Boot(uint32_t nowMs);
    void EnterPhase(Phase phase, uint32_t nowMs);
    uint8_t NextNonce();

    TimeSource _time;
    Config _config;
    std::unique_ptr<Vdac4MediaRange> _media;
    std::unique_ptr<Vdac4EspSpi> _spi;
    std::unique_ptr<EspSpiLink> _link;
    std::unique_ptr<EspFrameTransport> _transport;
    std::unique_ptr<Vdac4EspFirmware> _firmware;

    Phase _phase = Phase::Off;
    uint32_t _phaseStartMs = 0;
    uint32_t _lastPassMs = 0;
    uint32_t _nonceState;
    uint8_t _bootNonce = 0;
    uint32_t _boots = 0;
    bool _inHost = false;  // re-entry guard: a firmware pass never re-enters through the SPI
};
