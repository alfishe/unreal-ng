// The TS-Conf machine's snapshot commit policy (snapshot pipeline, PLAN #84): an SPG is committed from the neutral
// snapshot::Image by the machine's own policy, not from the loader's private record. The tests change the IMAGE between the
// plan and the commit and check the machine follows it, and pin who decides on every machine.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "_helpers/testpathhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_tsconf.h"
#include "loaders/snapshot/loaderspg.h"
#include "loaders/snapshot/snapshotpipeline.h"

namespace
{
std::string EmptySpg()
{
    return (TestPathHelper::FindProjectRoot() / "testdata" / "machines" / "tsconf" / "spg" / "empty.spg").string();
}

class TsConfProgramSnapshot_Test : public ::testing::Test
{
protected:
    void TearDown() override
    {
        for (const auto& id : EmulatorManager::GetInstance()->GetEmulatorIds())
            EmulatorManager::GetInstance()->RemoveEmulator(id);
    }
    std::shared_ptr<Emulator> Create(const char* model, uint32_t ramKb = 0)
    {
        return ramKb ? EmulatorManager::GetInstance()->CreateEmulatorWithModelAndRAM("spg-policy", model, ramKb, LoggerLevel::LogError)
                     : EmulatorManager::GetInstance()->CreateEmulatorWithModel("spg-policy", model, LoggerLevel::LogError);
    }
};
}  // namespace

// The machine's policy decides: the report names it, and what it did
TEST_F(TsConfProgramSnapshot_Test, AnSpgIsCommittedByTheMachinesPolicy)
{
    auto emulator = Create("TSL", 4096);
    ASSERT_NE(emulator, nullptr);
    ASSERT_TRUE(emulator->LoadSnapshot(EmptySpg()));
    const snapshot::Report& report = emulator->LastSnapshotReport();
    EXPECT_EQ(report.format, "spg");
    EXPECT_EQ(report.commit, "tsconf-program");
    EXPECT_FALSE(report.refused);
    bool registers = false, blocks = false, cpu = false;
    for (const auto& item : report.items)
    {
        registers = registers || item.item == "registers";
        blocks = blocks || item.item == "blocks";
        cpu = cpu || item.item == "CPU";
    }
    EXPECT_TRUE(registers && blocks && cpu) << "the policy reports what it applied";
}

// What the machine ends up with is what the IMAGE says: change the image after the plan, commit, and the machine follows
TEST_F(TsConfProgramSnapshot_Test, TheMachineFollowsTheImage)
{
    auto emulator = Create("TSL", 4096);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    snapshot::Image image;
    std::string error;
    ASSERT_TRUE(LoaderSPG::ReadSnapshotImage(EmptySpg(), image, error)) << error;

    snapshot::Report report;
    snapshot::Decision decision = snapshot::Pipeline::Plan(image, context, {}, report);
    ASSERT_TRUE(decision.Proceeds());
    ASSERT_EQ(decision.action, snapshot::Decision::Action::Take);

    image.cpu.pc = 0x1234;
    image.cpu.sp = 0x5678;
    image.cpu.iff1 = image.cpu.iff2 = true;
    for (snapshot::Extension& extension : image.extensions)
    {
        if (extension.origin == LoaderSPG::kHeaderOrigin)
        {
            extension.payload[0] = 0x05;   // the RAM page at #C000
            extension.payload[1] = 0x01;   // SYS_CONFIG clock
        }
    }
    image.physical.clear();
    image.physical.push_back(snapshot::PhysicalRun{0x00A000, {0xDE, 0xAD, 0xBE, 0xEF}});
    ASSERT_TRUE(decision.Commit(image, *context, report));

    Z80& z80 = *context->pCore->GetZ80();
    EXPECT_EQ(z80.pc, 0x1234);
    EXPECT_EQ(z80.sp, 0x5678);
    EXPECT_EQ(z80.iff1, 1);
    auto* decoder = dynamic_cast<PortDecoder_TSConf*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    EXPECT_EQ(decoder->GetState().regs[TsConfReg::Page3], 0x05);
    const uint8_t* ram = context->pMemory->RAMBase();
    EXPECT_EQ(ram[0x00A000], 0xDE);
    EXPECT_EQ(ram[0x00A003], 0xEF);
}

// Another machine has no such policy: an SPG is refused with the reason there, before anything is written
TEST_F(TsConfProgramSnapshot_Test, AnotherMachineRefusesTheProgram)
{
    auto pentagon = Create("PENTAGON");
    ASSERT_NE(pentagon, nullptr);
    EXPECT_FALSE(pentagon->LoadSnapshot(EmptySpg()));
    LoaderSPG loader(pentagon->GetContext(), EmptySpg());
    EXPECT_FALSE(loader.load());
    EXPECT_NE(loader.GetError().find("TS-Conf"), std::string::npos) << loader.GetError();
}

// A Spectrum snapshot on the TS-Conf machine is not the program policy's business: it declines and the usual plan decides
TEST_F(TsConfProgramSnapshot_Test, ASpectrumSnapshotIsNotTheProgramPolicysBusiness)
{
    auto emulator = Create("TSL", 4096);
    ASSERT_NE(emulator, nullptr);
    ASSERT_TRUE(emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/action.sna")));
    EXPECT_NE(emulator->LastSnapshotReport().commit, "tsconf-program");
    EXPECT_FALSE(emulator->LastSnapshotReport().refused);
}

// `commit=legacy` still loads an SPG (the same code from the same image)
TEST_F(TsConfProgramSnapshot_Test, LegacyCommitLoadsTheProgramToo)
{
    auto emulator = Create("TSL", 4096);
    ASSERT_NE(emulator, nullptr);
    snapshot::Options options;
    options.commit = "legacy";
    ASSERT_TRUE(emulator->LoadSnapshot(EmptySpg(), {}, options));
    EXPECT_EQ(emulator->LastSnapshotReport().commit, "legacy");
    EXPECT_EQ(emulator->GetContext()->pCore->GetZ80()->pc, 0xE000);
}
