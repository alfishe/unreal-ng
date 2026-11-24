/// @file jobs-routes_test.cpp
/// @brief The jobs routes (PLAN #122 S2) in the WebAPI sources and the OpenAPI manifest: list, the instance's list,
/// start, the job with the long-poll, cancel (DELETE and POST); the long-poll holds no thread (a registry watch and
/// an event-loop timer, Unwatch on the timeout); the long verbs' routes pass `async` on; the WebSocket hub knows the
/// topics. The drogon code is not linked into core-tests, so the sources are read; jobs::Execute's answers are checked
/// in JobControl_Test, the verbs' in JobsVerbs_Test.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "debugger/analyzers/analysis/analysiscontrol.h"
#include "emulator/jobs/jobcontrol.h"

namespace
{
std::string ReadAll(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::filesystem::path WebApi() { return TestPathHelper::FindProjectRoot() / "core" / "automation" / "webapi"; }
}  // namespace

TEST(WebAPIJobsRoutes_Test, RoutesAreDeclaredAndBuilt)
{
    const std::string routes = ReadAll(WebApi() / "src" / "emulator_api.h");
    for (const char* route : {"\"/api/v1/jobs\", drogon::Get", "\"/api/v1/emulator/{id}/jobs\", drogon::Get",
                              "\"/api/v1/emulator/{id}/jobs\", drogon::Post", "\"/api/v1/jobs/{job}\", drogon::Get",
                              "\"/api/v1/jobs/{job}\", drogon::Delete", "\"/api/v1/jobs/{job}/cancel\", drogon::Post"})
        EXPECT_NE(routes.find(route), std::string::npos) << route;
    EXPECT_NE(ReadAll(WebApi() / "CMakeLists.txt").find("src/api/jobs_api.cpp"), std::string::npos);
}

TEST(WebAPIJobsRoutes_Test, LongPollHoldsNoThread)
{
    const std::string source = ReadAll(WebApi() / "src" / "api" / "jobs_api.cpp");
    EXPECT_NE(source.find("Registry::Instance().Watch("), std::string::npos) << "the end answers through a watch";
    EXPECT_NE(source.find("getLoop()->runAfter("), std::string::npos) << "the timeout is an event-loop timer";
    EXPECT_NE(source.find(".Unwatch(poll->job, poll->token)"), std::string::npos) << "the timer takes its watch back";
    EXPECT_NE(source.find("answered.exchange(true)"), std::string::npos) << "one answer";
    EXPECT_EQ(source.find("Registry::Instance().Wait("), std::string::npos) << "no blocking wait on a server thread";
    EXPECT_EQ(source.find("sleep_for"), std::string::npos);
}

TEST(WebAPIJobsRoutes_Test, LongVerbsPassAsyncOn)
{
    // GET verbs take only their own options from the query: async is added
    const std::string ttd = ReadAll(WebApi() / "src" / "api" / "ttd_api.cpp");
    EXPECT_NE(ttd.find("req->getParameter(\"async\")"), std::string::npos);
    // analysis: OptionsFor names async for the long actions, so the query and body pass it
    for (const char* action : {"build", "trace", "experiment", "cover"})
    {
        const std::vector<std::string>& options = analysis::OptionsFor(action);
        EXPECT_NE(std::find(options.begin(), options.end(), "async"), options.end()) << action;
    }
}

TEST(WebAPIJobsRoutes_Test, ManifestDescribesTheRoutesAndAsync)
{
    const std::string manifest = ReadAll(WebApi() / "src" / "openapi" / "openapi_jobs.inc");
    for (const char* text : {"paths[\"/api/v1/jobs\"][\"get\"]", "paths[\"/api/v1/emulator/{id}/jobs\"][\"post\"]",
                             "paths[\"/api/v1/jobs/{job}\"][\"get\"]", "\"/api/v1/jobs/{job}/cancel\"",
                             "queryParam(\"wait_ms\"", "queryParam(\"async\"", "#/components/schemas/JobAccepted",
                             "\"/api/v1/emulator/{id}/ttd/dump\"", "\"/api/v1/emulator/{id}/analysis/build\""})
        EXPECT_NE(manifest.find(text), std::string::npos) << text;
    EXPECT_NE(manifest.find(std::to_string(jobs::kMaxWaitMs)), std::string::npos) << "the documented cap is the code's";
    const std::string spec = ReadAll(WebApi() / "src" / "openapi_spec.cpp");
    EXPECT_NE(spec.find("openapi/openapi_jobs.inc"), std::string::npos);
    EXPECT_NE(spec.find("openapi/openapi_jobs_schemas.inc"), std::string::npos);
    EXPECT_LT(spec.find("openapi/openapi_ttd.inc"), spec.find("openapi/openapi_jobs.inc"))
        << "async is added to the TTD routes after they are described";
}

TEST(WebAPIJobsRoutes_Test, WebSocketHubKnowsTheTopics)
{
    const std::string hub = ReadAll(WebApi() / "src" / "ws-hub.cpp");
    EXPECT_NE(hub.find("\"jobs.progress\""), std::string::npos);
    EXPECT_NE(hub.find("\"jobs.done\""), std::string::npos);
}
