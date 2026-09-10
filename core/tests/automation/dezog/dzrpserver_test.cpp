// dzrp::Server integration tests: real TCP session against a live emulator.
//
// A minimal blocking DZRP client drives the server exactly the way DeZog does
// (length-prefixed frames, 4-bit sequence numbers, NTF_PAUSE consumption).

#include "dezogtestfixture.h"

#include "automation-dezog.h"
#include "dzrpprotocol.h"
#include "dzrpserver.h"
#include "platform-sockets.h"

#include <cstdlib>
#include <cstring>
#include <optional>

namespace
{

class TestDzrpClient
{
public:
    ~TestDzrpClient() { disconnect(); }

    bool connect(uint16_t port)
    {
        initializeSockets();
        _sock = socket(AF_INET, SOCK_STREAM, 0);
        if (_sock == INVALID_SOCKET)
            return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        if (::connect(_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        {
            closeSocket(_sock);
            return false;
        }
        return true;
    }

    void disconnect()
    {
        if (_sock != INVALID_SOCKET)
            closeSocket(_sock);
    }

    uint8_t nextSeq()
    {
        _seq = static_cast<uint8_t>((_seq % 15) + 1);
        return _seq;
    }

    void sendRaw(const std::vector<uint8_t>& bytes)
    {
        size_t sent = 0;
        while (sent < bytes.size())
        {
            ssize_t n = send(_sock, reinterpret_cast<const char*>(bytes.data() + sent), bytes.size() - sent, 0);
            ASSERT_GT(n, 0);
            sent += static_cast<size_t>(n);
        }
    }

    // Returns full frame body (seq byte + payload), empty on timeout/close
    std::vector<uint8_t> recvFrame(int timeoutMs = 3000)
    {
        std::vector<uint8_t> header(4);
        if (!recvExact(header, timeoutMs))
            return {};
        uint32_t len = dzrp::Protocol::readU32LE(header.data());
        std::vector<uint8_t> body(len);
        if (len > 0 && !recvExact(body, timeoutMs))
            return {};
        return body;
    }

    struct Response
    {
        uint8_t seq = 0;
        bool nak = false;
        std::vector<uint8_t> payload;
        bool valid = false;
    };

    Response command(dzrp::CommandId cmd, const std::vector<uint8_t>& payload = {})
    {
        uint8_t seq = nextSeq();
        std::vector<uint8_t> frame(4);
        dzrp::Protocol::writeU32LE(frame.data(), static_cast<uint32_t>(payload.size()));
        frame.push_back(seq);
        frame.push_back(static_cast<uint8_t>(cmd));
        frame.insert(frame.end(), payload.begin(), payload.end());
        sendRaw(frame);

        Response resp;
        // Like DeZog: notifications (seq 0) may interleave with responses at any
        // time - stash them and keep reading until the matching response arrives.
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            auto body = recvFrame();
            if (body.empty())
                return resp;
            if (body[0] == dzrp::SEQ_NOTIFICATION)
            {
                _pendingNotifications.push_back(std::move(body));
                continue;
            }
            resp.seq = body[0] & 0x0F;
            resp.nak = (body[0] & dzrp::NAK_BIT) != 0;
            resp.payload.assign(body.begin() + 1, body.end());
            resp.valid = (resp.seq == seq);
            return resp;
        }
        return resp;
    }

    size_t pendingNotifications() const { return _pendingNotifications.size(); }

    struct PauseNotification
    {
        uint8_t reason = 0;
        uint16_t address = 0;
        uint8_t bank = 0;
        bool valid = false;
    };

    PauseNotification waitNotification(int timeoutMs = 3000)
    {
        PauseNotification ntf;
        std::vector<uint8_t> body;
        if (!_pendingNotifications.empty())
        {
            body = std::move(_pendingNotifications.front());
            _pendingNotifications.erase(_pendingNotifications.begin());
        }
        else
        {
            body = recvFrame(timeoutMs);
        }
        if (body.size() < 6 || body[0] != dzrp::SEQ_NOTIFICATION ||
            body[1] != static_cast<uint8_t>(dzrp::NotificationId::NTF_PAUSE))
            return ntf;
        ntf.reason = body[2];
        ntf.address = dzrp::Protocol::readU16LE(body.data() + 3);
        ntf.bank = body[5];
        ntf.valid = true;
        return ntf;
    }

private:
    bool recvExact(std::vector<uint8_t>& buf, int timeoutMs)
    {
        size_t got = 0;
        while (got < buf.size())
        {
            int ready = waitForSocketRead(_sock, timeoutMs);
            if (ready <= 0)
                return false;
            ssize_t n = recv(_sock, reinterpret_cast<char*>(buf.data() + got), buf.size() - got, 0);
            if (n <= 0)
                return false;
            got += static_cast<size_t>(n);
        }
        return true;
    }

    SOCKET _sock = INVALID_SOCKET;
    uint8_t _seq = 0;
    std::vector<std::vector<uint8_t>> _pendingNotifications;
};

std::vector<uint8_t> u16(uint16_t v)
{
    return {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8)};
}

std::vector<uint8_t> cat(std::initializer_list<std::vector<uint8_t>> parts)
{
    std::vector<uint8_t> out;
    for (const auto& p : parts)
        out.insert(out.end(), p.begin(), p.end());
    return out;
}

}  // namespace

class DZRPServer_test : public DezogEmulatorFixture
{
protected:
    void SetUp() override
    {
        DezogEmulatorFixture::SetUp();

        // Production wiring: adapter notifications → server NTF_PAUSE
        dzrp::ServerConfig config;
        config.port = 0;  // ephemeral
        config.serverName = "Unreal-NG-Test";
        _server = std::make_unique<dzrp::Server>(_adapter.get(), config);
        _adapter->setPauseNotifier([this](dzrp::BreakReason reason, uint16_t addr, uint8_t bank) {
            _server->notifyPause(reason, addr, bank);
        });
        ASSERT_TRUE(_server->start());
        ASSERT_NE(_server->getPort(), 0);

        ASSERT_TRUE(_client.connect(_server->getPort()));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    void TearDown() override
    {
        _client.disconnect();
        if (_adapter)
            _adapter->setPauseNotifier(nullptr);
        if (_server)
            _server->stop();
        _server.reset();
        DezogEmulatorFixture::TearDown();
    }

    TestDzrpClient::Response init()
    {
        std::vector<uint8_t> payload = {2, 0, 0};
        const char* name = "DeZog";
        payload.insert(payload.end(), name, name + std::strlen(name) + 1);
        return _client.command(dzrp::CommandId::CMD_INIT, payload);
    }

    std::unique_ptr<dzrp::Server> _server;
    TestDzrpClient _client;
};

/// region <Handshake / capabilities>

TEST_F(DZRPServer_test, InitReportsMachineAndVersion)
{
    auto resp = init();
    ASSERT_TRUE(resp.valid);
    EXPECT_FALSE(resp.nak);
    ASSERT_GE(resp.payload.size(), 6u);
    EXPECT_EQ(resp.payload[0], 0);  // no error
    EXPECT_EQ(resp.payload[1], dzrp::VERSION_MAJOR);
    EXPECT_EQ(resp.payload[4], static_cast<uint8_t>(dzrp::MachineType::ZX128K));
    std::string name(reinterpret_cast<const char*>(resp.payload.data() + 5));
    EXPECT_NE(name.find("Unreal-NG-Test"), std::string::npos);
}

TEST_F(DZRPServer_test, SupportedCommandsBitfield)
{
    init();
    auto resp = _client.command(dzrp::CommandId::CMD_GET_SUPPORTED_COMMANDS);
    ASSERT_TRUE(resp.valid);
    ASSERT_EQ(resp.payload.size(), 32u);
    auto has = [&](int id) { return (resp.payload[id / 8] >> (id % 8)) & 1; };
    for (int id : {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 20, 21, 24, 40, 41, 42, 43, 50, 51})
        EXPECT_TRUE(has(id)) << "command " << id;
}

TEST_F(DZRPServer_test, UnknownCommandReturnsEmptyAck)
{
    init();
    auto resp = _client.command(static_cast<dzrp::CommandId>(99));
    ASSERT_TRUE(resp.valid);
    EXPECT_FALSE(resp.nak);
    EXPECT_TRUE(resp.payload.empty());

    // Session survives
    auto regs = _client.command(dzrp::CommandId::CMD_GET_REGISTERS);
    EXPECT_TRUE(regs.valid);
}

/// endregion </Handshake / capabilities>

/// region <Registers / memory over the wire>

TEST_F(DZRPServer_test, GetRegistersCarriesLiveStateAndSlots)
{
    init();
    _adapter->setRegister(dzrp::RegisterId::PC, 0x8000);
    _adapter->setRegister(dzrp::RegisterId::HL, 0xBEEF);

    auto resp = _client.command(dzrp::CommandId::CMD_GET_REGISTERS);
    ASSERT_TRUE(resp.valid);
    ASSERT_GE(resp.payload.size(), 29u + 4u);  // 29-byte reg block (nslots@28) + 4 slots@29
    EXPECT_EQ(dzrp::Protocol::readU16LE(resp.payload.data() + 0), 0x8000);
    EXPECT_EQ(dzrp::Protocol::readU16LE(resp.payload.data() + 10), 0xBEEF);
    EXPECT_EQ(resp.payload[28], 4);  // slot count
    EXPECT_EQ(resp.payload[29 + 1], 5);
    EXPECT_EQ(resp.payload[29 + 2], 2);
}

TEST_F(DZRPServer_test, SetRegisterOverWire)
{
    init();
    auto resp = _client.command(dzrp::CommandId::CMD_SET_REGISTER,
                                cat({{static_cast<uint8_t>(dzrp::RegisterId::PC)}, u16(0x1234)}));
    ASSERT_TRUE(resp.valid);
    EXPECT_EQ(_emulator->GetZ80State()->pc, 0x1234);

    resp = _client.command(dzrp::CommandId::CMD_SET_REGISTER,
                           cat({{static_cast<uint8_t>(dzrp::RegisterId::A)}, u16(0x77)}));
    ASSERT_TRUE(resp.valid);
    EXPECT_EQ(_emulator->GetZ80State()->a, 0x77);
}

TEST_F(DZRPServer_test, MemoryRoundTripOverWire)
{
    init();
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
    auto w = _client.command(dzrp::CommandId::CMD_WRITE_MEM, cat({{0}, u16(0x8100), data}));
    ASSERT_TRUE(w.valid);

    auto r = _client.command(dzrp::CommandId::CMD_READ_MEM, cat({{0}, u16(0x8100), u16(4)}));
    ASSERT_TRUE(r.valid);
    EXPECT_EQ(r.payload, data);
    EXPECT_EQ(_emulator->GetMemory()->DirectReadFromZ80Memory(0x8103), 0x04);
}

TEST_F(DZRPServer_test, SetSlotAndWriteBankOverWire)
{
    init();
    std::vector<uint8_t> bankData(8, 0x3C);
    auto wb = _client.command(dzrp::CommandId::CMD_WRITE_BANK, cat({{6}, bankData}));
    ASSERT_TRUE(wb.valid);
    ASSERT_GE(wb.payload.size(), 1u);
    EXPECT_EQ(wb.payload[0], 0);

    auto ss = _client.command(dzrp::CommandId::CMD_SET_SLOT, {3, 6});
    ASSERT_TRUE(ss.valid);
    EXPECT_EQ(_emulator->GetMemory()->GetRAMPageForBank3(), 6);

    auto r = _client.command(dzrp::CommandId::CMD_READ_MEM, cat({{0}, u16(0xC000), u16(8)}));
    ASSERT_TRUE(r.valid);
    EXPECT_EQ(r.payload, bankData);

    _client.command(dzrp::CommandId::CMD_SET_SLOT, {3, 0});
}

TEST_F(DZRPServer_test, SetBorderOverWire)
{
    init();
    auto resp = _client.command(dzrp::CommandId::CMD_SET_BORDER, {6});
    ASSERT_TRUE(resp.valid);
    EXPECT_EQ(_emulator->GetContext()->pScreen->GetBorderColor(), 6);
}

TEST_F(DZRPServer_test, PortRoundTripOverWire)
{
    // DeZog's cspectremote.ts sendDzrpCmdReadPort takes data[0] as the value:
    // the response must be EXACTLY one byte. An empty ACK (the old behavior
    // for unimplemented commands) would surface as `undefined` in DeZog's
    // custom-dump variables view.
    init();
    auto rd = _client.command(dzrp::CommandId::CMD_READ_PORT, u16(0xFE));
    ASSERT_TRUE(rd.valid);
    EXPECT_FALSE(rd.nak);
    ASSERT_EQ(rd.payload.size(), 1u);

    auto wr = _client.command(dzrp::CommandId::CMD_WRITE_PORT, cat({u16(0xFE), {0x02}}));
    ASSERT_TRUE(wr.valid);
    EXPECT_TRUE(wr.payload.empty());

    auto rd2 = _client.command(dzrp::CommandId::CMD_READ_PORT, u16(0xFE));
    ASSERT_TRUE(rd2.valid);
    ASSERT_EQ(rd2.payload.size(), 1u);

    // Session survives
    auto regs = _client.command(dzrp::CommandId::CMD_GET_REGISTERS);
    EXPECT_TRUE(regs.valid);
}

/// endregion </Registers / memory over the wire>

/// region <Breakpoints / execution>

TEST_F(DZRPServer_test, AddAndRemoveBreakpointOverWire)
{
    init();
    auto add = _client.command(dzrp::CommandId::CMD_ADD_BREAKPOINT, cat({u16(0x8006), {0}, {0}}));
    ASSERT_TRUE(add.valid);
    ASSERT_EQ(add.payload.size(), 2u);
    uint16_t id = dzrp::Protocol::readU16LE(add.payload.data());
    ASSERT_NE(id, 0);
    EXPECT_NE(_emulator->GetBreakpointManager()->GetBreakpointById(id), nullptr);

    auto rm = _client.command(dzrp::CommandId::CMD_REMOVE_BREAKPOINT, u16(id));
    ASSERT_TRUE(rm.valid);
    EXPECT_EQ(_emulator->GetBreakpointManager()->GetBreakpointById(id), nullptr);
}

TEST_F(DZRPServer_test, ContinueHitsBreakpointAndNotifies)
{
    init();
    installProgram();

    auto add = _client.command(dzrp::CommandId::CMD_ADD_BREAKPOINT, cat({u16(PROGRAM_JP), {0}, {0}}));
    ASSERT_TRUE(add.valid);

    // CMD_CONTINUE, no temp breakpoints
    auto cont = _client.command(dzrp::CommandId::CMD_CONTINUE, std::vector<uint8_t>(11, 0));
    ASSERT_TRUE(cont.valid);

    auto ntf = _client.waitNotification();
    ASSERT_TRUE(ntf.valid);
    EXPECT_EQ(ntf.reason, static_cast<uint8_t>(dzrp::BreakReason::BREAKPOINT));
    EXPECT_EQ(ntf.address, PROGRAM_JP);
    EXPECT_TRUE(_emulator->IsPaused());

    auto regs = _client.command(dzrp::CommandId::CMD_GET_REGISTERS);
    ASSERT_TRUE(regs.valid);
    EXPECT_EQ(dzrp::Protocol::readU16LE(regs.payload.data()), PROGRAM_JP);
}

TEST_F(DZRPServer_test, ContinueWithTemporaryBreakpointsStepsAndClears)
{
    init();
    installProgram();

    // DeZog step-over shape: bp1 = next instruction, bp2 = alternate
    std::vector<uint8_t> payload = cat({{1}, u16(PROGRAM_STORE), {1}, u16(PROGRAM_JP), {0, 0, 0, 0, 0}});
    auto cont = _client.command(dzrp::CommandId::CMD_CONTINUE, payload);
    ASSERT_TRUE(cont.valid);
    EXPECT_EQ(_adapter->getTemporaryBreakpointCount(), 2u);

    auto ntf = _client.waitNotification();
    ASSERT_TRUE(ntf.valid);
    EXPECT_EQ(ntf.reason, static_cast<uint8_t>(dzrp::BreakReason::BREAKPOINT));
    EXPECT_EQ(ntf.address, PROGRAM_STORE);

    // Server clears temporaries on notify
    EXPECT_EQ(_adapter->getTemporaryBreakpointCount(), 0u);
    EXPECT_EQ(_emulator->GetBreakpointManager()->GetBreakpointsCount(), 0u);

    // Second step lands on the JP
    payload = cat({{1}, u16(PROGRAM_JP), {0, 0, 0}, {0, 0, 0, 0, 0}});
    cont = _client.command(dzrp::CommandId::CMD_CONTINUE, payload);
    ASSERT_TRUE(cont.valid);
    ntf = _client.waitNotification();
    ASSERT_TRUE(ntf.valid);
    EXPECT_EQ(ntf.address, PROGRAM_JP);
}

TEST_F(DZRPServer_test, PauseCommandNotifiesManual)
{
    init();
    installProgram();

    auto cont = _client.command(dzrp::CommandId::CMD_CONTINUE, std::vector<uint8_t>(11, 0));
    ASSERT_TRUE(cont.valid);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(_emulator->IsPaused());

    auto pause = _client.command(dzrp::CommandId::CMD_PAUSE);
    ASSERT_TRUE(pause.valid);
    // Wire order contract: ACK first, NTF_PAUSE strictly after
    EXPECT_EQ(_client.pendingNotifications(), 0u);

    auto ntf = _client.waitNotification();
    ASSERT_TRUE(ntf.valid);
    EXPECT_EQ(ntf.reason, static_cast<uint8_t>(dzrp::BreakReason::MANUAL));
    EXPECT_TRUE(_emulator->IsPaused());
    // Stopped inside the loop body
    EXPECT_GE(ntf.address, PROGRAM_START);
    EXPECT_LE(ntf.address, PROGRAM_JP + 2);
}

TEST_F(DZRPServer_test, WatchpointOverWireNotifies)
{
    init();
    installProgram();

    auto add = _client.command(dzrp::CommandId::CMD_ADD_WATCHPOINT,
                               cat({u16(WATCH_TARGET), {0}, u16(1), {static_cast<uint8_t>(dzrp::WatchAccess::WRITE)}}));
    ASSERT_TRUE(add.valid);
    ASSERT_EQ(add.payload.size(), 1u);
    EXPECT_EQ(add.payload[0], 0);

    auto cont = _client.command(dzrp::CommandId::CMD_CONTINUE, std::vector<uint8_t>(11, 0));
    ASSERT_TRUE(cont.valid);

    auto ntf = _client.waitNotification();
    ASSERT_TRUE(ntf.valid);
    EXPECT_EQ(ntf.reason, static_cast<uint8_t>(dzrp::BreakReason::WATCHPOINT_WRITE));
    EXPECT_EQ(ntf.address, WATCH_TARGET);

    auto rm = _client.command(dzrp::CommandId::CMD_REMOVE_WATCHPOINT,
                              cat({u16(WATCH_TARGET), {0}, u16(1), {static_cast<uint8_t>(dzrp::WatchAccess::WRITE)}}));
    ASSERT_TRUE(rm.valid);
    EXPECT_EQ(_adapter->getWatchpointCount(), 0u);
}

/// endregion </Breakpoints / execution>

/// region <State>

TEST_F(DZRPServer_test, ReadWriteStateOverWire)
{
    init();
    _adapter->writeMemory(0x8200, {0xA1, 0xB2});
    _adapter->setRegister(dzrp::RegisterId::PC, 0x8200);

    auto rs = _client.command(dzrp::CommandId::CMD_READ_STATE);
    ASSERT_TRUE(rs.valid);
    ASSERT_FALSE(rs.payload.empty());

    _adapter->writeMemory(0x8200, {0x00, 0x00});
    _adapter->setRegister(dzrp::RegisterId::PC, 0x0000);

    auto ws = _client.command(dzrp::CommandId::CMD_WRITE_STATE, rs.payload);
    ASSERT_TRUE(ws.valid);

    std::vector<uint8_t> expected = {0xA1, 0xB2};
    EXPECT_EQ(_adapter->readMemory(0x8200, 2), expected);
    EXPECT_EQ(_adapter->getRegisters().pc, 0x8200);
}

/// endregion </State>

/// region <Session lifecycle>

TEST_F(DZRPServer_test, CloseThenReconnect)
{
    init();
    auto close = _client.command(dzrp::CommandId::CMD_CLOSE);
    ASSERT_TRUE(close.valid);
    _client.disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    TestDzrpClient second;
    ASSERT_TRUE(second.connect(_server->getPort()));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::vector<uint8_t> payload = {2, 0, 0, 'X', 0};
    auto resp = second.command(dzrp::CommandId::CMD_INIT, payload);
    EXPECT_TRUE(resp.valid);
}

TEST_F(DZRPServer_test, ClientDropWhileRunningCleansUpAndReconnects)
{
    init();
    installProgram();

    auto add = _client.command(dzrp::CommandId::CMD_ADD_BREAKPOINT, cat({u16(PROGRAM_JP), {0}, {0}}));
    ASSERT_TRUE(add.valid);
    ASSERT_TRUE(_client.command(dzrp::CommandId::CMD_CONTINUE, std::vector<uint8_t>(11, 0)).valid);

    // VS Code window closed / network drop: no CMD_CLOSE, socket just goes away
    _client.disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Stale breakpoints are gone and the emulator is not left stuck on the hit
    EXPECT_EQ(_emulator->GetBreakpointManager()->GetBreakpointsCount(), 0u);
    EXPECT_FALSE(_emulator->IsPaused());

    // Fresh session works as if nothing happened
    TestDzrpClient second;
    ASSERT_TRUE(second.connect(_server->getPort()));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::vector<uint8_t> payload = {2, 0, 0, 'X', 0};
    ASSERT_TRUE(second.command(dzrp::CommandId::CMD_INIT, payload).valid);
    ASSERT_TRUE(second.command(dzrp::CommandId::CMD_PAUSE).valid);
    auto ntf = second.waitNotification();
    ASSERT_TRUE(ntf.valid);
    EXPECT_EQ(ntf.reason, static_cast<uint8_t>(dzrp::BreakReason::MANUAL));
}

TEST_F(DZRPServer_test, CloseCommandResumesAndDropsBreakpoints)
{
    init();
    installProgram();
    ASSERT_TRUE(_client.command(dzrp::CommandId::CMD_ADD_BREAKPOINT, cat({u16(PROGRAM_JP), {0}, {0}})).valid);
    ASSERT_TRUE(_emulator->IsPaused());

    ASSERT_TRUE(_client.command(dzrp::CommandId::CMD_CLOSE).valid);
    _client.disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(_emulator->GetBreakpointManager()->GetBreakpointsCount(), 0u);
    EXPECT_FALSE(_emulator->IsPaused());
}

TEST_F(DZRPServer_test, ReadMemFull64KOverWire)
{
    init();
    auto r = _client.command(dzrp::CommandId::CMD_READ_MEM, cat({{0}, u16(0x0000), u16(0xFFFF)}));
    ASSERT_TRUE(r.valid);
    ASSERT_EQ(r.payload.size(), 0xFFFFu);
    EXPECT_EQ(r.payload[0x0038], _emulator->GetMemory()->DirectReadFromZ80Memory(0x0038));
    EXPECT_EQ(r.payload[0xC000], _emulator->GetMemory()->DirectReadFromZ80Memory(0xC000));
}

TEST_F(DZRPServer_test, ReadMemZeroLengthOverWire)
{
    init();
    auto r = _client.command(dzrp::CommandId::CMD_READ_MEM, cat({{0}, u16(0x8000), u16(0)}));
    ASSERT_TRUE(r.valid);
    EXPECT_TRUE(r.payload.empty());
}

TEST_F(DZRPServer_test, RapidStepLoopOverWire)
{
    init();
    installProgram();
    _adapter->setRegister(dzrp::RegisterId::PC, PROGRAM_LOOP);

    uint16_t pc = PROGRAM_LOOP;
    for (int i = 0; i < 40; ++i)
    {
        uint16_t target = (pc == PROGRAM_LOOP) ? PROGRAM_STORE : (pc == PROGRAM_STORE) ? PROGRAM_JP : PROGRAM_LOOP;
        auto cont = _client.command(dzrp::CommandId::CMD_CONTINUE,
                                    cat({{1}, u16(target), {0, 0, 0}, {0, 0, 0, 0, 0}}));
        ASSERT_TRUE(cont.valid) << "step " << i;
        auto ntf = _client.waitNotification();
        ASSERT_TRUE(ntf.valid) << "step " << i;
        ASSERT_EQ(ntf.address, target) << "step " << i;
        ASSERT_EQ(_client.pendingNotifications(), 0u) << "step " << i;
        pc = target;
    }
    EXPECT_EQ(_adapter->getTemporaryBreakpointCount(), 0u);
}

TEST_F(DZRPServer_test, HistoryCommandsAdvertisedAndInfo)
{
    init();
    auto caps = _client.command(dzrp::CommandId::CMD_GET_SUPPORTED_COMMANDS);
    ASSERT_TRUE(caps.valid);
    auto has = [&](int id) { return (caps.payload[id / 8] >> (id % 8)) & 1; };
    EXPECT_TRUE(has(0xE0));
    EXPECT_TRUE(has(0xE1));

    auto info = _client.command(dzrp::CommandId::CMD_GET_HISTORY_INFO);
    ASSERT_TRUE(info.valid);
    ASSERT_EQ(info.payload.size(), 4u);
    EXPECT_EQ(info.payload[0], 1);  // available
    EXPECT_EQ(info.payload[1], 1);  // recording (started by CMD_INIT)
}

TEST_F(DZRPServer_test, HistoryEntryWireFormat)
{
    init();
    installProgram();
    ASSERT_TRUE(_client.command(dzrp::CommandId::CMD_ADD_BREAKPOINT, cat({u16(PROGRAM_JP), {0}, {0}})).valid);
    ASSERT_TRUE(_client.command(dzrp::CommandId::CMD_CONTINUE, std::vector<uint8_t>(11, 0)).valid);
    ASSERT_TRUE(_client.waitNotification().valid);

    // index 0 = a recorded instruction in the program; assert the wire shape and
    // that the opcodes bytes are coherent with a READ_MEM at the entry's PC.
    auto e = _client.command(dzrp::CommandId::CMD_GET_HISTORY_ENTRY, {0, 0, 0, 0});
    ASSERT_TRUE(e.valid);
    // error(1) + reg block(29, nslots@28) + slots(4) + opcodes(4) + sp(2)
    ASSERT_EQ(e.payload.size(), 1u + 29u + 4u + 4u + 2u);
    EXPECT_EQ(e.payload[0], 0);
    uint16_t pc = dzrp::Protocol::readU16LE(e.payload.data() + 1);
    EXPECT_GE(pc, PROGRAM_START);
    EXPECT_LE(pc, PROGRAM_JP + 2);
    EXPECT_EQ(e.payload[1 + 28], 4);  // slot count (nslots @ index 28 of the reg block)
    const uint8_t* op = e.payload.data() + 1 + 29 + 4;  // after error + reg block + 4 slots

    // Browsing moved the emulator to `pc`; READ_MEM there must match the opcodes
    auto mem = _client.command(dzrp::CommandId::CMD_READ_MEM, cat({{0}, u16(pc), u16(4)}));
    ASSERT_TRUE(mem.valid);
    for (int b = 0; b < 4; ++b)
        EXPECT_EQ(op[b], mem.payload[b]) << "opcode byte " << b;

    // Out of range → error 1, and a plain GET_REGISTERS afterwards shows the present again
    auto far = _client.command(dzrp::CommandId::CMD_GET_HISTORY_ENTRY, {0xFF, 0xFF, 0x00, 0x00});
    ASSERT_TRUE(far.valid);
    ASSERT_EQ(far.payload.size(), 1u);
    EXPECT_EQ(far.payload[0], 1);

    auto regs = _client.command(dzrp::CommandId::CMD_GET_REGISTERS);
    ASSERT_TRUE(regs.valid);
    EXPECT_EQ(dzrp::Protocol::readU16LE(regs.payload.data()), PROGRAM_JP);

    // Continue after browsing hits the breakpoint again
    ASSERT_TRUE(_client.command(dzrp::CommandId::CMD_CONTINUE, std::vector<uint8_t>(11, 0)).valid);
    auto ntf = _client.waitNotification();
    ASSERT_TRUE(ntf.valid);
    EXPECT_EQ(ntf.address, PROGRAM_JP);
}

TEST_F(DZRPServer_test, NotificationWithoutClientDoesNotCrash)
{
    _client.disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    _server->notifyPause(dzrp::BreakReason::MANUAL, 0x1234, 0);
    SUCCEED();
}

/// endregion </Session lifecycle>

/// region <AutomationDezog module>

class AutomationDezog_test : public ::testing::Test
{
protected:
    static uint16_t freePort()
    {
        initializeSockets();
        SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        socklen_t len = sizeof(addr);
        getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len);
        uint16_t port = ntohs(addr.sin_port);
        closeSocket(s);
        return port;
    }

    static void setEnv(const char* name, const char* value)
    {
#ifdef _WIN32
        _putenv_s(name, value ? value : "");
#else
        if (value)
            setenv(name, value, 1);
        else
            unsetenv(name);
#endif
    }
};

TEST_F(AutomationDezog_test, ResolvePortDefaults)
{
    setEnv(AutomationDezog::PORT_ENV_VAR, nullptr);
    EXPECT_EQ(AutomationDezog::resolvePort(0), dzrp::DEFAULT_PORT);
    EXPECT_EQ(AutomationDezog::resolvePort(4242), 4242);
}

TEST_F(AutomationDezog_test, ResolvePortFromEnvironment)
{
    setEnv(AutomationDezog::PORT_ENV_VAR, "13001");
    EXPECT_EQ(AutomationDezog::resolvePort(0), 13001);
    EXPECT_EQ(AutomationDezog::resolvePort(5), 5);  // explicit wins

    setEnv(AutomationDezog::PORT_ENV_VAR, "garbage");
    EXPECT_EQ(AutomationDezog::resolvePort(0), dzrp::DEFAULT_PORT);

    setEnv(AutomationDezog::PORT_ENV_VAR, "70000");
    EXPECT_EQ(AutomationDezog::resolvePort(0), dzrp::DEFAULT_PORT);

    setEnv(AutomationDezog::PORT_ENV_VAR, nullptr);
}

TEST_F(AutomationDezog_test, StartStopLifecycle)
{
    AutomationDezog module;
    EXPECT_FALSE(module.isRunning());
    EXPECT_EQ(module.getPort(), 0);

    uint16_t port = freePort();
    ASSERT_TRUE(module.start(port));
    EXPECT_TRUE(module.isRunning());
    EXPECT_EQ(module.getPort(), port);
    EXPECT_NE(module.getAdapter(), nullptr);

    // Idempotent start
    EXPECT_TRUE(module.start(port));

    // A client can connect and handshake
    TestDzrpClient client;
    ASSERT_TRUE(client.connect(port));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::vector<uint8_t> payload = {2, 0, 0, 'D', 0};
    auto resp = client.command(dzrp::CommandId::CMD_INIT, payload);
    EXPECT_TRUE(resp.valid);
    client.disconnect();

    module.stop();
    EXPECT_FALSE(module.isRunning());
    EXPECT_EQ(module.getAdapter(), nullptr);

    // Idempotent stop
    module.stop();
    EXPECT_FALSE(module.isRunning());
}

TEST_F(AutomationDezog_test, StartFailsWhenPortBusy)
{
    uint16_t port = freePort();
    AutomationDezog first;
    ASSERT_TRUE(first.start(port));

    AutomationDezog second;
    EXPECT_FALSE(second.start(port));
    EXPECT_FALSE(second.isRunning());

    first.stop();
}

/// endregion </AutomationDezog module>
