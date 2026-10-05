#include "emulator/io/network/traffic/trafficaccess.h"

#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/io/network/traffic/networktraffictap.h"

namespace
{
NetworkTrafficTap* TapOf(EmulatorContext* context, std::string& error)
{
    NetworkManager* manager = context && context->pCore ? context->pCore->GetNetworkManager() : nullptr;
    if (!manager)
    {
        error = "this machine has no network manager";
        return nullptr;
    }
    return &manager->Traffic();
}

NetworkTrafficTap::Filter FilterOf(const TrafficAccess::Query& q)
{
    NetworkTrafficTap::Filter f;
    f.since = q.since;
    f.adapter = q.adapter;
    f.kind = q.kind == "frame" ? 0 : q.kind == "socket" ? 1 : -1;
    f.last = q.last;
    return f;
}
}  // namespace

namespace TrafficAccess
{
StateNode Records(EmulatorContext* context, const Query& query)
{
    std::string error;
    NetworkTrafficTap* tap = TapOf(context, error);
    StateNode ret = StateNode::Object();
    ret["available"] = tap != nullptr;
    if (!tap)
    {
        ret["error"] = error;
        return ret;
    }
    ret["tap"] = tap->Describe();
    StateNode records = StateNode::Array();
    for (const TrafficRecord& r : tap->Records(FilterOf(query)))
        records.push(NetworkTrafficTap::RecordNode(r));
    ret["records"] = records;
    return ret;
}

bool Pcapng(EmulatorContext* context, const Query& query, std::vector<uint8_t>& out, std::string& error)
{
    NetworkTrafficTap* tap = TapOf(context, error);
    if (!tap)
        return false;
    out = tap->Pcapng(FilterOf(query));
    return true;
}

bool Control(EmulatorContext* context, const std::string& action, const std::string& path, uint64_t ringBytes,
             std::string& error)
{
    NetworkTrafficTap* tap = TapOf(context, error);
    if (!tap)
        return false;
    if (action == "clear")
        tap->Clear();
    else if (action == "start")
    {
        if (path.empty())
        {
            error = "start needs a file path (a .pcapng file to write)";
            return false;
        }
        return tap->StartFile(path, error);
    }
    else if (action == "stop")
        tap->StopFile();
    else if (action == "ring")
    {
        if (!ringBytes)
        {
            error = "ring needs ring_bytes";
            return false;
        }
        tap->SetRingBytes(static_cast<size_t>(ringBytes));
    }
    else
    {
        error = "action '" + action + "': clear | start | stop | ring";
        return false;
    }
    return true;
}
}  // namespace TrafficAccess
