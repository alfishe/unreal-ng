// Action execution against a fake host: order, delays, nesting, failure, cancel, toggles, substitution.
#include "sdb/actions.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "check.h"

using namespace sdb;
using namespace sdb::scn;

namespace
{
struct Fake final : ActionHost
{
    std::vector<std::string> log;
    int failStatus = 0;  // webapi answers this status when non-zero
    bool cancelAfterFirst = false;
    HttpResponse Webapi(const std::string& m, const std::string& p, const json& b, bool control) override
    {
        log.push_back("webapi " + m + " " + p + (b.is_null() ? "" : " " + b.dump()) + (control ? " ctl" : ""));
        HttpResponse r;
        r.status = failStatus ? failStatus : 200;
        if (failStatus)
            r.body = R"({"message":"not now"})";
        return r;
    }
    HttpResponse Http(const std::string& u, const std::string& m, const json&, const std::string& body) override
    {
        log.push_back("http " + m + " " + u + (body.empty() ? "" : " " + body));
        HttpResponse r;
        r.status = 204;
        return r;
    }
    int Shell(const std::string& c, int, std::string& out) override
    {
        log.push_back("shell " + c);
        out = "done\n";
        return c == "false" ? 1 : 0;
    }
    bool Open(const std::string& t) override
    {
        log.push_back("open " + t);
        return true;
    }
    bool Keys(const std::string& c, const std::string& t, std::string&) override
    {
        log.push_back("keys " + c + t);
        return true;
    }
    void Navigate(const json& r) override
    {
        log.push_back("nav " + r.value("to", "") + r.value("node", ""));
    }
    void Brightness(const json& r) override
    {
        log.push_back("brightness " + r.dump());
    }
    void SleepMs(int ms) override
    {
        log.push_back("sleep " + std::to_string(ms));
    }
    void Note(const std::string& l) override
    {
        log.push_back("note " + l);
    }
    bool Cancelled() const override
    {
        return cancelAfterFirst && log.size() >= 1;
    }
    HttpResponse TtdSeekBy(long long frames) override
    {
        log.push_back("seek " + std::to_string(frames));
        HttpResponse r;
        r.status = 200;
        r.body = json{{"arrived_at", {{"frame", 1 + 100 + frames}}}, {"session_start", 1}}.dump();
        return r;
    }
};
Action Act(const json& j)
{
    return {j.value("kind", ""), j};
}
}  // namespace

void TestActions()
{
    std::printf("actions\n");
    CHECK(Substitute(json{{"delta", "$ticks"}, {"s", "x=$ticks"}}, {{"ticks", -3}}) ==
          (json{{"delta", -3}, {"s", "x=-3"}}));
    Fake h;
    auto r = Execute(Act({{"kind", "webapi"}, {"path", "/pause"}}), h);
    CHECK(r.ok && h.log.back() == "webapi POST /pause ctl");
    CHECK(r.message.find("-> 200") != std::string::npos);
    r = Execute(Act({{"kind", "webapi"}, {"method", "GET"}, {"path", "/state"}}), h);
    CHECK(h.log.back() == "webapi GET /state");  // GET: no run-control session
    r = Execute(Act({{"kind", "webapi"}, {"path", "/ttd/seek"}, {"body", {{"delta", "$ticks"}}}}), h, {{"ticks", 5}});
    CHECK(h.log.back() == R"(webapi POST /ttd/seek {"delta":5} ctl)");
    h.failStatus = 409;
    r = Execute(Act({{"kind", "webapi"}, {"path", "/step"}}), h);
    CHECK(!r.ok && r.message.find("409") != std::string::npos && r.message.find("not now") != std::string::npos);
    h.failStatus = 0;

    // multi: order, delays, nesting; stops at the first failure
    h.log.clear();
    const json multi = json::parse(R"({"kind": "multi", "steps": [
        {"kind": "http", "url": "http://h/a"},
        {"kind": "delay", "ms": 50},
        {"kind": "multi", "steps": [{"kind": "shell", "command": "echo hi"}, {"kind": "open", "target": "https://x"}]},
        {"kind": "nav", "to": "home"}]})");
    r = Execute(Act(multi), h);
    CHECK(r.ok);
    CHECK((h.log ==
           std::vector<std::string>{"http GET http://h/a", "sleep 50", "shell echo hi", "open https://x", "nav home"}));
    h.log.clear();
    r = Execute(
        Act(json::parse(
            R"({"kind": "multi", "steps": [{"kind": "shell", "command": "false"}, {"kind": "keys", "combo": "cmd+s"}]})")),
        h);
    CHECK(!r.ok && r.message.find("step 1/2") != std::string::npos && h.log.size() == 1);
    // dir: the side by the sign, once per detent; first: the first that works
    h.log.clear();
    const json dir = json::parse(
        R"({"kind": "dir", "left": {"kind": "webapi", "path": "/ttd/step-instruction", "body": {"dir": "back"}},
                                     "right": {"kind": "webapi", "path": "/step"}})");
    r = Execute(Act(dir), h, {{"ticks", -2}});
    CHECK(r.ok && h.log.size() == 2 && h.log[0].find("step-instruction") != std::string::npos);
    h.log.clear();
    r = Execute(Act(dir), h, {{"ticks", 3}});
    CHECK(r.ok && (h.log == std::vector<std::string>(3, "webapi POST /step ctl")));
    bool onState = false;
    h.log.clear();  // one side only: the other does nothing (was a crash)
    r = Execute(Act(json::parse(R"({"kind": "dir", "right": {"kind": "webapi", "path": "/step"}})")), h,
                {{"ticks", -1}});
    CHECK(r.ok && h.log.empty());
    r = Execute(Act(json::parse(R"({"kind": "toggle", "on": {"kind": "webapi", "path": "/x"}})")), h, json::object(),
                &(onState = true));
    CHECK(!r.ok && h.log.empty());
    h.log.clear();
    r = Execute(
        Act(json::parse(
            R"({"kind": "first", "try": [{"kind": "shell", "command": "false"}, {"kind": "open", "target": "x"}]})")),
        h);
    CHECK(r.ok && (h.log == std::vector<std::string>{"shell false", "open x"}));
    // run_toggle: reads the machine's state, then the other one (the fake answers {} = running -> pause)
    h.log.clear();
    r = Execute(Act({{"kind", "run_toggle"}}), h);
    CHECK(r.ok && (h.log == std::vector<std::string>{"webapi GET ", "webapi POST /pause ctl"}));
    // ttd_seek: min a detent when slow, max when fast; frames or a time code
    h.log.clear();
    r = Execute(Act({{"kind", "ttd_seek"}, {"min", 1}, {"max", 100}}), h, {{"ticks", -3}, {"rate", 1.0}});
    CHECK(r.ok && h.log.back() == "seek -3" && r.status == "FRAME 0000098  ");
    r = Execute(Act({{"kind", "ttd_seek"}, {"min", 1}, {"max", 100}}), h, {{"ticks", 2}, {"rate", 60.0}});
    CHECK(h.log.back() == "seek 200");
    r = Execute(Act({{"kind", "ttd_seek"}, {"min", 10}, {"max", 1000}}), h, {{"ticks", 1}, {"rate", 22.0}});
    CHECK(h.log.back() == "seek 100");  // halfway in speed = the geometric middle
    r = Execute(Act({{"kind", "ttd_seek"}, {"unit", "seconds"}, {"min", 1}, {"max", 10}}), h,
                {{"ticks", 2}, {"rate", 0.0}});
    CHECK(h.log.back() == "seek 100" && r.status == "TTD 00:04.00  ");  // 2 s at 50 fps; 200 frames from the start
    // cancel between steps
    Fake c;
    c.cancelAfterFirst = true;
    r = Execute(Act(json::parse(
                    R"({"kind": "multi", "steps": [{"kind": "keys", "text": "a"}, {"kind": "keys", "text": "b"}]})")),
                c);
    CHECK(!r.ok && c.log.size() == 1 && r.message.find("cancel") != std::string::npos);

    // toggle flips its state only on success
    h.log.clear();
    bool state = false;
    const json tog = json::parse(
        R"({"kind": "toggle", "on": {"kind": "webapi", "path": "/tape/play"}, "off": {"kind": "webapi", "path": "/tape/stop"}})");
    r = Execute(Act(tog), h, json::object(), &state);
    CHECK(r.ok && state && h.log.back() == "webapi POST /tape/play ctl");
    r = Execute(Act(tog), h, json::object(), &state);
    CHECK(r.ok && !state && h.log.back() == "webapi POST /tape/stop ctl");
    h.failStatus = 500;
    r = Execute(Act(tog), h, json::object(), &state);
    CHECK(!r.ok && !state);
    h.failStatus = 0;

    // the rest
    r = Execute(Act({{"kind", "brightness"}, {"delta", 10}}), h);
    CHECK(r.ok && h.log.back() == R"(brightness {"delta":10,"kind":"brightness"})");
    r = Execute(Act({{"kind", "nav"}, {"to", "node"}, {"node", "debug"}}), h);
    CHECK(h.log.back() == "nav nodedebug");
    CHECK(!Execute(Act({{"kind", "warp"}}), h).ok);
    json deep = {{"kind", "delay"}, {"ms", 1}};
    for (int i = 0; i < kMaxMultiDepth + 2; ++i)
        deep = {{"kind", "multi"}, {"steps", json::array({deep})}};
    CHECK(!Execute(Act(deep), h).ok);

    // the system host: shell with output and exit code, timeout
    SystemHostCallbacks cb;
    std::vector<std::string> navs;
    cb.navigate = [&](const json& j) { navs.push_back(j.value("to", "")); };
    auto sys = MakeSystemHost(Settings{}, cb);
#ifdef _WIN32  // cmd.exe there
    const char* exit3 = "echo hello& exit 3";
    const char* sleep5 = "ping -n 6 127.0.0.1 > nul";
#else
    const char* exit3 = "echo hello; exit 3";
    const char* sleep5 = "sleep 5";
#endif
    r = Execute(Act({{"kind", "shell"}, {"command", exit3}}), *sys);
    CHECK(!r.ok && r.message == "shell exit 3: hello");
    r = Execute(Act({{"kind", "shell"}, {"command", sleep5}, {"timeout_ms", 200}}), *sys);
    CHECK(!r.ok && r.message.find("timeout") != std::string::npos);
    Execute(Act({{"kind", "nav"}, {"to", "back"}}), *sys);
    CHECK(navs.size() == 1 && navs[0] == "back");

    // every kind of the table is one Execute runs and Parse knows
    for (const std::string_view k : ActionKinds())
    {
        Fake any;
        const std::string kind(k);
        const std::string unknown = "unknown action kind '" + kind + "'";  // (an empty toggle side is another one)
        CHECK(Execute(Act({{"kind", kind}}), any).message.find(unknown) == std::string::npos);
        std::vector<Issue> issues;
        (void)Parse(
            json{{"model", "plusxl"},
                 {"root",
                  {{"id", "home"},
                   {"type", "menu"},
                   {"items",
                    json::array({json{{"id", "a"}, {"type", "action"}, {"at", {0, 0}}, {"do", {{"kind", kind}}}}})}}}},
            issues);
        for (const auto& i : issues)
            CHECK(i.message.find(unknown) == std::string::npos);
    }
    CHECK(ActionKinds().size() == 14);

    // keys combos: the same names on every platform
    auto combo = ParseKeyCombo("Cmd+Shift+4");
    CHECK(combo && combo->cmd && combo->shift && !combo->ctrl && combo->key == "4");
    combo = ParseKeyCombo("ctrl+alt+forwarddelete");
    CHECK(combo && combo->ctrl && combo->alt && combo->key == "forwarddelete");
    combo = ParseKeyCombo("option+command+fn+F12");
    CHECK(combo && combo->alt && combo->cmd && combo->fn && combo->key == "f12");
    combo = ParseKeyCombo("return");
    CHECK(combo && combo->key == "return" && !combo->cmd);
    std::string why;
    CHECK(!ParseKeyCombo("cmd+banana", &why) && why.find("unknown key 'banana'") != std::string::npos);
    CHECK(!ParseKeyCombo("cmd+shift", &why) && why.find("no key") != std::string::npos);
    CHECK(!ParseKeyCombo("a+b", &why) && why.find("two keys") != std::string::npos);
    CHECK(!ParseKeyCombo("cmd++", &why) && why.find("unknown key '+'") != std::string::npos);
    CHECK(KeyNames().size() == 76);
    for (const std::string_view k : KeyNames())
        CHECK(ParseKeyCombo("ctrl+" + std::string(k)).has_value());
    // a bad combo is an issue in the scenario and a clear failure on the system host (nothing is typed)
    std::vector<Issue> keyIssues;
    CHECK(!Parse(json{{"model", "plusxl"},
                      {"root",
                       {{"id", "home"},
                        {"type", "menu"},
                        {"items", json::array({json{{"id", "k"},
                                                    {"type", "action"},
                                                    {"at", {0, 0}},
                                                    {"do", {{"kind", "keys"}, {"combo", "cmd+banana"}}}}})}}}},
                 keyIssues)
               .has_value());
    CHECK(std::ranges::any_of(keyIssues, [](const Issue& i) {
        return i.error && i.message.find("unknown key 'banana'") != std::string::npos;
    }));
    r = Execute(Act({{"kind", "keys"}, {"combo", "cmd+banana"}}), *sys);
    CHECK(!r.ok && r.message == "keys cmd+banana failed: unknown key 'banana' in 'cmd+banana'");
}
