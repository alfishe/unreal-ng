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
    TrafficStream& stream = context->pCore->GetNetworkManager()->Stream();
    StateNode s = StateNode::Object();
    s["running"] = stream.Running();
    s["port"] = static_cast<uint64_t>(stream.Port());
    s["clients"] = static_cast<uint64_t>(stream.Clients());
    if (stream.Running())
        s["wireshark"] = "wireshark -k -i TCP@127.0.0.1:" + std::to_string(stream.Port());
    ret["stream"] = s;
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
    NetworkManager* manager = context->pCore->GetNetworkManager();
    if (action == "clear")
        tap->Clear();
    else if (action == "stream")
    {
        // The live pcapng stream for Wireshark on a TCP port (`ringBytes` carries the port; 0 = any free port)
        if (ringBytes > 65535)
        {
            error = "stream: port 0..65535";
            return false;
        }
        return manager->Stream().Start(manager->StreamListenAddress(), static_cast<uint16_t>(ringBytes), error);
    }
    else if (action == "stream-stop")
        manager->Stream().Stop();
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
        error = "action '" + action + "': clear | start | stop | ring | stream | stream-stop";
        return false;
    }
    return true;
}
}  // namespace TrafficAccess
