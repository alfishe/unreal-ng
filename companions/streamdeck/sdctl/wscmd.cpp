// sdctl ws <ws://host:port/path> [message ...] [--token T] [--seconds N] [--count N]: open a WebSocket with the
// daemon's client, send the messages once it opens, print what comes back (one line per message, ms since start).
// A developer check of the client against a live unreal-ng (/api/v1/companions/events, /api/v1/websocket).
#include <chrono>
#include <cstdio>
#include <string>

#include "cli.h"
#include "sdb/eventloop.h"
#include "sdb/websocket.h"

namespace cli
{

int CmdWs(const Args& a)
{
    if (a.pos.size() < 2)
    {
        std::fputs("usage: sdctl ws <ws://host:port/path> [message ...] [--token T] [--seconds N] [--count N]\n",
                   stderr);
        return kBadUsage;
    }
    sdb::net::EventLoop loop;
    sdb::ws::ClientOptions options;
    options.url = a.pos[1];
    options.reconnect = false;
    if (a.Has("token"))
        options.headers.emplace_back("Authorization", "Bearer " + a.Str("token"));
    sdb::ws::Client client(loop, options);

    const auto t0 = std::chrono::steady_clock::now();
    auto ms = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
    const int count = a.Num("count", 0);
    int received = 0;
    int rc = kOk;
    client.onOpen = [&] {
        std::printf("%8.1f open %s\n", ms(), options.url.c_str());
        for (std::size_t i = 2; i < a.pos.size(); ++i)
        {
            (void)client.SendText(a.pos[i]);
            std::printf("%8.1f  -> %s\n", ms(), a.pos[i].c_str());
        }
        std::fflush(stdout);
    };
    client.onText = [&](std::string_view text) {
        std::printf("%8.1f  <- %.*s\n", ms(), static_cast<int>(text.size()), text.data());
        std::fflush(stdout);
        if (count > 0 && ++received >= count)
            client.Close();
    };
    client.onClose = [&](int code, std::string_view reason) {
        std::printf("%8.1f close %d %.*s\n", ms(), code, static_cast<int>(reason.size()), reason.data());
        if (code != sdb::ws::kCloseNormal)
            rc = kDeviceError;
        loop.Stop();
    };
    loop.After(std::chrono::milliseconds(static_cast<long long>(a.Dbl("seconds", 5) * 1000)), [&] {
        if (client.IsOpen())
            client.Close();
        else
            loop.Stop();
    });
    client.Start();
    loop.Run();
    return rc;
}

}  // namespace cli
