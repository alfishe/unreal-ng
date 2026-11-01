// childprocess::Child (common/childprocess.h): the ready pipe a started child writes to once it serves (the Stream
// Deck daemon's --ready-fd). A shell stands in for the daemon; Windows has none, its path is the same CreatePipe /
// PeekNamedPipe pair checked live with the daemon.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "_helpers/testwaithelper.h"
#include "common/childprocess.h"

#ifndef _WIN32

namespace
{
/// `sh -c SCRIPT --ready-fd 3`: the flag and the descriptor come as $0 and $1
std::unique_ptr<childprocess::Child> Shell(const std::string& script, bool ready)
{
    childprocess::Spec spec;
    spec.path = "/bin/sh";
    spec.args = {"-c", script};
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
    auto child = Shell(R"(test "$0" = --ready-fd && printf 'ready\n' >&"$1")", true);
    ASSERT_NE(child, nullptr);
    EXPECT_TRUE(TestWait::For([&] { return child->Ready(); })) << "the line written on descriptor $1 is seen";
    EXPECT_TRUE(child->Ready()) << "and stays seen";
    EXPECT_TRUE(TestWait::For([&] { return Ended(*child); }));
}

TEST(ChildProcess_Test, AChildThatEndsWithoutAWordIsNeverReady)
{
    auto child = Shell("exit 0", true);
    ASSERT_NE(child, nullptr);
    ASSERT_TRUE(TestWait::For([&] { return Ended(*child); }));
    EXPECT_FALSE(child->Ready()) << "the pipe closed empty";

    auto plain = Shell("exit 0", false);
    ASSERT_NE(plain, nullptr);
    ASSERT_TRUE(TestWait::For([&] { return Ended(*plain); }));
    EXPECT_FALSE(plain->Ready()) << "no flag: no pipe";
}

#endif
