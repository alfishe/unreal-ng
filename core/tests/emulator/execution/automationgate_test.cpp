// The automation gate's classification (automationgate.h): which requests and commands change the machine and so
// open a session implicitly for a client without one

#include "pch.h"

#include <string>
#include <vector>

#include "emulator/execution/automationgate.h"

using automation_gate::ChangesMachine;
using automation_gate::CliCommandChangesMachine;

TEST(AutomationGate_Test, ReadsAndObserversDoNotChangeTheMachine)
{
    EXPECT_FALSE(ChangesMachine("GET", "/registers"));
    EXPECT_FALSE(ChangesMachine("GET", "/memory/0x8000"));
    EXPECT_FALSE(ChangesMachine("POST", "/pause"));
    EXPECT_FALSE(ChangesMachine("POST", "/control/session"));
    EXPECT_FALSE(ChangesMachine("DELETE", "/control/session"));
    EXPECT_FALSE(ChangesMachine("POST", "/profiler/opcode/start"));
    EXPECT_FALSE(ChangesMachine("POST", "/coverage/start"));
    EXPECT_FALSE(ChangesMachine("PUT", "/audio/mixer/master"));
    EXPECT_FALSE(ChangesMachine("POST", "/symbols/import"));
    EXPECT_FALSE(ChangesMachine("POST", "/labels"));
    EXPECT_FALSE(ChangesMachine("POST", "/snapshot/save"));
    EXPECT_FALSE(ChangesMachine("POST", "/ttd/start"));
    EXPECT_FALSE(ChangesMachine("POST", "/next/reg-journal"));
    EXPECT_FALSE(ChangesMachine("POST", "/sprinter/pld-journal"));
    EXPECT_FALSE(ChangesMachine("DELETE", ""));                       // removing the instance
}

TEST(AutomationGate_Test, RunsWritesMediaInputAndBreakpointsChangeIt)
{
    EXPECT_TRUE(ChangesMachine("POST", "/resume"));
    EXPECT_TRUE(ChangesMachine("POST", "/steps"));
    EXPECT_TRUE(ChangesMachine("POST", "/run_frames"));
    EXPECT_TRUE(ChangesMachine("POST", "/memory/write"));
    EXPECT_TRUE(ChangesMachine("PUT", "/registers/pc"));
    EXPECT_TRUE(ChangesMachine("POST", "/snapshot/load"));
    EXPECT_TRUE(ChangesMachine("POST", "/tape/load"));
    EXPECT_TRUE(ChangesMachine("POST", "/keyboard/tap"));
    EXPECT_TRUE(ChangesMachine("POST", "/reset"));
    EXPECT_TRUE(ChangesMachine("POST", "/breakpoints"));
    EXPECT_TRUE(ChangesMachine("POST", "/ttd/seek"));
    EXPECT_TRUE(ChangesMachine("POST", "/next/nextreg"));
    EXPECT_TRUE(ChangesMachine("POST", "/resume?x=1"));
    // A prefix is a whole segment: /profilers is not /profiler
    EXPECT_TRUE(ChangesMachine("POST", "/profilers"));
}

TEST(AutomationGate_Test, CliCommandsByTheirChangingForms)
{
    EXPECT_TRUE(CliCommandChangesMachine("run_frames", {"10"}));
    EXPECT_TRUE(CliCommandChangesMachine("step", {}));
    EXPECT_TRUE(CliCommandChangesMachine("bp", {"0x8000"}));
    EXPECT_TRUE(CliCommandChangesMachine("memory", {"write", "0x8000", "00"}));
    EXPECT_FALSE(CliCommandChangesMachine("memory", {"read", "0x8000"}));
    EXPECT_TRUE(CliCommandChangesMachine("tape", {"load", "x.tap"}));
    EXPECT_FALSE(CliCommandChangesMachine("tape", {"info"}));
    EXPECT_TRUE(CliCommandChangesMachine("ttd", {"seek", "100"}));
    EXPECT_FALSE(CliCommandChangesMachine("ttd", {"status"}));
    EXPECT_TRUE(CliCommandChangesMachine("r", {"pc", "8000"}));
    EXPECT_FALSE(CliCommandChangesMachine("r", {}));
    EXPECT_FALSE(CliCommandChangesMachine("pause", {}));
    EXPECT_FALSE(CliCommandChangesMachine("session", {"take"}));
    EXPECT_FALSE(CliCommandChangesMachine("status", {}));
    EXPECT_FALSE(CliCommandChangesMachine("rzx", {"status"}));
    EXPECT_TRUE(CliCommandChangesMachine("rzx", {"seek", "5"}));
}

TEST(AutomationGate_Test, TheClientNameIsTheUserAgentsFirstProduct)
{
    EXPECT_EQ(automation_gate::ClientFromUserAgent("Python-urllib/3.12"), "Python-urllib/3.12");
    EXPECT_EQ(automation_gate::ClientFromUserAgent("curl/8.7.1"), "curl/8.7.1");
    EXPECT_EQ(automation_gate::ClientFromUserAgent("Mozilla/5.0 (Macintosh)"), "Mozilla/5.0");
    EXPECT_EQ(automation_gate::ClientFromUserAgent(""), "HTTP client");
}
