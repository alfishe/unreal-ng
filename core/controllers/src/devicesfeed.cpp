#include "devicesfeed.h"

#include "hostinputcontrol.h"

namespace controllers
{

bool DevicesFeed::Poll(IHostInputOwner& owner, uint64_t inputRevision, Clock::time_point now)
{
    const uint64_t changes = HostInputControl::Changes();
    if (_have && inputRevision == _inputRevision && changes == _changes && now - _builtAt < kStatusEvery)
        return false;
    _inputRevision = inputRevision;
    _changes = changes;
    _builtAt = now;
    _builds++;
    HostReply reply = HostInputControl(owner).Execute({"devices", {}});
    std::string text = reply.body.Dump();
    if (_have && text == _text)
        return false;
    _have = true;
    _text = std::move(text);
    _value = std::move(reply.body);
    _revision++;
    return true;
}

}  // namespace controllers
