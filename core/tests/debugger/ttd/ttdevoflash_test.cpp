/// @file ttdevoflash_test.cpp
/// @brief Time travel over ZX-Evo flash writes (evoflash.h, TTDEvoFlash): a TS-Conf program programs a ROM byte
/// and starts a sector erase while the engine's controller records; seeking to each checkpoint gives the ROM bytes
/// (engine region EvoFlash) and the chip's command state (blob EvoFlash) of that moment, and seeking back after the
/// end undoes the flash writes.
///
/// Boots a TS-Conf and records 12 frames: slower than the 50 ms guideline, one acceptance check.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/assembler/z80textassembler.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/atm/evoflash.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_tsconf.h"

namespace
{
struct Moment
{
    uint8_t programmed = 0;  ///< the byte the program writes
    uint8_t erased = 0;      ///< a byte of the sector being erased
    bool arrayMode = true;
};
}  // namespace

TEST(TTDEvoFlash_Test, SeeksRestoreTheFlashBytesAndTheChipState)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("TSL", LoggerLevel::LogError);
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
    auto* decoder = dynamic_cast<PortDecoder_TSConf*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    Memory* memory = context->pMemory;
    uint8_t* rom = memory->ROMBase();

    // The byte to program (page 8) and a byte of the sector to erase (sector 4 = pages 16..19), both with a bit set
    uint32_t programAt = 8 * PAGE_SIZE + 0x0100;
    while (rom[programAt] == 0x00)
        ++programAt;
    uint32_t eraseAt = 16 * PAGE_SIZE;
    while (rom[eraseAt] == 0xFF && eraseAt < 20 * PAGE_SIZE)
        ++eraseAt;
    ASSERT_LT(eraseAt, 20u * PAGE_SIZE) << "the TS-Conf ROM has data in pages 16..19";
    const uint8_t original = rom[programAt];
    const uint8_t value = static_cast<uint8_t>(original & (original - 1));
    const uint8_t sectorByte = rom[eraseAt];

    // A program in RAM (window 2): about 3 frames idle, program the byte and poll DQ6, about 1 frame idle, start
    // the sector erase, idle (the erase runs 1 s, past the recording)
    const std::string source = "PAGE EQU " + std::to_string(programAt / PAGE_SIZE) + "\nADDR EQU " +
                               std::to_string(programAt % PAGE_SIZE) + "\nVALUE EQU " + std::to_string(value) + R"(
        DI
        LD BC,8000
W1      DEC BC
        LD A,B
        OR C
        JR NZ,W1
        LD BC,#10AF
        XOR A
        OUT (C),A
        LD A,#AA
        LD (#0555),A
        LD A,#55
        LD (#02AA),A
        LD A,#A0
        LD (#0555),A
        LD A,PAGE
        OUT (C),A
        LD A,VALUE
        LD (ADDR),A
P1      LD A,(ADDR)
        LD D,A
        LD A,(ADDR)
        XOR D
        BIT 6,A
        JR NZ,P1
        LD BC,3000
W2      DEC BC
        LD A,B
        OR C
        JR NZ,W2
        LD BC,#10AF
        XOR A
        OUT (C),A
        LD A,#AA
        LD (#0555),A
        LD A,#55
        LD (#02AA),A
        LD A,#80
        LD (#0555),A
        LD A,#AA
        LD (#0555),A
        LD A,#55
        LD (#02AA),A
        LD A,16
        OUT (C),A
        LD A,#30
        LD (#0000),A
IDLE    JR IDLE
)";
    Z80TextAssembler assembler;
    const AsmResult code = assembler.Assemble(source, 0x8000);
    ASSERT_TRUE(code.ok) << code.error.line << ": " << code.error.message << " | " << code.error.sourceLine;

    decoder->DecodePortOut(0x12AF, 0x02, 0x0000);  // PAGE2 = RAM 2
    decoder->DecodePortOut(0x21AF, 0x06, 0x0000);  // MEM_CONFIG: normal mode, ROM, W0_WE
    for (size_t i = 0; i < code.bytes.size(); ++i)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code.bytes[i]);
    Z80* z80 = context->pCore->GetZ80();
    z80->pc = 0x8000;
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
            c->pTimeTravelHooks = c->pTimeTravelManager;
            c->ttdWriteSink = c->pTimeTravelManager;
        }
    } detach{context, controller.get()};

    ASSERT_TRUE(controller->StartRecording());
    std::vector<Moment> live;
    auto now = [&]() {
        Moment m;
        m.programmed = rom[programAt];
        m.erased = rom[eraseAt];
        m.arrayMode = decoder->GetFlash().Chip().arrayMode();
        return m;
    };
    live.push_back(now());
    for (int f = 0; f < 12; ++f)
    {
        emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        live.push_back(now());
    }
    controller->StopRecording();
    ASSERT_EQ(live.back().programmed, value) << "the program ran";
    ASSERT_FALSE(live.back().arrayMode) << "the erase is running at the end";
    ASSERT_EQ(live.back().erased, sectorByte) << "and not finished";

    const size_t count = controller->GetCheckpointCount();
    ASSERT_GE(count, 4u);
    bool sawOriginal = false;
    bool sawProgrammedInArrayMode = false;
    for (size_t i = 0; i < count; ++i)
    {
        const ttd::TTDTimePoint at{controller->GetCheckpoint(i)->time.frame, 0};
        ASSERT_TRUE(controller->SeekTo(at)) << "checkpoint " << i;
        const Moment m = now();
        sawOriginal = sawOriginal || (m.programmed == original && m.arrayMode);
        sawProgrammedInArrayMode = sawProgrammedInArrayMode || (m.programmed == value && m.arrayMode);
        EXPECT_EQ(m.erased, sectorByte) << "checkpoint " << i << ": the erase never finished";
        if (m.programmed == original)
            EXPECT_TRUE(m.arrayMode) << "checkpoint " << i << ": nothing started before the program";
        EXPECT_EQ(decoder->GetFlash().IsInstalled(), !m.arrayMode || decoder->GetFlash().WriteWindows() != 0);
    }
    EXPECT_TRUE(sawOriginal) << "a checkpoint before the program";
    EXPECT_TRUE(sawProgrammedInArrayMode) << "a checkpoint between the program and the erase";

    // Back to the first checkpoint after the end: the programmed byte is undone, the chip reads the array
    ASSERT_TRUE(controller->SeekTo({controller->GetCheckpoint(count - 1)->time.frame, 0}));
    EXPECT_FALSE(decoder->GetFlash().Chip().arrayMode()) << "the last checkpoint is mid-erase";
    ASSERT_TRUE(controller->SeekTo({controller->GetCheckpoint(0)->time.frame, 0}));
    EXPECT_EQ(rom[programAt], original);
    EXPECT_TRUE(decoder->GetFlash().Chip().arrayMode());
}
