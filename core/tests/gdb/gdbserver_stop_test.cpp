// GDBServer::stop() while the accept loop waits for a client: it must return. A blocking accept() is not woken by
// close() on Linux (the Steam Deck build hung at exit there, 2026-10-20), so the loop polls and stop() joins first

#ifndef _WIN32

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <memory>
#include <thread>

#include "_helpers/testwaithelper.h"
#include "gdbserver.h"

namespace
{

/// A port nothing listens on now (bound to 0, read back, released)
uint16_t FreePort()
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    socklen_t length = sizeof(addr);
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &length) != 0)
        addr.sin_port = 0;
    close(fd);
    return ntohs(addr.sin_port);
}

}  // namespace

// Up to ~120 ms: stop() waits for the accept loop's poll quantum (100 ms, as the DeZog and ZRCP servers)
TEST(GdbServer_Test, StopReturnsWhileTheAcceptLoopWaitsForAClient)
{
    const uint16_t port = FreePort();
    ASSERT_NE(port, 0);
    auto* server = new GDBServer();
    ASSERT_TRUE(server->start(port));

    // A client comes and goes: once its session is gone, the accept loop is certainly back waiting for the next one
    const int client = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    ASSERT_EQ(connect(client, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    ASSERT_TRUE(TestWait::For([&] { return server->getSessionCount() == 1; }));
    shutdown(client, SHUT_RDWR);
    close(client);
    ASSERT_TRUE(TestWait::For([&] { return server->getSessionCount() == 0; }));

    auto stopped = std::make_shared<std::atomic<bool>>(false);   // outlives the test if stop() hangs
    std::thread stopping([server, stopped] {
        server->stop();
        *stopped = true;
    });
    const bool returned = TestWait::For([&] { return stopped->load(); });
    EXPECT_TRUE(returned) << "stop() hangs while the accept loop waits";
    if (!returned)
    {
        stopping.detach();   // the server stays alive for the hung thread: leaked on purpose
        return;
    }
    stopping.join();
    EXPECT_FALSE(server->isRunning());
    delete server;
}

#endif  // _WIN32
