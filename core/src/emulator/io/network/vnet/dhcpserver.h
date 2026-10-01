#pragma once

/// @file dhcpserver.h
/// @brief The virtual network's DHCP server (RFC 2131, the subset a ZX client
/// uses). One lease per client hardware address, handed out from the first
/// lease address upwards. Replies never contain Pad options: NedoOS wizcfg.com
/// reads every option as code + length (reference-wizcfg.md §3).

#include <array>
#include <cstdint>
#include <map>
#include <vector>

class DhcpServer
{
public:
    struct Settings
    {
        uint32_t serverAddr = 0;    ///< server identifier and router (the gateway)
        uint32_t mask = 0;
        uint32_t dnsAddr = 0;
        uint32_t firstLease = 0;
        uint32_t leaseCount = 64;
        uint32_t leaseSeconds = 86400;
    };

    using Mac = std::array<uint8_t, 6>;

    explicit DhcpServer(const Settings& settings) : _settings(settings) {}

    /// Handle one BOOTP request (UDP payload sent to port 67). Returns the
    /// reply payload, or empty when there is nothing to answer.
    std::vector<uint8_t> Handle(const uint8_t* request, size_t length);

    /// The lease of `mac`, made now if it has none (a station that joins the
    /// network without a DHCP exchange of its own: an emulated ESP module)
    uint32_t Lease(const Mac& mac) { return LeaseFor(mac); }

    /// Current leases (MAC -> address)
    const std::map<Mac, uint32_t>& Leases() const { return _leases; }

    void Clear() { _leases.clear(); }

    /// Replace the lease table (TTD restore)
    void SetLeases(const std::map<Mac, uint32_t>& leases) { _leases = leases; }

private:
    uint32_t LeaseFor(const Mac& mac);

    Settings _settings;
    std::map<Mac, uint32_t> _leases;
};
