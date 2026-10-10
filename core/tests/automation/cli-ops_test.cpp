/// @file cli-ops_test.cpp
/// @brief CLI `ops` (cli-ops.h): its arguments become the since / limit every surface passes to OperationsStatus. The
/// report itself is OperationsStatus_Test's.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../../automation/cli/src/commands/cli-ops.h"

namespace
{
struct Query
{
    bool ok;
    uint64_t since;
    uint64_t limit;
};

Query Parse(const std::vector<std::string>& args)
{
    Query q{};
    q.ok = CliOps::ParseArgs(args, q.since, q.limit);
    return q;
}
}  // namespace

TEST(CliOps_Test, ArgumentsBecomeSinceAndLimit)
{
    Query q = Parse({});
    EXPECT_TRUE(q.ok);
    EXPECT_EQ(q.since, 0u);
    EXPECT_EQ(q.limit, CliOps::kDefaultLimit) << "the newest 20 by default";

    q = Parse({"5"});
    EXPECT_TRUE(q.ok);
    EXPECT_EQ(q.limit, 5u);
    EXPECT_EQ(Parse({"all"}).limit, 0u);
    EXPECT_EQ(Parse({"0"}).limit, 0u);

    q = Parse({"since", "41"});
    EXPECT_TRUE(q.ok);
    EXPECT_EQ(q.since, 41u);
    EXPECT_EQ(q.limit, 0u) << "since: everything after it";
    q = Parse({"since", "41", "3"});
    EXPECT_EQ(q.limit, 3u);
    EXPECT_EQ(Parse({"since", "41", "all"}).limit, 0u);
}

TEST(CliOps_Test, AnythingElseIsUsage)
{
    EXPECT_FALSE(Parse({"since"}).ok);
    EXPECT_FALSE(Parse({"since", "x"}).ok);
    EXPECT_FALSE(Parse({"-1"}).ok);
    EXPECT_FALSE(Parse({"5", "6"}).ok);
    EXPECT_FALSE(Parse({"since", "1", "2", "3"}).ok);
    EXPECT_FALSE(Parse({"12345678901234567890"}).ok) << "too long for a count";
}
