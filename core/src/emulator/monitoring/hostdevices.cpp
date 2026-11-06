#include "hostdevices.h"

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/platform.h"

namespace monitoring
{

HostDevices& HostDevices::Instance()
{
    static HostDevices instance;
    return instance;
}

void HostDevices::Publish(StateNode devices, uint64_t revision)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _available = true;
        _revision = revision;
        _devices = std::move(devices);
    }
    MessageCenter::DefaultMessageCenter().Post(NC_CONTROLLER_DEVICES_CHANGED);
}

void HostDevices::Withdraw()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_available)
            return;
        _available = false;
        _devices = StateNode::Array();
    }
    MessageCenter::DefaultMessageCenter().Post(NC_CONTROLLER_DEVICES_CHANGED);
}

StateNode HostDevices::Read() const
{
    StateNode node = StateNode::Object();
    std::lock_guard<std::mutex> lock(_mutex);
    node["available"] = _available;
    if (!_available)
        return node;
    node["revision"] = _revision;
    node["devices"] = _devices;
    return node;
}

}  // namespace monitoring
