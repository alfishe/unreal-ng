#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/basic-lang/basicencoder.h"
#include "debugger/ttd/machine_state_hash.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/tape/tape.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"

/// Integration / differential tests for the fast tape loading trap
/// (design §12.2: docs/inprogress/2026-08-30-fast-tape-loading).
///
/// All scenarios run the REAL ROM flow on a freshly booted 48K BASIC machine:
/// the command line is typed through the keyboard matrix (BasicEncoder) and
/// frames are executed by the real main loop. No CPU state is ever set by hand.
///
/// The BASIC payload under test:
///   1 REM TAPETEST
///   2 PRINT 42
/// encoded as tokenized program lines (length little-endian, line number
/// big-endian, REM = $EA, PRINT = $F4, ENTER = $0D).
class TapeLoading_Integration_Test : public ::testing::Test
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
        for (const char c : std::string("tapetest  "))
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
        const std::string path = TestPathHelper::GetTestScratchPath("tapefastload-int/" + name);

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
    /// (verified live: PROG=$5CCB + 23 bytes + $80, VARS=$5CE3)
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

    /// 64-bit architectural fingerprint (registers + port latches + counters
    /// + full RAM digest) — the same primitive the TTD seek tests use for
    /// round-trip determinism checks
    static uint64_t HashMachineState(EmulatorContext* context)
    {
        Z80* z80 = context->pCore->GetZ80();
        Memory* memory = context->pMemory;
        if (z80 == nullptr || memory == nullptr)
            return 0;

        // RAM digest: hash every model-RAM byte. config.ramsize is in KB
        const uint32_t ramBytes = static_cast<uint32_t>(context->config.ramsize) * 1024u;
        const uint64_t ramDigest = ttd::HashBytes(memory->RAMBase(), ramBytes);

        const auto snapshot = ttd::CaptureSnapshot(*static_cast<Z80State*>(z80),
                                                    context->emulatorState,
                                                    ramDigest);
        return ttd::HashSnapshot(snapshot);
    }

    /// endregion </Machine helpers>
};

/// §12.2-1: Fast LOAD "" end-to-end — the ROM flow never touches the signal path
TEST_F(TapeLoading_Integration_Test, FastLoadEndToEnd)
{
    MessageCenter::DisposeDefaultMessageCenter();
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("Pentagon", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();

    // Boot to 48K BASIC
    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
    for (int i = 0; i < 100; i++)
        mainLoop->RunFrame();

    // Insert the tape (the trap loads it lazily on the first invocation)
    context->coreState.tapeFilePath = WriteTAPFile("e2e.tap", MakeProgramTAP());

    // Type LOAD "" through the real keyboard matrix and run
    auto result = BasicEncoder::runCommand(emulator, "LOAD \"\"");
    EXPECT_TRUE(result.success) << result.message;

    bool playbackEverStarted = context->pTape->IsPlaying();
    int framesUsed = 0;
    for (; framesUsed < 30; framesUsed++)
    {
        mainLoop->RunFrame();
        if (context->pTape->IsPlaying())
            playbackEverStarted = true;
        if (ProgramLoaded(context->pMemory))
            break;
    }

    // A few extra frames for the ROM to settle the report
    for (int i = 0; i < 5; i++)
        mainLoop->RunFrame();

    // Program in RAM exactly as saved, sysvars consistent, OK report state
    EXPECT_TRUE(ProgramLoaded(context->pMemory)) << "Program not loaded after " << framesUsed << " frames";
    EXPECT_FALSE(playbackEverStarted) << "Signal playback must never start when the trap serves the load";
    EXPECT_LT(framesUsed, 30);
    EXPECT_EQ(context->pMemory->DirectReadFromZ80Memory(SYS_ERR_NR), 0xFF);

    // Whole tape consumed by the trap: both blocks, cursor at end
    EXPECT_EQ(context->pTape->GetConsumptionCursor(), 2u);

    EmulatorTestHelper::CleanupEmulator(emulator);
    MessageCenter::DisposeDefaultMessageCenter();
}

/// §12.2-2: Differential — traps ON vs traps OFF must reach the same machine state
TEST_F(TapeLoading_Integration_Test, DifferentialTrapsOnOff)
{
    struct RunResult
    {
        bool loaded = false;
        int framesUsed = 0;
        uint16_t sp = 0;
        std::vector<uint8_t> ram;  // $4000 .. SP-1
    };

    // Timing-dependent sysvars excluded from the comparison (design §12.2-2):
    // FRAMES, KSTATE, LAST_K — they differ in every run pair by construction
    auto isExcluded = [](uint16_t address)
    {
        return (address >= 0x5C78 && address <= 0x5C7A) ||  // FRAMES
               (address >= 0x5C40 && address <= 0x5C47) ||  // KSTATE
               (address == 0x5C48);                          // LAST_K
    };

    auto runScenario = [&](bool trapsOn, int frameBudget) -> RunResult
    {
        MessageCenter::DisposeDefaultMessageCenter();
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("Pentagon", LoggerLevel::LogError);
        RunResult run;
        if (emulator == nullptr)
            return run;

        EmulatorContext* context = emulator->GetContext();

        // Switch the fast-load mechanism via the runtime 'fasttape' feature —
        // the sole control plane (design §10, r3)
        if (context->pFeatureManager == nullptr ||
            !context->pFeatureManager->setFeature(Features::kFastTape, trapsOn))
        {
            ADD_FAILURE() << "fasttape feature toggle failed";
            return run;
        }

        context->coreState.tapeFilePath = WriteTAPFile(trapsOn ? "diff-on.tap" : "diff-off.tap", MakeProgramTAP());

        auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
        for (int i = 0; i < 100; i++)
            mainLoop->RunFrame();

        auto command = BasicEncoder::runCommand(emulator, "LOAD \"\"");
        EXPECT_TRUE(command.success) << command.message;

        for (; run.framesUsed < frameBudget; run.framesUsed++)
        {
            mainLoop->RunFrame();
            if (ProgramLoaded(context->pMemory))
                break;
        }

        // Settle: report issued, watchdogs stop playback on the signal path
        for (int i = 0; i < 25; i++)
            mainLoop->RunFrame();

        run.loaded = ProgramLoaded(context->pMemory);
        Z80* cpu = context->pCore->GetZ80();
        run.sp = cpu->sp;

        // Stack-history guard: the two load paths (trap vs signal) leave stale
        // bytes below the settled SP from their different CALL chains (the
        // signal path's LD-BYTES frames dip deepest). 128 bytes below SP covers
        // the whole transient region; everything meaningful (sysvars, program,
        // screen, system areas) sits far below it
        const uint16_t end = static_cast<uint16_t>(cpu->sp - 128);
        run.ram.reserve(end - 0x4000);
        for (uint16_t address = 0x4000; address < end; address++)
        {
            if (isExcluded(address))
            {
                run.ram.push_back(0x00);  // Neutralized: value differs per run by design
                continue;
            }
            run.ram.push_back(context->pMemory->DirectReadFromZ80Memory(address));
        }

        EmulatorTestHelper::CleanupEmulator(emulator);
        MessageCenter::DisposeDefaultMessageCenter();
        return run;
    };

    // Traps OFF needs the real signal timing: pilot + ~46 bytes of payload
    const RunResult fast = runScenario(true, 60);
    const RunResult slow = runScenario(false, 1500);

    ASSERT_TRUE(fast.loaded) << "Traps-ON run failed to load (frame " << fast.framesUsed << ")";
    ASSERT_TRUE(slow.loaded) << "Traps-OFF (signal emulation) run failed to load (frame " << slow.framesUsed << ")";

    // The strongest guard for the exit-state contract (design §4.3): if the
    // trap's postconditions were wrong, the ROM flow after LD-BYTES would
    // diverge and RAM below the stack guard would differ
    EXPECT_EQ(fast.sp, slow.sp);
    ASSERT_EQ(fast.ram.size(), slow.ram.size());
    for (size_t i = 0; i < fast.ram.size(); i++)
    {
        ASSERT_EQ(fast.ram[i], slow.ram[i]) << "RAM divergence at $" << std::hex << (0x4000 + i) << std::dec;
    }

    // And the speed goal: the trap path is two orders of magnitude faster
    EXPECT_LT(fast.framesUsed, slow.framesUsed);
}

/// §12.2-3: Custom-loader shape — trap declines, signal playback engages
TEST_F(TapeLoading_Integration_Test, CustomLoaderFallback)
{
    MessageCenter::DisposeDefaultMessageCenter();
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("Pentagon", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();

    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
    for (int i = 0; i < 100; i++)
        mainLoop->RunFrame();

    // Headerless data block: a custom loader's territory — the ROM LOAD ""
    // expects a header (A = $00) first, so the trap must decline (flag mismatch)
    context->coreState.tapeFilePath = WriteTAPFile("custom.tap",
        { MakeTAPBlock(0xFF, { 0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34, 0x56, 0x78 }) });

    auto command = BasicEncoder::runCommand(emulator, "LOAD \"\"");
    EXPECT_TRUE(command.success) << command.message;

    // The ROM loader polls EAR at $0562/$0564; the auto-start hook there must
    // engage signal playback from the cursor
    bool playbackStarted = false;
    int framesUsed = 0;
    for (; framesUsed < 200; framesUsed++)
    {
        mainLoop->RunFrame();
        if (context->pTape->IsPlaying())
        {
            playbackStarted = true;
            break;
        }
    }

    EXPECT_TRUE(playbackStarted) << "Signal fallback must engage when the trap declines";
    EXPECT_EQ(context->pTape->GetConsumptionCursor(), 0u) << "Trap must not have consumed the block";

    // Let the signal path run the tape to its end (end-of-tape stop)
    for (int i = 0; i < 300; i++)
        mainLoop->RunFrame();

    EmulatorTestHelper::CleanupEmulator(emulator);
    MessageCenter::DisposeDefaultMessageCenter();
}

/// §12.2-4: Multi-part hybrid — vanilla pair fast-loads, the custom tail falls
/// back to signal playback starting exactly at block 2
TEST_F(TapeLoading_Integration_Test, MultiPartHybrid)
{
    MessageCenter::DisposeDefaultMessageCenter();
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("Pentagon", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();

    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
    for (int i = 0; i < 100; i++)
        mainLoop->RunFrame();

    // Vanilla header/data pair + a custom-flag block behind it
    std::vector<std::vector<uint8_t>> blocks = MakeProgramTAP();
    blocks.push_back(MakeTAPBlock(0x3C, { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA }));
    context->coreState.tapeFilePath = WriteTAPFile("hybrid.tap", blocks);

    // Part 1: the program fast-loads without the signal path
    auto load = BasicEncoder::runCommand(emulator, "LOAD \"\"");
    EXPECT_TRUE(load.success) << load.message;

    for (int i = 0; i < 30; i++)
        mainLoop->RunFrame();

    EXPECT_TRUE(ProgramLoaded(context->pMemory));
    EXPECT_FALSE(context->pTape->IsPlaying());
    EXPECT_EQ(context->pTape->GetConsumptionCursor(), 2u);

    // Part 2: `LOAD "" CODE 30000,10` is a headerless DATA load — LD-BYTES
    // entered with A = $FF while the block at the cursor carries flag $3C.
    // The trap must decline (flag mismatch) and the auto-start must begin
    // signal playback AT BLOCK 2, not at tape start
    for (int i = 0; i < 5; i++)
        mainLoop->RunFrame();  // Report settle

    auto code = BasicEncoder::runCommand(emulator, "LOAD \"\" CODE 30000,10");
    EXPECT_TRUE(code.success) << code.message;

    bool playbackStarted = false;
    for (int i = 0; i < 200; i++)
    {
        mainLoop->RunFrame();
        if (context->pTape->IsPlaying())
        {
            playbackStarted = true;
            break;
        }
    }

    EXPECT_TRUE(playbackStarted);
    EXPECT_EQ(context->pTape->GetConsumptionCursor(), 2u) << "Playback must begin at the custom block, not block 0";

    // The custom block never fast-loaded: no payload store ever happened
    EXPECT_EQ(context->pMemory->DirectReadFromZ80Memory(30000), 0x00);

    EmulatorTestHelper::CleanupEmulator(emulator);
    MessageCenter::DisposeDefaultMessageCenter();
}

/// §12.2-6: Read-gap watchdog pause + sustained-poll resume — the custom
/// multi-stage loader lifecycle (insult.tap shape). Signal playback engaged,
/// the program stops reading EAR while it processes: the watchdog must FREEZE
/// the head in place (in-flight block NOT consumed — the old terminal stop
/// lost it and left the loader polling a dead line forever), keyboard
/// scanning in the editor must NOT resume playback, and the loader's own
/// poll loop must resume mid-block with the cursor unmoved.
TEST_F(TapeLoading_Integration_Test, WatchdogPauseFreezesAndSustainedPollResumes)
{
    MessageCenter::DisposeDefaultMessageCenter();
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("Pentagon", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();

    // Pure signal path: fasttape off
    ASSERT_TRUE(context->pFeatureManager->setFeature(Features::kFastTape, false));

    // Program pair + two large tail blocks: 4 KiB payload each plays for
    // ~1200 frames, so the 150-frame watchdog pauses MID-BLOCK, never at
    // end-of-tape
    std::vector<std::vector<uint8_t>> blocks = MakeProgramTAP();
    blocks.push_back(MakeTAPBlock(0xFF, std::vector<uint8_t>(4096, 0xA5)));
    blocks.push_back(MakeTAPBlock(0xFF, std::vector<uint8_t>(4096, 0x5A)));
    context->coreState.tapeFilePath = WriteTAPFile("watchdog-pause.tap", blocks);

    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
    for (int i = 0; i < 100; i++)
        mainLoop->RunFrame();

    // Signal-load the program pair; the tape keeps rolling into block 2
    auto load = BasicEncoder::runCommand(emulator, "LOAD \"\"");
    ASSERT_TRUE(load.success) << load.message;

    int frames = 0;
    for (; frames < 1500; frames++)
    {
        mainLoop->RunFrame();
        if (ProgramLoaded(context->pMemory))
            break;
    }
    ASSERT_LT(frames, 1500) << "Signal load did not complete";
    ASSERT_TRUE(context->pTape->IsPlaying()) << "Tape must still be rolling past the loaded pair";

    // A silent processing phase — a DI'd delay loop with NO port reads at
    // all (with interrupts on, the ROM ISR keyboard-scan would read the ULA
    // port ~8x/frame and keep the watchdog fed; real loaders DI exactly like
    // this during read-free processing phases). 16 x 65536 DEC-HL iterations
    // is ~380 frames, comfortably past the 150-frame watchdog; then EI / RET.
    //   30000: DI / LD B,$10 / LD HL,0 / DEC HL / LD A,H / OR L / JR NZ /
    //   30011: DJNZ $30003 / EI / RET  (HL inner: LD BC would clobber B)
    auto delay = BasicEncoder::runCommand(emulator,
        "POKE 30000,243:POKE 30001,6:POKE 30002,16:POKE 30003,33:POKE 30004,0:POKE 30005,0:POKE 30006,43:POKE 30007,124:POKE 30008,181:"
        "POKE 30009,32:POKE 30010,251:POKE 30011,16:POKE 30012,246:POKE 30013,251:POKE 30014,201:RANDOMIZE USR 30000");
    ASSERT_TRUE(delay.success) << delay.message;

    // Watchdog pauses mid-block while the loop runs. Capture the frozen cursor.
    size_t cursorAtPause = 0;
    bool paused = false;
    for (int i = 0; i < 600; i++)
    {
        mainLoop->RunFrame();
        if (!context->pTape->IsPlaying())
        {
            paused = true;
            cursorAtPause = context->pTape->GetConsumptionCursor();
            break;
        }
    }
    ASSERT_TRUE(paused) << "Watchdog did not pause playback";
    ASSERT_EQ(cursorAtPause, 2u) << "Pause must freeze MID-BLOCK (cursor = in-flight block), not consume it";

    // Editor keyboard scanning (~8 half-row reads/frame) must never reach the
    // sustained-poll threshold: playback stays paused, cursor stays frozen.
    // The window must ALSO outlast the delay loop (~390 frames from USR start;
    // pause lands at ~151): typing is injected via LAST_K and only the editor
    // input loop consumes it — keystrokes landing while the DI'd loop still
    // runs are swallowed and corrupt the next command line
    for (int i = 0; i < 350; i++)
        mainLoop->RunFrame();
    EXPECT_FALSE(context->pTape->IsPlaying()) << "Keyboard scan must not trip the sustained-poll threshold";
    EXPECT_EQ(context->pTape->GetConsumptionCursor(), cursorAtPause) << "Frozen cursor must not drift while paused";

    // The loader's own poll loop (typed, not hand-set): LD A,0 / IN A,($FE) /
    // JR $-4 at 31000 — polls $00FE thousands of times per frame
    auto poke = BasicEncoder::runCommand(emulator,
        "POKE 31000,62:POKE 31001,0:POKE 31002,219:POKE 31003,254:POKE 31004,24:POKE 31005,252:RANDOMIZE USR 31000");
    ASSERT_TRUE(poke.success) << poke.message;

    // Settle frames for ENTER: tokenize + parse + POKE chain takes ~7 frames;
    // the POKEs must land (guards the fixture — a swallowed keystroke corrupts
    // the line and silently skips the loop)
    for (int i = 0; i < 15; i++)
        mainLoop->RunFrame();

    EXPECT_EQ(context->pMemory->DirectReadFromZ80Memory(31000), 62)   // LD A,0
        << "Poll loop POKEs did not execute — command line corrupted";
    EXPECT_EQ(context->pMemory->DirectReadFromZ80Memory(31002), 219); // IN A,($FE)
    EXPECT_EQ(context->pMemory->DirectReadFromZ80Memory(31004), 24);  // JR NZ

    // Sustained polling resumes within a frame — MID-BLOCK: cursor unmoved
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
    EXPECT_EQ(context->pTape->GetConsumptionCursor(), cursorAtPause)
        << "Resume continues the frozen block in place, not from the next block";

    // And the tape makes progress again through the tail blocks
    bool progressed = false;
    for (int i = 0; i < 2500; i++)
    {
        mainLoop->RunFrame();
        if (context->pTape->GetConsumptionCursor() > cursorAtPause)
        {
            progressed = true;
            break;
        }
    }
    EXPECT_TRUE(progressed) << "Playback must roll past the frozen block after resume";

    EmulatorTestHelper::CleanupEmulator(emulator);
    MessageCenter::DisposeDefaultMessageCenter();
}

/// §12.2-7: A pure custom loader (no ROM LD-BYTES call at all) starts signal
/// playback purely by sustained EAR polling — the ROM $0562/$0564 auto-start
/// anchor is never executed. Guards the loader-agnostic entry path.
TEST_F(TapeLoading_Integration_Test, SustainedPollStartsPlaybackWithoutRomAnchor)
{
    MessageCenter::DisposeDefaultMessageCenter();
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("Pentagon", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();

    // fasttape ON (default): irrelevant here — the trap never fires because
    // the program never enters $0556. It only matters that no ROM loader poll
    // ever runs.
    context->coreState.tapeFilePath = WriteTAPFile("poll-start.tap",
        { MakeTAPBlock(0xFF, { 0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34, 0x56, 0x78 }) });

    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
    for (int i = 0; i < 100; i++)
        mainLoop->RunFrame();

    // Sanity: typing in the editor (keyboard scanning) never starts playback
    auto poke = BasicEncoder::runCommand(emulator,
        "POKE 30000,62:POKE 30001,0:POKE 30002,219:POKE 30003,254:POKE 30004,24:POKE 30005,252:RANDOMIZE USR 30000");
    ASSERT_TRUE(poke.success) << poke.message;
    EXPECT_FALSE(context->pTape->IsPlaying()) << "Typing/keyboard scan must not start playback";

    // The poll loop starts playback from the consumption cursor (block 0)
    bool started = false;
    for (int i = 0; i < 30; i++)
    {
        mainLoop->RunFrame();
        if (context->pTape->IsPlaying())
        {
            started = true;
            break;
        }
    }
    ASSERT_TRUE(started) << "Sustained EAR polling must start playback without the ROM anchor";
    EXPECT_EQ(context->pTape->GetConsumptionCursor(), 0u);

    EmulatorTestHelper::CleanupEmulator(emulator);
    MessageCenter::DisposeDefaultMessageCenter();
}

/// §12.2-5: TTD round-trip across a fast load — record a session that crosses
/// the trap, then SeekTo on both sides of the load boundary. Three contracts:
///   1. Restored state matches the recorded checkpoints (architectural hash).
///   2. The tape consumption cursor restores with the checkpoint's subsystem
///      blob (design §9.4: cursor is the single serialized source of truth).
///   3. Seeking is never blocked by an external-event marker for the trap —
///      the fast-load path is pure CPU-visible state, so it needs no replay
///      barrier (unlike signal playback, whose markers live in startTape /
///      stopTape / stopPlayback).
TEST_F(TapeLoading_Integration_Test, TTDRoundTripAcrossFastLoad)
{
    MessageCenter::DisposeDefaultMessageCenter();
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("Pentagon", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    Memory* memory = context->pMemory;
    ttd::TimeTravelManager* ttdMgr = context->pTimeTravelManager;
    ASSERT_NE(ttdMgr, nullptr);

    // Same enable dance as the TTD fixtures: debug mode + time travel, then
    // Memory picks up the feature cache so dirty tracking hooks fire
    FeatureManager* fm = emulator->GetFeatureManager();
    ASSERT_NE(fm, nullptr);
    fm->setFeature(Features::kDebugMode, true);
    fm->setFeature(Features::kTimeTravel, true);
    memory->UpdateFeatureCache();

    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
    for (int i = 0; i < 100; i++)
        mainLoop->RunFrame();

    // Record across the whole load; the pre-load bookmark sits a couple of
    // frames into the session (past the baseline, so the backward seek is a
    // real restore and not the baseline no-op special case)
    ASSERT_TRUE(ttdMgr->StartRecording());
    for (int i = 0; i < 2; i++)
        mainLoop->RunFrame();
    const uint64_t frameBefore = ttdMgr->CurrentPosition().frame;
    ASSERT_FALSE(ProgramLoaded(memory)) << "Sanity: pre-load bookmark must be pre-load";

    context->coreState.tapeFilePath = WriteTAPFile("ttd-roundtrip.tap", MakeProgramTAP());
    auto command = BasicEncoder::runCommand(emulator, "LOAD \"\"");
    ASSERT_TRUE(command.success) << command.message;

    int framesUsed = 0;
    for (; framesUsed < 60; framesUsed++)
    {
        mainLoop->RunFrame();
        if (ProgramLoaded(memory))
            break;
    }
    ASSERT_LT(framesUsed, 60) << "Fast load did not complete under TTD recording";

    // Settle the OK report, then close the session at the after-load boundary
    for (int i = 0; i < 3; i++)
        mainLoop->RunFrame();
    const uint64_t frameAfter = ttdMgr->CurrentPosition().frame;
    ttdMgr->StopRecording();
    ASSERT_GT(ttdMgr->GetCheckpointCount(), 0u) << "Session recorded no checkpoints";

    // Checkpoints are periodic keyframes (not one per frame), so the frame <->
    // checkpoint-index mapping is opaque here. The reference hash comes from a
    // real seek landing instead: cross the boundary backward first (a genuine
    // restore, not a no-op — current position is frameAfter), then forward
    // (checkpoint + deterministic replay re-running the trap)
    ttd::TimeTravelManager::TTDSeekResult result;

    // First backward crossing: pre-load RAM, cursor rewound, no marker barrier
    EXPECT_TRUE(ttdMgr->SeekTo({frameBefore, 0}, &result));
    EXPECT_EQ(result.haltReason, ttd::TimeTravelManager::TTDSeekHaltReason::Target)
        << "The trap path must not emit external-event markers";
    EXPECT_FALSE(ProgramLoaded(memory)) << "Pre-load boundary must have no program in RAM";
    EXPECT_EQ(context->pTape->GetConsumptionCursor(), 0u)
        << "Tape cursor must restore with the checkpoint's subsystem blob";

    // First forward crossing: replay must re-run the trap and reproduce the
    // load; the landing becomes the reference fingerprint
    EXPECT_TRUE(ttdMgr->SeekTo({frameAfter, 0}, &result));
    EXPECT_EQ(result.haltReason, ttd::TimeTravelManager::TTDSeekHaltReason::Target);
    ASSERT_TRUE(ProgramLoaded(memory)) << "Forward replay across the trap must reproduce the load";
    ASSERT_EQ(context->pTape->GetConsumptionCursor(), 2u);
    const uint64_t hashAfterRef = HashMachineState(context);

    // Second crossing: same targets, opposite order of arrival — every landing
    // must reproduce bit-for-bit (seek determinism across the trap boundary)
    EXPECT_TRUE(ttdMgr->SeekTo({frameBefore, 0}, &result));
    EXPECT_EQ(result.haltReason, ttd::TimeTravelManager::TTDSeekHaltReason::Target);
    EXPECT_FALSE(ProgramLoaded(memory));
    EXPECT_EQ(context->pTape->GetConsumptionCursor(), 0u);

    EXPECT_TRUE(ttdMgr->SeekTo({frameAfter, 0}, &result));
    EXPECT_EQ(result.haltReason, ttd::TimeTravelManager::TTDSeekHaltReason::Target);
    EXPECT_TRUE(ProgramLoaded(memory));
    EXPECT_EQ(context->pTape->GetConsumptionCursor(), 2u);
    EXPECT_EQ(HashMachineState(context), hashAfterRef)
        << "Second crossing must land on the identical machine state";

    EmulatorTestHelper::CleanupEmulator(emulator);
    MessageCenter::DisposeDefaultMessageCenter();
}
