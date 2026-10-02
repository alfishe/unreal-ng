#include "stdafx.h"
#include "pch.h"

#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/romeditortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/basic-lang/commandtyper.h"
#include "debugger/debugmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

/// unreal-ng's runner for the co-emulation harness (tools/verification/coemu, contract in its README): loads a
/// test program the way a user would on each named machine, runs it until its DONE byte is 1, and writes its
/// memory and final screen for the harness to compare. Driven by tools/verification/coemu/unreal-ng/run.sh
/// through the environment; without COEMU_OUT it does nothing:
///   COEMU_OUT         the output folder (<machine>.bin, .screen.txt, .log)
///   COEMU_PROGRAM     the program's files without the extension (.tap, .trd, .sym)
///   COEMU_MACHINES    the machines, space-separated (48k 128k plus2 plus2a plus3 pentagon scorpion atm710 atm3 ...)
///   COEMU_MAX_FRAMES  give up after this many frames

namespace
{
struct CoEmuMachine
{
    const char* editor;                 // RomEditorFixture::BootEditor name
    bool disk;                          // the .trd through TR-DOS, else the .tap
    std::vector<std::string> commands;  // typed in order
};

const std::map<std::string, CoEmuMachine>& CoEmuMachines()
{
    static const std::map<std::string, CoEmuMachine> machines = {
        { "48k", { "48K", false, { "LOAD \"\"" } } },
        { "128k", { "128K-128BASIC", false, { "LOAD \"\"" } } },
        { "plus2", { "Plus2-128BASIC", false, { "LOAD \"\"" } } },
        { "plus2a", { "Plus2A-3BASIC", false, { "LOAD \"t:\"", "LOAD \"\"" } } },
        { "plus3", { "Plus3-3BASIC", false, { "LOAD \"t:\"", "LOAD \"\"" } } },
        { "pentagon", { "Pentagon-TRDOS", true, { "RUN" } } },
        { "scorpion", { "Scorpion-TRDOS", true, { "RUN" } } },
        { "atm710", { "ATM710-TRDOS", true, { "RUN" } } },
        { "atm3", { "ATM3-TRDOS", true, { "RUN" } } },  // ERS set to 3.5 MHz, reset with SPACE held
        { "profi", { "Profi-TRDOS", true, { "RUN" } } },
        { "profscorp", { "ProfScorp-TRDOS", true, { "RUN" } } },
    };
    return machines;
}

/// "NAME equ #ABCD" lines
std::map<std::string, uint16_t> ReadSymbols(const std::string& path)
{
    std::map<std::string, uint16_t> symbols;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line))
    {
        std::istringstream in(line);
        std::string name, equ, value;
        if (in >> name >> equ >> value && equ == "equ" && value.size() > 1 && value[0] == '#')
            symbols[name] = static_cast<uint16_t>(std::stoul(value.substr(1), nullptr, 16));
    }
    return symbols;
}

void WriteText(const std::string& path, const std::string& text)
{
    std::ofstream(path) << text;
}
}  // namespace

class CoEmu_Test : public RomEditorFixture
{
};

TEST_F(CoEmu_Test, Run)
{
    const char* out = std::getenv("COEMU_OUT");
    const char* program = std::getenv("COEMU_PROGRAM");
    if (!out || !program)
        GTEST_SKIP() << "driven by tools/verification/coemu/unreal-ng/run.sh (COEMU_OUT, COEMU_PROGRAM)";
    const int maxFrames = std::getenv("COEMU_MAX_FRAMES") ? std::atoi(std::getenv("COEMU_MAX_FRAMES")) : 60000;
    const std::string outDir = out;
    const std::string base = program;

    const std::map<std::string, uint16_t> symbols = ReadSymbols(base + ".sym");
    ASSERT_TRUE(symbols.count("DONE") && symbols.count("START") && symbols.count("PROBEEND")) << base << ".sym";

    std::istringstream machineList(std::getenv("COEMU_MACHINES") ? std::getenv("COEMU_MACHINES") : "48k");
    std::string name;
    while (machineList >> name)
    {
        const std::string log = outDir + "/" + name + ".log";
        auto it = CoEmuMachines().find(name);
        if (it == CoEmuMachines().end())
        {
            WriteText(outDir + "/" + name + ".skip", "no such machine in unreal-ng\n");
            continue;
        }
        const CoEmuMachine& m = it->second;
        TearDown();  // a fresh emulator per machine
        // The machine as a user has it: the test runner leaves the sound slot empty, but every machine here
        // but the 48K has its AY on the board (a program may read #FFFD / #BFFD)
        std::optional<SoundCardScope> ay;
        if (name != "48k")
            ay.emplace(TestSound::TurboSound);
        BootEditor(m.editor);
        if (HasFatalFailure())
        {
            WriteText(log, "could not boot " + std::string(m.editor) + "\n");
            return;
        }
        _context->pFeatureManager->setFeature(Features::kFastTape, true);
        const bool loaded = m.disk ? _emulator->LoadDisk(base + ".trd") : _emulator->LoadTape(base + ".tap");
        if (!loaded)
        {
            WriteText(log, "could not load the program\n");
            continue;
        }
        CommandTyper* typer = _context->pDebugManager->GetCommandTyper();
        for (const std::string& command : m.commands)
        {
            typer->Request(command, CommandTyper::Options{});
            RunUntil([&] { return typer->GetStatus() == CommandTyper::Status::Done; }, 4000);
        }
        const uint16_t done = symbols.at("DONE");
        int frames = 0;
        const bool finished = RunUntil([&] { frames++; return _context->pMemory->DirectReadFromZ80Memory(done) == 1; },
                                       maxFrames);
        RunFrames(50);  // the closing lines
        std::ostringstream text;
        text << m.editor << (finished ? ": DONE after " : ": not done after ") << frames << " frames\n";
        WriteText(log, text.str());
        WriteText(outDir + "/" + name + ".screen.txt", Screen());
        if (!finished)
            continue;
        std::ofstream dump(outDir + "/" + name + ".bin", std::ios::binary);
        for (uint32_t a = symbols.at("START"); a < symbols.at("PROBEEND"); a++)
            dump.put(static_cast<char>(_context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(a))));
    }
}
