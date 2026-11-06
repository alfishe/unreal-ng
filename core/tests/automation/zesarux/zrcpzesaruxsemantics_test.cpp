// zrcp::Server against ZEsarUX's own semantics: the findings of the live ZRCP suite
// (tools/verification/zesarux/verify_zrcp_emulator.py). Pass counts, breakpoint indexes, RAM / ROM, steps onto a
// breakpoint and after a history browse, and the commands DeZog's zesaruxremote.ts sends beyond the basic set.

#include "dezogzesaruxtestfixture.h"

#include "_helpers/scratchfolder.h"

#include <string>

namespace
{
const std::string kRunning = "Running until a breakpoint, key press or data sent, menu opening or other event";

// 8000: INC B; 8001: JR 8000h. B counts the passes over 8000h
const std::vector<uint8_t> kCounter = {0x04, 0x18, 0xFD};
}  // namespace

class ZrcpSemantics_test : public DezogZesaruxFixture
{
protected:
    void installCounter()
    {
        _adapter->writeMemory(0x8000, kCounter);
        _adapter->setRegister(dzrp::RegisterId::PC, 0x8000);
        _adapter->setRegister(dzrp::RegisterId::BC, 0x0000);
    }

    uint8_t B() const { return static_cast<uint8_t>(_adapter->getRegisters().bc >> 8); }

    /// "run" up to a stop; returns the "Breakpoint fired" line
    std::string runToStop()
    {
        _client.sendLine("run");
        EXPECT_EQ(_client.readLine(), kRunning);
        const std::string fired = _client.readLine();
        EXPECT_FALSE(_client.readUntilPrompt().empty());
        return fired;
    }

    /// "run" that must not stop: interrupted by DeZog's blank line, the answer holds no breakpoint message
    void runWithoutStop()
    {
        _client.sendLine("run");
        EXPECT_EQ(_client.readLine(), kRunning);
        ASSERT_TRUE(TestWait::For([&] { return _emulator->IsRunning(); }, std::chrono::milliseconds(2000)));
        // A false hit resumes in the server: one frame of passes is plenty to show none of them stops
        freeRunFramesWhileRunning(1);
        _client.sendBlank();
        EXPECT_EQ(_client.readUntilPrompt().find("Breakpoint fired"), std::string::npos);
    }

    /// Wait until the running machine has completed `frames` more frames (it stops early on a breakpoint)
    void freeRunFramesWhileRunning(unsigned frames)
    {
        const uint64_t target = frameCounter() + frames;
        TestWait::For([&] { return frameCounter() >= target; }, std::chrono::milliseconds(1000));
    }
};

/// region <Pass count>

TEST_F(ZrcpSemantics_test, APassCountFiresOnceOnItsHit)
{
    initSession();
    installCounter();
    EXPECT_EQ(_client.command("set-breakpoint 1 PC=08000h"), "");
    EXPECT_EQ(_client.command("set-breakpointpasscount 1 3"), "");
    EXPECT_EQ(_client.command("enable-breakpoint 1"), "");

    EXPECT_EQ(runToStop(), "Breakpoint fired: PC=08000h");
    EXPECT_EQ(B(), 2) << "the third pass";

    // ZEsarUX: the counter passed the count, the breakpoint never fires again
    runWithoutStop();
}

TEST_F(ZrcpSemantics_test, SetBreakpointResetsThePassCount)
{
    initSession();
    installCounter();
    EXPECT_EQ(_client.command("set-breakpointpasscount 2 3"), "");
    EXPECT_EQ(_client.command("set-breakpoint 2 PC=08000h"), "");
    EXPECT_EQ(_client.command("enable-breakpoint 2"), "");

    EXPECT_EQ(runToStop(), "Breakpoint fired: PC=08000h");
    EXPECT_EQ(B(), 0) << "the first pass: set-breakpoint cleared the count";
}

/// endregion </Pass count>

/// region <Indexes>

TEST_F(ZrcpSemantics_test, BreakpointIndexesAreOneToOneHundred)
{
    initSession();
    for (const char* line : {"set-breakpoint 0 PC=08000h", "set-breakpoint 101 PC=08000h", "enable-breakpoint 101",
                             "disable-breakpoint 0", "set-breakpointpasscount 101 2", "set-breakpointaction 0 prints x"})
        EXPECT_EQ(_client.command(line), "Error. Index out of range") << line;
    EXPECT_EQ(_client.command("set-breakpoint 100 PC=08000h"), "");
    EXPECT_EQ(_client.command("set-breakpoint 100 0"), "");
}

TEST_F(ZrcpSemantics_test, GetBreakpointsListsTheConditions)
{
    initSession();
    EXPECT_EQ(_client.command("set-breakpoint 3 PC=08006h"), "");
    EXPECT_EQ(_client.command("enable-breakpoint 3"), "");
    EXPECT_EQ(_client.command("get-breakpoints 3 2"), "Breakpoints: On\nEnabled 3: PC=08006h\nDisabled 4: None");
    EXPECT_EQ(_client.command("get-breakpoints 101"), "ERROR. Index out of range");

    const std::string all = _client.command("get-breakpoints");
    EXPECT_EQ(all.substr(0, 16), "Breakpoints: On\n");
    EXPECT_NE(all.find("\nDisabled 100: None"), std::string::npos);
}

/// endregion </Indexes>

/// region <RAM / ROM>

TEST_F(ZrcpSemantics_test, RamIsTheBankAtC000WhereverTheBreakpointIs)
{
    // The fixture's Pentagon: bank 2 at 0x8000, bank 0 at 0xC000. ZEsarUX's RAM is the 0xC000 bank
    initSession();
    installProgram();
    EXPECT_EQ(_client.command("set-breakpoint 1 PC=08001h and RAM=2"), "");
    EXPECT_EQ(_client.command("enable-breakpoint 1"), "");
    EXPECT_EQ(_client.command("set-breakpoint 2 PC=08003h and RAM=0"), "");
    EXPECT_EQ(_client.command("enable-breakpoint 2"), "");

    EXPECT_EQ(runToStop(), "Breakpoint fired: PC=08003h and RAM=0");
    EXPECT_EQ(_adapter->getRegisters().pc, 0x8003u);
}

/// endregion </RAM / ROM>

/// region <Steps>

TEST_F(ZrcpSemantics_test, CpuStepLeavesABreakpointItStoppedOn)
{
    initSession();
    installProgram();
    EXPECT_EQ(_client.command("set-breakpoint 1 PC=08001h"), "");
    EXPECT_EQ(_client.command("enable-breakpoint 1"), "");

    EXPECT_EQ(_client.command("set-register PC=32768"), "");
    EXPECT_EQ(_client.command("cpu-step").substr(0, 5), "8001 ");
    // The breakpoint at PC does not hold the step
    EXPECT_EQ(_client.command("cpu-step").substr(0, 5), "8003 ");
    EXPECT_EQ(_adapter->getRegisters().pc, 0x8003u);
}

TEST_F(ZrcpSemantics_test, CpuStepAfterAHistoryBrowseStepsThePresent)
{
    initSession();
    installProgram();
    for (int i = 0; i < 3; ++i)
        ASSERT_FALSE(_client.command("cpu-step").empty());
    ASSERT_EQ(_adapter->getRegisters().pc, PROGRAM_JP);

    EXPECT_EQ(_client.command("cpu-history get 0").substr(0, 3), "PC=");
    EXPECT_EQ(_client.command("cpu-step").substr(0, 5), "8001 ") << "the JP at the present PC ran";
    EXPECT_EQ(_adapter->getRegisters().pc, PROGRAM_LOOP);
}

/// endregion </Steps>

/// region <Memory, files and snapshots>

TEST_F(ZrcpSemantics_test, WriteMemoryAndHexdump)
{
    initSession();
    EXPECT_EQ(_client.command("write-memory 32768 65 66 0"), "");
    EXPECT_EQ(_adapter->readMemory(0x8000, 3), (std::vector<uint8_t>{0x41, 0x42, 0x00}));

    std::string expected = "  8000H 41 42 00 ";
    for (int i = 3; i < 16; ++i)
        expected += "   ";
    expected += " |AB.|";
    EXPECT_EQ(_client.command("hexdump 32768 3"), expected);
    EXPECT_EQ(_client.command("hexdump 32768"), "ERROR. Needs two parameters");
}

TEST_F(ZrcpSemantics_test, LoadBinaryWritesAFileThroughTheMemoryMap)
{
    ScratchFolder folder("zrcp-load-binary");
    const std::filesystem::path file = folder.File("code.bin", std::string("\x01\x02\x03\x04", 4));
    initSession();

    EXPECT_EQ(_client.command("load-binary \"" + file.string() + "\" 36864 0"), "");
    EXPECT_EQ(_adapter->readMemory(0x9000, 4), (std::vector<uint8_t>{1, 2, 3, 4}));
    // A length takes that many bytes
    EXPECT_EQ(_client.command("load-binary \"" + file.string() + "\" 40960 2"), "");
    EXPECT_EQ(_adapter->readMemory(0xA000, 3), (std::vector<uint8_t>{1, 2, 0}));

    EXPECT_EQ(_client.command("load-binary nofile"), "ERROR. Needs three parameters");
    EXPECT_EQ(_client.command("load-binary \"" + (folder.Path() / "missing.bin").string() + "\" 0 0"),
              "ERROR loading file");
}

TEST_F(ZrcpSemantics_test, SnapshotSaveThenLoadRestoresTheMachine)
{
    ScratchFolder folder("zrcp-snapshot");
    initSession();
    installProgram();
    // DeZog's own extension (.zsf) and a format by its extension
    for (const char* name : {"state.zsf", "state.szx"})
    {
        const std::string path = (folder.Path() / name).string();
        _adapter->writeMemory(0x9100, {0xA5});
        EXPECT_EQ(_client.command("snapshot-save " + path), "") << name;
        ASSERT_TRUE(std::filesystem::exists(path)) << name;

        _adapter->writeMemory(0x9100, {0x00});
        EXPECT_EQ(_client.command("snapshot-load " + path), "") << name;
        EXPECT_EQ(_adapter->readMemory(0x9100, 1), (std::vector<uint8_t>{0xA5})) << name;
    }
    EXPECT_EQ(_client.command("snapshot-load " + (folder.Path() / "missing.zsf").string()).substr(0, 6), "ERROR.");
}

TEST_F(ZrcpSemantics_test, TbblueRegistersNeedTheNext)
{
    EXPECT_EQ(_client.command("tbblue-get-register 84"), "ERROR. Machine is not TBBlue");
    EXPECT_EQ(_client.command("tbblue-set-register 84 40"), "ERROR. Machine is not TBBlue");
}

/// endregion </Memory, files and snapshots>

/// region <ZX Next>

class ZrcpSemanticsNext_test : public ZrcpSemantics_test
{
protected:
    std::string ModelName() const override { return "NEXT"; }
};

TEST_F(ZrcpSemanticsNext_test, TbblueRegistersAreTheNextRegisters)
{
    // DeZog's setSlot: tbblue-set-register (0x50 + slot) bank
    EXPECT_EQ(_client.command("tbblue-set-register 84 40"), "");
    EXPECT_EQ(_adapter->readNextReg(0x54), 40);
    EXPECT_EQ(_client.command("tbblue-get-register 84"), "28H");
    EXPECT_EQ(_client.command("tbblue-get-register 300"), "ERROR. Out of range");
}

/// endregion </ZX Next>
