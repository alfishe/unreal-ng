// childprocess::Child (common/childprocess.h): the ready pipe a started child writes to once it serves (the Stream
// Deck daemon's --ready-fd). A shell stands in for the daemon: /bin/sh on POSIX (the descriptor), Windows PowerShell on
// Windows (the inherited handle's value, written through a SafeFileHandle).

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>

#include "_helpers/testwaithelper.h"
#include "common/childprocess.h"

namespace
{
#ifdef _WIN32
/// The words a ready child writes: the flag checked, then "ready" written to the handle the flag names
constexpr const char* kSayReady =
    "& { if ($args[0] -ne '--ready-fd') { exit 2 }; "
    "$h = New-Object Microsoft.Win32.SafeHandles.SafeFileHandle([IntPtr][long]$args[1], $true); "
    "$s = New-Object System.IO.FileStream($h, [System.IO.FileAccess]::Write); "
    "$s.Write([byte[]](114,101,97,100,121,10), 0, 6); $s.Close() }";
constexpr const char* kSayNothing = "& { exit 0 }";

/// `powershell -Command SCRIPT --ready-fd <handle>`: the words after -Command join the script, the scriptblock's $args
std::unique_ptr<childprocess::Child> Shell(const std::string& script, bool ready)
{
    childprocess::Spec spec;
    spec.path = "powershell.exe";  // found on PATH: CreateProcessW is given no application name
    spec.args = {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", script};
#else
constexpr const char* kSayReady = R"(test "$0" = --ready-fd && printf 'ready\n' >&"$1")";
constexpr const char* kSayNothing = "exit 0";

/// `sh -c SCRIPT --ready-fd 3`: the flag and the descriptor come as $0 and $1
std::unique_ptr<childprocess::Child> Shell(const std::string& script, bool ready)
{
    childprocess::Spec spec;
    spec.path = "/bin/sh";
    spec.args = {"-c", script};
#endif
    if (ready)
        spec.readyFlag = "--ready-fd";
    std::string error;
    auto child = childprocess::Child::Start(spec, error);
    EXPECT_NE(child, nullptr) << error;
    return child;
}

bool Ended(childprocess::Child& child)
{
    int code = 0;
    bool crashed = false;
    return child.Exited(code, crashed);
}
} // namespace

TEST(ChildProcess_Test, TheChildSaysItIsReadyThroughThePipeItsFlagNames)
{
    auto child = Shell(kSayReady, true);
    ASSERT_NE(child, nullptr);
    EXPECT_TRUE(TestWait::For([&] { return child->Ready(); }, std::chrono::seconds(10)))
        << "the line written on the descriptor / handle the flag names is seen";
    EXPECT_TRUE(child->Ready()) << "and stays seen";
    ASSERT_TRUE(TestWait::For([&] { return Ended(*child); }, std::chrono::seconds(10)));
    int code = -1;
    bool crashed = true;
    child->Exited(code, crashed);
    EXPECT_EQ(code, 0) << "the child found the flag before the descriptor";
}

TEST(ChildProcess_Test, AChildThatEndsWithoutAWordIsNeverReady)
{
    auto child = Shell(kSayNothing, true);
    ASSERT_NE(child, nullptr);
    ASSERT_TRUE(TestWait::For([&] { return Ended(*child); }, std::chrono::seconds(10)));
    EXPECT_FALSE(child->Ready()) << "the pipe closed empty";

    auto plain = Shell(kSayNothing, false);
    ASSERT_NE(plain, nullptr);
    ASSERT_TRUE(TestWait::For([&] { return Ended(*plain); }, std::chrono::seconds(10)));
    EXPECT_FALSE(plain->Ready()) << "no flag: no pipe";
}

