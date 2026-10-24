#pragma once

/// @file endpointmerge.h
/// @brief One logical host device per physical device when several endpoints see it (architecture.md §5, D18).
/// Pure logic, no OS calls: a backend feeds it what it sees and asks it which endpoint gives input and which
/// companion endpoint gives outputs. Used by the macOS IOHID backend (IOHID interfaces + GameController); written
/// without macOS types so the fakes test it on every OS.
///
/// - Raw HID endpoints of one physical device (the same `physical` key: bus, VID, PID, serial or port) are one device.
///   The endpoint with the most controls gives input, the others none (architecture.md §4 rule 2: one physical button,
///   one control event). An empty `physical` never merges (a two-player encoder shows two pads on one USB port).
/// - A companion endpoint (a `GCController`) hides VID / PID: it is matched to a raw HID device by its vendor name and
///   arrival (a tentative match) and confirmed by button-edge correlation: the same press on both within
///   kEdgeWindowMs, kConfirmEdges times, more often than on any other candidate and more often than not. Once
///   confirmed it adds the outputs the raw endpoint lacks.
/// - A companion with no raw device to match (GameController sees a pad IOHID does not: no unclaimed device of its
///   name) is standalone after kArrivalWindowMs (StandaloneDue): the backend reports it as a device of
///   its own (`gc:<vendor>#<n>`, architecture.md §5) with GameController's input. When its raw device appears later
///   and the presses confirm the match, the confirmation says `promoted`: the backend drops the `gc:` device and the
///   raw one goes on (its settings move, HostInputService::OnDevicePromoted).
///
/// Not thread-safe: the backend calls it under its own lock.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "hostdevice.h"

namespace controllers
{

/// One raw HID endpoint (an IOHID device; an interface of a physical device)
struct HidEndpointInfo
{
    std::string key;       ///< the backend's handle, unique while connected
    HostDeviceId id;       ///< the id the device gets when this endpoint is its first
    std::string physical;  ///< the physical device; "" for an endpoint that never merges
    std::string name;      ///< the product name the OS gives (kIOHIDProductKey): matched to a companion's vendor name
    size_t controls = 0;   ///< controls its driver decodes: the input endpoint is the one with the most
    uint64_t arrivalMs = 0;
};

/// One companion endpoint (a GCController): no VID / PID, a vendor name
struct CompanionInfo
{
    std::string key;
    std::string vendorName;
    uint64_t arrivalMs = 0;
};

class EndpointMerge
{
public:
    /// The same press on both endpoints: at most this far apart (architecture.md §5)
    static constexpr uint64_t kEdgeWindowMs = 20;
    /// Coincident presses that confirm a match
    static constexpr int kConfirmEdges = 2;
    /// A companion arriving this close to a raw device is its tentative match (both arrive at a connect, or both at
    /// the start)
    static constexpr uint64_t kArrivalWindowMs = 3000;

    enum class MatchState : uint8_t
    {
        None,       ///< no companion is near it
        Tentative,  ///< a companion of its name arrived with it; not confirmed by presses yet (no outputs from it)
        Confirmed,  ///< the companion is its other endpoint: it adds outputs
    };

    struct HidAdded
    {
        HostDeviceId device;     ///< the logical device the endpoint belongs to
        bool newDevice = false;  ///< the device's first endpoint: the backend reports it
    };
    struct HidRemoved
    {
        HostDeviceId device;
        bool deviceGone = false;  ///< its last endpoint: the backend reports the removal
    };
    /// A match a press just confirmed
    struct Confirmed
    {
        std::string companion;
        HostDeviceId device;
        bool promoted = false;  ///< the companion was standalone: its `gc:` device gives way to @p device
    };

    /// region <Raw HID endpoints>
    HidAdded AddHid(const HidEndpointInfo& endpoint);
    /// @p nowMs: a companion the device had waits from then for a raw device again before it is standalone
    HidRemoved RemoveHid(const std::string& key, uint64_t nowMs = 0);
    /// The endpoint gives its device's input (the one with the most controls; the first on a tie)
    bool IsInput(const std::string& key) const;
    /// The endpoints of a device, the input one first
    std::vector<std::string> EndpointsOf(const HostDeviceId& device) const;
    /// endregion </Raw HID endpoints>

    /// region <Companion endpoints>
    void AddCompanion(const CompanionInfo& companion);
    void RemoveCompanion(const std::string& key);
    /// A press on a raw device (its input endpoint), in the steady clock's milliseconds
    std::optional<Confirmed> HidEdge(const HostDeviceId& device, uint64_t timeMs);
    /// A press on a companion
    std::optional<Confirmed> CompanionEdge(const std::string& companion, uint64_t timeMs);

    /// The companions that became standalone by @p nowMs: unconfirmed, kArrivalWindowMs past their arrival, and no
    /// unclaimed raw device of their name (or a nameless one that arrived with them). Each is returned once; it stays standalone until confirmed
    std::vector<std::string> StandaloneDue(uint64_t nowMs);
    bool IsStandalone(const std::string& companion) const;

    MatchState StateOf(const HostDeviceId& device) const;
    /// The confirmed companion of a device; "" while there is none
    std::string CompanionOf(const HostDeviceId& device) const;
    /// The device a companion is confirmed for; "" while unconfirmed
    HostDeviceId DeviceOf(const std::string& companion) const;
    /// endregion </Companion endpoints>

    /// Vendor name and product name of one device: equal or one inside the other, ignoring case and spaces at the ends
    /// ("Xbox Wireless Controller" both; "DUALSHOCK 4 Wireless Controller" and "Wireless Controller")
    static bool NamesMatch(const std::string& vendorName, const std::string& productName);

private:
    struct Device
    {
        std::string physical;
        std::string name;
        uint64_t arrivalMs = 0;
        std::vector<std::pair<std::string, size_t>> endpoints;  ///< key, controls; the input one first
        std::vector<uint64_t> edges;                            ///< recent presses (kKeptEdges)
        std::string companion;                                  ///< confirmed
    };
    struct CompanionEdgeRecord
    {
        uint64_t timeMs = 0;
        std::vector<HostDeviceId> hit;  ///< devices with a press within the window
    };
    struct Companion
    {
        std::string vendorName;
        uint64_t arrivalMs = 0;
        HostDeviceId device;  ///< confirmed
        std::vector<CompanionEdgeRecord> edges;  ///< the recent presses (kKeptEdges), for a raw press that comes later
        int presses = 0;                         ///< every press since it arrived, unconfirmed
        std::map<HostDeviceId, int> hits;  ///< per candidate: its presses within the window of this companion's
        bool standalone = false;           ///< StandaloneDue declared it
    };
    static constexpr size_t kKeptEdges = 16;

    std::vector<HostDeviceId> CandidatesLocked(const Companion& companion) const;
    std::optional<Confirmed> ConfirmLocked(const std::string& key, Companion& companion);
    static void Order(Device& device);

    std::map<HostDeviceId, Device> _devices;
    std::map<std::string, HostDeviceId> _endpoints;  ///< endpoint key -> device
    std::map<std::string, Companion> _companions;
};

}  // namespace controllers
