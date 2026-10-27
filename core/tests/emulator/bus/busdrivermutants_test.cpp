// Driver mutation tests for the bus analyzer (docs/inprogress/2026-10-11-bus-analyzer/mutation-tests.md): a reference SD
// driver in Z80 assembly runs clean on every SPI host; each mutant - the reference with one typical driver bug - makes
// exactly the expected finding, at the mutated instruction. The forks run the same source on TS-Conf, ZX Next and a 48K
// with the DivMMC card; the timing mutants show what only a slow card exposes.
//
// Boot-bound: each case creates a machine and runs the driver for a few frames (the slow-card cases for ~70 frames of
// emulated time at turbo speed): the bus is only reachable through a running Z80.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/zcsdtesthelper.h"
#include "debugger/assembler/z80textassembler.h"
#include "emulator/bus/bustap.h"
#include "emulator/bus/bustrace.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/divmmc/divmmcpaging.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/ports/models/portdecoder_tsconf.h"
#include "emulator/state/devicestate.h"

namespace
{
/// The reference driver. Every {TOKEN} is a mutation point with its reference text (Variant::points); the host's ports
/// and the command arguments are EQUs. "IN returns the previous exchange's byte and clocks #FF" on every host here, so
/// the driver reads a stream one byte behind the wire. Two port accesses are at least 16 CPU clocks apart (the Next's
/// byte time) unless a mutant says otherwise.
const char* const kDriver = R"(
        ORG #8000
start:  DI
        LD SP,#7FF0
        LD A,SON
        OUT (SELP),A
        LD B,10
pre:    LD A,#FF
        OUT (DATAP),A
        DJNZ pre
        LD HL,cmd0
        CALL command
        CP 1
        JP NZ,fail
        LD HL,cmd8
        CALL command
        CALL rx4
        LD DE,ACMDTRIES
acmd:   LD HL,cmd55
        CALL command
        LD HL,acmd41
        CALL command
        OR A
        JR Z,ready
        DEC DE
        LD A,D
        OR E
        JR NZ,acmd
        JP fail
ready:  LD HL,cmd58
        CALL command
        CALL rx4
        LD HL,cmd17
        CALL command
        OR A
        JP NZ,fail
        CALL token
        JP NZ,fail
{READ}
{CRC}
        LD HL,cmd24
        CALL command
        OR A
        JP NZ,fail
        LD A,#FF
        OUT (DATAP),A
        LD A,#FE
        OUT (DATAP),A
        LD HL,BUF
        LD BC,512
wr:     LD A,(HL)
        OUT (DATAP),A
        INC HL
        DEC BC
        LD A,B
        OR C
        JR NZ,wr
        LD A,#FF
        OUT (DATAP),A
        LD A,#FF
        OUT (DATAP),A
        CALL resp
        AND #1F
        CP 5
        JP NZ,fail
{BUSY}
{PRE13}
        LD HL,cmd13
        CALL command
        CALL rx
{DESELECT}
done:   JR done
fail:   JR fail

command: LD B,6
cmdl:   LD A,(HL)
cmdout: OUT (DATAP),A
        INC HL
        DJNZ cmdl
        LD B,16
cmdr:   CALL rx
        CP #FF
        RET NZ
        DJNZ cmdr
        RET
rx:     IN A,(DATAP)
        RET
rx4:    CALL rx
        CALL rx
        CALL rx
        JP rx
resp:   LD B,16
respl:  CALL rx
        CP #FF
        RET NZ
        DJNZ respl
        RET
token:  LD DE,TOKENTRIES
tokl:   CALL rx
        CP #FE
        RET Z
        DEC DE
        LD A,D
        OR E
        JR NZ,tokl
        OR 1
        RET
cmd0:   DB #40,0,0,0,0,#95
cmd8:   DB #48,0,0,1,#AA,{CMD8CRC}
cmd55:  DB #77,0,0,0,0,#FF
acmd41: DB #69,#40,0,0,0,#FF
cmd58:  DB #7A,0,0,0,0,#FF
cmd17:  DB #51,(ARG17>>24)&255,(ARG17>>16)&255,(ARG17>>8)&255,ARG17&255,#FF
cmd24:  DB #58,(ARG24>>24)&255,(ARG24>>16)&255,(ARG24>>8)&255,ARG24&255,#FF
cmd13:  DB #4D,0,0,0,0,#FF
)";

/// The reference text of every mutation point
std::map<std::string, std::string> ReferencePoints()
{
    return {
        {"READ", R"(        LD HL,BUF
        LD BC,512
rd:     CALL rx
        LD (HL),A
        INC HL
        DEC BC
        LD A,B
        OR C
        JR NZ,rd)"},
        {"CRC", "        CALL rx\n        CALL rx"},
        {"BUSY", "busy:   CALL rx\n        CP #FF\n        JR NZ,busy"},
        {"PRE13", ""},
        {"DESELECT", "        LD A,SOFF\n        OUT (SELP),A"},
        {"CMD8CRC", "#87"},
    };
}

/// One host: its ports, the chip-select values, the bus id, how commands address blocks
struct Host
{
    std::string model;
    std::string bus;
    uint8_t selPort;
    uint8_t dataPort;
    uint8_t selOn;
    uint8_t selOff;
    bool sdhc;  ///< block addressing (else byte addressing, SDSC)
};

const Host kTsConf{"TSL", "zc.spi", 0x77, 0x57, 0x00, 0x02, false};
const Host kNext{"NEXT", "next.spi", 0xE7, 0xEB, 0xFE, 0xFF, true};
const Host kDivMmc{"48K", "divmmc.spi", 0xE7, 0xEB, 0xFE, 0xFF, true};

constexpr uint32_t kReadBlock = 5;
constexpr uint32_t kWriteBlock = 6;
constexpr uint16_t kBuffer = 0x9000;

struct Variant
{
    std::map<std::string, std::string> points = ReferencePoints();
    unsigned tokenTries = 65535;
    unsigned acmdTries = 65535;
};

AsmResult AssembleDriver(const Host& host, const Variant& variant)
{
    std::string source = kDriver;
    for (const auto& [name, text] : variant.points)
    {
        const std::string token = "{" + name + "}";
        for (size_t at = source.find(token); at != std::string::npos; at = source.find(token))
            source.replace(at, token.size(), text);
    }
    // The text assembler lowercases a symbol inside a port operand "(name)" while labels are case-sensitive: the ports go in
    // as numbers
    char port[8];
    for (const auto& [name, value] : {std::pair<const char*, uint8_t>{"(SELP)", host.selPort}, {"(DATAP)", host.dataPort}})
    {
        std::snprintf(port, sizeof port, "(#%02X)", value);
        for (size_t at = source.find(name); at != std::string::npos; at = source.find(name))
            source.replace(at, std::strlen(name), port);
    }
    char equ[512];
    std::snprintf(equ, sizeof equ,
                  "SELP EQU #%02X\nDATAP EQU #%02X\nSON EQU #%02X\nSOFF EQU #%02X\nBUF EQU #%04X\nARG17 EQU %u\nARG24 EQU %u\n"
                  "TOKENTRIES EQU %u\nACMDTRIES EQU %u\n",
                  host.selPort, host.dataPort, host.selOn, host.selOff, kBuffer, host.sdhc ? kReadBlock : kReadBlock * 512,
                  host.sdhc ? kWriteBlock : kWriteBlock * 512, variant.tokenTries, variant.acmdTries);
    Z80TextAssembler assembler;
    return assembler.Assemble(std::string(equ) + source, 0x8000);
}

/// A machine with an SD card on the host's bus, the trace and checks on
class DriverRun
{
public:
    explicit DriverRun(const Host& host) : _host(host)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(host.model, LoggerLevel::LogError, RamPowerOn::Zero);
        if (!_emulator)
            return;
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        auto disk = zcsdtest::PatternDisk(64);
        if (host.model == "TSL")
            _card = dynamic_cast<PortDecoder_TSConf&>(*_context->pPortDecoder).InsertSdCard(std::move(disk), SdCardSpi::WriteMode::Session)
                        ? &dynamic_cast<PortDecoder_TSConf&>(*_context->pPortDecoder).GetSdCard()
                        : nullptr;
        else if (host.model == "NEXT")
        {
            auto& next = dynamic_cast<PortDecoder_Next&>(*_context->pPortDecoder);
            _card = next.InsertSdCard(0, std::move(disk), SdCardSpi::WriteMode::Session) ? &next.SdCard(0) : nullptr;
        }
        else
        {
            _div = std::make_unique<DivMmcPaging>(_context);
            std::vector<uint8_t> rom(DivMmcPaging::kRomSize, 0xFF);
            if (_div->LoadRom(rom.data(), rom.size()) && _div->Attach() &&
                _div->Card(0).insert(std::move(disk), SdCardSpi::WriteMode::Session, SdCardSpi::Type::SDHC))
                _card = &_div->Card(0);
        }
        _emulator->EnableTurboMode();
        DeviceState::BusControl(_context, host.bus, 1, 1, true);
    }

    ~DriverRun()
    {
        _div.reset();
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    bool Ready() const { return _emulator && _card; }
    SdCardSpi& Card() { return *_card; }

    /// Load and run the driver until it reaches `done` or `fail`; true at `done`
    bool Run(const AsmResult& driver, int maxFrames = 20)
    {
        _symbols = driver.symbols;
        Memory* memory = _context->pMemory;
        for (size_t i = 0; i < driver.bytes.size(); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), driver.bytes[i]);
        _z80->pc = 0x8000;
        const uint16_t done = Symbol("done");
        const uint16_t fail = Symbol("fail");
        EmulatorTestHelper::RunUntil(_emulator, [&] { return _z80->pc == done || _z80->pc == fail; }, maxFrames, 1);
        return _z80->pc == done;
    }

    uint16_t Symbol(const std::string& name) const
    {
        auto it = _symbols.find(name);
        return it != _symbols.end() ? static_cast<uint16_t>(it->second) : 0;
    }

    /// Findings as "rule@pc"
    std::vector<std::string> Findings() const
    {
        std::vector<std::string> out;
        BusFindingQuery q;
        q.limit = 0;
        const StateNode report = DeviceState::BusFindings(_context, _host.bus, q);
        if (const StateNode* list = report.find("findings"))
            for (const StateNode& f : list->items)
                out.push_back(f.find("rule")->s + "@" + f.find("pc")->s);
        return out;
    }

    std::vector<std::string> Commands() const
    {
        std::vector<std::string> out;
        BusEventQuery q;
        std::string error;
        BusEventQueryFromStrings("command", "", "", "", "", "0", q, error);
        const StateNode report = DeviceState::BusEvents(_context, _host.bus, q);
        if (const StateNode* list = report.find("events"))
            for (const StateNode& e : list->items)
                out.push_back(e.find("command")->s);
        return out;
    }

    uint8_t Peek(uint16_t address) { return _context->pMemory->DirectReadFromZ80Memory(address); }
    EmulatorContext* Context() { return _context; }
    const Host& HostOf() const { return _host; }

private:
    Host _host;
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    SdCardSpi* _card = nullptr;
    std::unique_ptr<DivMmcPaging> _div;
    std::map<std::string, uint32_t> _symbols;
};

std::string Hex16(uint16_t value)
{
    char text[8];
    std::snprintf(text, sizeof text, "0x%04X", value);
    return text;
}

/// The pattern byte of block b, offset o (zcsdtest::PatternDisk)
uint8_t Pattern(uint32_t block, uint32_t offset) { return static_cast<uint8_t>(block + (block * 512 + offset) % 7); }

/// A mutant: the reference with one point replaced
Variant Mutant(const std::string& point, const std::string& text)
{
    Variant v;
    v.points[point] = text;
    return v;
}

class DriverMutants_Test : public ::testing::TestWithParam<Host>
{
};

std::string HostName(const ::testing::TestParamInfo<Host>& info)
{
    return info.param.model == "48K" ? "DivMmc" : info.param.model;
}
}  // namespace

/// The reference driver: reads the block, writes the next one, no finding, the protocol in order
TEST_P(DriverMutants_Test, TheReferenceDriverRunsClean)
{
    DriverRun run(GetParam());
    ASSERT_TRUE(run.Ready());
    const AsmResult driver = AssembleDriver(GetParam(), Variant{});
    ASSERT_TRUE(driver.ok) << driver.error.message << " @ " << driver.error.line;
    ASSERT_TRUE(run.Run(driver)) << "the driver reached fail";
    for (uint32_t i : {0u, 1u, 300u, 511u})
        EXPECT_EQ(run.Peek(static_cast<uint16_t>(kBuffer + i)), Pattern(kReadBlock, i)) << i;
    uint8_t written[512];
    ASSERT_TRUE(run.Card().readBlock(kWriteBlock, written));
    EXPECT_EQ(written[0], Pattern(kReadBlock, 0)) << "the block read was written to the next one";
    EXPECT_EQ(run.Findings(), std::vector<std::string>{}) << "a correct driver: no finding";
    const auto commands = run.Commands();
    ASSERT_GE(commands.size(), 8u);
    EXPECT_EQ(commands.front(), "CMD0");
    EXPECT_EQ(commands[1], "CMD8");
    EXPECT_EQ(commands.back(), "CMD13");
}

/// M1: no busy poll after the write's data response
TEST_P(DriverMutants_Test, M1_NoBusyPollIsACommandWhileBusy)
{
    DriverRun run(GetParam());
    ASSERT_TRUE(run.Ready());
    const AsmResult driver = AssembleDriver(GetParam(), Mutant("BUSY", ""));
    ASSERT_TRUE(driver.ok) << driver.error.message;
    run.Run(driver);
    EXPECT_EQ(run.Findings(), std::vector<std::string>{"command_while_busy@" + Hex16(run.Symbol("cmdout"))});
}

/// M2: the 2 CRC bytes of the block are not read
TEST_P(DriverMutants_Test, M2_UnreadCrcIsACommandThatDropsTheAnswer)
{
    DriverRun run(GetParam());
    ASSERT_TRUE(run.Ready());
    const AsmResult driver = AssembleDriver(GetParam(), Mutant("CRC", ""));
    ASSERT_TRUE(driver.ok) << driver.error.message;
    run.Run(driver);
    EXPECT_EQ(run.Findings(), std::vector<std::string>{"command_drops_answer@" + Hex16(run.Symbol("cmdout"))});
}

/// M3: only half of the block is read
TEST_P(DriverMutants_Test, M3_HalfABlockIsACommandThatDropsTheAnswer)
{
    DriverRun run(GetParam());
    ASSERT_TRUE(run.Ready());
    std::string half = ReferencePoints()["READ"];
    half.replace(half.find("LD BC,512"), 9, "LD BC,256");
    const AsmResult driver = AssembleDriver(GetParam(), Mutant("READ", half));
    ASSERT_TRUE(driver.ok) << driver.error.message;
    run.Run(driver);
    EXPECT_EQ(run.Findings(), std::vector<std::string>{"command_drops_answer@" + Hex16(run.Symbol("cmdout"))});
}

/// M4: the card is deselected after 3 bytes of a command (an interrupt handler taking the bus)
TEST_P(DriverMutants_Test, M4_DeselectInsideACommand)
{
    DriverRun run(GetParam());
    ASSERT_TRUE(run.Ready());
    const AsmResult driver = AssembleDriver(GetParam(), Mutant("PRE13", R"(        LD A,#4D
        OUT (DATAP),A
        LD A,0
        OUT (DATAP),A
        LD A,0
        OUT (DATAP),A
        LD A,SOFF
bug:    OUT (SELP),A
        LD A,SON
        OUT (SELP),A)"));
    ASSERT_TRUE(driver.ok) << driver.error.message;
    EXPECT_TRUE(run.Run(driver)) << "the card drops the half command, the full one after it works";
    EXPECT_EQ(run.Findings(), std::vector<std::string>{"deselect_mid_command@" + Hex16(run.Symbol("bug"))});
}

/// M5: CMD8 with a wrong CRC (CMD8 is always CRC-checked)
TEST_P(DriverMutants_Test, M5_WrongCrcOnCmd8)
{
    DriverRun run(GetParam());
    ASSERT_TRUE(run.Ready());
    const AsmResult driver = AssembleDriver(GetParam(), Mutant("CMD8CRC", "#FF"));
    ASSERT_TRUE(driver.ok) << driver.error.message;
    run.Run(driver);
    EXPECT_EQ(run.Findings(), std::vector<std::string>{"crc_error@" + Hex16(run.Symbol("cmdout"))});
}

// ZX-Evo (BaseConf) is not a fork here: after a reset the Evo runs in shadow mode, where #77 is the ATM port and the
// card's chip select is the #8057 write, and its windows map the boot ROM. Its master is the same ZControllerSpi as
// TS-Conf's (forked above); the Evo's real SD software is checked by ZXEvoErs_Test.NedoOsSdDriverIsCleanOnTheBus
INSTANTIATE_TEST_SUITE_P(Hosts, DriverMutants_Test, ::testing::Values(kTsConf, kNext, kDivMmc), HostName);

/// region <TS-Conf: the SPI DMA>

namespace
{
/// The block read by the SPI DMA (SPI -> RAM #50000, 256 words), then the wait for DMA_STATUS bit 7; {DMAWAIT} is the
/// mutation point
const char* const kDmaRead = R"(        LD BC,#1DAF
        XOR A
        OUT (C),A
        LD B,#1E
        OUT (C),A
        LD B,#1F
        LD A,#14
        OUT (C),A
        LD B,#26
        LD A,#FF
        OUT (C),A
        LD B,#28
        XOR A
        OUT (C),A
        LD B,#27
        LD A,#02
        OUT (C),A
{DMAWAIT})";
const char* const kDmaWait = R"(dmaw:   LD BC,#27AF
        IN A,(C)
        RLCA
        JR C,dmaw)";
}  // namespace

TEST(DriverMutantsTsConf_Test, TheSpiDmaReadWaitingForTheDmaRunsClean)
{
    DriverRun run(kTsConf);
    ASSERT_TRUE(run.Ready());
    std::string read = kDmaRead;
    read.replace(read.find("{DMAWAIT}"), 9, kDmaWait);
    const AsmResult driver = AssembleDriver(kTsConf, Mutant("READ", read));
    ASSERT_TRUE(driver.ok) << driver.error.message;
    ASSERT_TRUE(run.Run(driver));
    EXPECT_EQ(run.Findings(), std::vector<std::string>{});
}

/// M6: the CRC read without waiting for the DMA
TEST(DriverMutantsTsConf_Test, M6_ReadingTheDataPortDuringTheDmaIsACollision)
{
    DriverRun run(kTsConf);
    ASSERT_TRUE(run.Ready());
    std::string read = kDmaRead;
    read.replace(read.find("{DMAWAIT}"), 9, "");
    const AsmResult driver = AssembleDriver(kTsConf, Mutant("READ", read));
    ASSERT_TRUE(driver.ok) << driver.error.message;
    run.Run(driver);
    const auto findings = run.Findings();
    ASSERT_FALSE(findings.empty());
    EXPECT_EQ(findings.front(), "dma_collision@" + Hex16(run.Symbol("rx"))) << "the first IN of the CRC read";
}

/// M7: deselect while the DMA runs
TEST(DriverMutantsTsConf_Test, M7_DeselectingDuringTheDmaIsACollision)
{
    DriverRun run(kTsConf);
    ASSERT_TRUE(run.Ready());
    std::string read = kDmaRead;
    read.replace(read.find("{DMAWAIT}"), 9, "        LD A,SOFF\nbug:    OUT (SELP),A\n        LD A,SON\n        OUT (SELP),A\n" + std::string(kDmaWait));
    const AsmResult driver = AssembleDriver(kTsConf, Mutant("READ", read));
    ASSERT_TRUE(driver.ok) << driver.error.message;
    run.Run(driver);
    const auto findings = run.Findings();
    ASSERT_FALSE(findings.empty());
    EXPECT_EQ(findings.front(), "dma_collision@" + Hex16(run.Symbol("bug")));
}

/// endregion

/// region <Next: the 16-clock byte time>

/// M8: the CRC bytes read back to back (IN A,(C) twice: 12 T apart, the byte takes 16)
TEST(DriverMutantsNext_Test, M8_BackToBackReadsAreEarlyAccesses)
{
    DriverRun run(kNext);
    ASSERT_TRUE(run.Ready());
    const AsmResult driver = AssembleDriver(kNext, Mutant("CRC", "        LD BC,DATAP\n        IN A,(C)\nbug:    IN A,(C)\n        CALL rx"));
    ASSERT_TRUE(driver.ok) << driver.error.message;
    run.Run(driver);
    const auto findings = run.Findings();
    ASSERT_FALSE(findings.empty());
    EXPECT_EQ(findings.front(), "early_access@" + Hex16(run.Symbol("bug")));
}

/// M9: the card deselected 15 clocks after a write to #EB: the byte is still shifting
TEST(DriverMutantsNext_Test, M9_DeselectRightAfterAWriteIsASelectMidByte)
{
    DriverRun run(kNext);
    ASSERT_TRUE(run.Ready());
    const AsmResult driver = AssembleDriver(kNext, Mutant("DESELECT", "        LD B,SOFF\n        LD A,#FF\n        OUT (DATAP),A\n"
                                                                      "        LD A,B\nbug:    OUT (SELP),A"));
    ASSERT_TRUE(driver.ok) << driver.error.message;
    run.Run(driver);
    EXPECT_EQ(run.Findings(), std::vector<std::string>{"select_mid_byte@" + Hex16(run.Symbol("bug"))});
}

/// endregion

/// region <Timing: what only a slow card exposes>

/// T1: an 8-bit token poll (255 tries) passes on the ideal card and gives up on a slow one; the reference's 16-bit
/// poll survives the slow card
TEST(DriverMutantsTiming_Test, T1_AShortTokenPollFailsOnASlowCard)
{
    for (unsigned tries : {255u, 65535u})
    {
        DriverRun run(kTsConf);
        ASSERT_TRUE(run.Ready());
        Variant v;
        v.tokenTries = tries;
        const AsmResult driver = AssembleDriver(kTsConf, v);
        ASSERT_TRUE(driver.ok) << driver.error.message;
        EXPECT_TRUE(run.Run(driver)) << "the ideal card: every poll limit passes (" << tries << ")";
        run.Card().SetTiming(SdCardSpi::Timing::Slow);
        const bool passed = run.Run(driver, 200);
        EXPECT_EQ(passed, tries == 65535u) << tries << " token polls on the slow card";
    }
}

/// T2: 20 ACMD41 polls are enough for the ideal card (4) and not for a slow one (1 s)
TEST(DriverMutantsTiming_Test, T2_AShortInitLoopFailsOnASlowCard)
{
    DriverRun run(kTsConf);
    ASSERT_TRUE(run.Ready());
    Variant v;
    v.acmdTries = 20;
    const AsmResult driver = AssembleDriver(kTsConf, v);
    ASSERT_TRUE(driver.ok) << driver.error.message;
    EXPECT_TRUE(run.Run(driver));
    run.Card().SetTiming(SdCardSpi::Timing::Slow);
    run.Card().powerOn();
    EXPECT_FALSE(run.Run(driver, 200)) << "the card is still initializing after 20 polls";
    const auto commands = run.Commands();
    EXPECT_EQ(commands.back(), "ACMD41") << "the trace ends in the give-up loop";
}

/// endregion
