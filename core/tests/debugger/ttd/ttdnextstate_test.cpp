/// @file ttdnextstate_test.cpp
/// @brief Time travel on the ZX Next (PeripheralId::NextState, nextstatecodec.h): a program writes NextREGs while the engine's controller
/// records; seeking to each checkpoint gives the NextREG file, the MMU slot and the sprite upload latch of that moment, and the image
/// round-trips on its own. Boots a Next and records 8 frames: slower than the 50 ms guideline, one acceptance check.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/assembler/z80textassembler.h"
#include "debugger/ttd/next/ttdnextstate.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextboard.h"
#include "emulator/io/z80n/nextstatecodec.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"

namespace
{
constexpr uint8_t kRegScratch = 0x16;        // NR #16 stores its byte
constexpr unsigned kSlot = 2;                // an MMU slot the program does not run from (0x4000)
constexpr uint16_t kProgram = 0x8000;
constexpr int kFrames = 8;

struct Moment
{
    uint8_t scratch = 0;
    uint8_t mmu = 0;
    uint8_t selected = 0;
    bool operator==(const Moment& o) const { return scratch == o.scratch && mmu == o.mmu && selected == o.selected; }
};
}  // namespace

TEST(TTDNextState_Test, TheImageRoundTripsOnItsOwn)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    struct Cleanup
    {
        Emulator* e;
        ~Cleanup() { EmulatorTestHelper::CleanupEmulator(e); }
    } cleanup{emulator};
    auto* decoder = dynamic_cast<PortDecoder_Next*>(emulator->GetContext()->pPortDecoder);
    ASSERT_NE(decoder, nullptr);

    const std::vector<uint8_t> before = NextStateCodec::Save(*decoder);
    decoder->Board().Write(kRegScratch, 0x77);
    decoder->Board().Write(NextBoard::kRegMmu0 + kSlot, 0x20);
    const std::vector<uint8_t> changed = NextStateCodec::Save(*decoder);
    EXPECT_EQ(changed.size(), before.size()) << "a fixed length";
    EXPECT_NE(changed, before);
    ASSERT_TRUE(NextStateCodec::Load(*decoder, before.data(), before.size()));
    EXPECT_EQ(NextStateCodec::Save(*decoder), before) << "the load put everything back";
    EXPECT_NE(decoder->Board().Stored(kRegScratch), 0x77);
    EXPECT_FALSE(NextStateCodec::Load(*decoder, before.data(), before.size() - 1)) << "another length is refused";
}

TEST(TTDNextState_Test, TheSoundAndTheSystemAreaTravelToo)
{
    constexpr uint16_t kPortAyRegister = 0xFFFD, kPortAyData = 0xBFFD;
    constexpr uint8_t kAyRegVolumeA = 8, kVolume = 0x0B;
    constexpr uint16_t kPc = 0x8000;
    constexpr uint8_t kByte = 0x5A;
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    struct Cleanup
    {
        Emulator* e;
        ~Cleanup() { EmulatorTestHelper::CleanupEmulator(e); }
    } cleanup{emulator};
    EmulatorContext* context = emulator->GetContext();
    auto* decoder = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);

    // the AY: a register written through the sound ports is in the image and comes back
    const std::vector<uint8_t> before = NextStateCodec::Save(*decoder);
    decoder->DecodePortOut(kPortAyRegister, kAyRegVolumeA, kPc);
    decoder->DecodePortOut(kPortAyData, kVolume, kPc);
    const std::vector<uint8_t> changed = NextStateCodec::Save(*decoder);
    EXPECT_NE(changed, before) << "the AY register is part of the image";
    ASSERT_TRUE(NextStateCodec::Load(*decoder, before.data(), before.size()));
    EXPECT_EQ(NextStateCodec::Save(*decoder), before);

    // the system area has its own blob, plain bytes: a ROM page the firmware loads in config mode and the DivMMC RAM both travel
    ttd::TTDNextSystemArea area(*context->pMemory);
    std::vector<uint8_t> image(area.TTDStateSize());
    area.TTDSaveState(image.data());
    uint8_t* romPage = context->pMemory->ROMPageHostAddress(0);
    uint8_t* divMmcRam = context->pMemory->ROMPageHostAddress(ttd::TTDNextSystemArea::kDivMmcRamPage);
    const uint8_t romOriginal = romPage[0x10], ramOriginal = divMmcRam[0x10];
    romPage[0x10] = static_cast<uint8_t>(romOriginal ^ kByte);
    divMmcRam[0x10] = static_cast<uint8_t>(ramOriginal ^ kByte);
    const uint64_t changedHash = area.TTDHashState();
    area.TTDLoadState(image.data());
    EXPECT_EQ(romPage[0x10], romOriginal) << "the load put the ROM page back (a config-mode load of the firmware)";
    EXPECT_EQ(divMmcRam[0x10], ramOriginal) << "and the DivMMC RAM";
    EXPECT_NE(area.TTDHashState(), changedHash) << "the divergence hash sees the area";
    EXPECT_EQ(area.TTDStateSize(), ttd::TTDNextSystemArea::kBytes);
}

TEST(TTDNextState_Test, SeeksRestoreTheNextRegisterFileAndTheMemoryMap)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    struct Cleanup
    {
        Emulator* e;
        ~Cleanup() { EmulatorTestHelper::CleanupEmulator(e); }
    } cleanup{emulator};

    EmulatorContext* context = emulator->GetContext();
    emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    auto* decoder = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    auto* memory = static_cast<NextMemory*>(context->pMemory);

    // NEXTREG n,A is ED 92 n (the assembler has no Z80N mnemonics). Counts up forever: NR #16 gets the counter and the MMU register of slot 2 its low bits, so every checkpoint differs
    const std::string source = R"(
        DI
        LD A,0
LOOP    DB #ED,#92,#16
        DB #ED,#92,#52
        INC A
        AND #1F
        JR LOOP
)";
    Z80TextAssembler assembler;
    const AsmResult code = assembler.Assemble(source, kProgram);
    ASSERT_TRUE(code.ok) << code.error.line << ": " << code.error.message << " | " << code.error.sourceLine;
    for (size_t i = 0; i < code.bytes.size(); ++i)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kProgram + i), code.bytes[i]);
    Z80* z80 = context->pCore->GetZ80();
    z80->pc = kProgram;
    z80->iff1 = z80->iff2 = 0;

    auto controller = std::make_unique<ttd::TimeTravelController>(context);
    context->pTimeTravelHooks = controller.get();
    context->ttdWriteSink = controller.get();
    struct Detach
    {
        EmulatorContext* c;
        ttd::TimeTravelController* t;
        ~Detach()
        {
            t->StopRecording();
            c->pTimeTravelHooks = c->pTimeTravelController;
            c->ttdWriteSink = c->pTimeTravelController;
        }
    } detach{context, controller.get()};

    ASSERT_TRUE(controller->StartRecording());
    auto now = [&]() {
        Moment m;
        m.scratch = decoder->Board().Stored(kRegScratch);
        m.mmu = memory->GetMmu(kSlot);
        m.selected = decoder->Board().SelectedRegister();
        return m;
    };
    std::vector<Moment> live;
    live.push_back(now());
    for (int f = 0; f < kFrames; ++f)
    {
        emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        live.push_back(now());
    }
    controller->StopRecording();

    const size_t count = controller->GetCheckpointCount();
    ASSERT_GE(count, 3u);
    // The values at the end differ from those at the start (the program ran), and the checkpoints hold the moments in between
    ASSERT_FALSE(live.front() == live.back()) << "the program changed the registers";
    const uint64_t firstFrame = controller->GetCheckpoint(0)->time.frame;
    size_t compared = 0;
    for (size_t i = count; i-- > 0;)  // newest first: a restore that does nothing would keep the end values and fail the older ones
    {
        const uint64_t frame = controller->GetCheckpoint(i)->time.frame;
        ASSERT_TRUE(controller->SeekTo(ttd::TTDTimePoint{frame, 0})) << "checkpoint " << i;
        const uint64_t index = frame - firstFrame;  // one checkpoint per frame boundary: the live moment of that frame
        if (index >= live.size())
            continue;
        const Moment m = now();
        EXPECT_TRUE(m == live[index]) << "checkpoint " << i << " (frame " << index << "): NR #16 = " << int(m.scratch) << " (live "
                                      << int(live[index].scratch) << "), slot " << kSlot << " = " << int(m.mmu) << " (live "
                                      << int(live[index].mmu) << ")";
        compared++;
    }
    EXPECT_GE(compared, 3u);
    // Back to the first checkpoint: the registers are the first ones again
    ASSERT_TRUE(controller->SeekTo(ttd::TTDTimePoint{controller->GetCheckpoint(0)->time.frame, 0}));
    EXPECT_TRUE(now() == live.front());
}
