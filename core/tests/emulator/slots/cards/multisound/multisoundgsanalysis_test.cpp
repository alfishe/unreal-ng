// The card CPU analysis of the ZX-MultiSound's General Sound (gsanalysisreport.h, cpu "ms"; gs-debugger P5.6): the
// switch and the reports find the board's GS, its ROM and RAM by physical page, apart from the GS slot

#include <gtest/gtest.h>

#include <string>

#include "emulator/sound/chips/gs/gsanalysisreport.h"
#include "emulator/sound/chips/gs/gsslotstats.h"
#include "emulator/state/devicecatalog.h"
#include "multisoundstagedmachine.h"

using namespace multisoundtest;

namespace
{
const StateNode& Field(const StateNode& node, const std::string& key)
{
    static const StateNode missing;
    const StateNode* found = node.find(key);
    return found != nullptr ? *found : missing;
}
} // namespace

TEST(MultiSoundGsAnalysis_Test, TheMsCpuMarksTheBoardsGs)
{
    StagedMachine m("pentagon128k", "zxbus.1 = multisound");
    ASSERT_TRUE(m.Ok());
    GSAnalysisControlRequest on;
    on.action = "on";
    on.cpu = "ms";
    const GSAnalysisReply reply = GSAnalysisControl(m.Context(), on);
    ASSERT_EQ(reply.status, 200) << Field(reply.body, "error").s;
    EXPECT_EQ(Field(reply.body, "cpu").s, "ms");
    EXPECT_EQ(Field(reply.body, "ram_bytes").i, 1024 * 1024) << "the 1 MB build";

    ParkCpu(m);
    m.Machine().RunNFrames(3);

    GSAnalysisQuery query;
    query.cpu = "ms";
    const StateNode coverage = DeviceState::GsCoverage(m.Context(), query);
    ASSERT_TRUE(Field(coverage, "available").b) << Field(coverage, "description").s;
    EXPECT_TRUE(Field(coverage, "recording").b);
    const StateNode& rom = Field(coverage, "spaces").items.at(0);
    EXPECT_GT(Field(Field(rom, "totals"), "exec").i, 100) << "the board's firmware ran: " << Field(coverage, "text").s;

    // The GS slot is empty: cpu gs has nothing; the catalog lists the ms reports
    GSAnalysisControlRequest slot;
    slot.action = "on";
    EXPECT_EQ(GSAnalysisControl(m.Context(), slot).status, 404);
    bool listed = false;
    const StateNode catalog = DeviceState::Catalog(m.Context());
    for (const StateNode& entry : Field(catalog, "reports").items)
        listed = listed || Field(entry, "id").s == "ms-gs-coverage";
    EXPECT_TRUE(listed);
}
