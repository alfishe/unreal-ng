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
    EXPECT_FALSE(ChangesMachine("PUT", "/video/window"));            // the GUI window's display, not the machine
    EXPECT_FALSE(ChangesMachine("POST", "/video/window"));
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

// Companion clients (docs/inprogress/2026-10-16-companion-client): the scope each user action needs

using namespace automation_gate;

TEST(AutomationGate_Test, CompanionScopesCoverTheUserActions)
{
    EXPECT_EQ(CompanionScopeOf("POST", "/resume"), uint32_t(ScopeRun));
    EXPECT_EQ(CompanionScopeOf("POST", "/reset"), uint32_t(ScopeRun));
    EXPECT_EQ(CompanionScopeOf("POST", "/nmi"), uint32_t(ScopeRun));
    EXPECT_EQ(CompanionScopeOf("PUT", "/settings/speed"), uint32_t(ScopeRun));
    EXPECT_EQ(CompanionScopeOf("POST", "/settings/turbo_mode"), uint32_t(ScopeRun));
    EXPECT_EQ(CompanionScopeOf("POST", "/feature/turbomode"), uint32_t(ScopeRun));
    EXPECT_EQ(CompanionScopeOf("POST", "/keyboard/tap"), uint32_t(ScopeInput));
    EXPECT_EQ(CompanionScopeOf("POST", "/keyboard/type"), uint32_t(ScopeInput));
    EXPECT_EQ(CompanionScopeOf("POST", "/joystick/press"), uint32_t(ScopeInput));
    EXPECT_EQ(CompanionScopeOf("POST", "/mouse/click"), uint32_t(ScopeInput));
    EXPECT_EQ(CompanionScopeOf("POST", "/controllers/kempston/press"), uint32_t(ScopeInput));
    EXPECT_EQ(CompanionScopeOf("POST", "/controllers/kempston/plug"), uint32_t(ScopeMachine));
    EXPECT_EQ(CompanionScopeOf("POST", "/tape/play"), uint32_t(ScopeMedia));
    EXPECT_EQ(CompanionScopeOf("POST", "/disk/A/insert"), uint32_t(ScopeMedia));
    EXPECT_EQ(CompanionScopeOf("POST", "/snapshot/load"), uint32_t(ScopeMedia));
    EXPECT_EQ(CompanionScopeOf("POST", "/media/disk0/insert"), uint32_t(ScopeMedia));
    EXPECT_EQ(CompanionScopeOf("POST", "/rzx/play"), uint32_t(ScopeMedia));
    EXPECT_EQ(CompanionScopeOf("POST", "/settings/fast_tape"), uint32_t(ScopeMedia));
    EXPECT_EQ(CompanionScopeOf("POST", "/feature/ftape"), uint32_t(ScopeMedia));
    EXPECT_EQ(CompanionScopeOf("POST", "/ttd/step-back"), uint32_t(ScopeRewind));
    EXPECT_EQ(CompanionScopeOf("POST", "/ttd/seek?x=1"), uint32_t(ScopeRewind));
    EXPECT_EQ(CompanionScopeOf("POST", "/model"), uint32_t(ScopeMachine));
    EXPECT_EQ(CompanionScopeOf("POST", "/slots/0/insert"), uint32_t(ScopeMachine));
    EXPECT_EQ(CompanionScopeOf("POST", "/control/take-back"), uint32_t(ScopeTakeBack));
    // The take-back routes change no machine: their handler checks the companion, every other client is refused there
    EXPECT_FALSE(ChangesMachine("POST", "/control/take-back"));
    EXPECT_FALSE(ChangesMachine("POST", "/control/allow-again"));
}

TEST(AutomationGate_Test, EveryOtherActionIsDebugOrConfiguration)
{
    for (const char* path : {"/memory/write", "/registers/pc", "/ports/out", "/breakpoints", "/step", "/steps",
                             "/run_frames", "/skip_until", "/basic/inject", "/memory/rom/flash", "/debugmode",
                             "/feature/debugmode", "/tape/render", "/disk/A/sector/0/0/1", "/ttd/load",
                             "/ttd/reverse-step", "/resume/now"})
        EXPECT_EQ(CompanionScopeOf("POST", path), uint32_t(ScopeDebug)) << path;
    EXPECT_EQ(CompanionScopeOf("DELETE", "/breakpoints/3"), uint32_t(ScopeDebug));
    for (const char* path : {"/settings/trdos_traps", "/feature/sound", "/start", "/stop", "/network/config",
                             "/snapshot/transfer", "/state/screen/overscan", "/memory/rom/protect", "/switches"})
        EXPECT_EQ(CompanionScopeOf("POST", path), uint32_t(ScopeMachine)) << path;
}

TEST(AutomationGate_Test, SteppingNeedsAPause)
{
    for (const char* path : {"/step", "/steps", "/stepover", "/stepout", "/skip_until", "/run_frames", "/run_tstates",
                             "/registers/pc", "/ttd/reverse-step", "/listing/step_line"})
        EXPECT_TRUE(CompanionNeedsPause(path)) << path;
    for (const char* path : {"/resume", "/memory/write", "/breakpoints", "/ttd/seek", "/stepping"})
        EXPECT_FALSE(CompanionNeedsPause(path)) << path;
    EXPECT_TRUE(CompanionCliNeedsPause("stepover", {}));
    EXPECT_TRUE(CompanionCliNeedsPause("r", {"pc", "8000"}));
    EXPECT_FALSE(CompanionCliNeedsPause("r", {}));
    EXPECT_TRUE(CompanionCliNeedsPause("ttd", {"rs"}));
    EXPECT_FALSE(CompanionCliNeedsPause("memory", {"write", "8000", "00"}));
}

TEST(AutomationGate_Test, CompanionCliScopesFollowTheCommandForms)
{
    EXPECT_EQ(CompanionCliScopeOf("resume", {}), uint32_t(ScopeRun));
    EXPECT_EQ(CompanionCliScopeOf("setting", {"speed", "2"}), uint32_t(ScopeRun));
    EXPECT_EQ(CompanionCliScopeOf("feature", {"turbo", "on"}), uint32_t(ScopeRun));
    EXPECT_EQ(CompanionCliScopeOf("controllers", {"tap", "kempston", "fire"}), uint32_t(ScopeInput));
    EXPECT_EQ(CompanionCliScopeOf("controllers", {"plug", "kempston", "kempston"}), uint32_t(ScopeMachine));
    EXPECT_EQ(CompanionCliScopeOf("setting", {"fast_tape", "on"}), uint32_t(ScopeMedia));
    EXPECT_EQ(CompanionCliScopeOf("setting", {"trdos_traps", "1"}), uint32_t(ScopeMachine));
    EXPECT_EQ(CompanionCliScopeOf("key", {"tap", "SPACE"}), uint32_t(ScopeInput));
    EXPECT_EQ(CompanionCliScopeOf("tape", {"play"}), uint32_t(ScopeMedia));
    EXPECT_EQ(CompanionCliScopeOf("disk", {"insert", "A", "x.trd"}), uint32_t(ScopeMedia));
    EXPECT_EQ(CompanionCliScopeOf("disk", {"write", "A"}), uint32_t(ScopeDebug));
    EXPECT_EQ(CompanionCliScopeOf("snapshot", {"load", "x.sna"}), uint32_t(ScopeMedia));
    EXPECT_EQ(CompanionCliScopeOf("ttd", {"sb"}), uint32_t(ScopeRewind));
    EXPECT_EQ(CompanionCliScopeOf("ttd", {"load", "x.ttd"}), uint32_t(ScopeDebug));
    EXPECT_EQ(CompanionCliScopeOf("model", {"PENTAGON"}), uint32_t(ScopeMachine));
    for (const char* command : {"memory", "r", "step", "run_frames", "bp", "out", "romflash", "assemble", "debugmode",
                                "basic"})
        EXPECT_EQ(CompanionCliScopeOf(command, {"write", "1"}), uint32_t(ScopeDebug)) << command;
    EXPECT_EQ(CompanionCliScopeOf("start", {}), uint32_t(ScopeMachine));
    EXPECT_EQ(CompanionCliScopeOf("network", {"set", "card=zxwifi"}), uint32_t(ScopeMachine));
}
