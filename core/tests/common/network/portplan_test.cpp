// PortPlan (re-api-efficiency V7): the free port base search of `--port-base auto` with the "can bind" check
// injected, the real check against a listener of the test's own, and the ports file.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/scratchfolder.h"
#include "common/filehelper.h"
#include "common/network/netsockets.h"
#include "common/network/portplan.h"

namespace
{
portplan::SearchRange SmallRange()
{
    portplan::SearchRange range;
    range.firstBase = 20000;
    range.lastBase = 21200;  // 20000, 20600, 21200
    range.step = 600;
    return range;
}
}  // namespace

TEST(PortPlan_Test, TheFirstBaseIsTakenWhenAllItsPortsAreFree)
{
    std::vector<int> asked;
    std::string error = "untouched";
    const int base = portplan::FindFreePortBase(SmallRange(), [&asked](int port) {
        asked.push_back(port);
        return true;
    }, &error);

    EXPECT_EQ(base, 20000);
    EXPECT_EQ(asked, (std::vector<int>{20000, 20100, 20200, 20300, 20400, 20500}));  // every derived port, once
    EXPECT_EQ(error, "untouched");
}

TEST(PortPlan_Test, OneTakenPortMovesToTheNextBase)
{
    // Each derived port in turn: a base with any one of them in use is passed over
    for (const portplan::PortRole& role : portplan::kRoles)
    {
        const int taken = 20000 + role.offset;
        const int base = portplan::FindFreePortBase(SmallRange(), [taken](int port) { return port != taken; });
        EXPECT_EQ(base, 20600) << role.name;
    }
}

TEST(PortPlan_Test, NothingFreeIsAnErrorThatNamesTheRange)
{
    std::string error;
    const int base = portplan::FindFreePortBase(SmallRange(), [](int) { return false; }, &error);

    EXPECT_EQ(base, 0);
    EXPECT_NE(error.find("no free port base from 20000 to 21200"), std::string::npos) << error;
    EXPECT_NE(error.find("3 candidates"), std::string::npos) << error;
}

TEST(PortPlan_Test, TheStartSlotIsTriedFirstAndTheSearchWrapsAround)
{
    portplan::SearchRange range = SmallRange();
    range.startSlot = 2 + 3 * 7;  // modulo the three candidates: the last one
    EXPECT_EQ(portplan::FindFreePortBase(range, [](int) { return true; }), 21200);

    // The last candidate taken: on to the first, not past the end of the range
    std::set<int> seen;
    const int base = portplan::FindFreePortBase(range, [&seen](int port) {
        seen.insert(port);
        return port < 21200;
    });
    EXPECT_EQ(base, 20000);
    EXPECT_EQ(seen.count(21200), 1u);
    EXPECT_LE(*seen.rbegin(), 21200 + portplan::kLastOffset);
}

TEST(PortPlan_Test, AnInvalidRangeIsRefused)
{
    std::string error;
    portplan::SearchRange overlapping = SmallRange();
    overlapping.step = 100;  // two candidates would share ports
    EXPECT_EQ(portplan::FindFreePortBase(overlapping, [](int) { return true; }, &error), 0);
    EXPECT_FALSE(error.empty());

    portplan::SearchRange pastTheEnd = SmallRange();
    pastTheEnd.lastBase = 65500;  // + 500 is not a port
    EXPECT_EQ(portplan::FindFreePortBase(pastTheEnd, [](int) { return true; }), 0);

    EXPECT_TRUE(portplan::IsValidPortBase(1));
    EXPECT_TRUE(portplan::IsValidPortBase(65035));
    EXPECT_FALSE(portplan::IsValidPortBase(65036));
    EXPECT_FALSE(portplan::IsValidPortBase(0));
}

TEST(PortPlan_Test, TheRealCheckSeesAListener)
{
    ASSERT_TRUE(netsock::Startup());
    const netsock::Handle listener = netsock::OpenTcp();
    ASSERT_NE(listener, netsock::kInvalid);
    ASSERT_EQ(netsock::Bind(listener, 0, 0, false), netsock::Result::Ok);  // any free port
    ASSERT_EQ(netsock::Listen(listener, 1), netsock::Result::Ok);
    const int port = netsock::LocalPort(listener);
    ASSERT_GT(port, 0);

    EXPECT_FALSE(portplan::CanBindTcpPort(port));
    netsock::Close(listener);
    EXPECT_TRUE(portplan::CanBindTcpPort(port));

    EXPECT_FALSE(portplan::CanBindTcpPort(0));
    EXPECT_FALSE(portplan::CanBindTcpPort(65536));
}

TEST(PortPlan_Test, ThePortsFileNamesEveryPortAndIsRenamedIntoPlace)
{
    EXPECT_EQ(portplan::PortsFileJson(35100, 4242),
              "{\"port_base\": 35100, \"webapi\": 35100, \"cli\": 35200, \"mcp\": 35300, \"gdb\": 35400, "
              "\"dezog\": 35500, \"zrcp\": 35600, \"pid\": 4242}\n");

    ScratchFolder folder("portplan");
    const std::string path = FileHelper::FromFsPath(folder.Path() / "ports.json");
    std::string error;
    ASSERT_TRUE(portplan::WritePortsFile(path, 35100, 4242, &error)) << error;
    ASSERT_TRUE(portplan::WritePortsFile(path, 20600, 4242, &error)) << error;  // over an older one

    std::ifstream in(folder.Path() / "ports.json", std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    EXPECT_EQ(text.str(), portplan::PortsFileJson(20600, 4242));

    size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder.Path()))
    {
        (void)entry;
        files++;
    }
    EXPECT_EQ(files, 1u);  // no temporary file left beside it

    EXPECT_FALSE(portplan::WritePortsFile(FileHelper::FromFsPath(folder.Path() / "none" / "ports.json"), 35100, 1,
                                          &error));
    EXPECT_NE(error.find("cannot write"), std::string::npos) << error;
}
