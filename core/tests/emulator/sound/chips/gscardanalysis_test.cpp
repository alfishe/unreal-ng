// The GS card CPU analysis (gscardanalysis.h, gsanalysisreport.h; gs-debugger P5.6, requirements §4.13 X8 / X10 /
// X11): a flag byte per physical byte of the card's ROM / flash and RAM, the RAM heat map, the reads of RAM never
// written since the card reset. Hand-built card programs on bare cards mark exactly their bytes on their physical
// pages; the reports and the switch on an emulated Pentagon with the real firmware.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/mainloop.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/gsanalysisreport.h"
#include "emulator/sound/chips/gs/gscardanalysis.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/devicecatalog.h"

namespace
{
using A = GSCardAnalysis;
constexpr uint32_t kPage = 0x4000;

const StateNode& Field(const StateNode& node, const char* key)
{
    static const StateNode none;
    const StateNode* v = node.find(key);
    return v ? *v : none;
}

void Configure(EmulatorContext& ctx)
{
    ctx.config.sound.gs_vol = 8000;
    ctx.config.frame = 69888;
    ctx.config.frame_duration_us = 19968;
    ctx.emulatorState.current_z80_frequency_multiplier = 1;
    ctx.emulatorState.hw_turbo_ratio_applied = 1;
}

/// A classic GS with a zeroed ROM: the CPU runs NOPs through ROM page 0 into #4000 (RAM page 1, the fixed window)
struct ClassicCard
{
    EmulatorContext ctx{LoggerLevel::LogError};
    std::unique_ptr<SoundChip_GeneralSound> chip;

    ClassicCard()
    {
        Configure(ctx);
        chip = std::make_unique<SoundChip_GeneralSound>(&ctx, 512, 44100);
    }

    void frame()
    {
        chip->handleFrameStart();
        chip->handleFrameEnd(SAMPLES_PER_FRAME);
        ctx.emulatorState.frame_counter++;
    }

    void poke(uint16_t at, const std::vector<uint8_t>& bytes)
    {
        for (uint8_t b : bytes)
            ASSERT_TRUE(chip->pokeCardMemory(at++, b));
    }

    /// #4000: MPAG = 2 (windows 2 / 3 = RAM pages 2 / 3), a read of #8000 never written, a write and a read of #8001,
    /// a DAC read of #6010, CALL #4020 (a read of #C000 never written, RET), JR $
    void loadProgram()
    {
        poke(0x4000, {0x3E, 0x02,          // LD A,2
                      0xD3, 0x00,          // OUT (0),A: MPAG
                      0x3A, 0x00, 0x80,    // #4004 LD A,(#8000)
                      0x32, 0x01, 0x80,    // #4007 LD (#8001),A
                      0x3A, 0x01, 0x80,    // #400A LD A,(#8001)
                      0x3A, 0x10, 0x60,    // #400D LD A,(#6010): a DAC fetch
                      0xCD, 0x20, 0x40,    // #4010 CALL #4020
                      0x18, 0xFE});        // #4013 JR $
        poke(0x4020, {0x3A, 0x00, 0xC0,    // #4020 LD A,(#C000)
                      0xC9});              // #4023 RET
    }

    std::shared_ptr<GSCardAnalysis> on()
    {
        std::string error;
        EXPECT_TRUE(chip->setAnalysisOn(true, &ctx.emulatorState.frame_counter, &error)) << error;
        return chip->analysis();
    }
};

uint8_t RamFlag(const GSCardAnalysis& a, uint32_t page, uint32_t offset)
{
    return a.Flags(A::Ram)[page * kPage + offset];
}

uint32_t Count(const GSCardAnalysis& a, A::Space space, uint8_t mask, uint32_t from = 0, uint32_t to = ~0u)
{
    uint32_t n = 0;
    const uint8_t* f = a.Flags(space);
    for (uint32_t i = from; i < a.SpaceBytes(space) && i < to; i++)
        n += (f[i] & mask) != 0;
    return n;
}
}  // namespace

TEST(GSCardAnalysis_Test, OffAllocatesNothingAndMarksNothing)
{
    ClassicCard card;
    card.loadProgram();
    card.frame();
    EXPECT_EQ(card.chip->analysis(), nullptr) << "off by default: no memory";
    EXPECT_FALSE(card.chip->analysisOn());
    EXPECT_FALSE(card.chip->analysisBound());

    const auto a = card.on();
    ASSERT_NE(a, nullptr);
    EXPECT_FALSE(card.chip->analysisBound()) << "bound at the card's next run";
    EXPECT_EQ(Count(*a, A::Rom, 0xFF), 0u);
    card.chip->reset();
    card.frame();
    EXPECT_TRUE(card.chip->analysisBound());
    const uint32_t marked = Count(*a, A::Ram, 0xFF);
    EXPECT_GT(marked, 0u);

    // Off: unbound at the next run, the data kept, nothing more marked
    std::string error;
    ASSERT_TRUE(card.chip->setAnalysisOn(false, nullptr, &error)) << error;
    card.frame();
    EXPECT_FALSE(card.chip->analysisBound());
    a->Clear();
    card.chip->reset();
    card.frame();
    EXPECT_EQ(Count(*a, A::Ram, 0xFF), 0u) << "off marks nothing";
    EXPECT_EQ(Count(*a, A::Rom, 0xFF), 0u);

    card.chip->releaseAnalysis();
    card.frame();
    EXPECT_EQ(card.chip->analysis(), nullptr) << "release frees the arrays";
}

TEST(GSCardAnalysis_Test, AHandBuiltProgramMarksExactlyItsBytesOnTheirPhysicalPages)
{
    ClassicCard card;
    card.loadProgram();
    const auto a = card.on();
    ASSERT_NE(a, nullptr);
    card.chip->reset();
    card.frame();
    ASSERT_TRUE(card.chip->analysisBound());

    // ROM page 0: 16,384 NOPs executed, ROM page 1 untouched
    EXPECT_EQ(Count(*a, A::Rom, A::Exec, 0, kPage), kPage);
    EXPECT_EQ(Count(*a, A::Rom, 0xFF, kPage, 2 * kPage), 0u);

    // RAM page 1 (the fixed window #4000): the instruction starts and operands, nothing else
    const std::vector<uint32_t> starts = {0x00, 0x02, 0x04, 0x07, 0x0A, 0x0D, 0x10, 0x13, 0x20, 0x23};
    const std::vector<uint32_t> operands = {0x01, 0x03, 0x05, 0x06, 0x08, 0x09, 0x0B, 0x0C,
                                            0x0E, 0x0F, 0x11, 0x12, 0x14, 0x21, 0x22};
    for (uint32_t o : starts)
        EXPECT_TRUE(RamFlag(*a, 1, o) & A::Exec) << std::hex << o;
    for (uint32_t o : operands)
        EXPECT_EQ(RamFlag(*a, 1, o) & (A::Exec | A::Operand), A::Operand) << std::hex << o;
    EXPECT_EQ(Count(*a, A::Ram, A::Exec, kPage, 2 * kPage), starts.size());
    EXPECT_EQ(Count(*a, A::Ram, A::Operand, kPage, 2 * kPage), operands.size());
    EXPECT_EQ(RamFlag(*a, 1, 0x20), A::Exec | A::Sub) << "the CALL target is a subroutine entry";
    EXPECT_EQ(Count(*a, A::Ram, A::Sub), 1u);
    EXPECT_EQ(Count(*a, A::Ram, A::Jump), 0u) << "sequential from ROM, the RET's return and JR $ are not targets";

    // The data: page 2 #0000 read, #0001 written then read, page 3 #0000 read, page 1 #2010 read by the DAC
    EXPECT_EQ(RamFlag(*a, 2, 0), A::Read);
    EXPECT_EQ(RamFlag(*a, 2, 1), A::Read | A::Write | A::Written);
    EXPECT_EQ(RamFlag(*a, 3, 0), A::Read);
    EXPECT_EQ(RamFlag(*a, 1, 0x2010), A::Read | A::Dac);
    // The CALL's return address: pushed at #FFFD-#FFFE (RAM page 3, SP #FFFF after the reset) and read back by RET
    EXPECT_EQ(RamFlag(*a, 3, 0x3FFD), A::Read | A::Write | A::Written);
    EXPECT_EQ(RamFlag(*a, 3, 0x3FFE), A::Read | A::Write | A::Written);
    EXPECT_EQ(Count(*a, A::Ram, A::Read), 6u);
    EXPECT_EQ(Count(*a, A::Ram, A::Write), 3u);
    EXPECT_EQ(Count(*a, A::Ram, A::Dac), 1u);
    EXPECT_EQ(Count(*a, A::Rom, A::Read | A::Write), 0u);

    // Sessions OR-merge: a second run of the same program adds nothing
    card.chip->reset();
    card.frame();
    EXPECT_EQ(Count(*a, A::Ram, A::Exec | A::Operand | A::Read | A::Write | A::Dac | A::Sub), starts.size() +
                                                                                               operands.size() + 6u);
}

TEST(GSCardAnalysis_Test, NeverWrittenReadsCarryThePcAndACardResetStartsThemAgain)
{
    ClassicCard card;
    card.loadProgram();
    const auto a = card.on();
    card.chip->reset();
    card.frame();

    uint64_t dropped = 0;
    std::vector<A::UninitRead> reads;
    for (const A::UninitRead& r : a->UninitRows(dropped))
        if (!r.fetch)
            reads.push_back(r);
    EXPECT_EQ(dropped, 0u);
    ASSERT_EQ(reads.size(), 3u) << "#8000, #6010 and #C000; #8001 was written first";
    EXPECT_EQ(reads[0].address, 0x8000);
    EXPECT_EQ(reads[0].pc, 0x4004);
    EXPECT_EQ(reads[0].offset, 2 * kPage);
    EXPECT_EQ(reads[1].address, 0x6010);
    EXPECT_EQ(reads[1].pc, 0x400D);
    EXPECT_EQ(reads[2].address, 0xC000);
    EXPECT_EQ(reads[2].pc, 0x4020) << "the instruction's PC, not the caller's";
    EXPECT_EQ(reads[2].offset, 3 * kPage);

    // The card reset: the stamps and the rows start again, the code / data marks stay
    card.chip->resetCard();
    EXPECT_TRUE(a->UninitRows(dropped).empty());
    EXPECT_EQ(Count(*a, A::Ram, A::Written), 0u);
    EXPECT_EQ(RamFlag(*a, 2, 1), A::Read | A::Write) << "written before the reset: still a write";
    EXPECT_EQ(a->CardResets(), 1u) << "resetCard(); the reset() before the first run came before the binding";
    card.frame();
    size_t again = 0;
    for (const A::UninitRead& r : a->UninitRows(dropped))
        again += !r.fetch;
    EXPECT_EQ(again, 3u) << "each byte once per card reset";

    a->Clear();
    EXPECT_EQ(Count(*a, A::Ram, 0xFF), 0u);
    EXPECT_EQ(Count(*a, A::Rom, 0xFF), 0u);
    EXPECT_TRUE(a->UninitRows(dropped).empty());
    EXPECT_EQ(a->HeatAt(A::DacFetch, (kPage + 0x2010) / a->CellBytes(), a->Frame()), 0);
}

TEST(GSCardAnalysis_Test, DacFetchesGlowInTheHeatMapAndDecay)
{
    ClassicCard card;
    card.loadProgram();
    const auto a = card.on();
    card.chip->reset();
    card.frame();
    const uint64_t fetched = card.ctx.emulatorState.frame_counter - 1;  // the frame the program ran in

    const uint32_t cell = (kPage + 0x2010) / a->CellBytes();
    EXPECT_EQ(a->CellBytes(), 16u) << "512 KB in 16-byte cells";
    EXPECT_EQ(a->HeatAt(A::DacFetch, cell, fetched), A::kHeatBump);
    EXPECT_EQ(a->HeatAt(A::DacFetch, cell + 1, fetched), 0);
    EXPECT_EQ(a->HeatAt(A::DataRead, cell, fetched), A::kHeatBump) << "a DAC fetch is a CPU read too";
    EXPECT_EQ(a->HeatAt(A::DacFetch, cell, fetched + 2), A::kHeatBump - 2 * A::kDefaultDecay);
    EXPECT_EQ(a->HeatAt(A::DacFetch, cell, fetched + 4), 0) << "dark after bump / decay frames";
    EXPECT_EQ(a->Accesses(A::DacFetch), 1u);
    uint64_t last = 0;
    ASSERT_TRUE(a->LastTouch(A::DacFetch, cell, last));
    EXPECT_EQ(last, fetched);

    // A slower decay keeps it longer
    a->SetDecay(1);
    EXPECT_EQ(a->HeatAt(A::DacFetch, cell, fetched + 4), A::kHeatBump - 4);
}

/// gs105a's POST runs before it has a stack (no CALL: it tests the RAM first): instruction starts and jump targets
TEST(GSCardAnalysis_Test, TheRealFirmwareMarksItsInstructionsAndJumpTargets)
{
    ClassicCard card;
    card.chip->loadROM("rom/gs105a.rom");
    const auto a = card.on();
    card.chip->reset();
    for (int i = 0; i < 30; i++)
        card.frame();
    EXPECT_GT(Count(*a, A::Rom, A::Exec), 100u);
    EXPECT_GT(Count(*a, A::Rom, A::Jump), 0u);
    EXPECT_TRUE(a->Flags(A::Rom)[0] & A::Exec) << "the reset vector";
    EXPECT_TRUE(a->Flags(A::Rom)[0x0148] & A::Jump) << "JP #0148 at #0001";
    EXPECT_EQ(Count(*a, A::Rom, A::Written), 0u) << "a write to a ROM window is marked, never stamped as written";
}

TEST(GSCardAnalysis_Test, NeoGSCodeCopiedToRamIsKeyedByItsPhysicalPage)
{
    EmulatorContext ctx{LoggerLevel::LogError};
    Configure(ctx);
    NeoGSConfig config;
    auto chip = std::make_unique<SoundChip_NeoGS>(&ctx, config, 44100);
    // Flash page 0: copy the 6-byte routine at #0010 to #4000 (window 1, RAM) and jump there
    std::vector<uint8_t> program = {
        0xF3,                    // DI
        0x21, 0x10, 0x00,        // LD HL,#0010
        0x11, 0x00, 0x40,        // LD DE,#4000
        0x01, 0x06, 0x00,        // LD BC,6
        0xED, 0xB0,              // LDIR
        0xC3, 0x00, 0x40,        // JP #4000
        0x00,
        0x3E, 0x05,              // #0010 LD A,5
        0x00, 0x00,              // NOP : NOP
        0x18, 0xFE};             // JR $
    chip->flash().load(program.data(), program.size());
    std::string error;
    ASSERT_TRUE(chip->setAnalysisOn(true, &ctx.emulatorState.frame_counter, &error)) << error;
    const auto a = chip->analysis();
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->RomBytes(), 512u * 1024) << "the flash";
    chip->reset();
    for (int i = 0; i < 2; i++)
    {
        chip->handleFrameStart();
        chip->handleFrameEnd(SAMPLES_PER_FRAME);
        ctx.emulatorState.frame_counter++;
    }
    ASSERT_TRUE(chip->analysisBound());
    ASSERT_FALSE(chip->memory().isFlash(1));
    const uint32_t base = chip->memory().physical(0x4000);
    ASSERT_EQ(base % kPage, 0u);
    const uint8_t* ram = a->Flags(A::Ram);

    // RAM: written by the LDIR (stamped), then run there; keyed by the physical page window 1 maps
    for (uint32_t o : {0u, 2u, 3u, 4u})
        EXPECT_TRUE(ram[base + o] & A::Exec) << o;
    for (uint32_t o : {1u, 5u})
        EXPECT_EQ(ram[base + o] & (A::Exec | A::Operand), A::Operand) << o;
    for (uint32_t o = 0; o < 6; o++)
        EXPECT_TRUE((ram[base + o] & (A::Write | A::Written)) == (A::Write | A::Written)) << o;
    EXPECT_TRUE(ram[base] & A::Jump) << "the JP target";
    EXPECT_EQ(Count(*a, A::Ram, A::Exec), 4u);
    EXPECT_EQ(Count(*a, A::Ram, A::Exec, base, base + kPage), 4u) << "all on that page";
    EXPECT_EQ(Count(*a, A::Ram, A::Write), 6u);
    uint64_t dropped = 0;
    EXPECT_TRUE(a->UninitRows(dropped).empty()) << "the copy was written before it ran";

    // Flash page 0: the copier ran, the routine's source bytes were read, the flash was not written
    const uint8_t* flash = a->Flags(A::Rom);
    for (uint32_t o : {0x00u, 0x01u, 0x04u, 0x07u, 0x0Au, 0x0Cu})
        EXPECT_TRUE(flash[o] & A::Exec) << o;
    for (uint32_t o = 0x10; o < 0x16; o++)
        EXPECT_EQ(flash[o], A::Read) << o;
    EXPECT_EQ(Count(*a, A::Rom, A::Write), 0u);
    EXPECT_EQ(Count(*a, A::Rom, A::Jump), 0u) << "the LDIR's repeats are not jumps";
}

TEST(GSCardAnalysis_Test, BusMasterWritesStampAndGlowInTheirChannel)
{
    uint64_t frame = 7;
    GSCardAnalysis a(32 * 1024, 512 * 1024, &frame);
    a.BusMaster(A::ZxDma, 0x13FF8, 16, true);
    a.BusMaster(A::SdDma, 0x20000, 512, false);
    EXPECT_EQ(Count(a, A::Ram, A::Write | A::Written), 16u);
    EXPECT_EQ(a.Flags(A::Ram)[0x13FF8], A::Write | A::Written);
    EXPECT_EQ(Count(a, A::Ram, 0xFF, 0x20000, 0x20200), 0u) << "a DMA read marks no CDL flag";
    EXPECT_EQ(a.HeatAt(A::ZxDma, 0x13FF8 / 16, frame), 8 * A::kHeatBump > 255 ? 255 : 8 * A::kHeatBump)
        << "eight bytes in the cell";
    EXPECT_EQ(a.HeatAt(A::SdDma, 0x20000 / 16, frame), 255) << "16 bytes a cell saturate";
    EXPECT_EQ(a.Accesses(A::SdDma), 512u);
    // A card reset clears the stamps, keeps the writes
    a.CardReset();
    EXPECT_EQ(Count(a, A::Ram, A::Written), 0u);
    EXPECT_EQ(Count(a, A::Ram, A::Write), 16u);
}

// region <The reports and the switch on a Pentagon>

namespace
{
class GSAnalysisReport_Test : public ::testing::Test
{
protected:
    SoundCardScope _gs{TestSound::GeneralSound};
    Emulator* _emulator = nullptr;
    EmulatorContext* _ctx = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON");
        ASSERT_NE(_emulator, nullptr);
        _ctx = _emulator->GetContext();
        Z80* z80 = _ctx->pCore->GetZ80();
        z80->DirectWrite(0x8000, 0xF3);  // DI : JR $ - the host leaves the card alone
        z80->DirectWrite(0x8001, 0x18);
        z80->DirectWrite(0x8002, 0xFE);
        z80->pc = 0x8000;
        z80->iff1 = z80->iff2 = 0;
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void frame() { reinterpret_cast<MainLoop_CUT*>(_ctx->pMainLoop)->RunFrame(); }

    GSAnalysisReply control(const std::string& action, const std::string& cpu = {}, int decay = -1)
    {
        GSAnalysisControlRequest request;
        request.action = action;
        request.cpu = cpu;
        request.decay = decay;
        return GSAnalysisControl(_ctx, request);
    }
};
}  // namespace

TEST_F(GSAnalysisReport_Test, TheLightweightGsIsNotAvailable)
{
    ASSERT_TRUE(FitGeneralSoundCard(_ctx->pSoundManager, GSTypeKind::LW));
    const StateNode coverage = DeviceState::GsCoverage(_ctx);
    EXPECT_FALSE(Field(coverage, "available").b);
    EXPECT_NE(Field(coverage, "description").s.find("not available"), std::string::npos)
        << Field(coverage, "description").s;
    EXPECT_FALSE(Field(DeviceState::GsHeatmap(_ctx), "available").b);
    const GSAnalysisReply on = control("on");
    EXPECT_EQ(on.status, 400);
    EXPECT_NE(Field(on.body, "error").s.find("not available"), std::string::npos);
    const StateNode catalog = DeviceState::Catalog(_ctx);
    for (const StateNode& entry : Field(catalog, "reports").items)
        EXPECT_NE(Field(entry, "id").s, "gs-coverage") << "listed only with a card CPU";
}

TEST_F(GSAnalysisReport_Test, TheSwitchTheCoverageAndTheHandlersRunOnTheRealFirmware)
{
    ASSERT_TRUE(FitGeneralSoundCard(_ctx->pSoundManager, GSTypeKind::Z80));
    GeneralSoundCard* card = _ctx->pSoundManager->getGeneralSound();
    ASSERT_NE(card, nullptr);

    StateNode status = DeviceState::GsAnalysisStatus(_ctx);
    ASSERT_TRUE(Field(status, "available").b) << Field(status, "description").s;
    EXPECT_FALSE(Field(status, "enabled").b);
    EXPECT_FALSE(Field(status, "allocated").b);
    EXPECT_NE(Field(status, "text").s.find("gs_analysis"), std::string::npos) << "how to start it";
    EXPECT_FALSE(Field(DeviceState::GsCoverage(_ctx), "allocated").b);

    EXPECT_EQ(control("bogus").status, 400);
    EXPECT_EQ(control("on", "zz").status, 404);
    EXPECT_EQ(control("on", "ms").status, 404) << "no MultiSound fitted";
    GSAnalysisReply on = control("on", "", 4);
    ASSERT_EQ(on.status, 200) << Field(on.body, "error").s;
    EXPECT_TRUE(Field(on.body, "enabled").b);
    EXPECT_EQ(Field(on.body, "decay").i, 4);
    EXPECT_GT(Field(on.body, "memory_bytes").i, 512 * 1024);

    // The host sends #20 (total RAM) until the booted firmware takes it (its start clears the command flag): the
    // handler read from the firmware's own dispatch table runs
    const auto handlerRan = [](const StateNode& coverage, int64_t command) {
        for (const StateNode& h : Field(Field(coverage, "handlers"), "executed").items)
            if (Field(h, "command").i == command)
                return true;
        return false;
    };
    GSAnalysisQuery query;
    StateNode coverage;
    for (int i = 0; i < 400; i += 10)
    {
        card->portDeviceOutMethod(GeneralSoundCard::PORT_COMMAND, 0x20);
        for (int f = 0; f < 10; f++)
            frame();
        coverage = DeviceState::GsCoverage(_ctx, query);
        if (handlerRan(coverage, 0x20))
            break;
    }
    EXPECT_TRUE(Field(DeviceState::GsAnalysisStatus(_ctx), "recording").b);

    ASSERT_TRUE(Field(coverage, "available").b);
    const StateNode& spaces = Field(coverage, "spaces");
    ASSERT_EQ(spaces.size(), 2u);
    EXPECT_EQ(Field(spaces.items[0], "space").s, "rom");
    EXPECT_EQ(Field(spaces.items[0], "pages").i, 2);
    EXPECT_GT(Field(Field(spaces.items[0], "totals"), "exec").i, 100);
    EXPECT_GT(Field(Field(spaces.items[1], "totals"), "write").i, 0) << "the firmware's variables and buffers";
    const StateNode& rom0 = Field(spaces.items[0], "page_list").items.at(0);
    EXPECT_EQ(Field(rom0, "page").i, 0);
    EXPECT_GT(Field(rom0, "code_ranges").size(), 0u);

    const StateNode& handlers = Field(coverage, "handlers");
    ASSERT_TRUE(Field(handlers, "available").b) << Field(handlers, "description").s;
    EXPECT_GT(Field(handlers, "entries").i, 10);
    EXPECT_TRUE(handlerRan(coverage, 0x20)) << Field(coverage, "text").s;
    EXPECT_NE(Field(coverage, "text").s.find("handlers executed"), std::string::npos);

    // One page with its raw flags; bad parameters
    std::string error;
    GSAnalysisQuery one;
    ASSERT_TRUE(GSAnalysisQueryFromParams(
        [](const std::string& key) -> std::string {
            return key == "space" ? "rom" : key == "page" ? "0" : key == "raw" ? "1" : "";
        },
        one, error))
        << error;
    const StateNode page = DeviceState::GsCoverage(_ctx, one);
    ASSERT_EQ(Field(page, "spaces").size(), 1u);
    const StateNode& p = Field(Field(page, "spaces").items[0], "page_list").items.at(0);
    EXPECT_EQ(Field(p, "flags").s.size(), 21848u) << "16,384 bytes in base64";
    EXPECT_TRUE(p.find("sub_entries")) << "one page asked: its CALL / RST entries";
    GSAnalysisQuery bad;
    EXPECT_FALSE(GSAnalysisQueryFromParams([](const std::string& key) { return key == "page" ? "1" : ""; }, bad, error))
        << "page needs space";
    EXPECT_FALSE(GSAnalysisQueryFromParams([](const std::string& key) { return key == "channels" ? "dac,x" : ""; },
                                           bad, error));

    // The heat map: the firmware's fetches glow; the grid in base64 and hex
    GSAnalysisQuery heat;
    heat.cells = 256;
    heat.recent = {A::Fetch, A::DataWrite};
    StateNode map = DeviceState::GsHeatmap(_ctx, heat);
    ASSERT_TRUE(Field(map, "available").b);
    EXPECT_EQ(Field(map, "data_cells").i, 256);
    EXPECT_EQ(Field(map, "columns").i, 16);
    const StateNode& channels = Field(map, "channels");
    ASSERT_EQ(channels.size(), static_cast<size_t>(A::ChannelCount));
    EXPECT_EQ(Field(channels.items[0], "name").s, "fetch");
    EXPECT_GT(Field(channels.items[A::DataWrite], "hot_cells").i, 0);
    EXPECT_EQ(Field(channels.items[0], "data").s.size(), 344u) << "256 cells in base64";
    EXPECT_EQ(Field(Field(Field(map, "recent"), "channels").items.at(1), "name").s, "write");
    EXPECT_GT(Field(Field(Field(map, "recent"), "channels").items.at(1), "ranges").size(), 0u);
    heat.format = "hex";
    map = DeviceState::GsHeatmap(_ctx, heat);
    EXPECT_EQ(Field(Field(map, "channels").items[0], "rows").size(), 16u);

    // The catalog lists both; reset clears; release frees
    bool listed = false;
    const StateNode catalog = DeviceState::Catalog(_ctx);
    for (const StateNode& entry : Field(catalog, "reports").items)
        listed = listed || Field(entry, "id").s == "gs-heatmap";
    EXPECT_TRUE(listed) << DeviceState::CatalogText(catalog);
    ASSERT_EQ(control("reset").status, 200);
    coverage = DeviceState::GsCoverage(_ctx);
    EXPECT_EQ(Field(Field(Field(coverage, "spaces").items[0], "totals"), "exec").i, 0);
    ASSERT_EQ(control("release").status, 200);
    EXPECT_FALSE(Field(DeviceState::GsAnalysisStatus(_ctx), "allocated").b);
}

TEST_F(GSAnalysisReport_Test, ZxDmaUploadsShowAsWritesOnTheNeoGs)
{
    ASSERT_TRUE(FitGeneralSoundCard(_ctx->pSoundManager, GSTypeKind::NGS));
    auto* card = dynamic_cast<SoundChip_NeoGS*>(_ctx->pSoundManager->getGeneralSound());
    ASSERT_NE(card, nullptr);
    constexpr uint32_t kCardBase = 0x013FF8;
    const std::vector<uint8_t> program = {
        0xF3,                                       // DI
        0x3E, 0x01, 0xD3, 0x1B,                     // DMA_MOD = 1 (ZX)
        0x3E, uint8_t(kCardBase >> 16), 0xD3, 0x1C, // HAD
        0x3E, uint8_t(kCardBase >> 8), 0xD3, 0x1D,  // MAD
        0x3E, uint8_t(kCardBase), 0xD3, 0x1E,       // LAD
        0x3E, 0x80, 0xD3, 0x1F,                     // CST = #80: start
        0x18, 0xFE};                                // JR $
    card->flash().load(program.data(), program.size());
    ASSERT_EQ(control("on").status, 200);
    card->reset();
    frame();
    frame();
    ASSERT_EQ(card->zxDma().mode(), NeoGSZxDma::Mode::Divert);

    // The host LDIRs 16 bytes into the window
    Z80* z80 = _ctx->pCore->GetZ80();
    const std::vector<uint8_t> ldir = {0x21, 0x00, 0x90, 0x11, 0x00, 0x00, 0x01, 16, 0x00, 0xED, 0xB0, 0x18, 0xFE};
    for (size_t i = 0; i < ldir.size(); i++)
        z80->DirectWrite(static_cast<uint16_t>(0x8100 + i), ldir[i]);
    z80->pc = 0x8100;
    for (int guard = 0; guard < 10 && z80->pc != 0x8100 + 11; guard++)
        frame();
    ASSERT_EQ(z80->pc, 0x8100 + 11);

    const auto a = card->analysis();
    ASSERT_NE(a, nullptr);
    for (uint32_t i = 0; i < 16; i++)
        EXPECT_EQ(a->Flags(A::Ram)[kCardBase + i], A::Write | A::Written) << i;
    EXPECT_EQ(a->Accesses(A::ZxDma), 16u);

    GSAnalysisQuery query;
    query.recent = {A::ZxDma};
    const StateNode map = DeviceState::GsHeatmap(_ctx, query);
    const StateNode& ranges = Field(Field(Field(map, "recent"), "channels").items.at(0), "ranges");
    ASSERT_EQ(ranges.size(), 1u) << Field(map, "text").s;
    EXPECT_EQ(Field(ranges.items[0], "start").i, kCardBase);
    EXPECT_EQ(Field(ranges.items[0], "end").i, kCardBase + 15);
    EXPECT_EQ(Field(ranges.items[0], "page").i, kCardBase / kPage);
}

// endregion </The reports and the switch on a Pentagon>
