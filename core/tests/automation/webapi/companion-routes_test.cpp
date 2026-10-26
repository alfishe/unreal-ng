/// @file companion-routes_test.cpp
/// @brief Every WebAPI route that changes the machine has its companion scope (docs/inprogress/2026-10-16-companion-client):
/// a companion may do anything the user may, within the scopes granted at pairing. Routes no user-action scope names
/// fall to `debug`; this test lists them, so a new changing route fails here until someone decides whether it is debugger
/// work or belongs to another scope. The handlers are drogon code that core-tests do not link, so the routes are read
/// from emulator_api.h.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <string>

#include "_helpers/testpathhelper.h"
#include "emulator/execution/automationgate.h"

namespace
{
std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/// "{verb}" -> a sample value, so that a route whose scope depends on it is classified by a typical request
std::string Concrete(std::string path)
{
    if (path.rfind("/feature/", 0) == 0)
        return "/feature/turbomode";  // a feature a companion may switch; the others are checked by name in AutomationGate_Test
    static const std::map<std::string, std::string> samples = {
        {"{slot}", "disk0"}, {"{verb}", "insert"}, {"{drive}", "A"},   {"{name}", "speed"}, {"{cyl}", "0"},
        {"{side}", "0"},     {"{sec}", "1"},       {"{type}", "ram"},  {"{page}", "0"},     {"{offset}", "0"},
        {"{address}", "0"},  {"{source}", "master"}, {"{bus}", "isa"}, {"{label}", "x"},    {"{index}", "0"},
        {"{chip}", "0"},     {"{reg}", "0"},       {"{part}", "0"},    {"{group}", "g"},    {"{id2}", "0"}};
    for (const auto& [placeholder, value] : samples)
        for (size_t at = path.find(placeholder); at != std::string::npos; at = path.find(placeholder))
            path.replace(at, placeholder.size(), value);
    return path;
}

/// Changing routes in the `debug` scope: memory, registers, ports, breakpoints, steps, runs, TTD analysis, BASIC, the
/// assembler, chip pokes
const std::set<std::string> kDebugRoutes = {
    "DELETE /breakpoints",
    "DELETE /breakpoints/{bp_id}",
    "DELETE /ttd/bookmarks/{label}",
    "POST /analysis/build",
    "POST /analysis/cancel",
    "POST /analysis/export",
    "POST /analysis/start",
    "POST /analysis/stop",
    "POST /asm/sync/{action}",
    "POST /asm/{verb}",
    "POST /assemble",
    "POST /ay/log",
    "POST /basic/clear",
    "POST /basic/inject",
    "POST /basic/mode",
    "POST /basic/run",
    "POST /breakpoints",
    "POST /breakpoints/hits/reset",
    "POST /breakpoints/remove",
    "POST /control/audio/gs",
    "POST /control/audio/gs/porttrace",
    "POST /control/audio/midi",
    "POST /control/isa",
    "POST /listing/load",
    "POST /listing/run_to_line",
    "POST /listing/step_line",
    "POST /memory/page/{type}/{page}",
    "POST /memory/region/{name}",
    "POST /memory/rom/flash",
    "POST /memory/write",
    "POST /network/frame",
    "POST /next/magic-port",
    "POST /next/nextreg",
    "POST /ports/out",
    "POST /rtc/cells",
    "POST /run_frame",
    "POST /run_frames",
    "POST /run_scanlines",
    "POST /run_to_interrupt",
    "POST /run_to_pixel",
    "POST /run_to_scanline",
    "POST /run_tstates",
    "POST /skip_until",
    "POST /step",
    "POST /stepout",
    "POST /stepover",
    "POST /steps",
    "POST /tape/import",
    "POST /tape/render",
    "POST /ttd/bookmarks",
    "POST /ttd/dump",
    "POST /ttd/find-last",
    "POST /ttd/history-limit",
    "POST /ttd/journal",
    "POST /ttd/journal/build",
    "POST /ttd/journal/build/cancel",
    "POST /ttd/load",
    "POST /ttd/memory-at",
    "POST /ttd/memory-diff",
    "POST /ttd/port-events",
    "POST /ttd/reverse-continue",
    "POST /ttd/reverse-step",
    "POST /ttd/step-instruction",
    "POST /video/window/record/frames",
    "PUT /breakpoints/active",
    "PUT /breakpoints/{bp_id}/disable",
    "PUT /breakpoints/{bp_id}/enable",
    "PUT /breakpoints/{bp_id}/group",
    "PUT /breakpoints/{bp_id}/note",
    "PUT /debugmode",
    "PUT /disk/{drive}/sector/{cyl}/{side}/{sec}",
    "PUT /memory/{addr}",
    "PUT /memory/{type}/{page}/{offset}",
    "PUT /registers/{name}",
};
}  // namespace

TEST(CompanionRoutes_Test, EveryChangingRouteHasAScopeAndTheDebugOnesAreListed)
{
    const std::filesystem::path header =
        TestPathHelper::FindProjectRoot() / "core" / "automation" / "webapi" / "src" / "emulator_api.h";
    const std::string text = ReadFile(header);
    ASSERT_FALSE(text.empty()) << header;
    const std::regex route(R"re(ADD_METHOD_TO\(EmulatorAPI::\w+,\s*"/api/v1/emulator/\{id\}(/[^"]*)",\s*([^)]*)\))re");
    const std::regex method(R"(drogon::(Post|Put|Delete))");
    std::set<std::string> unclassified;
    std::set<std::string> seen;
    size_t changing = 0;
    for (std::sregex_iterator it(text.begin(), text.end(), route), end; it != end; ++it)
    {
        const std::string pattern = (*it)[1];
        const std::string methods = (*it)[2];
        for (std::sregex_iterator m(methods.begin(), methods.end(), method), mend; m != mend; ++m)
        {
            std::string verb = (*m)[1];
            for (char& c : verb)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            const std::string subpath = Concrete(pattern);
            if (!automation_gate::ChangesMachine(verb, subpath))
                continue;
            ++changing;
            const std::string key = verb + " " + pattern;
            seen.insert(key);
            const uint32_t scope = automation_gate::CompanionScopeOf(verb, subpath);
            EXPECT_NE(scope, 0u) << key << ": every changing route has a scope";
            if ((scope == automation_gate::ScopeDebug) != (kDebugRoutes.count(key) != 0))
                unclassified.insert(key + (scope == automation_gate::ScopeDebug ? " (debug, not listed)"
                                                                                : " (listed, but " +
                                                                                      automation_gate::CompanionScopeName(scope) + ")"));
        }
    }
    EXPECT_GT(changing, 100u) << "the routes were not found in " << header;
    std::string list;
    for (const std::string& key : unclassified)
        list += "    \"" + key + "\",\n";
    EXPECT_TRUE(unclassified.empty()) << "the debug scope and this list disagree (automationgate.cpp / this file):\n"
                                      << list;
    std::string stale;
    for (const std::string& key : kDebugRoutes)
        if (!seen.count(key))
            stale += "    " + key + "\n";
    EXPECT_TRUE(stale.empty()) << "listed routes that no longer exist or no longer change the machine:\n" << stale;
}
