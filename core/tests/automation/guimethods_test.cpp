/// @file guimethods_test.cpp
/// @brief The GUI control plane's methods in the core's API (gui-control-plane design §4.10): GuiPlane hands a call to
/// the methods the program registered and passes the answer back; without them every call answers no_gui; the
/// waiting surfaces (CLI, Lua, Python) get the answer from another thread, or `timeout`; the CLI's words become the
/// call's target and parameters.

#include <gtest/gtest.h>

#include "../../automation/common/guicall.h"
#include "../../automation/common/guimethods.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>


namespace
{
/// Registers the methods for the test's scope, withdraws them after
class ScopedMethods
{
public:
    explicit ScopedMethods(std::shared_ptr<GuiMethods> methods) { GuiPlane::Set(std::move(methods)); }
    ~ScopedMethods() { GuiPlane::Set(nullptr); }
};

struct Received
{
    std::string method;
    std::string target;
    std::string params;
};

const StateNode* Member(const StateNode& node, const char* name)
{
    return node.find(name);
}
}  // namespace

TEST(GuiMethods_Test, WithoutMethodsEveryCallAnswersNoGui)
{
    GuiPlane::Set(nullptr);
    std::string answer;
    GuiPlane::Call("memory.goto", "", "{}", [&answer](const std::string& result) { answer = result; });
    const StateNode result = gui_call::Parse(answer);
    ASSERT_NE(Member(result, "ok"), nullptr);
    EXPECT_FALSE(Member(result, "ok")->b);
    EXPECT_EQ(Member(result, "error")->s, GuiPlane::kNoGui);
    EXPECT_TRUE(GuiPlane::Catalogue().empty());
    EXPECT_EQ(Member(gui_call::CallAndWait("list", "", StateNode::Object()), "error")->s, GuiPlane::kNoGui);
}

TEST(GuiMethods_Test, CallsReachTheMethodsAndTheAnswerComesBack)
{
    auto received = std::make_shared<Received>();
    auto methods = std::make_shared<GuiMethods>();
    methods->catalogueJson = [] { return std::string(R"({"format":"unreal-ng-gui-catalogue","interfaces":[]})"); };
    methods->call = [received](const std::string& method, const std::string& target, const std::string& params,
                               GuiMethods::Done done) {
        *received = {method, target, params};
        done(R"({"ok":true,"body":{"address":"0x6000"}})");
    };
    ScopedMethods scope(methods);

    EXPECT_EQ(GuiPlane::Catalogue(), R"({"format":"unreal-ng-gui-catalogue","interfaces":[]})");
    std::string answer;
    GuiPlane::Call("memory.goto", "debugger/memory", R"({"addr":"0x6000"})",
                   [&answer](const std::string& result) { answer = result; });
    EXPECT_EQ(received->method, "memory.goto");
    EXPECT_EQ(received->target, "debugger/memory");
    EXPECT_EQ(received->params, R"({"addr":"0x6000"})");
    EXPECT_EQ(answer, R"({"ok":true,"body":{"address":"0x6000"}})");

    // No parameters: an empty object, never ""
    GuiPlane::Call("window.raise", "", "", [](const std::string&) {});
    EXPECT_EQ(received->params, "{}");

    // The waiting form: the parameters as JSON, the answer as a StateNode
    StateNode params = StateNode::Object();
    params["factor"] = 2;
    const StateNode result = gui_call::CallAndWait("screen.zoom", "main/screen", params);
    EXPECT_EQ(received->params, R"({"factor":2})");
    EXPECT_TRUE(Member(result, "ok")->b);
    EXPECT_EQ(Member(*Member(result, "body"), "address")->s, "0x6000");
}

TEST(GuiMethods_Test, TheAnswerMayComeFromAnotherThread)
{
    std::atomic<bool> started{false};
    std::vector<std::thread> workers;
    auto methods = std::make_shared<GuiMethods>();
    methods->call = [&workers, &started](const std::string&, const std::string&, const std::string&,
                                         GuiMethods::Done done) {
        // The GUI thread answers later: the caller waits meanwhile
        workers.emplace_back([&started, done = std::move(done)]() {
            started = true;
            done(R"({"ok":false,"error":"not_now","message":"the debugger is closed"})");
        });
    };
    ScopedMethods scope(methods);

    const StateNode result = gui_call::CallAndWait("debugger.focus", "", StateNode::Object());
    for (std::thread& worker : workers)
        worker.join();
    EXPECT_TRUE(started);
    EXPECT_FALSE(Member(result, "ok")->b);
    EXPECT_EQ(Member(result, "error")->s, "not_now");
    EXPECT_EQ(Member(result, "message")->s, "the debugger is closed");
}

TEST(GuiMethods_Test, NoAnswerInTimeIsATimeoutAndALateOneIsDropped)
{
    auto late = std::make_shared<GuiMethods::Done>();
    auto methods = std::make_shared<GuiMethods>();
    methods->call = [late](const std::string&, const std::string&, const std::string&, GuiMethods::Done done) {
        *late = std::move(done);  // kept, not answered
    };
    ScopedMethods scope(methods);

    const StateNode result = gui_call::CallAndWait("window.raise", "main", StateNode::Object(), 20);
    EXPECT_FALSE(Member(result, "ok")->b);
    EXPECT_EQ(Member(result, "error")->s, "timeout");
    ASSERT_TRUE(*late);
    (*late)(R"({"ok":true})");  // the caller is gone: nothing to touch
}

TEST(GuiMethods_Test, AnAnswerThatIsNoResultIsFailed)
{
    const StateNode result = gui_call::Parse("not json");
    EXPECT_FALSE(Member(result, "ok")->b);
    EXPECT_EQ(Member(result, "error")->s, "failed");
    EXPECT_EQ(Member(gui_call::Parse("[1]"), "error")->s, "failed");
}

TEST(GuiMethods_Test, FailureIsJsonWhateverTheMessage)
{
    const StateNode result = gui_call::Parse(GuiPlane::Failure("bad_request", "a \"quoted\" \\ path\nand a tab\t"));
    EXPECT_FALSE(Member(result, "ok")->b);
    EXPECT_EQ(Member(result, "error")->s, "bad_request");
    EXPECT_EQ(Member(result, "message")->s, "a \"quoted\" \\ path\nand a tab\t");
}

TEST(GuiMethods_Test, WordsBecomeTheTargetAndTypedParameters)
{
    std::string target;
    StateNode params;
    std::string error;
    ASSERT_TRUE(gui_call::ParseWords({"addr=0x6000", "factor=2", "on=true", "pane=memory", "target=debugger/memory"},
                                     target, params, error));
    EXPECT_EQ(target, "debugger/memory");
    EXPECT_EQ(Member(params, "addr")->s, "0x6000");  // an address stays text: the plane parses it
    EXPECT_EQ(Member(params, "factor")->kind, StateNode::Kind::Int);
    EXPECT_EQ(Member(params, "factor")->i, 2);
    EXPECT_EQ(Member(params, "on")->kind, StateNode::Kind::Bool);
    EXPECT_TRUE(Member(params, "on")->b);
    EXPECT_EQ(Member(params, "pane")->s, "memory");
    EXPECT_EQ(Member(params, "target"), nullptr);

    EXPECT_FALSE(gui_call::ParseWords({"0x6000"}, target, params, error));
    EXPECT_NE(error.find("name=value"), std::string::npos);
    EXPECT_FALSE(gui_call::ParseWords({"=1"}, target, params, error));
}
