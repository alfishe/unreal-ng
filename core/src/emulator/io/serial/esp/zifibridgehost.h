#pragma once

/// @file zifibridgehost.h
/// @brief What the ZIFI-NATIVE file servers (FTP, WebDAV) need from the module they run in: its socket stack, its
/// VFS client, the emulated clock, the event frames to the Z80, the Wi-Fi state and the ESP's own clock.

#include <cstdint>
#include <vector>

class EspStack;
class ZiFiVfsBridge;

class ZiFiBridgeHost
{
public:
    virtual ~ZiFiBridgeHost() = default;

    virtual EspStack& BridgeStack() = 0;
    virtual ZiFiVfsBridge& BridgeVfs() = 0;
    /// Emulated time (T-states) and microseconds as T-states
    virtual uint64_t BridgeNow() const = 0;
    virtual uint64_t BridgeMicros(uint64_t us) const = 0;
    /// An event frame to the Z80 (60 / 61 FTP, 66 Wi-Fi signal): S3 through its 8-deep queue, E01 at once
    virtual void BridgeEvent(uint8_t cmd, const std::vector<uint8_t>& data) = 0;
    /// Room for one more event in the queue to the Z80 (the WC updater waits up to 5 s for it)
    virtual bool BridgeEventRoom() const { return true; }
    virtual bool BridgeWifiUp() const = 0;
    virtual uint32_t BridgeIp() const = 0;
    /// The ESP's time of day (S3: SNTP after the Wi-Fi join); false while it is not set
    virtual bool BridgeClock(int64_t& unixNow) const = 0;
    /// The zone from zifi.ini (time:, whole hours)
    virtual int8_t BridgeTimeZone() const = 0;
};
