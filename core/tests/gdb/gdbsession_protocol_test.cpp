// The GDB session against a live machine over a socket pair: the replies the RSP specification and gdb expect
// (tools/verification/gdb/verify_gdb_protocol.py checks the same live, through TCP). A Z80 loop at 0x8000 makes
// every hit deterministic:
//
//   8000 F3        DI
//   8001 AF        XOR A
//   8002 21 00 00  LD HL,0
//   8005 3C        INC A           <- LOOP: arrival k has A = HL = k
//   8006 23        INC HL
//   8007 32 00 90  LD (9000h),A    the write watch target
//   800A 47        LD B,A
//   800B 3A 01 90  LD A,(9001h)    the read watch target
//   800E 78        LD A,B
//   800F C3 05 80  JP LOOP

#ifndef _WIN32

#include <gtest/gtest.h>

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <utility>
#include <string>
#include <vector>

#include "_helpers/testthread.h"
#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "gdbpacket.h"
#include "gdbserver.h"

namespace
{
constexpr uint8_t kProgram[] = {0xF3, 0xAF, 0x21, 0x00, 0x00, 0x3C, 0x23, 0x32, 0x00,
                                0x90, 0x47, 0x3A, 0x01, 0x90, 0x78, 0xC3, 0x05, 0x80};
constexpr uint16_t kStart = 0x8000;
constexpr uint16_t kLoop = 0x8005;
constexpr uint16_t kStore = 0x8007;
constexpr uint16_t kWriteTarget = 0x9000;
constexpr uint16_t kReadTarget = 0x9001;
}  // namespace

class GdbSessionProtocol_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("gdb-protocol", "PENTAGON",
                                                                            LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _emulator->StartAsync();
        ASSERT_TRUE(TestWait::For([this] { return _emulator->GetState() == StateRun; }));
        _emulator->Pause();
        ASSERT_TRUE(_emulator->IsPaused());
        // As unreal-qt starts: gdb's breakpoints have to turn the debug mode and the breakpoints feature on
        _context->pFeatureManager->setFeature(Features::kDebugMode, false);
        _context->pFeatureManager->setFeature(Features::kBreakpoints, false);

        for (size_t i = 0; i < sizeof(kProgram); ++i)
            _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(kStart + i), kProgram[i]);
        _context->pMemory->DirectWriteToZ80Memory(kWriteTarget, 0);
        _context->pMemory->DirectWriteToZ80Memory(kReadTarget, 0);
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = kStart;
        z80->sp = 0xFF00;

        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, _fds), 0);
        _session = std::make_shared<GDBSession>(_fds[1], /*noAckMode=*/true);
        std::shared_ptr<GDBSession> session = _session;
        _serving = std::make_unique<TestThread>([session] { session->run(); }, [session] { session->stop(); });

        // One machine: the session attached to it on its own (auto-attach); otherwise attach by its pid
        const std::vector<std::string> ids = EmulatorManager::GetInstance()->GetEmulatorIds();
        _single = ids.size() == 1;
        if (!_single)
        {
            const auto it = std::find(ids.begin(), ids.end(), _emulator->GetId());
            ASSERT_NE(it, ids.end());
            const size_t pid = static_cast<size_t>(it - ids.begin()) + 1;
            ASSERT_EQ(Request("vAttach;" + GDBPacket::toHex(pid)), "T05thread:01;");
        }
    }

    void TearDown() override
    {
        _serving.reset();   // stops the session and joins it: its breakpoints are gone
        _session.reset();
        if (_fds[0] >= 0)
            close(_fds[0]);
        if (_fds[1] >= 0)
            close(_fds[1]);
        if (_emulator)
        {
            if (BreakpointManager* breakpoints = _emulator->GetBreakpointManager())
                breakpoints->ClearBreakpoints();
            if (_emulator->IsPaused())
                _emulator->Resume();
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetId());
        }
    }

    void SendRaw(const std::string& bytes)
    {
        ASSERT_EQ(send(_fds[0], bytes.data(), bytes.size(), 0), static_cast<ssize_t>(bytes.size()));
    }

    void Send(const std::string& payload) { SendRaw(GDBPacket::encode(payload)); }

    /// The next packet's payload; nullopt when none came within the timeout (the empty packet is "")
    std::optional<std::string> Read(std::chrono::milliseconds timeout = std::chrono::milliseconds(2000))
    {
        const auto deadline = std::chrono::steady_clock::now() + TestWait::Scaled(timeout);
        while (true)
        {
            const size_t start = _buffer.find('$');
            const size_t hash = start == std::string::npos ? std::string::npos : _buffer.find('#', start);
            if (hash != std::string::npos && _buffer.size() >= hash + 3)
            {
                std::string payload = _buffer.substr(start + 1, hash - start - 1);
                _buffer.erase(0, hash + 3);
                return payload;
            }
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (left.count() <= 0)
                return std::nullopt;
            pollfd p{_fds[0], POLLIN, 0};
            if (poll(&p, 1, static_cast<int>(left.count())) <= 0)
                continue;
            char chunk[4096];
            const ssize_t n = recv(_fds[0], chunk, sizeof(chunk), 0);
            if (n <= 0)
                return std::nullopt;
            _buffer.append(chunk, static_cast<size_t>(n));
        }
    }

    /// The next reply that is not console output (an 'O' packet)
    std::optional<std::string> ReadReply(std::chrono::milliseconds timeout = std::chrono::milliseconds(2000))
    {
        while (true)
        {
            std::optional<std::string> packet = Read(timeout);
            if (!packet || packet->empty() || (*packet)[0] != 'O' || *packet == "OK")
                return packet;
            _console += GDBPacket::hexDecode(packet->substr(1));
        }
    }

    std::optional<std::string> Request(const std::string& payload)
    {
        Send(payload);
        return ReadReply();
    }

    std::string Monitor(const std::string& command)
    {
        const std::optional<std::string> reply = Request("qRcmd," + GDBPacket::hexEncode(command));
        return reply ? GDBPacket::hexDecode(*reply) : std::string("<no reply>");
    }

    uint16_t Pc() const { return _context->pCore->GetZ80()->pc; }
    uint16_t Hl() const { return _context->pCore->GetZ80()->hl; }

    /// PC at the program start (P, register 5, little-endian)
    void Restart() { ASSERT_EQ(Request("P5=0080"), "OK"); }

    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    int _fds[2] = {-1, -1};
    std::shared_ptr<GDBSession> _session;
    std::unique_ptr<TestThread> _serving;
    std::string _buffer;
    std::string _console;
    bool _single = false;
};

/// RSP: a packet the stub does not support gets the empty packet $#00, never silence (gdb waits for it)
TEST_F(GdbSessionProtocol_Test, UnsupportedPacketsGetTheEmptyReply)
{
    for (const char* packet : {"qFooBar", "QFooBar", "vFooBar", "bx", "Z5,8000,1", "z5,8000,1", "!", "vCont;t",
                               "qXfer:features:read:other.xml:0,100", "qXfer:foo:read::0,100", "qOffsets", "qTStatus",
                               "vMustReplyEmpty"})
        EXPECT_EQ(Request(packet), std::optional<std::string>("")) << packet;
    EXPECT_EQ(Request("m8000,0"), std::optional<std::string>("")) << "length 0: an empty read";
}

/// qXfer reads honour offset and length: 'm' and a chunk while more follows, 'l' for the last one
TEST_F(GdbSessionProtocol_Test, QXferReadsInChunks)
{
    const std::optional<std::string> whole = Request("qXfer:features:read:target.xml:0,3fff");
    ASSERT_TRUE(whole && !whole->empty() && (*whole)[0] == 'l');
    const std::string xml = whole->substr(1);
    ASSERT_GT(xml.size(), 0x40u);
    EXPECT_EQ(Request("qXfer:features:read:target.xml:10,20"), "m" + xml.substr(0x10, 0x20));
    EXPECT_EQ(Request("qXfer:features:read:target.xml:" + GDBPacket::toHex(xml.size()) + ",100"), "l");

    std::string assembled;
    for (size_t offset = 0;; offset += 0x40)
    {
        const std::optional<std::string> chunk =
            Request("qXfer:features:read:target.xml:" + GDBPacket::toHex(offset) + ",40");
        ASSERT_TRUE(chunk && !chunk->empty());
        assembled += chunk->substr(1);
        if ((*chunk)[0] == 'l')
            break;
        ASSERT_EQ((*chunk)[0], 'm');
    }
    EXPECT_EQ(assembled, xml);
}

/// X carries binary data with '}' escapes; a 0x03 inside a packet is data, not an interrupt
TEST_F(GdbSessionProtocol_Test, XWritesEscapedBinaryData)
{
    EXPECT_EQ(Request("X8120,0:"), "OK") << "gdb's probe";
    const std::vector<uint8_t> payload = {0x12, 0x03, 0x23, 0x7D, 0x24, 0x2A, 0x00, 0xFF};
    EXPECT_EQ(Request("X8120,8:" + GDBPacket::escapeBinary(payload)), "OK");
    for (size_t i = 0; i < payload.size(); ++i)
        EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(0x8120 + i)), payload[i]) << i;
    EXPECT_EQ(Request("M8120,2:abcd"), "OK");
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0x8121), 0xCD);
    EXPECT_EQ(Request("M8120,2:ab"), "E01") << "length mismatch";
}

/// A lone 0x03 is answered at once; a packet in the same write after it is answered too
TEST_F(GdbSessionProtocol_Test, InterruptByteAloneAndBeforeAPacket)
{
    SendRaw("\x03");
    EXPECT_EQ(Read(), "T02thread:01;");
    SendRaw("\x03" + GDBPacket::encode("qC"));
    EXPECT_EQ(Read(), "T02thread:01;");
    EXPECT_EQ(Read(), "QC01");
    EXPECT_EQ(Request("?"), "T02thread:01;");
}

/// The session reads the socket while a c runs: 0x03 stops the machine and answers T02
TEST_F(GdbSessionProtocol_Test, InterruptStopsARunningContinue)
{
    Send("c");
    ASSERT_TRUE(TestWait::For([this] { return !_emulator->IsPaused(); }));
    EXPECT_FALSE(Read(std::chrono::milliseconds(20))) << "no stop reply while it runs";
    SendRaw("\x03");
    EXPECT_EQ(Read(), "T02thread:01;");
    EXPECT_TRUE(_emulator->IsPaused());
}

/// An external pause during c is a plain stop, not a breakpoint
TEST_F(GdbSessionProtocol_Test, ExternalPauseIsNotABreakpointStop)
{
    Send("c");
    ASSERT_TRUE(TestWait::For([this] { return !_emulator->IsPaused(); }));
    _emulator->Pause();
    EXPECT_EQ(Read(), "T05thread:01;");
}

/// Each kind of breakpoint names itself in the stop reply; setting one turns the debug mode and the breakpoints
/// feature on (unreal-qt starts with both off)
TEST_F(GdbSessionProtocol_Test, StopRepliesNameTheBreakpointKind)
{
    EXPECT_EQ(Request("Z0,8005,1"), "OK");
    EXPECT_TRUE(_context->pFeatureManager->isEnabled(Features::kDebugMode));
    EXPECT_TRUE(_context->pFeatureManager->isEnabled(Features::kBreakpoints));
    EXPECT_EQ(Request("c"), "T05swbreak:;thread:01;");
    EXPECT_EQ(Pc(), kLoop);
    EXPECT_EQ(Request("?"), "T05swbreak:;thread:01;");
    EXPECT_EQ(Request("z0,8005,1"), "OK");

    Restart();
    EXPECT_EQ(Request("Z1,8007,1"), "OK");
    EXPECT_EQ(Request("c"), "T05hwbreak:;thread:01;");
    EXPECT_EQ(Pc(), kStore);
    EXPECT_EQ(Request("z1,8007,1"), "OK");

    const struct
    {
        const char* set;
        const char* clear;
        const char* reply;
    } watches[] = {
        {"Z2,9000,1", "z2,9000,1", "T05watch:9000;thread:01;"},
        {"Z3,9001,1", "z3,9001,1", "T05rwatch:9001;thread:01;"},
        {"Z4,9001,1", "z4,9001,1", "T05awatch:9001;thread:01;"},
        {"Z4,9000,1", "z4,9000,1", "T05awatch:9000;thread:01;"},   // the write side of an access watch
        {"Z2,9000,2", "z2,9000,2", "T05watch:9000;thread:01;"},
    };
    for (const auto& watch : watches)
    {
        Restart();
        EXPECT_EQ(Request(watch.set), "OK") << watch.set;
        EXPECT_EQ(Request("c"), std::optional<std::string>(watch.reply)) << watch.set;
        EXPECT_EQ(Request(watch.clear), "OK") << watch.clear;
    }
    EXPECT_EQ(Request("Z2,ffff,2"), "E22") << "past the 64K view";
}

/// gdb's z packets remove only gdb's breakpoints; a watchpoint where another surface has a breakpoint still fires
TEST_F(GdbSessionProtocol_Test, OtherSurfacesBreakpointsStay)
{
    BreakpointManager& breakpoints = *_emulator->GetBreakpointManager();
    const uint16_t userExec = breakpoints.AddExecutionBreakpoint(0x8180);
    const uint16_t userWrite = breakpoints.AddMemWriteBreakpoint(0x8181);
    const uint16_t userAtTarget = breakpoints.AddExecutionBreakpoint(kWriteTarget);
    ASSERT_NE(userExec, BRK_INVALID);
    ASSERT_NE(userWrite, BRK_INVALID);
    ASSERT_NE(userAtTarget, BRK_INVALID);

    EXPECT_EQ(Request("z0,8180,1"), "OK");
    EXPECT_EQ(Request("Z2,8181,1"), "OK");
    EXPECT_EQ(Request("z2,8181,1"), "OK");
    EXPECT_EQ(Request("Z4,8181,1"), "OK");
    EXPECT_EQ(Request("z4,8181,1"), "OK");
    EXPECT_TRUE(breakpoints.GetBreakpointById(userExec));
    EXPECT_TRUE(breakpoints.GetBreakpointById(userWrite));

    EXPECT_EQ(Request("Z2,9000,1"), "OK");
    EXPECT_EQ(Request("c"), "T05watch:9000;thread:01;") << "the write watch fires though a user breakpoint is there";
    EXPECT_EQ(Request("z2,9000,1"), "OK");
    EXPECT_TRUE(breakpoints.GetBreakpointById(userAtTarget));
}

/// vCont? advertises C and S: vCont;C05 and vCont;S05 run as c and s
TEST_F(GdbSessionProtocol_Test, VContWithASignal)
{
    EXPECT_EQ(Request("vCont?"), "vCont;c;C;s;S");
    EXPECT_EQ(Request("Z0,8005,1"), "OK");
    EXPECT_EQ(Request("vCont;c"), "T05swbreak:;thread:01;");
    EXPECT_EQ(Request("vCont;S05:01"), "T05thread:01;");
    EXPECT_EQ(Pc(), 0x8006);
    EXPECT_EQ(Request("vCont;C05"), "T05swbreak:;thread:01;");
    EXPECT_EQ(Pc(), kLoop);
}

/// monitor bport: clear takes the id exactly as set printed it; another surface's breakpoint is not gdb's to clear
TEST_F(GdbSessionProtocol_Test, PortBreakpointIdsRoundTrip)
{
    BreakpointManager& breakpoints = *_emulator->GetBreakpointManager();
    // Ids below 10 read the same in decimal and hex: hold ten first
    for (uint16_t port = 0x10; port < 0x1A; ++port)
        ASSERT_NE(breakpoints.AddPortInBreakpoint(port), BRK_INVALID);
    const std::string set = Monitor("bport out fe");
    ASSERT_EQ(set.rfind("Port breakpoint set: ID ", 0), 0u) << set;
    const std::string idText = set.substr(24, set.find(' ', 24) - 24);
    const uint16_t id = static_cast<uint16_t>(std::stoul(idText));
    ASSERT_GE(id, 10);
    const BreakpointSnapshot bp = breakpoints.GetBreakpointById(id);
    ASSERT_TRUE(bp);
    EXPECT_EQ(bp->owner, "gdb");
    EXPECT_EQ(Monitor("bport clear " + idText).rfind("Port breakpoint removed", 0), 0u);
    EXPECT_FALSE(breakpoints.GetBreakpointById(id));

    const uint16_t user = breakpoints.AddPortOutBreakpoint(0xFE);
    EXPECT_EQ(Monitor("bport clear " + std::to_string(user)).rfind("Error", 0), 0u);
    EXPECT_TRUE(breakpoints.GetBreakpointById(user)) << "a user's port breakpoint stays";
    EXPECT_EQ(Monitor("bport clear zz").rfind("Error: invalid breakpoint ID", 0), 0u);
}

/// The physical view of a RAM page the machine lacks is an error, not zeros
TEST_F(GdbSessionProtocol_Test, AbsentPhysicalPageIsAnError)
{
    EXPECT_EQ(Request("m01ff0000,1"), "E03");
    EXPECT_EQ(Request("M01ff0000,1:00"), "E03");
    EXPECT_EQ(Request("m01020000,1")->size(), 2u) << "page 2 exists on a Pentagon";
}

/// monitor model names the machine
TEST_F(GdbSessionProtocol_Test, MonitorModelNamesTheMachine)
{
    const std::string text = Monitor("model");
    EXPECT_EQ(text.rfind("Model: ", 0), 0u) << text;
    EXPECT_NE(text.find("PENTAGON"), std::string::npos) << text;
}

/// c while another client holds run control runs nothing: an error, not a breakpoint stop
TEST_F(GdbSessionProtocol_Test, ContinueRefusedByAnotherClientsRunControl)
{
    if (!_single)
        GTEST_SKIP() << "needs the auto-attach (one machine): vAttach takes run control itself";
    const unreal::UUID other = unreal::UUID::Generate();
    ASSERT_TRUE(_context->TakeRunControl(other, "webapi"));
    const std::optional<std::string> reply = Request("c");
    _context->ReleaseRunControl(other);
    ASSERT_TRUE(reply);
    EXPECT_EQ(*reply, "E0D");
    EXPECT_NE(_console.find("run_refused"), std::string::npos) << _console;
    EXPECT_TRUE(_emulator->IsPaused());
}

/// D removes gdb's breakpoints and lets a machine gdb stopped run again
TEST_F(GdbSessionProtocol_Test, DetachResumesTheMachineAndRemovesTheBreakpoints)
{
    EXPECT_EQ(Request("Z0,8005,1"), "OK");
    EXPECT_EQ(Request("Z2,9000,1"), "OK");
    EXPECT_EQ(Request("c"), "T05swbreak:;thread:01;");
    EXPECT_EQ(Request("D"), "OK");
    EXPECT_TRUE(TestWait::For([this] { return !_emulator->IsPaused(); })) << "the machine runs after D";
    for (const auto& [id, bp] : _emulator->GetBreakpointManager()->GetAllBreakpoints())
        EXPECT_NE(bp->owner, "gdb") << "breakpoint " << id;
}

/// Reverse execution leaves the machine in the recorded past; a forward c from there cuts the history and runs live
/// (time-travel TDD §8.3): it moves, and the machine is writable again. A loop of 1.5 frames a round (the history
/// steps by frames):
///   8200 DI / LD HL,0 / 8204 INC HL / LD BC,1000h / 8208 DEC BC / LD A,B / OR C / JR NZ,8208 / 820D LD (9000h),HL
///   8210 JP 8204          arrival k at 8204 has HL = k
TEST_F(GdbSessionProtocol_Test, ForwardRunFromThePastGoesLive)
{
    _context->pFeatureManager->setFeature(Features::kTimeTravel, true);
    _context->pMemory->UpdateFeatureCache();
    if (!_context->pTimeTravelHooks)
        GTEST_SKIP() << "no time travel in this build";
    const uint8_t slow[] = {0xF3, 0x21, 0x00, 0x00, 0x23, 0x01, 0x00, 0x10, 0x0B, 0x78,
                            0xB1, 0x20, 0xFB, 0x22, 0x00, 0x90, 0xC3, 0x04, 0x82};
    for (size_t i = 0; i < sizeof(slow); ++i)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8200 + i), slow[i]);
    ASSERT_EQ(Request("P5=0082"), "OK");
    ASSERT_EQ(Request("Z0,8204,1"), "OK");
    ASSERT_EQ(Request("c"), "T05swbreak:;thread:01;");
    ASSERT_EQ(Monitor("ttd start").rfind("TTD recording started", 0), 0u);
    for (int i = 0; i < 3; ++i)
        ASSERT_EQ(Request("c"), "T05swbreak:;thread:01;");
    ASSERT_EQ(Hl(), 3);

    ASSERT_EQ(Request("bs"), "T05thread:01;");
    EXPECT_EQ(Pc(), 0x8210);
    EXPECT_EQ(Request("M8100,1:00"), "E0D") << "the past is read-only";

    EXPECT_EQ(Request("c"), "T05swbreak:;thread:01;");
    EXPECT_EQ((std::pair<uint16_t, uint16_t>(Pc(), Hl())), (std::pair<uint16_t, uint16_t>(0x8204, 3)))
        << "the arrival after the JP, live";
    EXPECT_NE(_console.find("history after this point is discarded"), std::string::npos) << _console;
    EXPECT_EQ(Request("M8100,1:00"), "OK") << "live again";
    EXPECT_EQ(Request("c"), "T05swbreak:;thread:01;");
    EXPECT_EQ(Hl(), 4);

    // monitor ttd stop in the past comes back to live too
    ASSERT_EQ(Request("bs"), "T05thread:01;");
    EXPECT_EQ(Monitor("ttd stop").rfind("TTD recording stopped", 0), 0u);
    EXPECT_EQ(Request("M8100,1:00"), "OK") << "live after ttd stop";
}

#endif  // _WIN32
