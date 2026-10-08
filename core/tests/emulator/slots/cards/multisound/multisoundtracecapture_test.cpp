#ifdef UNREALNG_HAVE_SAM2695
// ZX-MultiSound real-program bus traces (CL-2, docs/inprogress/2026-10-03-zx-multisound/tdd-card-logic.md §8).
//
// MultiSoundTraceCapture_Test (disabled, not checks): runs a real program on a machine with the card in a ZX-bus slot
// and writes the card's bus trace (MultiSoundCard::SetBusTrace + MultiSoundTraceWriter) to the scratch folder. The
// programs are third-party and not in the repository: they are read from testdata/sound/multisound/software/ (see
// the README there) and a missing one skips its capture.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/multisoundscenario.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/trdostesthelper.h"
#include "debugger/analyzers/rom-print/screenocr.h"
#include "debugger/ttd/ttdcompression.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_tsconf.h"
#include "emulator/slots/cards/multisound/multisoundslotcard.h"
#include "common/image/imagehelper.h"
#include "emulator/sound/audio.h"
#include "emulator/video/screen.h"
#include "multisoundstagedmachine.h"

using namespace multisoundtest;

namespace
{

std::filesystem::path SoftwarePath(const std::string& relative)
{
    return TestPathHelper::FindProjectRoot() / "testdata" / "sound" / "multisound" / "software" / relative;
}

/// The screen as text: the text layer (TS-Conf text mode) or the ZX screen through the OCR
std::string ScreenText(Emulator& emulator)
{
    std::string text;
    if (ScreenOCR::textLayerScreen(&emulator, text))
        return text;
    Memory* memory = emulator.GetContext()->pMemory;
    for (int row = 0; row < 24; row++)
    {
        for (int col = 0; col < 32; col++)
            text += ScreenOCR::ocrCell(memory, row, col);
        text += '\n';
    }
    return text;
}

/// A capture session: the machine, the trace writer on its card, and the steps of a script
class Capture
{
public:
    Capture(const std::string& configFolder, const std::string& slots, const std::string& extra = {})
        : machine(configFolder, slots, extra)
    {
        if (!machine.Ok() || machine.Card() == nullptr)
            return;
        // The machine has started: the card's power-on reset, the machine's reset at the card time it reports, the
        // first frame start. The trace begins with them (MultiSoundTraceWriter::StartUp)
        MultiSoundCardReport report;
        machine.Card()->Card().Describe(report);
        machine.Card()->Card().SetBusTrace(&writer);
        writer.StartUp(report.time);
        machine.Machine().EnableTurboMode(true);
        ok = true;
    }
    ~Capture()
    {
        if (machine.Card() != nullptr)
            machine.Card()->Card().SetBusTrace(nullptr);
    }

    Emulator& Emu() { return machine.Machine(); }
    EmulatorContext* Context() { return machine.Context(); }

    void Frames(unsigned n) { Emu().RunNFrames(n); }
    /// Holds `key` (a ZX key or a PC key name) for `hold` frames, then lets `after` frames pass
    void Tap(const std::string& key, uint16_t hold = 3, unsigned after = 10)
    {
        DebugKeyboardManager* keys = Context()->pDebugManager->GetKeyboardManager();
        keys->PressKey(key);
        Frames(hold);
        keys->ReleaseKey(key);
        Frames(after);
    }
    void Type(const std::string& text)
    {
        DebugKeyboardManager* keys = Context()->pDebugManager->GetKeyboardManager();
        keys->TypeText(text);
        for (int i = 0; i < 2000 && keys->IsSequenceRunning(); i++)
            Frames(1);
    }
    std::string Screen() { return ScreenText(Emu()); }
    /// Runs frames until `done` (checked every `step` frames) or `maxFrames`; the frames run
    unsigned RunUntil(const std::function<bool()>& done, unsigned maxFrames, unsigned step = 5)
    {
        unsigned frames = 0;
        while (frames < maxFrames && !done())
        {
            Frames(step);
            frames += step;
        }
        return frames;
    }

    /// The trace with its header directives, written to scratch/<name>.msc
    std::string Save(const std::string& name, const std::string& description)
    {
        const MultiSoundCard& card = machine.Card()->Card();
        const MultiSoundOptions& o = card.Options();
        std::string header = description;
        char line[160];
        std::snprintf(line, sizeof(line), "mask %s\nram %s\ndip %s\ncpu %.1f\nframe %llu\nrate %u\nlength %llu\n",
                      o.ctrlMask == MultiSoundCtrlMask::Classic ? "classic" : "pro",
                      card.Config().options.gsRam == MultiSoundGsRam::TwoMb ? "2m" : "1m",
                      FormatMultiSoundDip(o).c_str(), cpuMHz,
                      static_cast<unsigned long long>(Context()->config.frame), card.Config().hostTickRate,
                      static_cast<unsigned long long>(machine.Card()->Origin()));
        header += line;
        const std::string text = writer.Text(header);
        const std::string path = TestPathHelper::GetTestScratchPath(name + ".msc");
        std::ofstream out(path, std::ios::binary);
        out << text;
        // The stored form (testdata/sound/multisound/traces/): zstd level 19
        const std::vector<uint8_t> packed =
            ttd::codec::Compress(reinterpret_cast<const uint8_t*>(text.data()), text.size(), 19);
        std::ofstream zst(path + ".zst", std::ios::binary);
        zst.write(reinterpret_cast<const char*>(packed.data()), static_cast<std::streamsize>(packed.size()));
        const MultiSoundTraceWriter::Stats& s = writer.GetStats();
        std::printf("%s: %zu bytes, %llu lines; host writes %llu, reads %llu (%llu lines, longest run %u), resets %llu; "
                    "GS port %llu (%llu lines); DAC fetches %llu (%llu lines, %llu unchanged, %llu over budget)\n",
                    path.c_str(), text.size(), (unsigned long long)s.lines, (unsigned long long)s.hostWrites,
                    (unsigned long long)s.hostReads, (unsigned long long)s.hostReadLines, s.longestRun,
                    (unsigned long long)s.resets, (unsigned long long)s.gsPortCycles, (unsigned long long)s.gsPortLines,
                    (unsigned long long)s.dacFetches, (unsigned long long)s.dacLines, (unsigned long long)s.dacUnchanged,
                    (unsigned long long)s.dacOverBudget);
        return path;
    }

    StagedMachine machine;
    MultiSoundTraceWriter writer;
    double cpuMHz = 3.5;
    bool ok = false;
};

std::vector<std::string> Split(const std::string& text, char by)
{
    std::vector<std::string> out;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, by))
        out.push_back(item);
    return out;
}

} // namespace

/// Capture driver, not a check: MS_CAPTURE_SCRIPT names a script file (the stored traces' scripts are
/// testdata/sound/multisound/traces/*.script), one step per line, '#' starts a comment line:
///   machine <config folder>, slots <[SLOTS] line> (repeatable), describe <header comment> (repeatable): set-up
///   cpu <MHz>                      the host clock written to the trace header (the RTL bus timing)
///   disk <path> | sd <image>       a TR-DOS image into drive A / an SD card image (TS-Conf), relative to
///                                  testdata/sound/multisound/software/
///   trdos <command>                reset into TR-DOS and run the command (TRDOSTestHelper)
///   reset | frames <n> | key <ZX or pc.* key name> [hold frames] | type <text>
///   screen | shot <name>           the screen as text / as scratch/<name>.png
///   save <name>                    scratch/<name>.msc and scratch/<name>.msc.zst
/// The trace starts when the machine is created (the card's power-on); a machine reset is a 'reset' line.
TEST(MultiSoundTraceCapture_Test, DISABLED_Script)
{
    const char* scriptPath = std::getenv("MS_CAPTURE_SCRIPT");
    if (scriptPath == nullptr)
        GTEST_SKIP() << "MS_CAPTURE_SCRIPT not set";
    std::ifstream in(scriptPath);
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string folder = "pentagon128k";
    std::string slots;
    std::string description;
    std::vector<std::string> steps;
    for (const std::string& raw : Split(buffer.str(), '\n'))
    {
        const std::string line = raw.substr(0, raw.find('#') == 0 ? 0 : raw.size());
        if (line.empty())
            continue;
        if (line.rfind("machine ", 0) == 0)
            folder = line.substr(8);
        else if (line.rfind("slots ", 0) == 0)
            slots += line.substr(6) + "\n";
        else if (line.rfind("describe ", 0) == 0)
            description += "# " + line.substr(9) + "\n";
        else
            steps.push_back(line);
    }
    if (slots.empty())
        slots = "zxbus.1 = multisound\n";

    Capture capture(folder, slots);
    ASSERT_TRUE(capture.ok);
    std::unique_ptr<TRDOSTestHelper> trdos;
    for (const std::string& step : steps)
    {
        const size_t space = step.find(' ');
        const std::string verb = step.substr(0, space);
        const std::string arg = space == std::string::npos ? std::string() : step.substr(space + 1);
        if (verb == "cpu")
            capture.cpuMHz = std::atof(arg.c_str());
        else if (verb == "disk")
        {
            std::string error;
            ASSERT_TRUE(capture.Emu().LoadDisk(SoftwarePath(arg).string(), 0, &error)) << error;
        }
        else if (verb == "sd")
        {
            auto* decoder = dynamic_cast<PortDecoder_TSConf*>(capture.Context()->pPortDecoder);
            ASSERT_NE(decoder, nullptr);
            ASSERT_TRUE(decoder->InsertSdCard(SoftwarePath(arg).string(), SdCardSpi::WriteMode::Session));
        }
        else if (verb == "reset")
            capture.Emu().Reset();
        else if (verb == "trdos")
        {
            trdos = std::make_unique<TRDOSTestHelper>(&capture.Emu(), false);
            trdos->startCommand(arg);
        }
        else if (verb == "frames")
            capture.Frames(static_cast<unsigned>(std::atoi(arg.c_str())));
        else if (verb == "key")
        {
            const std::vector<std::string> parts = Split(arg, ' ');
            capture.Tap(parts[0], parts.size() > 1 ? static_cast<uint16_t>(std::atoi(parts[1].c_str())) : 3);
        }
        else if (verb == "type")
            capture.Type(arg);
        else if (verb == "screen")
        {
            Z80* z80 = capture.Context()->pCore->GetZ80();
            std::printf("--- screen (frame %llu, pc %04X) ---\n%s", (unsigned long long)capture.Context()->emulatorState.frame_counter,
                        z80->pc, capture.Screen().c_str());
        }
        else if (verb == "shot")
        {
            FramebufferDescriptor& fb = capture.Context()->pScreen->GetFramebufferDescriptor();
            const std::string path = TestPathHelper::GetTestScratchPath(arg + ".png");
            ImageHelper::SavePNG(path, fb.memoryBuffer, fb.memoryBufferSize, fb.width, fb.height);
            std::printf("shot %s (%ux%u)\n", path.c_str(), fb.width, fb.height);
        }
        else if (verb == "save")
            capture.Save(arg, description + "# Captured by MultiSoundTraceCapture_Test.DISABLED_Script from " +
                                  std::filesystem::path(scriptPath).filename().string() + "\n");
        else
            FAIL() << "unknown step: " << step;
    }
}

#endif // UNREALNG_HAVE_SAM2695
