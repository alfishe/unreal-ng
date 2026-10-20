// sdctl viewer [--port N] [--deck SERIAL | --own MODEL] [--seconds N]: join the daemon as a device-wire v2 viewer and
// print what arrives (the welcome, the replayed picture, then frame counts once a second). A developer check of the
// daemon's multiplexer without Stream Deck Studio.
#include <chrono>
#include <cstdio>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

#include "cli.h"
#include "sdb/devicewire.h"
#include "sdb/eventloop.h"

namespace cli
{
namespace
{

const char* TypeName(std::uint8_t t)
{
    switch (t)
    {
        case sdb::wire::Output:
            return "Output";
        case sdb::wire::SetFeature:
            return "SetFeature";
        case sdb::wire::GetFeature:
            return "GetFeature";
        case sdb::wire::HostInput:
            return "HostInput";
        case sdb::wire::HostNote:
            return "HostNote";
        case sdb::wire::HostPage:
            return "HostPage";
        case sdb::wire::Welcome:
            return "Welcome";
        case sdb::wire::DeckState:
            return "DeckState";
        case sdb::wire::ReplayBegin:
            return "ReplayBegin";
        case sdb::wire::ReplayEnd:
            return "ReplayEnd";
        default:
            return "?";
    }
}

}  // namespace

int CmdViewer(const Args& a)
{
    using namespace sdb;
    const int port = a.Num("port", wire::kDefaultPort);
    std::string error;
    const net::SocketHandle s = net::Connect("127.0.0.1", port, 2000, &error);
    if (s == net::kInvalidSocket)
    {
        std::fprintf(stderr, "error: %s (is unreal-streamdeck run going?)\n", error.c_str());
        return kNoDevice;
    }
    nlohmann::json join = {{"v", wire::kVersion}, {"name", "sdctl viewer"}};
    if (a.Has("own"))
        join.update({{"role", "own"}, {"model", a.Str("own")}});
    else
        join.update({{"role", "mirror"}, {"deck", a.Str("deck", "first")}});
    const std::string hello = wire::EncodeFrame(wire::Join, join.dump());
    if (!net::SendAll(s, hello.data(), hello.size(), 2000))
    {
        net::CloseSocket(s);
        std::fprintf(stderr, "error: send failed\n");
        return kDeviceError;
    }

    const auto t0 = std::chrono::steady_clock::now();
    auto ms = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
    const auto end = t0 + std::chrono::milliseconds(static_cast<long long>(a.Dbl("seconds", 5) * 1000));
    wire::FrameReader reader;
    std::map<std::string, int> counts;
    std::size_t bytes = 0;
    bool replaying = false;
    auto nextReport = t0 + std::chrono::seconds(1);
    char buf[65536];
    int rc = kOk;
    while (std::chrono::steady_clock::now() < end)
    {
        const int ready = net::WaitSocket(s, true, false, 100);
        if (ready < 0)
            break;
        if (ready > 0)
        {
            const long n = net::Recv(s, buf, sizeof buf);
            if (n < 0)
            {
                std::printf("%8.1f closed by the daemon\n", ms());
                rc = kDeviceError;
                break;
            }
            reader.Feed(std::string_view(buf, static_cast<std::size_t>(n)));
        }
        wire::Frame f;
        while (reader.Next(f))
        {
            bytes += f.payload.size() + 5;
            ++counts[TypeName(f.type)];
            if (f.type == wire::Welcome || f.type == wire::DeckState)
                std::printf("%8.1f %s %s\n", ms(), TypeName(f.type), f.payload.c_str());
            else if (f.type == wire::ReplayBegin)
                replaying = true;
            else if (f.type == wire::ReplayEnd)
            {
                replaying = false;
                std::printf("%8.1f replay:", ms());
                for (const auto& [k, v] : counts)
                    std::printf(" %s %d", k.c_str(), v);
                std::printf(" (%zu bytes)\n", bytes);
            }
            else if (f.type == wire::GetFeature)
            {
                // An own deck asks this "device": answer with the request itself (no serial, no firmware).
                const std::string reply = wire::EncodeFrame(wire::FeatureReply, f.payload);
                (void)net::SendAll(s, reply.data(), reply.size(), 1000);
            }
            else if (f.type == wire::HostNote && !replaying)
                std::printf("%8.1f note: %s\n", ms(), f.payload.c_str());
        }
        if (std::chrono::steady_clock::now() >= nextReport)
        {
            nextReport += std::chrono::seconds(1);
            std::printf("%8.1f frames:", ms());
            for (const auto& [k, v] : counts)
                std::printf(" %s %d", k.c_str(), v);
            std::printf(" (%zu bytes)\n", bytes);
        }
        std::fflush(stdout);
    }
    const std::string bye = wire::EncodeFrame(wire::Leave, {});
    (void)net::SendAll(s, bye.data(), bye.size(), 500);
    net::CloseSocket(s);
    return rc;
}

}  // namespace cli
