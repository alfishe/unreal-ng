// zrcp::Server session tests: a scripted line client drives the server the
// way DeZog does (init sequence with 100 breakpoint disables, interruptable
// run via blank line, breakpoints with conditions/pass counts, watchpoints,
// history and the quit sequence).

#include "dezogzesaruxtestfixture.h"

#include "automation-zesarux.h"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

/// region <Handshake / basics>

TEST_F(DezogZesaruxFixture, VersionAndUnknownCommand)
{
    EXPECT_EQ(_client.command("get-version"), "12.1");
    const std::string about = _client.command("about");
    EXPECT_NE(about.find("Unreal-NG-Test"), std::string::npos);
    EXPECT_NE(about.find("12.1"), std::string::npos);
    EXPECT_EQ(_client.command("totally-made-up-command"), "Unknown command");

    // Session survives the unknown command
    EXPECT_EQ(_client.command("get-version"), "12.1");
}

TEST_F(DezogZesaruxFixture, FullInitSequenceIncluding100Disables)
{
    // The exact command stream DeZog sends on connect (order included)
    EXPECT_EQ(_client.command("close-all-menus"), "");
    EXPECT_TRUE(!_client.command("about").empty());
    EXPECT_EQ(_client.command("get-version"), "12.1");
    EXPECT_EQ(_client.command("set-debug-settings 0"), "");
    // Log-only answers: DeZog strips the "log> " line before its prompt check,
    // so the command answer itself is empty and the text lands in logs
    _client.logs.clear();
    EXPECT_EQ(_client.command("hard-reset-cpu"), "");
    ASSERT_EQ(_client.logs.size(), 1u);
    EXPECT_NE(_client.logs[0].find("hard-reset-cpu"), std::string::npos);
    EXPECT_EQ(_client.command("enter-cpu-step"), "");
    _client.logs.clear();
    EXPECT_EQ(_client.command("load /tmp/game.z80"), "");
    ASSERT_EQ(_client.logs.size(), 1u);
    EXPECT_NE(_client.logs[0].find("load request ignored"), std::string::npos);
    _client.logs.clear();
    EXPECT_EQ(_client.command("smartload /tmp/game.z80"), "");
    ASSERT_EQ(_client.logs.size(), 1u);
    EXPECT_NE(_client.logs[0].find("load request ignored"), std::string::npos);
    EXPECT_EQ(_client.command("get-current-machine"), "ZX Spectrum 128K");
    EXPECT_EQ(_client.command("clear-membreakpoints"), "");
    EXPECT_EQ(_client.command("enable-breakpoints"), "");
    for (int id = 1; id <= 100; ++id)
    {
        const std::string response = _client.command("disable-breakpoint " + std::to_string(id));
        ASSERT_EQ(response, "") << "disable-breakpoint " << id;
    }
    EXPECT_EQ(_client.command("cpu-code-coverage enabled no"), "");
    EXPECT_EQ(_client.command("cpu-code-coverage get"), "");
    EXPECT_EQ(_client.command("cpu-code-coverage enabled get"), "");
    EXPECT_EQ(_client.command("extended-stack enabled yes"), "");
    EXPECT_EQ(_client.command("cpu-history enabled yes"), "");
    EXPECT_EQ(_client.command("cpu-history set-max-size 8192"), "");
    EXPECT_EQ(_client.command("cpu-history clear"), "");
    EXPECT_EQ(_client.command("cpu-history started yes"), "");
    EXPECT_EQ(_client.command("cpu-history ignrephalt yes"), "");
    EXPECT_EQ(_client.command("cpu-history ignrepldxr yes"), "");
    EXPECT_EQ(_client.command("cpu-history is-enabled"), "1");
    EXPECT_EQ(_client.command("cpu-history is-started"), "1");
    EXPECT_EQ(_client.command("blank"), "Unknown command");
}

TEST_F(DezogZesaruxFixture, ExecutionCommandsRequireCpuStepMode)
{
    EXPECT_EQ(_client.command("cpu-step"), "Error. You must first enter cpu-step mode");
    EXPECT_EQ(_client.command("cpu-step-over"), "Error. You must first enter cpu-step mode");
    EXPECT_EQ(_client.command("run"), "Error. You must first enter cpu-step mode");

    EXPECT_EQ(_client.command("enter-cpu-step"), "");
    EXPECT_NE(_client.command("cpu-step"), "Error. You must first enter cpu-step mode");
}

/// endregion </Handshake / basics>

/// region <cpu-step / cpu-step-over>

TEST_F(DezogZesaruxFixture, CpuStepAdvancesPc)
{
    initSession();
    installProgram();

    EXPECT_EQ(_client.command("set-register PC=32768"), "");
    EXPECT_EQ(_client.command("cpu-step").substr(0, 7), "8001 2 ");

    // 3E 01 (LD A,n): step lands on 8003
    EXPECT_EQ(_client.command("cpu-step").substr(0, 7), "8003 2 ");
    EXPECT_EQ(_adapter->getRegisters().pc, 0x8003u);
}

TEST_F(DezogZesaruxFixture, CpuStepOverCallRunsUntilAfterIt)
{
    initSession();

    // 8000: CD 06 80 (CALL 8006h); 8003: 00 (NOP); 8006: C9 (RET)
    _adapter->writeMemory(0x8000, {0xCD, 0x06, 0x80});
    _adapter->writeMemory(0x8003, {0x00});
    _adapter->writeMemory(0x8006, {0xC9});
    _adapter->setRegister(dzrp::RegisterId::PC, 0x8000);

    // CALL: temporary breakpoint at 8003; output is one disassembly line
    // (no "Running until" banner, no "Breakpoint fired" echo)
    EXPECT_EQ(_client.command("cpu-step-over"), "8003 2 NOP");
    EXPECT_EQ(_adapter->getRegisters().pc, 0x8003u);
}

TEST_F(DezogZesaruxFixture, CpuStepOverNonCallIsSingleStep)
{
    initSession();
    installProgram();

    EXPECT_EQ(_client.command("set-register PC=32768"), "");
    // F3 (DI): plain single step to 8001
    EXPECT_EQ(_client.command("cpu-step-over").substr(0, 7), "8001 2 ");
    EXPECT_EQ(_adapter->getRegisters().pc, 0x8001u);
}

/// endregion </cpu-step / cpu-step-over>

/// region <run: breakpoints / conditions / pass counts>

TEST_F(DezogZesaruxFixture, RunStopsOnBreakpointAndEchoesCondition)
{
    initSession();
    installProgram();

    // DeZog's sendSetBreakpoint order
    EXPECT_EQ(_client.command("set-breakpointaction 1"), "");
    EXPECT_EQ(_client.command("set-breakpoint 1 PC=08006h"), "");
    EXPECT_EQ(_client.command("enable-breakpoint 1"), "");
    EXPECT_NE(_emulator->GetBreakpointManager()->GetBreakpointsCount(), 0u);

    _client.sendLine("run");
    EXPECT_EQ(_client.readLine(), "Running until a breakpoint, key press or data sent, menu opening or other event");
    EXPECT_EQ(_client.readLine(), "Breakpoint fired: PC=08006h");
    EXPECT_FALSE(_client.readUntilPrompt().empty());

    EXPECT_TRUE(_emulator->IsPaused());
    EXPECT_EQ(_adapter->getRegisters().pc, 0x8006u);

    // DeZog remove: disable + the "set-breakpoint <id> 0" removal idiom
    EXPECT_EQ(_client.command("disable-breakpoint 1"), "");
    EXPECT_EQ(_client.command("set-breakpoint 1 0"), "");
    EXPECT_EQ(_client.command("disable-breakpoints"), "");
    EXPECT_EQ(_emulator->GetBreakpointManager()->GetBreakpointsCount(), 0u);
}

TEST_F(DezogZesaruxFixture, RunBreakpointConditionFalseSilentlyResumes)
{
    initSession();
    installProgram();

    // The loop at 8001 loads A=1 at the END of that instruction and keeps
    // A=1 forever after, so with A preset to 1 "A<>1" never holds: the
    // breakpoint must auto-resume without stopping or printing anything
    EXPECT_EQ(_client.command("set-register A=1"), "");
    EXPECT_EQ(_client.command("set-breakpoint 2 PC=08001h and (A<>1)"), "");
    EXPECT_EQ(_client.command("enable-breakpoint 2"), "");

    _client.sendLine("run");
    EXPECT_EQ(_client.readLine(), "Running until a breakpoint, key press or data sent, menu opening or other event");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(_emulator->IsRunning());

    // Interrupt with DeZog's blank line; stop is MANUAL: no break message,
    // just the step output
    _client.sendBlank();
    const std::string rest = _client.readUntilPrompt();
    EXPECT_EQ(rest.find("Breakpoint fired"), std::string::npos);
    EXPECT_FALSE(rest.empty());
    EXPECT_TRUE(_emulator->IsPaused());
}

TEST_F(DezogZesaruxFixture, RunBreakpointConditionTrueStops)
{
    initSession();
    installProgram();

    EXPECT_EQ(_client.command("set-breakpoint 3 PC=08001h and (A=1)"), "");
    EXPECT_EQ(_client.command("enable-breakpoint 3"), "");

    _client.sendLine("run");
    EXPECT_EQ(_client.readLine(), "Running until a breakpoint, key press or data sent, menu opening or other event");
    EXPECT_EQ(_client.readLine(), "Breakpoint fired: PC=08001h and (A=1)");
    EXPECT_FALSE(_client.readUntilPrompt().empty());
    EXPECT_EQ(_adapter->getRegisters().pc, 0x8001u);
}

TEST_F(DezogZesaruxFixture, PassCountSkipsFirstHits)
{
    initSession();
    installProgram();

    EXPECT_EQ(_client.command("set-breakpoint 4 PC=08001h"), "");
    EXPECT_EQ(_client.command("set-breakpointpasscount 4 3"), "");
    EXPECT_EQ(_client.command("enable-breakpoint 4"), "");

    _client.sendLine("run");
    EXPECT_EQ(_client.readLine(), "Running until a breakpoint, key press or data sent, menu opening or other event");
    // Skip count 2, stop on the 3rd hit
    EXPECT_EQ(_client.readLine(), "Breakpoint fired: PC=08001h");
    EXPECT_FALSE(_client.readUntilPrompt().empty());
}

TEST_F(DezogZesaruxFixture, DisableBreakpointKeepsRunning)
{
    initSession();
    installProgram();

    EXPECT_EQ(_client.command("set-breakpoint 5 PC=08001h"), "");
    EXPECT_EQ(_client.command("set-breakpointpasscount 5 5"), "");
    EXPECT_EQ(_client.command("enable-breakpoint 5"), "");
    EXPECT_EQ(_client.command("disable-breakpoint 5"), "");
    EXPECT_EQ(_emulator->GetBreakpointManager()->GetBreakpointsCount(), 0u);

    _client.sendLine("run");
    EXPECT_EQ(_client.readLine(), "Running until a breakpoint, key press or data sent, menu opening or other event");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(_emulator->IsRunning());

    _client.sendBlank();
    EXPECT_FALSE(_client.readUntilPrompt().empty());
}

/// endregion </run: breakpoints / conditions / pass counts>

/// region <Watchpoints>

TEST_F(DezogZesaruxFixture, WatchpointFiresOnWrite)
{
    initSession();
    installProgram();

    // set-membreakpoint <hexaddr>h <type> <size>: 2 = write
    EXPECT_EQ(_client.command("set-membreakpoint 9000h 2 1"), "");
    EXPECT_EQ(_adapter->getWatchpointCount(), 1u);

    _client.sendLine("run");
    EXPECT_EQ(_client.readLine(), "Running until a breakpoint, key press or data sent, menu opening or other event");
    EXPECT_EQ(_client.readLine(), "Breakpoint fired: Memory Breakpoint Write Address: 9000H");
    EXPECT_FALSE(_client.readUntilPrompt().empty());
    EXPECT_TRUE(_emulator->IsPaused());

    // DeZog removes watchpoints by type 0 (clear)
    EXPECT_EQ(_client.command("set-membreakpoint 9000h 0 1"), "");
    EXPECT_EQ(_adapter->getWatchpointCount(), 0u);

    EXPECT_EQ(_client.command("clear-membreakpoints"), "");
}

/// endregion </Watchpoints>

/// region <memory dump>

TEST_F(DezogZesaruxFixture, ReadMemoryFullAddressSpace)
{
    initSession();
    installProgram();

    // DeZog's disassembly view fetches the whole address space in one request
    // (fetch64kMemory) and asserts the answer holds exactly len*2 hex chars -
    // a uint16_t length cast turns 65536 into 0 and crashed it with 'assert'.
    const std::string full = _client.command("read-memory 0 65536");
    ASSERT_EQ(full.size(), 2u * 65536u);
    EXPECT_EQ(full.find_first_not_of("0123456789ABCDEF"), std::string::npos);

    // Same bytes as the equivalent split reads
    const std::string lowHalf = _client.command("read-memory 0 32768");
    const std::string highHalf = _client.command("read-memory 32768 32768");
    EXPECT_EQ(full, lowHalf + highHalf);

    // One 64K space is all there is
    EXPECT_EQ(_client.command("read-memory 0 65537"), "Error. Invalid length");

    // Wrapped read across the top of the address space
    const std::string wrapped = _client.command("read-memory 65535 2");
    EXPECT_EQ(wrapped.substr(0, 2), full.substr(2u * 65535u, 2));
    EXPECT_EQ(wrapped.substr(2, 2), full.substr(0, 2));
}

/// endregion </memory dump>

/// region <history>

TEST_F(DezogZesaruxFixture, HistoryIndexesAndErrors)
{
    initSession();
    installProgram();

    // Record a few instructions
    for (int i = 0; i < 5; ++i)
        ASSERT_FALSE(_client.command("cpu-step").empty());

    const std::string entry = _client.command("cpu-history get 0");
    EXPECT_EQ(entry.substr(0, 3), "PC=");

    const std::string size = _client.command("cpu-history get-size");
    EXPECT_GT(std::stoul(size), 5u);

    EXPECT_EQ(_client.command("cpu-history get 4294967295"), "ERROR: index out of range");
    EXPECT_EQ(_client.command("cpu-history bogus"), "Error. Unknown parameter");

    // Browsing history does not disturb the session
    EXPECT_EQ(_client.command("get-version"), "12.1");
}

// §6 regression (the reported "Break: Reached end of instruction history"):
// DeZog spot-fetches history at every stop, and every forward command used
// to wipe the timeline via the browse/leave restart - reverse-continue could
// never walk past the latest stop. Browse cycles must be non-destructive:
// instructions stepped BEFORE a browse stay browsable after
// browse -> step -> browse cycles.
TEST_F(DezogZesaruxFixture, HistorySurvivesBrowseAndStepCycles)
{
    initSession();
    installProgram();

    // Enough instructions for a deep index (DeZog's reverse-continue walks
    // far back; 12 steps = 12 recorded instructions)
    for (int i = 0; i < 12; ++i)
        ASSERT_FALSE(_client.command("cpu-step").empty());

    const std::string deep = _client.command("cpu-history get 8");
    EXPECT_EQ(deep.substr(0, 3), "PC=");

    // The DeZog stop pattern repeated: spot-fetch -> forward step -> spot-fetch
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        EXPECT_EQ(_client.command("cpu-history get 0").substr(0, 3), "PC=");
        ASSERT_FALSE(_client.command("cpu-step").empty());
        EXPECT_EQ(_client.command("cpu-history get 0").substr(0, 3), "PC=");
    }

    // Pre-browse instructions are still browsable: with the old wipe only the
    // handful of post-wipe steps survived and index 10 was out of range.
    EXPECT_EQ(_client.command("cpu-history get 10").substr(0, 3), "PC=");
}

/// endregion </history>

/// region <Quit / session lifecycle>

TEST_F(DezogZesaruxFixture, QuitSequenceEndsSessionAndCleansUp)
{
    initSession();
    installProgram();
    EXPECT_EQ(_client.command("set-breakpoint 6 PC=08001h"), "");
    EXPECT_EQ(_client.command("enable-breakpoint 6"), "");
    EXPECT_NE(_emulator->GetBreakpointManager()->GetBreakpointsCount(), 0u);

    // The exact disconnect sequence DeZog sends
    EXPECT_EQ(_client.command(""), "");
    EXPECT_EQ(_client.command("cpu-history enabled no"), "");
    EXPECT_EQ(_client.command("cpu-code-coverage enabled no"), "");
    EXPECT_EQ(_client.command("extended-stack enabled no"), "");
    EXPECT_EQ(_client.command("clear-membreakpoints"), "");
    EXPECT_EQ(_client.command("disable-breakpoints"), "");
    EXPECT_EQ(_client.command("exit-cpu-step"), "");
    EXPECT_EQ(_client.command("quit"), "");

    EXPECT_TRUE(_client.socketClosed());
    _client.disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Session teardown removed the breakpoints and resumed the target
    EXPECT_EQ(_emulator->GetBreakpointManager()->GetBreakpointsCount(), 0u);
    EXPECT_FALSE(_emulator->IsPaused());

    // A fresh client can start a new session
    ASSERT_TRUE(_client.connect(_server->getPort()));
    const std::string banner = _client.readUntilPrompt();
    EXPECT_NE(banner.find("Welcome to Unreal-NG-Test"), std::string::npos);
    EXPECT_EQ(_client.command("get-version"), "12.1");
}

TEST_F(DezogZesaruxFixture, ClientDropWhileRunningCleansUp)
{
    initSession();
    installProgram();
    EXPECT_EQ(_client.command("set-breakpoint 7 PC=08001h"), "");
    EXPECT_EQ(_client.command("enable-breakpoint 7"), "");

    _client.sendLine("run");
    EXPECT_EQ(_client.readLine(), "Running until a breakpoint, key press or data sent, menu opening or other event");

    // VS Code window closed: socket just goes away
    _client.disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    EXPECT_EQ(_emulator->GetBreakpointManager()->GetBreakpointsCount(), 0u);

    ASSERT_TRUE(_client.connect(_server->getPort()));
    EXPECT_NE(_client.readUntilPrompt().find("Welcome to Unreal-NG-Test"), std::string::npos);
    EXPECT_EQ(_client.command("get-version"), "12.1");
}

/// endregion </Quit / session lifecycle>

/// region <AutomationZesarux module>

class AutomationZesarux_test : public ::testing::Test
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

TEST_F(AutomationZesarux_test, ResolvePortDefaults)
{
    setEnv(AutomationZesarux::PORT_ENV_VAR, nullptr);
    EXPECT_EQ(AutomationZesarux::resolvePort(0), zrcp::DEFAULT_PORT);
    EXPECT_EQ(AutomationZesarux::resolvePort(4343), 4343);
}

TEST_F(AutomationZesarux_test, ResolvePortFromEnvironment)
{
    setEnv(AutomationZesarux::PORT_ENV_VAR, "10001");
    EXPECT_EQ(AutomationZesarux::resolvePort(0), 10001);
    EXPECT_EQ(AutomationZesarux::resolvePort(5), 5);  // explicit wins

    setEnv(AutomationZesarux::PORT_ENV_VAR, "garbage");
    EXPECT_EQ(AutomationZesarux::resolvePort(0), zrcp::DEFAULT_PORT);

    setEnv(AutomationZesarux::PORT_ENV_VAR, "70000");
    EXPECT_EQ(AutomationZesarux::resolvePort(0), zrcp::DEFAULT_PORT);

    setEnv(AutomationZesarux::PORT_ENV_VAR, nullptr);
}

TEST_F(AutomationZesarux_test, StartStopLifecycle)
{
    AutomationZesarux module;
    EXPECT_FALSE(module.isRunning());
    EXPECT_EQ(module.getPort(), 0);

    uint16_t port = freePort();
    ASSERT_TRUE(module.start(port));
    EXPECT_TRUE(module.isRunning());
    EXPECT_EQ(module.getPort(), port);
    EXPECT_NE(module.getAdapter(), nullptr);

    // Idempotent start
    EXPECT_TRUE(module.start(port));

    // A client can connect, see the banner and quit
    TestDezogZesaruxClient client;
    ASSERT_TRUE(client.connect(port));
    EXPECT_NE(client.readUntilPrompt().find("Welcome"), std::string::npos);
    EXPECT_EQ(client.command("get-version"), zrcp::SERVER_VERSION);
    EXPECT_EQ(client.command("quit"), "");
    client.disconnect();

    module.stop();
    EXPECT_FALSE(module.isRunning());
    EXPECT_EQ(module.getAdapter(), nullptr);

    // Idempotent stop
    module.stop();
    EXPECT_FALSE(module.isRunning());
}

TEST_F(AutomationZesarux_test, StartFailsWhenPortBusy)
{
    uint16_t port = freePort();
    AutomationZesarux first;
    ASSERT_TRUE(first.start(port));

    AutomationZesarux second;
    EXPECT_FALSE(second.start(port));
    EXPECT_FALSE(second.isRunning());

    first.stop();
}

/// endregion </AutomationZesarux module>
