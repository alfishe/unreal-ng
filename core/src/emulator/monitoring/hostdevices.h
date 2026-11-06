#pragma once

/// @file hostdevices.h
/// @brief The host input devices as the automation planes read them (`controllers.devices`, monitoringapi.h): the
/// `devices` reply of the host input owner, published by its poll on its own thread when it changed
/// (controllers::DevicesFeed), so a read is a short lock and a copy, never a request to the owner's thread. Publish
/// posts NC_CONTROLLER_DEVICES_CHANGED: the WebSocket topic pushes the value then, never by a timer.
///
/// The value: {available, revision, devices: [...]} with the `devices` reply's entries (surfaces.md §2); without an
/// owner in the process (no Qt client, a build without controllers) {available: false}.

#include <cstdint>
#include <mutex>

#include "emulator/state/statenode.h"

namespace monitoring
{

class HostDevices
{
public:
    static HostDevices& Instance();

    /// On the owner's thread: the new `devices` array and the feed's revision
    void Publish(StateNode devices, uint64_t revision);
    /// The owner went: available false again
    void Withdraw();
    StateNode Read() const;

private:
    mutable std::mutex _mutex;
    bool _available = false;
    uint64_t _revision = 0;
    StateNode _devices = StateNode::Array();
};

}  // namespace monitoring
