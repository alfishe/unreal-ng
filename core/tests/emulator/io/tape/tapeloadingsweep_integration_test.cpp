#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "_helpers/romeditortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/basic-lang/commandtyper.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/tape/tape.h"

/// Loads every tape in testdata/loaders (tap, tzx) on the whole machine the way a person does: `LOAD ""`
/// through the verified command typer, keys pressed now and then for the prompts ("INFINITE LIVES Y/N?",
/// the Dizzy trainer menus "1 normal, 2 immortal, 0 start"), then a liveness check.
/// Plan step 5 of the nonstandard-loader investigation (docs/inprogress/2026-08-30-fast-tape-loading).
///
/// Opt-in: a full sweep plays minutes of tape per case, so the cases are registered only when
/// UNREAL_TAPE_SWEEP is set. A normal core-tests run has no such tests at all (none disabled, none
/// skipped). tools/verification/tape/tape-sweep.sh runs it and prints the report it writes to
/// scratch/tape-sweep-report.txt.
///
/// It asserts only the hard signal: for EMELYANOV and SAN-SAN with fast loading off (48K and Pentagon, the
/// key after 1 s, 10 s and 60 s) and for the control tape, the ROM loader is not left waiting for a
/// signal. Whether a program really runs cannot be told from outside: a game answers other keys than a
/// menu, and a still screen is not a dead machine. So "dead" / "ROM" in the report are hints, and the
/// final screen of every case goes to scratch/tape-sweep-screens/ for a contact sheet
/// (tape-sweep.sh renders it), the way the investigation judged its runs.
namespace
{
struct SweepCase
{
    std::string tape;       ///< Path under testdata/loaders
    std::string model;      ///< 48K or PENTAGON
    bool fastTape;
    int keyPeriodFrames;    ///< A prompt key is pressed every this many frames while the tape plays
};

enum class Verdict
{
    Ok,        ///< the program runs and reacts to keys
    Hang,      ///< the ROM loader still waits for a signal: the tape did not feed it
    Rom,       ///< in a ROM (BASIC editor, report) with no reaction to keys
    Dead,      ///< the screen does not react to keys, outside the ROM
    Needs128K, ///< the release has no 48K data: on a 48K it waits for a block the tape does not hold
};

const char* VerdictName(Verdict verdict)
{
    switch (verdict)
    {
        case Verdict::Ok: return "OK";
        case Verdict::Hang: return "hang";
        case Verdict::Rom: return "ROM";
        case Verdict::Dead: return "dead";
        case Verdict::Needs128K: return "128K-only";
    }
    return "?";
}

std::vector<SweepCase> Cases()
{
    if (std::getenv("UNREAL_TAPE_SWEEP") == nullptr)
        return {};

    const std::vector<std::string> tapes = {
        "tap/action.tap", "tap/aydetect.tap", "tap/AYtest_v0.2.tap", "tap/DIZZY_X_ALEX_S__MAX_IWAMOTO.tap",
        "tap/DIZZY_X_CHEFRANOV_VALENTIN.tap", "tap/DIZZY_X_EMELYANOV_PAVEL.tap", "tap/DIZZY_X_HACKER_SHURIK.tap",
        "tap/DIZZY_X_KID__DR.tap", "tap/DIZZY_X_SAN-SAN.tap", "tap/DIZZY_X_TIMOFEY_YUNAEV__ROBERT_MAKSIMOV.tap",
        "tap/earshaver.tap", "tap/echology.tap", "tap/greenberet.tap", "tap/insult.tap", "tap/IntTest+.tap",
        "tap/lphp.tap", "tap/traffic_lights.tap", "tzx/bb-redux.tzx", "tzx/blava-demo.tzx", "tzx/bubble-demo.tzx",
        "tzx/interlace-demo.tzx", "tzx/mda-demo.tzx", "tzx/parallax-demo.tzx",
    };

    std::vector<SweepCase> cases;
    for (const std::string& tape : tapes)
    {
        for (const char* model : { "48K", "PENTAGON" })
        {
            for (bool fast : { true, false })
                cases.push_back({ tape, model, fast, 500 });

            // Prompt tapes: the key after 1 s and after 60 s as well as after 10 s
            if (tape.find("DIZZY_X_") != std::string::npos)
            {
                cases.push_back({ tape, model, false, 50 });
                cases.push_back({ tape, model, false, 3000 });
            }
        }
    }
    return cases;
}

/// Releases that carry 128K data only. ALEX_S (O4, 2026-09-28): after its menu the program checks for 128K
/// memory; on a 128K it calls LD-BYTES for flag #FF, 33792 bytes at #61A8, then the screen (#1B00 at
/// #4000); on a 48K it asks for flag #13, 23737 bytes at #7D3C, and the tape holds no #13 block (its flags
/// are #00 #FF #FF #FF #FF). A real 48K waits the same way
bool Needs128K(const SweepCase& c)
{
    return c.model == "48K" && c.tape.find("DIZZY_X_ALEX_S") != std::string::npos;
}

/// Must not hang: the plan's requirement and the control tape
bool MustLoad(const SweepCase& c)
{
    const bool control = c.tape.find("CHEFRANOV") != std::string::npos;
    const bool required = !c.fastTape && (c.tape.find("EMELYANOV") != std::string::npos ||
                                          c.tape.find("SAN-SAN") != std::string::npos);
    return control || required;
}

std::string CaseName(const ::testing::TestParamInfo<SweepCase>& info)
{
    std::string name = info.param.tape.substr(info.param.tape.find('/') + 1);
    name = name.substr(0, name.find('.'));
    name += "_" + info.param.model + (info.param.fastTape ? "_fast" : "_signal") + "_key" +
            std::to_string(info.param.keyPeriodFrames);
    for (char& ch : name)
    {
        if (!isalnum(static_cast<unsigned char>(ch)))
            ch = '_';
    }
    return name;
}
} // namespace

class TapeLoadingSweep_Test : public RomEditorFixture, public ::testing::WithParamInterface<SweepCase>
{
protected:
    static constexpr int MAX_FRAMES = 30000;      // 10 minutes of emulated time
    static constexpr int SETTLE_FRAMES = 1500;    // after the tape ended: the program starts up

    uint32_t HashScreen() const
    {
        uint32_t h = 2166136261u;
        for (uint16_t address = 0x4000; address < 0x5B00; address++)
            h = (h ^ _context->pMemory->DirectReadFromZ80Memory(address)) * 16777619u;
        return h;
    }

    bool InRomLoader() const
    {
        const uint16_t pc = _context->pCore->GetZ80()->pc;
        const uint8_t* rom = _context->pMemory->GetPhysicalAddressForZ80Page(0);
        return _context->pMemory->IsBank0ROM() && pc >= 0x0556 && pc <= 0x0605 && rom[0x0564] == 0x1F;
    }

    bool InRom() const
    {
        return _context->pMemory->IsBank0ROM() && _context->pCore->GetZ80()->pc < 0x4000;
    }

    void Press(ZXKeysEnum key, int frames)
    {
        _context->pKeyboard->PressKey(key);
        RunFrames(frames);
        _context->pKeyboard->ReleaseKey(key);
    }
};

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(TapeLoadingSweep_Test);

TEST_P(TapeLoadingSweep_Test, LoadsAndRuns)
{
    const SweepCase& c = GetParam();

    if (c.model == "48K")
        Boot("48K", RM_SOS, "1982 Sinclair");
    else
        Boot("PENTAGON", RM_128, "48 BASIC");
    ASSERT_FALSE(HasFatalFailure());
    _context->pFeatureManager->setFeature(Features::kFastTape, c.fastTape);
    _context->coreState.tapeFilePath = TestPathHelper::GetTestDataPath("loaders/" + c.tape);

    CommandTyper* typer = _context->pDebugManager->GetCommandTyper();
    ASSERT_NE(typer, nullptr);
    ASSERT_TRUE(typer->Request("LOAD \"\"", CommandTyper::Options{}));
    RunUntil([&] { return typer->GetStatus() == CommandTyper::Status::Done; }, 4000);
    ASSERT_TRUE(CommandTyper::Succeeded(typer->GetResult())) << typer->GetResult().message;

    // Play until the tape ended and the program settled. The prompt keys go round: Y for Y/N questions,
    // 1 for a mode menu, 0 for "start" (the Dizzy trainer menus). Never SPACE: LD-BYTES tests BREAK by
    // the SPACE bit alone (IN A,(#7FFE) bit 0), so SPACE aborts a ROM load in the middle of a block
    const ZXKeysEnum promptKeys[] = { ZXKEY_Y, ZXKEY_1, ZXKEY_0 };
    Tape* tape = _context->pTape;
    int endedAt = -1;
    int frame = 0;
    int keyIndex = 0;
    for (; frame < MAX_FRAMES; frame++)
    {
        const int phase = frame % c.keyPeriodFrames;
        const ZXKeysEnum key = promptKeys[keyIndex % 3];
        if (phase == c.keyPeriodFrames / 2)
            _context->pKeyboard->PressKey(key);
        if (phase == c.keyPeriodFrames / 2 + 12)
        {
            _context->pKeyboard->ReleaseKey(key);
            keyIndex++;
        }
        _loop->RunFrame();

        if (endedAt < 0 && tape->GetPlaybackState() == TapePlaybackState::Ended)
            endedAt = frame;
        if (endedAt >= 0 && frame - endedAt >= SETTLE_FRAMES)
            break;
    }
    for (ZXKeysEnum key : promptKeys)
        _context->pKeyboard->ReleaseKey(key);

    // A ROM loader still waiting for a signal is a hang, whether the tape ran out or stands parked
    const bool loaderWaiting = InRomLoader();

    // Liveness: the screen reacts to keys a program or a menu would take
    const uint32_t before = HashScreen();
    for (ZXKeysEnum key : { ZXKEY_0, ZXKEY_1, ZXKEY_ENTER, ZXKEY_SPACE, ZXKEY_N })
    {
        Press(key, 10);
        RunFrames(100);
    }
    const bool reacts = HashScreen() != before;

    Verdict verdict = Verdict::Ok;
    if (Needs128K(c))
        verdict = Verdict::Needs128K;
    else if (loaderWaiting)
        verdict = Verdict::Hang;
    else if (!reacts)
        verdict = InRom() ? Verdict::Rom : Verdict::Dead;

    char line[256];
    snprintf(line, sizeof(line), "%-44s %-8s %-6s key%-4d %-5s tape=%-9s cursor=%zu/%zu frames=%d\n",
             c.tape.c_str(), c.model.c_str(), c.fastTape ? "fast" : "signal", c.keyPeriodFrames,
             VerdictName(verdict), tape->GetPlaybackState() == TapePlaybackState::Ended ? "ended" : "not-ended",
             tape->GetConsumptionCursor(), tape->GetBlocks().size(), frame);
    if (FILE* report = fopen(TestPathHelper::GetTestScratchPath("tape-sweep-report.txt").c_str(), "a"))
    {
        fputs(line, report);
        fclose(report);
    }
    printf("%s", line);

    if (FILE* out = fopen(TestPathHelper::GetTestScratchPath("tape-sweep-screens/" + CaseName(
                                  ::testing::TestParamInfo<SweepCase>(c, 0)) + ".scr").c_str(), "wb"))
    {
        for (uint16_t address = 0x4000; address < 0x5B00; address++)
            fputc(_context->pMemory->DirectReadFromZ80Memory(address), out);
        fclose(out);
    }

    if (MustLoad(c))
        EXPECT_NE(verdict, Verdict::Hang) << line << Screen();
}

INSTANTIATE_TEST_SUITE_P(Tapes, TapeLoadingSweep_Test, ::testing::ValuesIn(Cases()), CaseName);
