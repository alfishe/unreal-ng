// Automation sessions over GDB (execution arbiter design §6a, X7c): GDB's attach is a session; when the user takes
// control back at the GUI, whatever runs, steps or writes the machine is refused with the reason on the client's
// console (an O packet) and E5E - reads still answer, so the client can look and detach

#ifndef _WIN32

#include <gtest/gtest.h>

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>
#include <thread>

#include "_helpers/testthread.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "gdbpacket.h"
#include "gdbserver.h"

namespace
{

/// The next "$...#xx" packet's payload from the session ("" on timeout)
std::string ReadPacket(int fd, std::string& buffer)
{
    for (int round = 0; round < 200; ++round)
    {
        const size_t start = buffer.find('$');
        const size_t hash = start == std::string::npos ? std::string::npos : buffer.find('#', start);
        if (hash != std::string::npos && buffer.size() >= hash + 3)
        {
            std::string payload = buffer.substr(start + 1, hash - start - 1);
            buffer.erase(0, hash + 3);
            return payload;
        }
        pollfd p{fd, POLLIN, 0};
        if (poll(&p, 1, 10) <= 0)
            continue;
        char chunk[4096];
        const ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0)
            return {};
        buffer.append(chunk, static_cast<size_t>(n));
    }
    return {};
}

void Send(int fd, const std::string& payload)
{
    const std::string packet = GDBPacket::encode(payload);
    ASSERT_EQ(send(fd, packet.data(), packet.size(), 0), static_cast<ssize_t>(packet.size()));
}

}  // namespace

TEST(GdbSessionTakeBack_Test, AfterTheUserTakesControlBackRunsAndWritesAreRefusedReadsStay)
{
    std::shared_ptr<Emulator> emulator =
        EmulatorManager::GetInstance()->CreateEmulatorWithModel("gdb-takeback", "48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    auto session = std::make_unique<GDBSession>(fds[1], /*noAckMode=*/true);
    TestThread serving([&session]() { session->run(); }, [&] {
        session->stop();
        shutdown(fds[0], SHUT_RDWR);
        close(fds[0]);
    });
    std::string buffer;

    Send(fds[0], "vAttach;1");
    EXPECT_FALSE(ReadPacket(fds[0], buffer).empty());
    EmulatorContext* context = emulator->GetContext();
    EXPECT_TRUE(context->IsRunControlClaimed()) << "the attach is a session";

    ASSERT_TRUE(context->RevokeRunControl("gui"));
    Send(fds[0], "s");
    const std::string console = ReadPacket(fds[0], buffer);
    ASSERT_FALSE(console.empty());
    EXPECT_EQ(console[0], 'O');
    EXPECT_NE(GDBPacket::hexDecode(console.substr(1)).find("revoked_by_user"), std::string::npos);
    EXPECT_EQ(ReadPacket(fds[0], buffer), "E5E");

    Send(fds[0], "M4000,1:aa");
    EXPECT_EQ(ReadPacket(fds[0], buffer).substr(0, 1), "O");
    EXPECT_EQ(ReadPacket(fds[0], buffer), "E5E");

    Send(fds[0], "g");
    const std::string registers = ReadPacket(fds[0], buffer);
    EXPECT_FALSE(registers.empty());
    EXPECT_NE(registers, "E5E") << "reads still answer";

    serving.Join();  // stops the session and closes the client's end
    session.reset();
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetId());
}

/// The client goes away (its socket closes) without a detach: the session ends and run control is free again
TEST(GdbSessionTakeBack_Test, AClosedSocketEndsTheSession)
{
    std::shared_ptr<Emulator> emulator =
        EmulatorManager::GetInstance()->CreateEmulatorWithModel("gdb-close", "48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    auto session = std::make_unique<GDBSession>(fds[1], /*noAckMode=*/true);
    std::thread serving([&session]() { session->run(); });
    std::string buffer;

    Send(fds[0], "vAttach;1");
    EXPECT_FALSE(ReadPacket(fds[0], buffer).empty());
    EmulatorContext* context = emulator->GetContext();
    EXPECT_TRUE(context->IsRunControlClaimed());

    shutdown(fds[0], SHUT_RDWR);
    close(fds[0]);
    serving.join();  // the session's loop ends on the closed socket
    session.reset();
    EXPECT_FALSE(context->IsRunControlClaimed()) << "the session released run control";
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetId());
}

#endif  // _WIN32
