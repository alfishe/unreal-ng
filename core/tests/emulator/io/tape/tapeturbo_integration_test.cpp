#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/basic-lang/basicencoder.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/tape/tape.h"
#include "emulator/io/tape/tapeturbocontroller.h"
#include "emulator/mainloop.h"

/// Integration tests for turbo tape loading (design §9.2:
/// docs/inprogress/2026-09-04-turbo-tape-loading).
///
/// Every scenario runs the REAL machine: frames come from the real MainLoop
/// (which ticks the context-owned TapeTurboController right after the tape's
/// own frame end), signal playback starts through the actual ROM loader or a
/// typed EAR-poll loop, and the watchdog/resume machinery fires on its own.
/// The controller's decisions are observed only through the live Core turbo
/// flag and the public ownership query — no hand-set state anywhere.
///
/// The BASIC payload under test is the same tokenized program the fast-load
/// integration suite uses:
///   1 REM TAPETEST
///   2 PRINT 42
class TapeTurbo_Integration_Test : public ::testing::Test
{
protected:
    /// region <Payload builders>

    /// Tokenized `1 REM TAPETEST` + `2 PRINT 42` (23 bytes)
    static std::vector<uint8_t> MakeProgramBytes()
    {
        return {
            // Line 1: length 12 (LE), number 1 (BE), REM, "TAPETEST", ENTER
            0x0C, 0x00, 0x00, 0x01, 0xEA, 'T', 'A', 'P', 'E', 'T', 'E', 'S', 'T', 0x0D,
            // Line 2: length 7 (LE), number 2 (BE), PRINT, ' ', '4', '2', ENTER
            0x07, 0x00, 0x00, 0x02, 0xF4, 0x20, '4', '2', 0x0D
        };
    }

    /// Build one TAP block body: flag + payload + XOR checksum
    static std::vector<uint8_t> MakeTAPBlock(uint8_t flag, const std::vector<uint8_t>& payload)
    {
        std::vector<uint8_t> block;
        block.reserve(payload.size() + 2);

        uint8_t checksum = flag;
        block.push_back(flag);
        for (const uint8_t byte : payload)
        {
            block.push_back(byte);
            checksum ^= byte;
        }
        block.push_back(checksum);

        return block;
    }

    /// Standard vanilla pair for the test program: Program header + data block
    static std::vector<std::vector<uint8_t>> MakeProgramTAP()
    {
        const std::vector<uint8_t> program = MakeProgramBytes();

        std::vector<uint8_t> header;
        header.push_back(0x00);  // Program
        for (const char c : std::string("tapeturbo "))
            header.push_back(static_cast<uint8_t>(c));
        header.push_back(static_cast<uint8_t>(program.size() & 0xFF));
        header.push_back(static_cast<uint8_t>(program.size() >> 8));
        header.push_back(0x00);  // autostart line lo
        header.push_back(0x80);  // autostart line hi ($8000 = no autorun)
        header.push_back(0x00);  // vars start lo
        header.push_back(0x00);  // vars start hi

        return { MakeTAPBlock(0x00, header), MakeTAPBlock(0xFF, program) };
    }

    static std::string WriteTAPFile(const std::string& name, const std::vector<std::vector<uint8_t>>& blocks)
    {
        const std::string path = TestPathHelper::GetTestScratchPath("tapeturbo-int/" + name);

        FILE* file = fopen(path.c_str(), "wb");
        if (file == nullptr)
            return path;

        for (const std::vector<uint8_t>& block : blocks)
        {
            const uint16_t size = static_cast<uint16_t>(block.size());
            fputc(size & 0xFF, file);
            fputc((size >> 8) & 0xFF, file);
            fwrite(block.data(), 1, block.size(), file);
        }
        fclose(file);

        return path;
    }

    /// endregion </Payload builders>

    /// region <Machine helpers>

    // 48K sysvars used below
    static constexpr uint16_t SYS_ERR_NR = 23610;  // $5C3A
    static constexpr uint16_t SYS_PROG = 0x5C53;
    static constexpr uint16_t SYS_VARS = 0x5C5B;

    static uint16_t ReadWord(Memory* memory, uint16_t address)
    {
        return static_cast<uint16_t>(memory->DirectReadFromZ80Memory(address) |
                                     (memory->DirectReadFromZ80Memory(static_cast<uint16_t>(address + 1)) << 8));
    }

    /// The tokenized program sits at PROG; the ROM leaves the $80 vars
    /// end-marker as the last program-area byte and VARS points one past it
    static bool ProgramLoaded(Memory* memory)
    {
        const std::vector<uint8_t> program = MakeProgramBytes();
        const uint16_t prog = ReadWord(memory, SYS_PROG);
        const uint16_t vars = ReadWord(memory, SYS_VARS);

        if (vars != prog + program.size() + 1)
            return false;

        if (memory->DirectReadFromZ80Memory(static_cast<uint16_t>(prog + program.size())) != 0x80)
            return false;

        for (size_t i = 0; i < program.size(); i++)
        {
            if (memory->DirectReadFromZ80Memory(static_cast<uint16_t>(prog + i)) != program[i])
                return false;
        }

        return true;
    }

    /// endregion </Machine helpers>
};

/// §9.2-1: The full custom-loader lifecycle under the real MainLoop — the
/// design §4.1 walkthrough, live. Signal playback running -> warp engaged by
/// the per-frame tick; read-gap watchdog freeze (the loader stopped reading
/// while it processes) -> warp disengaged in the SAME frame; sustained EAR
/// polling resumes the deck -> warp re-engaged.
TEST_F(TapeTurbo_Integration_Test, WarpLifecycleWithCustomLoaderTiming)
{
    MessageCenter::DisposeDefaultMessageCenter();
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("Pentagon", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    Core* core = context->pCore;
    TapeTurboController* controller = context->pTapeTurboController;
    ASSERT_NE(controller, nullptr);

    // Pure signal path (fasttape off); turbo tape explicitly on regardless of
    // any features.ini sitting in the working directory
    ASSERT_TRUE(context->pFeatureManager->setFeature(Features::kFastTape, false));
    ASSERT_TRUE(context->pFeatureManager->setFeature(Features::kTurboTape, true));

    // Program pair + one large tail block: 4 KiB plays for ~1200 frames, so
    // the watchdog pauses MID-BLOCK, never at end-of-tape
    std::vector<std::vector<uint8_t>> blocks = MakeProgramTAP();
    blocks.push_back(MakeTAPBlock(0xFF, std::vector<uint8_t>(4096, 0xA5)));
    context->coreState.tapeFilePath = WriteTAPFile("lifecycle.tap", blocks);

    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
    for (int i = 0; i < 100; i++)
        mainLoop->RunFrame();

    // Signal-load the program pair through the real ROM loader; the tape
    // keeps rolling into the tail block
    auto load = BasicEncoder::runCommand(emulator, "LOAD \"\"");
    ASSERT_TRUE(load.success) << load.message;

    bool sawWarpWhilePlaying = false;
    int frames = 0;
    for (; frames < 1500; frames++)
    {
        mainLoop->RunFrame();
        if (context->pTape->IsPlaying() && core->IsTurboMode() && controller->IsAutoTurboActive())
            sawWarpWhilePlaying = true;
        if (ProgramLoaded(context->pMemory))
            break;
    }
    ASSERT_LT(frames, 1500) << "Signal load did not complete";
    ASSERT_TRUE(context->pTape->IsPlaying()) << "Tape must still be rolling past the loaded pair";
    EXPECT_TRUE(sawWarpWhilePlaying) << "Playing tape must be under warp (E1 engage, live MainLoop tick)";

    // A silent processing phase — DI'd delay loop with NO port reads (~380
    // frames), comfortably past the 150-frame watchdog; the same opcode
    // sequence the fast-load integration suite uses
    auto delay = BasicEncoder::runCommand(emulator,
        "POKE 30000,243:POKE 30001,6:POKE 30002,16:POKE 30003,33:POKE 30004,0:POKE 30005,0:POKE 30006,43:POKE 30007,124:POKE 30008,181:"
        "POKE 30009,32:POKE 30010,251:POKE 30011,16:POKE 30012,246:POKE 30013,251:POKE 30014,201:RANDOMIZE USR 30000");
    ASSERT_TRUE(delay.success) << delay.message;

    // Watchdog pauses mid-block; the controller ticks in the SAME frame end,
    // so warp must already be gone the moment the pause is observable
    bool paused = false;
    for (int i = 0; i < 600; i++)
    {
        mainLoop->RunFrame();
        if (!context->pTape->IsPlaying())
        {
            paused = true;
            break;
        }
    }
    ASSERT_TRUE(paused) << "Watchdog did not pause playback";
    EXPECT_FALSE(core->IsTurboMode()) << "Watchdog pause must drop warp in the same frame (E2)";
    EXPECT_FALSE(controller->IsAutoTurboActive());

    // Editor keyboard scanning keeps the pause stable — and must never bring
    // warp back on its own
    for (int i = 0; i < 350; i++)
        mainLoop->RunFrame();
    EXPECT_FALSE(context->pTape->IsPlaying()) << "Keyboard scan must not trip the sustained-poll threshold";
    EXPECT_FALSE(core->IsTurboMode()) << "No warp while paused, ever";

    // The loader's own poll loop resumes the deck — warp comes back with it
    auto poke = BasicEncoder::runCommand(emulator,
        "POKE 31000,62:POKE 31001,0:POKE 31002,219:POKE 31003,254:POKE 31004,24:POKE 31005,252:RANDOMIZE USR 31000");
    ASSERT_TRUE(poke.success) << poke.message;
    for (int i = 0; i < 15; i++)
        mainLoop->RunFrame();  // Settle ENTER: POKE chain must land first

    bool resumed = false;
    for (int i = 0; i < 30; i++)
    {
        mainLoop->RunFrame();
        if (context->pTape->IsPlaying())
        {
            resumed = true;
            break;
        }
    }
    ASSERT_TRUE(resumed) << "Sustained EAR polling must resume paused playback";
    EXPECT_TRUE(core->IsTurboMode()) << "Resumed playback must be under warp again (E1 re-engage)";
    EXPECT_TRUE(controller->IsAutoTurboActive());

    EmulatorTestHelper::CleanupEmulator(emulator);
    MessageCenter::DisposeDefaultMessageCenter();
}

/// §9.2-2: The DIZZY_X_CHEFRANOV shape — trap-eligible pair first, a
/// headerless tail the trap can never serve behind it. The trap phase must
/// run at normal speed with playback never started (warp is a signal-path
/// tool only); the headerless tail plays under warp once a consumer polls for
/// it; the natural end-of-tape stands warp down (E3 live).
TEST_F(TapeTurbo_Integration_Test, WarpServesHeaderlessTailAndEndsAtEndOfTape)
{
    MessageCenter::DisposeDefaultMessageCenter();
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("Pentagon", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    Core* core = context->pCore;
    TapeTurboController* controller = context->pTapeTurboController;
    ASSERT_NE(controller, nullptr);

    // fasttape on (default, explicit): the pair fast-loads without playback
    ASSERT_TRUE(context->pFeatureManager->setFeature(Features::kFastTape, true));
    ASSERT_TRUE(context->pFeatureManager->setFeature(Features::kTurboTape, true));

    // Vanilla pair + one large headerless tail (4 KiB, ~1200 frames of signal)
    std::vector<std::vector<uint8_t>> blocks = MakeProgramTAP();
    blocks.push_back(MakeTAPBlock(0xFF, std::vector<uint8_t>(4096, 0x5A)));
    context->coreState.tapeFilePath = WriteTAPFile("headerless-tail.tap", blocks);

    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
    for (int i = 0; i < 100; i++)
        mainLoop->RunFrame();

    // Phase 1: the trap serves the pair — no signal path, no warp, ever
    auto load = BasicEncoder::runCommand(emulator, "LOAD \"\"");
    ASSERT_TRUE(load.success) << load.message;

    int frames = 0;
    for (; frames < 60; frames++)
    {
        mainLoop->RunFrame();
        ASSERT_FALSE(context->pTape->IsPlaying()) << "Trap phase must never start playback";
        ASSERT_FALSE(core->IsTurboMode()) << "Trap phase must never engage warp";
        if (ProgramLoaded(context->pMemory))
            break;
    }
    ASSERT_LT(frames, 60) << "Fast load did not complete";
    ASSERT_EQ(context->pTape->GetConsumptionCursor(), 2u) << "Trap consumed exactly the pair";
    EXPECT_EQ(context->pMemory->DirectReadFromZ80Memory(SYS_ERR_NR), 0xFF);

    // Phase 2: a custom consumer polls for the headerless tail — playback
    // starts from the cursor and warp engages (the case the trap cannot serve)
    auto poke = BasicEncoder::runCommand(emulator,
        "POKE 31000,62:POKE 31001,0:POKE 31002,219:POKE 31003,254:POKE 31004,24:POKE 31005,252:RANDOMIZE USR 31000");
    ASSERT_TRUE(poke.success) << poke.message;
    for (int i = 0; i < 15; i++)
        mainLoop->RunFrame();  // Settle ENTER: POKE chain must land first

    bool warpDuringTail = false;
    bool reachedEnd = false;
    for (int i = 0; i < 1600; i++)
    {
        mainLoop->RunFrame();
        if (context->pTape->IsPlaying() && core->IsTurboMode() && controller->IsAutoTurboActive())
            warpDuringTail = true;
        if (!context->pTape->IsPlaying() && context->pTape->GetConsumptionCursor() == 3u)
        {
            reachedEnd = true;
            break;
        }
    }
    EXPECT_TRUE(warpDuringTail) << "Headerless tail must load under warp (E1 on poll-started playback)";
    ASSERT_TRUE(reachedEnd) << "Tail block did not reach the natural end-of-tape";

    // E3: the natural end stood warp down — and the still-running poll loop
    // must not re-engage anything (cursor at end leaves the tape off)
    for (int i = 0; i < 50; i++)
        mainLoop->RunFrame();
    EXPECT_FALSE(context->pTape->IsPlaying());
    EXPECT_FALSE(core->IsTurboMode()) << "End-of-tape must leave the machine at 50 Hz (E3)";
    EXPECT_FALSE(controller->IsAutoTurboActive());

    EmulatorTestHelper::CleanupEmulator(emulator);
    MessageCenter::DisposeDefaultMessageCenter();
}

/// §9.2-3: Ownership under the live MainLoop. A user disable during playback
/// vetoes warp for the rest of the session (E6); a manual warp the controller
/// never owned is left completely alone across playback start and stop (E5) —
/// the Machine-menu toggle semantics from design §6.2.
TEST_F(TapeTurbo_Integration_Test, ManualTurboOwnershipUnderLiveMainLoop)
{
    MessageCenter::DisposeDefaultMessageCenter();
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("Pentagon", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    Core* core = context->pCore;
    TapeTurboController* controller = context->pTapeTurboController;
    ASSERT_NE(controller, nullptr);

    ASSERT_TRUE(context->pFeatureManager->setFeature(Features::kFastTape, false));
    ASSERT_TRUE(context->pFeatureManager->setFeature(Features::kTurboTape, true));

    // Single large headerless block: the poll loop starts it and keeps it fed
    context->coreState.tapeFilePath = WriteTAPFile("ownership.tap",
        { MakeTAPBlock(0xFF, std::vector<uint8_t>(4096, 0xA5)) });

    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
    for (int i = 0; i < 100; i++)
        mainLoop->RunFrame();

    // The consumer's poll loop (infinite) — starts and re-starts playback all
    // by itself for the whole test from wherever the cursor sits
    auto poke = BasicEncoder::runCommand(emulator,
        "POKE 31000,62:POKE 31001,0:POKE 31002,219:POKE 31003,254:POKE 31004,24:POKE 31005,252:RANDOMIZE USR 31000");
    ASSERT_TRUE(poke.success) << poke.message;
    for (int i = 0; i < 15; i++)
        mainLoop->RunFrame();

    // E1: warp engaged on the poll-started playback
    bool engaged = false;
    for (int i = 0; i < 30; i++)
    {
        mainLoop->RunFrame();
        if (context->pTape->IsPlaying() && core->IsTurboMode() && controller->IsAutoTurboActive())
        {
            engaged = true;
            break;
        }
    }
    ASSERT_TRUE(engaged) << "Poll-started playback must engage warp";

    // E6: the user switches warp OFF mid-playback (exactly what the Machine
    // menu action does) — the controller must not fight the decision while
    // the still-playing tape keeps the feature's engage conditions true
    core->DisableTurboMode();
    for (int i = 0; i < 30; i++)
        mainLoop->RunFrame();
    EXPECT_TRUE(context->pTape->IsPlaying()) << "Poll loop keeps the deck rolling";
    EXPECT_FALSE(core->IsTurboMode()) << "Suppression must hold warp off while playback continues (E6)";
    EXPECT_FALSE(controller->IsAutoTurboActive());

    // Playback over (API stop — the CLI/WebAPI/Qt stop path): the veto dies
    // with the session
    context->pTape->stopPlayback();
    for (int i = 0; i < 3; i++)
        mainLoop->RunFrame();
    ASSERT_FALSE(context->pTape->IsPlaying());
    EXPECT_FALSE(core->IsTurboMode());

    // E5: a manual warp engaged while idle is never claimed on playback start
    // and never disabled on playback stop — the poll loop restarts playback
    // from the rewound cursor on its own
    context->pTape->RewindToStart();
    core->EnableTurboMode();
    for (int i = 0; i < 30; i++)
        mainLoop->RunFrame();
    ASSERT_TRUE(context->pTape->IsPlaying()) << "Poll loop must restart playback after rewind";
    EXPECT_TRUE(core->IsTurboMode()) << "Manual warp must survive playback start untouched (E5)";
    EXPECT_FALSE(controller->IsAutoTurboActive()) << "Controller must not claim a warp it does not own";

    context->pTape->stopPlayback();
    for (int i = 0; i < 3; i++)
        mainLoop->RunFrame();
    EXPECT_FALSE(context->pTape->IsPlaying());
    EXPECT_TRUE(core->IsTurboMode()) << "Manual warp must survive playback stop untouched (E5)";

    EmulatorTestHelper::CleanupEmulator(emulator);
    MessageCenter::DisposeDefaultMessageCenter();
}
